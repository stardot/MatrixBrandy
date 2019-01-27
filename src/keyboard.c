/*
** This file is part of the Brandy Basic V Interpreter.
** Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005 David Daniels
** and Copyright (C) 2006, 2007 Colin Tuckley
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
** Requires: updated function.c
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
**                  Tested on: CentOS, DJGPP, MinGW, WinSDL, RISCOS.
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
#include "keyboard.h"
#include "screen.h"
#include "mos.h"
#include "inkey.h"
#ifdef TARGET_DJGPP
#include <pc.h>
#include <keys.h>
#include <bios.h>
#include <errno.h>
#include <termios.h>
#endif
#ifdef TARGET_UNIX

//#include <sys/time.h>
//#include <sys/types.h>
//#include <errno.h>
//#include <unistd.h>
#include <termios.h>

// Move these later
static struct termios origtty;  /* Copy of original keyboard parameters */
static int32 keyboard;          /* File descriptor for keyboard */

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
#define DELETE          0x7F
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


#ifndef TARGET_RISCOS
/* holdcount and holdstack are used when decoding ANSI key sequences. If a
** sequence is read that does not correspond to an ANSI sequence the
** characters are stored here so that they can be returned by future calls
** to 'kbd_get()'. Note that this is a *stack* not a queue.
*/
static int32 holdcount;		/* Number of characters held on stack			*/
static int32 holdstack[8];	/* Hold stack - Characters waiting to be passed back via 'get' */

#define INKEYMAX 0x7FFF		/* Maximum wait time for INKEY				*/

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

static boolean enable_insert;     /* TRUE if keyboard input code is in insert mode	*/
static char histbuffer[HISTSIZE]; /* Command history buffer				*/
static int32 histlength[MAXHIST]; /* Table of sizes of entries in history buffer	*/

static int nokeyboard=0;
static int escint=128;
static int escmul=1;
static int fx44x=1;
#endif


#ifdef USE_SDL
#include "SDL.h"
#include "SDL_events.h"
extern void mode7flipbank();
extern void reset_vdu14lines();
Uint8 mousestate, *keystate=NULL;
#endif


#ifdef NEWKBD
/* New keyboard routines */
/* --------------------- */

#ifdef TARGET_RISCOS
 #include "kernel.h"
 #include "swis.h"
#else
 #include <stdlib.h>
 #if defined(TARGET_MINGW) || defined(TARGET_WIN32) || defined(TARGET_BCC32)
  #include <windows.h>
  #include <keysym.h>
 #endif
#endif

static boolean waitkey(int wait);		/* To prevent a forward reference	*/
static int32 pop_key(void);			/* To prevent a forward reference	*/
static int32 read_fn_string(void);		/* To prevent a forward reference	*/
//static int32 is_fn_key(int32 key);		/* To prevent a forward reference	*/
static int32 switch_fn_string(int32 key);	/* To prevent a forward reference	*/


/* kbd_init called to initialise the keyboard code
** --------------------------------------------------------------
** Clears the function key strings, checks if stdin is connected
** to the keyboard or not. If it is then the keyboard functions
** are used to read keypresses. If not, then standard C functions
** are used instead, the assumption being that stdin is taking
** input from a file, similar to *EXEC.
** -------------------------------------------------------------- */
boolean kbd_init() {
#ifdef TARGET_RISCOS
  // RISC OS, nothing to do
  // ----------------------
  return TRUE;
#else /* !RISCOS */

  // Non-RISC OS, perform the action manually
  // ----------------------------------------
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

  if (tcgetattr(fileno(stdin), &tty) == 0) return TRUE;		/* Keyboard being used */
/* tcgetattr() returned an error. If the error is ENOTTY then stdin does not point at
** a keyboard and so the program does simple reads from stdin rather than use the custom
** keyboard code. If the error is not ENOTTY then something has gone wrong so we abort
** the program
*/
  if (errno != ENOTTY) return FALSE;    /* tcgetattr() returned an error we cannot handle */
  basicvars.runflags.inredir = TRUE;    /* tcgetattr() returned ENOTTY - use C functions for input */
  return TRUE;
#else /* !DOS */

  // Windows target, nothing to do
  // -----------------------------
  return TRUE;

#endif
#endif /* DOSWIN */

#ifdef TARGET_UNIX
  // Unix target
  // -----------
  struct termios tty;

/* Set up keyboard for unbuffered I/O */
  if (tcgetattr(fileno(stdin), &tty) < 0) {	/* Could not obtain keyboard parameters	*/
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
#endif
}


/* kbd_quit called to terminate keyboard control on termination */
/* ------------------------------------------------------------ */
void kbd_quit() {
#ifdef TARGET_RISCOS
  // RISC OS, nothing to do
  // ----------------------
#else /* !RISCOS */

#ifdef TARGET_DOSWIN
  // DOS/Windows target, nothing to do
  // ---------------------------------
#endif /* DOSWIN */

#ifdef TARGET_UNIX
  // Unix target
  // -----------
  (void) tcsetattr(keyboard, TCSADRAIN, &origtty);
#endif /* UNIX */

#ifdef TARGET_AMIGA
  // AMIGA target
  // ------------
  rawcon(0);
#endif /* AMIGA */
#endif
}


#ifndef TARGET_RISCOS
/* kbd_fnkeyset - define a function key string */
/* ------------------------------------------- */
/* Definition is defined by length, so can contain NULs
 * Returns:   0 if ok
 *          <>0 if can't set because key is in use
 */
int kbd_fnkeyset(int key, char *string, int length) {
  if (fn_string_count) return fn_string_count;		/* Key in use			*/
  if (fn_key[key].text != NIL) free(fn_key[key].text);	/* Remove existing definition	*/
  fn_key[key].length = length;
  fn_key[key].text = malloc(length);			/* Get space for new definition	*/
  if (fn_key[key].text != NIL) memcpy(fn_key[key].text, string, length);
  return 0;						/* Ok				*/
}

/* kbd_fnkeyget - get a function key string for *SHOW */
/* -------------------------------------------------- */
/* Returns: string which can include NULs
 *          len updated with length of string
 */
char *kbd_fnkeyget(int key, int *len) {
  *len=fn_key[key].length;
  return fn_key[key].text;
}

/* kbd_isfnkey - check if keycode is a function key */
/* ------------------------------------------------ */
// NB: INSERT is actually function key 13.
int32 kbd_isfnkey(int32 key) {
  if (key & 0x100) {
    key = key & 0xFF;
    if (key >= KEY_F0 && key <= KEY_F9) return key - KEY_F0;
    if (key >= KEY_F10 && key <= KEY_F12) return key - KEY_F10 + 10;
  }
  return -1;						/* Not a function key		*/
}

// To do: move remaining function key code here
#endif /* RISCOS */


/* kbd_inkey called to implement Basic INKEY and INKEY$ functions */
/* -------------------------------------------------------------- */
int32 kbd_inkey(int32 arg) {

#ifdef TARGET_RISCOS
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

#else /* !RISCOS */
  // Non-RISC OS, perform the action manually
  // ----------------------------------------
  arg = arg & 0xFFFF;				/* Argument is a 16-bit number		*/

  // Return host operating system
  // ----------------------------
  if (arg == 0xFF00) return OSVERSION;

  // Timed wait for keypress
  // -----------------------
  if (arg < 0x8000) {
#ifdef USE_SDL
    mode7flipbank();
#endif
    if (basicvars.runflags.inredir		/* Input redirected (equiv. of *EXEC)	*/
      || fn_string_count) return kbd_get();	/* Function key active			*/
    if (holdcount > 0)	return pop_key();	/* Character waiting so return it	*/
    if (waitkey(arg))	return kbd_get();	/* Wait for keypress and return it	*/
    else		return -1;		/* Otherwise return -1 for nothing	*/

  // Negative INKEY - scan for keypress
  // ----------------------------------
  } else {
    arg = arg ^ 0xFFFF;				/* Convert to keyscan number		*/

#ifdef USE_SDL
    SDL_Event ev;
    SDL_PumpEvents();
    keystate = SDL_GetKeyState(NULL);
    mousestate = SDL_GetMouseState(NULL, NULL);
    while(SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) exit_interpreter(EXIT_SUCCESS);
    }

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
    }

    if (arg < 128) {				/* Test for single keypress		*/
      switch (arg) {				/* Not visible from SDL keyscan		*/
#ifdef TARGET_DOSWIN
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
			  GetAsyncKeyState(0xDC) == 0 ? -1 : 0);	/* K#  #~	*/
#endif
						/* Translate SDL keyscan		*/
        default:  return ((keystate[inkeylookup[arg]] != 0) ? -1 : 0);
      }
    }

    if (arg < 256) return -1;			/* Scan range - unimplemented		*/

    if ((arg & 0xFE00) == 0x0200) {		/* Direct API keyscan, INKEY(&FC00+nn)	*/
      return ((keystate[arg ^ 0x3FF] != 0) ? -1 : 0);
    }

    return 0;					/* Anything else, return FALSE		*/

