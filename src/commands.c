/*
** This file is part of the Matrix Brandy Basic VI Interpreter.
** Copyright (C) 2000-2014 David Daniels
** Copyright (C) 2018-2019 Michael McConnell and contributors
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
**      This file contains all the 'immediate' Basic commands
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "common.h"
#include "target.h"
#include "basicdefs.h"
#include "miscprocs.h"
#include "tokens.h"
#include "statement.h"
#include "variables.h"
#include "editor.h"
#include "errors.h"
#include "heap.h"
#include "stack.h"
#include "strings.h"
#include "evaluate.h"
#include "screen.h"
#include "keyboard.h"

#ifdef TARGET_RISCOS
#include "kernel.h"
#include "swis.h"
#endif


#define PAGESIZE 20     /* Number of lines listed before pausing */

static char editname[80];       /* Default Name of editor invoked by 'EDIT' command */

#ifndef NOINLINEHELP
static void detailed_help(char *);
#endif

/*
** 'get_number' is called to evaluate an expression that returns an
** integer value. It is used by the functions handling the various
** Basic commands in this file
*/
static int64 get_number(void) {
  factor();
  return pop_anynum64();
}

/*
** 'get_pair' is called to return a pair of values. It assumes that
** 'basicvars.current' points at the first item after the token for the
** command for which it is being used
*/
static void get_pair(size_t *first, size_t *second, size_t firstdef, size_t secondef) {
  size_t low, high = 0;
  *first = firstdef;
  *second = secondef;
  if (isateol(basicvars.current)) return;       /* Return if there is nothing to do */
  if (*basicvars.current == ',' || *basicvars.current == '-')
    low = firstdef;
  else {
    low = get_number();
  }
  if (isateol(basicvars.current))
    high = low;
  else if (*basicvars.current == ',' || *basicvars.current == '-') {
    basicvars.current++;
    if (isateol(basicvars.current))
      high = secondef;
    else {
      high = get_number();
      check_ateol();
    }
  }
  else {
    error(ERR_SYNTAX);
  }
  *first = low;
  *second = high;
}

/*
** 'get_name' evaluates an expression that returns a string (normally
** a file name). It returns a pointer to that string as a null-terminated
** C string. If this string has to be kept, a copy of has to be made
*/
static char *get_name(void) {
  stackitem topitem;
  basicstring descriptor;
  char *cp;
  expression();
  topitem = get_topitem();
  if (topitem != STACK_STRING && topitem != STACK_STRTEMP) error(ERR_TYPESTR);
  descriptor = pop_string();
  cp = tocstring(descriptor.stringaddr, descriptor.stringlen);
  if (topitem == STACK_STRTEMP) free_string(descriptor);
  return cp;
}

/*
** 'exec_new' clears away the program currently in memory. It can also be
** used to alter the amount of memory used by the interpreter in which
** to store and run programs
*/
static void exec_new(void) {
  int32 oldsize, newsize;
  boolean ok;
  if (basicvars.runflags.running) error(ERR_COMMAND);   /* Cannot edit a running program */
  basicvars.current++;
  if (!isateol(basicvars.current)) {    /* New workspace size supplied */
    newsize = get_number();
    check_ateol();
    oldsize = basicvars.worksize;
    release_workspace();                        /* Discard horrible, rusty old Basic workspace */
    ok = init_workspace(ALIGN(newsize));        /* Obtain nice, shiny new one */
    if (!ok) {  /* Allocation failed - Should still be a block of the old size available */
      (void) init_workspace(oldsize);
      error(ERR_NOMEMORY);
    }
    error(WARN_NEWSIZE, basicvars.worksize);
  }
#ifdef USE_SDL
  if (basicvars.worksize && (basicvars.worksize <= 0x7C00)) {
    matrixflags.mode7fb = 0x7C00;
  } else {
    matrixflags.mode7fb = 0xFFFF7C00;
  }
#endif
  clear_program();
  init_expressions();
}

/*
** 'exec_old' checks to see if there is a program still in memory and
** attempts to recover it. Unfortunately, it doesn't work.
*/
static void exec_old(void) {
#if 1
  error(ERR_UNSUPPORTED);
#else
  basicvars.current++;
  check_ateol();
  if (basicvars.runflags.running) error(ERR_COMMAND);   /* Cannot edit a running program */
  recover_program();
#endif
}


/*
** 'list_vars' lists the variables, procedures and functions in the
** symbol table and their values or parameter types. If followed by
** a character, then only the names of variables starting with that
** letter are listed. If followed by a name in double quotes, the
** name is taken to be that of a library and the variables in that
** library are listed.
*/
static void list_vars(void) {
  char *p, ch;
  library *lp;
  boolean found;
  p = CAST(get_srcaddr(basicvars.current), char *);     /* Get the argument of the LVAR command if one is supplied */
  basicvars.current+=1+OFFSIZE;
  check_ateol();
  ch = *p;
  if (ch == '"') {      /* List variables in library */
    int len;
    char *start;
    p++;
    start = p;
    while (*p != '"') p++;
    len = p-start;
    if (len == 0) return;               /* Quick way out if length of name is zero */
    memcpy(basicvars.stringwork, start, len);
    basicvars.stringwork[len] = asc_NUL;
    lp = basicvars.liblist;
    while (lp != NIL && strcmp(lp->libname, basicvars.stringwork) != 0) lp = lp->libflink;
    found = lp != NIL;
    if (lp != NIL) detail_library(lp);
    lp = basicvars.installist;
    while (lp != NIL && strcmp(lp->libname, basicvars.stringwork) != 0) lp = lp->libflink;
    found = found || lp != NIL;
    if (lp != NIL) detail_library(lp);
    if (!found) error(ERR_NOLIB, basicvars.stringwork);
  }
  else {
    if (isalpha(ch))
      emulate_printf("Variables in program starting with '%c':\r\n", ch);
    else {
      ch = ' ';
      emulate_printf("Variables in program:\r\n");
    }
    list_variables(ch);
    list_libraries(ch);
  }
}

/*
** 'list_if' deals with the 'LISTIF' command. It lists each line
** where there is at least one occurence of the string following
** the 'LISTIF' command.
*/
static void list_if(void) {
  byte *p, *tp;
  int32 targetlen, statelen;
  char first, *sp;
  boolean more;
  p = tp = get_srcaddr(basicvars.current);      /* Get address of string to search for */
  basicvars.current+=1+OFFSIZE;
  check_ateol();
  while (*p != asc_NUL) p++;        /* Find the end of the string */
  targetlen = p-tp;             /* Number of characters in search string */
  if (targetlen == 0) return;   /* End if search string is empty */
  p = basicvars.start;
  more = TRUE;
  first = *tp;
  while (more && !AT_PROGEND(p)) {
    reset_indent();
    expand(p, basicvars.stringwork);
    sp = basicvars.stringwork;
    statelen = strlen(basicvars.stringwork);
    do {
      sp++;
      while (statelen>=targetlen && *sp != first) {
        statelen--;
        sp++;
      }
    } while(statelen>=targetlen && memcmp(sp, tp, targetlen) != 0);
    if (statelen>=targetlen) {  /* Can only be true if the string was found */
      if (basicvars.debug_flags.tokens)
        emulate_printf("%08p  %s\r\n", p, basicvars.stringwork);
      else {
        emulate_printf("%s\r\n", basicvars.stringwork);
      }
    }
    p+=GET_LINELEN(p);
  }
}

static void set_listoption(int32 listopts) {
  basicvars.list_flags.space = (listopts & LIST_SPACE) != 0;
  basicvars.list_flags.indent = (listopts & LIST_INDENT) != 0;
  basicvars.list_flags.split = (listopts & LIST_SPLIT) != 0;
  basicvars.list_flags.noline = (listopts & LIST_NOLINE) != 0;
  basicvars.list_flags.lower = (listopts & LIST_LOWER) != 0;
  basicvars.list_flags.showpage = (listopts & LIST_PAGE) != 0;
  basicvars.list_flags.expand = (listopts & LIST_EXPAND) != 0;
}

