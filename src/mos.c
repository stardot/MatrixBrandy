/*
** This file is part of the Matrix Brandy Basic VI Interpreter.
** Copyright (C) 2000-2014 David Daniels
** Copyright (C) 2014 Jonathan Harston
** Copyright (C) 2019 David Hawes (sound code, *-command parser)
** Copyright (C) 2018-2024 Michael McConnell, Jonathan Harston and contributors
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
**	This is one the files that contains functions that emulate
**	some features of RISC OS such as the VDU drivers. The others
**	are fileio.c, keyboard.c, textonly.c, graphsdl.c and
**	riscos.c All OS-specific items should be put in these files.
**
**	Some of the functions provided in here are not supported on
**	any operating system other than RISC OS and in general using
**	any of these in a program will result in an error. However,
**	some of the features are cosmetic in that they do materially
**	affect how the program runs, for example, the colours on the
**	screen. There is a command line option, -ignore, that will
**	allow the use of these features to be silently ignored rather
**	than flagging them and bringing the program to a halt. The
**	results might not look pretty but the program will run.
**
**
** Crispian Daniels August 20th 2002:
**	Included Mac OS X target in conditional compilation.
**
** 06-Mar-2014 JGH: Rewritten *FX, simply calls OSBYTE, generalised
**                  decimal parser, added mos_osbyte(), added *HELP.
**                  *HELP reports main Brandy branch and fork.
**                  Rewritten *KEY with generalised decimal parser and
**                  generalise gstrans string converter.
** 29-Mar-2014 JGH: MinGW needs cursor restoring after system().
** 05-Apr-2014 JGH: Combined all the oscli() functions together.
**                  BEATS returns number of beats instead of current beat.
** 04-Dec-2018 JGH: *KEY checks if redefining the key currently being expanded.
** 05-Dec-2018 JGH: Added *SHOW, action currently commented out.
** 06-Dec-2018 JGH: *cd, *badcommand cursor position restored.
** 07-Dec-2018 JGH: *SHOW displays key string.
**                  Have removed some BODGE conditions.
** 09-Dec-2018 JGH: *HELP BASIC lists attributions, as per license.
**                  Layout and content needs a bit of tidying up.
** 05-Jan-2019 JGH: GSTrans returns string+length, so embedded |@ allowed in *KEY.
** 30-Aug-2019 JGH: Preparing to generalise OSBYTE 166+.
** 04-Sep-2019 JGH: Added OSBYTE system variables for OSBYTE 166+.
** 18-Oct-2019 JGH: Some keyboard OSBYTEs have to call keyboard module.
**
** Note to developers: after calling external command that generates screen output,
** need find_cursor() to restore VDU state and sometimes emulate_printf("\r\n") as well.
*/

#define _MOS_C

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
//#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include "common.h"
#include "target.h"
#include "errors.h"
#include "basicdefs.h"
#include "mos.h"
#include "mos_sys.h"
#include "screen.h"
#include "keyboard.h"
#include "miscprocs.h"

#ifdef TARGET_RISCOS
#include "kernel.h"
#include "swis.h"
#endif

#ifdef TARGET_DJGPP
#include "dos.h"
#endif

#ifdef TARGET_MINGW
#include <windows.h>
#include <tchar.h>
//#include <strsafe.h>
#endif

#if defined(TARGET_MINGW) || defined(TARGET_UNIX) || defined(TARGET_MACOSX)
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

#ifdef USE_SDL
#include "SDL.h"
#include "graphsdl.h"
#include "soundsdl.h"
extern threadmsg tmsg;
#endif

#ifndef TARGET_RISCOS
static int check_command(char *text);
static void mos_osword(int32 areg, int64 xreg);
static int32 mos_osbyte(int32 areg, int32 xreg, int32 yreg, int32 xflag);

static void native_oscli(char *command, char *respfile, FILE *respfh);
#endif

/* Address range used to identify emulated calls to the BBC Micro MOS */

#define LOW_MOS 0xFFC0
#define HIGH_MOS 0xFFF7

/* Emulated BBC MOS calls */

#define BBC_OSRDCH 0xFFE0
#define BBC_OSASCI 0xFFE3
#define BBC_OSNEWL 0xFFE7
#define BBC_OSWRCH 0xFFEE
#define BBC_OSWORD 0xFFF1
#define BBC_OSBYTE 0xFFF4
#define BBC_OSCLI  0xFFF7

/* Some defines for the Raspberry Pi GPIO support */
#define GPSET0 7
#define GPSET1 8

#define GPCLR0 10
#define GPCLR1 11

#define GPLEV0 13
#define GPLEV1 14

#define GPPUD     37
#define GPPUDCLK0 38
#define GPPUDCLK1 39

#define PI_BANK (inregs[0].i >> 5)
#define PI_BIT  (1<<(inregs[0].i & 0x1F))

#define PI_INPUT  0
#define PI_OUTPUT 1
#define PI_ALT0   4
#define PI_ALT1   5
#define PI_ALT2   6
#define PI_ALT3   7
#define PI_ALT4   3
#define PI_ALT5   2

static time_t startime;		/* Adjustment subtracted in 'TIME' */

#ifdef BRANDY_PATCHDATE
char mos_patchdate[]=__DATE__;
#endif

static void show_meminfo() {
  emulate_printf("\r\nMemory allocation information:\r\n");
  emulate_printf("  Workspace is at &" FMT_SZX ", size is &" FMT_SZX "\r\n  PAGE = &" FMT_SZX ", HIMEM = &" FMT_SZX "\r\n",
  basicvars.workspace, basicvars.worksize, basicvars.page, basicvars.himem);
  emulate_printf("  stacktop = &" FMT_SZX ", stacklimit = &" FMT_SZX "\r\n", basicvars.stacktop.bytesp, basicvars.stacklimit.bytesp);
#ifdef USE_SDL
  emulate_printf("  Video frame buffer is at &" FMT_SZX ", size &%X\r\n", matrixflags.modescreen_ptr, matrixflags.modescreen_sz);
  emulate_printf("  MODE 7 Teletext frame buffer is at &" FMT_SZX "\r\n", MODE7FB);
#endif /* USE_SDL */
  if (matrixflags.gpio) emulate_printf("  GPIO interface mapped at &" FMT_SZX "\r\n", matrixflags.gpiomem);
}

static void cmd_brandyinfo() {
  emulate_printf("\r\n%s\r\n", IDSTRING);
#ifdef BRANDY_GITCOMMIT
#ifdef BRANDY_RELEASE
  emulate_printf("  Git commit %s on branch %s (%s)\r\n", BRANDY_GITCOMMIT, BRANDY_GITBRANCH, BRANDY_GITDATE);
#else
  emulate_printf("  Built from git branch %s)\r\n", BRANDY_GITBRANCH);
#endif /* BRANDY_RELEASE */
#endif /* BRANDY_GITCOMMIT */
  // Try to get attributions correct, as per license.
  emulate_printf("  Forked from Brandy Basic v1.20.1 (24 Sep 2014)\r\n");
  emulate_printf("  Merged Banana Brandy Basic v0.02 (05 Apr 2014)\r\n");
#ifdef BRANDY_PATCHDATE
  emulate_printf("  Patch %s compiled at %s on ", BRANDY_PATCHDATE, __TIME__);
  emulate_printf("%c%c %c%c%c %s\r\n", mos_patchdate[4]==' ' ? '0' : mos_patchdate[4],
  mos_patchdate[5], mos_patchdate[0], mos_patchdate[1], mos_patchdate[2], &mos_patchdate[7]);
  // NB: Adjust spaces in above to align version and date strings correctly
#endif
  show_meminfo();
  emulate_printf("Networking facilities ");
  if (matrixflags.networking == 0) emulate_printf("not ");
  emulate_printf("available\r\n");
  emulate_printf("\r\n");
}

/* =================================================================== */
/* ======= Emulation functions common to all operating systems ======= */
/* =================================================================== */

/*
** 'emulate_mos' provides an emulation of some of the BBC Micro
** MOS calls emulated by the Acorn interpreter
** This is only 32-bit clean, it will not work reliably on 64-bit systems.
*/
static int32 emulate_mos(int32 address) {
  int32 areg, xreg, yreg;
  areg = basicvars.staticvars[A_PERCENT].varentry.varinteger;
  xreg = basicvars.staticvars[X_PERCENT].varentry.varinteger;
  yreg = basicvars.staticvars[Y_PERCENT].varentry.varinteger;
  switch (address) {
  case BBC_OSBYTE:
#ifdef TARGET_RISCOS
    return ((_kernel_osbyte(areg, xreg, yreg) << 8) | areg);
#else
    return mos_osbyte(areg, xreg, yreg, 0);
#endif
    break;
  case BBC_OSWORD:
#ifdef TARGET_RISCOS
    (void) _kernel_osword(areg, (int *)xreg);
#else
    if ((yreg & 0xFF000000) == 0xFF000000) {
      /* Y register borked due to high bit. Let's try to shore it up. */
      yreg = (yreg & 0xFFFFFF) -1;
    }
    if ((xreg & 0xFFFFFF00) == 0xFFFFFF00) {
      /* MOD on a high-bit value sets ALL the bits. So, if MOD 256 used, let's strip them out. */
      xreg &= 0xFF;
    }
    mos_osword(areg, xreg | ( yreg << 8));
#endif
    return areg;
  case BBC_OSWRCH:	/* OSWRCH - Output a character */
    emulate_vdu(areg);
    return areg;
  case BBC_OSRDCH:
    return(kbd_get() & 0xFF);
    break;
  case BBC_OSASCI:
    if (areg != 13) {
      emulate_vdu(areg);
      return(areg);
    }
    /* Deliberate fall-through to OSNEWL */
  case BBC_OSNEWL:
    emulate_printf("\r\n");
    return areg;
  case BBC_OSCLI:
    mos_oscli((char *)(size_t)(xreg | ( yreg << 8)), NIL, NULL);
    break;
  }
  return 0;
}

/*
** 'mos_call' deals with the Basic 'CALL' statement. This is
** unsupported except to provide limited support for the BBC MOS
** calls supported by the Acorn interpreter
*/
void mos_call(int32 address, int32 parmcount, int32 parameters []) {
  if (parmcount==0 && address>=LOW_MOS && address<=HIGH_MOS)
    emulate_mos(address);
  else {
    error(ERR_UNSUPPORTED);
  }
}

/*
** 'mos_usr' deals with the Basic function 'USR'. It
** provides some limited support for the BBC MOS calls
** emulated by USR in the Acorn interpreter (where the
** called address is in the range 0xFFF4 to 0xFFC5)
*/
int32 mos_usr(int32 address) {
  if (address>=LOW_MOS && address<=HIGH_MOS) return emulate_mos(address);
  error(ERR_UNSUPPORTED);
  return 0;
}

/* =================================================================== */
/* ================== RISC OS versions of functions ================== */
/* =================================================================== */

#ifdef TARGET_RISCOS

/* OS_Word and OS_Byte calls used */

#define WRITE_PALETTE 12	/* OS_Word call number to set palette entry */
#define CONTROL_MOUSE 21	/* OS_Word call number to control the mouse pointer */
#define SELECT_MOUSE 106	/* OS_Byte call number to select a mouse pointer */

/* Processor flag bits used in mos_sys() */

#define OVERFLOW_FLAG 1
#define CARRY_FLAG 2
#define ZERO_FLAG 4
#define NEGATIVE_FLAG 8


int64 mos_centiseconds(void) {
  basicvars.centiseconds = clock();
  return basicvars.centiseconds; // (clock() * 100) / CLOCKS_PER_SEC;
}


/*
** 'mos_rdtime' is called to deal with the Basic pseudo-variable
** 'TIME' to return its current value. Under RISC OS, the C function
** 'clock' returns the current value of the centisecond clock, which
** is just what we want
*/
int32 mos_rdtime(void) {
  return clock()-startime;
}

/*
** 'mos_wrtime' handles assignments to the Basic pseudo variable 'TIME'.
** As adjusting 'TIME' is frowned upon the code emulates the effects
** of doing this
*/
void mos_wrtime(int32 time) {
  startime = clock()-time;
}
/*
** 'mos_wrrtc' is called to handle assignments to the Basic
** pseudo variable 'TIME$'. This is used to set the computer's clock.
** It does not seem to be worth the effort of parsing the string, nor
** does it seem worth rejecting the assignment so this code just
** quietly ignores it
*/
void mos_wrrtc(char *time) {
}

/*
** 'mos_mouse_on' turns on the mouse pointer
*/
void mos_mouse_on(int32 pointer) {
  (void) _kernel_osbyte(SELECT_MOUSE, 1, 0);	/* R1 = 1 = select mouse pointer 1 */
}

/*
** 'mos_mouse_off' turns off the mouse pointer
*/
void mos_mouse_off(void) {
  (void) _kernel_osbyte(SELECT_MOUSE, 0, 0);	/* R1 = 0 = do not show the mouse pointer */
}

/*
** 'mos_mouse_to' moves the mouse pointer to position (x,y) on the screen
*/
void mos_mouse_to(int32 x, int32 y) {
  struct {byte call_number, x_lsb, x_msb, y_lsb, y_msb;} osword_parms;
  osword_parms.call_number = 3;		/* OS_Word 21 call 3 sets the mouse position */
  osword_parms.x_lsb = x & BYTEMASK;
  osword_parms.x_msb = x>>BYTESHIFT;
  osword_parms.y_lsb = y & BYTEMASK;
  osword_parms.y_msb = y>>BYTESHIFT;
  (void) _kernel_osword(CONTROL_MOUSE, TOINTADDR(&osword_parms));
}

/*
** 'mos_mouse_step' defines the mouse movement multipliers
*/
void mos_mouse_step(int32 xmult, int32 ymult) {
  struct {byte call_number, xmult, ymult;} osword_parms;
  osword_parms.call_number = 2;		/* OS_Word 21 call 2 defines the mouse movement multipliers */
  osword_parms.xmult = xmult;
  osword_parms.ymult = ymult;
  (void) _kernel_osword(CONTROL_MOUSE, TOINTADDR(&osword_parms));
}