#else /* !USE_SDL */

#ifdef VK_SHIFT
    /* adapted from con_keyscan() from JGH 'console' library */
    if (arg <0x080) {				/* Test for single keypress		*/
#ifndef TARGET_DJGPP				/* DOS doesn't support GetKbdLayout	*/
      if (((int)(GetKeyboardLayout(0)) & 0xFFFF)==0x0411) {	/* BBC layout keyboard	*/
	/* Note: as a console app, this is the ID from when the program started		*/
        switch (arg) {
          case 24: return (GetAsyncKeyState(0xDE)<0 ? -1 : 0);	/* ^~        */
          case 46: return (GetAsyncKeyState(0xDC)<0 ? -1 : 0);	/* UKP/Y/etc */
          case 72: return (GetAsyncKeyState(0xBA)<0 ? -1 : 0);	/* :         */
          case 87: return (GetAsyncKeyState(0xBB)<0 ? -1 : 0);	/* ;         */
          case 90:						/* #~  K#    */
          case 93:						/* =+        */
          case 94:						/* Left \|   */
          case 120:						/* \|        */
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

    if (arg < 0x200) {				/* Direct API keyscan, INKEY(&FE00+nn)	*/
      if (GetAsyncKeyState(arg ^ 0x1ff)<0) return -1;	/* Key pressed			*/
      else				   return 0;	/* Key not pressed		*/
    }

    return 0;					/* Anything else, return FALSE		*/
#endif /* VK_SHIFT */

#endif /* !USE_SDL */

/* AMIGA, BEOS, non-SDL UNIX remaining */
#ifdef TARGET_AMIGA
/* A KeyIO call with command KBD_READMATRIX reads the keyboard matrix to a 16-byte buffer,
 * one bit per keystate, similar to the SDL GetKeyState call.
 */
    return 0;					/* For now, return NOT PRESSED		*/
#endif
#ifdef TARGET_BEOS
    return 0;					/* For now, return NOT PRESSED		*/
#endif
    return 0;					/* Everything else, return NOT PRESSED	*/
  }

#endif /* !RISCOS */
}


/* kbd_modkeys - do a fast read of state of modifier keys */
/* ------------------------------------------------------ */
/* Equivalent to the BBC MOS OSBYTE 118/KEYV call
 * arg is a bitmap of modifier keys to test for
 *  bit<n> tests key<n>, ie b0=Shift, b1=Ctrl, b2=Alt, etc.
 * Currently, only SHIFT tested by SDL for VDU paged scrolling.
 * To do: need S/C/A for modifying special keypresses.
 */
int32 kbd_modkeys(int32 arg) {
#ifdef USE_SDL
  if (keystate==NULL) return 0;				/* Not yet been initialised	*/
  if (keystate[SDLK_LSHIFT] || keystate[SDLK_RSHIFT])	/* Either SHIFT key		*/
    return -1;
#endif
  return kbd_inkey(-1);					/* Just test SHIFT for now	*/
}


#ifdef TARGET_DJGPP
/* GetAsyncKeyState is a Windows API call, DOS only has API call to read Shift/Ctrl/Alt.
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


/* kbd_get called to implement Basic GET and GET$ functions */
/* -------------------------------------------------------- */
int32 kbd_get(void) {

#ifdef TARGET_RISCOS
  // RISC OS, pass directly to MOS
  // -----------------------------
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  oserror = _kernel_swi(OS_ReadC, &regs, &regs);
  if (oserror != NIL) error(ERR_CMDFAIL, oserror->errmess);
  return regs.r[0];

#else /* !RISCOS */

  int ch, fnkey;

  if (basicvars.runflags.inredir) {		/* Input redirected (equiv. of *EXEC)	*/
#ifdef TARGET_UNIX
    if ((ch=getchar()) != EOF) return ch;
#else
    if ((ch=getch()) != EOF) return ch;
#endif
    else error(ERR_READFAIL);			/* I/O error occured on STDIN		*/
  }
  if (fn_string != NIL) return read_fn_string(); /* Function key active			*/
  if (holdcount > 0) return pop_key();		/* Return held character if one is held	*/

// To do, allow *FX221-8 to specify special keypress expansion and *FX4 cursor key control
// For the moment, &18n<A and &1Cn>9 are function keys, &18n>9 are cursor keys
  if ((ch=kbd_get0()) & 0x100) {	  	/* Get a keypress from 'keyboard buffer'*/
#if defined(TARGET_DOSWIN) && !defined(USE_SDL)
    if ((ch & 0x00F) >= 10) ch=ch ^ 0x40;	/* Swap to RISC OS ordering		*/
    if ((ch & 0x0CF) == 0xc6) ch=ch + 7;	/* INSERT */
    if (ch == 0x1C7) ch=127;			/* DELETE */
    if (ch == 0x1c8) ch=30;			/* HOME   */
#endif
  }
  if ((fnkey = kbd_isfnkey(ch)) < 0) return ch;	/* Not a function key			*/
  if (fn_key[fnkey].length == 0)     return ch;	/* Function key undefined		*/
  return switch_fn_string(fnkey);		/* Switch to fnkey and return first char*/

#endif /* !RISCOS */
}


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
0xc4,0xc1,0xc5,0x0b,0x0c,0x0d,0x0e,0x0f, /* sBS,sTAB,sRET,0C-0F		*/
0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17, /* 10-17			*/
0x18,0x19,0x1a,0xc3,0x1c,0x1d,0x1e,0x1f, /* 18-1A,sESC,1C-1F		*/
0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27, /* 20-27			*/
0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f, /* 28-2F			*/
0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37, /* 30-37			*/
0x38,0x39,0x3a,0x81,0x82,0x83,0x84,0x85, /* 38-3A,F1-F5			*/
0x86,0x87,0x88,0x89,0x8a,0x45,0x46,0xc8, /* F6-F10,45,46,Home		*/
0xcf,0xdf,0x4a,0xcc,0x4c,0xcd,0x4e,0xcb, /* Up,PgUp,4A,<-,4C,->,4E,End	*/
0xce,0xde,0xc6,0xc7,0x91,0x92,0x93,0x94, /* Down,PgDn,Ins,Del,sF1-sF4	*/
0x95,0x96,0x97,0x98,0x99,0x9a,0xa1,0xa2, /* sF5-sF10,cF1,cF2		*/
0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa, /* cF3-cF10			*/
0xb1,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8, /* aF1-aF8			*/
0xb9,0xba,0xa0,0xcc,0xcd,0xcb,0xde,0xc8, /* aF9-aF10,cPrint,c<-,c->,cEnd,cPgDn,cHome */
0xdf,0x79,0x7a,0x7b,0x7c,0x7d,0x7e,0x7f, /* cPgUp,79-7F				*/
0x80,0x81,0x82,0x83,0x84,0x8b,0x8c,0x9b, /* 80-84,F11-12,sF11			*/
0x9c,0xab,0xac,0xbb,0xbc,0xcf,0x8e,0x8f, /* sF12,cF11-12,aF11-12,cUp,8E-8F	*/
0x90,0xce,0xc6,0xc7,0xc1,0x95,0x96,0xc8, /* 90,cDown,cIns,cDel,cTab,95,96,aHome	*/
0xcf,0xdf,0x9a,0xcc,0x9c,0xcd,0x9e,0xcb, /* aUp,aPgUp,9A,a<-,9C,a->,9E,aEnd	*/
0xce,0xde,0xc6,0xc7,0xa4,0xc1,0xa6,0xa7, /* aDn,aPgDn,aIns,aDel,A4,aTab,A6-A7	*/
0xa8,0xa9,0xaa,0xab,0xac,0xad,0xae,0xaf, /* A8-AF				*/
0xb0,0xb1,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,
0xb8,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,0xbf,
0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,
0xc8,0xc9,0xca,0xcb,0xcc,0xcd,0xce,0xcf,
0xd0,0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,
0xd8,0xd9,0xda,0xdb,0xdc,0xdd,0xde,0xdf,
0xe0,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,
0xe8,0xe9,0xea,0xeb,0xec,0xed,0xee,0xef,
0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,
0xf8,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff };


