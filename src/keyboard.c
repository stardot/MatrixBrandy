/*
** This file is part of the Matrix Brandy Basic VI Interpreter.
** Copyright (C) 2000-2014 David Daniels
** Copyright (C) 2006, 2007 Colin Tuckley
** Copyright (C) 2014 Jonathan Harston
** Copyright (C) 2018-2021 Michael McConnell, Jonathan Harston and contributors
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
**      This file contains the keyboard handling routines.
**
**      When running under operating systems other than RISC OS the
**      interpreter uses its own keyboard handling functions to
**      provide both line editing and a line recall feature
**
**
** 20th August 2002 Crispian Daniels:
**      Included Mac OS X target in conditional compilation.
**
** December 2006 Colin Tuckley:
**      Rewrite to use SDL library Event handling.
**
** 06-Mar-2014 JGH: Zero-length function key strings handled correctly.
** 13-Nov-2018 JGH: SDL INKEY-1/2/3 checks for either modifier key.
**                  Bug: Caps/Num returns lock state, not key state.
** 03-Dec-2018 JGH: SDL wasn't detecting DELETE. Non-function special
**                  keys modified correctly.
** 04-Dec-2018 JGH: set_fn_string checks if function key in use.
**                  Some tidying up before deeper work.
** 07-Dec-2018 JGH: get_fn_string returns function key string for *SHOW.
**                  Have removed some BODGE conditions.
** 11-Dec-2018 JGH: Gradually moving multiple emulate_inkey() functions into
**                  one kbd_inkey() function. First result is that all platforms
**                  now support INKEY-256 properly.
** kbd_inkey(): -256: all platforms
**              +ve:  RISCOS:ok, WinSDL:ok, MGW:ok, DJP:ok
**              -ve:  RISCOS:ok, WinSDL:ok, MGW:ok, DJP:to do
**              Credit: con_keyscan() from JGH 'console' library
** 27-Dec-2018 JGH: DOS target can detect Shift/Ctrl/Alt as that's the only DOS API.
** 28-Dec-2018 JGH: kbd_init(), kbd_quit() all consolidated.
** 05-Jan-2019 JGH: kbd_fnkeyget(), kbd_fnkeyset().
** 06-Jan-2019 JGH: Started on kbd_readline().
** 17-Jan-2019 JGH: Merged common MinGW and DJGPP kbd_get() code. Modifiers fully
**                  work with non-function special keys. Some temporary scaffolding
**                  in readline().
**                  Tested on: MinGW
** 27-Jan-2019 JGH: Unix build works again, missed a #ifdef block.
**                  Tested on: DJGPP, MinGW, WinSDL, CentOS, RISCOS.
** 25-Aug-2019 JGH: Started stipping out legacy code.
**                  Tested on: DJGPP, MinGW, WinSDL, CentOS.
**                  RISCOS builds, but other issues cause problems.
** 28-Aug-2019 JGH: Cut out a lot of old code, added some more documentation.
**                  Corrected some errors in dostable[] with PgUp/PgDn/End.
**                  Corrected low level top-bit codes returned from SDL key read.
**                  Low-level keycode doesn't attempt to expand soft keys, read
**                  EXEC file or input redirection, that's kbd_get()'s responsibility.
**                  kbd_modkeys() obeys its API.
**                  Tested on: DJGPP, MinGW, WinSDL, CentOS.
**                  RISCOS builds, but other issues cause problems.
** 29-Aug-2019 JGH: Stopped kbd_get0() returning &00 when keypress returns nothing.
**                  Found a couple of obscure errors in dostable[] translation.
** 30-Aug-2019 JGH: Testing, minor bugs in BBC/JP keyboard layout. US/UK all ok.
**                  Some builds don't "see" Print/Pause/Width. SDL only one that sees Print.
** 07-Sep-2019 JGH: Last bits of emulate_get() now migrated into kbd_get0(). Loads of
**                  redundant code removed. Escape processing moving into here.
**                  *FX229 tested: DJGPP, MinGW, WinSDL, CentOS/SDL
**                  *FX220 starting to work, escpoll() and SIGINT check physical key, needs
**                  to check character.
** 14-Sep-2019 JGH: ANSI keypresses working again. Amputated entirety of decode_sequence(),
**                  replaced with parsing code from JGH Console library. Needs negative
**                  INKEY code for Unix+NoSDL.
** 25-Sep-2019 JGH: Moved all Escape polling into here, solves EscapeEscape problems on non-SDL.
**                  Escape setting checks sysvars, character input checks sv_EscapeChar.
**                  Background Escape still checks physical key.
** 28-Sep-2019 JGH: WinDJPP:  CHR$27=Esc, CHR$3=null, CtrlBreak=null, no EscEsc
**                  WinMinGW: CHR$27=Esc, CHR$3=Esc,  CtrlBreak=Quit, EscEsc fixed
**                  WinSDL:   CHR$27=Esc, CHR$3=null, CtrlBreak=null, EscEsc fixed
**                  WinSDL loses some keypresses
**                         probably background and foreground Escape checking clashing
**                         cf cZ80Tube and PDPTube.
**                  UnixSDL:  CHR$27=Esc, CHR$3=null, CtrlBreak=null, no EscEsc
**                  UnixSDL doesn't seem to lose keypresses
**                  tbrandy:  CHR$27=null, CHR$3=Esc, CtrlBreak=null, no EscEsc
**                  sbrandy:  CHR$27=null, CHR$3=Esc, CtrlBreak=null, no EscEsc
**
** 07-Oct-2019 JGH: Background Escape polling disabled during foreground key polling.
**                  Prevents lost keypresses during input.
**
** Issues: Alt+alphanum gives &180+n instead of &080+n.
**         A few outstanding bugs in BBC/JP keyboard layout.
** To do:  Implementing *FX225,etc will resolve raw/cooked keycode issue, cf below.
**
** Note: This is the only file that tests for BEOS.
**
*/

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "common.h"
#include "target.h"
#include "basicdefs.h"
#include "errors.h"
#include "screen.h"
#include "keyboard.h"
#include "inkey.h"
#include "mos.h"

#ifdef TARGET_RISCOS
/* New keyboard routines */
/* --------------------- */

#include "kernel.h"
#include "swis.h"

static boolean waitkey(int wait);		/* To prevent a forward reference	*/
static int32 pop_key(void);			/* To prevent a forward reference	*/

/* Veneers, fill in later */
boolean kbd_init() { return init_keyboard(); }
void    kbd_end()  { end_keyboard(); }
int     kbd_setfkey(int key, char *string, int length) {
		return set_fn_string(key, string, length); }
char   *kbd_getfkey(int key, int *len) {
		return get_fn_string(key, len); }
readstate kbd_readln(char buffer[], int32 length, int32 echochar) {
		return emulate_readline(&buffer[0], length, echochar); }


/* kbd_inkey called to implement Basic INKEY and INKEY$ functions */
/* -------------------------------------------------------------- */
int32 kbd_inkey(int32 arg) {

  // RISC OS, pass directly to MOS
  // -----------------------------
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = 129;				/* Use OS_Byte 129 for this		*/
  regs.r[1] = arg & BYTEMASK;
  regs.r[2] = (arg>>BYTESHIFT) & BYTEMASK;
  oserror = _kernel_swi(OS_Byte, &regs, &regs);
  if (oserror != NIL)	error(ERR_CMDFAIL, oserror->errmess);
  if (regs.r[2] == 0)	return regs.r[1];	/* Character was read successfully	*/
  else			return -1;		/* Timed out				*/
}


/* kbd_get called to implement Basic GET and GET$ functions */
/* -------------------------------------------------------- */
int32 kbd_get() {
  // RISC OS, pass directly to MOS
  // -----------------------------
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  oserror = _kernel_swi(OS_ReadC, &regs, &regs);
  if (oserror != NIL) error(ERR_CMDFAIL, oserror->errmess);
  return regs.r[0];
}


/* Legacy code from here onwards */
/* ----------------------------- */
static int nokeyboard=0;
static int escint=128;
static int escmul=1;
static int fx44x=1;

/* ================================================================= */
/* ================= RISC OS versions of functions ================= */
/* ================================================================= */

#include "kernel.h"
#include "swis.h"

/*
** 'emulate_get' emulates the Basic function 'get'
*/
int32 emulate_get(void) {
  return _kernel_osrdch();
}

/*
** 'emulate_inkey' does the hard work for the Basic 'inkey' function
*/
int32 emulate_inkey(int32 arg) {
  _kernel_oserror *oserror;     /* Use OS_Byte 129 to obtain the info */
  _kernel_swi_regs regs;
  regs.r[0] = 129;      	/* Use OS_Byte 129 for this */
  regs.r[1] = arg & BYTEMASK;
  regs.r[2] = (arg>>BYTESHIFT) & BYTEMASK;
  oserror = _kernel_swi(OS_Byte, &regs, &regs);
  if (oserror != NIL) error(ERR_CMDFAIL, oserror->errmess);
  if (arg >= 0) {               /* +ve argument = read keyboard with time limit */
    if (regs.r[2] == 0) 	/* Character was read successfully */
      return (regs.r[1]);
    else if (regs.r[2] == 0xFF) /* Timed out */
      return -1;
    else {
      error(ERR_CMDFAIL, "C library has missed an escape event");
    }
  }
  else {        /* -ve argument */
    if (regs.r[1] == 0xFF)
      return -1;
    else {
      return regs.r[1];
    }
  }
  return 0;     /* Not needed, but it keeps the Acorn C compiler happy */
}

/*
** 'emulate_readline' reads a line from the keyboard. It returns 'true'
** if the call worked successfully or 'false' if 'escape' was pressed.
** The data input is stored at 'buffer'. Up to 'length' characters can
** be read. A 'null' is added after the last character. The reason for
** using this function in preference to 'fgets' under RISC OS is that
** 'fgets' does not use 'OS_ReadLine' and therefore bypasses the command
** line history and other features that might be available via this SWI
** call.
*/
readstate emulate_readline(char buffer[], int32 length, int32 echochar) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  int32 carry;
  regs.r[0] = (int32)(&buffer[0]);
  regs.r[1] = length-1;         /* -1 to allow for a NULL to be added at the end in all cases */
  regs.r[2] = 0;                /* Allow any character to be input */
  regs.r[3] = 255;
  oserror = _kernel_swi_c(OS_ReadLine, &regs, &regs, &carry);
  if (oserror != NIL) error(ERR_CMDFAIL, oserror->errmess);
  if (carry != 0)               /* Carry is set - 'escape' was pressed */
    buffer[0] = NUL;
  else {
    buffer[regs.r[1]] = NUL;    /* Number of characters read is returned in R1 */
  }
  return carry == 0 ? READ_OK : READ_ESC;
}