/*
** 'mos_mouse_colour' sets one of the colours used for the mouse pointer
*/
void mos_mouse_colour(int32 colour, int32 red, int32 green, int32 blue) {
  struct {byte ptrcol, mode, red, green, blue;} osword_parms;
  osword_parms.ptrcol = colour;
  osword_parms.mode = 25;	/* Setting mouse pointer colour */
  osword_parms.red = red;
  osword_parms.green = green;
  osword_parms.blue = blue;
  (void) _kernel_osword(WRITE_PALETTE, TOINTADDR(&osword_parms));
}

/*
** 'mos_mouse_rectangle' defines the mouse bounding box
*/
void mos_mouse_rectangle(int32 left, int32 bottom, int32 right, int32 top) {
  struct {
    byte call_number,
    left_lsb, left_msb,
    bottom_lsb, bottom_msb,
    right_lsb, right_msb,
    top_lsb, top_msb;
  } osword_parms;
  osword_parms.call_number = 1;		/* OS_Word 21 call 1 defines the mouse bounding box */
  osword_parms.left_lsb = left & BYTEMASK; 	   /* Why didn't Acorn provide a nice, clean SWI */
  osword_parms.left_msb = left>>BYTESHIFT;	   /* for this call? */
  osword_parms.bottom_lsb = bottom & BYTEMASK;
  osword_parms.bottom_msb = bottom>>BYTESHIFT;
  osword_parms.right_lsb = right & BYTEMASK;
  osword_parms.right_msb = right>>BYTESHIFT;
  osword_parms.top_lsb = top & BYTEMASK;
  osword_parms.top_msb = top>>BYTESHIFT;
  (void) _kernel_osword(CONTROL_MOUSE, TOINTADDR(&osword_parms));
}

/*
** 'mos_mouse' returns the current position of the mouse
*/
void mos_mouse(size_t values[]) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  oserror = _kernel_swi(OS_Mouse, &regs, &regs);
  if (oserror!=NIL) error(ERR_CMDFAIL, oserror->errmess);
  values[0] = regs.r[0];	/* Mouse X coordinate is in R0 */
  values[1] = regs.r[1];	/* Mouse Y coordinate is in R1 */
  values[2] = regs.r[2];	/* Mouse button state is in R2 */
  values[3] = regs.r[3];	/* Timestamp is in R3 */
}

/*
** 'mos_adval' implements the Basic function 'ADVAL' which returns
** either the number of free/waiting entries in buffer 'x' or the
** current value of an input device such as A/D convertor or mouse.
** If x<0 reads buffer status, if x>=0 reads input device.
*/
int32 mos_adval(int32 x) {
//  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = 128;	/* Use OS_Byte 128 for this */
  regs.r[1] = x;
  regs.r[2] = x >> 8;
//  oserror = _kernel_swi(OS_Byte, &regs, &regs);
  _kernel_swi(OS_Byte, &regs, &regs);
// Bug in RISC OS, *mustn't* return an error here
  return regs.r[1]+(regs.r[2]<<BYTESHIFT);
}

/*
** 'mos_sound_on' handles the Basic 'SOUND ON' statement
*/
void mos_sound_on(void) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = 2;	/* Turn on the sound system */
  oserror = _kernel_swi(Sound_Enable, &regs, &regs);
  if (oserror!=NIL) error(ERR_CMDFAIL, oserror->errmess);
}

/*
** 'mos_sound_off' handles the Basic 'SOUND OFF' statement
*/
void mos_sound_off(void) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = 1;	/* Turn off the sound system */
  oserror = _kernel_swi(Sound_Enable, &regs, &regs);
  if (oserror!=NIL) error(ERR_CMDFAIL, oserror->errmess);
}

/*
** 'mos_sound' handles the Basic 'SOUND' statement.
** Note that this code ignores the 'delay' parameter
*/
void mos_sound(int32 channel, int32 amplitude, int32 pitch, int32 duration, int32 delay) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = channel;
  regs.r[1] = amplitude;
  regs.r[2] = pitch;
  regs.r[3] = duration;
  oserror = _kernel_swi(Sound_Enable, &regs, &regs);
  if (oserror!=NIL) error(ERR_CMDFAIL, oserror->errmess);
}

/*
** 'mos_wrbeat' implments the Basic statement 'BEATS'
*/
void mos_wrbeat(int32 barlength) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = barlength;
  oserror = _kernel_swi(Sound_QBeat, &regs, &regs);
  if (oserror!=NIL) error(ERR_CMDFAIL, oserror->errmess);
}

/*
** 'mos_rdbeat' implements the Basic functions 'BEAT', which
** returns the current microbeat.
*/
int32 mos_rdbeat(void) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = 0;
  oserror = _kernel_swi(Sound_QBeat, &regs, &regs);
  if (oserror!=NIL) error(ERR_CMDFAIL, oserror->errmess);
  return regs.r[0];
}

/*
** 'mos_rdbeats' implements the Basic functions 'BEATS', which
** returns the number of microbeats in a bar.
*/
int32 mos_rdbeats(void) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = -1;
  oserror = _kernel_swi(Sound_QBeat, &regs, &regs);
  if (oserror!=NIL) error(ERR_CMDFAIL, oserror->errmess);
  return regs.r[0];
}

/*
** 'mos_wrtempo' implements the Basic statement 'TEMPO'
*/
void mos_wrtempo(int32 x) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = x;
  oserror = _kernel_swi(Sound_QTempo, &regs, &regs);
  if (oserror!=NIL) error(ERR_CMDFAIL, oserror->errmess);
}

/*
** 'mos_rdtempo' emulates the Basic function 'TEMPO'
*/
int32 mos_rdtempo(void) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = 0;
  oserror = _kernel_swi(Sound_QTempo, &regs, &regs);
  if (oserror!=NIL) error(ERR_CMDFAIL, oserror->errmess);
  return regs.r[0];
}

/*
**'mos_voice' attaches the voice named to a voice channel
*/
void mos_voice(int32 channel, char *name) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = channel;
  regs.r[1] = (int)(name);
  oserror = _kernel_swi(Sound_AttachNamedVoice, &regs, &regs);
  if (oserror!=NIL) error(ERR_CMDFAIL, oserror->errmess);
}

/*
** 'mos_voices' handles the Basic statement 'VOICES' which
** is define to set the number of voice channels to be used
*/
void mos_voices(int32 count) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = count;	/* Number of voice channels to usse */
  regs.r[1] = 0;
  regs.r[2] = 0;
  regs.r[3] = 0;
  regs.r[4] = 0;
  oserror = _kernel_swi(Sound_Configure, &regs, &regs);
  if (oserror!=NIL) error(ERR_CMDFAIL, oserror->errmess);
}

void mos_stereo(int32 channel, int32 position) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = channel;
  regs.r[1] = position;
  oserror = _kernel_swi(Sound_Stereo, &regs, &regs);
  if (oserror!=NIL) error(ERR_CMDFAIL, oserror->errmess);
}

/*
** 'read_monotonic' returns the current value of the monotonic
** timer
*/
static int32 read_monotonic(void) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  oserror = _kernel_swi(OS_ReadMonotonicTime, &regs, &regs);
  if (oserror!=NIL) error(ERR_CMDFAIL, oserror->errmess);
  return regs.r[0];	/* Value of timer is returned in R0 */
}

/*
** 'mos_waitdelay' emulate the Basic statement 'WAIT <time>'
*/
void mos_waitdelay(int32 delay) {
  int32 target;
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  if (delay<=0) return;		/* Nothing to do */
  target = read_monotonic()+delay;
/*
** Wait for 'delay' centiseconds. OS_Byte 129 is again misused
** here to make the program pause
*/
  do {
    if (delay>32767) delay = 32767;	/* Maximum time OS_Byte 129 can wait */
    regs.r[0] = 129;
    regs.r[1] = delay & BYTEMASK;
    regs.r[2] = delay>>BYTESHIFT;
    oserror = _kernel_swi(OS_Byte, &regs, &regs);
    if (oserror!=NIL) error(ERR_CMDFAIL, oserror->errmess);
    if (regs.r[2]==ESC || basicvars.escape) break;	/* Escape was pressed - Escape from wait */
    delay = target-read_monotonic();
  } while (delay>0);
}

/*
** 'mos_setend' emulates the 'END=' form of the 'END' statement. This
** can be used to extend the Basic workspace to. It is not supported by
** this version of the interpreter
*/
void mos_setend(int32 newend) {
  error(ERR_UNSUPPORTED);
}

/*
** This is the RISC OS-specific version of 'oscli' that uses the low
** level library functions to issue the command passed to it in order
** to better trap any errors. 'respfile' is set to NIL if the command
** output is to displayed in the normal way. If it is not NIL, the
** command output is redirected to it.
** Note that it is assumed that the buffer holding the command
** string is large enough to have the redirection commands
** appended to it. (basicvars.stringwork is used for this and so
** it should not be a problem unless the size of stringwork is
** drastically reduced.)
*/
void mos_oscli(char *command, char *respfile, FILE *respfh) {
  if (respfile==NIL) {	/* Command output goes to normal place */
    if (!strcasecmp(command, "brandyinfo")) {
      cmd_brandyinfo();
    } else if ( (!strncasecmp(command, "refresh", 7)) ||
                (!strncasecmp(command, "fullscreen", 10)) ||
                (!strncasecmp(command, "wintitle", 8)) ) {
      /* Do nothing - Matrix Brandy specific commands for other platforms */
    } else {
      basicvars.retcode = _kernel_oscli(command);
      if (basicvars.retcode<0) error(ERR_CMDFAIL, _kernel_last_oserror()->errmess);
    }
  }
  else {	/* Want response back from command */
    strcat(command, "{ > ");
    strcat(command, respfile);
    strcat(command, " }");
    basicvars.retcode = _kernel_oscli(command);
    if (basicvars.retcode<0) {	/* Call failed */
      remove(respfile);
      error(ERR_CMDFAIL, _kernel_last_oserror()->errmess);
    }
  }
}

/* This gets the virtual SWI num of Brandy-specific calls that are only valid within Matrix Brandy */
static int32 mos_getswinum2(char *name, int32 length, int32 inxflag) {
  int32 ptr;
  int32 xflag=0;
  char namebuffer[128];

  if (name[0] == 'X') {
    name++;
    length--;
    xflag=0x20000;
  }
  for (ptr=0; swilist[ptr].swinum!=0xFFFFFFFF; ptr++) {
    if ((!strncmp(name, swilist[ptr].swiname, length)) && length==strlen(swilist[ptr].swiname)) break;
  }
  strncpy(namebuffer,name, length);
  namebuffer[length]='\0';
  if (swilist[ptr].swinum==0xFFFFFFFF) {
    if (inxflag != XBIT) error(ERR_SWINAMENOTKNOWN, namebuffer);
    return (-1);
  } else {
    return ((swilist[ptr].swinum)+xflag);
  }
}


/*
** 'mos_getswinum' returns the SWI number corresponding to
** SWI 'name'
*/
size_t mos_getswinum(char *name, int32 length, int32 inxflag) {
  int32 ilength = length;
  char *iname = name;
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  char swiname[100];

  if (length==0) length = strlen(name);
  memmove(swiname, name, length);
  swiname[length] = NUL;		/* Ensure name is null-terminated */
  regs.r[1] = (int)(&swiname[0]);
  oserror = _kernel_swi(OS_SWINumberFromString, &regs, &regs);
  if (oserror!=NIL) return mos_getswinum2(iname, ilength, inxflag);
  return regs.r[0];
}

/*
** 'mos_sys' issues a SWI call and returns the result.
** The code fakes the processor flags returned in 'flags'. The carry
** flag is returned by the call to _kernel_swi_c(). The state of the
** overflow bit can be determined by checking if the swi function
** returns null. The Acorn interpreter always seems to set the zero
** flag and the sign flag can probably be ignored.
** The layout of the flags bits are:
**  N (sign) Z (zero) C (carry) V (overflow)
*/
void mos_sys(size_t swino, sysparm inregs[], size_t outregs[], size_t *flags) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  int n;
  if ((swino & ~XBIT) == 57) { /* OS_SWINumberFromString - call our local version */
    outregs[1]=inregs[1].i;
    for(n=0;*(char *)(inregs[1].i+n) >=32; n++) ;
    *(char *)(inregs[1].i+n)='\0';
    outregs[0]=mos_getswinum((char *)inregs[1].i, strlen((char *)inregs[1].i), (swino & XBIT));
  } else if (swino >= 0x140000) {
    /* Brandy-specific virtual SYS calls */
    mos_sys_ext(swino & ~XBIT, inregs, outregs, swino & XBIT, flags);
  } else {
    for (n=0; n<10; n++) regs.r[n] = inregs[n].i;
    oserror = _kernel_swi_c(swino, &regs, &regs, (int *)flags);
    if (oserror!=NIL && (swino & XBIT)==0) error(ERR_CMDFAIL, oserror->errmess);
    *flags = *flags!=0 ? CARRY_FLAG : 0;
    if (oserror!=NIL) *flags+=OVERFLOW_FLAG;
    for (n=0; n<10; n++) outregs[n] = regs.r[n];
  }
}

/*
** mos_init - Called to initialise the emulation code
*/
boolean mos_init(void) {
  startime = 0;
  return TRUE;
}

void mos_final(void) {
}

#else /* not TARGET_RISCOS */

/* ====================================================================== *
 * ================== Non-RISC OS versions of functions ================= *
 * ====================================================================== */

#if (defined(TARGET_WIN32) | defined(TARGET_AMIGA)) && !defined(TARGET_MINGW)

int64 mos_centiseconds(void) {
  basicvars.centiseconds = (clock() * 100) / CLOCKS_PER_SEC
  return basicvars.centiseconds;
}