int32 kbd_get0(void) {
  int ch,s,c,a;

// Keyboard buffer NULL prefixing is the opposite to RISC OS.
// So, swap the prefixing around to convert to RISC OS mapping.
// Extracting more guts from JGH Console library.
// Tweek a bit to regularise low-level keycodes before being converted to RISC OS codes.
#if defined(TARGET_DOSWIN) && !defined(USE_SDL)
  ch=getch();
  if (ch == NUL || ch == 0xE0) {		/* DOS escaped characters		*/
    // When kbd_modkeys() returns all keys, change this to call it
    s=(GetAsyncKeyState(VK_SHIFT)<0);		/* Check modifier keys			*/
    c=(GetAsyncKeyState(VK_CONTROL)<0);
    a=(GetAsyncKeyState(VK_MENU)<0);
    ch=getch();					/* Get second key byte			*/
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
#else

// Win+SDL, Unix+SDL, Unix+NoSDL, Amiga, BEOS, MacOS
  ch=emulate_get();				/* Call legacy code			*/
  if (ch == 0) {
    ch=emulate_get();				/* Call legacy code			*/
    if (ch & 0x80) ch = ch | 0x100;
  }						/* 0x000+n=raw key, 0x100+n=special key	*/
  return ch;
#endif
}


/* kbd_readline - read a line of text from input stream
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

#ifdef TARGET_RISCOS
  // RISC OS, pass directly to MOS
  // -----------------------------
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  int32 carry;

// RISC OS compiler doesn't like code before variable declaration, so have to duplicate
  if (length<1) return 0;			/* Filter out impossible or daft calls	*/
  chars=(chars & 0xFF) | 0x00FF2000;		/* temp'y force all allowable chars	*/

  regs.r[0] = TOINT(&buffer[0]);
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
#else /* !RISCOS */

  if (length<1) return 0;			/* Filter out impossible or daft calls	*/
  chars=(chars & 0xFF) | 0x00FF2000;		/* temp'y force all allowable chars	*/

  length=(int32)emulate_readline(&buffer[0], length, chars & 0xFF);
  if (length==READ_OK) return strlen(buffer);
  if (length==READ_ESC) return -1;
  return -2;

#endif /* !RISCOS */
}

#endif /* NEWKBD */



/* Legacy code from here onwards */
/* ----------------------------- */
#ifdef USE_SDL
//#include "SDL.h"
//#include "SDL_events.h"
//#include "keyboard-inkey.h"

//extern void mode7flipbank();
//extern void reset_vdu14lines();

static Uint32 waitkey_callbackfunc(Uint32 interval, void *param)
{
  SDL_Event event;
  SDL_UserEvent userevent;
  userevent.type = SDL_USEREVENT;
  userevent.code = 0;
  userevent.data1 = NULL;
  userevent.data2 = NULL;

  event.type = SDL_USEREVENT;
  event.user = userevent;

  SDL_PushEvent(&event);
  return(0);  /* cancel the timer */
}
#endif

//static int nokeyboard=0;
//static int escint=128;
//static int escmul=1;
//static int fx44x=1;

#ifdef TARGET_RISCOS

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
  regs.r[0] = TOINT(&buffer[0]);
  regs.r[1] = length-1;         /* -1 to allow for a NULL to be added at the end in all cases */
  regs.r[2] = 32;               /* Allow any character to be input */
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

#else

/* ==================================================================== */
/* DOS/Linux/NetBSD/FreeBSD/MacOS/OpenBSD/AmigaOS versions of functions */
/* ==================================================================== */

#include <stdlib.h>

#if defined(TARGET_LINUX) | defined(TARGET_NETBSD) | defined(TARGET_MACOSX)\
 | defined(TARGET_DJGPP) | defined(TARGET_FREEBSD) | defined(TARGET_OPENBSD)\
 | defined(TARGET_AMIGA) & defined(__GNUC__)\
 | defined(TARGET_GNUKFREEBSD) | defined(TARGET_GNU)
#include <sys/time.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#endif

#if defined(TARGET_WIN32) | defined(TARGET_BCC32) | defined(TARGET_MINGW)
#include <sys/time.h>
#include <conio.h>
#endif

#ifdef TARGET_DJGPP
#include <pc.h>
#include <keys.h>
#endif

#define WAITIME 10              /* Time to wait in centiseconds when dealing with ANSI key sequences */

// #define HISTSIZE 1024           /* Size of command history buffer */
// #define MAXHIST 20              /* Maximum number of entries in history list */
//
// static int32
//   place,                        /* Offset where next character will be added to buffer */
//   highplace,                    /* Highest value of 'place' (= number of characters in buffer) */
//   histindex,                    /* Index of next entry to fill in in history list */
//   highbuffer,                   /* Index of first free character in 'histbuffer' */
//   recalline;                    /* Index of last line recalled from history display */
//
// static boolean enable_insert;   /* TRUE if keyboard input code is in insert mode */
//
// static char histbuffer[HISTSIZE];       /* Command history buffer */
// static int32 histlength[MAXHIST];       /* Table of sizes of entries in history buffer */

//#define FN_KEY_COUNT 16         /* Number of function keys supported (0 to FN_KEY_COUNT-1) */

///* function key strings */
//static struct {int length; char *text;} fn_key[FN_KEY_COUNT];
//
///*
//** holdcount and holdstack are used when decoding ANSI key sequences. If a
//** sequence is read that does not correspond to an ANSI sequence the
//** characters are stored here so that they can be returned by future calls
//** to 'emulate_get'. Note that this is a *stack* not a queue
//*/
//static int32 holdcount;         /* Number of characters held on stack */
//static int32 holdstack[8];      /* Hold stack - Characters waiting to be passed back via 'get' */
//
///*
//** fn_string and fn_string_count are used when expanding a function
//** key string. Effectively input switches to the string after a
//** function key with a string associated with it is pressed
//*/
//static char *fn_string;         /* Non-NULL if taking chars from a function key string */
//static int fn_string_count;     /* Count of characters left in function key string */ 

#if defined(TARGET_LINUX) | defined(TARGET_NETBSD) | defined(TARGET_MACOSX)\
 | defined(TARGET_FREEBSD) |defined(TARGET_OPENBSD) | defined(TARGET_AMIGA) & defined(__GNUC__)\
 | defined(TARGET_GNUKFREEBSD) | defined(TARGET_GNU)

static struct termios origtty;  /* Copy of original keyboard parameters */
static int32 keyboard;          /* File descriptor for keyboard */

#endif

/* The following functions are common to all operating systems */

/*
** 'push_key' adds a key to the held key stack
*/
static void push_key(int32 ch) {
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
  while(SDL_PollEvent(&ev)) ;
#endif
}

