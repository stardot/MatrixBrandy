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
**      This file contains the main interpreter command loop
*/

#include "target.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <setjmp.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#ifndef TARGET_RISCOS
#include <pthread.h>
#endif
#ifndef TARGET_MINGW
#include <sys/mman.h>
#endif
#ifdef USE_SDL
#include <SDL.h>
#endif /* USE_SDL */
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
#ifdef USE_SDL
#include "graphsdl.h"
#endif
#include "miscprocs.h"
#include "evaluate.h"
#include "net.h"

#ifdef USE_SDL
extern threadmsg tmsg;
#endif

/* #define DEBUG */

workspace basicvars;            /* This contains all the important interpreter variables */
matrixbits matrixflags;         /* This contains flags used by Matrix Brandy extensions */

/* Forward references */

static void init1(void);
static void init2(void);
static void gpio_init(void);
#ifdef USE_SDL
static void *escape_thread(void *);
static void init_timer(void);
#endif
static void *run_interpreter(void *);
#ifndef TARGET_RISCOS
static void *timer_thread(void *);
#endif
static void init_clock(void);

static char inputline[INPUTLEN];        /* Last line read */
static size_t worksize;                 /* Initial workspace size */

static cmdarg *arglast;                 /* Pointer to end of command line argument list */

static struct loadlib {char *name; struct loadlib *next;} *liblist, *liblast;

static void check_configfile(void);
static void check_cmdline(int, char *[]);
#ifndef BRANDYAPP
static char *loadfile;                  /* Pointer to name of file to load when interpreter starts */
#endif

/*
** 'main' just starts things going. Control does not returns here after
** 'run_interpreter' is called. The program finishes when 'exec_quit'
** (in statement.c) is invoked. 'exec_quit' handles the 'QUIT' command
*/

#ifdef TARGET_RISCOS
/* Cut-down version without threads, as we use the OS for graphics and timer */
int main(int argc, char *argv[]) {
  init1();
#ifndef NONET
  brandynet_init();
#endif
#ifdef BRANDYAPP
  basicvars.runflags.quitatend = TRUE;
  basicvars.runflags.loadngo = TRUE;
#endif
  check_configfile();
  check_cmdline(argc, argv);
  init2();
  gpio_init();
  run_interpreter(0);
  return EXIT_FAILURE;
}

#else
int main(int argc, char *argv[]) {
#ifdef USE_SDL
  pthread_t escape_thread_id;
#endif
  pthread_t interp_thread_id;
  pthread_attr_t threadattrs, *threadattrp;
  init1();
#ifndef NONET
  brandynet_init();
#endif
#ifdef BRANDYAPP
  basicvars.runflags.quitatend = TRUE;
  basicvars.runflags.loadngo = TRUE;
#endif
  check_configfile();
  check_cmdline(argc, argv);
  init2();
  gpio_init();
  /* Populate threadattrs to tweak stack size */
  if (pthread_attr_init(&threadattrs)) {
    /* If that didn't work, carry on without it. */
    threadattrp = NULL;
  } else {
    int32 stacksize = basicvars.worksize;
    if (stacksize < 2*1024*1024) stacksize=2*1024*1024;
    threadattrp = &threadattrs;
    if (pthread_attr_setstacksize(threadattrp, stacksize)) {
      basicvars.maxrecdepth = 512;
      fprintf(stderr, "Unable to override stack size\n");
    } else {
#ifdef TARGET_MINGW
      basicvars.maxrecdepth = (stacksize / 670);
#else
      basicvars.maxrecdepth = (stacksize / 512);
#endif
    }
  }
#ifdef USE_SDL
  init_timer(); /* Initialise the timer thread */
  tmsg.bailout = -1;
  if (pthread_create(&escape_thread_id, NULL, &escape_thread, NULL)) {
    fprintf(stderr, "Unable to create Escape handler thread.\n");
    exit(1);
  }
  if (pthread_create(&interp_thread_id, threadattrp, &run_interpreter, NULL)) {
    fprintf(stderr, "Unable to create Interpreter thread\n");
    exit(1);
  }
  videoupdatethread();
#else
  if (pthread_create(&interp_thread_id, threadattrp, &run_interpreter, NULL)) {
    fprintf(stderr, "Unable to create Interpreter thread\n");
    exit(1);
  }
  timer_thread(0);
#endif
  return EXIT_FAILURE;
}
#endif /* TARGET_RISCOS */

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
  init_clock();                               /* Init to something sensible */
  basicvars.monotonictimebase = basicvars.centiseconds;
  basicvars.list_flags.space = FALSE;         /* Set initial listing options */
  basicvars.list_flags.indent = FALSE;
  basicvars.list_flags.split = FALSE;
  basicvars.list_flags.noline = FALSE;
  basicvars.list_flags.lower = FALSE;
  basicvars.list_flags.expand = FALSE;