/*
** 'mos_rdtime' is called to deal with the Basic pseudo-variable
** 'TIME' to return its current value. This should be the current
** value of the centisecond clock, but how accurate the value is
** depends on the underlying OS.
*/
int32 mos_rdtime(void) {
  return (clock() - startime) * 100 / CLOCKS_PER_SEC;
}

/*
** 'mos_wrtime' handles assignments to the Basic pseudo variable 'TIME'.
** The effects of 'TIME=' are emulated here
*/
void mos_wrtime(int32 time) {
  startime = clock() -(time * CLOCKS_PER_SEC / 100);
}

#else
/*
** 'mos_rdtime' is called to deal with the Basic pseudo-variable
** 'TIME' to return its current value. This should be the current
** value of the centisecond clock, but how accurate the value is
** depends on the underlying OS.
** This code was supplied by Jeff Doggett, and modified
** by Michael McConnell
** Further modified by moving code into brandy.c and running in a
** separate thread that updates basicvars.centiseconds.
*/

int64 mos_centiseconds(void) {
  return basicvars.centiseconds;
}

int32 mos_rdtime(void) {
  return ((int32) (basicvars.centiseconds - startime));
}

/*
** 'mos_wrtime' handles assignments to the Basic pseudo variable 'TIME'.
** The effects of 'TIME=' are emulated here
*/
void mos_wrtime (int32 time) {
  startime = (basicvars.centiseconds - time);
}

#endif

/* OSWORD call to read the centisecond clock */
static void osword01(int64 x) {
  int32 *block;

  block=(int32 *)(size_t)x;
  *block=mos_rdtime();
}

/* OSWORD call to write the centisecond clock */
static void osword02(int64 x) {
  int32 *block;

  block=(int32 *)(size_t)x;
  mos_wrtime(*block);
}

/*
** 'mos_wrrtc' is called to handle assignments to the Basic
** pseudo variable 'TIME$'. This is used to set the computer's clock.
** It does not seem to be worth the effort of parsing the string, nor
** does it seem worth rejecting the assignment so this code just
** quietly ignores it
*/
void mos_wrrtc(char *time) {
}

/*
** 'mos_mouse_on' turns on the mouse pointer
*/
void mos_mouse_on(int32 pointer) {
#ifdef USE_SDL
  sdl_mouse_onoff(1);
  return;
#else
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
#endif
}

/*
** 'mos_mouse_off' turns off the mouse pointer
*/
void mos_mouse_off(void) {
#ifdef USE_SDL
  sdl_mouse_onoff(0);
  return;
#else
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
#endif
}

/*
** 'mos_mouse_to' moves the mouse pointer to (x,y) on the
** screen
*/
void mos_mouse_to(int32 x, int32 y) {
#ifdef USE_SDL
  warp_sdlmouse(x,y);
  return;
#else
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
#endif
}

/*
** 'mos_mouse_step' changes the number of graphics units moved
** per step of the mouse to 'x' and 'y'.
*/
void mos_mouse_step(int32 x, int32 y) {
#ifdef USE_SDL
  return; // Do nothing, silently.
#else
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
#endif
}

/*
** 'mos_mouse_colour' sets colour 'colour' of the mouse sprite
** to the specified values 'red', 'green' and 'blue'
*/
void mos_mouse_colour(int32 colour, int32 red, int32 green, int32 blue) {
#ifdef USE_SDL
  return; // Do nothing, silently.
#else
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
#endif
}

/*
** 'mos_mouse_rectangle' restricts the mouse pointer to move
** in the rectangle defined by (left, bottom) and (right, top).
*/
void mos_mouse_rectangle(int32 left, int32 bottom, int32 right, int32 top) {
#ifdef USE_SDL
  return; // Do nothing, silently.
#else
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
#endif
}

/*
** 'mos_mouse' emulates the Basic 'MOUSE' statement
*/
void mos_mouse(size_t values[]) {
#ifdef USE_SDL
  get_sdl_mouse(values);
#else
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
#endif
}

/*
** 'mos_adval' emulates the Basic function 'ADVAL' which
** either returns the number of entries free in buffer 'x' or
** the current value of a device, eg A/D convertor, mouse, etc.
**
** Positive parameter - read device
** Negative parameter - read buffer
** 128-buffer         - low-level raw read of buffer (extension)
**
** 0 b0-b7=buttons, b8-b15=last converted channel
** 1 ADC 1 - Device 1 X position
** 2 ADC 2 - Device 1 Y position
** 3 ADC 3 - Device 2 X position
** 4 ADC 4 - Device 2 Y position
** 5 Mouse X boundary
** 6 Mouse Y boundary
** 7 Mouse X position
** 8 Mouse Y position
** 9 Mouse button state %xxxxRML
** 10+ other devices
**
** -1  Keyboard buffer		127  Low-level examine keyboard buffer
** -2  Serial input buffer	126
** -3  Serial output buffer	125
** -4  Printer output buffer	124
** -5  Sound output buffer 0	123
** -6  Sound output buffer 1	122
** -7  Sound output buffer 2	121
** -8  Sound output buffer 3	120
** -9  Speach output buffer	119
** -10 Mouse input buffer	118
** -11 MIDI input buffer	117
** -12 MIDI output buffer	116
** -12- other buffers		etc
*/
int32 mos_adval(int32 x) {
  size_t inputvalues[4]={0,0,0,0}; /* Initialise to zero to keep non-SDL builds happy */

  x = x & 0xFFFF;				/* arg is a 16-bit value		*/
  if((x>6) & (x<10)) {
    mos_mouse(inputvalues);
    return inputvalues[x-7];
  }
  if (x == 0x007F) return kbd_get0();		/* Low-level examine of keyboard buffer	*/
  if (x == 0xFFFF) return kbd_buffered();	/* ADVAL(-1), amount in keyboard buffer	*/

  return 0;
}

/*
** 'mos_sound_on' handles the Basic 'SOUND ON' statement
*/
void mos_sound_on(void) {
#ifdef USE_SDL
 sdl_sound_onoff(1);
#else
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
#endif
}

/*
** 'mos_sound_off' handles the Basic 'SOUND OFF' statement
*/
void mos_sound_off(void) {
#ifdef USE_SDL
 sdl_sound_onoff(0);
#else
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
#endif
}

/*
** 'mos_sound' handles the Basic 'SOUND' statement
*/
void mos_sound(int32 channel, int32 amplitude, int32 pitch, int32 duration, int32 delay) {
#ifdef USE_SDL
  sdl_sound(channel, amplitude, pitch, duration, delay);
#else
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
#endif
}

/*
** 'mos_wrbeat' emulates the Basic statement 'BEATS'
*/
void mos_wrbeat(int32 x) {
#ifdef USE_SDL
  sdl_wrbeat(x);
#else
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
#endif
}

/*
** 'mos_rdbeat' emulates the Basic functions 'BEAT' which
** returns the current microbeat
*/
int32 mos_rdbeat(void) {
#ifdef USE_SDL
 return sdl_rdbeat();
#else
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
  return 0;
#endif
}

/*
** 'mos_rdbeats' emulates the Basic functions 'BEATS', which
** returns the number of microbeats in a bar
*/
int32 mos_rdbeats(void) {
#ifdef USE_SDL
 return sdl_rdbeats();
#else
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
  return 0;
#endif
}

/*
** 'mos_wrtempo' emulates the Basic statement version of 'TEMPO'
*/
void mos_wrtempo(int32 x) {
#ifdef USE_SDL
  sdl_wrtempo(x);
#else
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
#endif
}

/*
** 'mos_rdtempo' emulates the Basic function version of 'TEMPO'
*/
int32 mos_rdtempo(void) {
#ifdef USE_SDL
  return sdl_rdtempo();
#else
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
  return 0;
#endif
}

/*
** 'mos_voice' emulates the Basic statement 'VOICE'
*/
void mos_voice(int32 channel, char *name) {
#ifdef USE_SDL
  sdl_voice(channel, name);
#else
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
#endif
}

/*
** 'mos_voices' emulates the Basic statement 'VOICES'
*/
void mos_voices(int32 count) {
#ifdef USE_SDL
  sdl_voices(count);
#else
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
#endif
}

/*
** 'mos_stereo' emulates the Basic statement 'STEREO'
*/
void mos_stereo(int32 channels, int32 position) {
#ifdef USE_SDL
  sdl_stereo(channels, position);
#else
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
#endif
}

/*
** 'mos_setend' emulates the 'END=' form of the 'END' statement. This
** can be used to extend the Basic workspace to. It is not supported by
** this version of the interpreter
*/
void mos_setend(int32 newend) {
  error(ERR_UNSUPPORTED);
}

#if defined(TARGET_DJGPP)
/*
** 'mos_waitdelay' emulate the Basic statement 'WAIT <time>'
** 'time' is the time to wait in centiseconds.
** Note that the actual delay is about 20% longer than the
** time specified.
*/
void mos_waitdelay(int32 time) {
  if (time <= 0) return		/* Nothing to do */
  delay(time * 10);		/* delay() takes the time in ms */
}

#elif defined(TARGET_WIN32)

/*
** 'mos_waitdelay' emulate the Basic statement 'WAIT <time>'
** This is not supported under Windows.
*/
void mos_waitdelay(int32 time) {
  error(ERR_UNSUPPORTED);	/* Not supported under windows */
}

#elif defined(TARGET_AMIGA)

/*
** 'mos_waitdelay' emulate the Basic statement 'WAIT <time>'
** 'time' is the time to wait in centiseconds.
*/
void mos_waitdelay(int32 time) {
  if (time<=0) return;		/* Nothing to do */
  Delay(time*2);		/* Delay() takes the time in 1/50 s */
}

#else /* not DJGPP, WIN32 or AMIGA */

/*
** 'mos_waitdelay' emulate the Basic statement 'WAIT <time>'
** 'time' is the time to wait in centiseconds.
*/
void mos_waitdelay(int32 time) {
  int64 tbase;

  if (time<=0) return;			/* Nothing to do */
  tbase=mos_centiseconds();
  while(mos_centiseconds() < tbase + time) {
#ifdef USE_SDL
    if (basicvars.escape) {
      time=0;
      error(ERR_ESCAPE);
    }
#endif /* USE_SDL */
    usleep(2000);
  }
}
#endif /* DJGPP, WIN32 or AMIGA cascade */
#endif /* ! TARGET_RISCOS */


/* ======================================
 * === *commands and *command parsing ===
 * ======================================
 */

#ifndef TARGET_RISCOS
/*
 * mos_gstrans() - General String Translation
 * GSTrans convert input string, as used by *KEY, etc.
 * On entry: pointer to string to convert
 * On exit:  pointer to converted string, overwriting input string
 *           recognises |<letter> |" || !? |!
 */
static char *mos_gstrans(char *instring, unsigned int *len) {
	int quoted=0, escape=0;
	char *result, *outstring;
	char ch;

	result=outstring=instring;
	while (*instring == ' ') instring++;		// Skip spaces
	if ((quoted = (*instring == '"'))) instring++;

	while (*instring) {
		ch = *instring++;
		if (ch == '"' && *instring != '"' && quoted)
			break;
		if ((ch == (char)124 || ch == (char)221) && *instring == '!') {
			instring++;
			escape=128;
			ch=*instring++;
		}
		if ((ch == (char)124 || ch == (char)221)) {
			if (*instring == (char)124 || *instring == (char)221) {
                        	instring++;
				ch = (char)124;
			} else {
				if (*instring == '"' || *instring == '?' || *instring >= '@') {
					ch = *instring++ ^ 64;
					if (ch < 64 ) ch = ch & 31; else if (ch == 98) ch = 34;
				}
			}
		}
		*outstring++=ch | escape;
		escape=0;
	}
	*outstring=0;
	if (quoted && *(instring-1) != '"')
		error(ERR_BADSTRING);
	*len=outstring-result;
	return result;
}

/*
 * cmd_parse_dec()
 * Parse 8-bit decimal number
 * On entry: address of pointer to command line
 * On exit:  pointer to command line updated
 *           returns decimal number
 *           leading and trailing spaces skipped
 *           error if no number or number>255
 */
static unsigned int cmd_parse_dec(char** text)
{
	unsigned int ByteVal;
	char *command;

	command=*text;
	while (*command == ' ') command++;	// Skip spaces
	if (*command < '0' || *command > '9')
		error(ERR_BADNUMBER);
	ByteVal = (*command++) - '0';
	while (*command >= '0' && *command <='9') {
		ByteVal=ByteVal*10 + (*command++) - '0';
		if (ByteVal > 255)
			error(ERR_BADNUMBER);
	}
	while (*command == ' ') command++;	// Skip spaces
	*text=command;
	return ByteVal;
}
#endif /* !TARGET_RISCOS */

#ifdef USE_SDL /* This code doesn't depend on SDL, but it's currently only called by code that does depend on it */
static unsigned int cmd_parse_num(char** text)
{
	unsigned int ByteVal;
	char *command;

	command=*text;
	while (*command == ' ') command++;	// Skip spaces
	if (*command < '0' || *command > '9')
		error(ERR_BADNUMBER);
	ByteVal = (*command++) - '0';
	while (*command >= '0' && *command <='9') {
		ByteVal=ByteVal*10 + (*command++) - '0';
	}
	while (*command == ' ') command++;	// Skip spaces
	*text=command;
	return ByteVal;
}
#endif

#ifndef TARGET_RISCOS
/*
 * List of *commands implemented by this code
 */