/*
 * set_fn_string - Define a function key string
 */
int set_fn_string(int key, char *string, int length) {
  printf("Key = %d  String = '%s'\n", key, string);
  return 0;
}

char *get_fn_string(int key, int *len) {
}

boolean init_keyboard(void) {
  return TRUE;
}

void end_keyboard(void) {
}

void kbd_quit() {
}

int kbd_escack() {
  return 0;
}

int kbd_esctest() {
  return FALSE;
}

int kbd_escpoll() {
  return 0;
}

int32 kbd_readline(char *buffer, int32 length, int32 chars) {
  // RISC OS, pass directly to MOS
  // -----------------------------
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  int32 carry;

// RISC OS compiler doesn't like code before variable declaration, so have to duplicate
  if (length<1) return 0;			/* Filter out impossible or daft calls	*/
  chars=(chars & 0xFF) | 0x00FF2000;		/* temp'y force all allowable chars	*/

  regs.r[0] = (int)(&buffer[0]);
  regs.r[1] = length - 1;			/* Subtract one to allow for terminator	*/
  regs.r[2] = (chars >> 8) & 0xFF;		/* Lowest acceptable character		*/
  regs.r[3] = (chars >> 16) & 0xFF;		/* Highest acceptable character		*/

  if (regs.r[0] & 0xFF000000) {			/* High memory				*/
    regs.r[4] = chars & 0xFF0000FF;		/* R4=Echo character and flags		*/
    oserror = _kernel_swi_c(0x00007D, &regs, &regs, &carry);	/* OS_ReadLine32	*/
    /* If OS_ReadLine32 doesn't exist, we don't have high memory anyway, so safe	*/

  } else {					/* Low memory				*/
    regs.r[0]=regs.r[0] | (chars & 0xFF000000);	/* R0=Flags and address			*/
    regs.r[4]=chars & 0x000000FF;		/* R4=Echo character			*/
    oserror = _kernel_swi_c(OS_ReadLine, &regs, &regs, &carry);
  }

  if (oserror != NIL) error(ERR_CMDFAIL, oserror->errmess);
  if (carry) buffer[0] = NUL;			/* Carry is set - 'Escape' was pressed	*/
  else       buffer[regs.r[1]] = NUL;		/* Number of characters returned in R1	*/

  return carry == 0 ? regs.r[1] : -1;		/* To do: -1=READ_ESC			*/
}


#else /* Matching endif is at end of file */
/* **********************************
 * NON RISC OS VERSIONS OF THE CODE * 
 ************************************/

// Temporary split while finalising NEWKBD code.
#ifndef NEWKBD
#include "kbd-old.c"
#else /* Matching endif is at the end of the file */

#if defined(TARGET_MINGW) || defined(TARGET_WIN32) || defined(TARGET_BCC32)
 #include <windows.h>
#ifdef CYGWINBUILD
 #include <fcntl.h>
#endif
#endif

#ifdef TARGET_DJGPP
#include <pc.h>
#include <keys.h>
#include <bios.h>
#include <errno.h>
#include <termios.h>
#endif

#ifdef TARGET_UNIX
// #include <sys/time.h>
// #include <sys/types.h>
#include <errno.h>
// #include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
// Move these later
static struct termios origtty;  /* Copy of original keyboard parameters */
#endif

#ifdef USE_SDL
#include "SDL.h"
#include "SDL_events.h"
#include "graphsdl.h"
// Move these later
Uint8 mousestate, *keystate=NULL;
int64 esclast=0;
#endif

#include <stdlib.h>
#if defined(TARGET_MINGW) || defined(TARGET_WIN32) || defined(TARGET_BCC32)
 #include <sys/time.h>
 #include <sys/types.h>
 #ifndef CYGWINBUILD
   #include <keysym.h>
 #endif
#endif


/* ASCII codes of various useful characters */
#define CTRL_A          0x01
#define CTRL_B          0x02
#define CTRL_C          0x03
#define CTRL_D          0x04
#define CTRL_E          0x05
#define CTRL_F          0x06
#define CTRL_H          0x08
#define CTRL_K          0x0B
#define CTRL_L          0x0C
#define CTRL_N          0x0E
#define CTRL_O          0x0F
#define CTRL_P          0x10
#define CTRL_U          0x15
#define ESCAPE          0x1B
#define DEL             0x7F

/*
** Following are the key codes given in the RISC OS PRMs for the various
** special keys of interest to this program. RISC OS sequences for these
** keys consists of a NUL followed by one of these values. They are used
** internally when processing the keys. The key codes returned by foreign
** operating systems are mapped on to these values where possible.
*/
#define HOME            0x1E
#define CTRL_HOME       0x1E
#define END             0x8B
#define CTRL_END        0xAB
#define UP              0x8F
#define CTRL_UP         0xAF
#define DOWN            0x8E
#define CTRL_DOWN       0xAE
#define LEFT            0x8C
#define CTRL_LEFT       0xAC
#define RIGHT           0x8D
#define CTRL_RIGHT      0xAD
#define PGUP            0x9F
#define CTRL_PGUP       0xBF
#define PGDOWN          0x9E
#define CTRL_PGDOWN     0xBE
#define INSERT          0xCD
#define CTRL_INSERT     0xED
/* DELETE is already defined in MinGW */
#define KEY_DELETE      0x7F
#define CTRL_DELETE     0x7F

/* Function key codes */
#define KEY_F0          0x80
#define SHIFT_F0        0x90
#define CTRL_F0         0xA0
#define KEY_F1          0x81
#define SHIFT_F1        0x91
#define CTRL_F1         0xA1
#define KEY_F2          0x82
#define SHIFT_F2        0x92
#define CTRL_F2         0xA2
#define KEY_F3          0x83
#define SHIFT_F3        0x93
#define CTRL_F3         0xA3
#define KEY_F4          0x84
#define SHIFT_F4        0x94
#define CTRL_F4         0xA4
#define KEY_F5          0x85
#define SHIFT_F5        0x95
#define CTRL_F5         0xA5
#define KEY_F6          0x86
#define SHIFT_F6        0x96
#define CTRL_F6         0xA6
#define KEY_F7          0x87
#define SHIFT_F7        0x97
#define CTRL_F7         0xA7
#define KEY_F8          0x88
#define SHIFT_F8        0x98
#define CTRL_F8         0xA8
#define KEY_F9          0x89
#define SHIFT_F9        0x99
#define CTRL_F9         0xA9
#define KEY_F10         0xCA
#define SHIFT_F10       0xDA
#define CTRL_F10        0xEA
#define KEY_F11         0xCB
#define SHIFT_F11       0xDB
#define CTRL_F11        0xEB
#define KEY_F12         0xCC
#define SHIFT_F12       0xDC
#define CTRL_F12        0xEC


/* holdcount and holdstack are used when decoding ANSI key sequences. If a
** sequence is read that does not correspond to an ANSI sequence the
** characters are stored here so that they can be returned by future calls
** to 'kbd_get0()'. Note that this is a *stack* not a queue.
*/
#if (defined(USE_SDL) && !defined(TARGET_MINGW)) || !defined(USE_SDL)
static int32 keyboard;          /* File descriptor for keyboard */
#endif
static int32 holdcount;		/* Number of characters held on stack			*/
static int32 holdstack[8];	/* Hold stack - Characters waiting to be passed back via 'get' */

#define INKEYMAX 0x7FFF		/* Maximum wait time for INKEY				*/
#define WAITTIME 10		/* Time to wait in centiseconds when dealing with ANSI key sequences */

/* fn_string and fn_string_count are used when expanding a function key string.
** Effectively input switches to the string after a function key with a string
** associated with it is pressed.
*/
static char *fn_string;		/* Non-NULL if taking chars from a function key string	*/
static int fn_string_count;	/* Count of characters left in function key string	*/

/* function key strings */
#define FN_KEY_COUNT 16		/* Number of function keys supported (0 to FN_KEY_COUNT-1) */
static struct {int length; char *text;} fn_key[FN_KEY_COUNT];

/* Line editor history */
#define HISTSIZE 1024		/* Size of command history buffer			*/
#define MAXHIST 20		/* Maximum number of entries in history list		*/

static int32
  place,			/* Offset where next character will be added to buffer	*/
  highplace,			/* Highest value of 'place' (= number of characters in buffer) */
  histindex,			/* Index of next entry to fill in in history list	*/
  highbuffer,			/* Index of first free character in 'histbuffer'	*/
  recalline;			/* Index of last line recalled from history display	*/

static boolean backgnd_escape=TRUE; /* Escape polled in the background			*/
static boolean enable_insert;       /* TRUE if keyboard input code is in insert mode	*/
static char histbuffer[HISTSIZE];   /* Command history buffer				*/
static int32 histlength[MAXHIST];   /* Table of sizes of entries in history buffer	*/

static int nokeyboard=0;
static int fx44x=1;

static boolean waitkey(int wait);		/* Forward reference	*/
static int32 pop_key(void);			/* Forward reference	*/
static int32 read_fn_string(void);		/* Forward reference	*/
static int32 switch_fn_string(int32 key);	/* Forward reference	*/
// static int32 decode_sequence(void);		/* Forward reference	*/

#if !defined(USE_SDL) && defined(CYGWINBUILD) /* text-mode build */
static void reinitWinConsole() {
  HANDLE hStdin;
  DWORD mode;
  hStdin = (HANDLE) _get_osfhandle(STDIN_FILENO);
  if (GetFileType(hStdin) == FILE_TYPE_CHAR) {
	GetConsoleMode(hStdin, &mode);
	mode |= (ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT);
	SetConsoleMode(hStdin, mode);
	_setmode(_fileno(stdin), O_TEXT);
	mode &= ~(ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT);
	SetConsoleMode(hStdin, mode);
	_setmode(_fileno(stdin), O_BINARY);
  }
  return;
}
#endif /* Windows Text-mode under Cygwin */

/* Keyboard initialise and finalise */
/* ================================ */

/* kbd_init() called to initialise the keyboard code
 * --------------------------------------------------------------
 * Clears the function key strings, checks if stdin is connected
 * to the keyboard or not. If it is then the keyboard functions
 * are used to read keypresses. If not, then standard C functions
 * are used instead, the assumption being that stdin is taking
 * input from a file, similar to *EXEC.
 */