#ifdef DEBUG
  basicvars.debug_flags.debug = FALSE;        /* Set interpreter debug options */
  basicvars.debug_flags.tokens = FALSE;
  basicvars.debug_flags.variables = FALSE;
  basicvars.debug_flags.strings = FALSE;
  basicvars.debug_flags.stats = FALSE;
  basicvars.debug_flags.stack = FALSE;
  basicvars.debug_flags.allstack = FALSE;
  basicvars.debug_flags.vdu = FALSE;
#endif
  basicvars.errorislocal = 0;

  basicvars.runflags.inredir = FALSE;         /* Input is being taken from the keyboard */
  basicvars.runflags.outredir = FALSE;        /* Output is going to the screen */
  basicvars.runflags.loadngo = FALSE;         /* Do not start running program immediately */
  basicvars.runflags.quitatend = FALSE;       /* Do not exit from interpreter when program finishes */
  basicvars.runflags.ignore_starcmd = FALSE;  /* Do not ignore built-in '*' commands */
  basicvars.escape_enabled = TRUE;            /* Allow the Escape key to stop execution */
#ifdef DEFAULT_IGNORE
  basicvars.runflags.flag_cosmetic = FALSE;     /* Ignore all unsupported features */
#else
  basicvars.runflags.flag_cosmetic = TRUE;      /* Unsupported features generate errors */
#endif
  basicvars.misc_flags.trapexcp = TRUE;         /* Trap exceptions */
  basicvars.misc_flags.validedit = FALSE;       /* Contents of edit_flags are not valid */

  basicvars.loadpath = NIL;
  basicvars.argcount = 0;
  basicvars.recdepth = 0;
  basicvars.arglist = NIL;            /* List of command line arguments */
  basicvars.maxrecdepth = MAXRECDEPTH;
  arglast = NIL;                      /* End of list of command line arguments */

  liblist = liblast = NIL;            /* List of libraries to load when interpreter starts */
  worksize = 0;                       /* Use default workspace size */

  matrixflags.doexec = NULL;          /* We're not doing a *EXEC to begin with */
  matrixflags.failovermode = 255;     /* Report Bad Mode on unavailable screen mode */
  matrixflags.int_uses_float = 0;     /* Does INT() use floats? Default no = RISC OS and BBC behaviour */
  matrixflags.legacyintmaths = 0;     /* Enable legacy integer maths? Default no = BASIC VI behaviour */
  matrixflags.cascadeiftweak = 1;     /* Handle cascaded IFs BBCSDL-style? Default no = ARM BBC BASIC behaviour */
  matrixflags.hex64 = 0;              /* Decode hex as 64-bit? Default no = BASIC VI behaviour */
  matrixflags.bitshift64 = 0;         /* Bit shifts operate in 64-bit space? Default no = BASIC VI behaviour */
  matrixflags.pseudovarsunsigned = 0; /* Are memory pseudovariables unsigned on 32-bit? */
  matrixflags.tekenabled = 0;         /* Tektronix enabled in text mode (default: no) */
  matrixflags.tekspeed = 0;
  matrixflags.osbyte4val = 0;         /* Default OSBYTE 4 value */
#if (defined(TARGET_UNIX) & !defined(USE_SDL)) | defined(TARGET_MACOSX)
  matrixflags.delcandelete = 1;       /* DEL character can delete? */
#else
  matrixflags.delcandelete = 0;       /* DEL character can delete? */
#endif
#ifndef TARGET_RISCOS
  matrixflags.dospool = NULL;         /* By default, not doing a *SPOOL */