#define CMD_UNKNOWN       0
#define CMD_KEY           1
#define CMD_CAT           2
#define CMD_EX            3
#define CMD_CD            4
#define CMD_QUIT          5
#define CMD_WINDOW        6
#define CMD_FX            7
#define CMD_VER           8
#define CMD_TITLE         9
#define CMD_HELP          10
#define CMD_WINTITLE      11
#define CMD_FULLSCREEN    12
#define CMD_NEWMODE       13
#define CMD_REFRESH       14
#define CMD_SCREENSAVE    15
#define CMD_SCREENLOAD    16
#define CMD_SHOW          17
#define CMD_EXEC          18
#define CMD_SPOOL         19
#define CMD_SPOOLON       20
#define CMD_LOAD          27
#define CMD_SAVE          28
#define CMD_VOLUME        29
#define CMD_CHANNELVOICE  30
#define CMD_VOICES        31
#define CMD_POINTER       32
#define CMD_BRANDYINFO    33
#define HELP_BASIC        1024
#define HELP_HOST         1025
#define HELP_MOS          1026
#define HELP_MATRIX       1027
#define HELP_SOUND        1028
#define HELP_MEMINFO      1029

#define CMDTABSIZE 256

typedef struct cmdtabent { char *name; uint32 hash,value; } cmdtabent;

cmdtabent *cmdtab = (cmdtabent*)0;

int hash_name(char *name){
  int h=0;
  int ch;

 while((ch=*name++)>32){
  h = (h<<1)+(ch&31);
 }
 h += (h>>14);
 h = (h<<2)+(h>>6);
 if(h==0) return (CMDTABSIZE-1);
 return h;
}

void add_cmd(char *name, int value){
 int i,h;
 int j;

 h=hash_name(name);
 i=(h & (CMDTABSIZE-1));
 j=i;

  // fprintf(stderr," name %s hash %d index %d\n",name,h,i);

 while(cmdtab[i].name != (char*)0){
   i=((i+1)&(CMDTABSIZE-1));
   if(i==j){
     fprintf(stderr,"add_cmd: command table is full\n");
     return;
   }
 }

 cmdtab[i].name  = name;
 cmdtab[i].hash  = (unsigned)h;
 cmdtab[i].value = (unsigned)value;

 // fprintf(stderr, "add_cmd name \"%s\"\n hash %10u entry %4d (+%d) value %4d\n",name,(unsigned)h,i,i-j,value);
}

int get_cmdvalue(char *name){
 int i,j,h;
 h=hash_name(name);

 //fprintf(stderr,"GET_cmdvalue 1 name %s name %s entry %d \n",name,cmdtab[i].name != (char*)0 ? cmdtab[i].name : "NULL",i);

 i=(h &(CMDTABSIZE-1));
 j=i;

 for(;;)
 {
  if(cmdtab[i].name == (char*)0 ) return CMD_UNKNOWN;
  if(cmdtab[i].hash == h && (strcmp(name,cmdtab[i].name)==0)){
    return cmdtab[i].value;
  }
  i=((i+1)&(CMDTABSIZE-1));
  if(i==j) return CMD_UNKNOWN; /* we are back where we started and haven't found it */
 }

}

void make_cmdtab(){
  if(cmdtab != (cmdtabent*)0) free(cmdtab);

  cmdtab=calloc(CMDTABSIZE,sizeof(cmdtabent));

  add_cmd( "key",          CMD_KEY  );
  add_cmd( "cat",          CMD_CAT  );
  add_cmd( "cd",           CMD_CD   );
  add_cmd( "chdir",        CMD_CD   );
  add_cmd( "ex",           CMD_EX   );
  add_cmd( "quit",         CMD_QUIT );
  add_cmd( "fx",           CMD_FX   );
  add_cmd( "help",         CMD_HELP );
  add_cmd( "ver",          CMD_VER  );
  add_cmd( "screensave",   CMD_SCREENSAVE );
  add_cmd( "screenload",   CMD_SCREENLOAD );
  add_cmd( "wintitle",     CMD_WINTITLE   );
  add_cmd( "fullscreen",   CMD_FULLSCREEN );
  add_cmd( "newmode",      CMD_NEWMODE );
  add_cmd( "refresh",      CMD_REFRESH );
  add_cmd( "show",         CMD_SHOW    );
  add_cmd( "exec",         CMD_EXEC    );
  add_cmd( "spool",        CMD_SPOOL   );
  add_cmd( "spoolon",      CMD_SPOOLON );
  add_cmd( "load",         CMD_LOAD );
  add_cmd( "save",         CMD_SAVE );
  add_cmd( "brandyinfo",   CMD_BRANDYINFO );
#ifdef USE_SDL
  add_cmd( "volume",       CMD_VOLUME  );
  add_cmd( "channelvoice", CMD_CHANNELVOICE );
  add_cmd( "voices",       CMD_VOICES  );
  add_cmd( "pointer",      CMD_POINTER );
  add_cmd( "sound",        HELP_SOUND  );
#endif
  add_cmd( "basic",        HELP_BASIC   );
  add_cmd( "host",         HELP_HOST    );
  add_cmd( "mos",          HELP_MOS     );
  add_cmd( "matrix",       HELP_MATRIX  );
  add_cmd( "meminfo",      HELP_MEMINFO );
  add_cmd( "os",           HELP_MOS     );
}

/* Strip quotes around a file name */
static void strip_quotes(char *buffer) {
  if (buffer[0] == '"') {
    memmove(buffer,buffer+1, strlen(buffer));
    buffer[strlen(buffer)-1]='\0';
  }
}

/*
 * *.(<directory>) or *cat (<directory>)
 * Catalogue directory
 * Internal implementation, slightly styled on BBC Micro output, with
 * column spacing to work in any 20/40/80 screen mode.
 */
