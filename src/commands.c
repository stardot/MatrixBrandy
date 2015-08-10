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

static int32 lastaddr = 0;

static char editname[20];       /* Default Name of editor invoked by 'EDIT' command */

/*
** 'get_number' is called to evaluate an expression that returns an
** integer value. It is used by the functions handling the various
** Basic commands in this file
*/
static int32 get_number(void) {
  factor();
  switch (get_topitem()) {
  case STACK_INT:
    return pop_int();
  case STACK_FLOAT:
    return TOINT(pop_float());
  default:
    error(ERR_TYPENUM);
  }
  return 0;
}

/*
** 'get_pair' is called to return a pair of values. It assumes that
** 'basicvars.current' points at the first item after the token for the
** command for which it is being used
*/
static void get_pair(int32 *first, int32 *second, int32 firstdef, int32 secondef) {
  int32 low, high = 0;
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
  clear_program();
  init_expressions();
}

/*
** 'exec_old' checks to see if there is a program still in memory and
** attempts to recover it
*/
static void exec_old(void) {
  basicvars.current++;
  check_ateol();
  if (basicvars.runflags.running) error(ERR_COMMAND);   /* Cannot edit a running program */
  recover_program();
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
    basicvars.stringwork[len] = NUL;
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
  while (*p != NUL) p++;        /* Find the end of the string */
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
}

/*
** 'delete' is called to delete a range of lines from the program
*/
static void delete(void) {
  int32 low, high;
  if (basicvars.misc_flags.badprogram) error(ERR_BADPROG);
  if (basicvars.runflags.running) error(ERR_COMMAND);   /* Cannot modify a running program */
  basicvars.current++;
  get_pair(&low, &high, 0, MAXLINENO);
  check_ateol();
  if (low<0 || low>MAXLINENO || high<0 || high>MAXLINENO) error(ERR_LINENO);
  delete_range(low, high);
}

/*
** 'renumber' renumbers a Basic program
*/
static void renumber(void) {
  int32 start, step;
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
  int32 lowaddr, highaddr;
  which = *basicvars.current;
  basicvars.current++;
  get_pair(&lowaddr, &highaddr, lastaddr, lastaddr+0x40);
  check_ateol();
  if (highaddr == lowaddr) highaddr = lowaddr+0x40;
  if (which == TOKEN_LISTB)
    show_byte(lowaddr, highaddr);
  else {
    show_word(lowaddr, highaddr);
  }
  lastaddr = highaddr;
}

/*
** 'list_program' lists the source of a Basic  program
*/
static void list_program(void) {
  int32 lowline, highline, count;
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
          if (basicvars.escape) error(ERR_ESCAPE);
          count = emulate_get();
          switch (count) {
          case ' ':
            count = 0;
            paused = FALSE;
            break;
          case CR: case LF:
            count = PAGESIZE-1; /* A hack, but it does the job */
            paused = FALSE;
            break;
          case ESC:
            paused = more = FALSE;
          }
        } while (paused);
        emulate_printf("\r          \r");       /* Overwrite 'more' */
      }
    }
    if (basicvars.escape) error(ERR_ESCAPE);
  }
}

/*
** 'list_hexline' lists a line as a hex dump
*/
static void list_hexline(void) {
  int32 length, theline;
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
  show_byte(where-basicvars.offbase, where-basicvars.offbase+length);
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
  while (*p != NUL && *p != '>') p++;   /* Look for a '>' */
  if (*p == NUL) return NIL;            /* Did not find one so give up */
  p = skip(p+1);
  if (*p == NUL) return NIL;            /* There is nothing after the '>' */
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
      if (basicvars.program[0] == NUL)
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
  write_text(np);
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
  write_text(np);
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
  clear_varlists();
  clear_strings();
  clear_heap();
  read_basic(np);
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
  basicvars.current++;
  check_ateol();
  show_options();
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
  _kernel_osfile_block now, then;
  if (basicvars.runflags.running) error(ERR_COMMAND);   /* Cannot edit a running program */
  basicvars.listo_copy = basicvars.list_flags;
  if (basicvars.misc_flags.validedit) basicvars.list_flags = basicvars.edit_flags;
  basicvars.list_flags.lower = FALSE;   /* Changing keyword to lower case is useless here */
  basicvars.list_flags.expand = FALSE;  /* So is adding extra blanks */
  if (!secure_tmpnam(tempname)) {
    error(ERR_EDITFAIL, strerror (errno));
    return;
  }
  reset_indent();
  write_text(tempname);
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
#ifdef TARGET_DJGPP
  char *p;
#endif
  if (basicvars.runflags.running) error(ERR_COMMAND);   /* Cannot edit a running program */
  if (!secure_tmpnam(tempname)) {
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
  write_text(tempname);
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
** 'amend_line' is called to alter one line in a program by copying
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
  tokenize(basicvars.stringwork, thisline, HASLINE);
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
  longjmp(basicvars.restart, 1);
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

/*
** 'exec_command' handles all the Basic statement types that are normally
** only run as immediate commands. Commands that modify the program such
** as 'DELETE' can only be used at the command line. The command functions
** are called with 'current' pointing at the command's token. 'current'
** should be left pointing at the end of the statement. The functions
** should also check that the statement ends properly. All errors are
** handled by 'setjmp' and 'longjmp' in the normal way
*/
void exec_command(void) {
  basicvars.current++;  /* Point at command type token */
  switch (*basicvars.current) {
  case TOKEN_NEW:
    exec_new();
    break;
  case TOKEN_OLD:
    exec_old();
    break;
  case TOKEN_LOAD: case TOKEN_TEXTLOAD:
    load_program();
    break;
  case TOKEN_SAVE: case TOKEN_TEXTSAVE:
    save_program();
    break;
  case TOKEN_SAVEO: case TOKEN_TEXTSAVEO:
    saveo_program();
    break;
  case TOKEN_INSTALL:
    install_library();
    break;
  case TOKEN_LIST:
    list_program();
    break;
  case TOKEN_LISTB: case TOKEN_LISTW:
    show_memory();
    break;
  case TOKEN_LISTL:
    list_hexline();
    break;
  case TOKEN_LISTIF:
    list_if();
    break;
  case TOKEN_LISTO:
    set_listopt();
    break;
  case TOKEN_LVAR:
    list_vars();
    break;
  case TOKEN_RENUMBER:
    renumber();
    break;
  case TOKEN_DELETE:
    delete();
    break;
  case TOKEN_HELP:
    print_help();
    break;
  case TOKEN_EDIT: case TOKEN_TWIN:
    exec_editor();
    break;
  case TOKEN_EDITO: case TOKEN_TWINO:
    exec_edito();
    break;
  case TOKEN_CRUNCH:
    exec_crunch();
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
  if (editor != NIL)
    strcpy(editname, editor);
  else {
    strcpy(editname, DEFAULT_EDITOR);
  }
}