#endif
  matrixflags.printer = NULL;         /* By default, printer is closed */
  matrixflags.printer_ignore = 13;    /* By default, ignore carriage return characters */
  matrixflags.translatefname = 2;     /* 0 = Don't, 1 = Always, 2 = Attempt autodetect */
  matrixflags.startupmode = BRANDY_STARTUP_MODE;  /* Defaults to 0 */
#ifdef BRANDYAPP
  matrixflags.checknewver = 0;        /* By default, try to check for a new version */
#else
  matrixflags.checknewver = 1;        /* By default, try to check for a new version */
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

  matrixflags.gpio = 0;               /* Initialise the flag to 0 (not enabled) */
  matrixflags.gpiomem = (byte *)-1;   /* Initialise, will internally return &FFFFFFFF */

  fd=open("/dev/gpiomem", O_RDWR | O_SYNC);
  if (fd == -1) return;               /* Couldn't open /dev/gpiomem - exit quietly */

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
  matrixflags.gpio = 0;               /* Initialise the flag to 0 (not enabled) */
  matrixflags.gpiomem = (byte *)-1;   /* Initialise, will internally return &FFFFFFFF */
#endif
  return;
}

/*
** 'init2' finishes initialising the interpreter
*/
static void init2(void) {
  if (!mos_init() || !kbd_init() || !init_screen()) {
    cmderror(CMD_INITFAIL);           /* Initialisation failed */
    exit_interpreter(EXIT_FAILURE);   /* End run */
  }
  if (!init_heap() || !init_workspace(worksize)) {
    cmderror(CMD_NOMEMORY);           /* Not enough memory to run interpreter */
    kbd_quit();
    exit(EXIT_FAILURE);
  }
#ifdef USE_SDL
  matrixflags.vdu14lines=0;
#endif
  init_commands();
  init_fileio();
  clear_program();
  basicvars.current = NIL;
  basicvars.misc_flags.validsaved = FALSE;  /* Want this to be 'FALSE' when the interpreter first starts */
  init_interpreter();
}

/* 'check_configfile' is called to check the configuration file
 * (~/.brandyrc on UNIX-type systems) to override compiled defaults
 * before checking the command line.
 */
