/*
** This file is part of the Brandy Basic V Interpreter.
** Copyright (C) 2000, 2001, 2002, 2003, 2004 David Daniels
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

#if defined(TARGET_MINGW)
#include <windows.h>
#endif

/*
** Error handling
** --------------
** The way in which the interpreter deals with any error is to call
** 'error' and then either branch back to the start of the interpreter's
** command loop using 'longjmp' or to execute the code defined on a
** 'ON ERROR' statement (again using 'longjmp' to jump back into the
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

#define COPYRIGHT "Brandy is free software; you can redistribute it and/or modify\r\n" \
        "it under the terms of the GNU General Public License as published by\r\n" \
        "the Free Software Foundation. See the file COPYING for further details.\r\n"

#define MAXCALLDEPTH 10      /* Maximum no. of entries printed in PROC/FN traceback */

typedef enum {INFO, WARNING, NONFATAL, FATAL} errortype;

typedef enum {NOPARM, INTEGER, INTSTR, STRING, BSTRING} errorparm;

typedef struct {
  errortype severity;           /* Severity of error */
  errorparm parmtype;           /* Type of parameters error message takes */
  int32 equiverror;             /* Equivalent Basic V error number for ERR */
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
  switch (signo) {
  case SIGINT:
    (void) signal(SIGINT, handle_signal);
    basicvars.escape = TRUE;
    return;
  case SIGFPE:
    (void) signal(SIGFPE, handle_signal);
    error(ERR_ARITHMETIC);
  case SIGSEGV:
    (void) signal(SIGSEGV, handle_signal);
    error(ERR_ADDRESS);
#if defined(TARGET_LINUX) | defined(TARGET_NETBSD) | defined(TARGET_MACOSX)\
 | defined(TARGET_FREEBSD) |defined(TARGET_OPENBSD) | defined(TARGET_GNUKFREEBSD)
  case SIGCONT:
    (void) signal(SIGCONT, handle_signal);
    init_keyboard();
    return;
#endif
  default:
    error(ERR_UNKNOWN, signo);
  }
}

#ifdef TARGET_MINGW
/*
** 'watch_escape' runs as a thread and polls the escape key every centisecond
** since the escape key doesn't produce a SIGINT in itself
*/
static DWORD watch_escape(LPVOID unused) {
  boolean       alreadyraised = FALSE;

  while (1) {
    if (GetAsyncKeyState(VK_ESCAPE) < 0)
    {
      if (!alreadyraised && (GetForegroundWindow() == GetConsoleWindow())) {
        raise(SIGINT);
        alreadyraised = TRUE;
      }
    }
    else
      alreadyraised = FALSE;
    Sleep(5);
  }
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
  errortext[0] = NUL;
  if (basicvars.misc_flags.trapexcp) {  /* Want program to trap exceptions */
    (void) signal(SIGFPE, handle_signal);
    (void) signal(SIGSEGV, handle_signal);
    (void) signal(SIGINT, handle_signal);
#if defined(TARGET_LINUX) | defined(TARGET_NETBSD) | defined(TARGET_MACOSX)\
 | defined(TARGET_FREEBSD) |defined(TARGET_OPENBSD) | defined(TARGET_GNUKFREEBSD)
    (void) signal(SIGCONT, handle_signal);
#endif
#ifdef TARGET_DJGPP
    sigintkey = __djgpp_set_sigint_key(ESCKEY);
#endif
#ifdef TARGET_MINGW
    /* Launch a thread to poll the escape key to emulate asynchronous SIGINTs */
    if (sigintthread == 0)
      sigintthread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&watch_escape, NULL, 0, NULL);
#endif
  }
}

/*
** 'restore_handlers' restores the signal handlers to their default
** values. This is probably not needed but it is best to be on the
** safe side
*/
void restore_handlers(void) {
  if (basicvars.misc_flags.trapexcp) {
    (void) signal(SIGFPE, SIG_DFL);
    (void) signal(SIGSEGV, SIG_DFL);
    (void) signal(SIGINT, SIG_DFL);
#if defined(TARGET_LINUX) | defined(TARGET_NETBSD) | defined(TARGET_MACOSX)\
 | defined(TARGET_FREEBSD) |defined(TARGET_OPENBSD) | defined(TARGET_GNUKFREEBSD)
    (void) signal(SIGCONT, SIG_DFL);
#endif
#ifdef TARGET_DJGPP
    (void) __djgpp_set_sigint_key(sigintkey);
#endif
#ifdef TARGET_MINGW
    if (sigintthread != NULL) TerminateThread(sigintthread, 0);
#endif
  }
}