/*
** set_fn_string - Define a function key string
*/
int set_fn_string(int key, char *string, int length) {
  if (fn_string_count) return fn_string_count;
  if (fn_key[key].text != NIL) free(fn_key[key].text);
  fn_key[key].length = length;
  fn_key[key].text = malloc(length);
  if (fn_key[key].text != NIL) memcpy(fn_key[key].text, string, length);
  return 0;
}

/*
** get_fn_string - Get a function key string for *SHOW
*/
char *get_fn_string(int key, int *len) {
  *len=fn_key[key].length;
  return fn_key[key].text;
}

/*
** switch_fn_string - Called to switch input to a function
** key string. It returns the first character of the string
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

/*
** read_fn_string - Called when input is being taken from a
** function key string, that is, fn_string is not NULL. It
** returns the next character in the string.
*/
static int32 read_fn_string(void) {
  int32 ch;  
  ch = *fn_string;
  fn_string++;
  fn_string_count--;
  if (fn_string_count == 0) fn_string = NIL;    /* Last character read */
  return ch;
}

/*
 * is_fn_key - Returns the function key number if the RISC OS
 * key code passed to it is one for a function key. This is used
 * when checking for function key strings, so shifted and CTRL'ed
 * function keys are of no interest
 */
static int32 is_fn_key(int32 key) {
  if (key >= KEY_F1 && key <= KEY_F9) return key - KEY_F1 + 1;
  if (key >= KEY_F10 && key <= KEY_F12) return key - KEY_F10 + 10;
/* Not a function key */
  return 0;
}

void set_escint(int i) {
  if (i==0) {
    escint=128;
    escmul=0;
  } else escint=i;
}
void set_escmul(int i) {
  escmul=i<<8;
}

int escinterval=0;
int64 esclast=0;

void checkforescape(void) {
#ifdef USE_SDL
int64 i;
  if (!escinterval) {
    escinterval=escint+escmul;
    i=mos_centiseconds();
    if (i > esclast) {
      esclast=i;
#ifdef NEWKBD
      if(kbd_inkey(-113) && basicvars.escape_enabled) basicvars.escape=TRUE;
#else
      if(emulate_inkey(-113) && basicvars.escape_enabled) basicvars.escape=TRUE;
#endif
    }
  } else escinterval--;
#endif
  return;
}

void osbyte44(int x) {
  fx44x=x;
}

#if defined(TARGET_DJGPP)
// Should be merged with following
static boolean waitkey(int wait) {
  int tmp;
  tmp=clock()+wait; //*(CLOCKS_PER_SEC/100);
  for(;;) { if(kbhit() || (clock()>tmp)) break; }
  return kbhit();
}
#endif

#if defined(TARGET_LINUX) | defined(TARGET_NETBSD) | defined(TARGET_MACOSX)\
 | defined(TARGET_FREEBSD) | defined(TARGET_OPENBSD) | defined(TARGET_AMIGA) & defined(__GNUC__)\
 | defined(TARGET_GNUKFREEBSD) | defined(TARGET_GNU) | defined(TARGET_MINGW)

/* ----- Linux-, *BSD- and MACOS-specific keyboard input functions ----- */

/*
** 'waitkey' is called to wait for up to 'wait' centiseconds for
** keyboard input. It returns non-false if there is a character available
**
*/
static boolean waitkey(int wait) {
#ifndef TARGET_MINGW
  fd_set keyset;
  struct timeval waitime;
#endif
#ifdef BODGEMGW
  int tmp;
#endif

#ifdef USE_SDL
  SDL_Event ev;
  SDL_TimerID timer_id = NULL;
/* set up timer if wait time not zero */
  if (wait != 0) timer_id = SDL_AddTimer(wait*10, waitkey_callbackfunc, 0);
  while ( 1 ) {
/*
 * First check for SDL events
*/
    while (SDL_PollEvent(&ev) > 0) 
      switch(ev.type)
      {
        case SDL_USEREVENT:
	  if (timer_id) SDL_RemoveTimer(timer_id);
	  return 0;             /* timeout expired */
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
	      if (timer_id) SDL_RemoveTimer(timer_id);
              SDL_PushEvent(&ev);  /* we got a char - push the event back and say we found one */
              return 1;
              break;
          }
          break;
        case SDL_QUIT:
          exit_interpreter(EXIT_SUCCESS);
          break;
      }
#ifndef TARGET_MINGW
/*
 * Then for stdin keypresses
*/
#ifndef BODGEMGW
    FD_ZERO(&keyset);
    FD_SET(keyboard, &keyset);
#endif
    waitime.tv_sec = waitime.tv_usec = 0;
    if ( select(1, &keyset, NIL, NIL, &waitime) > 0 ) return 1;
#endif
    if (wait == 0) return 0; /* return after one check if wait time = 0 */
    usleep(1000);
  }
#else
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
  int errcode;
  byte ch = 0;
#ifdef USE_SDL
#ifndef TARGET_MINGW
  fd_set keyset;
  struct timeval waitime;
#endif
  SDL_Event ev;

  while (ch == 0) {
/*
** First check the SDL event Queue
*/
    mode7flipbank();
    if (SDL_PollEvent(&ev))
      switch(ev.type)
      {
	case SDL_KEYUP:
	  break;
        case SDL_KEYDOWN:
          switch(ev.key.keysym.sym)
          {
            case SDLK_RSHIFT:   /* ignored keys */
            case SDLK_LSHIFT:
            case SDLK_RCTRL:
            case SDLK_LCTRL:
            case SDLK_RALT:
            case SDLK_LALT:
              break;

/* There is a better way to do this */
            case SDLK_PRINT:
            case SDLK_END:
            case SDLK_LEFT:
            case SDLK_RIGHT:
            case SDLK_DOWN:
            case SDLK_UP:
            case SDLK_PAGEUP:
            case SDLK_PAGEDOWN:
            case SDLK_INSERT:
              switch(ev.key.keysym.sym) {
                case SDLK_PRINT:    ch=KEY_F0; break;
                case SDLK_END:      ch=END;    break;
                case SDLK_LEFT:     ch=LEFT;   break;
                case SDLK_RIGHT:    ch=RIGHT;  break;
                case SDLK_DOWN:     ch=DOWN;   break;
                case SDLK_UP:       ch=UP;     break;
                case SDLK_PAGEUP:   ch=PGUP;   break;
                case SDLK_PAGEDOWN: ch=PGDOWN; break;
                case SDLK_INSERT:   ch=INSERT; break;
		default: break;
              }
              if (ev.key.keysym.mod & KMOD_SHIFT) ch ^= 0x10;
              if (ev.key.keysym.mod & KMOD_CTRL)  ch ^= 0x20;
              if (ev.key.keysym.mod & KMOD_ALT)   ch ^= 0x30;
              push_key(ch);
              return NUL;
            case SDLK_HOME:
              return HOME;
            case SDLK_DELETE:
              return DELETE;
            case SDLK_ESCAPE:
	      if (basicvars.escape_enabled) error(ERR_ESCAPE);
	      return ESCAPE;
            case SDLK_F1: case SDLK_F2: case SDLK_F3: case SDLK_F4: case SDLK_F5:
            case SDLK_F6: case SDLK_F7: case SDLK_F8: case SDLK_F9:
              ch = KEY_F1 + ev.key.keysym.sym - SDLK_F1;
              if (ev.key.keysym.mod & KMOD_SHIFT) ch ^= 0x10;
              if (ev.key.keysym.mod & KMOD_CTRL)  ch ^= 0x20;
              if (ev.key.keysym.mod & KMOD_ALT)   ch ^= 0x30;
              push_key(ch);
              return NUL;
            case SDLK_F10: case SDLK_F11: case SDLK_F12:
              ch = KEY_F10 + ev.key.keysym.sym - SDLK_F10;
              if (ev.key.keysym.mod & KMOD_SHIFT) ch ^= 0x10;
              if (ev.key.keysym.mod & KMOD_CTRL)  ch ^= 0x20;
              if (ev.key.keysym.mod & KMOD_ALT)   ch ^= 0x30;
              push_key(ch);
              return NUL;
            default:
              ch = ev.key.keysym.unicode;
              return ch;
          }
          break;
        case SDL_QUIT:
          exit_interpreter(EXIT_SUCCESS);
          break;
      }
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
    SDL_Delay(10);
  }
