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
**	This file contains the main interpreter command loop
*/

#include "target.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <setjmp.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#ifdef USE_SDL
#include <SDL.h>
#else
#ifndef TARGET_RISCOS
#include <pthread.h>
#endif
#endif /* USE_SDL */
#ifndef TARGET_MINGW
#include <sys/mman.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "common.h"
#include "basicdefs.h"
#include "tokens.h"
#include "errors.h"
#include "heap.h"
#include "editor.h"
#include "commands.h"
#include "statement.h"
#include "fileio.h"
#include "mos.h"
#include "keyboard.h"
#include "screen.h"
#include "miscprocs.h"
#include "evaluate.h"
#include "net.h"

/* #define DEBUG */

workspace basicvars;		/* This contains all the important interpreter variables */
matrixbits matrixflags;		/* This contains flags used by Matrix Brandy extensions */

/* Forward references */

char *collapse; /* debug hack */

static void init1(void);
static void init2(void);
static void gpio_init(void);
static void run_interpreter(void);
static void init_timer(void);

static char inputline[INPUTLEN];	/* Last line read */
static size_t worksize;			/* Initial workspace size */

static cmdarg *arglast;			/* Pointer to end of command line argument list */

static struct loadlib {char *name; struct loadlib *next;} *liblist, *liblast;

static void check_cmdline(int, char *[]);
#ifndef BRANDYAPP
static char *loadfile;			/* Pointer to name of file to load when interpreter starts */
#endif

/*
** 'main' just starts things going. Control does not returns here after
** 'run_interpreter' is called. The program finishes when 'exec_quit'
** (in statement.c) is invoked. 'exec_quit' handles the 'QUIT' command
*/

int main(int argc, char *argv[]) {
// Hmmm. Why doesn't this work?
//#ifdef TARGET_RISCOS
//  _kernel_oscli("WimpSlot 1600K");
//#endif
  /* DEBUG HACK */
  collapse=NULL;
  init1();
  init_timer();	/* Initialise the timer thread */
#ifndef NONET
  brandynet_init();
#endif
#ifdef BRANDYAPP
  basicvars.runflags.quitatend = TRUE;
  basicvars.runflags.loadngo = TRUE;
#endif
  check_cmdline(argc, argv);
  init2();
  gpio_init();
  run_interpreter();
  return EXIT_FAILURE;
}

#ifdef TARGET_MINGW
int WinMain(void) {
  return main(__argc, __argv);
}
#endif

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
  basicvars.centiseconds = mos_centiseconds();	/* Init to something sensible */
  basicvars.monotonictimebase = basicvars.centiseconds;
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
  basicvars.errorislocal = 0;

  basicvars.runflags.inredir = FALSE;           /* Input is being taken from the keyboard */
  basicvars.runflags.outredir = FALSE;          /* Output is going to the screen */
  basicvars.runflags.loadngo = FALSE;		/* Do not start running program immediately */
  basicvars.runflags.quitatend = FALSE;		/* Do not exit from interpreter when program finishes */
  basicvars.runflags.ignore_starcmd = FALSE;	/* Do not ignore built-in '*' commands */
  basicvars.escape_enabled = TRUE;		/* Allow the Escape key to stop execution */
#ifdef DEFAULT_IGNORE
  basicvars.runflags.flag_cosmetic = FALSE;	/* Ignore all unsupported features */
#else
  basicvars.runflags.flag_cosmetic = TRUE;	/* Unsupported features generate errors */