static void cmd_cat(char *command) {
  char dbuf[FILENAME_MAX+1];
  char fbuf[FILENAME_MAX+1];
  int buflen=0, loop=0;
  struct dirent *entry;
  DIR *dirp;
  struct stat statbuf;

  memset(dbuf,0,FILENAME_MAX+1);
  memset(fbuf,0,FILENAME_MAX+1);
  getcwd(dbuf, FILENAME_MAX);
  buflen=FILENAME_MAX - strlen(dbuf);
  while (*command && (*command != ' ')) command++;	// Skip command
	while (*command && (*command == ' ')) command++;	// Skip spaces
  if (strlen(command)) {
#ifdef TARGET_MINGW
    if ((*(command+1) == ':') && ((*(command+2) == '\\') || (*(command+2) == '/'))) {
#else
    if (*command == '/') {
#endif
      strncpy(dbuf, command, buflen);
    } else {
      buflen--;
      strncat(dbuf, "/", buflen);
      strncat(dbuf, command, buflen-strlen(command));
    }
  }
  emulate_printf("Dir. %s\r\n", dbuf);
  dirp=opendir(dbuf);
  if (!dirp) error(ERR_DIRNOTFOUND);
  else {
    emulate_printf("\r\n", dbuf);
    while ((entry = readdir(dirp)) != NULL) {
      if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
      strncpy(fbuf, dbuf, FILENAME_MAX+1);
      strncat(fbuf, "/", 2);
      strncat(fbuf, entry->d_name, FILENAME_MAX - strlen(dbuf));
      stat(fbuf, &statbuf);
      emulate_printf("%s", entry->d_name);
      loop=(1000 - (5+strlen(entry->d_name))) % 20;
      while(loop--) emulate_printf(" ");
      emulate_printf("/");
      if ((statbuf.st_mode & S_IFMT) == S_IFDIR) emulate_printf("D");
      if (!access(fbuf, W_OK))
        emulate_printf("W");
      else
        emulate_printf("L");
      if (!access(fbuf, R_OK))
        emulate_printf("R ");
      else
        emulate_printf("  ");
      if ((statbuf.st_mode & S_IFMT) != S_IFDIR) emulate_printf(" ");
    }
  }
  emulate_printf("\r\n");
}

/* Sets the window title. */
static void cmd_wintitle(char *command) {
  while (*command == ' ') command++;	// Skip spaces
  if (strlen(command) == 0)
    emulate_printf("Syntax: WinTitle <window title>\r\n");	// This should be an error
  else
    set_wintitle(command);
  return;
}

static void cmd_ex(char *command) {
	while (*command == ' ') command++;	// Skip spaces
#if defined(TARGET_DOSWIN)
	native_oscli("dir", NIL, NULL);
#elif defined(TARGET_MACOSX) | defined(TARGET_UNIX)
	native_oscli("ls -l", NIL, NULL);
	// native_oscli("ls -C", NIL, NULL);
#elif defined(TARGET_AMIGA)
	native_oscli("list", NIL, NULL);
#endif

}

static void cmd_fullscreen(char *command) {
#ifdef USE_SDL
  int flag=3;
  while (*command == ' ') command++;	// Skip spaces
  if (strlen(command) == 0) flag=2;
  if (strcmp(command, "1" ) == 0) flag=1;
  if (strcmp(command, "0" ) == 0) flag=0;
  if (strcasecmp(command, "on" ) == 0) flag=1;
  if (strcasecmp(command, "off" ) == 0) flag=0;
  if (flag != 3) {
    tmsg.modechange = 0x400 + (flag);
    while (tmsg.modechange >= 0) usleep(1000);
  } else emulate_printf("Syntax: FullScreen [<ON|OFF|1|0>]\r\nWith no parameter, this command toggles the current setting.\r\n");
#endif
  return;
}

static void cmd_screensave(char *command) {
#ifdef USE_SDL
  while (*command == ' ') command++;	// Skip spaces
  if (strlen(command) == 0) {
    emulate_printf("Syntax: ScreenSave <filename.bmp>\r\n");	// This should be an error
  } else {
    sdl_screensave(command);
  }
#else
  error(ERR_BADCOMMAND);
#endif
  return;
}

static void cmd_screenload(char *command) {
#ifdef USE_SDL
  while (*command == ' ') command++;	// Skip spaces
  if (strlen(command) == 0) {
    emulate_printf("Syntax: ScreenLoad <filename.bmp>\r\n");	// This should be an error
  } else {
    sdl_screenload(command);
  }
#else
  error(ERR_BADCOMMAND);
#endif
  return;
}

#ifdef USE_SDL
static void cmd_newmode_err() {
  emulate_printf("Syntax:\r\n  NewMode <mode> <xres> <yres> <colours> <xscale> <yscale> [<xeig> [<yeig>]]\r\nMode must be between 64 and 126, and colours must be one of 2, 4, 16, 256 or\r\n16777216.\r\nEigen factors must be in the range 0-3, default 1. yeig=xeig if omitted.\r\nExample: *NewMode 80 640 256 2 1 2 recreates MODE 0 as MODE 80.\r\n");
  return;							// This should be an error
}
#endif

static void cmd_newmode(char *command) {
#ifdef USE_SDL
  int mode, xres, yres, cols, xscale, yscale, xeig, yeig;

  while (*command == ' ') command++;	// Skip spaces
  if (strlen(command) == 0) {
    cmd_newmode_err();
  } else {
    mode=cmd_parse_dec(&command);
    if (*command == ',') command++;			// Step past any comma
    while (*command == ' ') command++;			// Skip spaces
    if (!*command) {cmd_newmode_err(); return;}
    xres=cmd_parse_num(&command);
    if (*command == ',') command++;			// Step past any comma
    while (*command == ' ') command++;			// Skip spaces
    if (!*command) {cmd_newmode_err(); return;}
    yres=cmd_parse_num(&command);
    if (*command == ',') command++;			// Step past any comma
    while (*command == ' ') command++;			// Skip spaces
    if (!*command) {cmd_newmode_err(); return;}
    cols=cmd_parse_num(&command);
    if (*command == ',') command++;			// Step past any comma
    while (*command == ' ') command++;			// Skip spaces
    if (!*command) {cmd_newmode_err(); return;}
    xscale=cmd_parse_dec(&command);
    if (*command == ',') command++;			// Step past any comma
    while (*command == ' ') command++;			// Skip spaces
    if (!*command) {cmd_newmode_err(); return;}
    yscale=cmd_parse_dec(&command);
    if (*command == ',') command++;			// Step past any comma
    if (!*command) xeig=yeig=1;
    else {
      xeig=cmd_parse_dec(&command);
      if (*command == ',') command++;			// Step past any comma
      if (!*command) yeig=xeig;
      else yeig=cmd_parse_dec(&command);
    }
    if((xeig > 3) || (yeig > 3)) cmd_newmode_err();
    setupnewmode(mode, xres, yres, cols, xscale, yscale, xeig, yeig);
  }
#endif
  return;
}

static void cmd_refresh(char *command) {
#ifdef USE_SDL
  int flag=3;

  while (*command == ' ') command++;	// Skip spaces
  if (strlen(command) == 0) {
    star_refresh(3);
  } else {
    if (strcasecmp(command, "onerror") == 0) {
      flag=2;
    } else if (strcasecmp(command, "on") == 0) {
      flag=1;
    } else if (strcasecmp(command, "off") == 0) {
      flag=0;
    }
    if (flag == 3) {
      emulate_printf("Syntax: Refresh [<On|Off|OnError>]\r\n");	// This should be an error
      return;
    }
    star_refresh(flag);
  }
#endif
  return;
}

/*
 * *CD / *CHDIR <directory>
 * Change directory
 * Has to be an internal command as CWD is per-process
 */
static void cmd_cd(char *command) {
  int err=0;

  if (*command == 'd' || *command == 'D') command +=3;	// *CHDIR
  while (*command == ' ') command++;			// Skip spaces
  err=chdir(command);
#if defined(TARGET_DOSWIN)
  find_cursor();				// Figure out where the cursor has gone to
#if defined(TARGET_MINGW)
#ifndef USE_SDL
  emulate_printf("\r\n");			// Restore cursor position
#endif
#endif
#endif
  if (err) error(ERR_DIRNOTFOUND);
}

/*
 * *FX num(,num(,num))
 * Make OSBYTE call
 */
static void cmd_fx(char *command) {
  // *FX *must* purely and simply parse its parameters and pass them to OSBYTE
  // *FX *MUST* *NOT* impose any preconceptions
  // *FX is allowed to test the returned 'unsupported' flag and give a Bad command error
  // OSBYTE *MUST* *NOT* give a Bad command error, it *MUST* purely and simply just return if unsupported
  // Yes, RISC OS gets this wrong.

  unsigned int areg=0, xreg=0, yreg=0;		// Default parameters

  while (*command == ' ') command++;		// Skip spaces
  if (*command == 0) {
    error(ERR_BADSYNTAX, "FX <num> (,<num> (,<num>))");
    return;
  }
  // Not sure why this call to error() needs a return after it, doesn't need it elsewhere
  areg=cmd_parse_dec(&command);			// Get first parameter
  if (*command == ',') command++;		// Step past any comma
  while (*command == ' ') command++;		// Skip spaces
  if (*command) {
    xreg=cmd_parse_dec(&command);		// Get second parameter
    if (*command == ',') command++;		// Step past any comma
    while (*command == ' ') command++;		// Skip spaces
    if (*command) {
      yreg=cmd_parse_dec(&command);		// Get third parameter
    }
  }

  if (mos_osbyte(areg, xreg, yreg, 0) & 0x80000000) error(ERR_BADCOMMAND);
}

/*
 * *HELP - display help on topic
 */
static void cmd_help(char *command)
{
  int cmd;

  while (*command == ' ') command++;		// Skip spaces
  cmd = check_command(command);
#ifdef TARGET_MINGW
  find_cursor();
#endif
  emulate_printf("\r\n%s\r\n", IDSTRING);
  switch (cmd) {
    case HELP_BASIC:
// Need to think about making this neat but informative
#ifdef BRANDY_GITCOMMIT
      emulate_printf("  Git commit %s on branch %s (%s)\r\n", BRANDY_GITCOMMIT, BRANDY_GITBRANCH, BRANDY_GITDATE);
#endif
      // Try to get attributions correct, as per license.
      emulate_printf("  Forked from Brandy Basic v1.20.1 (24 Sep 2014)\r\n");
      emulate_printf("  Merged Banana Brandy Basic v0.02 (05 Apr 2014)\r\n");
#ifdef BRANDY_PATCHDATE
      emulate_printf("  Patch %s compiled at %s on ", BRANDY_PATCHDATE, __TIME__);
      emulate_printf("%c%c %c%c%c %s\r\n", mos_patchdate[4]==' ' ? '0' : mos_patchdate[4],
      mos_patchdate[5], mos_patchdate[0], mos_patchdate[1], mos_patchdate[2], &mos_patchdate[7]);
      // NB: Adjust spaces in above to align version and date strings correctly
#endif
      break;
    case HELP_HOST:
    case HELP_MOS:
      emulate_printf("  CD      <dir>\r\n");
      emulate_printf("  EXEC    (<filename>)\r\n");
      emulate_printf("  FX      <num>(,<num>(,<num>))\r\n");
      emulate_printf("  HELP    (<text>)\r\n");
      emulate_printf("  KEY     <num> <string>\r\n");
      emulate_printf("  LOAD    <filename> <load addr>\r\n");
      emulate_printf("  POINTER (<0|1>)\r\n");
      emulate_printf("  QUIT\r\n");
      emulate_printf("  SAVE    <filename> <start addr> <end addr>|+<length>\r\n");
      emulate_printf("  SHOW    (<num>)\r\n");
      emulate_printf("  SPOOL   (<filename>)\r\n");
      emulate_printf("  SPOOLON (<filename>)\r\n");
      break;
    case HELP_MATRIX:

#ifdef USE_SDL
      emulate_printf("  FullScreen (<ON|OFF|1|0>)\r\n");
      emulate_printf("  NewMode    <mode> <xres> <yres> <colours> <xscale> <yscale> (<xeig> (<yeig>))\r\n");
      emulate_printf("  Refresh    (<On|Off|OnError>)\r\n");
      emulate_printf("  ScreenLoad <filename.bmp>\r\n");
      emulate_printf("  ScreenSave <filename.bmp>\r\n");
#endif /* USE_SDL */
      emulate_printf("  WinTitle   <window title>\r\n");
      break;
    case HELP_MEMINFO:
      show_meminfo();
      emulate_printf("\r\n");
    break;
#ifdef USE_SDL
    case HELP_SOUND:
      emulate_printf("  ChannelVoice <channel> <voice index|voice name>\r\n");
      emulate_printf("  Voices\r\n");
      emulate_printf("  Volume       <n>\r\n");
      break;
#endif
    case CMD_WINTITLE:
      emulate_printf("Syntax: *WinTitle <window title>\r\n");
      emulate_printf("  This command sets the text on the SDL or xterm window title bar.\r\n");
      break;
#ifdef USE_SDL
    case CMD_FULLSCREEN:
      emulate_printf("Syntax: *FullScreen (<On|Off|1|0>)\r\n");
      emulate_printf("  This controls  fullscreen mode  within  SDL. On or 1 switches to fullscreen,\r\n");
      emulate_printf("  0 or Off restores the windowws mode, and with no parameter given,\r\n");
      emulate_printf("  toggles fullscreen mode.\r\n");
#ifdef CYGWINBUILD
      emulate_printf("\r\n  Please note drawing on a fullscreen surface on the Windows build is known to\r\n");
      emulate_printf("  be glitchy, at least on some systems, due to an SDL bug.\r\n  MODE 7 seems to mostly work.\r\n");
#endif
      break;
    case CMD_NEWMODE:
      emulate_printf("Syntax: *NewMode <mode> <xres> <yres> <colours> <xsc> <ysc> (<xeig> (<yeig>))\r\n");
      emulate_printf("  This defines a new mode. The parameters are:\r\n");
      emulate_printf("  mode:    Mode number, range 64-126\r\n");
      emulate_printf("  xres:    X resolution in pixels, minimum 8.\r\n");
      emulate_printf("  yres:    Y resolution in pixels, minimum 8.\r\n");
      emulate_printf("  colours: Colour depth, valid values 2, 4, 16, 256 or 16777216.\r\n");
      emulate_printf("  xsc:     X scaling (e.g. Mode 1 uses 2, Mode 0 uses 1)\r\n");
      emulate_printf("  ysc:     Y scaling (e.g. Mode 0 uses 2, Mode 18 uses 1)\r\n");
      emulate_printf("  xeig:    X eigen value. OS units per pixel = 1<<xeig, default 1\r\n");
      emulate_printf("  yeig:    Y eigen value. OS units per pixel = 1<<xeig, default 1\r\n");
      break;
    case CMD_REFRESH:
      emulate_printf("Syntax: *Refresh (<On|Off|OnError>)\r\n");
      emulate_printf("  This sets the SDL refresh mode. Default is on.\r\n");
      emulate_printf("  On:      Normal mode, display is updated after any change.\r\n");
      emulate_printf("  Off:     Updates are suspended.\r\n");
      emulate_printf("  OnError: Updates are suspended, and re-enabled on an error condition.\r\n");
      emulate_printf("  If no parameter is given, force an immediate display refresh.\r\n");
      break;
    case CMD_SCREENSAVE:
      emulate_printf("Syntax: *ScreenSave <filename>\r\n");
      emulate_printf("  This saves out the current screen as a .bmp (Windows bitmap) file.\r\n");
      emulate_printf("  This works in all screen modes, including 3, 6 and 7.\r\n");
      break;
    case CMD_SCREENLOAD:
      emulate_printf("Syntax: *ScreenLoad <filename>\r\n  This loads a .bmp into the display window.\r\n");
      break;
    case CMD_VOLUME:
      emulate_printf("Syntax: *Volume <n>\r\n  This sets the audio channel loudness; range 1-127.\r\n");
      break;
    case CMD_CHANNELVOICE:
      emulate_printf("Syntax: *ChannelVoice <channel> <voice index|voice name>\r\n  This attaches a Voice to a Sound Channel.\r\n");
      break;
    case CMD_VOICES:
      emulate_printf("Syntax: *Voices\r\n  This lists the available voices and channel allocation.\r\n");
      break;
#endif /* USE_SDL */
    default:
      if (*command == '.' || *command == '\0') {
        emulate_printf("  BASIC\r\n  MOS\r\n");
#if defined(USE_SDL) | defined(TARGET_UNIX)
        emulate_printf("  MATRIX\r\n");
#endif
#ifdef USE_SDL
        emulate_printf("  SOUND\r\n");
#endif
        emulate_printf("  MEMINFO\r\n");
      }
  }
}

/*
 * *KEY - define a function key string.
 * The string parameter is GSTransed so that '|' escape sequences
 * can be used.
 * On entry, 'command' points at the start of the parameter string.
 * Bug: as uses 0-terminated strings, cannot embed |@ in string. - fixed
 */
#define HIGH_FNKEY 15			/* Highest function key number */
static void cmd_key(char *command) {
  unsigned int key, len;

  while (*command == ' ') command++;		// Skip spaces
  if (*command == 0) error(ERR_BADSYNTAX, "KEY <num> (<string>");
  key=cmd_parse_dec(&command);			// Get key number
  if (key > HIGH_FNKEY) error(ERR_BADKEY);
  if (*command == ',') command++;		// Step past any comma

  command=mos_gstrans(command, &len);		// Get GSTRANS string
  if (kbd_fnkeyset(key, command, len)) error(ERR_KEYINUSE);
}

/*
 * *SHOW - show function key definition
 */
static void cmd_show(char *command) {
  int key1, key2, len;
  char *string;
  char c;

  while (*command == ' ') command++;		// Skip spaces
  if (*command == 0) {
    key1 = 0; key2 = HIGH_FNKEY;		// All keys
  } else {
    key2=(key1=cmd_parse_dec(&command));	// Get key number
    if (key1 > HIGH_FNKEY) error(ERR_BADKEY);
  }
  while (*command == ' ') command++;		// Skip spaces
  if (*command != 0) error(ERR_BADCOMMAND);

  for (; key1 <= key2; key1++) {
    string=kbd_fnkeyget(key1, &len);
    emulate_printf("*Key %d \x22", key1);
    while (len--) {
      c=*string++;
      if (c&128) { emulate_printf("|!"); c=c&127; }
      if (c<32 || c==127) {
	emulate_printf("|%c",c^64);
      } else {
	if (c==34 || c==124) {
	  emulate_printf("|%c",c);
	} else {
	  emulate_printf("%c",c);
	}
      }
    }
    emulate_printf("\x22\r\n");
  }
}

/*
 * *EXEC - Part of the implementation for *EXEC
 */
static void cmd_exec(char *command) {
  while (*command == ' ') command++;		// Skip spaces
  if (*command == 0) {
    if (matrixflags.doexec) fclose(matrixflags.doexec);
    matrixflags.doexec = NULL;
  } else {
    if ((command[0] == '"') && (command[strlen(command)-1] == '"')) {
      command[strlen(command)-1] = '\0';
      command++;
    }
    matrixflags.doexec=fopen(command, "r");
    if (!matrixflags.doexec) error(ERR_NOTFOUND, command);
  }
}

static void cmd_spool(char *command, int append) {
  while (*command == ' ') command++;		// Skip spaces
  if (*command == 0) {
    if (matrixflags.dospool) fclose(matrixflags.dospool);
    matrixflags.dospool=NULL;
  } else {
    if ((command[0] == '"') && (command[strlen(command)-1] == '"')) {
      command[strlen(command)-1] = '\0';
      command++;
    }
    if (append) {
      matrixflags.dospool=fopen(command, "a");
    } else {
      matrixflags.dospool=fopen(command, "w");
    }
    if (!matrixflags.dospool) error(ERR_CANTWRITE, command);
  }
}

/*
 * *QUIT
 * Exit interpreter
 */
static void cmd_quit(char *command) {
	exit_interpreter(0);
}

static int ishex(char ch){
 if (ch >='0' && ch <='9') return ch-'0';
 if (ch >='A' && ch <='F') return 10+ch-'A';
 if (ch >='a' && ch <='f') return 10+ch-'a';
 return -1;
}

static void cmd_load(char *command){
  int i,len,ch,n;
  size_t num;
  char chbuff[256], *ptr;
  FILE *filep;

  memset(chbuff, 0, 256);
  while( (ch= *command)>0 && ch <=32)command++;
  len=255;
  for(i=0;i<256;i++){
    ch=chbuff[i]=command[i];
    if(ch<=32){
      len=i;
    break;
    }
  }
  chbuff[len] ='\0';
  strip_quotes(chbuff);
  // fprintf(stderr,"load filename is \"%s\"\n",chbuff);

  ptr=&command[len];
  while( (ch= *ptr)>0 && ch <=32)ptr++;

  num = 0;
  while((n=ishex(ch=*ptr))>=0) {num=(num<<4)+n; ptr++;}

  //fprintf(stderr,"load addr is %ld (0x%08lx)\n",num,num);

  if (num == 0) {
    emulate_printf("Syntax: LOAD <filename> <load address>\r\n");
    return;
  }

  if ( (filep = fopen(chbuff,"rb")) == (FILE*)0){
    // fprintf(stderr,"LOAD: Could not open file \"%s\"\n",chbuff);
    error(ERR_NOTFOUND, chbuff);
    return;
  }
  ptr=(char*)num;
#ifdef USE_SDL
  ptr = (char *)m7offset((size_t)ptr);
#endif

  while((ch=getc(filep)) != EOF) *ptr++ = ch;

  fclose(filep);
}

static void cmd_save(char *command){
  int i,len,ch,n;
  size_t addr,size;
  int f;
  char chbuff[256], *ptr;
  FILE *filep;

  ptr=command;
  // fprintf(stderr,"SAVE: command is :- \"");
  // while( (ch=*ptr++) >= 32)putc(ch,stderr);
  // fprintf(stderr,"\"\n");

  while((ch=*command)==32 || ch ==9)command++;

  len=255;
  for(i=0;i<256;i++){
    ch=chbuff[i]=command[i];
    if(ch<=32){
      len=i;
      break;
    }
  }
  chbuff[len] ='\0';
  strip_quotes(chbuff);
  // fprintf(stderr,"save filename is \"%s\"\n",chbuff);

  ptr=&command[len];
  while( (ch= *ptr)>0 && ch <=32)ptr++;

  addr = 0;
  while((n=ishex(ch=*ptr))>=0) {addr=(addr<<4)+n; ptr++;}

  // fprintf(stderr,"save addr is %ld (0x%08lx)\n",addr,addr);

  while( (ch= *ptr)>0 && ch <=32)ptr++;

  size=0;
  f=0;
  if(ch == '+'){
    ptr++;
    f=1;
  }
   while((n=ishex(ch=*ptr))>=0) {size=(size<<4)+n; ptr++;}
  if(!f) size -= addr-1;
  // fprintf(stderr,"save size is %ld (0x%08lx)\n",size,size);

  if ((addr == 0) || (size == 0)) {
    emulate_printf("Syntax: SAVE <fname> <start addr> <end addr>|+<length>\r\n");
    return;
  }

  if ( (filep = fopen(chbuff,"wb")) == (FILE*)0){
    emulate_printf("SAVE: Could not open file \"%s\"\r\n",chbuff);
    return;
  }
  ptr=(char*)addr;
#ifdef USE_SDL
  ptr=(char *)m7offset((size_t)ptr);
#endif
  for(i=0; i<size; i++){
   fputc(*ptr++,filep);
  }

  fclose(filep);
}

static void cmd_volume(char *command){
#ifdef USE_SDL
 int ch,v;
 while((ch=*command)== ' ' || ch == '\t') command ++;
 v=0;
 while( (ch= *command++) >= '0' && ch <= '9') v=(v*10)+ (ch-'0');

 sdl_volume(v);
#endif
}

static void cmd_channelvoice(char *command){
#ifdef USE_SDL
 int ch,channel;

 while((ch=*command)== ' ' || ch == '\t') command ++;
 
 channel=0;
 while( (ch= *command++) >= '0' && ch <= '9') channel=(channel*10)+ (ch-'0');

 while((ch=*command)== ' ' || ch == '\t') command ++;

 sdl_voice(channel, command);
#endif
}

static void cmd_voices(){
#ifdef USE_SDL
 sdl_star_voices();
#endif
}

static void cmd_pointer(char *command){

  while(*command == ' ')command++;
  if(*command == '0') mos_mouse_off(); else mos_mouse_on(1);
}

/*
 * check_command - Check if the command is one of the RISC OS
 * commands implemented by this code.
 */
static int check_command(char *text) {
  char command[16];
  int length;

  if (*text == 0) return CMD_UNKNOWN;
  if (*text == '.') return CMD_CAT;

  length = 0;
  while (length < 16 && isalpha(*text)) {
    command[length] = tolower(*text);
    length++;
    text++;
  }
  command[length] = 0;
//if (strcmp(command, "cat")    == 0) return CMD_CAT; /* Disabled, *. works but *cat is passed to OS */
//if (strcmp(command, "window") == 0) return CMD_WINDOW;
//if (strcmp(command, "title")  == 0) return CMD_TITLE;

  if(cmdtab == (cmdtabent*)0) make_cmdtab();
  return get_cmdvalue(command);
}


/*
** mos_oscli issues the operating system command 'command'.
** 'respfile' is set to NIL if the command output is to displayed
** in the normal way. If it is not NIL, the command output is
** redirected to it.
** The return code from the command is stored in
** basicvars.retcode and can be read by the function RETCODE
** Note that it is assumed that the buffer holding the command
** string is large enough to have the redirection commands
** appended to it. (basicvars.stringwork is used for this and so
** it should not be a problem unless the size of stringwork is
** drastically reduced.)
*/
void mos_oscli(char *command, char *respfile, FILE *respfh) {
  int cmd;

  while (*command == ' ' || *command == '*') command++;
  if (*command == 0) return;					/* Null string */
  if (*command == (char)124 || *command == (char)221) return;	/* Comment     */
//if (*command == '\\') { }					/* Extension   */

  if (!basicvars.runflags.ignore_starcmd) {
/*
 * Check if command is one of the *commands implemented
 * by this code.
 */
    cmd = check_command(command);
    switch(cmd){
      case CMD_KEY:           cmd_key(command+3); return;
      case CMD_CAT:           cmd_cat(command); return;
      case CMD_EX:            cmd_ex(command); return;
      case CMD_QUIT:          cmd_quit(command+4); return;
      case CMD_HELP:          cmd_help(command+4); return;
      case CMD_CD:            cmd_cd(command+2); return;
      case CMD_FX:            cmd_fx(command+2); return;
      case CMD_SHOW:          cmd_show(command+4); return;
      case CMD_EXEC:          cmd_exec(command+4); return;
      case CMD_SPOOL:         cmd_spool(command+5,0); return;
      case CMD_SPOOLON:       cmd_spool(command+7,1); return;
//    case CMD_VER:           cmd_ver(); return;
      case CMD_SCREENSAVE:    cmd_screensave(command+10); return;
      case CMD_SCREENLOAD:    cmd_screenload(command+10); return;
      case CMD_WINTITLE:      cmd_wintitle(command+8); return;
      case CMD_FULLSCREEN:    cmd_fullscreen(command+10); return;
      case CMD_NEWMODE:       cmd_newmode(command+7); return;
      case CMD_REFRESH:       cmd_refresh(command+7); return;
      case CMD_BRANDYINFO:    cmd_brandyinfo(); return;

      case CMD_LOAD:          cmd_load(command+4); return;
      case CMD_SAVE:          cmd_save(command+4); return;

      case CMD_VOLUME:        cmd_volume(command+6);return;
      case CMD_CHANNELVOICE:  cmd_channelvoice(command+12);return;
      case CMD_VOICES:        cmd_voices();return;
      case CMD_POINTER:       cmd_pointer(command+7);return;
    }
  }

  if (*command == '/') {		/* Run file, so just pass to OS     */
    command++;				/* Step past '/'                    */
    while (*command == ' ') command++;	/* And skip any more leading spaces */
  }

  native_oscli(command, respfile, respfh);
}

#ifdef TARGET_MINGW
/* Code from Microsoft example code of how to do popen without using popen */
HANDLE g_hChildStd_IN_Rd = NULL;
HANDLE g_hChildStd_IN_Wr = NULL;
HANDLE g_hChildStd_OUT_Rd = NULL;
HANDLE g_hChildStd_OUT_Wr = NULL;
HANDLE g_hInputFile = NULL;

/* Create a child process that uses the previously created pipes for STDIN and STDOUT. */
void CreateChildProcess(char *procname) {
   char cmdLine[256];
   PROCESS_INFORMATION piProcInfo;
   STARTUPINFO siStartInfo;
   BOOL bSuccess = FALSE;

   *cmdLine='\0';
   strcat(cmdLine, "cmd /c ");
   strncat(cmdLine, procname, 247);

// Set up members of the PROCESS_INFORMATION structure.
   ZeroMemory( &piProcInfo, sizeof(PROCESS_INFORMATION) );

// Set up members of the STARTUPINFO structure.
// This structure specifies the STDIN and STDOUT handles for redirection.
   ZeroMemory( &siStartInfo, sizeof(STARTUPINFO) );
   siStartInfo.cb = sizeof(STARTUPINFO);
   siStartInfo.hStdError = g_hChildStd_OUT_Wr;
   siStartInfo.hStdOutput = g_hChildStd_OUT_Wr;
   siStartInfo.hStdInput = g_hChildStd_IN_Rd;
   siStartInfo.dwFlags |= (STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW);
   siStartInfo.wShowWindow = SW_HIDE;


// Create the child process.
   bSuccess = CreateProcess(NULL,
      cmdLine,       // command line
      NULL,          // process security attributes
      NULL,          // primary thread security attributes
      TRUE,          // handles are inherited
      0,             // creation flags
      NULL,          // use parent's environment
      NULL,          // use parent's current directory
      &siStartInfo,  // STARTUPINFO pointer
      &piProcInfo);  // receives PROCESS_INFORMATION

   // If an error occurs, complain.
   if (!bSuccess)
      error(ERR_CMDFAIL);
   else {
      // Close handles to the child process and its primary thread.
      // Some applications might keep these handles to monitor the status
      // of the child process, for example.
      CloseHandle(piProcInfo.hProcess);
      CloseHandle(piProcInfo.hThread);
      CloseHandle(g_hChildStd_IN_Wr);
      CloseHandle(g_hChildStd_OUT_Wr);
   }
}

// Read output from the child process's pipe for STDOUT
// Stop when there is no more data.
int ReadFromPipe(void) {
   DWORD dwRead;
   char buf;
   BOOL bSuccess = FALSE;

   bSuccess = ReadFile(g_hChildStd_OUT_Rd, &buf, 1, &dwRead, NULL);
   if (!bSuccess || dwRead == 0) {
	 CloseHandle(g_hChildStd_OUT_Rd);
	 return(-1);
   }
   return buf;
}
#endif /* TARGET_MINGW */

static void native_oscli(char *command, char *respfile, FILE *respfh) {
  int clen;
  FILE *sout;
  char *cmdbuf, *cmdbufbase, *pipebuf=NULL;
#ifdef USE_SDL
#ifndef TARGET_MINGW
  char buf;
#endif
#endif
#if defined(TARGET_MINGW) && defined(USE_SDL)
  int getChar;
  SECURITY_ATTRIBUTES saAttr;

  saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
  saAttr.bInheritHandle = TRUE;
  saAttr.lpSecurityDescriptor = NULL;
#endif

  clen=strlen(command) + 256;
  cmdbufbase=malloc(clen);
  cmdbuf=cmdbufbase;
  memcpy(cmdbuf, command, strlen(command)+1);

#if defined(TARGET_DJGPP) | defined(TARGET_WIN32)
/* Command is to be sent to underlying DOS-style OS */
  if (respfile==NIL) {			/* Command output goes to normal place */
    basicvars.retcode = system(cmdbuf);
    find_cursor();			/* Figure out where the cursor has gone to */
#if defined(TARGET_MINGW)
    emulate_printf("\r\n");		/* Restore cursor position */
#endif
    if (basicvars.retcode < 0) error(ERR_CMDFAIL);
  } else {				/* Want response back from command */
    strcat(cmdbuf, " >");
    strcat(cmdbuf, respfile);
    basicvars.retcode = system(cmdbuf);
    find_cursor();			/* Figure out where the cursor has gone to */
#if defined(TARGET_MINGW)
    emulate_printf("\r\n");		/* Restore cursor position */
#endif
    if (basicvars.retcode < 0) {
      remove(respfile);
      error(ERR_CMDFAIL);
    }
  }

#elif defined(TARGET_MACOSX) | defined(TARGET_UNIX) | defined(TARGET_AMIGA)
/* Command is to be sent to underlying Unix-style OS, excluding MinGW */
/* This is the Unix version of the function, where both stdout
** and stderr can be redirected to a file
*/
  if (respfile == NIL) {		/* Command output goes to normal place */
#ifdef USE_SDL
    strcat(cmdbuf, " 2>&1");
    sout = popen(cmdbuf, "r");
    if (sout == NULL) error(ERR_CMDFAIL);
    while (fread(&buf, 1, 1, sout) > 0) {
      if (buf == '\n') emulate_vdu('\r');
      emulate_vdu(buf);
    }
    pclose(sout);
#else
    fflush(stdout);			/* Make sure everything has been output */
    fflush(stderr);
    basicvars.retcode = system(cmdbuf);
    find_cursor();			/* Figure out where the cursor has gone to */
    if (basicvars.retcode < 0) error(ERR_CMDFAIL);
#endif
  } else {				/* Want response back from command */
    strcat(cmdbuf, " 2>&1");
    sout = popen(cmdbuf, "r");
    if (sout == NULL) {
      fclose(respfh);
      remove(respfile);
      error(ERR_CMDFAIL);
    } else {
      pipebuf=malloc(4096);
#if !defined(USE_SDL) && !defined(TARGET_RISCOS)
      echo_off();
#endif
      while (fgets(pipebuf, sizeof(pipebuf)-1, sout)) {
        fprintf(respfh, "%s", pipebuf);
      }
#if !defined(USE_SDL) && !defined(TARGET_RISCOS)
      echo_on();
#endif
      pclose(sout);
      fclose(respfh);
    }
  }
  if (pipebuf) free(pipebuf);

#elif defined(TARGET_MINGW)
/* Command is to be sent to underlying Windows OS via MinGW */
/* This is the Windows/MinGW version of the function, where both
** stdout and stderr can be redirected to a file
*/
  if (respfile == NIL) {		/* Command output goes to normal place */
#ifdef USE_SDL
    //strcat(cmdbuf, " 2>&1");

// Create a pipe for the child process's STDOUT.
    if (!CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, 0)) error(ERR_CMDFAIL);
// Ensure the read handle to the pipe for STDOUT is not inherited.
    if (!SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0)) error(ERR_CMDFAIL);
// Create a pipe for the child process's STDIN.
    if (!CreatePipe(&g_hChildStd_IN_Rd, &g_hChildStd_IN_Wr, &saAttr, 0))  error(ERR_CMDFAIL);
// Ensure the write handle to the pipe for STDIN is not inherited.
    if (!SetHandleInformation(g_hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0)) error(ERR_CMDFAIL);
// Create the child process.
    CreateChildProcess(cmdbuf);

    while((getChar=ReadFromPipe()) >0) {
      if (getChar == '\n') emulate_vdu('\r');
      emulate_vdu(getChar);
    }
#if 0
    /* This really needs to be redone using Windows API calls instead of popen() */
    sout = popen(cmdbuf, "r");
    if (sout == NULL) error(ERR_CMDFAIL);
    while (fread(&buf, 1, 1, sout) > 0) {
      if (buf == '\n') emulate_vdu('\r');
      emulate_vdu(buf);
    }
    pclose(sout);
#endif
#else /* !USE_SDL */
    fflush(stdout);			/* Make sure everything has been output */
    fflush(stderr);
    basicvars.retcode = system(cmdbuf);
    find_cursor();			/* Figure out where the cursor has gone to */
    emulate_printf("\r\n");		/* Restore cursor position */
    if (basicvars.retcode < 0) error(ERR_CMDFAIL);
#endif /* USE_SDL */
  } else {				/* Want response back from command */
    strcat(cmdbuf, " 2>&1");
    sout = popen(cmdbuf, "r");
    if (sout == NULL) {
      fclose(respfh);
      remove(respfile);
      error(ERR_CMDFAIL);
    } else {
      pipebuf=malloc(4096);
#if !defined(USE_SDL) && !defined(TARGET_RISCOS)
      echo_off();
#endif
      while (fgets(pipebuf, sizeof(pipebuf)-1, sout)) {
	fprintf(respfh, "%s", pipebuf);
      }
#if !defined(USE_SDL) && !defined(TARGET_RISCOS)
      echo_on();
#endif
      pclose(sout);
      fclose(respfh);
    }
  }
  if (pipebuf) free(pipebuf);