boolean kbd_init() {
  int n;

  /* We do function key processing outselves */
  for (n = 0; n < FN_KEY_COUNT; n++) fn_key[n].text = NIL;
  fn_string_count = 0;
  fn_string = NIL;

  /* We provide a line editor */
  holdcount = 0;
  histindex = 0;
  highbuffer = 0;
  enable_insert = TRUE;
  set_cursor(enable_insert);

#ifdef TARGET_DOSWIN
#ifdef TARGET_DJGPP
  // DOS target
  // ----------
  struct termios tty;

  keyboard = fileno(stdin);		/* Handle to keyboard */
  if (tcgetattr(keyboard, &tty) == 0) return TRUE;		/* Keyboard being used */
//  if (tcgetattr(fileno(stdin), &tty) == 0) return TRUE;	/* Keyboard being used */
/* tcgetattr() returned an error. If the error is ENOTTY then stdin does not point at
** a keyboard and so the program does simple reads from stdin rather than use the custom
** keyboard code. If the error is not ENOTTY then something has gone wrong so we abort
** the program
*/
  if (errno != ENOTTY) return FALSE;    /* tcgetattr() returned an error we cannot handle	   */
  basicvars.runflags.inredir = TRUE;    /* tcgetattr() returned ENOTTY - use C functions for input */
  return TRUE;
#else /* !DOS */

  // Windows target, little to do
  // ----------------------------
#if !defined(USE_SDL) && defined(CYGWINBUILD) /* text-mode build */
  reinitWinConsole();
#endif
  nokeyboard=0;
  return TRUE;

#endif
#endif /* DOSWIN */

#ifdef TARGET_UNIX
  // Unix target
  // -----------
  struct termios tty;

/* Set up keyboard for unbuffered I/O */
  keyboard = fileno(stdin);
  if (tcgetattr(keyboard, &tty) < 0) {		/* Could not obtain keyboard parameters	*/
//  if (tcgetattr(fileno(stdin), &tty) < 0) {	/* Could not obtain keyboard parameters	*/
    nokeyboard=1;
/* tcgetattr() returned an error. If the error is ENOTTY then stdin does not point at
** a keyboard and so the program does simple reads from stdin rather than use the custom
** keyboard code.
*/
#ifndef USE_SDL				/* if SDL the window can still poll for keyboard input	*/
    if (errno != ENOTTY) return FALSE;  /* tcgetattr() returned an error we cannot handle	*/
    basicvars.runflags.inredir = TRUE;	/* tcgetattr() returned ENOTTY - use C functions for input */
#endif
    return TRUE;
  }

/* We are connected to a keyboard, so set it up for unbuffered input */
  origtty = tty;			/* Preserve original settings for later	*/
#ifdef TARGET_LINUX
  tty.c_lflag &= ~(XCASE|ECHONL|NOFLSH); /* Case off, EchoNL off, Flush off	*/
#else
  tty.c_lflag &= ~(ECHONL|NOFLSH);	 /* EchoNL off, Flush off		*/
#endif
  tty.c_lflag &= ~(ICANON|ECHO);	/* Line editor off, Echo off		*/
  tty.c_iflag &= ~(ICRNL|INLCR);	/* Raw LF and CR			*/
  tty.c_cflag |= CREAD;			/* Enable reading			*/
  tty.c_cc[VTIME] = 1;			/* 1cs timeout				*/
  tty.c_cc[VMIN] = 1;			/* One character at a time		*/
//  tty.c_cc[VINTR] = 27; // doing this here stops get0() working and kills tbrandy
  if (tcsetattr(keyboard, TCSADRAIN, &tty) < 0) return FALSE;
					/* Could not set up keyboard in the way desired	*/
  return TRUE;
#endif /* UNIX */

#ifdef TARGET_AMIGA
  // AMIGA target - just turn Console on
  // -----------------------------------
  rawcon(1);
  return TRUE;
#endif /* AMIGA */
}


/* kbd_quit() called to terminate keyboard control on termination */
/* -------------------------------------------------------------- */
void kbd_quit() {
#ifdef TARGET_DOSWIN
  // DOS/Windows target, nothing to do
  // ---------------------------------
#endif /* DOSWIN */

#ifdef TARGET_UNIX
  // Unix target - restore console settings
  // --------------------------------------
  (void) tcsetattr(keyboard, TCSADRAIN, &origtty);
#endif /* UNIX */

#ifdef TARGET_AMIGA
  // AMIGA target - turn console off
  // -------------------------------
  rawcon(0);
#endif /* AMIGA */
}


/* Test state of keyboard system */
/* ============================= */

/* kbd_buffered() - is there anything in the keyboard buffer - ADVAL(-1) */
/* --------------------------------------------------------------------- */
int32 kbd_buffered() {
  int num=0;
#ifdef TARGET_UNIX
  ioctl(STDIN_FILENO, FIONREAD, &num);		/* Count bytes in keyboard buffer	*/
#endif
  return num;
}

/* kbd_pending() - will the next GET/INKEY fetch something - EOF#0 */
/* --------------------------------------------------------------- */
int32 kbd_pending() {
  if (matrixflags.doexec) {
    if (!feof(matrixflags.doexec)) return TRUE;	/* Still bytes from exec file		*/
    }
  if (holdcount) return TRUE;			/* Pushed keypresses pending		*/
  if (fn_string_count) return TRUE;		/* Soft key being expanded		*/
  return kbd_buffered()!=0;			/* Test keyboard buffer			*/
}

/* kbd_escpoll() - is there a pending Escape state */
/* ----------------------------------------------- */
/* With background keypress processing, this just tests the flag set by the background,
 * similar to BIT ESCFLG in other BASICs. However, on some targets we can't see
 * keypresses in the background, we don't have an equivalent of SIGINT, so this routine
 * also polls the Escape key.
 * To do: the Escape key needs to be definable, but in this routine we need the keynumber
 * not the character code.
 */
int kbd_escpoll() {
#ifdef USE_SDL
int64 tmp;
#endif

  if (backgnd_escape) {				/* Only poll when not doing key input	*/
    if (kbd_esctest()) {			/* Only poll if Escapes are enabled	*/
#ifdef USE_SDL
      tmp=basicvars.centiseconds;
      if (tmp > esclast) {
        esclast=tmp;
        if (kbd_inkey(-113)) basicvars.escape=TRUE;	// Should check key character, not keycode
      }
#else
#ifdef TARGET_MINGW
      if (GetAsyncKeyState(VK_ESCAPE)) {		// Should check key character, not keycode
        while (GetAsyncKeyState(VK_ESCAPE));	/* Wait until key not pressed		*/
        basicvars.escape=TRUE;
      }
#endif
#endif
    }
  }
  return basicvars.escape;			/* Return Escape state			*/
}

/* kbd_esctest() - set Escape state if allowed */
/* ------------------------------------------- */
int kbd_esctest() {
  if (sysvar[sv_EscapeAction]==0) {		/* Does Escape key generate Escapes?	*/
    if ((sysvar[sv_EscapeBreak] & 1)==0) {	/* Do Escapes set Escape state?		*/
      return TRUE;
    }
  }
  return FALSE;
}

void kbd_escchar(char a, char b) { return; }		// set Escape character
void kbd_escset()   { basicvars.escape=TRUE; }		// set Escape state
void kbd_escclr()   { basicvars.escape=FALSE; }		// clear Escape state

/* kbd_escack() - acknowledge and clear Escape state */
/* ------------------------------------------------- */
int kbd_escack() {
  byte tmp;

  tmp=sysvar[sv_EscapeEffect] ^ 0x0f;
  if (basicvars.escape == 0) tmp=tmp | (tmp >> 4);
  if (tmp & 1) {
    if (matrixflags.doexec) {				/* Close EXEC file		*/
      fclose(matrixflags.doexec);
      matrixflags.doexec=NULL;
    }
    holdcount=0;					/* Cancel pending keypress  	*/
    fn_string_count=0; fn_string = NIL;			/* Cancel soft key expansion	*/
#ifdef TARGET_MINGW
    FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE)); /* Consume any queued characters */
#endif
    purge_keys();					/* Cancel pending keypresses	*/
//  cancel VDU queue
//  cancel sounds
  }
#ifdef USE_SDL
  if (tmp & 2) { star_refresh(1); }			/* Refresh screen		*/
#endif
  if (tmp & 4) { }
  if (tmp & 8) { }
  tmp=basicvars.escape;
  basicvars.escape=FALSE;				/* Clear pending Escape		*/
  return tmp ? -1 : 0;					/* Return previous Escape state	*/
}

/* kbd_modkeys() - do a fast read of state of modifier keys */
/* -------------------------------------------------------- */
/* Equivalent to the BBC MOS OSBYTE 118/KEYV call
 * On entry: arg is a bitmap of modifier keys to test for
 *           bit<n> tests key<n>
 *           ie, b0=Shift, b1=Ctrl, b2=Alt, etc.
 * Returns:  bitmap of keys pressed, as requested
 *           bit<n> set if key<n> pressed and key<n> was asked for
 *           ie, kbd_modkeys(3) will test Shift and Ctrl
 *
 * Currently, only SHIFT tested by SDL for VDU paged scrolling.
 */
int32 kbd_modkeys(int32 arg) {
#ifdef USE_SDL
  if (keystate==NULL) return 0;				/* Not yet been initialised	*/
  if (keystate[SDLK_LSHIFT] || keystate[SDLK_RSHIFT])	/* Either SHIFT key		*/
    return 1; else return 0;
#endif
  return kbd_inkey(-1) & 0x01;				/* Just test SHIFT for now	*/
}


#ifdef TARGET_DJGPP
/* GetAsyncKeyState() is a Windows API call, DOS only has API call to read Shift/Ctrl/Alt.
 * ---------------------------------------------------------------------------------------
 * So, we fake an API just for those keys, useful for programs that do
 *  ON ERROR IF INKEY-1 THEN REPORT:END ELSE something else
 * If running in DOSBOX on Windows would be able to call Windows API, but simpler to
 * just run a Windows target. Somebody with more skill than me could write the code
 * to call Windows from DOS to implement this.
 */
//static int32 vecGetAsyncKeyState=0;
int GetAsyncKeyState(int key) {
  int y=0;

  y=_bios_keybrd(_NKEYBRD_SHIFTSTATUS);
  switch (key) {
    case VK_SHIFT:    y=(y & 0x003); break;	/* SHIFT	*/
    case VK_CONTROL:  y=(y & 0x004); break;	/* CTRL		*/
    case VK_MENU:     y=(y & 0x008); break;	/* ALT		*/
    case VK_LSHIFT:   y=(y & 0x002); break;	/* Left SHIFT	*/
    case VK_LCONTROL: y=(y & 0x100); break;	/* Left CTRL	*/
    case VK_LMENU:    y=(y & 0x200); break;	/* Left ALT	*/
    case VK_RSHIFT:   y=(y & 0x001); break;	/* Right SHIFT	*/
    case VK_RCONTROL: y=(y & 0x400); break;	/* Right CTRL	*/
    case VK_RMENU:    y=(y & 0x800); break;	/* Right ALT	*/
    default: y=0;
  }
  return (y ? -1 : 0);
}
//  __asm__ __volatile__(
//  "cld\n"
//  "movw %w1,  %%ax\n"
//  "andw $255, %%ax\n"
//  "push %%ax\n"
//  "call [vecGetAsyncKeyState]\n"
//  "movw %%ax, %w0\n"
//  : "=r" (y)
//  : "r" (x)
//  );
//  return y;
#endif