/*
** 'set_listopt' sets the options for the LIST command. It is also
** used to set the level of internal debugging information produced
** by the interpreter.
*/
static void set_listopt(void) {
  int32 listopts;
  basicvars.current++;
  listopts = get_number();
  check_ateol();
  set_listoption(listopts);
/* Internal debugging options */
  basicvars.debug_flags.debug = (listopts & DEBUG_DEBUG) != 0;
  basicvars.debug_flags.tokens = (listopts & DEBUG_TOKENS) != 0;
  basicvars.debug_flags.variables = (listopts & DEBUG_VARIABLES) != 0;
  basicvars.debug_flags.strings = (listopts & DEBUG_STRINGS) != 0;
  basicvars.debug_flags.stats = (listopts & DEBUG_STATS) != 0;
  basicvars.debug_flags.stack = (listopts & DEBUG_STACK) != 0;
  basicvars.debug_flags.allstack = (listopts & DEBUG_ALLSTACK) != 0;
  basicvars.debug_flags.functions = (listopts & DEBUG_FUNCTIONS) != 0;
}

/*
** 'delete' is called to delete a range of lines from the program
*/
static void delete(void) {
  size_t low, high;
  if (basicvars.misc_flags.badprogram) error(ERR_BADPROG);
  if (basicvars.runflags.running) error(ERR_COMMAND);   /* Cannot modify a running program */
  basicvars.current++;
  if (isateol(basicvars.current)) error(ERR_SYNTAX);
  get_pair(&low, &high, 0, MAXLINENO);
  check_ateol();
  if (low<0 || low>MAXLINENO || high<0 || high>MAXLINENO) error(ERR_LINENO);
  delete_range(low, high);
}

/*
** 'renumber' renumbers a Basic program
*/
static void renumber(void) {
  size_t start, step;
  if (basicvars.misc_flags.badprogram) error(ERR_BADPROG);
  if (basicvars.runflags.running) error(ERR_COMMAND);   /* Cannot modify a running program */
  basicvars.current++;
  get_pair(&start, &step, 10, 10);
  check_ateol();
  if (start<0 || start>MAXLINENO) error(ERR_LINENO);
  if (step<=0 || step>=MAXLINENO) error(ERR_SILLY);     /* An increment of zero is silly */
  renumber_program(basicvars.start, start, step);
}

/*
** 'show_memory' prints out chunks of memory in hex and character form.
** 'LISTB' displays it as byte data and 'LISTW' as word.
*/
static void show_memory(void) {
  byte which;
  size_t lowaddr, highaddr;
  which = *basicvars.current;
  basicvars.current++;
  get_pair(&lowaddr, &highaddr, basicvars.memdump_lastaddr, basicvars.memdump_lastaddr+0x40);
  check_ateol();
  if (highaddr == lowaddr) highaddr = lowaddr+0x40;
  if (which == BASIC_TOKEN_LISTB)
    show_byte(lowaddr, highaddr);
  else {
    show_word(lowaddr, highaddr);
  }
  basicvars.memdump_lastaddr = highaddr;
}

/*
** 'list_program' lists the source of a Basic  program
*/
static void list_program(void) {
  size_t lowline, highline;
  int32 count;
  boolean more, paused;
  byte *p;
  if (basicvars.misc_flags.badprogram) error(ERR_BADPROG);
  basicvars.current++;
  get_pair(&lowline, &highline, 0, MAXLINENO);
  check_ateol();
  if (lowline<0 || lowline>MAXLINENO || highline<0 || highline>MAXLINENO) error(ERR_LINENO);
  if (lowline == 0)
    p = basicvars.start;
  else {
    p = find_line(lowline);
  }
  reset_indent();
  basicvars.printcount = 0;
  count = 0;
  more = TRUE;
  while (more && !AT_PROGEND(p) && get_lineno(p)<=highline) {
    expand(p, basicvars.stringwork);
    if (basicvars.debug_flags.tokens)
      emulate_printf("%p  %s\r\n", p, basicvars.stringwork);
    else {
      emulate_printf("%s\r\n", basicvars.stringwork);
    }
    p+=GET_LINELEN(p);
    if (basicvars.list_flags.showpage) {
      count++;
      if (count == PAGESIZE) {
        paused = TRUE;
        emulate_printf("-- More --");
        do {
#ifdef NEWKBD
//        if (basicvars.escape) error(ERR_ESCAPE);
          if (kbd_escpoll()) error(ERR_ESCAPE);
          count = kbd_get();
#else
          if (basicvars.escape) error(ERR_ESCAPE);
          count = emulate_get();
#endif
          switch (count) {
          case ' ':
            count = 0;
            paused = FALSE;
            break;
          case asc_CR: case asc_LF:
            count = PAGESIZE-1; /* A hack, but it does the job */
            paused = FALSE;
            break;
          case asc_ESC:
            paused = more = FALSE;
          }
        } while (paused);
        emulate_printf("\r          \r");       /* Overwrite 'more' */
      }
    }
#ifdef USE_SDL
#ifdef NEWKBD
    if (kbd_escpoll()) error(ERR_ESCAPE);
//  if (kbd_inkey(-113)) basicvars.escape=TRUE;
#else
    if (emulate_inkey(-113)) basicvars.escape=TRUE;
#endif
#endif
    if (basicvars.escape) error(ERR_ESCAPE);
  }
}

/*
** 'list_hexline' lists a line as a hex dump
*/
static void list_hexline(void) {
  int32 length;
  size_t theline;
  byte *where;
  basicvars.current++;
  get_pair(&theline, &theline, 0, 0);
  check_ateol();
  if (theline<0 || theline>MAXLINENO) error(ERR_LINENO);
  if (theline == 0)
    where = basicvars.start;
  else {
    where = find_line(theline);
  }
  if (theline != get_lineno(where)) error(ERR_LINEMISS, theline);       /* Line not found */
  length = get_linelen(where);
  emulate_printf("Line %d at %p, length=%d", get_lineno(where), where, length);
  if (length<MINSTATELEN || length>MAXSTATELEN) {
    emulate_printf("  ** Statement length is bad **\r\n");
    length = 96;        /* Print six lines of sixteen bytes */
  }
  else {
    emulate_printf("\r\n");
  }
  show_byte((size_t)where, (size_t)where+length);
}

/*
** 'check_incore' looks for a so-called 'incore' filename, that is, the
** name of the file to save as given on the first line of the program after
** a '>'. It returns a pointer to the file name or NIL if it cannot find
(( one
*/
static char *check_incore(void) {
  byte *p;
  if (AT_PROGEND(basicvars.start)) return NIL;  /* There is nothing to search */
  p = basicvars.start+OFFSOURCE;
  while (*p != asc_NUL && *p != '>') p++;   /* Look for a '>' */
  if (*p == asc_NUL) return NIL;            /* Did not find one so give up */
  p = skip(p+1);
  if (*p == asc_NUL) return NIL;            /* There is nothing after the '>' */
  return TOSTRING(p);
}

/*
** 'get_savefile' returns a pointer to the name to be used when
** saving a file. This can be a name given on the command line,
** an 'in core' name or one saved from a previous load or save
** command. The 'in core' name takes precedence over the one
** saved in basicvars.program. (Should this be reversed?)
*/
static char *get_savefile(void) {
  char *np;
  if (isateol(basicvars.current)) {
    np = check_incore();                /* Check for an 'incore' file name */
    if (np == NIL) {                    /* Did not find one */
      if (basicvars.program[0] == asc_NUL)
        error(ERR_FILENAME);
      else {
        np = basicvars.program;
      }
    }
  }
  else {
    np = get_name();
    check_ateol();
  }
  return np;
}

/*
** 'save_program' is called to save a program. Programs are
** always saved in text form
*/
static void save_program(void) {
  char *np;
  if (basicvars.misc_flags.badprogram) error(ERR_BADPROG);
  basicvars.current++;
  np = get_savefile();
  reset_indent();
  write_text(np, NULL);
  strcpy(basicvars.program, np);        /* Preserve name used when saving program for later */
}

/*
** 'saveo_program' implements TEXTSAVEO and SAVEO
*/
static void saveo_program(void) {
  char *np;
  int32 saveopts;
  if (basicvars.misc_flags.badprogram) error(ERR_BADPROG);
  basicvars.current++;
  if (isateol(basicvars.current)) error(ERR_SYNTAX);
  saveopts = get_number();
  if (*basicvars.current == ',') basicvars.current++;   /* Skip comma between options and name */
  np = get_savefile();
  basicvars.listo_copy = basicvars.list_flags;
  set_listoption(saveopts);             /* Change list options to save options */
  basicvars.list_flags.lower = FALSE;   /* Lose pointless options */
  basicvars.list_flags.showpage = FALSE;
  basicvars.list_flags.expand = FALSE;
  reset_indent();
  write_text(np, NULL);
  strcpy(basicvars.program, np);        /* Preserve name used for program for later */
  basicvars.list_flags = basicvars.listo_copy;  /* Restore LISTO flags to original values */
}