#endif
  basicvars.misc_flags.trapexcp = TRUE;		/* Trap exceptions */
  basicvars.misc_flags.validedit = FALSE;	/* Contents of edit_flags are not valid */

  basicvars.loadpath = NIL;
  basicvars.argcount = 0;
  basicvars.arglist = NIL;		/* List of command line arguments */
  arglast = NIL;			/* End of list of command line arguments */

  liblist = liblast = NIL;		/* List of libraries to load when interpreter starts */
  worksize = 0;				/* Use default workspace size */

  matrixflags.doexec = NULL;		/* We're not doing a *EXEC to begin with */
  matrixflags.failovermode = 255;	/* Report Bad Mode on unavailable screen mode */
  matrixflags.int_uses_float = 0;	/* Does INT() use floats? Default no = RISC OS and BBC behaviour */
  matrixflags.legacyintmaths = 0;	/* Enable legacy integer maths? Default no = BASIC VI behaviour */
  matrixflags.hex64 = 0;		/* Decode hex as 64-bit? Default no = BASIC VI behaviour */
  matrixflags.bitshift64 = 0;		/* Bit shifts operate in 64-bit space? Default no = BASIC VI behaviour */
  matrixflags.pseudovarsunsigned = 0;	/* Are memory pseudovariables unsigned on 32-bit? */
  matrixflags.tekenabled = 0;		/* Tektronix enabled in text mode (default: no) */
  matrixflags.tekspeed = 0;
  matrixflags.osbyte4val = 0;		/* Default OSBYTE 4 value */
#if (defined(TARGET_UNIX) & !defined(USE_SDL)) | defined(TARGET_MACOSX)
  matrixflags.delcandelete = 1;		/* DEL character can delete? */
#else
  matrixflags.delcandelete = 0;		/* DEL character can delete? */
#endif

/*
 * Add dummy first parameter for Basic program command line.
 * This is the Basic program's name
 */
  add_arg("");
}

static void gpio_init() {
#ifdef TARGET_UNIX
  int fd;

  matrixflags.gpio = 0;				/* Initialise the flag to 0 (not enabled) */
  matrixflags.gpiomem = (byte *)-1;		/* Initialise, will internally return &FFFFFFFF */

  fd=open("/dev/gpiomem", O_RDWR | O_SYNC);
  if (fd == -1) return;				/* Couldn't open /dev/gpiomem - exit quietly */

  matrixflags.gpiomem=(byte *)mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if (matrixflags.gpiomem == MAP_FAILED) {
    matrixflags.gpiomem = NULL;
    return;
  }
  /* If we got here, mmap succeeded. */
  matrixflags.gpio = 1;
  matrixflags.gpiomemint=(uint32 *)matrixflags.gpiomem;
#else
  matrixflags.gpio = 0;				/* Initialise the flag to 0 (not enabled) */
  matrixflags.gpiomem = (byte *)-1;		/* Initialise, will internally return &FFFFFFFF */
#endif
  return;
}

/*
** 'init2' finishes initialising the interpreter
*/
static void init2(void) {
#ifdef NEWKBD
  if (!mos_init() || !kbd_init() || !init_screen()) {
#else
  if (!mos_init() || !init_keyboard() || !init_screen()) {
#endif
    cmderror(CMD_INITFAIL);	/* Initialisation failed */
    exit_interpreter(EXIT_FAILURE);	/* End run */
  }
  if (!init_heap() || !init_workspace(worksize)) {
    cmderror(CMD_NOMEMORY);	/* Not enough memory to run interpreter */
#ifdef NEWKBD
    kbd_quit();
#else
    end_keyboard();
#endif
    exit(EXIT_FAILURE);
  }
#ifdef USE_SDL
  if ((size_t)basicvars.page >= 0x8000) {
    matrixflags.mode7fb = 0x7C00;
  } else {
    matrixflags.mode7fb = 0xFFFF7C00;
  }
  matrixflags.vdu14lines=0;
#endif
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
#ifndef BRANDYAPP
  loadfile = NIL;
#endif
  n = 1;
  while (n<argc) {
    p = argv[n];
    if (*p=='-') {	/* Got an option */
      optchar = tolower(*(p+1));	/* Get first character of option name */
      if (optchar=='h') {		/* -help */
        show_help();
        exit(0);
      }
      else if (optchar == 'v') {
#ifdef BRANDY_GITCOMMIT
	printf("%s\n  Git commit %s on branch %s (%s)\n", IDSTRING, BRANDY_GITCOMMIT, BRANDY_GITBRANCH, BRANDY_GITDATE);
#else
	printf("%s\n", IDSTRING);
#endif
        exit(0);
      }
#ifdef USE_SDL
      else if (optchar=='f') {		/* -fullscreen */
        basicvars.runflags.startfullscreen=TRUE;
      }
      else if (optchar=='s' && tolower(*(p+2))=='w') {
        basicvars.runflags.swsurface=TRUE;
      }
#endif
#ifndef BRANDYAPP
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
      else if (optchar=='s' && tolower(*(p+2))=='t')	/* -strict  Error on cosmetic errors */
        basicvars.runflags.flag_cosmetic = TRUE;
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
          worksize = CAST(strtol(argv[n], &sp, 10), size_t);	/* Fetch workspace size (n.b. no error checking) */
          if (tolower(*sp)=='k') {	/* Size is in kilobytes */
            worksize = worksize*1024;
          } else if (tolower(*sp)=='m') {	/* Size is in megabytes */
            worksize = worksize*1024*1024;
          } else if (tolower(*sp)=='g') {	/* Size is in gigabytes */
            worksize = worksize*1024*1024*1024;
          }
        }
      }
      else if (optchar=='!')		/* -! - Don't initialise signal handlers */
        basicvars.misc_flags.trapexcp = FALSE;
      else {
/* Any unrecognised options are assumed to be for the Basic program */
        add_arg(argv[n]);
      }
#endif /* BRANDYAPP */
    }
#ifndef BRANDYAPP
    else {	/* Name of file to run supplied */
      if (loadfile==NIL) {
        loadfile = p;	/* Make note of name of file to load */
        basicvars.runflags.quitatend = basicvars.runflags.loadngo = TRUE;
      }
      else {	/* Assume anything else is for the Basic program */
        add_arg(argv[n]);
      }
    }
#endif /* BRANDYAPP */
    n++;
  }