/* Programmable function key functions */
/* =================================== */

/* kbd_fnkeyset() - define a function key string */
/* --------------------------------------------- */
/* Definition is defined by length, so can contain NULs
 * Returns:   0 if ok
 *          <>0 if can't set because key is in use
 */
//  kbd_fnkeyset(int key, int length, char *string) {
int kbd_fnkeyset(int key, char *string, int length) {
  if (fn_string_count)         return fn_string_count;	/* Key in use			*/
  if (fn_key[key].text != NIL) free(fn_key[key].text);	/* Remove existing definition	*/
  fn_key[key].length = length;
  fn_key[key].text   = malloc(length);			/* Get space for new definition	*/
  if (fn_key[key].text != NIL) memcpy(fn_key[key].text, string, length);
  return 0;						/* Ok				*/
}


/* kbd_fnkeyget() - get a function key string for *SHOW */
/* ---------------------------------------------------- */
/* Returns: string which can include NULs
 *          len updated with length of string
 */
char *kbd_fnkeyget(int key, int *length) {
  *length=fn_key[key].length;
  return fn_key[key].text;
}


/* kbd_isfnkey() - check if keycode is a function key */
/* -------------------------------------------------- */
/* Returns: <0 if not a RISC OS function key code
 * Returns:    function key number if a function key
 * NB: INSERT is actually function key 13.
 */
int32 kbd_isfnkey(int32 key) {
  if (key & 0x100) {
    key = key & 0xFF;
    if (key >= KEY_F0  && key <= KEY_F9)  return key - KEY_F0;
    if (key >= KEY_F10 && key <= KEY_F12) return key - KEY_F10 + 10;
    if (matrixflags.osbyte4val == 2) {
      if ((key >= 0x8B) && (key <= 0x8F)) return (key - 0x80);
    }
  }
  return -1;						/* Not a function key		*/
}


/* switch_fn_string() - Called to switch input to a function key string */
/* -------------------------------------------------------------------- */
/* Returns: first character of the string
 */
static int32 switch_fn_string(int32 key) {
  int32 ch;
  if (fn_key[key].length == 1) return *fn_key[key].text;
  fn_string = fn_key[key].text;
  fn_string_count = fn_key[key].length - 1;
  ch = *fn_string;
  fn_string++;
  return ch;
}


/* read_fn_string() - Called when input being taken from a function key string */
/* --------------------------------------------------------------------------- */
/* Returns: the next character in the string
 *          if last character fetched, fn_string set to NIL
 */
static int32 read_fn_string(void) {
  int32 ch;
  ch = *fn_string;
  fn_string++;
  fn_string_count--;
  if (fn_string_count == 0) fn_string = NIL;    /* Last character read */
  return ch;
}


/* Main key input functions */
/* ======================== */

/* kbd_inkey() called to implement Basic INKEY and INKEY$ functions */
/* ---------------------------------------------------------------- */
/* On entry: <&7FFF  : wait for centisecond time for 8-bit keypress
 * On exit:  <0      : escape or timed out (always -1)
 *           >=0     : 8-bit keypress
 *
 * On entry: &FF00   : return a value indication the type of host hardware
 *           &FF80+n : test for BBC keypress n
 *           &FF00+n : scan for BBC keypress starting from n         (rarely implemented)
 *           &FE00+n : test for DOS/Windows keypress n               (extension)
 *           &FC00+n : test for SDL keypress n                       (extension)
 * On exit:  -1      : key pressed
 *           0       : key not pressed
 *
 * On entry: &8000+n : wait for centisecond time for 16-bit keypress (extension)
 * On exit:  <0      : escape or timed out (always -1)
 *           >=0     : 16-bit keypress
 *
 * If unsupported: return 0 (except &FF00+n returns -1 if unsupported)
 */
int32 kbd_inkey(int32 arg) {
  arg = arg & 0xFFFF;				/* Argument is a 16-bit number		*/

  // Return host operating system
  // ----------------------------
  if (arg == 0xFF00) return OSVERSION;

  // Timed wait for keypress
  // -----------------------
  if (arg < 0x8000) {
    if (basicvars.runflags.inredir		/* Input redirected			*/
      || matrixflags.doexec			/*  or *EXEC file active		*/
      || fn_string_count) return kbd_get();	/*  or function key active		*/
    if (holdcount > 0) return pop_key() & 0xFF;	/* Character waiting so return it	*/
    if (waitkey(arg))  return kbd_get() & 0xFF;	/* Wait for keypress and return it	*/
    else	       return -1;		/* Otherwise return -1 for nothing	*/

  // Negative INKEY - scan for keypress
  // ----------------------------------
  } else {
    arg = arg ^ 0xFFFF;				/* Convert to keyscan number		*/

#ifdef USE_SDL
    SDL_Event ev;

    while (matrixflags.videothreadbusy) usleep(1000);
    matrixflags.noupdate = 1;
    int mx, my;
    keystate = SDL_GetKeyState(NULL);
    mousestate = SDL_GetMouseState(&mx, &my);
    while(SDL_PeepEvents(&ev, 1, SDL_GETEVENT, SDL_ALLEVENTS&(~SDL_KEYEVENTMASK))) {
      switch(ev.type) {
        case SDL_QUIT:
          exit_interpreter(EXIT_SUCCESS);
          break;
        case SDL_MOUSEBUTTONDOWN:
          if (ev.button.button == SDL_BUTTON_LEFT) mousebuttonstate |= 4;
          if (ev.button.button == SDL_BUTTON_MIDDLE) mousebuttonstate |= 2;
          if (ev.button.button == SDL_BUTTON_RIGHT) mousebuttonstate |= 1;
          add_mouseitem(mx, my, mousebuttonstate, basicvars.centiseconds);
          break;
        case SDL_MOUSEBUTTONUP:
          if (ev.button.button == SDL_BUTTON_LEFT) mousebuttonstate &= 3;
          if (ev.button.button == SDL_BUTTON_MIDDLE) mousebuttonstate &= 5;
          if (ev.button.button == SDL_BUTTON_RIGHT) mousebuttonstate &= 6;
          add_mouseitem(mx, my, mousebuttonstate, basicvars.centiseconds);
          break;
      }
    }
    matrixflags.noupdate = 0;

    if (arg <= 2) {				/* Test either modifier key		*/
      if (
      (keystate[inkeylookup[arg + 3]])		/* left key  */
      ||
      (keystate[inkeylookup[arg + 6]])		/* right key */
      ) return -1;
      else
        return 0;
    }

    if ((arg >= 9) && (arg <= 11)) {		/* Test mouse buttons			*/
      if ((arg ==  9) && (mousestate & 1)) return -1;
      if ((arg == 10) && (mousestate & 2)) return -1;
      if ((arg == 11) && (mousestate & 4)) return -1;
      return 0;
    }

    if (arg < 0x080) {				/* Test for single keypress, INKEY-key	*/
      switch (arg) {
#ifdef TARGET_DOSWIN
						/* Not visible from SDL keyscan		*/
        case 32:  return (GetAsyncKeyState(0x2C)<0 ? -1 : 0);		/* F0/PRINT	*/
        case 95:  return (GetAsyncKeyState(0xE2)<0 ||
			  GetAsyncKeyState(0xC1)<0 ? -1 : 0);		/* Right \_	*/
        case 109: return (GetAsyncKeyState(0x1D)<0 ||
			  GetAsyncKeyState(0xEB)<0 ? -1 : 0);		/* NoConvert	*/
        case 110: return (GetAsyncKeyState(0x1C)<0 ? -1 : 0);		/* Convert	*/
				/* Kana is fiddly as it toggles between F0, F1, F2	*/
        case 111: return (GetAsyncKeyState(0x15)<0 ? -1 : 0);		/* Kana		*/

						/* Exceptions from SDL keyscan		*/
        case 46:  return (keystate[0x5C] !=0 &&
			  GetAsyncKeyState(0xDC) < 0  ? -1 : 0);	/* UKP/Y/etc	*/
        case 90:  return (keystate[0x5C] !=0 &&
			  GetAsyncKeyState(0xDC) == 0 ? -1 : 0);	/* #~  Keypad#	*/
#endif
						/* Translate SDL keyscan		*/
        default:  return ((keystate[inkeylookup[arg]] != 0) ? -1 : 0);
      }
    }

    if (arg < 0x100) return -1;			/* Scan range - unimplemented		*/

#ifdef TARGET_DOSWIN
    if (arg < 0x200) {				/* Direct DOS keyscan, INKEY(&FE00+nn)	*/
      if (GetAsyncKeyState(arg ^ 0x1ff)<0) return -1;	/* Key pressed			*/
      else				   return 0;	/* Key not pressed		*/
    }
#endif

    if ((arg & 0xFE00) == 0x0200) {		/* Direct SDL keyscan, INKEY(&FC00+nn)	*/
      return ((keystate[arg ^ 0x3FF] != 0) ? -1 : 0);
    }

    return 0;					/* Anything else, return FALSE		*/

#else /* !USE_SDL */

#ifdef VK_SHIFT
    /* adapted from con_keyscan() from JGH 'console' library */
    if (arg < 0x080) {				/* Test for single keypress, INKEY-key	*/
#ifndef TARGET_DJGPP				/* DOS doesn't support GetKbdLayout	*/
      if (((size_t)(GetKeyboardLayout(0)) & 0xFFFF)==0x0411) {	/* BBC layout keyboard	*/
	/* Note: as a console app, this is the ID from when the program started		*/
        switch (arg) {
          case 24: return (GetAsyncKeyState(0xDE)<0 ? -1 : 0);	/* ^~        		*/
          case 46: return (GetAsyncKeyState(0xDC)<0 ? -1 : 0);	/* UKP/Y/etc 		*/
          case 72: return (GetAsyncKeyState(0xBA)<0 ? -1 : 0);	/* :         		*/
          case 87: return (GetAsyncKeyState(0xBB)<0 ? -1 : 0);	/* ;         		*/
          case 90:						/* #~ or Keypad#	*/
          case 93:						/* =+        		*/
          case 94:						/* Left \|   		*/
          case 120:						/* \|        		*/
            return 0;
        }
      }
#endif /* !DJGPP */
      arg=inkeylookup[arg];			/* Get translated keyscan code		*/
      if (arg) {
        if (GetAsyncKeyState(arg)<0) return -1; /* Translated key pressed		*/
        else			     return 0;  /* Translated key not pressed		*/
      }
      return 0;					/* No translated key to test		*/
    }

    if (arg < 0x100) return -1;			/* Scan range - unimplemented		*/

    if (arg < 0x200) {				/* Direct DOS keyscan, INKEY(&FE00+nn)	*/
      if (GetAsyncKeyState(arg ^ 0x1ff)<0) return -1;	/* Key pressed			*/
      else				   return 0;	/* Key not pressed		*/
    }

    return 0;					/* Anything else, return FALSE		*/
#endif /* VK_SHIFT */

#endif /* !USE_SDL */

/* AMIGA, BEOS, non-SDL UNIX remaining */
    if ((arg & 0xFF80)==0x0080) return -1;	/* Scan range - unimplemented		*/

#ifdef TARGET_AMIGA
/* A KeyIO call with command KBD_READMATRIX reads the keyboard matrix to a 16-byte buffer,
 * one bit per keystate, similar to the SDL GetKeyState call.
 */
    return 0;					/* For now, return NOT PRESSED		*/
#endif
#ifdef TARGET_BEOS
    return 0;					/* For now, return NOT PRESSED		*/
#endif
  }
  return 0;					/* Everything else, return NOT PRESSED	*/
}