#else
#error There is no mos_oscli() function for this target
#endif
  free(cmdbufbase);
}


void mos_getswiname(size_t swino, size_t namebuf, size_t buflen, int32 inxflag) {
  int32 ptr;
  char *swiname_result=(char *)namebuf;

  *swiname_result='\0';
  if (swino & XBIT) {
    swino &= ~XBIT;
    strncat(swiname_result, "X",2);
    buflen--;
  }
  for (ptr=0; swilist[ptr].swinum!=0xFFFFFFFF; ptr++) {
    if (swino == swilist[ptr].swinum) {
      strncat(swiname_result, swilist[ptr].swiname, buflen);
      break;
    }
  }
  if (swilist[ptr].swinum==0xFFFFFFFF)
    if (inxflag != XBIT) error(ERR_SWINUMNOTKNOWN, swino);
}

/*
** 'mos_get_swinum' returns the SWI number corresponding to
** SWI 'name'
*/
size_t mos_getswinum(char *name, int32 length, int32 inxflag) {
  int32 ptr;
  int32 xflag=0;
  char namebuffer[128];

  if (name[0] == 'X') {
    name++;
    length--;
    xflag=XBIT;
  }
  for (ptr=0; swilist[ptr].swinum!=0xFFFFFFFF; ptr++) {
    if ((!strncmp(name, swilist[ptr].swiname, length)) && length==strlen(swilist[ptr].swiname)) break;
  }
  strncpy(namebuffer,name, length);
  namebuffer[length]='\0';
  if (swilist[ptr].swinum==0xFFFFFFFF) {
    if (inxflag != XBIT) error(ERR_SWINAMENOTKNOWN, namebuffer);
    return (-1);
  } else {
    return ((swilist[ptr].swinum)+xflag);
  }
}