#ifndef BRANDYAPP
/* Update program's name in Basic's command line list */
  if (loadfile != NIL) {
    basicvars.arglist->argvalue = loadfile;
  }
#endif
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

#ifndef BRANDYAPP
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
#endif

#ifdef USE_SDL
static int timer_thread(void *data) {
#else
static void *timer_thread(void *data) {
#endif
  struct timeval tv;
  while(1) {
    gettimeofday (&tv, NULL);

    /* tv.tv_sec  = Seconds since 1970 */
    /* tv.tv_usec = and microseconds */

    basicvars.centiseconds = (((unsigned)tv.tv_sec * 100) + ((unsigned)tv.tv_usec / 10000));
    usleep(5000);
  }
  return 0;
}

/* This function starts a timer thread */
static void init_timer() {
#ifndef TARGET_RISCOS
#ifdef USE_SDL
  basicvars.csec_thread = NULL;
  basicvars.csec_thread = SDL_CreateThread(timer_thread,NULL);
  if (basicvars.csec_thread == NULL) {
    fprintf(stderr, "Timer thread failed to start\n");
    exit(1);
  }
#else
  pthread_t timer_thread_id;
  int err = pthread_create(&timer_thread_id,NULL,&timer_thread,NULL);
  if(err) {
    fprintf(stderr,"Unable to create timer thread\n");
    exit(1);
  }
#endif /* USE_SDL */
#endif /* TARGET_RISCOS */
}

/*
** 'run_interpreter' is the main command loop for the interpreter.
** It reads commands and executes then. Control is also returned
** here in the event of an error by means of a 'siglongjmp' to
** 'basicvars.restart'
*/
static void run_interpreter(void) {
  if (sigsetjmp(basicvars.restart, 1)==0) {
    if (!basicvars.runflags.loadngo && !basicvars.runflags.outredir) announce();	/* Say who we are */
    init_errors();	/* Set up the signal handlers */
#ifdef BRANDYAPP
    read_basic_block();
    run_program(basicvars.start);
#else
    if (liblist!=NIL) load_libraries();
    if (loadfile!=NIL) {	/*  Name of program to load was given on command line */
      read_basic(loadfile);
      init_expressions();
      strcpy(basicvars.program, loadfile);	/* Save the name of the file */
      if (basicvars.runflags.loadngo) run_program(basicvars.start);	/* Start program execution */
    }
#endif
  }
/* Control passes to this point in the event of an error via a 'siglongjmp' */
  while (TRUE) {
    read_command();
    tokenize(inputline, thisline, HASLINE, TRUE);
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
#ifdef NEWKBD
  kbd_quit();
#else
  end_keyboard();
#endif
  mos_final();
  restore_handlers();
  release_heap();
  exit(retcode);
}