#else /* ! USE_SDL */
#ifndef BODGEMGW
  errcode = read(keyboard, &ch, 1);
#endif
  if (errcode < 0) {            /* read() returned an error */
#ifndef BODGEMGW
    if (basicvars.escape_enabled && (errno == EINTR)) error(ERR_ESCAPE);      /* Assume Ctrl-C was pressed */
#endif
    error(ERR_BROKEN, __LINE__, "keyboard");    /* Otherwise roll over and die */
  }
#endif
  return ch;
}

/*
** 'decode_sequence' reads a possible ANSI escape sequence and attempts
** to decode it, converting it to a RISC OS key code. It returns the first
** character of the key code (a null) if the sequence is recognised or
** the first character of the sequence read if it cannot be identified.
** Note that the decoding is incomplete as the function only deals
** with keys of interest to it. Note also that it deals with both Linux
** and NetBSD key sequences, for example, the one for 'F1' under Linux
** is 1B 5B 5B 41 and for NetBSD it is 1B 4F 50. It should also be noted
** that the range of keys that can be decoded is a long way  short of the
** key combinations possible. Lastly, the same ANSI sequences are used for
** more than one key, for example, F11 and shift-F1 both return the same
** code.
**
** States:
** 1  ESC read          2  ESC 'O'              3  ESC '['
** 4  ESC '[' '1'       5  ESC '[' '2'          6  ESC '[' '3'
** 7  ESC '[' '4'       8  ESC '[' '5'          9  ESC '[' '6'
** 10 ESC '[' '['
** 11 ESC '[' '1' '1'   12 ESC '[' '1' '2'      13 ESC '[' '1' '3'
** 14 ESC '[' '1' '4'   15 ESC '[' '1' '5'      16 ESC '[' '1' '7'
** 17 ESC '[' '1' '8'   18 ESC '[' '1' '9'
** 19 ESC '[' '2' '0'   20 ESC '[' '2' '1'      21 ESC '[' '2' '3'
** 12 ESC '[' '2' '4'   23 ESC '[' '2' '5'      24 ESC '[' '2' '6'
** 25 ESC '[' '2' '8'   26 ESC '[' '2' '9'
** 27 ESC '[' '3' '1'   28 ESC '[' '3' '2'      29 ESC '[' '3' '3'
** 30 ESC '[' '3' '4'
**
** This code cannot deal with the 'alt' key sequences that KDE passes
** through. alt-home, for example, is presented as ESC ESC '[' 'H' and
** as 'ESC ESC' is not a recognised sequence the function simply passes
** on the data as supplied.
**
** Note: there seem to be some NetBSD escape sequences missing here
*/
static int32 decode_sequence(void) {
  int state, newstate;
  int32 ch;
  boolean ok;
  static int32 state2key [] = { /* Maps states 11 to 24 to function key */
    KEY_F1, KEY_F2, KEY_F3, KEY_F4,             /* [11..[14 */
    KEY_F5, KEY_F6, KEY_F7, KEY_F8,             /* [15..[19 */
    KEY_F9, KEY_F10, KEY_F11, KEY_F12,          /* [20..[24 */
    SHIFT_F3, SHIFT_F4, SHIFT_F5, SHIFT_F6,     /* [25..[29 */
    SHIFT_F7, SHIFT_F8, SHIFT_F9, SHIFT_F10     /* [31..[34 */
  };
  static int statelbno[] = {4, 5, 6, 7, 8, 9};
/*
** The following tables give the next machine state for the character
** input when handling the ESC '[' '1', ESC '[' '2' and ESC '[' '3'
** sequences
*/
  static int state1[] = {11, 12, 13, 14, 15, 0, 16, 17, 18};    /* 1..9 */
  static int state2[] = {19, 20, 0, 21, 22, 23, 24, 0, 25, 26}; /* 0..9 */
  static int state3[] = {27, 28, 29, 30};       /* 1..4 */
  state = 1;    /* ESC read */
  ok = TRUE;
  while (ok && waitkey(WAITIME)) {
    ch = read_key();
    switch (state) {
    case 1:     /* ESC read */
      if (ch == 'O')    /* ESC 'O' */
        state = 2;
      else if (ch == '[')       /* ESC '[' */
        state = 3;
      else {    /* Bad sequence */
        ok = FALSE;
      }
      break;
    case 2:     /* ESC 'O' read */
      if (ch >= 'P' && ch <= 'S') {     /* ESC 'O' 'P'..'S' */
        push_key(ch - 'P' + KEY_F1);    /* Got NetBSD F1..F4. Map to RISC OS F1..F4 */
        return NUL;     /* RISC OS first char of key sequence */
      }
      else {    /* Not a known key sequence */
        ok = FALSE;
      }
      break;
    case 3:     /* ESC '[' read */
      switch (ch) {
      case 'A': /* ESC '[' 'A' - cursor up */
        push_key(UP);
        return NUL;
      case 'B': /* ESC '[' 'B' - cursor down */
        push_key(DOWN);
        return NUL;
      case 'C': /* ESC '[' 'C' - cursor right */
        push_key(RIGHT);
        return NUL;
      case 'D': /* ESC '[' 'D' - cursor left */
        push_key(LEFT);
        return NUL;
      case 'F': /* ESC '[' 'F' - 'End' key */
        push_key(END);
        return NUL;
      case 'H': /* ESC '[' 'H' - 'Home' key */
        return HOME;
      case '1': case '2': case '3': case '4': case '5': case '6': /* ESC '[' '1'..'6' */
        state = statelbno[ch - '1'];
        break;
      case '[': /* ESC '[' '[' */
        state = 10;
        break;
      default:
        ok = FALSE;
      }
      break;
    case 4:     /* ESC '[' '1' read */
      if (ch >= '1' && ch <= '9') {     /* ESC '[' '1' '1'..'9' */
        newstate = state1[ch - '1'];
        if (newstate == 0)      /* Bad character */
          ok = FALSE;
        else {
          state = newstate;
        }
      }
      else if (ch == '~')       /* ESC '[' '1 '~' - 'Home' key */
        return HOME;
      else {
        ok = FALSE;
      }
      break;
    case 5:     /* ESC '[' '2' read */
      if (ch >= '0' && ch <= '9') {     /* ESC '[' '2' '0'..'9' */
        newstate = state2[ch - '0'];
        if (newstate == 0)      /* Bad character */
          ok = FALSE;
        else {
          state = newstate;
        }
      }
      else if (ch == '~') {     /* ESC '[' '2' '~' - 'Insert' key */
        push_key(INSERT);
        return NUL;
      }
      else {
        ok = FALSE;
      }
      break;
    case 6:     /* ESC '[' '3' read */
      if (ch >= '1' && ch <= '4') {     /* ESC '[' '3' '1'..'4' */
        newstate = state3[ch - '1'];
        if (newstate == 0)      /* Bad character */
          ok = FALSE;
        else {
          state = newstate;
        }
      }
      else if (ch == '~')
        return CTRL_H;  /* ESC '[' '3' '~' - 'Del' key */
      else {
        ok = FALSE;
      }
      break;
    case 7:     /* ESC '[' '4' read */
      if (ch == '~') {  /* ESC '[' '4' '~' - 'End' key */
        push_key(END);
        return NUL;
      }
      ok = FALSE;
      break;
    case 8:     /* ESC '[' '5' read */
      if (ch == '~') {  /* ESC '[' '5' '~' - 'Page up' key */
        push_key(PGUP);
        return NUL;
      }
      ok = FALSE;
      break;
    case 9:     /* ESC '[' '6' read */
      if (ch == '~') {  /* ESC '[' '6' '~' - 'Page down' key */
        push_key(PGDOWN);
        return NUL;
      }
      ok = FALSE;
      break;
    case 10:    /* ESC '[' '[' read */
      if (ch >= 'A' && ch <= 'E') {     /* ESC '[' '[' 'A'..'E' -  Linux F1..F5 */
        push_key(ch - 'A' + KEY_F1);
        return NUL;
      }
      ok = FALSE;
      break;
    case 11: case 12: case 13: case 14: /* ESC '[' '1' '1'..'4' */
    case 15: case 16: case 17: case 18: /* ESC '[' '1' '5'..'9' */
    case 19: case 20:                           /* ESC '[' '2' '0'..'1' */
    case 21: case 22: case 23: case 24: /* ESC '[' '2' '3'..'6' */
    case 25: case 26:                           /* ESC '[' '2' '8'..'9' */
    case 27: case 28: case 29: case 30: /* ESC '[' '3' '1'..'4' */
      if (ch == '~') {
        push_key(state2key[state - 11]);
        return NUL;
      }
      ok = FALSE;
    }
  }
/*
** Incomplete or bad sequence found. If it is bad then 'ok' will be set to
** 'false'. If incomplete, 'ok' will be 'true'. 'ch' will be undefined
** in this case.
*/
  if (!ok) push_key(ch);
  switch (state) {
  case 1:       /* ESC read */
    return ESCAPE;
  case 2:       /* ESC 'O' read */
    push_key('O');
    return ESCAPE;
  case 3:       /* ESC '[' read */
    break;
  case 4: case 5: case 6: case 7:
  case 8: case 9:       /* ESC '[' '1'..'6' read */
    push_key('1' + state - 4);
    break;
  case 10:      /* ESC '[' '[' read */
    push_key('[');
    break;
  case 11: case 12: case 13: case 14: case 15:  /* ESC '[' '1' '1'..'5' read */
    push_key(1 + state - 11);
    push_key('1');
    break;
  case 16: case 17: case 18:    /* ESC '[' '1' '7'..'9' read */
    push_key('7' + state - 16);
    push_key('1');
    break;
  case 19: case 20:                     /* ESC '[' '2' '0'..'1' read */
    push_key('0' + state - 19);
    push_key('2');
    break;
  case 21: case 22: case 23: case 24:   /* ESC '[' '2' '3'..'6' read */
    push_key('3' + state - 21);
    push_key('2');
    break;
  case 25: case 26:                     /* ESC '[' '2' '8'..'9' read */
    push_key('8' + state - 25);
    push_key('2');
  case 27: case 28: case 29: case 30:   /* ESC '[' '3' '1'..'4' */
    push_key('1' + state - 27);
    push_key(3);
  }
  push_key('[');
  return ESCAPE;
}