/* kbd_get() called to implement Basic GET and GET$ functions */
/* ---------------------------------------------------------- */
/* Returns 9-bit value so line editor can capture special keys
 * Fetches from: *EXEC file
 *               Input redirection
 *               Active function key string
 *               Pending keys from untranslated ANSI keycodes
 *               Keyboard buffer, translated to RISCOS-style values
 */
int32 kbd_get(void) {
  int ch, fnkey;
//  int cooked;
//  int raw=0;

  if (matrixflags.doexec) {			/* Are we doing *EXEC?			*/
    if (kbd_escpoll()) error(ERR_ESCAPE);
    ch=fgetc(matrixflags.doexec);
    if (!feof(matrixflags.doexec)) return (ch & BYTEMASK);
    fclose(matrixflags.doexec);
    matrixflags.doexec=NULL;
  }

  if (basicvars.runflags.inredir) {		/* Input redirected at command line	*/
#if defined(TARGET_UNIX) || defined(CYGWINBUILD)
    if ((ch=getchar()) != EOF) return ch;
#else
    if ((ch=getch()) != EOF) return ch;
#endif
    else error(ERR_READFAIL);			/* I/O error occured on STDIN		*/
  }

  if (fn_string != NIL) return read_fn_string(); /* Function key active			*/

// To do, allow *FX221-8 to specify special keypress expansion and *FX4 cursor key control
// For the moment, &18n<A and &1Cn>9 are function keys, &18n>9 are cursor keys

  backgnd_escape=FALSE;				/* We poll Escape during key input	*/
  ch=kbd_get0();				/* Get a keypress from 'keyboard buffer'*/
  backgnd_escape=TRUE;

//  cooked=(sysvar[sv_KeyOptions]&192)==192;
//  if (cooked) {
    if (ch & 0x100) {				/* Translate special keys		*/
      if ((ch & 0x00F) >= 10)   ch=ch ^ 0x40;	/* Swap to RISC OS ordering		*/
      if ((ch & 0x0CE) == 0x8A) ch=ch ^ 0x14;	/* PGDN/PGUP */
      if ((ch & 0x0CF) == 0xC9) ch=ch - 62;	/* END       */
      if (ch == 0x1C8)          ch=30;		/* HOME      */
      if (ch == 0x1C7)          ch=127;		/* DELETE    */
      if ((ch & 0x0CF) == 0xC6) ch=ch + 7;	/* INSERT    */
      if (matrixflags.osbyte4val == 1) {
        if ((ch >= 0x18B) && (ch <= 0x18F)) ch -=0x104;
      }
    }
//  }

#if defined(TARGET_MINGW) || defined(USE_SDL)
  while (kbd_escpoll()) basicvars.escape=FALSE;	/* Rather brute-force			*/
#endif
  if (ch == sysvar[sv_EscapeChar]) {
    if (kbd_esctest()) basicvars.escape=TRUE;	/* If not ASCII key, set Escape state	*/
  }
  if ((fnkey = kbd_isfnkey(ch)) < 0) return ch;	/* Not a function key			*/
  if (fn_key[fnkey].length == 0)     return ch;	/* Function key undefined		*/
  return switch_fn_string(fnkey);		/* Switch and return first char		*/
}


/* kbd_readline() - read a line of text from input stream
** --------------------------------------------------------------------
** On entry:
** char *buffer - address to place text
** int32 length - size of buffer, maximum line length-1
** int32 chars  - processing characters:
**       b0-b7:   echo character
**       b8-b15:  lowest acceptable character
**       b16-b23: highest acceptable character
**       b24-b31: flags: b31=use echo character
** Returns:
**       int32:   >=0 - line length read, also offset to terminator
**                 <0 - failed
**
** The data input is stored at 'buffer'. Up to 'length' characters
** can be read. A 'null' is added after the last character. 'buffer'
** can be prefilled with characters if required to allow existing text
** to be edited. Note that memory after the 'null' may be overwritten
** up to the maximum size of the buffer.
**
** Note that 'length' is defined differently to BBC/RISC OS, where 'length'
** is the buffer size-1. That is, the length is the highest possible offset
** to the terminator, where a 'length' of zero requires a buffer size of
** one to allow the terminator to be at buffer[0]. In Brandy, 'length' is
** the size of the buffer, so a 'length' of one results in the terminator
** being at buffer[0], and a 'length' of zero is illegal.
**
** The readline function provides both DOS and Unix style line editing
** facilities and line history, for example, both 'HOME' and control 'A'
** move the cursor to the start of the line.
**
** There is a problem with LCC-WIN32 running under Windows 98 in that the
** extended key codes for the cursor movement keys (insert, left arrow
** and so on) are not returned correctly by 'getch()'. In theory they
** should appear as a two byte sequence of 0xE0 followed by the key code.
** Only the 0xE0 is returned. This appears to be a bug in the C runtime
** library.
** -------------------------------------------------------------------- */
int32 kbd_readline(char *buffer, int32 length, int32 chars) {
  if (length<1) return 0;			/* Filter out impossible or daft calls	*/
  chars=(chars & 0xFF) | 0x00FF2000;		/* temp'y force all allowable chars	*/

// Temp'y patch, copy keyboard sysvars into basicvars.*
// basicvars.escape_enabled=!sysvar[sv_EscapeAction];
basicvars.escape=FALSE;

  length=(int32)emulate_readline(&buffer[0], length, chars & 0xFF);
  if (length==READ_OK)  return strlen(buffer);
  if (length==READ_ESC) return -1;
  return -2;
}

/*
** From here onwards are internal routines used by the above keyboard routines on
** non-RISC OS builds.
** ==============================================================================
 */
#if defined(TARGET_DOSWIN) && !defined(USE_SDL)
/*
** This table gives the mapping from DOS extended key codes to modified RISC OS equivalents.
** These 'keyboard buffer' values are slightly different to RISC OS values to make them
** easier to translate from host values. The low level 'buffer' values are the regular
** 'Console library' values where all function keys are 0x180+n and non-function special
** keys are all 0x1C0+n. They are then translated higher up to the RISC OS values documented
** in the PRMs.
**
** Under DOS, the special keys, for example, 'Home', 'Page Up', 'F1' and so forth, appear
** as a two byte sequence where the first byte is an escape character and the second
** denotes the key. The escape character for DJGPP is always a null. The LCC-WIN32
** version of 'getch' uses the value 0xE0 for keys such as 'Home' but zero for the
** function keys. It also uses zero for the 'alt' versions of the keys.
*/
static byte dostable[] = {
0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07, /* 00-07			*/
0xc2,0xc3,0xc5,0x0b,0x0c,0x0d,0xc2,0xc3, /* sBS,sTAB,sRET,0C-0E,sTAB	*/
0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17, /* 10-17			*/
0x18,0x19,0x1a,0xc3,0x1c,0x1d,0x1e,0x1f, /* 18-1A,sESC,1C-1F		*/
0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27, /* 20-27			*/
0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f, /* 28-2F			*/
0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37, /* 30-37			*/
0x38,0x39,0x3a,0x81,0x82,0x83,0x84,0x85, /* 38-3A,F1-F5			*/
0x86,0x87,0x88,0x89,0x8a,0x45,0x46,0xc8, /* F6-F10,45,46,Home		*/
0xcf,0xcb,0x4a,0xcc,0x4c,0xcd,0x4e,0xc9, /* Up,PgUp,4A,<-,4C,->,4E,End	*/
0xce,0xca,0xc6,0xc7,0x91,0x92,0x93,0x94, /* Down,PgDn,Ins,Del,sF1-sF4	*/
0x95,0x96,0x97,0x98,0x99,0x9a,0xa1,0xa2, /* sF5-sF10,cF1,cF2		*/
0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa, /* cF3-cF10			*/
0xb1,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8, /* aF1-aF8			*/
0xb9,0xba,0xa0,0xcc,0xcd,0xc9,0xca,0xc8, /* aF9-aF10,cPrint,c<-,c->,cEnd,cPgDn,cHome	*/
0xcb,0x79,0x7a,0x7b,0x7c,0x7d,0x7e,0x7f, /* cPgUp,79-7F					*/
0x80,0x81,0x82,0x83,0xcb,0x8b,0x8c,0x9b, /* 80-83,cPgUp,F11-12,sF11			*/
0x9c,0xab,0xac,0xbb,0xbc,0xcf,0x8e,0x8f, /* sF12,cF11-12,aF11-12,cUp,8E-8F		*/
0x90,0xce,0xc6,0xc7,0xc3,0x95,0x96,0xc8, /* 90,cDown,cIns,cDel,cTab,95,96,aHome		*/
0xcf,0xcb,0x9a,0xcc,0x9c,0xcd,0x9e,0xc9, /* aUp,aPgUp,9A,a<-,9C,a->,9E,aEnd		*/
0xce,0xca,0xc6,0xc7,0xa4,0xc3,0xa6,0xa7, /* aDn,aPgDn,aIns,aDel,A4,aTab,A6-A7		*/
0xa8,0xa9,0xaa,0xab,0xac,0xad,0xae,0xc2, /* A8-AE,WIDTH					*/
0xc2,0xc2,0xf3,0xb3,0xb4,0xb5,0xb6,0xb7, /* sWIDTH,cWIDTH,aWIDTH			*/
0xb8,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,0xbf,
0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,
0xc8,0xc9,0xca,0xcb,0xcc,0xcd,0xce,0xcf,
0xd0,0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,
0xd8,0xd9,0xda,0xdb,0xdc,0xdd,0xde,0xdf,
0xe0,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,
0xe8,0xe9,0xea,0xeb,0xec,0xed,0xee,0xef,
0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,
0xf8,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff };
#endif