/*
** 'announce' prints out the start messages for the interpreter
*/
void announce(void) {
  emulate_printf("\n%s\r\n\nStarting with %d bytes free\r\n\n", IDSTRING, basicvars.himem-basicvars.page);
#ifdef DEBUG
  emulate_printf("Basicvars is at &%p, tokenised line is at &%p\r\n", &basicvars, &thisline);
  emulate_printf("Workspace is at &%p, size is &%x, page = &%p\r\nhimem = &%p\r\n",
   basicvars.workspace, basicvars.worksize, basicvars.page, basicvars.himem);
#endif
}

/*
** 'show_options' prints some information on the program and the listing
** and debugging options in effect
*/
void show_options(void) {
  emulate_printf("%s\r\n\n", IDSTRING);
  if (basicvars.program[0] != NUL) emulate_printf("Program name: %s\r\n\n", basicvars.program);
  if (basicvars.loadpath != NIL) emulate_printf("Directory search list for libraries: %s\r\n\n", basicvars.loadpath);
  emulate_printf("The program starts at &%X and is %d bytes long.\r\nVariables start at &%X and occupy %d bytes. %d bytes of memory remain\r\n",
   basicvars.page - basicvars.offbase, basicvars.top - basicvars.page,
   basicvars.lomem - basicvars.offbase, basicvars.vartop - basicvars.lomem,
   basicvars.himem - basicvars.vartop);
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

void show_help(void) {
  printf("%s\n\n%s\nThe command syntax is:\n\n", IDSTRING, COPYRIGHT);
  printf("    brandy [<options>]\n\n");
  printf("where <options> is one or more of the following options:\n");
  printf("  -help          Print this message\n");
  printf("  -size <size>   Set Basic workspace size to <size> bytes when starting\n");
  printf("  -path <list>   Look for programs and libraries in directories in list <list>\n");
  printf("  -load <file>   Load Basic program <file> when the interpreter starts\n");
  printf("  -chain <file>  Run Basic program <file> and stay in interpreter when it ends\n");
  printf("  -quit <file>   Run Basic program <file> and leave interpreter when it ends\n");
  printf("  -lib <file>    Load the Basic library <file> when the interpreter starts\n");
  printf("  -ignore        Ignore 'unsupported feature' errors where possible\n");
  printf("  <file>         Run Basic program <file> and stay in interpreter when it ends\n\n");
#ifdef HAVE_ZLIB_H
  printf("Basic program files may be gzipped.\n\n");
#endif
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
  va_start(parms, errnumber);
  switch (badcmdtable[errnumber].parmtype) {
  case INTEGER:
    printf(badcmdtable[errnumber].msgtext, va_arg(parms, int32));
    break;
  case STRING:
    printf(badcmdtable[errnumber].msgtext, va_arg(parms, char *));
    break;
  case NOPARM:
    printf("%s", badcmdtable[errnumber].msgtext);
    break;
  default:
    break;
  }
  va_end(parms);
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
  {INFO,     NOPARM,   0, "No error"},
  {FATAL,    NOPARM,   0, "Unsupported Basic V feature found"},
  {FATAL,    NOPARM,   0, "Unsupported Basic V statement type found"},
  {FATAL,    NOPARM,   0, "This version of the interpreter does not support graphics"},
  {FATAL,    NOPARM,   0, "VDU commands cannot be used as output is not to a screen"},
  {NONFATAL, NOPARM,  16, "Syntax error"},
  {NONFATAL, NOPARM,   0, "Silly!"},
  {NONFATAL, NOPARM,   0, "Bad program"},
  {NONFATAL, NOPARM,  17, "Escape"},
  {FATAL,    NOPARM,   0, "STOP"},
  {NONFATAL, NOPARM,   0, "Line is longer than 1024 characters"},
  {NONFATAL, NOPARM,   0, "Line number is outside the range 0..65279"},
  {NONFATAL, INTEGER, 41, "Cannot find line %d"},
  {NONFATAL, STRING,  26, "Cannot find variable '%s'"},
  {NONFATAL, STRING,  14, "Cannot find array '%s)'"},
  {NONFATAL, STRING,  29, "Cannot find function 'FN%s'"},
  {NONFATAL, STRING,  29, "Cannot find procedure 'PROC%s'"},
  {NONFATAL, BSTRING, 31, "There are too many parameters in the call to '%s'"},
  {NONFATAL, BSTRING, 31, "There are not enough parameters in the call to '%s'"},
  {NONFATAL, INTEGER, 31, "Parameter no. %d is not a valid 'RETURN' parameter"},
  {NONFATAL, NOPARM,  31, "Call to built-in function has too many parameters"},
  {NONFATAL, NOPARM,  31, "Call to built-in function does not have enough parameters"},
  {FATAL,    NOPARM,   0, "Program execution has run into a PROC or FN"},
  {FATAL,    STRING,  10, "There is not enough memory to create array '%s)'"},
  {FATAL,    STRING,  10, "There is not enough memory to create a byte array"},
  {NONFATAL, STRING,  10, "Dimension of array '%s)' is negative"},
  {NONFATAL, STRING,  10, "Array '%s)' has too many dimensions"},
  {NONFATAL, STRING,  10, "Array '%s)' has already been created"},
  {NONFATAL, INTSTR,  15, "Array index value of %d is out of range in reference to '%s)'"},
  {NONFATAL, STRING,  15, "Number of array indexes in reference to '%s)' is wrong"},
  {NONFATAL, NOPARM,  15, "The dimension number in call to 'DIM()' is out of range"},
  {NONFATAL, STRING,  14, "The dimensions of array '%s)' have not been defined"},
  {NONFATAL, NOPARM,   0, "Address is out of range"},
  {WARNING,  NOPARM,   0, "Value entered is not a legal token value"},
  {WARNING,  NOPARM,  28, "Warning: bad hexadecimal constant"},
  {WARNING,  NOPARM,  28, "Warning: bad binary constant"},
  {WARNING,  NOPARM,  20, "Warning: exponent is too large"},
  {NONFATAL, NOPARM,   0, "Variable name expected"},
  {NONFATAL, NOPARM,   4,  "'=' missing or syntax error in statement has misled interpreter"},
  {NONFATAL, NOPARM,  27, "',' missing"},
  {NONFATAL, NOPARM,  27, "'(' missing"},
  {NONFATAL, NOPARM,  27, "')' missing"},
  {WARNING,  NOPARM,   9, "Warning: '\"' missing"},
  {NONFATAL, NOPARM,   9, "'\"' missing"},
  {NONFATAL, NOPARM,  45, "'#' missing"},
  {NONFATAL, NOPARM,  49, "Cannot find matching 'ENDIF' for this 'IF' or 'ELSE'"},
  {NONFATAL, NOPARM,  49, "Cannot find 'ENDWHILE' matching this 'WHILE'"},
  {NONFATAL, NOPARM,  47, "Cannot find 'ENDCASE'"},
  {NONFATAL, NOPARM,  48, "'OF' missing"},
  {NONFATAL, NOPARM,  36, "'TO' missing"},
  {NONFATAL, NOPARM,  27, "',' or ')' expected"},
  {NONFATAL, NOPARM,  46, "Not in a 'WHILE' loop"},
  {NONFATAL, NOPARM,  43, "Not in a 'REPEAT' loop"},
  {NONFATAL, NOPARM,  32, "Not in a 'FOR' loop"},
  {NONFATAL, NOPARM,  33, "Variable after 'NEXT' is not the control variable of the current 'FOR' loop"},
  {NONFATAL, NOPARM,  18, "Division by zero"},
  {NONFATAL, NOPARM,  21, "Tried to take square root of a negative number"},
  {NONFATAL, NOPARM,  22, "Tried to take log of zero or a negative number"},
  {NONFATAL, NOPARM,  20, "Number is out of range"},
  {NONFATAL, INTEGER, 40, "'ON' statement index value of %d is out of range"},
  {NONFATAL, NOPARM,  20, "Floating point exception"},
  {NONFATAL, NOPARM,  19, "Character string is too long"},
  {NONFATAL, NOPARM,   0, "Unrecognisable operand"},
  {NONFATAL, NOPARM,   6, "Type mismatch: number wanted"},
  {NONFATAL, NOPARM,   6, "Type mismatch: string wanted"},
  {NONFATAL, INTEGER,  6, "Type mismatch: number wanted for PROC/FN parameter no. %d"},
  {NONFATAL, INTEGER,  6, "Type mismatch: string wanted for PROC/FN parameter no. %d"},
  {NONFATAL, NOPARM,   6, "Type mismatch: numeric variable wanted"},
  {NONFATAL, NOPARM,   6, "Type mismatch: string variable wanted"},
  {NONFATAL, NOPARM,   6, "Type mismatch: number or string wanted"},
  {NONFATAL, NOPARM,   6, "Type mismatch: array wanted"},
  {NONFATAL, NOPARM,   6, "Type mismatch: integer array wanted"},
  {NONFATAL, NOPARM,   6, "Type mismatch: floating point array wanted"},
  {NONFATAL, NOPARM,   6, "Type mismatch: string array wanted"},
  {NONFATAL, NOPARM,   6, "Type mismatch: numeric array wanted"},
  {NONFATAL, NOPARM,   6, "Type mismatch: array must have only one dimension"},
  {NONFATAL, NOPARM,   6, "Type mismatch: arrays must have the same dimensions"},
  {NONFATAL, NOPARM,   6, "Type mismatch: cannot perform matrix multiplication on these arrays"},
  {NONFATAL, NOPARM,   0, "Type mismatch: cannot swap variables or arrays of different types"},
  {NONFATAL, NOPARM,   0, "Type mismatch: cannot compare these operands"},
  {NONFATAL, NOPARM,   0, "Arithmetic operations cannot be performed on these operands"},
  {NONFATAL, NOPARM,   0, "Syntax error in expression"},
  {NONFATAL, NOPARM,  38, "RETURN encountered outside a subroutine"},
  {NONFATAL, NOPARM,   0, "Functions cannot be used as PROCs"},
  {NONFATAL, NOPARM,   0, "PROCs cannot be used as functions"},
  {NONFATAL, NOPARM,  13, "ENDPROC encountered outside a PROC"},
  {NONFATAL, NOPARM,   7, "'=' (function return) encountered outside a function"},
  {NONFATAL, NOPARM,  12, "LOCAL found outside a PROC or FN"},
  {NONFATAL, NOPARM,   0, "There are no more 'DATA' statements to read"},
  {FATAL,    NOPARM,   0, "The interpreter has run out of memory"},
  {NONFATAL, NOPARM,   0, "'CASE' statement has too many 'WHEN' clauses"},
  {NONFATAL, NOPARM,   0, "'SYS' statement has too many parameters"},
  {FATAL,    NOPARM,   0, "Arithmetic stack overflow"},
  {FATAL,    NOPARM,   0, "Expression is too complex to evaluate"},
  {WARNING,  NOPARM,   0, "Value of HIMEM must be in the range END to end of the Basic workspace"},
  {WARNING,  NOPARM,   0, "Value of LOMEM must be in the range TOP to end of the Basic workspace"},
  {WARNING,  NOPARM,   0, "Value of PAGE must lie in the Basic workspace"},
  {NONFATAL, NOPARM,   0, "LOMEM cannot be changed in a PROC or FN"},
  {NONFATAL, NOPARM,   0, "HIMEM cannot be changed in a PROC, FN or any other program structure"},
  {NONFATAL, NOPARM,   0, "Invalid option found after 'TRACE'"},
  {NONFATAL, NOPARM,   0, "'RESTORE ERROR' information is not the top item on the Basic stack"},
  {NONFATAL, NOPARM,  42, "'RESTORE DATA' information is not the top item on the Basic stack"},
  {NONFATAL, NOPARM,   0, "'SPC()' or 'TAB()' found outside an 'INPUT' or 'PRINT' statement"},
  {NONFATAL, NOPARM,   0, "Screen mode descriptor is invalid"},
  {NONFATAL, NOPARM,   0, "Screen mode is not available"},
  {WARNING,  STRING,   0, "Library '%s' has already been loaded. Command ignored"},
  {NONFATAL, STRING,   0, "Cannot find library '%s'"},
  {FATAL,    STRING,   0, "There is not enough memory to load library '%s'"},
  {NONFATAL, NOPARM,   0, "'LIBRARY LOCAL' can only be used at the start of a library"},
  {NONFATAL, NOPARM,   0, "File name missing"},
  {FATAL,    STRING,   0, "Cannot find file '%s'"},
  {NONFATAL, STRING,   0, "Cannot open file '%s' for output"},
  {NONFATAL, STRING,   0, "Cannot open file '%s' for update"},
  {NONFATAL, NOPARM,   0, "Cannot write to file as it has been opened for input only"},
  {NONFATAL, NOPARM,   0, "Unable to read from file"},
  {NONFATAL, NOPARM,   0, "Unable to write to file"},
  {NONFATAL, NOPARM,   0, "Have reached end of file"},
  {FATAL,    STRING,   0, "Could not read file '%s'"},
  {FATAL,    STRING,   0, "Could not create file '%s'"},
  {FATAL,    STRING,   0, "Could not finish writing to file '%s'"},
  {FATAL,    STRING,   0, "Basic program file '%s' is empty"},
#ifdef TARGET_RISCOS
  {FATAL,    STRING,   0, "%s"},
  {FATAL,    INTEGER,  0, "Unexpected signal (&%x) received"},
  {NONFATAL, STRING,   0, "%s"},
#else
  {FATAL,    STRING,   0, "Hit problem with file '%s'"},
  {FATAL,    INTEGER,  0, "Unexpected signal (&%x) received"},
  {NONFATAL, NOPARM,   0, "OS command failed"},
#endif
  {FATAL,    NOPARM,   0, "Handle is invalid or file associated with it has been closed"},
  {FATAL,    NOPARM,   0, "The file pointer cannot be changed"},
  {FATAL,    NOPARM,   0, "The file pointer's value cannot be found"},
  {FATAL,    NOPARM,   0, "The size of the file cannot be found"},
  {FATAL,    NOPARM,   0, "The maximum allowed number of files is already open"},
  {FATAL,    NOPARM,   0, "Amount of memory requested exceeds what is available"},
  {FATAL,    INTSTR,   0, "The interpreter has gone wrong at line %d in %s"},
  {FATAL,    NOPARM,   0, "This Basic command cannot be used in a running program"},
  {FATAL,    NOPARM,   0, "Line number went outside the range 0..65279 when renumbering program"},
  {WARNING,  NOPARM,   0, "Warning: line number is outside the range 0..65279"},
  {WARNING,  INTEGER,  0, "Warning: could not find line %d when renumbering program"},
  {WARNING,  NOPARM,   0, "Line numbers have been added to the program"},
  {WARNING,  NOPARM,   0, "Warning: number of '(' in line exceeds the number of ')'"},
  {WARNING,  NOPARM,   0, "Warning: number of '(' in line is less than the number of ')'"},
  {WARNING,  NOPARM,   0, "Warning: '(' and ')' are nested incorrectly"},
  {WARNING,  INTEGER,  0, "Memory available for Basic programs is now %d bytes"},
  {WARNING,  NOPARM,   0, "Note: one open file has been closed"},
  {WARNING,  INTEGER,  0, "Note: %d open files have been closed"},
  {FATAL,    STRING,   0, "Edit session failed (%s)"},
  {NONFATAL, STRING,   0, "OSCLI failed (%s)"},
  {FATAL,    NOPARM,   0, "This build of the interpreter does not support gzipped programs"},
  {WARNING,  NOPARM,   0, "Warning: floating point number format is not known"},
  {NONFATAL, STRING,   0, "%s"},
  {FATAL,    NOPARM,   0, "SDL Timer initialisation failed"}
};

/*
** 'find_libname' finds the library into which the pointer 'p'
** points. It returns the name of the library or NIL if it points
** into the Basic program itself
*/
static char *find_libname(byte *p) {
  library *lp;
  if (p>=basicvars.page && p<basicvars.top) return NIL;
  lp = find_library(p);
  return lp==NIL ? NIL : lp->libname;
}

static char *procfn(char *name) {
  return *CAST(name, byte *)==TOKEN_PROC ? "PROC" : "FN";
}

/*
** 'print_details' prints an error message and a stack traceback
** if one has been requested. A backtrace is not produced is the
** error is just a warning, that is, iserror is FALSE
*/
static void print_details(boolean iserror) {
  int32 count;
  fnprocinfo *p;
  byte *lp;
  char *libname;
  basicvars.printcount = 0;             /* Reset no. of chars Basic has printed on line to zero */
  if (basicvars.error_line==0) {        /* Error occured when dealing with the command line */
    if (basicvars.linecount==0)
      emulate_printf("%s\r\n", errortext);
    else {
      emulate_printf("[Line %d] %s\r\n", basicvars.linecount, errortext);
    }
  }
  else {        /* Error occured in running program */
    if (basicvars.procstack==NIL)
      emulate_printf("%s at line %d", errortext, basicvars.error_line);
    else {
      emulate_printf("%s at line %d in %s%s", errortext, basicvars.error_line,
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
      count = 0;
      p = basicvars.procstack;
      emulate_printf("PROC/FN call trace:\r\n");
      while (p!=NIL && count<MAXCALLDEPTH) {
        lp = find_linestart(p->retaddr);
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
           procfn(p->fnprocname), p->fnprocname+1, get_lineno(lp));
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
    if (basicvars.error_handler.islocal)        /* Trapped via 'ON ERROR LOCAL' */
      longjmp(*basicvars.local_restart, 1);
    else {      /* Trapped via 'ON ERROR' - Reset everything and return to main interpreter loop */
      basicvars.procstack = NIL;
      basicvars.gosubstack = NIL;
      init_expressions();
      longjmp(basicvars.error_restart, 1);      /* Branch back to the main interpreter loop */
    }
  }
  else {        /* Print error message and halt program */
    basicvars.runflags.running = FALSE;
    emulate_vdu(VDU_ENABLE);    /* Ensure VDU driver is enabled */
    emulate_vdu(VDU_TEXTCURS);  /* And that output goes to the text cursor */
    print_details(severity>WARNING);
    if (basicvars.runflags.closefiles) fileio_shutdown();
    if (basicvars.runflags.quitatend) exit_interpreter(EXIT_FAILURE);   /* Leave interpreter is flag is set */
    basicvars.current = NIL;
    basicvars.procstack = NIL;
    basicvars.gosubstack = NIL;
    longjmp(basicvars.restart, 1);  /* Error - branch to main interpreter loop */
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
  if (errnumber<1 || errnumber>HIGHERROR) {
    emulate_printf("Out of range error number %d\r\n", errnumber);
    errnumber = ERR_BROKEN;
  }
  basicvars.escape = FALSE;             /* Ensure ESCAPE state is clear */
#ifdef TARGET_MINGW
  FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE)); /* Consume any queued characters */
#endif
#ifndef TARGET_RISCOS
  purge_keys();        /* RISC OS purges the keybuffer during escape processing */
#endif
  va_start(parms, errnumber);
  vsprintf(errortext, errortable[errnumber].msgtext, parms);
  va_end(parms);
  basicvars.error_number = errortable[errnumber].equiverror;
  if (basicvars.current==NIL)           /* Not running a program */
    basicvars.error_line = 0;
  else {
    badline = find_linestart(basicvars.current);
    if (badline==NIL && basicvars.curcount>0) badline = find_linestart(basicvars.savedcur[0]);
    if (badline==NIL)   /* Error did not occur in program - Assume it was in the command line */
      basicvars.error_line = 0;
    else {      /* Error occured in running program */
      basicvars.error_line = get_lineno(badline);
    }
  }
  if (errortable[errnumber].severity<=WARNING)  /* Error message is just a warning */
    print_details(FALSE);       /* Print message with no backtrace */
  else {
    handle_error(errortable[errnumber].severity);
  }
}

/*
** 'get_lasterror' is used to return the text of the last error message
*/
char *get_lasterror(void) {
  if (errortext[0]==NUL)
    return COPYRIGHT;
  else {
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
  basicvars.error_number = number;
  severity = number==0 ? FATAL : NONFATAL;
  strcpy(errortext, text);
  badline = find_linestart(basicvars.current);
  if (badline==NIL)     /* 'ERROR' was not used in program - Assume it was in the command line */
    basicvars.error_line = 0;
  else {        /* ERROR used in running program */
    basicvars.error_line = get_lineno(badline);
  }
  handle_error(severity);
}

/*
** 'set_error' is called to set up a normal Basic error handler
*/
void set_error(void) {
  basicvars.error_handler.current = basicvars.current;
  basicvars.error_handler.stacktop = get_safestack();
  basicvars.error_handler.islocal = FALSE;
#ifdef DEBUG
  if (basicvars.debug_flags.debug) fprintf(stderr, "Set up ON ERROR handler at %p,  stack = %p\n",
   basicvars.error_handler.current, basicvars.error_handler.stacktop);
#endif
}

/*
** 'set_local_error' is called to set up a 'local' Basic error handler
*/
void set_local_error(void) {
  basicvars.error_handler.current = basicvars.current;
  basicvars.error_handler.stacktop = get_stacktop();
  basicvars.error_handler.islocal = TRUE;
#ifdef DEBUG
  if (basicvars.debug_flags.debug) fprintf(stderr, "Set up ON ERROR LOCAL handler at %p,  stack = %p\n",
   basicvars.error_handler.current, basicvars.error_handler.stacktop);
#endif
}

/*
** 'clear_error' is called to clear any error handler set up by the
** Basic program
*/
void clear_error(void) {
  basicvars.error_handler.current = NIL;
  basicvars.local_restart = NIL;
  basicvars.escape = FALSE;
#ifdef DEBUG
  if (basicvars.debug_flags.debug) fprintf(stderr, "Clearing ON ERROR handler\n");
#endif
}