#ifndef BODGEMGW
/*
** 'emulate_get' emulates the Basic function 'get'. It is also the
** input routine used when reading a line from the keyboard
** Returns 00,nn for special keys, nn for normal keys.
*/
int32 emulate_get(void) {
  byte ch;
  int32 key, fn_keyno;
#ifndef USE_SDL
  int32 errcode;
#endif

// #ifdef USE_SDL
//   reset_vdu14lines();	/* This should be done in INPUT not in GET */
// #endif
  if (basicvars.runflags.inredir) error(ERR_UNSUPPORTED);       /* Not reading from the keyboard */
/*
 * Check if characters are being taken from a function
 * key string and if so return the next one
 */
  if (fn_string != NIL) return read_fn_string();
  if (holdcount > 0) return pop_key();  /* Return character from hold stack if one is present */
#ifdef USE_SDL
  ch = read_key();
#else
  errcode = read(keyboard, &ch, 1);
  if (errcode < 0) {
    if(basicvars.escape_enabled && (errno == EINTR)) error(ERR_ESCAPE);       /* Assume CTRL-C has been pressed */
    error(ERR_BROKEN, __LINE__, "keyboard");
  }
#endif
  ch = ch & BYTEMASK;
  if ((ch != ESCAPE) && (ch != 0)) return ch;
/*
 * Either ESC was pressed or it marks the start of an ANSI
 * escape sequence for a function key, cursor key or somesuch.
 * Try to make sense of what follows. If function key was
 * pressed, check to see if there is a function key string
 * associated with it and return the first character of
 * the string otherwise return a NUL. (The next character
 * returned will be the RISC OS key code in this case)
 */
  key = ch;
  if (ch == ESCAPE) key = decode_sequence();
  if (key != NUL) return key;
/* NUL found. Check for function key */
  key = pop_key();
  fn_keyno = is_fn_key(key);
  if (fn_keyno == 0) {	/* Not a function key - Return NUL then key code */
    push_key(key);
    return NUL;
  }
/* Function key hit. Check if there is a function key string */
  if (fn_key[fn_keyno].text == NIL || fn_key[fn_keyno].length == 0) {
    push_key(key);	/* No string is defined for this key */
    return NUL;
  }
/*
 * There is a function key string. Switch input to string
 * and return the first character
 */
  return switch_fn_string(fn_keyno);
}

/*
** 'emulate_inkey' emulates the Basic function 'inkey'. Only the 'timed wait',
** individual key scan and 'OS version' flavours of the function are supported.
** Note that the behaviour of the RISC OS version of INKEY with a +ve argument
** appears to be undefined if the wait exceeds 32767 centiseconds.
*/

//#ifdef USE_SDL
//Uint8 mousestate, *keystate=NULL;
//#endif

int32 emulate_inkey(int32 arg) {
  int32 result;
#ifdef USE_SDL
  mode7flipbank();
#endif
  if (arg >= 0) {	/* Timed wait for a key to be pressed */
    if (basicvars.runflags.inredir) error(ERR_UNSUPPORTED);     /* There is no keyboard to read */
    if (arg > INKEYMAX) arg = INKEYMAX; /* Wait must be in range 0..32767 centiseconds */
    if (waitkey(arg)) {
      do {
        result=emulate_get();
      } while (result==0);
      return result;	/* Fetch the key if one is available */
    } else {
      return -1;	/* Otherwise return -1 to say that nothing arrived in time */
    }
  }
  else if (arg == -256)	/* Return version of operating system */
    return OSVERSION;
  else {		/* Check is a specific key is being pressed */
#ifdef USE_SDL
    SDL_Event ev;
    if ((arg < -128) && (arg > -256)) return -1;	/* Scan range unimplemented */
    SDL_PumpEvents();
    keystate = SDL_GetKeyState(NULL);
    mousestate = SDL_GetMouseState(NULL, NULL);
    while(SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) exit_interpreter(EXIT_SUCCESS);
    }

    if ((arg & 0xFE00) == 0xFC00) {
    if (keystate[arg & 0x3FF])	// do raw API test, caution: can cause address error
      return -1;
    else
      return 0;
    }

    if ((arg <= -10) && (arg >= -12)) {
      /* Mouse button INKEYs */
      if ((arg == -10) && (mousestate & 1)) return -1;
      if ((arg == -11) && (mousestate & 2)) return -1;
      if ((arg == -12) && (mousestate & 4)) return -1;
    }
    if (arg >= -3) {
      /* Either modifier key */
      if (
      (keystate[inkeylookup[(arg * -1) +3-1]]) /* left key  */
      ||
      (keystate[inkeylookup[(arg * -1) +6-1]]) /* right key */
      ) return -1;
      else
        return 0;
    }
    if (keystate[inkeylookup[(arg * -1) -1]])
      return -1;
    else
      return 0;
#else
    error(ERR_UNSUPPORTED);     /* Check for specific key is unsupported */
#endif
  }
  return 0;
}
#endif

