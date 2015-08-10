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
**	This file contains the main interpreter command loop
*/

#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include <ctype.h>
#include "common.h"
#include "target.h"
#include "basicdefs.h"
#include "tokens.h"
#include "errors.h"
#include "heap.h"
#include "editor.h"
#include "commands.h"
#include "statement.h"
#include "fileio.h"
#include "emulate.h"
#include "keyboard.h"
#include "screen.h"
#include "miscprocs.h"

/* #define DEBUG */

workspace basicvars;		/* This contains all the important interpreter variables */

/* Forward references */

static void init1(void);
static void init2(void);
static void check_cmdline(int, char *[]);
static void run_interpreter(void);

static char inputline[INPUTLEN];	/* Last line read */
static char *loadfile;			/* Pointer to name of file to load when interpreter starts */
static int32 worksize;			/* Initial workspace size */

static cmdarg *arglast;			/* Pointer to end of command line argument list */

static struct loadlib {char *name; struct loadlib *next;} *liblist, *liblast;

/*
** 'main' just starts things going. Control does not returns here after
** 'run_interpreter' is called. The program finishes when 'exec_quit'
** (in statement.c) is invoked. 'exec_quit' handles the 'QUIT' command
*/
int main(int argc, char *argv[]) {
  init1();
  check_cmdline(argc, argv);
  init2();
  run_interpreter();
  return EXIT_FAILURE;
}

/*
** add_arg - Add a command line argument to the list accessible
** via the Basic ARGV$ function.
** The first time this function is called it is used to create the
** program name entry in the argument list. This is not included
** in the value of argcount, which gives the number of command
** line arguments. It is the value returned by ARGC.
*/
static void add_arg(char *p) {
  cmdarg *ap;
  ap = malloc(sizeof(cmdarg));
  ap->argvalue = p;
  ap->nextarg = NIL;
  if (arglast == NIL)
    basicvars.arglist = ap;
  else {
    basicvars.argcount++;
    arglast->nextarg = ap;
  }
  arglast = ap;
}

/*
** 'init1' initialises the interpreter
*/
static void init1(void) {
  basicvars.installist = NIL;
  basicvars.retcode = 0;
  basicvars.list_flags.space = FALSE;	/* Set initial listing options */
  basicvars.list_flags.indent = FALSE;
  basicvars.list_flags.split = FALSE;
  basicvars.list_flags.noline = FALSE;
  basicvars.list_flags.lower = FALSE;
  basicvars.list_flags.expand = FALSE;

  basicvars.debug_flags.debug = FALSE;	/* Set interpreter debug options */
  basicvars.debug_flags.tokens = FALSE;
  basicvars.debug_flags.variables = FALSE;
  basicvars.debug_flags.strings = FALSE;
  basicvars.debug_flags.stats = FALSE;
  basicvars.debug_flags.stack = FALSE;
  basicvars.debug_flags.allstack = FALSE;

  basicvars.runflags.inredir = FALSE;           /* Input is being taken from the keyboard */
  basicvars.runflags.outredir = FALSE;          /* Output is going to the screen */
  basicvars.runflags.loadngo = FALSE;		/* Do not start running program immediately */
  basicvars.runflags.quitatend = FALSE;		/* Do not exit from interpreter when program finishes */
  basicvars.runflags.start_graphics = FALSE;	/* Do not start in graphics mode */
  basicvars.runflags.ignore_starcmd = FALSE;	/* Do not ignore built-in '*' commands */
  basicvars.runflags.flag_cosmetic = TRUE;	/* Flag all unsupported features as errors */

  basicvars.misc_flags.trapexcp = TRUE;		/* Trap exceptions */
  basicvars.misc_flags.validedit = FALSE;	/* Contents of edit_flags are not valid */

  basicvars.loadpath = NIL;
  basicvars.argcount = 0;
  basicvars.arglist = NIL;		/* List of command line arguments */
  arglast = NIL;			/* End of list of command line arguments */

  liblist = liblast = NIL;		/* List of libraries to load when interpreter starts */
  worksize = 0;				/* Use default workspace size */

/*
 * Add dummy first parameter for Basic program command line.
 * This is the Basic program's name
 */
  add_arg("");
}