#if defined(TARGET_UNIX) && !defined(USE_SDL)
/* This table gives the mapping from Unix ANSI keypresses to modified RISC OS keycodes.
** As with the DOS translations, these 'keyboard buffer' values are slightly different
** to RISC OS values to make them easier to translate from host values. The low level
** 'buffer' values are the regular 'Console library' values where all function keys are
** 0x180+n and non-function special keys are all 0x1C0+n. They are then translated higher
** up to the RISC OS values documented in the PRMs.
*/
unsigned char ansikey[]={
0x00,0xC8,0xC6,0xC7,0xC9,0xCB,0xCA,0xC8, /* 0,Home,Ins,Del,End,PgUp,PgDn,Home */
0xC9,0x00,0x00,0x81,0x82,0x83,0x84,0x85, /* End,9,10,F1,F2,F3,F4,F5           */
0x00,0x86,0x87,0x88,0x89,0x8A,0x00,0x8B, /* 16,F6,F7,F8,F9,F10,22,F11      */
0x8C,0x8D,0x8E,0x00,0x8F,0x00,0x00,0x00, /* F12,F13,F14,27,F15,F16,30,F17  */
0x00,0x00,0x00,0x00,0xCC,0xCD,0xCE,0xCF  /* F18,F19,F20,35,<-,->,Down,Up   */
};
#endif


/* kbd_get0() is the low level code to fetch a keypress from the keyboard input.
 * It is the equivalent of the BBC/RISC OS code to fetch from the current input
 * buffer, called by INKEY/OSRDCH. It fetches host-specific keycodes and translates
 * them into regular keycodes that the caller can then map easily.
 * The returned value is a 16-bit value similar to the RISC OS Wimp keypress values.
 * &000+n  : character code
 * &100+n  : special key (function keys, etc) with b5/b4=Normal/Shift/Ctrl/Alt
 *  &180+n : function keys
 *  &1C0+n : non-function special keys (cursors, etc.)
 * (note, &100-&17F reserved)
 * This code should not expand function keys, that should be done by the caller.
 * -----------------------------------------------------------------------------------
 * Keyboard buffer NULL prefixing is the opposite to RISC OS.
 * So, swap the prefixing around to convert to RISC OS mapping.
 * This uses the guts from JGH Console library.
 * Tweek a bit to regularise low-level keycodes before being converted to RISC OS codes.
 * ----------------------------------------------------------------------------------- */
int32 kbd_get0(void) {
  int ch;

#if defined(TARGET_DOSWIN) && !defined(USE_SDL)
  int s,c,a;

#ifdef CYGWINBUILD
  ch=getchar();
#else
  ch=getch();
#endif /* CYGWINBUILD */
  if (ch == NUL || ch == 0xE0) {		/* DOS escaped characters		*/
    // When kbd_modkeys() returns all keys, change this to call it
    s=(GetAsyncKeyState(VK_SHIFT)<0);		/* Check modifier keys			*/
    c=(GetAsyncKeyState(VK_CONTROL)<0);
    if((a=(GetAsyncKeyState(VK_MENU)<0))) c=0;
#ifdef CYGWINBUILD
    ch=getchar();
#else
    ch=getch();
#endif /* CYGWINBUILD */
    if (ch == 0x29) return 0xAC;		/* Alt-top-left key			*/
    if (ch == 0x86) if (c) ch=0x78;		/* Separate F12 and cPgUp		*/
    ch=dostable[ch];				/* Translate escaped character		*/
    if ((ch & 0xC0) == 0xC0) {			/* Non-function keys need extra help	*/
      if (s) ch=ch ^ 0x10;			/* SHIFT pressed			*/
      if (c) ch=ch ^ 0x20;			/* CTRL pressed				*/
      if (a) ch=ch ^ 0x30;			/* ALT pressed				*/
    }
    return ch | 0x100;				/* 0x100+nn - top-bit special keys	*/
  }
  return ch;					/* 0x00+nn - normal keypress		*/
#else /* not (DOSWIN and !USE_SDL) */

 #if defined(TARGET_UNIX) && !defined(USE_SDL)
  int key, mod;

  fflush(stdout);
  read(STDIN_FILENO, &ch, 1);			/* Read without flushing		*/
  ch=ch & 0xFF;
  if (ch != 27) return ch;			/* Not <esc>				*/
  ioctl(STDIN_FILENO, FIONREAD, &key);		/* Count bytes in keyboard buffer	*/
  if (key == 0) return ch;			/* Nothing pending			*/

/* ANSI key sequence is:
 *    <esc> [ (<num>) (;<num>) non-digit
 * or <esc> O non-digit				*/

  key=0; mod=0;
  ch=getchar();					/* Get next character			*/
  if (ch=='O') mod=1;				/* Convert <esc>O to <esc>[1		*/
  else if (ch != '[') return (ch | 0x100);	/* Not opening <esc>[ or <esc>O		*/

  while ((ch=getchar())<'@') {			/* Parse through non-alphas		*/
    if (ch>='0' && ch<='9') {			/* Digit, add to current num		*/
      mod=mod*10+(ch-'0');
      }
    if (ch==';') {				/* Semicolon, step to next number	*/
      key=mod; mod=0;
      }
    }
  if (key==0) { key=mod; mod=1; }		/* Special cases			*/
  if (ch>='A' && ch<='D') key=39-(ch-'A');	/* Cursor keys				*/
  if (ch>='P' && ch<='S') key=11+(ch-'P');	/* f1 - f4				*/
  if (ch=='F') key=4;				/* End					*/
  if (ch=='H') key=1;				/* Home					*/

  if (key > 39) return 0;			/* Out of range (should loop)		*/
  ch=ansikey[key];				/* Translate keypress			*/

  mod=mod-1;					/* Convert modifiers to bitmap		*/
  if (mod & 1) ch=ch ^ 0x10;			/* SHIFT pressed			*/
  if (mod & 4) ch=ch ^ 0x20;			/* CTRL pressed				*/
  if (mod & 2) ch=ch ^ 0x30;			/* ALT pressed				*/
  return (ch | 0x100);

 #else
  if ((ch=read_key()) != 0)   return ch;	/* Fetch keypress or NULL		*/
  if ((ch=read_key()) & 0x80) ch = ch | 0x100;	/* Fetch extended byte from keyboard	*/
  return ch;
 #endif
#endif
}


/* Legacy code from here onwards */
/* ----------------------------- */
#if defined(TARGET_UNIX) | defined(TARGET_MACOSX) | defined(TARGET_GNU)\
 | defined(TARGET_AMIGA) & defined(__GNUC__)

static struct termios origtty;  /* Copy of original keyboard parameters */
static int32 keyboard;          /* File descriptor for keyboard */

#endif

/* The following functions are common to all operating systems */

/*
** 'push_key' adds a key to the held key stack
*/
void push_key(int32 ch) {
  holdcount++;
  holdstack[holdcount] = ch;
}

/*
** pop_key - Remove a key from the held key stack
*/
static int32 pop_key(void) {
  return holdstack[holdcount--];
}

/*
** purge_keys - Flattens the holding stack
*/
void purge_keys(void) {
#ifdef USE_SDL
  SDL_Event ev;
  holdcount = 0;
  while(SDL_PeepEvents(&ev, 1, SDL_GETEVENT, SDL_ALLEVENTS)) ;
#else
#endif
  while (kbd_inkey(0)>-1);		/* Suck everything out of keyboard	*/
}

void osbyte21(int32 xreg) {
#ifdef USE_SDL
  SDL_Event ev;

  switch(xreg) {
    case 0:
      while(SDL_PeepEvents(&ev,1,SDL_GETEVENT, SDL_EVENTMASK(SDL_KEYDOWN) | SDL_EVENTMASK(SDL_KEYUP))) ;
      break;
    case 9:
      while(SDL_PeepEvents(&ev,1,SDL_GETEVENT, SDL_EVENTMASK(SDL_MOUSEBUTTONDOWN) | SDL_EVENTMASK(SDL_MOUSEBUTTONUP) | SDL_EVENTMASK(SDL_MOUSEMOTION))) ;
      drain_mousebuffer();
      break;
  }
#endif
}

// int64 esclast=0;

// Should be called kbd_something, escenabled should be tested here, should be a function
/* The check for escape_enabled moved to the calling point in statement.c */
void checkforescape(void) {
#ifdef USE_SDL
int64 i;
  i=basicvars.centiseconds;
  if (i > esclast) {
    esclast=i;
// Should check key character, not keycode
    if(kbd_inkey(-113)) basicvars.escape=TRUE;
  }
#endif
  return;
}

// Should be called kbd_something
void osbyte44(int x) {
  fx44x=x;
}

// Called by kbd_inkey()
#if defined(TARGET_DJGPP)
// Should be merged with following
static boolean waitkey(int wait) {
  int tmp;
  tmp=clock()+wait; /*(CLOCKS_PER_SEC/100); */
  for(;;) { if (kbhit() || (clock()>tmp)) break; }
  return kbhit();
}
#endif

#if defined(TARGET_UNIX) | defined(TARGET_MACOSX) | defined(TARGET_GNU) | defined(TARGET_MINGW)\
 | defined(TARGET_AMIGA) & defined(__GNUC__)