/* This uses existing values of keystate and mousestate */
/* This is only ever called to check for SHIFT key for paged scrolling */
int32 emulate_inkey2(int32 arg) {
#ifdef USE_SDL
  if (!keystate) return 0; /* Be nice if we've not called emulate_inkey earlier */
  if ((arg <= -10) && (arg >= -12)) {
    /* Mouse button INKEYs */
    if ((arg == -10) && (mousestate & 1)) return -1;
    if ((arg == -11) && (mousestate & 2)) return -1;
    if ((arg == -12) && (mousestate & 4)) return -1;
  }
    if (arg >= -3) {
      /* Either modifier key */
      if (
      (keystate[inkeylookup[(arg * -1) +3-1]]) /* left key  */
      ||
      (keystate[inkeylookup[(arg * -1) +6-1]]) /* right key */
      ) return -1;
      else
        return 0;
    }
  if (keystate[inkeylookup[(arg * -1) -1]])
    return -1;
  else
    return 0;
#else
  error(ERR_UNSUPPORTED);     /* Check for specific key is unsupported */
  return 0; /* Control never reaches here, but keeps the compiler happy */
#endif
}

#endif

#if defined(TARGET_DJGPP) | defined(TARGET_WIN32) | defined(TARGET_BCC32) | defined(TARGET_MINGW)

/* ----- DJGPP/WIN32/DOS keyboard input functions ----- */

/*
** This table gives the mapping from DOS extended key codes to the RISC OS
** equivalents documented in the PRMs for special keys such as 'insert'
** and the function keys.
**
** Under DOS, the special keys, for example, 'Home', 'Page Up', 'F1' and so
** forth, appear as a two byte sequence where the first byte is an escape
** character and the second denotes the key. The escape character for DJGPP
** is always a null. The LCC-WIN32 version of 'getch' uses the value 0xE0
** for keys such as 'Home' but zero for the function keys. It also uses zero
** for the 'alt' versions of the keys. Only unshifted, control- or alt-
** versions of the specials keys can be detected by this code
*/
#ifndef NEWKBD
static byte dostable []  = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
  0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x81, 0x82, 0x83, 0x84, 0x85,
  0x86, 0x87, 0x88, 0x89, 0xCA, 0x45, 0x46, 0x1E, 0x8F, 0x9F, 0x4A, 0x8C, 0x4C, 0x8D, 0x4E, 0x8B,
  0x8E, 0x9E, 0xCD, 0x7F, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0xDA, 0xA1, 0xA2,
  0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xEA, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
  0x70, 0x71, 0x72, 0xAC, 0xAD, 0xAB, 0xBE, 0x1E, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
  0x80, 0x81, 0x82, 0x83, 0xBF, 0xCB, 0xCC, 0xDB, 0xDC, 0xEB, 0xEC, 0x8B, 0x8C, 0xAF, 0x8E, 0x8F,
  0x90, 0xAE, 0xED, 0x7F, 0x94, 0x95, 0x96, 0x97, 0x98, 0x00, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
  0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
  0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
  0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
  0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
  0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
  0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF
};
#endif
#endif
#ifndef USE_SDL
#if defined(TARGET_WIN32) | defined(TARGET_BCC32) | defined(TARGET_MINGW)
/*
** 'emulate_get' deals with the Basic function 'get'
*/
int32 emulate_get(void) {
  int32 ch, fn_keyno, key;
  
  if (basicvars.runflags.inredir) error(ERR_UNSUPPORTED);       /* There is no keyboard to read */
/*
 * Check if characters are being taken from a function
 * key string and if so return the next one
 */
  if (fn_string != NIL) return read_fn_string();
  if (holdcount > 0) return pop_key();  /* Return held character if one is present */
  ch = getch();
  if (ch == NUL || ch == 0xE0) {        /* DOS escape characters */
    ch = dostable[getch()];
    if (ch == HOME) return HOME;        /* Special case - 'Home' is only one byte */
/* Check for RISC OS function key */
    fn_keyno = is_fn_key(ch);
    if (fn_keyno == 0) {                /* Not a function key - Return NUL then key code */
      push_key(ch);
      return NUL;
    }
/* Function key hit. Check if there is a function key string */
    if (fn_key[fn_keyno].text == NIL || fn_key[fn_keyno].length == 0) {
      push_key(ch);			/* No string is defined for this key */
      return NUL;
    }
/*
 * There is a function key string. Switch input to string
 * and return the first character
 */
    return switch_fn_string(fn_keyno);
  }
  return ch & BYTEMASK;
}

/*
** 'emulate_inkey' emulates the Basic function 'inkey'. Only the 'OS
** version' flavour of the function are supported
*/
int32 emulate_inkey(int32 arg) {
  if (arg >= 0) {       	/* Timed wait for a key to be hit */
    if (arg > INKEYMAX) arg = INKEYMAX; /* Wait must be in range 0..32767 centiseconds */
    error(ERR_UNSUPPORTED);
  }
  else if (arg == -256)         /* Return version of operating system */
    return OSVERSION;
  else {        		/* Check if a specific key is being pressed */
    error(ERR_UNSUPPORTED);     /* Check for specific key is unsupported */
  }
  return 0;
}

#endif /* MINGW etc */
#endif /* ! USE_SDL */

#ifdef TARGET_DJGPP

#define EXTENDCHAR 0x100        /* Extended keys have codes >= 0x100 */
/*
** 'emulate_get' deals with the Basic function 'get'
*/
int32 emulate_get(void) {
  int32 ch, fn_keyno;
  if (basicvars.runflags.inredir) error(ERR_UNSUPPORTED);       /* There is no keyboard to read */
/*
 * Check if characters are being taken from a function
 * key string and if so return the next one
 */
  if (fn_string != NIL) return read_fn_string();
/*
 * Normal keyboard input
 */
  if (holdcount > 0) return pop_key();  /* Return held character if one is present */
  ch = getxkey();
  if (ch >= EXTENDCHAR) {               /* DOS function key or other extended key */
    ch = dostable[ch & BYTEMASK];
    if (ch == HOME) return HOME;        /* Special case - 'Home' is only one byte */
/* Check for RISC OS function key */
    fn_keyno = is_fn_key(ch);
    if (fn_keyno == 0) {        /* Not a function key - Return NUL then key code */
      push_key(ch);
      return NUL;
    }
/* Function key hit. Check if there is a function key string */
    if (fn_key[fn_keyno].text == NIL|| fn_key[fn_keyno].length == 0) {
      push_key(ch);		/* No string is defined for this key */
      return NUL;
    }
/*
 * There is a function key string. Switch input to string
 * and return the first character
 */
    return switch_fn_string(fn_keyno);
  }
  return ch & BYTEMASK;
}

/*
** 'emulate_inkey' emulates the Basic function 'inkey'. Only the 'timed wait'
** and 'OS version' flavours of the function are supported
*/
int32 emulate_inkey(int32 arg) {
  if (arg >= 0) {       /* Timed wait for a key to be hit */
  if (basicvars.runflags.inredir) error(ERR_UNSUPPORTED);       /* There is no keyboard to read */
    if (arg > INKEYMAX) arg = INKEYMAX;         /* Wait must be in the range 0..32767 centiseconds */
    if (holdcount > 0) return pop_key();        /* There is a character waiting so return that */
    if (kbhit()) return emulate_get();          /* There is an unread key waiting */
    while (arg > 0) {   /* Wait for 'arg' centiseconds, checking the keyboard every centisecond */
      delay(10);        /* Wait 10 milliseconds */
      if (kbhit()) return emulate_get();        /* Return a key if one is now available */
      arg--;
    }
    return -1;          /* Timeout - Return -1 to indicate that nothing was read */
  }
  else if (arg == -256)         /* Return version of operating system */
    return OSVERSION;
  else {        		/* Check if a specific key is being pressed */
    error(ERR_UNSUPPORTED);     /* Check for specific key is unsupported */
  }
  return 0;
}

#endif

#ifdef TARGET_BEOS

int32 emulate_get(void) {
  error(ERR_UNSUPPORTED);
  return 0;
}

int32 emulate_inkey(int32 arg) {
  error(ERR_UNSUPPORTED);
  return 0;
}

#endif

#ifdef TARGET_AMIGA

