/*
** This file is part of the Matrix Brandy Basic VI Interpreter.
** Copyright (C) 2000-2014 David Daniels
** Copyright (C) 2018-2024 Michael McConnell and contributors
**
** Brandy is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2, or (at your option)
** any later version.
**
** Brandy is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with Brandy; see the file COPYING.  If not, write to
** the Free Software Foundation, 59 Temple Place - Suite 330,
** Boston, MA 02111-1307, USA.
**
**
**      This module contains functions for dealing with errors.
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <stdarg.h>
#include "common.h"
#include "target.h"
#include "basicdefs.h"
#include "errors.h"
#include "stack.h"
#include "fileio.h"
#include "tokens.h"
#include "screen.h"
#include "evaluate.h"
#include "miscprocs.h"
#include "keyboard.h"
#include "net.h"
#ifdef USE_SDL
#include "graphsdl.h"
#endif

#if defined(TARGET_MINGW)
#include <windows.h>
#endif

/*
** Error handling
** --------------
** The way in which the interpreter deals with any error is to call
** 'error' and then either branch back to the start of the interpreter's
** command loop using 'siglongjmp' or to execute the code defined on a
** 'ON ERROR' statement (again using 'siglongjmp' to jump back into the
** interpreter). A number of signal handlers are also set up to trap
** errors such as the 'escape' key being pressed or out-of-range
** addresses. Note that the use of 'SIGINT' to trap 'escape' being
** pressed means that 'escape' is handled asynchronously and so there
** could be problems if this happens, say, when allocating memory
** instead of only when interpreting the Basic program.
** There is a command line option to stop the program setting up the
** signal handlers for debugging purposes (otherwise the interpreter
** traps exceptions that happen within its own code).
*/

#define COPYRIGHT "Matrix Brandy " BRANDY_MAJOR "." BRANDY_MINOR "." BRANDY_PATCHLEVEL " is free software;  you can redistribute it and/or modify\r\n" \
        "it under the  terms of the  GNU General Public License as published by the Free\r\n" \
        "Software  Foundation.   See  the  file  COPYING for further details.\r\n"

#define MAXCALLDEPTH 10      /* Maximum no. of entries printed in PROC/FN traceback */

typedef enum {INFO, WARNING, NONFATAL, FATAL} errortype;

typedef enum {NOPARM, INTEGER, INTSTR, STRING, BSTRING} errorparm;

typedef struct {
  errortype severity;           /* Severity of error */
  errorparm parmtype;           /* Type of parameters error message takes */
  int32 equiverror;             /* Equivalent Basic V/VI error number for ERR */
  char *msgtext;                /* Pointer to text of message */
} detail;

typedef void handler(int);

#ifdef TARGET_DJGPP
#define ESCKEY 0x01             /* Scan code for 'Esc' key */
static int sigintkey;           /* Scan code of old key used for Escape */
#endif

#ifdef TARGET_MINGW
static HANDLE sigintthread = NULL;     /* Thread number for Escape key watching */
#endif

static char errortext[200];     /* Copy of text of last error for REPORT */

/*
** 'handle_signal' deals with any signals raised during program execution.
** Under some operating systems raising a signal causes the signal handler
** to be set back to its default value so this code reinstates handlers as
** well as dealing with the signal
*/
static void handle_signal(int signo) {
  DEBUGFUNCMSGIN;
  switch (signo) {
#ifndef TARGET_MINGW
  case SIGUSR1:
    DEBUGFUNCMSGOUT;
    return;
  case SIGUSR2:
    DEBUGFUNCMSGOUT;
    return;
#ifdef SIGPIPE
  case SIGPIPE:
    DEBUGFUNCMSGOUT;
    return;
#endif
#endif
  case SIGINT:
#ifndef TARGET_RISCOS
    if (kbd_esctest())
#endif
      basicvars.escape = TRUE;
#if defined(TARGET_MINGW) || defined(TARGET_RISCOS)
    (void) signal(SIGINT, handle_signal);
#endif
    DEBUGFUNCMSGOUT;
    return;
  case SIGFPE:
#ifdef TARGET_MINGW
    (void) signal(SIGFPE, handle_signal);
#endif
    DEBUGFUNCMSGOUT;
    error(ERR_ARITHMETIC);
  case SIGSEGV:
#ifdef TARGET_MINGW
    (void) signal(SIGSEGV, handle_signal);
#endif
    DEBUGFUNCMSGOUT;
    error(ERR_ADDREXCEPT);
  case SIGABRT:
#ifdef TARGET_MINGW
    (void) signal(SIGABRT, handle_signal);
#endif
    DEBUGFUNCMSGOUT;
    error(ERR_ADDREXCEPT);
#if defined(TARGET_UNIX) | defined(TARGET_MACOSX)
  case SIGCONT:
#ifdef TARGET_MINGW
    (void) signal(SIGCONT, handle_signal);
#endif
//    kbd_init();               // shouldn't be re-init'ing kbd in middle of a signal
    return;
#endif
  default:
    error(ERR_UNKNOWN, signo);
  }
  DEBUGFUNCMSGOUT;
}

#ifdef TARGET_MINGW
/*
** 'watch_escape' runs as a thread and polls the escape key every centisecond
** since the escape key doesn't produce a SIGINT in itself
*/
static DWORD watch_escape(LPVOID unused) {
  boolean alreadyraised = FALSE;

  while (1) {
    if (kbd_escpoll()) {
      if (!alreadyraised && (GetForegroundWindow() == GetConsoleWindow())) {
        alreadyraised = TRUE;
        raise(SIGINT);
      }
    }
    else
      alreadyraised = FALSE;
    Sleep(5);
  }
  return 0; /* Execution never reaches here, but keeps compiler quiet */
}

/*
** 'watch_signals' just gives an opportunity for any pending signals to be
** picked up during idle time
*/
void watch_signals(void) {
  Sleep(10);
}
#else
void watch_signals(void) {
}
#endif