static void check_configfile() {
  /* Right now, this is a stub that does nothing. */
  FILE *conffile;
  char *conffname, *line, *item, *parameter;

  conffname=malloc(1024);
  memset(conffname, 0, 1024);
#ifdef TARGET_RISCOS
  snprintf(conffname, 1023, "<Brandy$Dir>.brandyrc");
#endif
#ifdef TARGET_MINGW
  snprintf(conffname, 1023, "%s\\brandyrc", getenv("APPDATA"));
#endif
#ifdef TARGET_UNIX
  snprintf(conffname, 1023, "%s/.brandyrc", getenv("HOME"));
#endif
  if(*conffname=='\0') {
    free(conffname);
    return;
  }

  conffile=fopen(conffname, "r");
  if (!conffile) {
    /* File doesn't exist. Not to worry. */
    free(conffname);
    return;
  }
  line=malloc(1024);
  while (!feof(conffile)) {
    memset(line,0,1024);          /* Clear the buffer before new entries are read */
    parameter=NULL;
    fgets(line, 1024, conffile);
    /* Borrow the 'item' pointer, to remove any trailing CR/LF */
    item=strchr(line, '\n');
    if (item) *item='\0';
        item=strchr(line, '\r');
    if (item) *item='\0';

    item=line;
    if(*item == '-') item++;      /* Skip a leading - */
    parameter=strchr(item, '=');  /* Parameter comes after space or = */
    if (!parameter) parameter=strchr(item, ' ');
    if (parameter) {
      *parameter='\0';
      parameter++;
    }

    if(!strcmp(item, "nocheck")) {
      matrixflags.checknewver = FALSE;
#ifdef USE_SDL
    } else if(!strcmp(item, "fullscreen")) {
      basicvars.runflags.startfullscreen=TRUE;
    } else if(!strcmp(item, "nofull")) {
      matrixflags.neverfullscreen=TRUE;
    } else if(!strcmp(item, "swsurface")) {
      basicvars.runflags.swsurface=TRUE;
#endif
    } else if(!strcmp(item, "tek")) {
      matrixflags.tekenabled=1;
    } else if(!strcmp(item, "ignore")) {
      basicvars.runflags.flag_cosmetic = FALSE;
    } else if(!strcmp(item, "strict")) {
      basicvars.runflags.flag_cosmetic = TRUE;
    } else if(!strcmp(item, "nostar")) {
      basicvars.runflags.ignore_starcmd = TRUE;
    } else if(!strcmp(item, "size")) {
      char *sp;
      worksize = CAST(strtol(parameter, &sp, 10), size_t);  /* Fetch workspace size (n.b. no error checking) */
      if (tolower(*sp)=='k') {          /* Size is in kilobytes */
        worksize = worksize*1024;
      } else if (tolower(*sp)=='m') {   /* Size is in megabytes */
        worksize = worksize*1024*1024;
      } else if (tolower(*sp)=='g') {   /* Size is in gigabytes */
        worksize = worksize*1024*1024*1024;
      }
#ifndef BRANDY_MODE7ONLY
    } else if(!strcmp(item, "startupmode")) {
      char *sp;
      matrixflags.startupmode = CAST(strtol(parameter, &sp, 10), size_t);  /* startup mode */
#endif
    } else if(!strcmp(item, "path")) {
      if (basicvars.loadpath!=NIL) free(basicvars.loadpath);  /* Discard existing list */
      basicvars.loadpath = malloc(strlen(parameter)+1);         /* +1 for the NUL */
      if (basicvars.loadpath==NIL) {    /* No memory available */
        cmderror(CMD_NOMEMORY);
        exit(EXIT_FAILURE);
      }
      strcpy(basicvars.loadpath, parameter);
    } else if(!strcmp(item, "lib")) {
      struct loadlib *p = malloc(sizeof(struct loadlib));
      if (p==NIL) {
        cmderror(CMD_NOMEMORY);
        exit(EXIT_FAILURE);
      } else {
        p->name = strdup(parameter);
        p->next = NIL;
        if (liblast==NIL)
          liblist = p;
        else {
          liblast->next = p;
        }
        liblast = p;
      }
    } else if(!strcmp(item, "intusesfloat")) {
      matrixflags.int_uses_float = TRUE;
    } else if(!strcmp(item, "legacyintmaths")) {
      matrixflags.legacyintmaths = TRUE;
    } else if(!strcmp(item, "hex64")) {
      matrixflags.hex64 = TRUE;
    } else if(!strcmp(item, "bitshift64")) {
      matrixflags.bitshift64 = TRUE;
    } else if(!strcmp(item, "pseudovarsunsigned")) {
      matrixflags.pseudovarsunsigned = TRUE;
    }
  }

  fclose(conffile);
  free(conffname);
  free(line);
}