// Why is this here?
// Moved to target.h
//#ifndef __AMIGADATE__
//#define __AMIGADATE__ "("__DATE__")"
//#endif
//const char *VERsion = "$VER: Brandy 1.19 "__AMIGADATE__" $";

#ifdef __SASC
long __stack = 67000;

/*
** 'read_key' reads the next character from the keyboard
*/
int32 read_key(void) {
  byte ch;
  ch = getchar();
/*    if (errno == EINTR) error(ERR_ESCAPE);    / * Assume Ctrl-C was pressed */
  return ch;
}

int32 emulate_get(void) {
/*  error(ERR_UNSUPPORTED);*/
  return read_key();
}

int32 emulate_inkey(int32 arg) {
  error(ERR_UNSUPPORTED);
  return 0;
}

boolean init_keyboard(void) {
  rawcon(1);
  return TRUE;
}

void end_keyboard(void) {
  rawcon(0);
}
#endif

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
    buffer[0] = NUL;
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

  if (basicvars.runflags.inredir) {     /* There is no keyboard to read - Read from file stdin */
    char *p;
    p = fgets(buffer, length, stdin);	/* Get all in one go */
    if (p == NIL) {     /* Call failed */
      if (ferror(stdin)) error(ERR_READFAIL);   /* I/O error occured on stdin */
      buffer[0] = NUL;          /* End of file */
      return READ_EOF;
    }
    return READ_OK;
  }

#ifdef USE_SDL
  reset_vdu14lines();	/* This should be done in INPUT not in GET */
#endif
  highplace = strlen(buffer);
  if (highplace > 0) emulate_vdustr(buffer, highplace);
  place = highplace;
  lastplace = length-2;         /* Index of last position that can be used in buffer */
  init_recall();
  do {
#ifdef NEWKBD
    ch = kbd_get();
    if ((ch & 0x100) || (ch == DEL)) {
      pendch=ch & 0xFF;		/* temp */
      ch = NUL;
    }
#else
    ch = emulate_get();
#endif
    watch_signals();           /* Let asynchronous signals catch up */
    if (((ch == ESCAPE) && basicvars.escape_enabled) || basicvars.escape) return READ_ESC;
	/* Check if the escape key has been pressed and bail out if it has */
    switch (ch) {       /* Normal keys */
    case CR: case LF:   /* End of line */
      emulate_vdu('\r');
      emulate_vdu('\n');
      buffer[highplace] = NUL;
      if (highplace > 0) add_history(buffer, highplace);
      break;
    case CTRL_H: case DEL:      /* Delete character to left of cursor */
      if (place > 0) {
        emulate_vdu(DEL);
        place--;
        shift_down(buffer, place);
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
    case NUL:                   /* Function or special key follows */
#ifdef NEWKBD
//    ch = kbd_get();           /* Fetch the key details */
      ch = pendch; /* temp */
#else
      ch = emulate_get();       /* Fetch the key details */
#endif
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
      case DELETE:      /* Delete character at the cursor */
        if (place < highplace) shift_down(buffer, place);
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
      if (ch < ' ' && ch != TAB)        /* Reject any other control character except tab */
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
  } while (ch != CR && ch != LF);
  return READ_OK;
}

/* Initialise keyboard emulation */

#if defined(TARGET_WIN32) | defined(TARGET_BCC32) | defined(TARGET_MINGW)

boolean init_keyboard(void) {
  int n;
  for (n = 0; n < FN_KEY_COUNT; n++) fn_key[n].text = NIL;
  fn_string_count = 0;
  fn_string = NIL;
  holdcount = 0;
  histindex = 0;
  highbuffer = 0;
  enable_insert = TRUE;
  set_cursor(enable_insert);
  return TRUE;
}

void end_keyboard(void) {
}

#elif defined(TARGET_DJGPP)

/*
** 'init_keyboard' initialises the keyboard code and checks if
** stdin is connected to the keyboard or not. If it is then the
** keyboard code uses BDOS-level keyboard function to read the
** individual keys. If stdin is not connected to the keyboard
** then standard C functions are used instead. The assumption
** here is that stdin is most likely taking input from a file.
*/
boolean init_keyboard(void) {
  struct termios tty;
  int errcode, keyboard, n;
  for (n = 0; n < FN_KEY_COUNT; n++) fn_key[n].text = NIL;
  fn_string_count = 0;
  fn_string = NIL;
  holdcount = 0;
  histindex = 0;
  highbuffer = 0;
  enable_insert = TRUE;
  set_cursor(enable_insert);
/*
** Check to see if stdin is attached to a keyboard
*/
  keyboard = fileno(stdin);
  errcode = tcgetattr(keyboard, &tty);
  if (errcode == 0) return TRUE;        /* Keyboard being used */
/*
** tcgetattr() returned an error. If the error is ENOTTY then
** stdin does not point at a keyboard and so the program does
** simple reads from stdin rather than use the custom keyboard
** code. If the error is not ENOTTY then something has gone
** wrong so we abort the program
*/
  if (errno != ENOTTY) return FALSE;    /* tcgetattr() returned an error we cannot handle */
  basicvars.runflags.inredir = TRUE;    /* tcgetattr() returned ENOTTY - Use normal C functions to read input */
  return TRUE;
}

void end_keyboard(void) {
}

#elif defined(TARGET_LINUX) | defined(TARGET_NETBSD) | defined(TARGET_MACOSX)\
 | defined(TARGET_FREEBSD) | defined(TARGET_OPENBSD) | defined(TARGET_AMIGA) & defined(__GNUC__)\
 | defined(TARGET_GNUKFREEBSD) | defined(TARGET_GNU)

/*
** 'init_keyboard' initialises the keyboard code. It checks to
** see if stdin is connected to a keyboard. If it is then the
** function changes stdin to use unbuffered I/O so that the
** individual key presses can be read. This is not done if
** stdin is pointing elsewhere as it is assumed that input is
** most likely being taken from a file.
*/
boolean init_keyboard(void) {
  struct termios tty;
  int n, errcode;
  for (n = 0; n < FN_KEY_COUNT; n++) fn_key[n].text = NIL;
  fn_string_count = 0;
  fn_string = NIL;
  holdcount = 0;
  histindex = 0;
  highbuffer = 0;
  enable_insert = TRUE;
  set_cursor(enable_insert);
/*
** Set up keyboard for unbuffered I/O
*/
  keyboard = fileno(stdin);
  errcode = tcgetattr(keyboard, &tty);
  if (errcode < 0) {    /* Could not obtain keyboard parameters */
    nokeyboard=1;
#ifndef USE_SDL /* if SDL the window can still poll for keyboard input */
    if (errno != ENOTTY) return FALSE;  /* tcgetattr() returned an error we cannot handle */
/*
** The error returned by tcgetattr() was ENOTTY (not a typewriter).
** This says that stdin is not associated with a keyboard. The
** most probable cause is that input is being taken from a file.
** Input is therefore read using fgets() instead of a character
** at a time using read(). This means that the command line editor
** and history facilities as well as functions like GET and INKEY
** will not be available but if input truly is from a file then
** this will not be a problem.
*/
    basicvars.runflags.inredir = TRUE;
#endif
    return TRUE;
  }
  origtty = tty;                /* Preserve original settings for later */
#ifdef TARGET_LINUX
  tty.c_lflag &= ~(XCASE|ECHONL|NOFLSH);
#else
  tty.c_lflag &= ~(ECHONL|NOFLSH);
#endif
  tty.c_lflag &= ~(ICANON|ECHO);
  tty.c_iflag &= ~(ICRNL|INLCR);
  tty.c_cflag |= CREAD;
  tty.c_cc[VTIME] = 1;
  tty.c_cc[VMIN] = 1;
  errcode = tcsetattr(keyboard, TCSADRAIN, &tty);
  if (errcode < 0) return FALSE;        /* Could not set up keyboard in the way desired */
  return TRUE;
}

void end_keyboard(void) {
  (void) tcsetattr(keyboard, TCSADRAIN, &origtty);
}

#endif

#endif