/*
** 'mos_sys' issues a SWI call and returns the result. On
** platforms other than RISC OS this is emulated.
** Most SWI calls are defined in mos_sys.c except the few that
** call other functions in this file.
*/
void mos_sys(size_t swino, sysparm inregs[], size_t outregs[], size_t *flags) {
  int32 ptr, rtn;
  int32 xflag;

  xflag = swino & XBIT;	/* Is the X flag set? */
  swino &= ~XBIT;		/* Strip off the X flag if set */
  switch (swino) {
    case SWI_OS_CLI:
      outregs[0]=inregs[0].i;
      mos_oscli((char *)(size_t)inregs[0].i, NIL, NULL);
      break;
    case SWI_OS_Byte:
      rtn=mos_osbyte(inregs[0].i, inregs[1].i, inregs[2].i, xflag);
      outregs[0]=inregs[0].i;
      outregs[1]=((rtn >> 8) & 0xFF);	// check
      outregs[2]=((rtn >> 16) & 0xFF);	// check
      break;
    case SWI_OS_Word:
      mos_osword(inregs[0].i, inregs[1].i);
      outregs[0]=inregs[0].i;
      outregs[1]=inregs[1].i;
      break;
    case SWI_OS_SWINumberToString:
      mos_getswiname(inregs[0].i, inregs[1].i, inregs[2].i, xflag);
      outregs[0]=inregs[0].i;
      outregs[1]=inregs[1].i;
      outregs[2]=strlen((char *)(size_t)outregs[1])+1; /* returned length includes terminator */
      break;
    case SWI_OS_SWINumberFromString:
      outregs[1]=inregs[1].i;
      for(ptr=0;*((char *)(size_t)inregs[1].i+ptr) >=32; ptr++) ;
      *((byte *)(size_t)inregs[1].i+ptr)='\0';
      outregs[0]=mos_getswinum((char *)(size_t)inregs[1].i, strlen((char *)(size_t)inregs[1].i), xflag);
      break;
    default:
      mos_sys_ext(swino, inregs, outregs, xflag, flags); /* in mos_sys.c */
      break;
  }
}

/*
** 'mos_init' is called to initialise the mos interface code for
** the versions of this program that do not run under RISC OS.
** The function returns 'TRUE' if initialisation was okay or
** 'FALSE' if it failed (in which case it is not safe for the
** interpreter to run)
*/
boolean mos_init(void) {
  (void) clock();	/* This might be needed to start the clock */
  mos_wrtime(0);
  return TRUE;
}

/*
** 'mos_final' is called to tidy up the emulation at the end
** of the run
*/
void mos_final(void) {
}

static void mos_osword(int32 areg, int64 xreg) {
  switch (areg) {
    case 1:
      osword01(xreg);
      break;
    case 2:
      osword02(xreg);
      break;
    case 9:
#ifdef USE_SDL
      osword09(xreg);
#endif
      break;
    case 10:
#ifdef USE_SDL
      osword0A(xreg);
#endif
      break;
    case 11:
#ifdef USE_SDL
      osword0B(xreg);
#endif
      break;
    case 12:
#ifdef USE_SDL
      osword0C(xreg);
#endif
      break;
    case 139:
#ifdef USE_SDL
      osword8B(xreg);
#endif
      break;
    case 140:
#ifdef USE_SDL
      osword8C(xreg);
#endif
      break;
   }
}


/*
* OSBYTE < &A6 perform actions
* OSBYTE < &80 X is only parameter
* OSBYTE > &7F X and Y are parameters
* OSBYTE > &A5 read/write variables, perform no action
* See beebwiki.mdfs.net/OSBYTEs
* Many of these are not sensible to implement, but what calls are implemented
* must use correct numbers.
*
* OSBYTE &00   0 Read host OS
* OSBYTE &01   1 Read/Write User Flag/Program Return Value
* OSBYTE &02   2 Specify Input Stream
* OSBYTE &03   3 Specify Output Streams
* OSBYTE &04   4 Define cursor editing key action
* OSBYTE &05   5 Printer Driver Type
* OSBYTE &06   6 Printer Ignore Character
* OSBYTE &07   7 Serial Baud Receive rate
* OSBYTE &08   8 Serial Baud Transmit Rate
* OSBYTE &09   9 First Colour Flash Duration
* OSBYTE &0A  10 Second Colour Flash Duration
* OSBYTE &0B  11 Keypress Auto Repeat Delay
* OSBYTE &0C  12 Keypress Auto Repeat Period
* OSBYTE &0D  13 Disable Event
* OSBYTE &0E  14 Enable Event
* OSBYTE &0F  15 Flush all buffers/input buffers
* OSBYTE &10  16 Set maximum ADC chanel
* OSBYTE &11  17 Force an ADC conversion
* OSBYTE &12  18 Reset soft key definitions
* OSBYTE &13  19 Wait for Vertical Retrace
* OSBYTE &14  20 Explode user defined characters
* OSBYTE &15  21 Flush Selected Buffer
* OSBYTE &16  22 Increment Polling Semaphore
* OSBYTE &17  23 Decrement Polling Semaphore
* OSBYTE &18  24 Select external sound system.
* OSBYTE &19  25 Reset a group of font definitions
* OSBYTE &1A  26
* OSBYTE &1B  27
* OSBYTE &1C  28
* OSBYTE &1D  29
* OSBYTE &1E  30
* OSBYTE &1F  31
* OSBYTE &20  32 Watford32K - Read top of memory
* OSBYTE &21  33 Watford32K - Read top of memory for mode
* OSBYTE &22  34 Watford32K - Read/Write RAM switch
* OSBYTE &23  35 Watford32K - Read workspace address
* OSBYTE &24  36 Watford32K - Read/Write RAM buffer bank
* OSBYTE &25  37
* OSBYTE &26  38
* OSBYTE &27  39
* OSBYTE &28  40 
* OSBYTE &29  41 
* OSBYTE &2A  42 (Deprecated, see 163,2)
* OSBYTE &2B  43 (Deprecated, see 163,3)
* OSBYTE &2C  44 (Deprecatedm see 163,4)
* OSBYTE &2D  45
* OSBYTE &2E  46
* OSBYTE &2F  47
* OSBYTE &30  48
* OSBYTE &31  49
* OSBYTE &32  50 NetFS - Poll transmit
* OSBYTE &33  51 NetFS - Poll receive
* OSBYTE &34  52 NetFS - Delete receive block, enable/disable events on reception
* OSBYTE &35  53 NetFS - Disconnect REMOTE
* OSBYTE &36  54
* OSBYTE &37  55
* OSBYTE &38  56
* OSBYTE &39  57
* OSBYTE &3A  58
* OSBYTE &3B  59
* OSBYTE &3C  60
* OSBYTE &3D  61
* OSBYTE &3E  62
* OSBYTE &3F  63 ZNOS CP/M - Reload CCP and BDOS
* OSBYTE &40  64
* OSBYTE &41  65
* OSBYTE &42  66
* OSBYTE &43  67 ParaMax - Enter CNC control program
* OSBYTE &44  68 Test sideways RAM presence
* OSBYTE &45  69 Test PSEUDO/Absolute usage
* OSBYTE &46  70 Read/write country number
* OSBYTE &47  71 Read/write alphabet or keyboard number
* OSBYTE &48  72
* OSBYTE &49  73
* OSBYTE &4A  74
* OSBYTE &4B  75
* OSBYTE &4C  76
* OSBYTE &4D  77
* OSBYTE &4E  78
* OSBYTE &4F  79
* OSBYTE &50  80
* OSBYTE &51  81
* OSBYTE &52  82
* OSBYTE &53  83
* OSBYTE &54  84
* OSBYTE &55  85
* OSBYTE &56  86
* OSBYTE &57  87
* OSBYTE &58  88
* OSBYTE &59  89
* OSBYTE &5A  90 Find/set ROM status
* OSBYTE &5B  91
* OSBYTE &5C  92
* OSBYTE &5D  93
* OSBYTE &5E  94
* OSBYTE &5F  95
* OSBYTE &60  96 Terminal Emulator flow control
* OSBYTE &61  97 HKSET Page timeout monitor
* OSBYTE &62  98 HKSET Poll received page status
* OSBYTE &63  99
* OSBYTE &64 100 Enter SPY debugger
* OSBYTE &65 101
* OSBYTE &66 102
* OSBYTE &67 103
* OSBYTE &68 104
* OSBYTE &69 105
* OSBYTE &6A 106 Select pointer/activate mouse
* OSBYTE &6B 107 External/Internal 1MHz Bus
* OSBYTE &6C 108 Main/Shadow RAM Usage
* OSBYTE &6D 109 Make Temporary FS permanent
* OSBYTE &6E 110 Early Watford DFS            - BBC-hardware specific, could use for something else
* OSBYTE &6F 111 Read/Write shadow RAM switch - BBC-hardware specific, could use for something else
* OSBYTE &70 112 Select Main/Shadow for VDU access
* OSBYTE &71 113 Select Main/Shadow for Display hardware
* OSBYTE &72 114 Write to Shadow/Main toggle
* OSBYTE &73 115 Blank/restore palette
* OSBYTE &74 116 Reset internal sound system
* OSBYTE &75 117 Read VDU Status Byte
* OSBYTE &76 118 Reflect keyboard status in LEDs
* OSBYTE &77 119 Close all Spool/Exec files
* OSBYTE &78 120 Write Key Pressed Data
* OSBYTE &79 121 Keyboard Scan
* OSBYTE &7A 122 Keyboard Scan from &10
* OSBYTE &7B 123 Printer Dormancy Warning
* OSBYTE &7C 124 Clear ESCAPE condition
* OSBYTE &7D 125 Set ESCAPE conditon
* OSBYTE &7E 126 Acknowledge ESCAPE condition
* OSBYTE &7F 127 Check for EOF
* OSBYTE &80 128 Read ADC Channel/Buffer/Mouse/Device status
* OSBYTE &81 129 Read Key with Time Limit/Scan for any keys/Read OS version
* OSBYTE &82 130 Read High Order Address
* OSBYTE &83 131 Read bottom of user memory (OSHWM)
* OSBYTE &84 132 Read top of user memory
* OSBYTE &85 133 Read base of display RAM for a given mode
* OSBYTE &86 134 Read text cursor position
* OSBYTE &87 135 Character at text cursor and screen MODE
* OSBYTE &88 136 Call user code (called by *CODE)
* OSBYTE &89 137 Cassette Motor Control (called by *MOTOR)
* OSBYTE &8A 138 Place character into buffer
* OSBYTE &8B 139 Set filing system attributes (called by *OPT)
* OSBYTE &8C 140 Select Tape FS at 1200/300 baud (called by *TAPE)
* OSBYTE &8D 141 Select RFS (called by *ROM)
* OSBYTE &8E 142 Enter Language ROM
* OSBYTE &8F 143 Issue Service Call
* OSBYTE &90 144 Set TV offset and interlacing (called by *TV x,y)
* OSBYTE &91 145 Read character from buffer
* OSBYTE &92 146 Read FRED
* OSBYTE &93 147 Write FRED
* OSBYTE &94 148 Read JIM
* OSBYTE &95 149 Write JIM
* OSBYTE &96 150 Read SHEILA
* OSBYTE &97 151 Write SHEILA
* OSBYTE &98 152 Examine Buffer Status
* OSBYTE &99 153 Write character into input buffer checking for ESCAPE
* OSBYTE &9A 154 Write to Video ULA control register and RAM copy
* OSBYTE &9B 155 Write to Video ULA palette register and RAM copy
* OSBYTE &9C 156 Read/write ACIA control registers
* OSBYTE &9D 157 Fast Tube BPUT
* OSBYTE &9E 158 Read from Speech Processor
* OSBYTE &9F 159 Write to Speech Processor
* OSBYTE &A0 160 Read VDU Variable
* OSBYTE &A1 161 Read CMOS RAM
* OSBYTE &A2 162 Write CMOS RAM
* OSBYTE &A3 163 Reserved for applications software:
             163,1
             163,2 Get/set *REFRESH state
             163,3 Output Y to Linux controlling terminal
             163,4 Enable/disable CTRL-N/CTRL-P for line editing
* OSBYTE &A4 164 Check Processor Type
* OSBYTE &A5 165 Read output Cursor Position
* OSBYTE &A6 166 Read Start of MOS variables
* OSBYTE &A7 167 Read Start of MOS variables
* OSBYTE &A8 168 Read address of extended vector table
* OSBYTE &A9 169 Read address of extended vector table
* OSBYTE &AA 170 Read address of ROM info table
* OSBYTE &AB 171 Read address of ROM info table
* OSBYTE &AC 172 Read address of keyboard table
* OSBYTE &AD 173 Read address of keyboard table
* OSBYTE &AE 174 Read address of VDU variables
* OSBYTE &AF 175 Read address of VDU variables
* OSBYTE &B0 176 Read/Write VSync counter
* OSBYTE &B1 177 Read/Write input device (FX2)
* OSBYTE &B2 178 Read/Write keyboard interrupt enable
* OSBYTE &B3 179 Read/Write primary OSHWM/ROM polling semaphore
* OSBYTE &B4 180 Read/Write OSHWM
* OSBYTE &B5 181 Read/Write RS423 interpretation
* OSBYTE &B6 182 Read/Write Font Explosion/NOIGNORE Status
* OSBYTE &B7 183 Read/Write TAPE/ROM switch  --> could reassign
* OSBYTE &B8 184 Read/Write MOS copy of Video ULA control register
* OSBYTE &B9 185 Read/Write MOS copy of palette register/ROM polling semaphore
* OSBYTE &BA 186 Read/Write ROM active on last BRK
* OSBYTE &BB 187 Read/Write ROM number of BASIC
* OSBYTE &BC 188 Read/Write current ADC channel number
* OSBYTE &BD 189 Read/Write highest ADC channel number
* OSBYTE &BE 190 Read/Write ADC resolution
* OSBYTE &BF 191 Read/Write RS423 busy flag
* OSBYTE &C0 192 Read/Write ACIA control register copy
* OSBYTE &C1 193 Read/Write flash counter
* OSBYTE &C2 194 Read/Write first colour duration
* OSBYTE &C3 195 Read/Write second colour duration
* OSBYTE &C4 196 Read/Write auto repeat delay
* OSBYTE &C5 197 Read/Write auto repeat period
* OSBYTE &C6 198 Read/Write *EXEC file handle
* OSBYTE &C7 199 Read/Write *SPOOL file handle
* OSBYTE &C8 200 Read/Write BREAK/ESCAPE effect
* OSBYTE &C9 201 Read/Write keyboard Enable/Disable
* OSBYTE &CA 202 Read/Write Keyboard Status
* OSBYTE &CB 203 Read/Write RS423 input buffer minimum
* OSBYTE &CC 204 Read/Write RS423 ignore flag
* OSBYTE &CD 205 Read/Write RS423 destination/firm key string length --> could reassign
* OSBYTE &CE 206 Read/Write ECONET call intepretation --> could reassign
* OSBYTE &CF 207 Read/Write ECONET input intepretation --> could reassign
* OSBYTE &D0 208 Read/Write ECONET output intepretation --> could reassign
* OSBYTE &D1 209 Read/Write speech supression status --> could reassign
* OSBYTE &D2 210 Read/Write sound supression flag
* OSBYTE &D3 211 Read/Write channel for BELL
* OSBYTE &D4 212 Read/Write volume/ENVELOPE For BELL
* OSBYTE &D5 213 Read/Write frequency for BELL
* OSBYTE &D6 214 Read/Write duration for BELL
* OSBYTE &D7 215 Read/Write Startup Message Enable/Disable
* OSBYTE &D8 216 Read/Write soft key string length
* OSBYTE &D9 217 Read/Write paged line count
* OSBYTE &DA 218 Read/Write VDU Queue length
* OSBYTE &DB 219 Read/Write ASCII code for TAB
* OSBYTE &DC 220 Read/Write ASCII for ESCAPE
* OSBYTE &DD 221 Read/Write Intrepretation of &C0-&CF
* OSBYTE &DE 222 Read/Write Interpretation of &D0-&DF
* OSBYTE &DF 223 Read/Write Interpretation of &E0-&EF
* OSBYTE &E0 224 Read/Write Interpretation of &F0-&FF
* OSBYTE &E1 225 Read/Write Interpretation of &80-&8F FKeys
* OSBYTE &E2 226 Read/Write Interpretation of &90-&9F Shift-FKeys
* OSBYTE &E3 227 Read/Write Interpretation of &A0-&AF Ctrl-FKeys
* OSBYTE &E4 228 Read/Write Interpretation of &B0-&BF Ctrl-Shift-FKeys/Alt-FKeys
* OSBYTE &E5 229 Read/Write ESCAPE key status
* OSBYTE &E6 230 Read/Write ESCAPE effects
* OSBYTE &E7 231 Read/Write 6522 User IRQ Mask
* OSBYTE &E8 232 Read/Write 6850 IRQ Mask
* OSBYTE &E9 233 Read/Write 6522 System IRQ Mask
* OSBYTE &EA 234 Read/Write Tube present flag
* OSBYTE &EB 235 Read/Write speech Processor Presence
* OSBYTE &EC 236 Read/Write character output device status (FX3)
* OSBYTE &ED 237 Read/Write Cursor Edit State (FX4)
* OSBYTE &EE 238 Read/Write base of numeric pad
* OSBYTE &EF 239 Read/Write shadow state
* OSBYTE &F0 240 Read/Write Country flag
* OSBYTE &F1 241 Read/Write value written by *FX1
* OSBYTE &F2 242 Read/Write OS copy of serial ULA register
* OSBYTE &F3 243 Read/Write offset to current TIME value
* OSBYTE &F4 244 Read/Write soft key consistency flag
* OSBYTE &F5 245 Read/Write printer Type (FX5)
* OSBYTE &F6 246 Read/Write printer Ignore character (FX6)
* OSBYTE &F7 247 Read/Write Intercept BREAK/Define action of BREAK key
* OSBYTE &F8 248 Read/Write LSB BREAK intercepter jump address
* OSBYTE &F9 249 Read/Write MSB BREAK intercepter jump address
* OSBYTE &FA 250 Read/Write RAM used for VDU access, Watford RAM board status
* OSBYTE &FB 251 Read/Write RAM used for Display hardware
* OSBYTE &FC 252 Read/Write current language Rom Number
* OSBYTE &FD 253 Read/Write Last Reset Type
* OSBYTE &FE 254 Read/Write available RAM/Effect of shift/ctrl on Numeric pad
* OSBYTE &FF 255 Read/Write startup Options
*
* Return value is &00YYXXAA if supported
* Return value is &C0YYFFAA if unsupported, check bit 31
* (b30 copy of b31 for compatability with 6502 V flag position)
*
*/
static byte _sysvar[] = { 0, 0, 0, 0, 0, 0, 0,  0, 0, 0,   /* &A6 - &AF */
  0,  0,  0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0,   /* &B0 - &BF */
  0,  0,  0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0,   /* &C0 - &CF */
  0,  0,  0,   0,   0, 0, 0, 0, 0, 0, 0, 9, 27, 1, 0, 0,   /* &D0 - &DF */
  0,  1,  128, 144, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0,   /* &E0 - &EF */
  0,  0,  0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 255, /* &F0 - &FF */
0 }; /* Overflow for &FF+1 */
byte *sysvar = _sysvar-166;