/*
** 'check_cmdline' is called to parse the command line.
** Note that any unrecognised parameters are assumed to be destined
** for the Basic program
*/
static void check_cmdline(int argc, char *argv[]) {
  boolean had_double_dash = FALSE;
  int n;
  char optchar, *p;
#ifndef BRANDYAPP
  loadfile = NIL;
#endif
  n = 1;
  while (n<argc) {
    p = argv[n];
    if (*p=='-' && !had_double_dash) {  /* Got an option */
      optchar = tolower(*(p+1));        /* Get first character of option name */
      if (optchar=='h') {               /* -help */
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
      else if (optchar=='f') {          /* -fullscreen */
        basicvars.runflags.startfullscreen=TRUE;
      }
      else if (optchar=='n' && tolower(*(p+2))=='o' && tolower(*(p+3))=='f') {  /* -nofull */
        matrixflags.neverfullscreen=TRUE;
      }
      else if (optchar=='s' && tolower(*(p+2))=='w') {    /* -swsurface */
        basicvars.runflags.swsurface=TRUE;
      }
#endif
#ifndef BRANDYAPP
      else if (optchar=='n' && tolower(*(p+2))=='o' && tolower(*(p+3))=='c') {  /* -nocheck */
        matrixflags.checknewver = FALSE;
      }
      else if (optchar == 'c' || optchar == 'q' || (optchar == 'l' && tolower(*(p+2)) == 'o')) {        /* -chain, -quit or -load */
        n++;
        if (n==argc)
          cmderror(CMD_NOFILE, p);      /* Filename missing */
        else if (loadfile!=NIL)
          cmderror(CMD_FILESUPP);       /* Filename already supplied */
        else {
          loadfile = argv[n];
          if (optchar=='c')             /* -chain */
            basicvars.runflags.loadngo = TRUE;
          else if (optchar=='q') {      /* -quit */
            basicvars.runflags.quitatend = basicvars.runflags.loadngo = TRUE;
          }
        }
      }
      else if (optchar=='t')                            /* -tek - enable Tek graphics */
        matrixflags.tekenabled=1;
      else if (optchar=='i' && tolower(*(p+2))=='g')    /* -ignore  Ignore cosmetic errors */
        basicvars.runflags.flag_cosmetic = FALSE;
      else if (optchar=='s' && tolower(*(p+2))=='t')    /* -strict  Error on cosmetic errors */
        basicvars.runflags.flag_cosmetic = TRUE;
      else if (optchar=='l' && tolower(*(p+2))=='i') {  /* -lib */
        n++;
        if (n==argc)
          cmderror(CMD_NOFILE, p);      /* Filename missing */
        else {                          /* Add name to list of libraries to load */
          struct loadlib *llp = malloc(sizeof(struct loadlib));
          if (llp==NIL)
            cmderror(CMD_NOMEMORY);
          else {
            llp->name = argv[n];
            llp->next = NIL;
            if (liblast==NIL)
              liblist = llp;
            else {
              liblast->next = llp;
            }
            liblast = llp;
          }
        }
      }
      else if (optchar == 'n' && tolower(*(p+2))=='o' && tolower(*(p+3))=='s')  /* -nostar  Ignore '*' commands */
        basicvars.runflags.ignore_starcmd = TRUE;
      else if (optchar=='p') {              /* -path */
        n++;
        if (n==argc)
          cmderror(CMD_NOFILE, p);          /* Directory list missing */
        else {                              /* Set up the path list */
          if (basicvars.loadpath!=NIL) free(basicvars.loadpath);  /* Discard existing list */
          basicvars.loadpath = malloc(strlen(argv[n])+1);         /* +1 for the NUL */
          if (basicvars.loadpath==NIL) {    /* No memory available */
            cmderror(CMD_NOMEMORY);
            exit(EXIT_FAILURE);
          }
          strcpy(basicvars.loadpath, argv[n]);
        }
      }
      else if (optchar=='s') {              /* -size */
        n++;
        if (n==argc)
          cmderror(CMD_NOSIZE, p);          /* Workspace size missing */
        else {
          char *sp;
          worksize = CAST(strtol(argv[n], &sp, 10), size_t);  /* Fetch workspace size (n.b. no error checking) */
          if (tolower(*sp)=='k') {          /* Size is in kilobytes */
            worksize = worksize*1024;
          } else if (tolower(*sp)=='m') {   /* Size is in megabytes */
            worksize = worksize*1024*1024;
          } else if (tolower(*sp)=='g') {   /* Size is in gigabytes */
            worksize = worksize*1024*1024*1024;
          }
        }
      }
      else if (optchar=='!')                /* -! - Don't initialise signal handlers */
        basicvars.misc_flags.trapexcp = FALSE;
      else if (optchar=='-' && *(p+2) == 0) /* -- - Pass all remaining options to the Basic program */
        had_double_dash = TRUE;
      else {
/* Any unrecognised options are assumed to be for the Basic program */
        add_arg(argv[n]);
      }
#endif /* BRANDYAPP */
    }
#ifndef BRANDYAPP
    else {                              /* Name of file to run supplied */
      if (loadfile==NIL) {
        loadfile = p;                   /* Make note of name of file to load */
        basicvars.runflags.quitatend = basicvars.runflags.loadngo = TRUE;
      }
      else {                            /* Assume anything else is for the Basic program */
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
  if (!ok) exit_interpreter(EXIT_SUCCESS);      /* EOF hit - Tidy up and end run */
}

/*
** 'interpret_line' either calls the editor or tries to run what is in
** 'thisline' as a sequence of commands
*/
static void interpret_line(void) {
  if (GET_LINENO(thisline)==NOLINENO)
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
    read_library(p->name, INSTALL_LIBRARY);     /* Read library and install it */
    p = p->next;
  } while (p!=NIL);
}
#endif

void init_clock() {
#ifdef TARGET_RISCOS
  basicvars.clocktype = -1;
  basicvars.centiseconds = clock();
#else
  struct timespec tv;
  int result=1;
#ifdef CLOCK_MONOTONIC
  basicvars.clocktype = CLOCK_MONOTONIC;
  result=clock_gettime(basicvars.clocktype, &tv);
#endif
  if(result) {
    basicvars.clocktype = CLOCK_REALTIME;
    result=clock_gettime(basicvars.clocktype, &tv);
  }
  if (result) {
    fprintf(stderr, "init_clock: Unable to get a sensible timer, even realtime failed (which shouldn't happen)\n");
    exit(1);
  }
  basicvars.centiseconds = (((uint64)tv.tv_sec * 100) + ((uint64)tv.tv_nsec / 10000000));
#endif /* !TARGET_RISCOS */
}

#ifndef TARGET_RISCOS
static void *timer_thread(void *data) {
  //struct timeval tv;
  struct timespec tv;
  while(1) {
    clock_gettime(basicvars.clocktype, &tv);

    /* tv.tv_sec  = Seconds */
    // /* tv.tv_usec = and microseconds */
    /* tv.tv_nsec = Nanoseconds */

    //basicvars.centiseconds = (((uint64)tv.tv_sec * 100) + ((uint64)tv.tv_usec / 10000));
    basicvars.centiseconds = (((uint64)tv.tv_sec * 100) + ((uint64)tv.tv_nsec / 10000000));
    usleep(5000);
  }
  return 0;
}

#ifdef USE_SDL
/* This function starts a timer thread */
static void init_timer() {
  pthread_t timer_thread_id;
  int err = pthread_create(&timer_thread_id,NULL,&timer_thread,NULL);
  if(err) {
    fprintf(stderr,"Unable to create timer thread\n");
    exit(1);
  }
}
#endif /* USE_SDL */
#endif /* !TARGET_RISCOS */


#ifdef USE_SDL
static void *escape_thread(void *dummydata) {
  while (1) {
    kbd_escpoll();
#ifndef BRANDY_NOBREAKONCTRLPRTSC
    if ((kbd_inkey(-2) && kbd_inkey(-33))) {
      tmsg.bailout = 0;
      while(TRUE) sleep(10);
    }
#endif
    usleep(10000);
  }
  return(0); /* Control never reaches here */
}
#endif

/*
** 'run_interpreter' is the main command loop for the interpreter.
** It reads commands and executes then. Control is also returned
** here in the event of an error by means of a 'siglongjmp' to
** 'basicvars.restart'
*/
static void *run_interpreter(void *dummydata) {
  if (sigsetjmp(basicvars.restart, 1)==0) {
    if (!basicvars.runflags.loadngo && !basicvars.runflags.outredir) announce();        /* Say who we are */
    init_errors();      /* Set up the signal handlers */
#ifdef BRANDYAPP
    read_basic_block();
    run_program(basicvars.start);
#else
    if (liblist!=NIL) load_libraries();
    if (loadfile!=NIL) {        /*  Name of program to load was given on command line */
      read_basic(loadfile);
      init_expressions();
      memset(basicvars.program, 0, FNAMESIZE);
      if (strlen(loadfile) < FNAMESIZE ) {
        strncpy(basicvars.program, loadfile, FNAMESIZE-1);      /* Save the name of the file */
      }
      if (basicvars.runflags.loadngo) run_program(basicvars.start);     /* Start program execution */
    }
#endif
  }
/* Control passes to this point in the event of an error via a 'siglongjmp' */
  while (TRUE) {
    read_command();
    tokenize(inputline, thisline, HASLINE, TRUE);
    interpret_line();
  }
  return(0); /* Control never reaches here */
}

/*
** 'exit_interpreter' finishes the run of the interpreter itself. It ensures
** that all files that the interpreter knows about have been closed and frees
** any memory allocated to it. It returns the status code 'retcode'. This is
** normally set to EXIT_SUCCESS. If command line option -quit was used and
** an error was found in the Basic program it returns EXIT_FAILURE. If the
** 'quit' command is followed by a return code, that value is used instead.
*/
void exit_interpreter_real(int retcode) {
  fileio_shutdown();
  end_screen();
  kbd_quit();
  mos_final();
  restore_handlers();
  release_heap();
  exit(retcode);
}

void exit_interpreter(int retcode) {
#ifdef USE_SDL
  tmsg.bailout = retcode;
#else
  exit_interpreter_real(retcode);
#endif
}