// Called by kbd_inkey()
/*
** 'waitkey' is called to wait for up to 'wait' centiseconds for
** keyboard input. It returns non-false if there is a character available
**
*/
static boolean waitkey(int wait) {
#ifdef USE_SDL
  int64 timerstart;
  int mx, my;
#endif
#if (defined(USE_SDL) && !defined(TARGET_MINGW)) || !defined(USE_SDL)
  fd_set keyset;
  struct timeval waitime;
#endif
#ifdef BODGEMGW
  int tmp;
#endif

#ifdef USE_SDL
  timerstart = basicvars.centiseconds;
  SDL_Event ev;
  while ( 1 ) {
/*
 * First check for SDL events
*/
    while (SDL_PeepEvents(&ev, 1, SDL_GETEVENT, SDL_ALLEVENTS) > 0) {
      SDL_GetMouseState(&mx, &my);
      switch(ev.type) {
        case SDL_KEYUP:
          break;
        case SDL_KEYDOWN:
          switch(ev.key.keysym.sym)
          {
            case SDLK_RSHIFT:   /* ignore non-character keys */
            case SDLK_LSHIFT:
            case SDLK_RCTRL:
            case SDLK_LCTRL:
            case SDLK_RALT:
            case SDLK_LALT:
              break;
            default:
              SDL_PushEvent(&ev);  /* we got a char - push the event back and say we found one */
              return 1;
              break;
          }
          break;
        case SDL_MOUSEBUTTONDOWN:
          if (ev.button.button == SDL_BUTTON_LEFT) mousebuttonstate |= 4;
          if (ev.button.button == SDL_BUTTON_MIDDLE) mousebuttonstate |= 2;
          if (ev.button.button == SDL_BUTTON_RIGHT) mousebuttonstate |= 1;
          add_mouseitem(mx, my, mousebuttonstate, basicvars.centiseconds);
          break;
        case SDL_MOUSEBUTTONUP:
          if (ev.button.button == SDL_BUTTON_LEFT) mousebuttonstate &= 3;
          if (ev.button.button == SDL_BUTTON_MIDDLE) mousebuttonstate &= 5;
          if (ev.button.button == SDL_BUTTON_RIGHT) mousebuttonstate &= 6;
          add_mouseitem(mx, my, mousebuttonstate, basicvars.centiseconds);
          break;
        case SDL_QUIT:
          exit_interpreter(EXIT_SUCCESS);
          break;
      }
    }
    if (basicvars.centiseconds - timerstart >= wait) return 0;

#ifndef TARGET_MINGW
/*
 * Then for stdin keypresses
*/
#ifndef BODGEMGW
    FD_ZERO(&keyset);
    FD_SET(keyboard, &keyset);
#endif
    waitime.tv_sec = waitime.tv_usec = 0;
    if (!nokeyboard && select(1, &keyset, NIL, NIL, &waitime) > 0 ) return 1;
#endif /* !TARGET_MINGW */
    if (basicvars.centiseconds - timerstart >= wait) return 0; /* return after one check if wait time = 0, or after timeout. */
    usleep(1000);
  }
#else /* !USE_SDL */
#ifdef BODGEMGW
  tmp=clock()+wait*(CLOCKS_PER_SEC/100);
  for(;;) { if(kbhit() || (clock()>tmp)) break; }
  return kbhit();
#else
  FD_ZERO(&keyset);
  FD_SET(keyboard, &keyset);
  waitime.tv_sec = wait/100;    /* Convert wait time to seconds and microseconds */
  waitime.tv_usec = wait%100*10000;
  return select(1, &keyset, NIL, NIL, &waitime) > 0;
#endif
#endif
}


/*
** 'read_key' reads the next character from the keyboard
** or gets the next keypress from the SDL event queue
*/
int32 read_key(void) {
#if (defined(USE_SDL) && !defined(TARGET_MINGW)) || !defined(USE_SDL)
  int errcode;
#endif
  uint32 ch = 0;

#if defined(TARGET_BEOS) || defined(TARGET_AMIGA)
  return getchar();				/* holding code */
#endif

#ifdef USE_SDL
#ifndef TARGET_MINGW
  fd_set keyset;
  struct timeval waitime;
#endif
  SDL_Event ev;
  int mx, my;

#ifndef USE_SDL // but this is within USE_SDL
  if ((read(keyboard, &ch, 1)) < 0) {		/* Read from keyboard stream		*/
//    if(basicvars.escape_enabled && (errno == EINTR)) error(ERR_ESCAPE);       /* Assume CTRL-C has been pressed */
//    error(ERR_BROKEN, __LINE__, "keyboard");
  }
  return ch;
#endif

  if (holdcount > 0) return pop_key();	// moved to here

  while (ch == 0) {
/*
** First check the SDL event Queue
*/

    while (matrixflags.videothreadbusy) usleep(1000);
    matrixflags.noupdate = 1;
    if (SDL_PeepEvents(&ev, 1, SDL_GETEVENT, SDL_ALLEVENTS)) {
      SDL_GetMouseState(&mx, &my);
      switch(ev.type) {
        case SDL_QUIT:
          exit_interpreter(EXIT_SUCCESS);
          break;
        case SDL_MOUSEBUTTONDOWN:
          if (ev.button.button == SDL_BUTTON_LEFT) mousebuttonstate |= 4;
          if (ev.button.button == SDL_BUTTON_MIDDLE) mousebuttonstate |= 2;
          if (ev.button.button == SDL_BUTTON_RIGHT) mousebuttonstate |= 1;
          add_mouseitem(mx, my, mousebuttonstate, basicvars.centiseconds);
          break;
        case SDL_MOUSEBUTTONUP:
          if (ev.button.button == SDL_BUTTON_LEFT) mousebuttonstate &= 3;
          if (ev.button.button == SDL_BUTTON_MIDDLE) mousebuttonstate &= 5;
          if (ev.button.button == SDL_BUTTON_RIGHT) mousebuttonstate &= 6;
          add_mouseitem(mx, my, mousebuttonstate, basicvars.centiseconds);
          break;
        case SDL_KEYUP:
          break;
        case SDL_KEYDOWN:
// emulate_printf("$%0X",ev.key.keysym.sym);
          switch(ev.key.keysym.sym) {
            case SDLK_RSHIFT:   /* ignored keys */
            case SDLK_LSHIFT:
            case SDLK_RCTRL:
            case SDLK_LCTRL:
            case SDLK_RALT:
            case SDLK_LALT:
              break;
#ifdef TARGET_DOSWIN
//            case 0:
// This seems to be the way, fill in the rest later.
// NB, use low-level codes, translate to RISC OS codes higher up.
// GetKeyState() gets state for this keypress
// GetAsyncKeyState() gets state right now this instant
//               if (GetAsyncKeyState(0x1D)<0) {	/* NoConvert	*/
//                 ch=0xC5;
//                 break;
//               }
//               if (GetAsyncKeyState(0x1C)<0) {	 /* Convert	*/
//                 ch=0xC6;
//                 break;
//               }
//               break;
#endif
            case SDLK_F1: case SDLK_F2: case SDLK_F3: case SDLK_F4: case SDLK_F5:
            case SDLK_F6: case SDLK_F7: case SDLK_F8: case SDLK_F9: case SDLK_F10:
            case SDLK_F11: case SDLK_F12:
              ch = 0x81 + ev.key.keysym.sym - SDLK_F1;
              break;
            case SDLK_PRINT:    ch=0x80; break;
            case SDLK_PAUSE:    ch=0xC4; break;
            case SDLK_INSERT:   ch=0xC6; break;
            case SDLK_DELETE:   ch=0xC7; break;
            case SDLK_HOME:     ch=0xC8; break;
            case SDLK_END:      ch=0xC9; break;
            case SDLK_PAGEDOWN: ch=0xCA; break;
            case SDLK_PAGEUP:   ch=0xCB; break;
            case SDLK_LEFT:     ch=0xCC; break;
            case SDLK_RIGHT:    ch=0xCD; break;
            case SDLK_DOWN:     ch=0xCE; break;
            case SDLK_UP:       ch=0xCF; break;
//          case SDLK_ESCAPE:
//            if (basicvars.escape_enabled) error(ERR_ESCAPE); // Should set flag for foreground to check
//            return ESCAPE;
            default:
              ch = ev.key.keysym.unicode;
              if (ch < 0x100) {
                matrixflags.noupdate = 0;
                return ch;
              } else {
                ch=0;
              }
          }
          if (ch) {
            if (ev.key.keysym.mod & KMOD_ALT)         ch ^= 0x30;
              else if (ev.key.keysym.mod & KMOD_CTRL) ch ^= 0x20;
            if (ev.key.keysym.mod & KMOD_SHIFT)       ch ^= 0x10;
            push_key(ch);
            matrixflags.noupdate = 0;
            return asc_NUL;
          }
        }
      }
      matrixflags.noupdate = 0;

/*
** Then check stdin
*/
#ifndef TARGET_MINGW
    FD_ZERO(&keyset);
    FD_SET(keyboard, &keyset);
    waitime.tv_sec = waitime.tv_usec = 0; /* Zero wait time */
    if ( !nokeyboard && ( select(1, &keyset, NIL, NIL, &waitime) > 0 )) {
#ifndef BODGEMGW
      errcode = read(keyboard, &ch, 1);
#endif
      if (errcode < 0) {                /* read() returned an error */
        if ((errno == EINTR) && basicvars.escape_enabled) error(ERR_ESCAPE);  /* Assume Ctrl-C was pressed */
        error(ERR_BROKEN, __LINE__, "keyboard");        /* Otherwise roll over and die */
      }
      else return ch;
    }
#endif /* TARGET_MINGW */
/*  If we reach here then nothing happened and so we should sleep */
    usleep(10000);
  }
#else /* ! USE_SDL */

#ifndef BODGEMGW
// does anything get here?
  errcode = read(keyboard, &ch, 1);
#endif
// does anything get here?
  if (errcode < 0) {            /* read() returned an error */
#ifndef BODGEMGW
// does anything get here?
    if (basicvars.escape_enabled && (errno == EINTR)) error(ERR_ESCAPE);      /* Assume Ctrl-C was pressed */
#endif
// does anything get here?
    error(ERR_BROKEN, __LINE__, "keyboard");    /* Otherwise roll over and die */
  }
#endif
  return ch;
//  ch=decode_sequence();		/* Temp'y, stop compiler complaining */
}

#endif


/*
** 'display' outputs the given character 'count' times
** special case for VDU_CURBACK because it doesn't
** work if echo is off
*/
static void display(int32 what, int32 count) {
  if ((what != VDU_CURBACK) && (what != DEL)) echo_off();
  while (count > 0) {
    emulate_vdu(what);
    count--;
  }
  if ((what != VDU_CURBACK) && (what != DEL)) echo_on();
}

/*
** 'remove_history' removes entries from the command history buffer.
** The first 'count' entries are deleted. The function also takes
** care of updating 'highbuffer' and 'histindex'
*/
static void remove_history(int count) {
  int n, freed;
  freed = 0;
  for (n = 0; n < count; n++) freed+=histlength[n];
  if (count < histindex) {      /* Not deleting everything - Move entries down */
    memmove(histbuffer, &histbuffer[freed], highbuffer-freed);
    for (n = count; n < histindex; n++) histlength[n-count] = histlength[n];
  }
  highbuffer-=freed;
  histindex-=count;
}

/*
** 'add_history' adds an entry to the command history buffer.
** The new command is added to the end of the buffer. If there is not
** enough room for it, one or more commands are removed from the front
** of the buffer to make space for it. Also, if the maximum number of
** entries has been reached, an entry is dropped off the front of the
** buffer.
** 'cmdlen' is the length of the command. It does not include the NULL
** at the end
*/
static void add_history(char command[], int32 cmdlen) {
  int32 wanted, n, freed;
  if (highbuffer+cmdlen >= HISTSIZE) {  /* There is not enough room at the end of the buffer */
    wanted = highbuffer+cmdlen-HISTSIZE+1;      /* +1 for the NULL at the end of the command */
/*
** Figure out how many commands have to be removed from the buffer to make
** room for the new one. Scan from the start of the history list adding up
** the lengths of the commands in the history buffer until the total equals
** or exceeds the number of characters required. Entries from 0 to n-1 have
** to be deleted.
*/
    freed = 0;
    n = 0;
    do {
      freed += histlength[n];
      n++;
    } while (n < histindex && freed < wanted);
    remove_history(n);
  }
  else if (histindex == MAXHIST) {      /* History list is full */
    remove_history(1);  /* Delete the first entry */
  }
  memmove(&histbuffer[highbuffer], command, cmdlen+1);
  histlength[histindex] = cmdlen+1;
  highbuffer += cmdlen+1;
  histindex += 1;
}