/*
** 'load_program' attempts to load a Basic program into memory.
** Note that the real name of the file loaded will be stored in
** basicvars.filename when the file is opened.
*/
static void load_program(void) {
  char *np;
  if (basicvars.runflags.running) error(ERR_COMMAND);   /* Cannot modify a running program */
  basicvars.current++;
  if (isateol(basicvars.current)) error(ERR_FILENAME);
  np = get_name();
  check_ateol();
  clear_varptrs();
  clear_varlists();
  clear_strings();
  clear_heap();
  clear_stack();
  read_basic(np);
  init_expressions();
  strcpy(basicvars.program, basicvars.filename);
}

/*
** 'install_library' loads the named library into permanent memory (as
** opposed to saving it on the Basic heap)
*/
static void install_library(void) {
  char *name;
  basicvars.current++;
  if (isateol(basicvars.current))               /* Filename missing */
    error(ERR_FILENAME);
  else {
    do {
      name = get_name();
      if (strlen(name)>0) read_library(name, INSTALL_LIBRARY);  /* Permanently install the library */
      if (*basicvars.current != ',') break;
      basicvars.current++;
    } while (TRUE);
    check_ateol();
  }
}

static void print_help(void) {
#ifndef NOINLINEHELP
  char *parm;
#endif
  basicvars.current++;
#ifndef NOINLINEHELP
  if (isateol(basicvars.current)) {
    show_options(1);
    emulate_printf("HELP can show help on a keyword, for example HELP \"MODE\". Note that the\r\nkeyword must be given in quotes. HELP \".\" will list the keywords help is\r\navailable on.\r\n");
  } else {
    parm=get_name();
    detailed_help(parm);
  }
#else
  show_options(1);
#endif
  check_ateol();
}

#ifdef TARGET_RISCOS
/*
** 'invoke_editor' invokes an editor. The Basic program current in memory
** is written to a temporary file and then a text editor invoked to edit
** the file. On returning from the editor, the Basic program is reloaded.
** There is no error checking here. If the editor flagged an error then
** the results are unpredictable
*/
static void invoke_editor(void) {
  char tempname[FNAMESIZE], savedname[FNAMESIZE];
  int32 retcode, r2byte;
  char *p;
  FILE *fhandle;
  _kernel_osfile_block now, then;
  if (basicvars.runflags.running) error(ERR_COMMAND);   /* Cannot edit a running program */
  basicvars.listo_copy = basicvars.list_flags;
  if (basicvars.misc_flags.validedit) basicvars.list_flags = basicvars.edit_flags;
  basicvars.list_flags.lower = FALSE;   /* Changing keyword to lower case is useless here */
  basicvars.list_flags.expand = FALSE;  /* So is adding extra blanks */
  fhandle=secure_tmpnam(tempname);
  if (!fhandle) {
    error(ERR_EDITFAIL, strerror (errno));
    return;
  }
  reset_indent();
  write_text(tempname, fhandle);
  basicvars.list_flags = basicvars.listo_copy;  /* Restore LISTO flags to original values */
  p = getenv("Wimp$State");             /* Is interpreter running under the RISC OS desktop? */
  if (p == NIL || strcmp(p, "desktop") != 0) {  /* Running at F12 command line or outside desktop */
/*
** Interpreter is running at the F12 command line or outside the
** desktop. The editor called is twin in this case, but it is
** fairly pointless to do this as twin does not return to the
** program that invoked it in the way this code wants
*/
    strcpy(basicvars.stringwork, "twin");
    strcat(basicvars.stringwork, " ");
    strcat(basicvars.stringwork, tempname);
    retcode = system(basicvars.stringwork);
    if (retcode == 0) { /* Invocation of editor worked */
      strcpy(savedname, basicvars.program);     /* Otherwise 'clear' erases it */
      clear_program();
      read_basic(tempname);
      strcpy(basicvars.program, savedname);
    }
    else
      error(ERR_EDITFAIL, strerror (errno));
  }
  else {
/*
** Program is running in the desktop, probably in a task window.
** The program can be passed to an editor easily enough by just
** writing it to a file and the filer_run'ing that file. Getting
** it back is more of a problem. The editor runs in parallel
** with the interpreter and the filer_run call returns immediately.
** There is no easy way to wait until the file has been saved so
** a hack involving the 'inkey' OSBYTE call is used. Once per
** second the code uses an OSFILE call to check if the timestamp
** on the file has changed and if it has, it ends the loop and
** reloads it
*/
    strcpy(basicvars.stringwork, editname);
    strcat(basicvars.stringwork, " ");
    strcat(basicvars.stringwork, tempname);
/* Extract details of file as written to disk */
    retcode = _kernel_osfile(17, tempname, &now);
    if (retcode != 1) error(ERR_BROKEN, __LINE__, "commands");
    retcode = system(basicvars.stringwork);
    if (retcode == 0) { /* Invocation of editor worked */
/* Now sit in a loop waiting for the file to change */
      do {
        retcode = _kernel_osbyte(129, 100, 0);          /* Wait for one second */
        r2byte = (retcode>>BYTESHIFT) & BYTEMASK;       /* Only interested in what R2 contains after call */
        if (r2byte == ESC || basicvars.escape) break;   /* Escape was pressed - Escape from loop */
        retcode = _kernel_osfile(17, tempname, &then);
      } while (retcode == 1 && now.load == then.load && now.exec == then.exec);
      if (retcode == 1) {       /* Everything is okay and file has been updated */
        strcpy(savedname, basicvars.program);   /* Otherwise 'clear' erases it */
        clear_program();
        read_basic(tempname);
        strcpy(basicvars.program, savedname);
      }
    }
    else
      error(ERR_EDITFAIL, strerror (errno));
  }
  remove(tempname);
}

/*
** 'exec_editor' either calls a text editor to edit the whole
** program or allows a single line to be edited. This latter
** feature is not supported under RISCOS (as it has cursor
** editing, which is far more flexible)
*/
static void exec_editor(void) {
  if (basicvars.misc_flags.badprogram) error(ERR_BADPROG);
  basicvars.current++;
  if (isateol(basicvars.current))       /* Nothing on line - start an editor */
    invoke_editor();
  else {        /* 'edit <line no>' is not supported under RISC OS */
    error(ERR_UNSUPPORTED);
  }
}

#else

/*
** 'invoke_editor' invokes an editor. The Basic program current in memory
** is written to a temporary file and then a text editor invoked to edit
** the file. On returning from the editor, the Basic program is reloaded.
** There is no error checking here. If the editor flagged an error then
** the results are unpredictable
*/
static void invoke_editor(void) {
  char tempname[FNAMESIZE], savedname[FNAMESIZE];
  int32 retcode;
  FILE *fhandle;
#ifdef TARGET_DJGPP
  char *p;
#endif
  if (basicvars.runflags.running) error(ERR_COMMAND);   /* Cannot edit a running program */
  fhandle=secure_tmpnam(tempname);
  if (!fhandle) {
    error(ERR_EDITFAIL, strerror (errno));
    return;
  }
#ifdef TARGET_DJGPP
  p = tempname;         /* Replace '/' in filename with '\' */
  while (*p != NUL) {
    if (*p == '/') *p = '\\';
    p++;
  }
#endif
  basicvars.listo_copy = basicvars.list_flags;
  if (basicvars.misc_flags.validedit) basicvars.list_flags = basicvars.edit_flags;
  basicvars.list_flags.lower = FALSE;   /* Changing keyword to lower case is useless here */
  basicvars.list_flags.expand = FALSE;
  reset_indent();
  write_text(tempname, fhandle); /* This function will close fhandle */
  basicvars.list_flags = basicvars.listo_copy;  /* Restore LISTO flags to original values */
  strcpy(basicvars.stringwork, editname);
  strcat(basicvars.stringwork, " ");
  strcat(basicvars.stringwork, tempname);
  retcode = system(basicvars.stringwork);
  if (retcode == 0) {                           /* Editor call worked */
    strcpy(savedname, basicvars.program);       /* Otherwise 'clear' erases it */
    clear_program();
    read_basic(tempname);
    strcpy(basicvars.program, savedname);
  }
  else
    error(ERR_EDITFAIL, strerror (errno));
  remove(tempname);
}