/*
** 'init_errors' is called to set up handlers for various error conditions.
** This step can be skipped for debugging purposes by setting 'opt_traps'
** to 'false'
*/
void init_errors(void) {
  DEBUGFUNCMSGIN;
  errortext[0] = asc_NUL;
  if (basicvars.misc_flags.trapexcp) {  /* Want program to trap exceptions */
#if defined(TARGET_MINGW) || defined(TARGET_DJGPP) || defined(__TARGET_SCL__)
#ifndef TARGET_MINGW
    (void) signal(SIGUSR1, handle_signal);
    (void) signal(SIGUSR2, handle_signal);
#ifdef SIGTTIN
    (void) signal(SIGTTIN, SIG_IGN);
#endif
#ifdef SIGTTOU
    (void) signal(SIGTTOU, SIG_IGN);
#endif
#ifdef SIGPIPE
    (void) signal(SIGPIPE, handle_signal);
#endif
#endif
    (void) signal(SIGFPE, handle_signal);
    (void) signal(SIGSEGV, handle_signal);
    (void) signal(SIGABRT, handle_signal);
    (void) signal(SIGINT, handle_signal);
#ifdef TARGET_DJGPP
    sigintkey = __djgpp_set_sigint_key(ESCKEY);
#endif

#ifdef TARGET_MINGW
    /* Launch a thread to poll the escape key to emulate asynchronous SIGINTs */
    if (sigintthread == 0)
      sigintthread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&watch_escape, NULL, 0, NULL);
#endif

#else /* not TARGET_MINGW | TARGET_DJGPP */
    struct sigaction sa;

    (void) memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sa.sa_flags=SA_RESTART;
    (void) sigemptyset(&sa.sa_mask);

    (void) sigaction(SIGUSR1, &sa, NULL);
    (void) sigaction(SIGUSR2, &sa, NULL);
#ifndef BODGEDJP
    (void) sigaction(SIGTTIN, &sa, NULL);
    (void) sigaction(SIGTTOU, &sa, NULL);
#endif
    (void) sigaction(SIGPIPE, &sa, NULL);
    (void) sigaction(SIGFPE, &sa, NULL);
    (void) sigaction(SIGSEGV, &sa, NULL);
    (void) sigaction(SIGABRT, &sa, NULL);
    (void) sigaction(SIGINT, &sa, NULL);
#if defined(TARGET_UNIX) | defined(TARGET_MACOSX)
    (void) sigaction(SIGCONT, &sa, NULL);
#endif

#endif /* TARGET_MINGW | TARGET_DJGPP */
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'restore_handlers' restores the signal handlers to their default
** values. This is probably not needed but it is best to be on the
** safe side
*/
void restore_handlers(void) {
  DEBUGFUNCMSGIN;
  if (basicvars.misc_flags.trapexcp) {
    (void) signal(SIGFPE, SIG_DFL);
    (void) signal(SIGSEGV, SIG_DFL);
    (void) signal(SIGABRT, SIG_DFL);
    (void) signal(SIGINT, SIG_DFL);
#if defined(TARGET_UNIX) | defined(TARGET_MACOSX)
    (void) signal(SIGCONT, SIG_DFL);
#endif
#ifdef TARGET_DJGPP
    (void) __djgpp_set_sigint_key(sigintkey);
#endif
#ifdef TARGET_MINGW
    if (sigintthread != NULL) TerminateThread(sigintthread, 0);
#endif
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'announce' prints out the start messages for the interpreter
*/
void announce(void) {
  DEBUGFUNCMSGIN;
#ifdef BRANDY_BANNER_MINIMAL
  emulate_printf("\nMatrix Brandy ");
  if ((basicvars.himem-basicvars.page) > (98304*1024))
    emulate_printf("%uM", (uint32)(basicvars.himem-basicvars.page)>>20);
  else
    emulate_printf("%uK", (uint32)(basicvars.himem-basicvars.page)>>10);
#ifdef USE_SDL
  emulate_printf(" \x99");
#endif /* USE_SDL */
  emulate_printf("\r\n\nBASIC\r\n\n");
#else
  emulate_printf("\n%s\r\n\nStarting with " FMT_SZD " bytes free\r\n\n", IDSTRING, basicvars.himem-basicvars.page);
#endif /* BRANDY_BANNER_MINIMAL */
#ifndef NONET
  if (matrixflags.checknewver) {
    if (checkfornewer()) emulate_printf("A newer version is available!\r\n\n");
  }
#endif /* NONET */
#ifdef DEBUG
  emulate_printf("Basicvars is at &" FMT_SZX ", tokenised line is at &" FMT_SZX "\r\nWorkspace is at &" FMT_SZX ", size is " FMT_SZX "\r\nPAGE = &" FMT_SZX ", HIMEM = &" FMT_SZX "\r\n",
   &basicvars, &thisline, basicvars.workspace, basicvars.worksize, basicvars.page, basicvars.himem);
#endif /*DEBUG*/
  DEBUGFUNCMSGOUT;
}

/*
** 'show_options' prints some information on the program and the listing
** and debugging options in effect
*/
void show_options(int32 showextra) {
  DEBUGFUNCMSGIN;
#ifdef BRANDY_GITCOMMIT
  emulate_printf("%s\r\n  Git commit %s on branch %s (%s)\r\n\n", IDSTRING, BRANDY_GITCOMMIT, BRANDY_GITBRANCH, BRANDY_GITDATE);
#else
  emulate_printf("%s\r\n\n", IDSTRING);
#endif
  if (basicvars.program[0] != asc_NUL) emulate_printf("Program name: %s\r\n\n", basicvars.program);
  if (basicvars.loadpath != NIL) emulate_printf("Directory search list for libraries: %s\r\n\n", basicvars.loadpath);
  emulate_printf("The program starts at &%X and is %d bytes long.\r\nVariables start at &%X and occupy %d bytes. %d bytes of memory remain\r\n",
   basicvars.page, basicvars.top - basicvars.page,
   basicvars.lomem, basicvars.vartop - basicvars.lomem,
   basicvars.himem - basicvars.vartop);
  if (showextra) {
    emulate_printf("\r\nLISTO options in effect:\r\n");
    emulate_printf("  Indent statements:                %s\r\n", basicvars.list_flags.indent ? "Yes" : "No");
    emulate_printf("  Do not show line number:          %s\r\n", basicvars.list_flags.noline ? "Yes" : "No");
    emulate_printf("  Insert space after line number:   %s\r\n", basicvars.list_flags.space ? "Yes" : "No");
    emulate_printf("  Split lines at colon:             %s\r\n", basicvars.list_flags.split ? "Yes" : "No");
    emulate_printf("  Show keywords in lower case:      %s\r\n", basicvars.list_flags.lower ? "Yes" : "No");
    emulate_printf("  Pause after showing 20 lines:     %s\r\n", basicvars.list_flags.showpage ? "Yes" : "No");
    emulate_printf("\nTRACE debugging options in effect:\r\n");
    emulate_printf("  Show numbers of lines executed:   %s\r\n", basicvars.traces.lines ? "Yes" : "No");
    emulate_printf("  Show PROCs and FNs entered/left:  %s\r\n", basicvars.traces.procs ? "Yes" : "No");
    emulate_printf("  Pause before each statement:      %s\r\n", basicvars.traces.pause ? "Yes" : "No");
    emulate_printf("  Show lines branched from/to:      %s\r\n", basicvars.traces.branches ? "Yes" : "No");
    emulate_printf("  Show PROC/FN call trace on error: %s\r\n\n", basicvars.traces.backtrace ? "Yes" : "No");
    if (basicvars.tracehandle != 0) emulate_printf("Trace output is being written to a file\r\n\n");
  }
  DEBUGFUNCMSGOUT;
}

void show_help(void) {
  DEBUGFUNCMSGIN;
  printf("%s\n\n%s\nThe command syntax is:\n\n", IDSTRING, COPYRIGHT);
#ifdef BRANDYAPP
  printf("    thisapp [<options>]\n\n");
#else
  printf("    brandy [<options>]\n\n");
#endif
  printf("where <options> is one or more of the following options:\n");
  printf("  -help          Print this message\n");
  printf("  -version       Print version\n");
  printf("  -size <size>   Set Basic workspace size to <size> bytes when starting\n");
  printf("                 Suffix with K, M or G to specify size in KiB, MiB or GiB.\n");
#ifdef USE_SDL
  printf("  -fullscreen    Start Brandy in fullscreen mode\n");
  printf("  -nofull        Never use fullscreen mode\n");
  printf("  -swsurface     Use a software SDL surface\n");
#endif
#if !defined(TARGET_RISCOS) && !defined(TARGET_MINGW) && !defined(USE_SDL)
  printf("  -tek           Enable Tek graphics (on compatible builds)\n");
#endif
#ifndef BRANDYAPP
  printf("  -nocheck       Skip new version check on immediate mode startup\n");
  printf("  -path <list>   Look for programs and libraries in directories in list <list>\n");
  printf("  -load <file>   Load Basic program <file> when the interpreter starts\n");
  printf("  -chain <file>  Run Basic program <file> and stay in interpreter when it ends\n");
  printf("  -quit <file>   Run Basic program <file> and leave interpreter when it ends\n");
  printf("  -lib <file>    Load the Basic library <file> when the interpreter starts\n");
#ifdef DEFAULT_IGNORE
  printf("  -strict        'Unsupported features' generate errors\n");
#else
  printf("  -ignore        Ignore 'unsupported feature' where possible\n");
#endif
#ifndef TARGET_RISCOS
  printf("  -nostar        Do not check OSCLI for internal *-commands, instead pass all\n");
  printf("                 commands to the underlying operating system.\n");
#endif
  printf("  --             Subsequent options are passed to Basic program\n");
  printf("  <file>         Run Basic program <file> and leave interpreter when it ends\n\n");
#ifdef HAVE_ZLIB_H
  printf("Basic program files may be gzipped.\n\n");
#endif
#endif /* BRANDYAPP */
  DEBUGFUNCMSGOUT;
}

static detail badcmdtable [] = {
  {WARNING, NOPARM, 0, ""},
  {WARNING, STRING, 0, "No filename was supplied after option '%s'\n"},
  {WARNING, STRING, 0, "Basic workspace size is missing after option '%s'\n"},
  {WARNING, NOPARM, 0, "The name of the file to load has already been supplied\n"},
  {WARNING, NOPARM, 0, "There is not enough memory available to run the interpreter\n"},
  {WARNING, NOPARM, 0, "Initialisation of the interpreter failed\n"}
};

/*
** 'cmderror' is called to report errors before the interpreter
** has been initialised. The calling function has to deal with the
** error itself
*/
void cmderror(int32 errnumber, ...) {
  va_list parms;

  DEBUGFUNCMSGIN;
  va_start(parms, errnumber);
  switch (badcmdtable[errnumber].parmtype) {
  case INTEGER:
    printf(badcmdtable[errnumber].msgtext, va_arg(parms, int32));
    break;
  case STRING:
    printf(badcmdtable[errnumber].msgtext, va_arg(parms, char *));
    break;
  case NOPARM:
    fputs(badcmdtable[errnumber].msgtext, stdout);
    break;
  default:
    break;
  }
  va_end(parms);
  DEBUGFUNCMSGOUT;
}

/*
** 'errortable' gives the texts of all the error messages. It must
** be kept in step with the error numbers in error.h
** The third field is the value to be returned for the error when the
** Basic function 'ERR' is used. Some of these are not really appropriate
** as some of the errors flagged by this interpreter differ from
** those produced by Acorn's interpreter, for example, the ones
** concerned with file I/O that would be reported by RISCOS rather than
** the Basic interpreter
*/
static detail errortable [] = {
/* ERR_NONE*/           {INFO,     NOPARM,   0, "No error"},
/* ERR_UNSUPPORTED */   {FATAL,    NOPARM,   0, "Unsupported Basic V/VI feature found"},
/* ERR_UNSUPSTATE */    {FATAL,    NOPARM,   0, "Unsupported Basic V/VI statement type found"},
/* ERR_NOGRAPHICS */    {FATAL,    NOPARM,   0, "This version of the interpreter does not support graphics"},
/* ERR_NOVDUCMDS */     {FATAL,    NOPARM,   0, "VDU commands cannot be used as output is not to a screen"},
/* ERR_SYNTAX */        {NONFATAL, NOPARM,  16, "Syntax error"},
/* ERR_SILLY */         {NONFATAL, NOPARM,   0, "Silly!"},
/* ERR_BADPROG */       {NONFATAL, NOPARM,   0, "Bad program"},
/* ERR_ESCAPE */        {NONFATAL, NOPARM,  17, "Escape"},
/* ERR_STOP */          {FATAL,    NOPARM,   0, "STOP"},
/* ERR_STATELEN */      {NONFATAL, NOPARM,   0, "Line is longer than 1024 characters"},
/* ERR_LINENO */        {NONFATAL, NOPARM,   0, "Line number is outside the range 0..65279"},
/* ERR_LINEMISS */      {NONFATAL, INTEGER, 41, "Cannot find line %d"},
/* ERR_VARMISS */       {NONFATAL, STRING,  26, "Cannot find variable '%s'"},
/* ERR_ARRAYMISS */     {NONFATAL, STRING,  14, "Cannot find array '%s)'"},
/* ERR_FNMISS */        {NONFATAL, STRING,  29, "Cannot find function 'FN%s'"},
/* ERR_PROCMISS */      {NONFATAL, STRING,  29, "Cannot find procedure 'PROC%s'"},
/* ERR_TOOMANY */       {NONFATAL, BSTRING, 31, "There are too many parameters in the call to '%s'"},
/* ERR_NOTENUFF */      {NONFATAL, BSTRING, 31, "There are not enough parameters in the call to '%s'"},
/* ERR_BADDIM */        {NONFATAL, STRING,  11, "There is not enough memory to create array '%s)'"},
/* ERR_BADBYTEDIM */    {NONFATAL, STRING,  11, "There is not enough memory to create a byte array"},
/* ERR_NEGDIM */        {NONFATAL, STRING,  10, "Dimension of array '%s)' is negative"},
/* ERR_NEGBYTEDIM */    {NONFATAL, STRING,  10, "Dimension of byte array '%s' is negative"},
/* ERR_DIMCOUNT */      {NONFATAL, STRING,  10, "Array '%s)' has too many dimensions"},
/* ERR_DUPLDIM */       {NONFATAL, STRING,  10, "Array '%s)' has already been created"},
/* ERR_BADINDEX */      {NONFATAL, INTSTR,  15, "Array index value of %d is out of range in reference to '%s)'"},
/* ERR_INDEXCO */       {NONFATAL, STRING,  15, "Number of array indexes in reference to '%s)' is wrong"},
/* ERR_DIMRANGE */      {NONFATAL, NOPARM,  15, "The dimension number in call to 'DIM()' is out of range"},
/* ERR_NODIMS */        {NONFATAL, STRING,  14, "The dimensions of array '%s)' have not been defined"},
/* ERR_ADDRESS */       {NONFATAL, NOPARM, 242, "Address is out of range"},
/* WARN_BADTOKEN */     {WARNING,  NOPARM,   0, "Value entered is not a legal token value"},
/* WARN_BADHEX */       {WARNING,  NOPARM,  28, "Warning: bad hexadecimal constant"},
/* WARN_BADBIN */       {WARNING,  NOPARM,  28, "Warning: bad binary constant"},
/* WARN_EXPOFLO */      {WARNING,  NOPARM,  20, "Warning: exponent is too large"},
/* ERR_NAMEMISS */      {NONFATAL, NOPARM,   0, "Variable name expected"},
/* ERR_EQMISS */        {NONFATAL, NOPARM,   4, "Mistake"},
/* ERR_COMISS */        {NONFATAL, NOPARM,  27, "Missing ','"},
/* ERR_LPMISS */        {NONFATAL, NOPARM,  27, "Missing '('"},
/* ERR_RPMISS */        {NONFATAL, NOPARM,  27, "Missing ')'"},
/* WARN_QUOTEMISS */    {WARNING,  NOPARM,   9, "Warning: missing '\"'"},
/* ERR_QUOTEMISS */     {NONFATAL, NOPARM,   9, "Missing '\"'"},
/* ERR_HASHMISS */      {NONFATAL, NOPARM,  45, "Missing '#'"},
/* ERR_ENDIF */         {NONFATAL, NOPARM,  49, "Cannot find matching 'ENDIF' for this 'IF' or 'ELSE'"},
/* ERR_ENDWHILE */      {NONFATAL, NOPARM,  49, "Cannot find 'ENDWHILE' matching this 'WHILE'"},
/* ERR_ENDCASE */       {NONFATAL, NOPARM,  47, "Cannot find 'ENDCASE'"},
/* ERR_UNTIL */         {NONFATAL, NOPARM,  43, "Cannot find 'UNTIL' matching this 'REPEAT'"},
/* ERR_NEXT */          {NONFATAL, NOPARM,  32, "Cannot find 'NEXT' matching this 'FOR'"},
/* ERR_MULTINEXT */     {WARNING,  NOPARM,   0, "'EXIT FOR' encountered an incompatible 'NEXT' statement"},
/* ERR_EXITFOR */       {NONFATAL, NOPARM,  32, "'EXIT FOR' statement"},
/* ERR_OFMISS */        {NONFATAL, NOPARM,  48, "'OF' missing"},
/* ERR_TOMISS */        {NONFATAL, NOPARM,  36, "'TO' missing"},
/* ERR_CORPNEXT */      {NONFATAL, NOPARM,  27, "',' or ')' expected"},
/* ERR_NOTWHILE */      {NONFATAL, NOPARM,  46, "Not in a 'WHILE' loop"},
/* ERR_NOTREPEAT */     {NONFATAL, NOPARM,  43, "Not in a 'REPEAT' loop"},
/* ERR_NOTFOR */        {NONFATAL, NOPARM,  32, "Not in a 'FOR' loop"},
/* ERR_DIVZERO */       {NONFATAL, NOPARM,  18, "Division by zero"},
/* ERR_NEGROOT */       {NONFATAL, NOPARM,  21, "Tried to take square root of a negative number"},
/* ERR_LOGRANGE */      {NONFATAL, NOPARM,  22, "Tried to take log of zero or a negative number"},
/* ERR_RANGE */         {NONFATAL, NOPARM,  20, "Number is out of range"},
/* ERR_ONRANGE */       {NONFATAL, INTEGER, 40, "'ON' statement index value of %d is out of range"},
/* ERR_ARITHMETIC */    {NONFATAL, NOPARM,  20, "Floating point exception"},
/* ERR_PRECISION */     {WARNING,  NOPARM,   0, "Warning: Precision lost in floating-point conversion"},
/* ERR_STRINGLEN */     {NONFATAL, NOPARM,  19, "Character string is too long"},
/* ERR_BADOPER */       {NONFATAL, NOPARM,   0, "Unrecognisable operand"},
/* ERR_TYPENUM */       {NONFATAL, NOPARM,   6, "Type mismatch: number wanted"},
/* ERR_TYPESTR */       {NONFATAL, NOPARM,   6, "Type mismatch: string wanted"},
/* ERR_PARMNUM */       {NONFATAL, INTEGER,  6, "Type mismatch: number wanted for PROC/FN parameter no. %d"},
/* ERR_PARMSTR */       {NONFATAL, INTEGER,  6, "Type mismatch: string wanted for PROC/FN parameter no. %d"},
/* ERR_VARNUM */        {NONFATAL, NOPARM,   6, "Type mismatch: numeric variable wanted"},
/* ERR_VARNUMSTR */     {NONFATAL, NOPARM,   6, "Type mismatch: number or string wanted"},
/* ERR_VARARRAY */      {NONFATAL, NOPARM,   6, "Type mismatch: array wanted"},
/* ERR_OFFHEAPARRAY */  {NONFATAL, NOPARM,   6, "Type mismatch: off-heap array wanted"},
/* ERR_INTARRAY */      {NONFATAL, NOPARM,   6, "Type mismatch: integer array wanted"},
/* ERR_FPARRAY */       {NONFATAL, NOPARM,   6, "Type mismatch: floating point array wanted"},
/* ERR_STRARRAY */      {NONFATAL, NOPARM,   6, "Type mismatch: string array wanted"},
/* ERR_NUMARRAY */      {NONFATAL, NOPARM,   6, "Type mismatch: numeric array wanted"},
/* ERR_NOTONEDIM */     {NONFATAL, NOPARM,   6, "Type mismatch: array must have only one dimension"},
/* ERR_TYPEARRAY */     {NONFATAL, NOPARM,   6, "Type mismatch: arrays must have the same dimensions"},
/* ERR_MATARRAY */      {NONFATAL, NOPARM,   6, "Type mismatch: cannot perform matrix multiplication on these arrays"},
/* ERR_NOSWAP */        {NONFATAL, NOPARM,   6, "Type mismatch: cannot swap variables or arrays of different types"},
/* ERR_UNSUITABLEVAR */ {NONFATAL, NOPARM,   6, "Type mismatch: unsuitable variable type for operation"},
/* ERR_BADARITH */      {NONFATAL, NOPARM,   6, "Arithmetic operations cannot be performed on these operands"},
/* ERR_BADEXPR */       {NONFATAL, NOPARM,  16, "Syntax error in expression"},
/* ERR_RETURN */        {NONFATAL, NOPARM,  38, "RETURN encountered outside a subroutine"},
/* ERR_NOTAPROC */      {NONFATAL, NOPARM,  30, "Functions cannot be used as PROCs"},
/* ERR_NOTAFN */        {NONFATAL, NOPARM,  30, "PROCs cannot be used as functions"},
/* ERR_ENDPROC */       {NONFATAL, NOPARM,  13, "ENDPROC encountered outside a PROC"},
/* ERR_FNRETURN */      {NONFATAL, NOPARM,   7, "'=' (function return) encountered outside a function"},
/* ERR_LOCAL */         {NONFATAL, NOPARM,  12, "LOCAL found outside a PROC or FN"},
/* ERR_DATA */          {NONFATAL, NOPARM,  42, "There are no more 'DATA' statements to read"},
/* ERR_NOROOM */        {FATAL,    NOPARM,   0, "The interpreter has run out of memory"},
/* ERR_WHENCOUNT */     {NONFATAL, NOPARM,  47, "'CASE' statement has too many 'WHEN' clauses"},
/* ERR_SYSCOUNT */      {NONFATAL, NOPARM,  51, "'SYS' statement has too many parameters"},
/* ERR_STACKFULL */     {FATAL,    NOPARM,   0, "Arithmetic stack overflow"},
/* ERR_OPSTACK */       {FATAL,    NOPARM,   0, "Expression is too complex to evaluate"},
/* WARN_BADHIMEM */     {WARNING,  NOPARM,   0, "Value of HIMEM must be in the range END to end of the Basic workspace"},
/* WARN_BADLOMEM */     {WARNING,  NOPARM,   0, "Value of LOMEM must be in the range TOP to end of the Basic workspace"},
/* WARN_BADPAGE */      {WARNING,  NOPARM,   0, "Value of PAGE must lie in the Basic workspace"},
/* ERR_LOMEMFIXED */    {NONFATAL, NOPARM,   0, "LOMEM cannot be changed in a PROC or FN"}, // need to check what uses this
/* ERR_HIMEMFIXED */    {NONFATAL, NOPARM,   0, "HIMEM cannot be changed in a PROC, FN or any other program structure"},
/* ERR_BADTRACE */      {NONFATAL, NOPARM,   0, "Invalid option found after 'TRACE'"},
/* ERR_ERRNOTOP */      {NONFATAL, NOPARM,  54, "'RESTORE ERROR' information is not the top item on the Basic stack"},
/* ERR_DATANOTOP */     {NONFATAL, NOPARM,  54, "'RESTORE DATA' information is not the top item on the Basic stack"},
/* ERR_BADMODESC */     {NONFATAL, NOPARM,  25, "Screen mode descriptor is invalid"},
/* ERR_BADMODE */       {NONFATAL, NOPARM,  25, "Screen mode is not available"},
/* WARN_LIBLOADED */    {WARNING,  STRING,   0, "Library '%s' has already been loaded. Command ignored"},
/* ERR_NOLIB */         {NONFATAL, STRING, 214, "Cannot find library '%s'"},
/* ERR_LIBSIZE */       {FATAL,    STRING,   0, "There is not enough memory to load library '%s'"},
/* ERR_NOLIBLOC */      {NONFATAL, NOPARM,   0, "'LIBRARY LOCAL' can only be used at the start of a library"},
/* ERR_FILENAME */      {NONFATAL, NOPARM,   0, "File name missing"},
// Filing system errors:
/* ERR_NOTFOUND */      {NONFATAL, STRING, 214, "Cannot find file '%s'"},
/* ERR_OPENWRITE */     {NONFATAL, STRING, 193, "Cannot open file '%s' for output"},
/* ERR_OPENIN */        {NONFATAL, NOPARM, 193, "Cannot write to file as it has been opened for input only"},
/* ERR_CANTREAD */      {NONFATAL, NOPARM, 189, "Unable to read from file"},
/* ERR_CANTWRITE */     {NONFATAL, NOPARM, 193, "Unable to write to file"},
/* ERR_HITEOF */        {NONFATAL, NOPARM, 223, "Have reached end of file"},
/* ERR_READFAIL */      {NONFATAL, STRING, 189, "Could not read file '%s'"},
/* ERR_NOTCREATED */    {NONFATAL, STRING, 192, "Could not create file '%s'"},
/* ERR_WRITEFAIL */     {NONFATAL, STRING, 202, "Could not finish writing to file '%s'"},
#ifdef TARGET_RISCOS
/* ERR_FILEIO */        {FATAL,    STRING,   0, "%s"},
/* ERR_CMDFAIL */       {NONFATAL, STRING,   254, "%s"},
#else
/* ERR_FILEIO */        {FATAL,    STRING,   0, "Hit problem with file '%s'"},
/* ERR_CMDFAIL */       {NONFATAL, NOPARM,   254, "OS command failed"},
#endif
/* ERR_UNKNOWN */       {FATAL,    INTEGER,  244, "Unexpected signal (&%x) received"},
/* ERR_BADHANDLE */     {NONFATAL, NOPARM, 222, "Handle is invalid or file associated with it has been closed"},
/* ERR_SETPTRFAIL */    {FATAL,    NOPARM,   0, "The file pointer cannot be changed"},
/* ERR_GETPTRFAIL */    {FATAL,    NOPARM,   0, "The file pointer's value cannot be found"},
/* ERR_GETEXTFAIL */    {FATAL,    NOPARM,   0, "The size of the file cannot be found"},
/* ERR_MAXHANDLE */     {NONFATAL, NOPARM, 192, "The maximum allowed number of files is already open"},
/* ERR_INVALIDFNAME */  {NONFATAL, NOPARM, 253, "Invalid file name"},
/* ERR_NOMEMORY */      {FATAL,    NOPARM,   0, "Amount of memory requested exceeds what is available"},
//
/* ERR_BROKEN */        {FATAL,    INTSTR,   0, "The interpreter has gone wrong at line %d in %s"},
/* ERR_COMMAND */       {FATAL,    NOPARM,   0, "This Basic command cannot be used in a running program"},
/* ERR_RENUMBER */      {FATAL,    NOPARM,   0, "Line number went outside the range 0..65279 when renumbering program"},
/* WARN_LINENO */       {WARNING,  NOPARM,   0, "Warning: line number is outside the range 0..65279"},
/* WARN_LINEMISS */     {WARNING,  INTEGER,  0, "Warning: could not find line %d when renumbering program"},
/* WARN_RPMISS */       {WARNING,  NOPARM,   0, "Warning: number of '(' in line exceeds the number of ')'"},
/* WARN_RPAREN */       {WARNING,  NOPARM,   0, "Warning: number of '(' in line is less than the number of ')'"},
/* WARN_PARNEST */      {WARNING,  NOPARM,   0, "Warning: '(' and ')' are nested incorrectly"},
//
/* ERR_EDITFAIL */      {FATAL,    STRING,   0, "Edit session failed (%s)"},
/* ERR_OSCLIFAIL */     {NONFATAL, STRING, 254, "OSCLI failed (%s)"},
/* ERR_NOGZIP */        {FATAL,    NOPARM,   0, "This build of the interpreter does not support gzipped programs"},
/* WARN_FUNNYFLOAT */   {WARNING,  NOPARM,   0, "Warning: floating point number format is not known"},
/* ERR_SWINAMENOTKNOWN*/{NONFATAL, NOPARM, 486, "SWI name '%s' not known"},
/* ERR_SWINUMNOTKNOWN */{NONFATAL, INTEGER,486, "SWI &%llX not known"},
/* ERR_DIRNOTFOUND */   {NONFATAL, NOPARM, 214, "Directory not found or could not be selected"},
/* ERR_BADBITWISE */    {NONFATAL, NOPARM,   6, "Bitwise operations cannot be performed on these operands"},
/* ERR_ADDREXCEPT */    {NONFATAL, NOPARM, 243, "Address exception"},
/* ERR_PRINTER */       {NONFATAL, NOPARM, 509, "Unable to connect to printer"},
/* ERR_BADVARPROCNAME */{NONFATAL, NOPARM, 126, "Variable or procedure/function name too long"},
/* ERR_BADPROCFNNAME */ {NONFATAL, INTEGER, 127, "Procedure or function name found at line %d is too long"},
//
// OSCLI (command line) errors:
/* ERR_BADCOMMAND */    {NONFATAL, NOPARM, 254, "Bad command"},
/* ERR_BADSTRING */     {NONFATAL, NOPARM, 253, "Bad string"},
/* ERR_BADNUMBER */     {NONFATAL, NOPARM, 252, "Bad number"},
/* ERR_BADKEY */        {NONFATAL, NOPARM, 251, "Bad key"},
/* ERR_KEYINUSE */      {NONFATAL, NOPARM, 250, "Key in use"},
/* ERR_MOSVERSION */    {NONFATAL, NOPARM, 247, "Matrix Brandy MOS V" BRANDY_MAJOR "." BRANDY_MINOR "." BRANDY_PATCHLEVEL " (" BRANDY_DATE ")"},
/* ERR_BADSYNTAX */     {NONFATAL, STRING, 220, "Syntax: %s"},
//
// Network errors
/* ERR_NET_CONNREFUSED*/{NONFATAL, NOPARM, 165, "Connection refused"},                  // 'No reply'
/* ERR_NET_NOTFOUND */  {NONFATAL, NOPARM, 213, "Host not found"},                      // 'Disk not present'
/* ERR_NET_MAXSOCKETS */{NONFATAL, NOPARM, 192, "The maximum allowed number of sockets is already open"},
/* ERR_NET_NOTSUPP */   {NONFATAL, NOPARM, 157, "Network operation not supported"},     // 'Unsupported operation'
/* ERR_NO_RPI_GPIO */   {NONFATAL, NOPARM, 510, "Raspberry Pi GPIO not available"},
//
// Dynamic Linker errors
/* ERR_DL_NODL */       {FATAL,    NOPARM,   0, "Dynamic Linking not available"},
/* ERR_DL_NOSYM */      {FATAL,    STRING,   0, "%s"},
//
/* ERR_BAD_OSFILE */    {NONFATAL, NOPARM,1026, "Bad OSFile call"},
/* ERR_FILELOCKED */    {NONFATAL, NOPARM, 195, "This item is locked to stop changes being made to it"},
/* ERR_DIRNOTEMPTY */   {NONFATAL, NOPARM, 180, "Directory not empty"},
/* ERR_NODIR */         {NONFATAL, NOPARM, 189, "Unable to create directory"},
//
// DO NOT PUT ANYTHING BELOW THIS LINE - THIS MUST BE THE LAST ERROR
/* HIGHERROR */         {FATAL,    NOPARM,   0, "You should never see this"} /* ALWAYS leave this as the last error */
};

/*
** 'find_libname' finds the library into which the pointer 'p'
** points. It returns the name of the library or NIL if it points
** into the Basic program itself
*/
static char *find_libname(byte *p) {
  library *lp;

  DEBUGFUNCMSGIN;
  if (p>=basicvars.page && p<basicvars.top) {
    DEBUGFUNCMSGOUT;
    return NIL;
  }
  lp = find_library(p);
  DEBUGFUNCMSGOUT;
  return lp==NIL ? NIL : lp->libname;
}

static char *procfn(char *name) {
  DEBUGFUNCMSGIN;
  DEBUGFUNCMSGOUT;
  return *CAST(name, byte *)==BASTOKEN_PROC ? "PROC" : "FN";
}

/*
** 'print_details' prints an error message and a stack traceback
** if one has been requested. A backtrace is not produced if the
** error is just a warning, that is, iserror is FALSE
*/
static void print_details(boolean iserror) {
  fnprocinfo *p;

  DEBUGFUNCMSGIN;
  basicvars.printcount = 0;             /* Reset no. of chars Basic has printed on line to zero */
  if (basicvars.error_line==0) {        /* Error occured when dealing with the command line */
    if (basicvars.linecount==0) {
      emulate_printf("\r\n%s\r\n", errortext);
    } else {
      emulate_printf("[Line %d] %s\r\n", basicvars.linecount, errortext);
      basicvars.linecount = 0;          /* Don't need to preserve it */
    }
  }
  else {        /* Error occured in running program */
    char *libname;
    if (basicvars.procstack==NIL) {
      emulate_printf("\r\n%s at line %d", errortext, basicvars.error_line);
    } else {
      emulate_printf("\r\n%s at line %d in %s%s", errortext, basicvars.error_line,
       procfn(basicvars.procstack->fnprocname), basicvars.procstack->fnprocname+1);
    }
/* Note: see comments in save_current() in miscprocs.c about savedcur[0] */
    libname = find_libname(basicvars.current);
    if (libname==NIL && basicvars.curcount>0) libname = find_libname(basicvars.savedcur[0]);
    if (libname==NIL)
      emulate_printf("\r\n");
    else {
      emulate_printf(" in library '%s'\r\n", libname);
    }
    if (iserror && basicvars.traces.backtrace && basicvars.procstack!=NIL) {
/* Print a stack backtrace */
      int32 count = 0;
      p = basicvars.procstack;
      emulate_printf("PROC/FN call trace:\r\n");
      while (p!=NIL && count<MAXCALLDEPTH) {
        byte *lp = find_linestart(p->retaddr);
        if (lp!=NIL)    /* Line was in the program or a library */
          libname = find_libname(p->retaddr);
        else if (basicvars.curcount>0) {        /* In EVAL or READ */
          lp = find_linestart(basicvars.savedcur[0]);
          libname = find_libname(basicvars.savedcur[0]);
        }
        else {
          libname = NIL;
        }
        if (lp==NIL)
          emulate_printf("  %s%s was called from the command line",
           procfn(p->fnprocname), p->fnprocname+1);
        else {
          emulate_printf("  %s%s was called from line %d",
           procfn(p->fnprocname), p->fnprocname+1, GET_LINENO(lp));
        }
        p = p->lastcall;
        if (p==NIL)
          emulate_printf("\r\n");
        else {
          emulate_printf(" in %s%s", procfn(p->fnprocname), p->fnprocname+1);
          if (libname==NIL)
            emulate_printf("\r\n");
          else {
            emulate_printf(" in library '%s'\r\n", libname);
          }
        }
        count++;
      }
    }
  }
  DEBUGFUNCMSGOUT;
}

/*
**'handle_error' deals with the aftermath of an error, either calling the
** Basic program's error handling if one has been set up or printing the
** requisite error message and halting the program.
** There are two types of error handler, 'ordinary' and 'local'. 'Ordinary'
** error handlers clear the Basic stack completely before restarting at the
** statement after the 'ON ERROR'. 'Local' error handlers are more flexible
** in that they restore the stack to its state where the 'ON ERROR LOCAL'
** statement was found.
**
** NOTE: THIS INTERPRETER HANDLES ERRORS IN A COMPLETELY DIFFERENT
** WAY TO THE ACORN INTERPRETER IN THAT IT CLEANS UP THE STACK AFTER
** AN ERROR. THE ACORN INTERPRETER JUST BRANCHES TO THE ERROR HANDLER
** AND LEAVES THE STACK IN AN UNDEFINED STATE WITH LOCAL VARIABLES,
** LOCAL ARRAYS AND SO FORTH WITH THE WRONG VALUES. BRANDY RESTORES
** EVERYTHING TO THE STATE IT SHOULD HAVE AT THE ERROR HANDLER.
** BRANDY'S ERROR HANDLING IS MUCH CLOSER TO PROPER EXCEPTION HANDLING
**
** Note that the creative use of 'ON ERROR' can cause this code to go
** wrong. Placing 'ON ERROR LOCAL' within a loop, for example, can
** cause problems if an error is then encountered outside the loop.
** There is a sanity check in the code that makes sure that the
** Basic stack pointer is at or below the value it will be set to
** if the 'ON ERROR LOCAL' handler is triggered. If it meets this
** requirement then it is assumed that everything is okay and the
** error handler can be called safely. If it is above this point
** then the contents of the stack between these points is
** indeterminate and the error handler is not called as it is not
** safe. Of course, the fact that the value of the stack point is
** okay is no guarantee that the stack contents are legal, but the
** check should catch most cases. This is different to how the
** Acorn interpreter works in that it will always branch to the
** error handler.
*/
static void handle_error(errortype severity) {
  DEBUGFUNCMSGIN;
#ifdef DEBUG
  if (basicvars.debug_flags.debug) {
    fprintf(stderr, "Error in Basic program - %s at line %d\n", errortext, basicvars.error_line);
    fprintf(stderr, "At time of error: current = %p,  stack = %p,  opstop = %p\n",
     basicvars.current, basicvars.stacktop.bytesp, basicvars.opstop);
  }
#endif
  if (severity != FATAL && basicvars.error_handler.current != NIL &&
   basicvars.error_handler.stacktop>=basicvars.stacktop.bytesp) {
/* Error is recoverable and there is an usable error handler in the Basic program */
    reset_stack(basicvars.error_handler.stacktop);
#ifdef DEBUG
    if (basicvars.debug_flags.debug) {
      fprintf(stderr, "Invoking ON ERROR %s handler at %p,  stack = %p,  opstop = %p\n",
       basicvars.error_handler.islocal ? "LOCAL" : "",
       basicvars.error_handler.current, basicvars.error_handler.stacktop, basicvars.opstop);
    }
#endif
    if (basicvars.error_handler.islocal) {        /* Trapped via 'ON ERROR LOCAL' */
#ifdef DEBUG
      if (basicvars.debug_flags.debug) fprintf(stderr, "About to siglongjmp(*basicvars.local_restart,1), local_restart = %p\n", basicvars.local_restart);
#endif
      DEBUGFUNCMSGOUT;
      siglongjmp(*basicvars.local_restart, 1);
    } else {      /* Trapped via 'ON ERROR' - Reset everything and return to main interpreter loop */
      basicvars.procstack = NIL;
      basicvars.gosubstack = NIL;
      init_expressions();
#ifdef DEBUG
      if (basicvars.debug_flags.debug) fprintf(stderr, "About to siglongjmp(*basicvars.error_restart,1), localrestart = %p\n", basicvars.error_restart);
#endif
      DEBUGFUNCMSGOUT;
      siglongjmp(basicvars.error_restart, 1);      /* Branch back to the main interpreter loop */
    }
  }
  else {        /* Print error message and halt program */
    basicvars.runflags.running = FALSE;
    emulate_vdu(VDU_ENABLE);    /* Ensure VDU driver is enabled */
    emulate_vdu(VDU_TEXTCURS);  /* And that output goes to the text cursor */
    print_details(severity>WARNING);
    if (basicvars.runflags.closefiles) fileio_shutdown();
    if (basicvars.runflags.quitatend) {
      int errcode = (basicvars.error_number & 0xFF);
      exit_interpreter(errcode ? errcode : 255);   /* Leave interpreter if flag is set */
    }
    basicvars.current = NIL;
    basicvars.procstack = NIL;
    basicvars.gosubstack = NIL;
    basicvars.recdepth = 0;
    clear_stack();              /* Clear the stack on an unhandled error */
    DEBUGFUNCMSGOUT;
    siglongjmp(basicvars.restart, 1);  /* Error - branch to main interpreter loop */
  }
}

/*
** 'error' is the main error handling function. It prints the error message
** and then either stops the program or invokes the user-defined error
** handler in the Basic program.
**
** In most cases 'basicvars.current' points at the line in the Basic
** program at which the error occured. However when dealing with 'READ'
** and 'EVAL' it will point at the buffer containing the expression
** being evaluated. In this case the saved copy of 'current' will held
** in the array basicvars.savedcur[]. savedcur[0] will always be the
** the real pointer into the Basic program. 'curcount' gives the number
** of entries in savedcur[]. If it is greater than zero than something
** is held in it.
*/
void error(int32 errnumber, ...) {
  va_list parms;
  byte *badline;

/* This causes a segfault on an error, to stop gdb in its tracks.
** UGLY UGLY hack. Use with compilation option -g to allow a stack
** backtrace to see exactly where an error condition was raised. */
#ifdef BORKONERROR
  char *collapse = NULL; /* debug hack */

  *collapse="bork";
#endif

  DEBUGFUNCMSGIN;
#ifdef USE_SDL
  while (matrixflags.videothreadbusy) usleep(1000);
  hide_cursor();
#endif
  if (errnumber<1 || errnumber>HIGHERROR) {
    emulate_printf("Out of range error number %d\r\n", errnumber);
    errnumber = ERR_BROKEN;
  }

  if (errnumber == ERR_ESCAPE) kbd_escack();                            /* Acknowledge and process Escape effects */

#ifdef USE_SDL
  if (2 == get_refreshmode()) star_refresh(1);  /* Re-enable Refresh if stopped using *Refresh OnError */
#endif
  va_start(parms, errnumber);
  vsprintf(errortext, errortable[errnumber].msgtext, parms);
  va_end(parms);
  if (errortable[errnumber].equiverror != -1) basicvars.error_number = errortable[errnumber].equiverror;
  if (basicvars.current==NIL)           /* Not running a program */
    basicvars.error_line = 0;
  else {
    badline = find_linestart(basicvars.current);
    if (badline==NIL && basicvars.curcount>0) badline = find_linestart(basicvars.savedcur[0]);
    basicvars.curcount = 0; /* otherwise the stack will eventually overflow */
    if (badline==NIL)   /* Error did not occur in program - Assume it was in the command line */
      basicvars.error_line = 0;
    else {      /* Error occured in running program */
      basicvars.error_line = GET_LINENO(badline);
    }
  }
  if (errortable[errnumber].severity<=WARNING &&   /* Error message is just a warning */
    /* But handle as an error if running in Strict mode and program running */
    (!basicvars.runflags.flag_cosmetic || !basicvars.runflags.running))
    print_details(FALSE);       /* Print message with no backtrace */
  else {
    handle_error(errortable[errnumber].severity);
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'get_lasterror' is used to return the text of the last error message
*/
char *get_lasterror(void) {
  DEBUGFUNCMSGIN;
  if (errortext[0]==asc_NUL) {
    DEBUGFUNCMSGOUT;
    return COPYRIGHT;
  } else {
    DEBUGFUNCMSGOUT;
    return &errortext[0];
  }
}

/*
** 'show_error' is called to report a user-specified error, that is, it
** deals with the error raised via an 'ERROR' statement
*/
void show_error(int32 number, char *text) {
  byte *badline;
  errortype severity;

  DEBUGFUNCMSGIN;
  basicvars.error_number = number;
  severity = number==0 ? FATAL : NONFATAL;
  strcpy(errortext, text);
  badline = find_linestart(basicvars.current);
  if (badline==NIL)     /* 'ERROR' was not used in program - Assume it was in the command line */
    basicvars.error_line = 0;
  else {        /* ERROR used in running program */
    basicvars.error_line = GET_LINENO(badline);
  }
  handle_error(severity);
  DEBUGFUNCMSGOUT;
}

/*
** 'set_error' is called to set up a normal Basic error handler
*/
void set_error(void) {
  DEBUGFUNCMSGIN;
  basicvars.error_handler.current = basicvars.current;
  basicvars.error_handler.stacktop = basicvars.safestack.bytesp; /* get_safestack */
  basicvars.error_handler.islocal = FALSE;
#ifdef DEBUG
  if (basicvars.debug_flags.debug) fprintf(stderr, "Set up ON ERROR handler at %p,  stack = %p\n",
   basicvars.error_handler.current, basicvars.error_handler.stacktop);
#endif
  DEBUGFUNCMSGOUT;
}

/*
** 'set_local_error' is called to set up a 'local' Basic error handler
*/
void set_local_error(void) {
  DEBUGFUNCMSGIN;
  basicvars.error_handler.current = basicvars.current;
  basicvars.error_handler.stacktop = basicvars.stacktop.bytesp; /* get_stacktop */
  basicvars.error_handler.islocal = TRUE;
#ifdef DEBUG
  if (basicvars.debug_flags.debug) fprintf(stderr, "Set up ON ERROR LOCAL handler at %p,  stack = %p\n",
   basicvars.error_handler.current, basicvars.error_handler.stacktop);
#endif
  DEBUGFUNCMSGOUT;
}

/*
** 'clear_error' is called to clear any error handler set up by the
** Basic program
*/
void clear_error(void) {
  DEBUGFUNCMSGIN;
  basicvars.error_handler.current = NIL;
  basicvars.local_restart = NIL;
  basicvars.escape = FALSE;
#ifdef DEBUG
  if (basicvars.debug_flags.debug) fprintf(stderr, "Clearing ON ERROR handler\n");
#endif
  DEBUGFUNCMSGOUT;
}