static void init_recall(void) {
  recalline = histindex;
}

static void recall_histline(char buffer[], int updown) {
  int n, start, count;
  if (updown < 0) {     /* Move backwards in history list */
    if (recalline == 0) return; /* Already at start of list */
    recalline -= 1;
  }
  else {        /* Move forwards in history list */
    if (recalline == histindex) return; /* Already at end of list */
    recalline += 1;
  }
  if (recalline == histindex)   /* Moved to last line */
    buffer[0] = asc_NUL;
  else {
    start = 0;
    for (n = 0; n < recalline; n++) start += histlength[n];
    strcpy(buffer, &histbuffer[start]);
  }
  display(VDU_CURBACK, place);          /* Move cursor to start of old line */
  place = strlen(buffer);
  if (place > 0) emulate_vdustr(buffer, place);
  count = highplace - place;    /* Find difference in old and new line lengths */
  if (count > 0) {      /* Some of the old line is still visible */
    display(' ', count);
    display(VDU_CURBACK, count);
  }
  highplace = place;
}

/*
** 'shift_down' moves all the characters in the buffer down by one
** overwriting the character at 'offset'. It also redraws the line on
** the screen, leaving the cursor at the postion of 'offset'
*/
static void shift_down(char buffer[], int32 offset) {
  int32 count;
  count = highplace-offset;     /* Number of characters by which to move cursor */
  highplace--;
  echo_off();
  while (offset < highplace) {
    buffer[offset] = buffer[offset+1];
    emulate_vdu(buffer[offset]);
    offset++;
  }
  emulate_vdu(32);
  echo_on();
  display(VDU_CURBACK, count);  /* Move cursor back to correct position */
}

/*
** 'shift_up' moves the text from 'offset' along by one character to
** make room for a new character, rewriting the line on the screen as
** well. It leaves the cursor at the screen position for the new
** character. The calling routine has to check that there is room
** for another character
*/
static void shift_up(char buffer[], int32 offset) {
  int32 n;
  if (offset == highplace) return;      /* Appending char to end of line */
  n = highplace;
  while (n >= offset+1) {
    buffer[n] = buffer[n-1];
    n--;
  }
  echo_off();
  emulate_vdu(VDU_CURFORWARD);
  emulate_vdu(DEL);     /* Where new character goes on screen */
  emulate_vdu(VDU_CURFORWARD);
  n = offset+1;
  while (n <= highplace) {
    emulate_vdu(buffer[n]);
    n++;
  }
  echo_on();
  while (n > offset) {  /* Put cursor back where it should be */
    emulate_vdu(VDU_CURBACK);
    n--;
  }
  highplace++;  /* Bump up 'last character' index */
}


/*
** 'emulate_readline' reads a line from the keyboard. It returns 'true'
** if the call worked successfully or 'false' if 'escape' was pressed.
** The data input is stored at 'buffer'. Up to 'length'-1 characters
** can be read. A 'null' is added after the last character. 'buffer'
** can be prefilled with characters if required to allow existing text
** to be edited.
**
** This function uses only the most basic facilities of the underlying
** OS to carry out its task (which counts as everything under DOS) so
** much of this code is ugly.
**
** The function provides both DOS and Unix style line editing facilities,
** for example, both 'HOME' and control 'A' move the cursor to the start
** of the line. It is also possible to recall the last line entered.
**
** There is a problem with LCC-WIN32 running under Windows 98 in that the
** extended key codes for the cursor movement keys (insert, left arrow
** and so on) are not returned correctly by 'getch()'. In theory they
** should appear as a two byte sequence of 0xE0 followed by the key code.
** Only the 0xE0 is returned. This appears to be a bug in the C runtime
** library.
*/
readstate emulate_readline(char buffer[], int32 length, int32 echochar) {
  int32 ch, lastplace;
  int32 pendch=0;
  byte oldopt;

  if (basicvars.runflags.inredir) {     /* There is no keyboard to read - Read from file stdin */
    char *p;
    p = fgets(buffer, length, stdin);	/* Get all in one go */
    if (p == NIL) {     		/* Call failed */
      if (ferror(stdin)) error(ERR_READFAIL);   /* I/O error occured on stdin */
      buffer[0] = asc_NUL;          	/* End of file */
      return READ_EOF;
    }
    return READ_OK;
  }

#ifdef USE_SDL
  matrixflags.vdu14lines=0;
#endif
  oldopt=sysvar[sv_KeyOptions];
  sysvar[sv_KeyOptions]=oldopt | 192;
  highplace = strlen(buffer);
  if (highplace > 0) emulate_vdustr(buffer, highplace);
  place = highplace;
  lastplace = length-2;         /* Index of last position that can be used in buffer	*/
  init_recall();
  do {
//basicvars.escape=FALSE;
//printf("$%02X",basicvars.escape);
    ch = kbd_get();		/* Get 9-bit keypress or expanded function key		*/
//fprintf(stderr, "&%02X &%03X\n",basicvars.escape,ch);
    if ((ch & 0x100) || ((ch == DEL) & !matrixflags.delcandelete)) {
      pendch=ch & 0xFF;		/* temp */
      ch = asc_NUL;
    }

    watch_signals();           /* Let asynchronous signals catch up */

//    if (basicvars.escape) return READ_ESC;
//	/* Check if the escape key has been pressed and bail out if it has */

//  if (((ch == ESCAPE) && basicvars.escape_enabled) || basicvars.escape) {
//    basicvars.escape=TRUE; // bodge
    if (basicvars.escape) {	/* Will have been set within kbd_get()	*/
      sysvar[sv_KeyOptions]=oldopt;
      return READ_ESC;
    }
    switch (ch) {       	/* Normal keys */
    case asc_CR: case asc_LF:   /* End of line */
      emulate_vdu('\r');
      emulate_vdu('\n');
      buffer[highplace] = asc_NUL;
      if (highplace > 0) add_history(buffer, highplace);
      break;
//  case CTRL_H: case DEL:      /* Delete character to left of cursor */
    case CTRL_H:                /* Delete character to left of cursor */
      if (place > 0) {
        emulate_vdu(DEL);
        place--;
        shift_down(buffer, place);
      }
      break;
    case DEL:			/* Can be optimised */
      if (matrixflags.delcandelete) {
        if (place > 0) {
          emulate_vdu(DEL);
          place--;
          shift_down(buffer, place);
        }
      }
      break;
    case CTRL_D:        /* Delete character under the cursor */
      if (place < highplace) shift_down(buffer, place);
      break;
    case CTRL_K:        /* Delete from cursor position to end of line */
      display(DEL, highplace-place);            /* Clears characters after cursor */
      display(VDU_CURBACK, highplace-place);            /* Now move cursor back */
      highplace = place;
      break;
    case CTRL_U:        /* Delete whole input line */
      while (place < highplace) {
        emulate_vdu(buffer[place]);     /* It does the job */
        place++;
      }
      display(DEL, place);  /* Overwrite text with blanks and backspace */
      highplace = place = 0;
      break;
    case CTRL_B:        /* Move cursor left */
      if (place > 0) {
        emulate_vdu(VDU_CURBACK);
        place--;
      }
      break;
    case CTRL_F:        /* Move cursor right */
      if (place < highplace) {
        emulate_vdu(buffer[place]);     /* It does the job */
        place++;
      }
      break;
    case CTRL_P:        /* Move backwards one entry in the history list */
      if (fx44x)
	recall_histline(buffer, -1);
#ifdef USE_SDL
      else
	emulate_vdu(16);
#endif
      break;
    case CTRL_N:        /* Move forwards one entry in the history list */
      if (fx44x)
	recall_histline(buffer, 1);
#ifdef USE_SDL
      else
	emulate_vdu(14);
#endif
      break;
    case CTRL_O:
    case CTRL_L:
#ifdef USE_SDL
      emulate_vdu(ch);
#endif
      break;
    case CTRL_A:        /* Move cursor to start of line */
      display(VDU_CURBACK, place);
      place = 0;
      break;
    case CTRL_E:                /* Move cursor to end of line */
      echo_off();
      while (place < highplace) {
        emulate_vdu(buffer[place]);     /* It does the job */
        place++;
      }
      echo_on();
      break;
    case HOME:  /* Move cursor to start of line */
      display(VDU_CURBACK, place);
      place = 0;
      break;
    case asc_NUL:                   /* Function or special key follows */

      ch = pendch; /* temp */

      switch (ch) {
      case END:                 /* Move cursor to end of line */
        echo_off();
        while (place < highplace) {
          emulate_vdu(buffer[place]);   /* It does the job */
          place++;
        }
        echo_on();
        break;
      case UP:          /* Move backwards one entry in the history list */
        recall_histline(buffer, -1);
        break;
      case DOWN:        /* Move forwards one entry in the history list */
        recall_histline(buffer, 1);
        break;
      case LEFT:        /* Move cursor left */
        if (place > 0) {
          emulate_vdu(VDU_CURBACK);
          place--;
        }
        break;
      case RIGHT:       /* Move cursor right */
        if (place < highplace) {
          emulate_vdu(buffer[place]);   /* It does the job */
          place++;
        }
        break;
      case KEY_DELETE:      /* Delete character at the cursor */
        if (!matrixflags.delcandelete) {
          if (place < highplace) shift_down(buffer, place);
        }
        break;
      case INSERT:      /* Toggle between 'insert' and 'overwrite' mode */
        enable_insert = !enable_insert;
        set_cursor(enable_insert);      /* Change cursor if in graphics mode */
        break;
      default:
        emulate_vdu(VDU_BEEP);  /* Bad character - Ring the bell */
      }
      break;
    default:
      if (ch < ' ' && ch != asc_TAB)        /* Reject any other control character except tab */
        emulate_vdu(VDU_BEEP);
      else if (highplace == lastplace)  /* No room left in buffer */
        emulate_vdu(VDU_BEEP);
      else {    /* Add character to buffer */
        if (enable_insert) shift_up(buffer, place);
        buffer[place] = ch;
        emulate_vdu(ch);
        place++;
        if (place > highplace) highplace = place;
      }
    }
  } while (ch != asc_CR && ch != asc_LF);
  sysvar[sv_KeyOptions]=oldopt;
  return READ_OK;
}

#endif // NEWKBD

#endif // TARGET_RISCOS