/*
** 'alter_line' is called to alter one line in a program by copying
** it to the input buffer so that it can be edited there
** One point to watch out for here is that this code overwrites the
** existing contents of 'thisline' (this contains the 'EDIT <line>'
** command). Since we are going to return to the command line loop
** anyway, the code avoids the problems this will cause in
** exec_statements() by branching directly back there instead of
** returning via the proper route
*/
static void alter_line(void) {
  int32 lineno;
  byte *p;
  boolean ok;
  lineno = get_number();
  check_ateol();
  if (basicvars.runflags.running) error(ERR_COMMAND);   /* Cannot edit a running program */
  if (basicvars.misc_flags.badprogram) error(ERR_BADPROG);
  if (lineno<0 || lineno>MAXLINENO) error(ERR_LINENO);
  p = find_line(lineno);
  if (get_lineno(p) != lineno) error(ERR_LINEMISS, lineno);
  basicvars.listo_copy = basicvars.list_flags;
  basicvars.list_flags.space = FALSE;   /* Reset listing options */
  basicvars.list_flags.indent = FALSE;
  basicvars.list_flags.split = FALSE;
  basicvars.list_flags.noline = FALSE;
  basicvars.list_flags.lower = FALSE;
  basicvars.list_flags.expand = FALSE;
  expand(p, basicvars.stringwork);
  basicvars.list_flags = basicvars.listo_copy;  /* Restore LISTOF flags to original values */
  ok = amend_line(basicvars.stringwork, MAXSTATELEN);
  if (!ok) error(ERR_ESCAPE);
  tokenize(basicvars.stringwork, thisline, HASLINE, FALSE);
//  tokenize(basicvars.stringwork, thisline, HASLINE);
  if (get_lineno(thisline) == NOLINENO) /* If line number has been removed, execute line */
    exec_thisline();
  else {
    edit_line();
  }
/*
** At this point the contents of 'thisline' are effectively undefined
** but this will cause problems in exec_statements() when we return
** to it. To avoid this, the code just branches back into the command
** loop via 'longjump()'. This is a kludge.
*/
  siglongjmp(basicvars.restart, 1);
}

/*
** 'exec_editor' either calls a text editor to edit the whole
** program or allows a single line to be edited
*/
static void exec_editor(void) {
  if (basicvars.misc_flags.badprogram) error(ERR_BADPROG);
  basicvars.current++;
  if (isateol(basicvars.current))       /* Nothing on line - start an editor */
    invoke_editor();
  else {        /* EDIT <line> command */
    alter_line();
  }
}

#endif

/*
** 'exec_edito' deals with the EDITO command. This invokes the editor
** using the supplied listo options to format the program
*/
static void exec_edito(void) {
  int32 editopts;
  if (basicvars.misc_flags.badprogram) error(ERR_BADPROG);
  basicvars.current++;
  if (isateol(basicvars.current)) error(ERR_SYNTAX);
  editopts = get_number();      /* Get LISTO options to use for tranferring file to editor */
  check_ateol();
  basicvars.edit_flags.space = (editopts & LIST_SPACE) != 0;
  basicvars.edit_flags.indent = (editopts & LIST_INDENT) != 0;
  basicvars.edit_flags.split = FALSE;
  basicvars.edit_flags.noline = (editopts & LIST_NOLINE) != 0;
  basicvars.edit_flags.lower = FALSE;
  basicvars.edit_flags.showpage = FALSE;
  basicvars.edit_flags.expand = FALSE;
  basicvars.misc_flags.validedit = TRUE;
  invoke_editor();
}

/*
** 'exec_crunch' looks after the 'CRUNCH' command. This has no
** effect with this interpreter so is ignored
*/
static void exec_crunch(void) {
  basicvars.current++;
  (void) get_number();
  check_ateol();
}

/* 'exec_auto' borrows heavily from alter_line (above), but unlike the editing
** of an existing line the lune number cannot be deleted (as per BBC/RISC OS
** BASIC). The same kludge is used should the while loop terminate, but usually
** this command will be terminated by hitting the ESCAPE key.
*/
static void exec_auto(void) {
  int32 lineno = 10, linestep = 10;
  boolean ok;
  basicvars.current++;
  if (!isateol(basicvars.current)) {
    lineno = get_number();
    basicvars.current++;
    if (!isateol(basicvars.current)) {
      linestep = get_number();
    }
  }
  if (basicvars.runflags.running) error(ERR_COMMAND);   /* Cannot edit a running program */
  if (basicvars.misc_flags.badprogram) error(ERR_BADPROG);
  if (lineno<0 || lineno>MAXLINENO) error(ERR_LINENO);
  if (linestep<=0) error(ERR_SILLY);
  if (linestep>MAXLINENO) error(ERR_SYNTAX);
  while (lineno <= MAXLINENO) { /* ESCAPE will interrupt */
    emulate_printf("%5d ",lineno);
    sprintf(basicvars.stringwork, "%5d", lineno);
    ok = amend_line(basicvars.stringwork+5, MAXSTATELEN);
    if (!ok) error(ERR_ESCAPE);
    tokenize(basicvars.stringwork, thisline, HASLINE, FALSE);
    edit_line();
    lineno += linestep;
  }
  siglongjmp(basicvars.restart, 1);
}

/*
** 'exec_command' handles all the Basic statement types that are normally
** only run as immediate commands. Commands that modify the program such
** as 'DELETE' can only be used at the command line. The command functions
** are called with 'current' pointing at the command's token. 'current'
** should be left pointing at the end of the statement. The functions
** should also check that the statement ends properly. All errors are
** handled by 'sigsetjmp' and 'siglongjmp' in the normal way
*/
void exec_command(void) {
  basicvars.current++;  /* Point at command type token */
  switch (*basicvars.current) {
  case BASIC_TOKEN_NEW:
    exec_new();
    break;
  case BASIC_TOKEN_OLD:
    exec_old();
    break;
  case BASIC_TOKEN_LOAD: case BASIC_TOKEN_TEXTLOAD:
    load_program();
    break;
  case BASIC_TOKEN_SAVE: case BASIC_TOKEN_TEXTSAVE:
    save_program();
    break;
  case BASIC_TOKEN_SAVEO: case BASIC_TOKEN_TEXTSAVEO:
    saveo_program();
    break;
  case BASIC_TOKEN_INSTALL:
    install_library();
    break;
  case BASIC_TOKEN_LIST:
    list_program();
    break;
  case BASIC_TOKEN_LISTB: case BASIC_TOKEN_LISTW:
    show_memory();
    break;
  case BASIC_TOKEN_LISTL:
    list_hexline();
    break;
  case BASIC_TOKEN_LISTIF:
    list_if();
    break;
  case BASIC_TOKEN_LISTO:
    set_listopt();
    break;
  case BASIC_TOKEN_LVAR:
    list_vars();
    break;
  case BASIC_TOKEN_RENUMBER:
    renumber();
    break;
  case BASIC_TOKEN_DELETE:
    delete();
    break;
  case BASIC_TOKEN_HELP:
    print_help();
    break;
  case BASIC_TOKEN_EDIT: case BASIC_TOKEN_TWIN:
    exec_editor();
    break;
  case BASIC_TOKEN_EDITO: case BASIC_TOKEN_TWINO:
    exec_edito();
    break;
  case BASIC_TOKEN_CRUNCH:
    exec_crunch();
    break;
  case BASIC_TOKEN_AUTO:
    exec_auto();
    break;
  default:
    error(ERR_UNSUPSTATE);
  }
}

/*
** 'init_commands' checks to if there is an environment variable set
** up that specifies the name of the editor the interpreter should
** call.
*/
void init_commands(void) {
  char *editor;
  editor = getenv(EDITOR_VARIABLE);
  if (editor != NULL)
    strcpy(editname, editor);
  else {
#ifdef TARGET_UNIX
    editor=getenv("EDITOR");
    if (editor != NULL)
      strcpy(editname, editor);
    else {
      editor=getenv("VISUAL");
      if (editor != NULL)
        strcpy(editname, editor);
      else {
        strcpy(editname, DEFAULT_EDITOR);
      }
    }
#else
    strcpy(editname, DEFAULT_EDITOR);
#endif
  }
}