static int32 mos_osbyte(int32 areg, int32 xreg, int32 yreg, int32 xflag){
int tmp,new;

tmp=(areg=areg & 0xFF);	// Prevent any sillyness

if (areg>=166) {
  new  = (byte)(sysvar[areg] & yreg) ^ xreg;
  xreg = sysvar[areg] & 0xFF;
  yreg = sysvar[areg+1] & 0xFF;
  sysvar[areg] = new;
// Some variables are 'unclean' because we don't control the kernel
  if (areg==210) { if (sysvar[210]!=0) mos_sound_off(); else mos_sound_on(); }
  if (areg==220) kbd_escchar(new, xreg);
  return (0 << 30) | (yreg << 16) | (xreg << 8) | areg;
}

switch (areg) {
	case 0:			// OSBYTE 0 - Return machine type
		if (xreg!=0) return MACTYPE;
		else if (!xflag) error(ERR_MOSVERSION);
// else return pointer to error block
		break;
	case 1: case 3: case 5:
		if (areg==3 || areg==4) tmp=tmp-7;
		return (mos_osbyte(tmp+0xF0, xreg, 0, 0) & 0xFFFFFF00) | areg;
	case 4:
		matrixflags.osbyte4val = xreg;
		break;
	case 6:
		matrixflags.printer_ignore = xreg;
		break;
	case 43:
		printf("%c", xreg);
		fflush(stdout);
		break;
	case 44:
		kbd_setvikeys(xreg);
		break;

#ifdef USE_SDL
	case 15:
		purge_keys();
		drain_mousebuffer();
		break;
	case 20:			// OSBYTE 20, reset font
		reset_sysfont(8);
		return 0x030114;
	case 21:			// OSBYTE 21, flush buffers (0 and 9 only)
		osbyte21(xreg);
		break;
	case 25:			// OSBYTE 25, reset font
		if (((xreg >= 0) && (xreg <= 7)) || (xreg == 16)) {
		  reset_sysfont(xreg);
		  return(0x19);
		} else {
		  return(0x19 + (xreg << 8));
		}
	case 42:			// OSBYTE 42 - local to Brandy
		return osbyte163_2(xreg);
		break;
	case 106:			// OSBYTE 106 - select pointer
		sdl_mouse_onoff(xreg & 0x7);
		break;
	case 112:			// OSBYTE 112 - screen bank written to 
		osbyte112(xreg);
		break;
	case 113:			// OSBYTE 113 - screen bank displayed
		osbyte113(xreg);
		break;
  case 124:
    basicvars.escape = FALSE;
    break;
  case 125:
    basicvars.escape = TRUE;
    error(ERR_ESCAPE);
    break;
	case 134:			// OSBYTE 134 - Read POS and VPOS
	case 165:			// OSBYTE 165 - Read editing cursor position (we don't have seperate cursors)
		return osbyte134_165(areg);
	case 135:			// OSBYTE 135 - Read character and screen MODE
		return osbyte135();
#endif


	case 128:			// OSBYTE 128 - ADVAL
		return ((mos_adval(xreg | yreg<<8) & 0xFFFF) << 8) | 0x80;

	case 129:			// OSBYTE 129 - INKEY
		return ((kbd_inkey(xreg | yreg<<8) & 0xFFFF) << 8) | 0x81;
// NB: Real OSBYTE 129 returns weird result for Escape/Timeout
		break;

	case 130:			// OSBYTE 130 - High word of user memory
		areg = (size_t)basicvars.workspace;
		return ((areg & 0xFFFF0000) >> 8) | 130;

	case 131:			// OSBYTE 132 - Bottom of user memory
		areg = (size_t)basicvars.workspace;
		if (areg < 0xFFFF)	return (areg << 8) | 131;
		else			return ((areg & 0xFF0000) >> 16) | ((areg & 0xFFFF) << 8);

	case 132:			// OSBYTE 132 - Top of user memory
		areg = (size_t)basicvars.slotend;
		if (areg < 0xFFFF)	return (areg << 8) | 132;
		else			return ((areg & 0xFF0000) >> 16) | ((areg & 0xFFFF) << 8);
//	case 133:			// OSBYTE 133 - Read screen start for MODE - not implemented in RISC OS.

	case 138:
		if (xreg==0) push_key(yreg);
		return ((kbd_inkey(xreg | yreg<<8) & 0xFFFF) << 8) | 0x8A; 
	case 160:			// OSBYTE 160 - Read VDU variable
		return emulate_vdufn(xreg) << 8 | 160;
#ifdef USE_SDL
	case 163:			// OSBYTE 163 - Application Support.
		if (xreg==1) {		// get/set REFRESH state
		  if (yreg == 255) return ((get_refreshmode() << 16) + 0x1A3);
		  else {
		    if (yreg > 2) return (0xC000FF2A + (yreg << 16));
		    else star_refresh(yreg);
		  }
		} else if (xreg==2) {
      return osbyte163_2(yreg);
    } else if (xreg==3) {
      printf("%c", yreg);
      fflush(stdout);
    } else if (xreg==4) {
      kbd_setvikeys(yreg);
    } else if (xreg==127) {	// Analogue to 'stty sane'
		  star_refresh(1);
		  osbyte112(1);
		  osbyte113(1);
		  emulate_vdu(6);
		} else if (xreg==242) { // GXR and dot pattern
		  return (osbyte163_242(yreg) << 8);
		}
		break;
#endif
// This is now in keyboard.c
//	case 200:		// OSBYTE 200 - bit 0 disables escape if unset
//	case 229:		// OSBYTE 229 - Enable or disable escape
//	case 250:
//	case 251:
//		break;
	}
if (areg <= 25 || (areg >= 40 && areg <= 44) || areg >= 106)
	return (0 << 30) | (yreg << 16) | (xreg << 8) | areg;	// Default null return
else
	return (3 << 30) | (yreg << 16) | (0xFF00) | areg;	// Default null return
}
#endif /* !TARGET_RISCOS */