/*
** 'init2' finishes initialising the interpreter
*/
static void init2(void) {
  if (!init_heap() || !init_workspace(worksize)) {
    cmderror(CMD_NOMEMORY);	/* Not enough memory to run interpreter */
    exit(EXIT_FAILURE);
  }
  if (!init_emulation() || !init_keyboard() || !init_screen()) {
    cmderror(CMD_INITFAIL);	/* Initialisation failed */
    exit_interpreter(EXIT_FAILURE);	/* End run */
  }
  init_commands();
  init_fileio();
  clear_program();
  basicvars.current = NIL;
  basicvars.misc_flags.validsaved = FALSE;		/* Want this to be 'FALSE' when the interpreter first starts */
  init_interpreter();
}

/*
** 'check_cmdline' is called to parse the command line.
** Note that any unrecognised parameters are assumed to be destined
** for the Basic program
*/
static void check_cmdline(int argc, char *argv[]) {
  int n;
  char optchar, *p;
  loadfile = NIL;
  n = 1;
  while (n<argc) {
    p = argv[n];
    if (*p=='-') {	/* Got an option */
      optchar = tolower(*(p+1));	/* Get first character of option name */
      if (optchar=='g')		/* -graphics */
        basicvars.runflags.start_graphics = TRUE;
      else if (optchar=='h') {		/* -help */
        show_help();
        exit(0);
      }
      else if (optchar == 'c' || optchar == 'q' || (optchar == 'l' && tolower(*(p+2)) == 'o')) {	/* -chain, -quit or -load */
        n++;
        if (n==argc)
          cmderror(CMD_NOFILE, p);	/* Filename missing */
        else if (loadfile!=NIL)
          cmderror(CMD_FILESUPP);	/* Filename already supplied */
        else {
          loadfile = argv[n];
          if (optchar=='c')		/* -chain */
            basicvars.runflags.loadngo = TRUE;
          else if (optchar=='q') {	/* -quit */
            basicvars.runflags.quitatend = basicvars.runflags.loadngo = TRUE;
          }
        }
      }
      else if (optchar=='i' && tolower(*(p+2))=='g')	/* -ignore  Ignore cosmetic errors */
        basicvars.runflags.flag_cosmetic = FALSE;
      else if (optchar=='l' && tolower(*(p+2))=='i') {	/* -lib */
        n++;
        if (n==argc)
          cmderror(CMD_NOFILE, p);	/* Filename missing */
        else {		/* Add name to list of libraries to load */
          struct loadlib *p = malloc(sizeof(struct loadlib));
          if (p==NIL)
            cmderror(CMD_NOMEMORY);
          else {
            p->name = argv[n];
            p->next = NIL;
            if (liblast==NIL)
              liblist = p;
            else {
              liblast->next = p;
            }
            liblast = p;
          }
        }
      }
      else if (optchar == 'n' && tolower(*(p+2))=='o')	/* -nostar  Ignore '*' commands */
        basicvars.runflags.ignore_starcmd = TRUE;
      else if (optchar=='p') {		/* -path */
        n++;
        if (n==argc)
          cmderror(CMD_NOFILE, p);	/* Directory list missing */
        else {		/* Set up the path list */
          if (basicvars.loadpath!=NIL) free(basicvars.loadpath);	/* Discard existing list */
          basicvars.loadpath = malloc(strlen(argv[n])+1);	/* +1 for the NUL */
          if (basicvars.loadpath==NIL) {	/* No memory available */
            cmderror(CMD_NOMEMORY);
            exit(EXIT_FAILURE);
          }
          strcpy(basicvars.loadpath, argv[n]);
        }
      }
      else if (optchar=='s') {		/* -size */
        n++;
        if (n==argc)
          cmderror(CMD_NOSIZE, p);		/* Workspace size missing */
        else {
          char *sp;
          worksize = CAST(strtol(argv[n], &sp, 10), int32);	/* Fetch workspace size (n.b. no error checking) */
          if (tolower(*sp)=='k')	/* Size is in kilobytes */
            worksize = worksize*1024;
          else if (tolower(*sp)=='m') {	/* Size is in megabytes */
            worksize = worksize*1024*1024;
          }
        }
      }
      else if (optchar=='!')		/* -! - Don't initialise signal handlers */
        basicvars.misc_flags.trapexcp = FALSE;
      else {
/* Any unrecognised options are assumed to be for the Basic program */
        add_arg(argv[n]);
      }
    }
    else {	/* Name of file to run supplied */
      if (loadfile==NIL) {
        loadfile = p;	/* Make note of name of file to load */
        basicvars.runflags.loadngo = TRUE;
      }
      else {	/* Assume anything else is for the Basic program */
        add_arg(argv[n]);
      }
    }
    n++;
  }

/* Update program's name in Basic's command line list */
  if (loadfile != NIL) {
    basicvars.arglist->argvalue = loadfile;
  }
}