/* Define NOINLINEHELP to disable this, to reduce binary size if required */
#ifndef NOINLINEHELP
static void detailed_help(char *cmd) {
  if        (!strcmp(cmd, "ABS")) {
    emulate_printf("This function gives the magnitude (absolute value) of a number (<factor>).");
  } else if (!strcmp(cmd, "ACS")) {
    emulate_printf("This function gives the arc cosine of a number (<factor>).");
  } else if (!strcmp(cmd, "ADVAL")) {
    emulate_printf("This function gives the value of the specified analogue port or buffer.\r\nNote that this function has limited support in Matrix Brandy.");
  } else if (!strcmp(cmd, "AND")) {
    emulate_printf("Bitwise logical AND between two integers. Priority 6.");
  } else if (!strcmp(cmd, "ASC")) {
    emulate_printf("This function gives the ASCII code of the first character of a string.");
  } else if (!strcmp(cmd, "ASN")) {
    emulate_printf("This function gives the arc sine of a number (<factor>).");
  } else if (!strcmp(cmd, "ATN")) {
    emulate_printf("This function gives the arc tangent of a number (<factor>).");
  } else if (!strcmp(cmd, "AUTO")) {
    emulate_printf("This command generates line numbers for typing in a program.\r\nAUTO [<base number>[,<step size>]]");
  } else if (!strcmp(cmd, "APPEND")) {
    emulate_printf("This command is not implemented in Matrix Brandy. In ARM BBC BASIC, this\r\ncommand appends a file to the program and renumbers the new lines.");
  } else if (!strcmp(cmd, "BEAT")) {
    emulate_printf("This function gives the current microbeat number.");
  } else if (!strcmp(cmd, "BEATS")) {
    emulate_printf("BEATS <expression>: Set the number of microbeats in a bar.\r\nAs a function BEATS gives the current number of microbeats.");
  } else if (!strcmp(cmd, "BGET")) {
    emulate_printf("This function gives the next byte from the specified channel: BGET#<channel>.\r\n<channel> is a file or network stream handle opened with OPENIN or OPENUP.\r\nThis function returns -1 if no data is available on a network stream, and\r\n-2 if the network connection has been closed remotely.");
  } else if (!strcmp(cmd, "BPUT")) {
    emulate_printf("BPUT#<channel>,<number>[,<number>...]: put byte(s) to open stream.\r\nBPUT#<channel>,<string>[;]: put string to open file, with[out] newline.\r\n<channel> is a file or network stream handle opened with OPENOUT or OPENUP.");
  } else if (!strcmp(cmd, "CALL")) {
    emulate_printf("CALL <expression>[,<variable>]^: Call machine code.\r\nIn Matrix Brandy, only calls to selected BBC Micro OS vectors are supported.");
  } else if (!strcmp(cmd, "CASE")) {
    emulate_printf("CASE <expression> OF: start of CASE..WHEN..OTHERWISE..ENDCASE structure.");
  } else if (!strcmp(cmd, "CHAIN")) {
    emulate_printf("Load and run a new BASIC program.");
  } else if (!strcmp(cmd, "CHR$")) {
    emulate_printf("This function gives the one character string of the supplied ASCII code.");
  } else if (!strcmp(cmd, "CIRCLE")) {
    emulate_printf("CIRCLE [FILL] x, y, r: draw circle outline [solid].");
  } else if (!strcmp(cmd, "CLEAR")) {
    emulate_printf("Forget all variables.");
  } else if (!strcmp(cmd, "CLG")) {
    emulate_printf("Clear graphics screen.");
  } else if (!strcmp(cmd, "CLOSE")) {
    emulate_printf("CLOSE#<channel>: close specified file or network socket.");
  } else if (!strcmp(cmd, "CLS")) {
    emulate_printf("Clear text screen.");
  } else if (!strcmp(cmd, "COLOUR") || !strcmp(cmd, "COLOR")) {
    emulate_printf("COLOUR A [TINT t]: set text foreground colour [and tint] (background 128+a)\r\nCOLOUR [OF f] [ON b]: set foreground to colour number f and/or background to b.\r\nCOLOUR a,p: set palette entry for logical colour a to physical colour p.\r\nCOLOUR [[OF] r,g,b] [ON r,g,b]: set foreground and/or background to r, g, b.\r\nCOLOUR a,r,g,b: set palette entry for a to r,g, b physical colour.");
  } else if (!strcmp(cmd, "COS")) {
    emulate_printf("This function gives the cosine of a number (<factor>).");
  } else if (!strcmp(cmd, "COUNT")) {
    emulate_printf("This function gives the number of characters PRINTed since the last newline.");
  } else if (!strcmp(cmd, "CRUNCH")) {
    emulate_printf("This command is ignored, and does nothing.");
  } else if (!strcmp(cmd, "DATA")) {
    emulate_printf("Introduces line of DATA to be READ. The list of items is separated by commas.\r\nLOCAL DATA, LOCAL RESTORE: save and restore current DATA pointer.");
  } else if (!strcmp(cmd, "DEF")) {
    emulate_printf("Define function or procedure: DEF FN|PROC<name>[(<parameter list>)].\r\nEnd function with =<expression>; end procedure with ENDPROC.");
  } else if (!strcmp(cmd, "DEG")) {
    emulate_printf("This function gives the value in degrees of a number in radians.");
  } else if (!strcmp(cmd, "DELETE")) {
    emulate_printf("This command deletes all lines between the specified numbers.\r\nDELETE <start line number>[,<end line number>]");
  } else if (!strcmp(cmd, "DIM")) {
    emulate_printf("DIM fred(100,100): create and initialise an array.\r\nDIM fred [LOCAL] 100: allocate [temporary] space for a byte array etc\r\nDIM(fred()): function gives the number of dimensions\r\nDIM(fred(),n): function gives the size of the n'th dimension.");
  } else if (!strcmp(cmd, "DIV")) {
    emulate_printf("Integer division, rounded towards zero, between two integers. Priority 3.");
  } else if (!strcmp(cmd, "DRAW")) {
    emulate_printf("DRAW [BY] x, y: graphics draw to [relative by] x, y.");
  } else if (!strcmp(cmd, "EDIT")) {
    emulate_printf("EDIT: opens the current program in an external ext editor.\r\nEDIT <line number>: Inline edits the specified line.");
  } else if (!strcmp(cmd, "ELLIPSE")) {
    emulate_printf("ELLIPSE [FILL] x, y, maj, min[,angle]: draw ellipse outline [solid].");
  } else if (!strcmp(cmd, "ELSE")) {
    emulate_printf("Part of the IF..THEN..ELSE structure. If found at the start of a line, it is\r\npart of the multi-line IF..THEN..ELSE..ENDIF structure.\r\nELSE can also appear in ON.. GOTO|GOSUB|PROC to set the default option.");
  } else if (!strcmp(cmd, "END")) {
    emulate_printf("END: statement marking end of program execution.\r\nAs a function END gives the end address of memory used.\r\nThe form END=<expression> to alter the memory allocation is not supported.");
  } else if (!strcmp(cmd, "ENDCASE")) {
    emulate_printf("End of CASE structure at start of line. See CASE.");
  } else if (!strcmp(cmd, "ENDIF")) {
    emulate_printf("End of multi-line IF structure at start of line. See IF.");
  } else if (!strcmp(cmd, "ENDPROC")) {
    emulate_printf("End of procedure definition.");
  } else if (!strcmp(cmd, "ENDWHILE")) {
    emulate_printf("End of WHILE structure. See WHILE.");
  } else if (!strcmp(cmd, "ENVELOPE")) {
    emulate_printf("ENVELOPE takes 14 numeric parameters separated by commas.\r\nThis command does nothing in Matrix Brandy or RISC OS, it is a legacy from the\r\nBBC Micro.");
  } else if (!strcmp(cmd, "EOF")) {
    emulate_printf("This function gives TRUE if at end of open file; else FALSE; EOF#<channel>.");
  } else if (!strcmp(cmd, "EOR")) {
    emulate_printf("Bitwise logical Exclusive-OR between two integers. Priority 7.");
  } else if (!strcmp(cmd, "ERL")) {
    emulate_printf("This function gives the line number of the last error.");
  } else if (!strcmp(cmd, "ERR")) {
    emulate_printf("This function gives the error number of the last error.");
  } else if (!strcmp(cmd, "ERROR")) {
    emulate_printf("Part of ON ERROR; LOCAL ERROR and RESTORE ERROR statements.\r\nCause an error: ERROR <number>,<string>.");
  } else if (!strcmp(cmd, "EVAL")) {
    emulate_printf("This function evaluates a string: EVAL(\"2*X+1\").");
  } else if (!strcmp(cmd, "EXP")) {
    emulate_printf("This function gives the exponential of a number (<factor>).");
  } else if (!strcmp(cmd, "EXT")) {
    emulate_printf("This function gives the length (extent) of an open file: EXT#<channel>.\r\nEXT#<channel>=<expression> sets the length of an open file.");
  } else if (!strcmp(cmd, "FALSE")) {
    emulate_printf("This function gives the logical value 'false', i.e. 0.");
  } else if (!strcmp(cmd, "FILL")) {
    emulate_printf("FILL [BY[ x,y: flood fill from [relative to] point x,y.");
  } else if (!strcmp(cmd, "FN")) {
    emulate_printf("Call a function with FNfred(x,y): define one with DEF FNfred(a,b).");
  } else if (!strcmp(cmd, "FOR")) {
    emulate_printf("FOR <variable> = <start value> TO <limit value> [STEP <step size>].");
  } else if (!strcmp(cmd, "GCOL")) {
    emulate_printf("GCOL a [TINT t]: set graphics foreground colour [and tint] (background 128+a).\r\nGCOL <action>,a [TINT t]: set graphics fore|background colour and action.\r\nGCOL [OF [<action>,]f] [ON [<action>,]b:\r\n     Set graphics foreground and/or background colour number [and action].\r\nGCOL [[OF] [<action>,]r,g,b] [ON [<action,]r,g,b]:\r\n     Set graphics foreground and/or background colour to r, g, b [and action].");
  } else if (!strcmp(cmd, "GET")) {
    emulate_printf("This function gives the ASCII value of the next character in the input stream.");
  } else if (!strcmp(cmd, "GET$")) {
    emulate_printf("This function gives the next input character as a one character string.\r\nGET$#<channel> gives the next string from the file.");
  } else if (!strcmp(cmd, "GOSUB")) {
    emulate_printf("GOSUB <line number>: call subroutine at line number.");
  } else if (!strcmp(cmd, "GOTO")) {
    emulate_printf("GOTO <line number>: go to line number.");
  } else if (!strcmp(cmd, "HELP")) {
    emulate_printf("This command gives help on usage of the interpreter.");
  } else if (!strcmp(cmd, "HIMEM")) {
    emulate_printf("This pseudo-variable reads or sets the address of the end of BASIC's memory.");
  } else if (!strcmp(cmd, "IF")) {
    emulate_printf("Single-line if: IF <expression> [THEN] <statements> [ELSE <statements>].\r\nMulti-line if: IF <expression> THEN<newline>\r\n                  <lines>\r\noptional:      ELSE <lines>\r\nmust:          ENDIF");
  } else if (!strcmp(cmd, "INKEY")) {
    emulate_printf("INKEY 0 to 32767: function waits <number> centiseconds to read character.\r\nINKEY -127 to -1: function checks specific key for TRUE|FALSE.\r\nINKEY -255 to -128: Not supported.\r\nINKEY -256: function gives operating system number.");
  } else if (!strcmp(cmd, "INKEY$")) {
    emulate_printf("Equivalent to CHR$(INKEY...): see INKEY.");
  } else if (!strcmp(cmd, "INPUT")) {
    emulate_printf("INPUT [LINE]['|TAB|SPC][\"display string\"][,|;]<variable>: input from user.\r\nINPUT#<channel>,<list of variables>: input data from open file.");
  } else if (!strcmp(cmd, "INSTALL")) {
    emulate_printf("This command permanently installs a library: see LIBRARY.");
  } else if (!strcmp(cmd, "INSTR(")) {
    emulate_printf("INSTR(<string>,<substring>[,<start position>]): find sub-string position.");
  } else if (!strcmp(cmd, "INT")) {
    emulate_printf("This function gives the nearest integer less than or equal to the number.");
  } else if (!strcmp(cmd, "LEFT$(")) {
    emulate_printf("LEFT$(<string>,<number>): gives leftmost number of characters from string.\r\nLEFT$(<string>): gives leftmost LEN-1 characters.\r\nLEFT$(<string variable>[,<count>])=<string>: overwrite characters from start.");
  } else if (!strcmp(cmd, "LEN")) {
    emulate_printf("This function gives the length of a string.");
  } else if (!strcmp(cmd, "LET")) {
    emulate_printf("Optional part of assignment.");
  } else if (!strcmp(cmd, "LIBRARY")) {
    emulate_printf("LIBRARY <string>; functions and procedures of the named program can be used.");
  } else if (!strcmp(cmd, "LINE")) {
    emulate_printf("Draw a line: LINE x1,y1,x2,y2\r\nPart of INPUT LINE or LINE INPUT statement.");
  } else if (!strcmp(cmd, "LIST")) {
    emulate_printf("This command lists the program.\r\nLIST [<line number>][,[<line number]]: List [section of] program.\r\nSee also LISTO which controls how LIST shows lines.");
  } else if (!strcmp(cmd, "LISTIF")) {
    emulate_printf("LISTIF <pattern>: lists lines of the program that match <pattern>.");
  } else if (!strcmp(cmd, "LISTO")) {
    emulate_printf("LISTO <option number>. Bits mean:-\r\n0: space after line number.\r\n1: indent structure\r\n2: split lines at :\r\n3: don't list line number\r\n4: list tokens in lower case\r\n5: pause after showing 20 lines");
#ifdef DEBUG
    emulate_printf("\r\n\nAdditional debug bits are offered:\r\n 8: Show debugging output (&100)\r\n 9: Show tokenised lines on input plus addresses on listings (&200)\r\n10: List addresses of variables when created + on LVAR (&400)\r\n11: Show allocation/release of memory for strings (&800)\r\n12: Show string heap statistics (&1000)\r\n13: Show structures pushed and popped from stack (&2000)\r\n14: Show in detail items pushed and popped from stack (&4000)\r\n15: Show which functions are called (incomplete) (&8000)");
#endif
  } else if (!strcmp(cmd, "LN")) {
    emulate_printf("This function gives the natural logarithm (base e) of a number(<factor>).");
  } else if (!strcmp(cmd, "LOAD")) {
    emulate_printf("This command loads a new program.");
  } else if (!strcmp(cmd, "LOCAL")) {
    emulate_printf("LOCAL <list of variables>: Makes things private to function or procedure\r\nLOCAL DATA: save DATA pointer on stack.\r\nLOCAL ERROR: save error control status on stack.");
  } else if (!strcmp(cmd, "LOG")) {
    emulate_printf("This function gives the common logarithm (base 10) of a number(<factor>).");
  } else if (!strcmp(cmd, "LOMEM")) {
    emulate_printf("This pseudo-variable reads or sets the address of the start of the variables.");
  } else if (!strcmp(cmd, "LVAR")) {
    emulate_printf("This command lists all variables in use.");
  } else if (!strcmp(cmd, "MID$(")) {
    emulate_printf("MID$(<string>,<position>): gives all of string starting from position.\r\nMID$(<string>,<position>,<count>): gives some of string from position.\r\nMID$(<string variable>,<position>[,<count>])=<string>: overwrite characters.");
  } else if (!strcmp(cmd, "MOD")) {
    emulate_printf("Remainder after integer division between two integers. Priority 3.\r\nThe MOD function gives the square root of the sum of the squares of all the\r\nelements in a numeric array.");
  } else if (!strcmp(cmd, "MODE")) {
    emulate_printf("MODE <number>|<string>: set screen mode.\r\nMODE <width>,<height>,<bpp>[,<framerate>]: set screen mode.\r\nMODE <width>,<height>,<modeflags>,<ncolour>,<log2bpp>[,<framerate>]: set screen\r\nmode.\r\nAs a function MODE gives the current screen mode.");
  } else if (!strcmp(cmd, "MOUSE")) {
    emulate_printf("MOUSE x,y,z[,t]: sets x,y to mouse position; z to button state [t to time].\r\nMOUSE OFF: turn mouse pointer off.\r\nMOUSE ON [a]: sets mouse pointer 1 [or a].\r\nMOUSE TO x,y: positions mouse and pointer at x,y.\r\nThe following three are not supported and are ignored:\r\nMOUSE COLOUR a,r,g,b: set mouse palette entry for a to r, g, b physical colour.\r\nMOUSE RECTANGLE x,y,width,height: constrain mouse movement to inside rectangle.\r\nMOUSE STEP a[,b]: sets mouse step multiplier to a,a [or a,b].");
  } else if (!strcmp(cmd, "MOVE")) {
    emulate_printf("MOVE [BY] x,y: graphics move to [relative by] x,y.");
  } else if (!strcmp(cmd, "NEXT")) {
    emulate_printf("NEXT [<variable>[,<variable>]^]: closes one or several FOR..NEXT structures.");
  } else if (!strcmp(cmd, "NEW")) {
    emulate_printf("NEW [<size>]: This command erases the current program.\r\nIf <size> specified, set the BASIC workspace size in bytes.");
  } else if (!strcmp(cmd, "NOT")) {
    emulate_printf("This function gives the number with all bits inverted (0 and 1 exchanged).");
  } else if (!strcmp(cmd, "OF")) {
    emulate_printf("Part of the CASE <expression> OF statement.\r\nAlso part of COLOUR and GCOL statements.");
  } else if (!strcmp(cmd, "OFF")) {
    emulate_printf("OFF: turn cursor off.\r\nPart of TRACE OFF, ON ERROR OFF statements.");
  } else if (!strcmp(cmd, "OLD")) {
    emulate_printf("This command is not supported.");
  } else if (!strcmp(cmd, "ON")) {
    emulate_printf("ON: cursor on.\r\nON ERROR [LOCAL|OFF]: define error handler.\r\nON <expression> GOTO|GOSUB|PROC.... ELSE: call from specified list item.");
  } else if (!strcmp(cmd, "OPENIN")) {
    emulate_printf("Open for Input: the function opens a file for input.");
  } else if (!strcmp(cmd, "OPENOUT")) {
    emulate_printf("Open for Output: the function opens a file for output.");
  } else if (!strcmp(cmd, "OPENUP")) {
    emulate_printf("Open for Update: the function opens a file for input and output.\r\nThis function can also open a TCP network socket, using the filename syntax of\r\nOPENUP(\"ip0:<hostname>:<port>\") - use ip4: for IPv4 only or ip6: for IPv6 only.");
  } else if (!strcmp(cmd, "OR")) {
    emulate_printf("Bitwise logical OR between two integers. Priority 7.");
  } else if (!strcmp(cmd, "ORIGIN")) {
    emulate_printf("ORIGIN x,y: sets x,y as the new graphics 0,0 point.");
  } else if (!strcmp(cmd, "OSCLI")) {
    emulate_printf("OSCLI <string> [TO <variable>$]: give string to OS Command Line Interpreter.");
  } else if (!strcmp(cmd, "OTHERWISE")) {
    emulate_printf("Identifies case exceptional section at start of line. See CASE.");
  } else if (!strcmp(cmd, "OVERLAY")) {
    emulate_printf("OVERLAY <string array>: Not implemented in Matrix Brandy.\r\n");
  } else if (!strcmp(cmd, "PAGE")) {
    emulate_printf("This pseudo-variable reads or sets the address of the start of the program.");
  } else if (!strcmp(cmd, "PI")) {
    emulate_printf("This function gives the value of 'pi' 3.1415926535.");
  } else if (!strcmp(cmd, "PLOT")) {
    emulate_printf("PLOT n,x,y: graphics operation n.");
  } else if (!strcmp(cmd, "POINT")) {
    emulate_printf("POINT [BY] x,y: set pixel at [relative to] x,y.\r\nPOINT TO x,y: Not supported.\r\nPOINT(x,y): function gives the logical colour number of the pixel at x, y.");
  } else if (!strcmp(cmd, "POS")) {
    emulate_printf("This function gives the x-coordinate of the text cursor.");
  } else if (!strcmp(cmd, "PRINT")) {
    emulate_printf("PRINT ['|TAB|SPC][\"display string\"][<expression>][;] print items in fields\r\ndefined by @%% - see HELP @%%\r\nPRINT#<channel>,<list of expressions>: print data to open file.");
  } else if (!strcmp(cmd, "PROC")) {
    emulate_printf("Call a procedure with PROCfred(x,y); define one with DEF PROCfred(a,b).");
  } else if (!strcmp(cmd, "PTR")) {
    emulate_printf("This function gives the position in a file: PTR#<channel>.\r\nPTR#<channel>=<expression> sets the position in a file.");
  } else if (!strcmp(cmd, "QUIT")) {
    emulate_printf("QUIT [<expression>]: leave the interpreter (passing optional return code\r\n<expression>).\r\nAs a function QUIT gives TRUE if BASIC was entered with a -quit option.");
  } else if (!strcmp(cmd, "RAD")) {
    emulate_printf("This function gives the value in radians of a number in degrees.");
  } else if (!strcmp(cmd, "READ")) {
    emulate_printf("READ <list of variables>: read the variables in turn from DATA statements.");
  } else if (!strcmp(cmd, "RECTANGLE")) {
    emulate_printf("RECTANGLE [FILL] xlo,ylo,width[,height] [TO xlo,ylo]:\r\nDraw a rectangle outline [solid] or copy [move] the rectangle.");
  } else if (!strcmp(cmd, "REM")) {
    emulate_printf("Ignores rest of line.");
  } else if (!strcmp(cmd, "RENUMBER")) {
    emulate_printf("This command renumbers the lines in the program:\r\nRENUMBER [<base number>[,<step size>]]");
  } else if (!strcmp(cmd, "REPEAT")) {
    emulate_printf("REPEAT: start of REPEAT..UNTIL structure; statement delimiter not required.");
  } else if (!strcmp(cmd, "REPORT")) {
    emulate_printf("REPORT: print last error message.\r\nREPORT$ function gives string of last error string.");
  } else if (!strcmp(cmd, "RESTORE")) {
    emulate_printf("RESTORE [+][<number>]: restore the data pointer to first or given line, or move\r\nforward <number> lines from the start of the next line.\r\nRESTORE DATA: restore DATA pointer from stack.\r\nRESTORE ERROR: restore error control status from stack.\r\nRESTORE LOCAL: Restore variables declared LOCAL to their global state.");
  } else if (!strcmp(cmd, "RETURN")) {
    emulate_printf("End of subroutine. See GOSUB");
  } else if (!strcmp(cmd, "RIGHT$(")) {
    emulate_printf("RIGHT$(<string>,<number>): gives rightmost number of characters from string.\r\nRIGHT$(<string>): gives rightmost character.\r\nRIGHT$(<string variable>[,<count>])=<string>: overwrite characters at end.");
  } else if (!strcmp(cmd, "RND")) {
    emulate_printf("RND: function gives a random integer.\r\nRND(n) where n<0: initialise random number generator based on n.\r\nRND(0): last RND(1) value.\r\nRND(1): random real 0..1.\r\nRND(n) where n>1: random value between 1 and INT(n).");
  } else if (!strcmp(cmd, "RUN")) {
    emulate_printf("Clear variables and start execution at beginning of program.");
  } else if (!strcmp(cmd, "SAVE")) {
    emulate_printf("This command saves the current program.");
  } else if (!strcmp(cmd, "SGN")) {
    emulate_printf("This function gives the values -1, 0, 1 for negative, zero, positive numbers.");
  } else if (!strcmp(cmd, "SIN")) {
    emulate_printf("This function gives the sine of a number (<factor>).");
  } else if (!strcmp(cmd, "SOUND")) {
    emulate_printf("SOUND <channel>,<amplitude>,<pitch>,<duration>[,<start beat>]: make a sound.\r\nSOUND ON|OFF: enable|disable sounds.");
  } else if (!strcmp(cmd, "SPC")) {
    emulate_printf("In PRINT or INPUT statements, prints out n spaces: PRINT SPC(10).");
  } else if (!strcmp(cmd, "SQR")) {
    emulate_printf("This function gives the square root of a number (<factor>).");
  } else if (!strcmp(cmd, "STEP")) {
    emulate_printf("Part of the FOR..TO..STEP structure.");
  } else if (!strcmp(cmd, "STEREO")) {
    emulate_printf("STEREO <channel>,<position>: set the stereo position for a channel.");
  } else if (!strcmp(cmd, "STOP")) {
    emulate_printf("Stop program.");
  } else if (!strcmp(cmd, "STR$")) {
    emulate_printf("STR$[~]<number>: gives string representation [in hex] of a number (<factor>).");
  } else if (!strcmp(cmd, "STRING$(")) {
    emulate_printf("STRING$(<number>,<string>): gives string replicated the number of times.");
  } else if (!strcmp(cmd, "SUM")) {
    emulate_printf("This function gives the sum of all elements in an array.\r\nSUMLEN gives the total length of all elements of a string array.");
  } else if (!strcmp(cmd, "SWAP")) {
    emulate_printf("SWAP <variable>,<variable>: exchange the contents.");
  } else if (!strcmp(cmd, "SYS")) {
    emulate_printf("The SYS statement calls the operating system:\r\nSYS <expression> [,<expression>]^ [TO <variable>[,<variable>]^[;<variable>]]\r\nIn Matrix Brandy, only built-in functionality is supported.\r\nNote that Matrix Brandy's SYS interface can return 64-bit values especially on\r\n64-bit hardware so programs should store such values in 64-bit integers.");
  } else if (!strcmp(cmd, "TAB(")) {
    emulate_printf("In PRINT or INPUT statements:\r\nTAB to column n: PRINT TAB(10)s$.\r\nTAB to screen position x,y: PRINT TAB(10,20)s$.");
  } else if (!strcmp(cmd, "TAN")) {
    emulate_printf("This function gives the tangent of a number (<factor>).");
  } else if (!strcmp(cmd, "TEMPO")) {
    emulate_printf("TEMPO <expression>: set the sound microbeat tempo.\r\nAs a function TEMPO gives the current microbeat tempo.");
  } else if (!strcmp(cmd, "TEXTLOAD")) {
    emulate_printf("This command loads a new program, converting from text form if required.");
  } else if (!strcmp(cmd, "TEXTSAVE")) {
    emulate_printf("This command saves the current program as text [with a LISTO option].\r\nTEXTSAVE[O <expression>,] <string>");
  } else if (!strcmp(cmd, "THEN")) {
    emulate_printf("Part of the IF..THEN structure. If THEN is followed by a newline it introduces a\r\nmulti-line structured IF..THEN..ELSE..ENDIF.");
  } else if (!strcmp(cmd, "TIME")) {
    emulate_printf("This pseudo-variable reads or sets the computational real time clock.\r\nTIME$ reads the display version of the clock. Setting TIME$ is ignored.");
  } else if (!strcmp(cmd, "TINT")) {
    emulate_printf("TINT a,t: set the tint for COLOUR|GCOL|fore|back a to t in 256 colour modes.\r\nAlso available as a suffix to GCOL and COLOUR.\r\nAs a function TINT(x,y) gives the tint of a point in 256 colour modes.");
  } else if (!strcmp(cmd, "TO")) {
    emulate_printf("Part of FOR..TO...");
  } else if (!strcmp(cmd, "TOP")) {
    emulate_printf("This function gives the address of the end of the program.");
  } else if (!strcmp(cmd, "TRACE")) {
    emulate_printf("TRACE [STEP] ON|OFF|PROC|FN|ENDPROC|<number>: trace [in single step mode] on or\r\noff, or procedure and function calls, or procedure/function exit points, or\r\nlines below <number>.\r\nTRACE TO <string>: send all output to stream <string>\r\nTRACE CLOSE: close stream output. Expression: TRACE gives handle of the stream.");
  } else if (!strcmp(cmd, "TRUE")) {
    emulate_printf("This function gives the logical value 'true' i.e. -1.");
  } else if (!strcmp(cmd, "UNTIL")) {
    emulate_printf("UNTIL <expression>: end of REPEAT..UNTIL structure.");
  } else if (!strcmp(cmd, "USR")) {
    emulate_printf("This function gives the value returned by a machine code routine.\r\nIn Matrix Brandy, only calls to selected BBC Micro OS vectors are supported.");
  } else if (!strcmp(cmd, "VAL")) {
    emulate_printf("This function gives the numeric value of a textual string e.g. VAL\"23\".");
  } else if (!strcmp(cmd, "VDU")) {
    emulate_printf("VDU <number>[;|][,<number>[;|]]: list of values to be sent to vdu.\r\n, only - 8 bits.\r\n; 16 bits.\r\n| 8 bytes of zeroes.\r\nAs a function VDU x gives the value of the specified vdu variable.");
  } else if (!strcmp(cmd, "VOICE")) {
    emulate_printf("VOICE <channel>,<string>: assign a named sound algorithm to the voice channel.");
  } else if (!strcmp(cmd, "VOICES")) {
    emulate_printf("VOICES <expression>: set the number of sound voice channels.");
  } else if (!strcmp(cmd, "VPOS")) {
    emulate_printf("This function gives the y-coordinate of the text cursor.");
  } else if (!strcmp(cmd, "WAIT")) {
    emulate_printf("Wait for vertical sync.\r\nWAIT n: pause for n centiseconds.");
  } else if (!strcmp(cmd, "WHEN")) {
    emulate_printf("WHEN <expression>[,<expression>]^: identifies case section at start of line.\r\nSee CASE.");
  } else if (!strcmp(cmd, "WHILE")) {
    emulate_printf("WHILE <expression>: start of WHILE..ENDWHILE structure.");
  } else if (!strcmp(cmd, "WIDTH")) {
    emulate_printf("WIDTH <expression>: set width of output.");
  } else if (!strcmp(cmd, "@%")) {
    emulate_printf("This pseudo-variable reads or sets the number print format:\r\nPRINT @%% gives a number, but LVAR and @%= use strings\r\n\"G<number>.<number>\" general format field and number of digits\r\n\"E<number>.<number>\" exponent format field and number of digits\r\n\"F<number>.<number>\" fixed format field and number of digits after '.'\r\nAll parts optional. , or . in the above prints , or . as the decimal point.\r\nLeading + means @%% applies to STR$ also.");
  } else if (!strcmp(cmd, ".")) {
    emulate_printf("Help is available on the following keywords:\r\n\
ABS       ACS       ADVAL     AND       ASC       ASN       ATN       AUTO\r\n\
APPEND    BEAT      BEATS     BGET      BPUT      CALL      CASE      CHAIN\r\n\
CHR$      CIRCLE    CLEAR     CLG       CLOSE     CLS       COLOUR    COLOR\r\n\
COS       COUNT     CRUNCH    DATA      DEF       DEG       DELETE    DIM\r\n\
DIV       DRAW      EDIT      ELLIPSE   ELSE      END       ENDCASE   ENDIF\r\n\
ENDPROC   ENDWHILE  ENVELOPE  EOF       EOR       ERL       ERR       ERROR\r\n\
EVAL      EXP       EXT       FALSE     FILL      FN        FOR       GCOL\r\n\
GET       GET$      GOSUB     GOTO      HELP      HIMEM     IF        INKEY\r\n\
INKEY$    INPUT     INSTALL   INSTR(    INT       LEFT$(    LEN       LET\r\n\
LIBRARY   LINE      LIST      LISTIF    LN        LOAD      LOCAL     LOG\r\n\
LOMEM     LVAR      MID$(     MOD       MODE      MOUSE     MOVE      NEXT\r\n\
NEW       NOT       OF        OFF       OLD       ON        OPENIN    OPENOUT\r\n\
OPENUP    OR        ORIGIN    OSCLI     OTHERWISE OVERLAY   PAGE      PI\r\n\
PLOT      POINT     POS       PRINT     PROC      PTR       QUIT      RAD\r\n\
READ      RECTANGLE REM       RENUMBER  REPEAT    REPORT    RESTORE   RETURN\r\n\
RIGHT$(   RND       RUN       SAVE      SGN       SIN       SOUND     SPC\r\n\
SQR       STEP      STEREO    STOP      STR$      STRING$(  SUM       SWAP\r\n\
SYS       TAB(      TAN       TEMPO     TEXTLOAD  TEXTSAVE  THEN      TIME\r\n\
TINT      TO        TOP       TRACE     TRUE      UNTIL     USR       VAL\r\n\
VDU       VOICE     VOICES    VPOS      WAIT      WHEN      WHILE     WIDTH");
  } else {
    emulate_printf("\r\nNo help available for '%s'", cmd);
  }
  emulate_printf("\r\n");
}
#endif