/*
** 'read_command' reads the next command
*/
static void read_command(void) {
  boolean ok;
  if (!basicvars.runflags.inredir) emulate_vdu('>');
  ok = read_line(inputline, INPUTLEN);
  if (!ok) exit_interpreter(EXIT_SUCCESS);	/* EOF hit - Tidy up and end run */
}

/*
** 'interpret_line' either calls the editor or tries to run what is in
** 'thisline' as a sequence of commands
*/
static void interpret_line(void) {
  if (get_lineno(thisline)==NOLINENO)
    exec_thisline();
  else {
    edit_line();
  }
}

/*
** 'load_libraries' loads the libraries specified on the command line
** via the option '-lib'. In the event of an error control either
** passes to the command loop in 'run_interpreter' or the program
** ends, depending on the setting of 'quitatend' flag
*/
static void load_libraries(void) {
  struct loadlib *p = liblist;
  do {
    read_library(p->name, INSTALL_LIBRARY);	/* Read library and install it */
    p = p->next;
  } while (p!=NIL);
}

/*
** 'run_interpreter' is the main command loop for the interpreter.
** It reads commands and executes then. Control is also returned
** here in the event of an error by means of a 'longjmp' to
** 'basicvars.restart'
*/
static void run_interpreter(void) {
  if (setjmp(basicvars.restart)==0) {
    if (!basicvars.runflags.loadngo && !basicvars.runflags.outredir) announce();	/* Say who we are */
    init_errors();	/* Set up the signal handlers */
    if (liblist!=NIL) load_libraries();
    if (loadfile!=NIL) {	/*  Name of program to load was given on command line */
      read_basic(loadfile);
      strcpy(basicvars.program, loadfile);	/* Save the name of the file */
      if (basicvars.runflags.loadngo) run_program(basicvars.start);	/* Start program execution */
    }
  }
/* Control passes to this point in the event of an error via a 'longjmp' */
  while (TRUE) {
    read_command();
    tokenize(inputline, thisline, HASLINE);
    interpret_line();
  }
}

/*
** 'exit_interpreter' finishes the run of the interpreter itself. It ensures
** that all files that the interpreter knows about have been closed and frees
** any memory allocated to it. It returns the status code 'retcode'. This is
** normally set to EXIT_SUCCESS. If command line option -quit was used and
** an error was found in the Basic program it returns EXIT_FAILURE. If the
** 'quit' command is followed by a return code, that value is used instead.
*/
void exit_interpreter(int retcode) {
  fileio_shutdown();
  end_screen();
  end_keyboard();
  end_emulation();
  restore_handlers();
  release_heap();
  exit(retcode);
}


