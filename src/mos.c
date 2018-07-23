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
**	This is one the files that contains functions that emulate
**	some features of RISC OS such as the VDU drivers. The others
**	are fileio.c, keyboard.c, textonly.c, textgraph.c and
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
*/
/*
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
**
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include "common.h"
#include "target.h"
#include "errors.h"
#include "basicdefs.h"
#include "mos.h"
#include "screen.h"
#include "keyboard.h"

#ifdef TARGET_RISCOS
#include "kernel.h"
#include "swis.h"
#endif

#ifdef TARGET_DJGPP
#include "dos.h"
#endif

#if defined(TARGET_UNIX) || defined(TARGET_MACOSX)
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#endif

int check_command(char *text);

/* Address range used to identify emulated calls to the BBC Micro MOS */

#define LOW_MOS 0xFFC0
#define HIGH_MOS 0xFFF7

/* Emulated BBC MOS calls */

#define BBC_OSWRCH 0xFFEE
#define BBC_OSWORD 0xFFF1
#define BBC_OSBYTE 0xFFF4

static time_t startime;		/* Adjustment subtracted in 'TIME' */

/* =================================================================== */
/* ======= Emulation functions common to all operating systems ======= */
/* =================================================================== */

/*
** 'emulate_mos' provides an emulation of some of the BBC Micro
** MOS calls emulated by the Acorn interpreter
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
    return mos_osbyte(areg, xreg, yreg);
#endif
    break;
  case BBC_OSWORD:
#ifdef TARGET_RISCOS
    (void) _kernel_osword(areg, (int *)xreg);
    return areg;
#endif
  case BBC_OSWRCH:	/* OSWRCH - Output a character */
    emulate_vdu(areg);
    return areg;
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

#define XBIT 0x20000		/* Mask for 'X' bit in SWI numbers */

/* Processor flag bits used in mos_sys() */

#define OVERFLOW_FLAG 1
#define CARRY_FLAG 2
#define ZERO_FLAG 4
#define NEGATIVE_FLAG 8

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
void mos_mouse(int32 values[]) {
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
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = 128;	/* Use OS_Byte 128 for this */
  regs.r[1] = x;
  regs.r[2] = x >> 8;
  oserror = _kernel_swi(OS_Byte, &regs, &regs);
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
  regs.r[1] = TOINT(name);
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
void mos_oscli(char *command, char *respfile) {
  if (respfile==NIL) {	/* Command output goes to normal place */
    basicvars.retcode = _kernel_oscli(command);
    if (basicvars.retcode<0) error(ERR_CMDFAIL, _kernel_last_oserror()->errmess);
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

/*
** 'mos_getswinum' returns the SWI number corresponding to
** SWI 'name'
*/
int32 mos_getswinum(char *name, int32 length) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  char swiname[100];
  if (length==0) length = strlen(name);
  memmove(swiname, name, length);
  swiname[length] = NUL;		/* Ensure name is null-terminated */
  regs.r[1] = TOINT(&swiname[0]);
  oserror = _kernel_swi(OS_SWINumberFromString, &regs, &regs);
  if (oserror!=NIL) error(ERR_CMDFAIL, oserror->errmess);
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
void mos_sys(int32 swino, int32 inregs[], int32 outregs[], int32 *flags) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  int n;
  for (n=0; n<10; n++) regs.r[n] = inregs[n];
  oserror = _kernel_swi_c(swino, &regs, &regs, flags);
  if (oserror!=NIL && (swino & XBIT)==0) error(ERR_CMDFAIL, oserror->errmess);
  *flags = *flags!=0 ? CARRY_FLAG : 0;
  if (oserror!=NIL) *flags+=OVERFLOW_FLAG;
  for (n=0; n<10; n++) outregs[n] = regs.r[n];
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

#else

/* ====================================================================== */
/* ================== Non-RISC OS versions of functions ================== */
/* ====================================================================== */

#if defined(TARGET_WIN32) | defined(TARGET_BCC32) | defined(TARGET_AMIGA) | defined(TARGET_MINGW)
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
** This code was supplied by Jeff Doggett
*/
int32 mos_rdtime(void) {
  unsigned int rc;
  struct timeval tv;
  struct timezone tzp;

  gettimeofday (&tv, &tzp);

  /* tv -> tv_sec = Seconds since 1970 */
  /* tv -> tv_usec = and microseconds */

  rc = tv.tv_sec & 0xFFFFFF;
  rc = rc * 100;
  rc = rc + (tv.tv_usec / 10000);
  rc = rc - startime;

  return ((int32) rc);
}

/*
** 'mos_wrtime' handles assignments to the Basic pseudo variable 'TIME'.
** The effects of 'TIME=' are emulated here
*/
void mos_wrtime (int32 time) {
  startime = time;
  startime = mos_rdtime();
}

#endif

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
  return; // Do nothing, silently.
#else
  error(ERR_UNSUPPORTED);
#endif
}

/*
** 'mos_mouse_off' turns off the mouse pointer
*/
void mos_mouse_off(void) {
#ifdef USE_SDL
  return; // Do nothing, silently.
#else
  error(ERR_UNSUPPORTED);
#endif
}

/*
** 'mos_mouse_to' moves the mouse pointer to (x,y) on the
** screen
*/
void mos_mouse_to(int32 x, int32 y) {
#ifdef USE_SDL
  return; // Do nothing, silently.
#else
  error(ERR_UNSUPPORTED);
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
  error(ERR_UNSUPPORTED);
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
  error(ERR_UNSUPPORTED);
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
  error(ERR_UNSUPPORTED);
#endif
}

/*
** 'mos_mouse' emulates the Basic 'MOUSE' statement
*/
void mos_mouse(int32 values[]) {
#ifdef USE_SDL
  get_sdl_mouse(values);
#else
  error(ERR_UNSUPPORTED);
#endif
}

/*
** 'mos_adval' emulates the Basic function 'ADVAL' which
** either returns the number of entries free in buffer 'x' or
** the current value of a device, eg A/D convertor, mouse, etc.
**
** Positive parameter - read device
** Negative parameter - read buffer
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
** -1  Keyboard buffer
** -2  Serial input buffer
** -3  Serial output buffer
** -4  Printer output buffer
** -5  Sound output buffer 0
** -6  Sound output buffer 1
** -7  Sound output buffer 2
** -8  Sound output buffer 3
** -9  Speach output buffer
** -10 Mouse input buffer
** -11 MIDI input buffer
** -12 MIDI output buffer
** -12- other buffers
*/
int32 mos_adval(int32 x) {
  int32 inputvalues[4];

  if(x>6 & x<10) {
    mos_mouse(inputvalues);
    return inputvalues[x-7];
  }
  return 0;
}

/*
** 'mos_sound_on' handles the Basic 'SOUND ON' statement
*/
void mos_sound_on(void) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
}

/*
** 'mos_sound_off' handles the Basic 'SOUND OFF' statement
*/
void mos_sound_off(void) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
}

/*
** 'mos_sound' handles the Basic 'SOUND' statement
*/
void mos_sound(int32 channel, int32 amplitude, int32 pitch, int32 duration, int32 delay) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
}

/*
** 'mos_wrbeat' emulates the Basic statement 'BEATS'
*/
void mos_wrbeat(int32 x) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
}

/*
** 'mos_rdbeat' emulates the Basic functions 'BEAT' which
** returns the current microbeat
*/
int32 mos_rdbeat(void) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
  return 0;
}

/*
** 'mos_rdbeats' emulates the Basic functions 'BEATS', which
** returns the number of microbeats in a bar
*/
int32 mos_rdbeats(void) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
  return 0;
}

/*
** 'mos_wrtempo' emulates the Basic statement version of 'TEMPO'
*/
void mos_wrtempo(int32 x) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
}

/*
** 'mos_rdtempo' emulates the Basic function version of 'TEMPO'
*/
int32 mos_rdtempo(void) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
  return 0;
}

/*
** 'mos_voice' emulates the Basic statement 'VOICE'
*/
void mos_voice(int32 channel, char *name) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
}

/*
** 'mos_voices' emulates the Basic statement 'VOICES'
*/
void mos_voices(int32 count) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
}

/*
** 'mos_stereo' emulates the Basic statement 'STEREO'
*/
void mos_stereo(int32 channels, int32 position) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
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

#elif defined(TARGET_WIN32) | defined(TARGET_BCC32) | defined(TARGET_MINGW)

/*
** 'mos_waitdelay' emulate the Basic statement 'WAIT <time>'
** This is not supported under Windows
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

#else

/*
** 'mos_waitdelay' emulate the Basic statement 'WAIT <time>'
** 'time' is the time to wait in centiseconds.
*/
void mos_waitdelay(int32 time) {
  struct timeval delay;
  if (time<=0) return;			/* Nothing to do */
  delay.tv_sec = time/100;		/* Time to wait (seconds) */
  delay.tv_usec = time%100*10000;	/* Time to wait (microseconds) */
  (void) select(0, NIL, NIL, NIL, &delay);
}

#endif
#endif


/* ======================================
 * === *commands and *command parsing ===
 * ======================================
 */

/*
 * mos_gstrans() - General String Translation
 * GSTrans convert input string, as used by *KEY, etc.
 * On entry: pointer to string to convert
 * On exit:  pointer to converted string, overwriting input string
 *           recognises |<letter> |" || !? |!
 * Bug: hello"there does not give correct result
 */
char *mos_gstrans(char *instring) {
	int quoted=0, escape=0;
	char *result, *outstring;
	char ch;

	result=outstring=instring;
	while (*instring == ' ') instring++;		// Skip spaces
	if (quoted = *instring == '"') instring++;

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
unsigned int cmd_parse_dec(char** text)
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


#ifndef TARGET_RISCOS
/*
 * List of *commands implemented by this code
 */
#define CMD_UNKNOWN		0
#define CMD_KEY			1
#define CMD_CAT			2
#define CMD_CD			3
#define CMD_QUIT		4
#define CMD_WINDOW		6
#define CMD_FX			7
#define CMD_VER			8
#define CMD_TITLE		9
#define CMD_HELP		10
#define HELP_BASIC		128
#define HELP_HOST		129
#define HELP_MOS		130

/*
 * *.(<directory>)
 * Catalog directory
 * NB, Only *. does catalog, *CAT passed to host OS
 */
void cmd_cat(char *command) {
	while (*command == ' ') command++;	// Skip spaces
#if defined(TARGET_DJGPP) | defined(TARGET_WIN32) | defined(TARGET_BCC32) | defined(TARGET_MINGW)
	system("dir");
	find_cursor();				// Figure out where the cursor has gone to
#if defined(TARGET_MINGW)
	emulate_printf("\r\n");			// Restore cursor position
#endif
#elif defined(TARGET_NETBSD) | defined(TARGET_LINUX) | defined(TARGET_MACOSX)\
 | defined(TARGET_UNIX) | defined(TARGET_FREEBSD) | defined(TARGET_OPENBSD)\
 | defined(TARGET_GNUKFREEBSD)
#ifdef USE_SDL
    FILE *sout;
    char buf;
    sout = popen("ls -l", "r");
    if (sout == NULL) error(ERR_CMDFAIL);
    echo_off();
    while (fread(&buf, 1, 1, sout) > 0) {
      if (buf == '\n') emulate_vdu('\r');
      emulate_vdu(buf);
    }
    echo_on();
    pclose(sout);
#else
	system("ls -l");
#endif
#elif defined(TARGET_AMIGA)
	system("list");
#endif
}

/*
 * *CD/*CHDIR <directory>
 * Change directory
 * Has to be an internal command as CWD is per-process
 */
void cmd_cd(char *command) {
	if (*command == 'd') command+=3;	// *CHDIR
	while (*command == ' ') command++;	// Skip spaces
	chdir(command);
#if defined(TARGET_DJGPP) | defined(TARGET_WIN32) | defined(TARGET_BCC32) | defined(TARGET_MINGW)
	find_cursor();				// Figure out where the cursor has gone to
#if defined(TARGET_MINGW)
	emulate_printf("\r\n");			// Restore cursor position
#endif
#endif
}

/*
 * *FX num(,num(,num))
 * Make OSBYTE call
 */
void cmd_fx(char *command) {
	// *FX *must* purely and simply parse its parameters and pass them to OSBYTE
	// *FX *MUST* *NOT* impose any preconceptions
	// *FX is allowed to test the returned 'unsupported' flag and give a Bad command error
	// OSBYTE *MUST* *NOT* give a Bad command error, it *MUST* purely and simply just return if unsupported
	// Yes, RISC OS gets this wrong.

	unsigned int areg=0, xreg=0, yreg=0;		// Default parameters
		
	while (*command == ' ') command++;		// Skip spaces
	if (*command == 0)
		{ error(ERR_BADSYNTAX, "FX <num> (,<num> (,<num>))"); return; }
	// Not sure why this call to error() needs a return after it, doesn't need it elsewhere
	areg=cmd_parse_dec(&command);			// Get first parameter
	if (*command == ',') command++;			// Step past any comma
	while (*command == ' ') command++;		// Skip spaces
	if (*command) {
		xreg=cmd_parse_dec(&command);		// Get second parameter
		if (*command == ',') command++;		// Step past any comma
		while (*command == ' ') command++;	// Skip spaces
		if (*command) {
			yreg=cmd_parse_dec(&command);	// Get third parameter
		}
	}

	if (mos_osbyte(areg, xreg, yreg) & 0x80000000)
		error(ERR_BADCOMMAND);
}

/*
 * *HELP - display help on topic
 */
void cmd_help(char *command)
{
	int cmd;

	while (*command == ' ') command++;		// Skip spaces
	cmd = check_command(command);

//	if (cmd == HELP_MOS) {
//		emulate_vdu(10); emulate_vdu(13);
//		cmd_ver();
//	} else {
		emulate_printf("\r\n%s\r\n", IDSTRING);
//	}
	if (cmd == HELP_BASIC) {
		emulate_printf("  Fork of Brandy BASIC\r\n", BRANDY_VERSION, BRANDY_DATE);
	}
	if (cmd == HELP_HOST || cmd == HELP_MOS) {
		emulate_printf("  CD   <dir>\n\r  FX   <num>(,<num>(,<num>))\n\r");
		emulate_printf("  KEY  <num> <string>\n\r  HELP <text>\n\r  QUIT\n\r");
//		emulate_printf("  VER\n\r");
	}
	if (*command == '.')
//		emulate_printf("  BASIC\n\r  HOST\n\r  MOS\n\r");
		emulate_printf("  BASIC\n\r  MOS\n\r");
}

/*
 * *KEY - define a function key string.
 * The string parameter is GSTransed so that '|' escape sequences
 * can be used.
 * Bug: Does not check if redefining the key currently being
 * expanded.
 * On entry, 'command' points at the start of the command.
 */
#define HIGH_FNKEY 15			/* Highest function key number */
static void cmd_key(char *command) {
	unsigned int key;

	while (*command == ' ') command++;		// Skip spaces
	if (*command == 0)
		error(ERR_BADSYNTAX, "KEY <num> (<string>");
	key=cmd_parse_dec(&command);			// Get key number
	if (key > HIGH_FNKEY)
		error(ERR_BADKEY);
	if (*command == ',') command++;			// Step past any comma

	command=mos_gstrans(command);			// Get GSTRANS string
	set_fn_string(key, command, strlen(command));
}

/*
 * *QUIT
 * Exit interpreter
 */
void cmd_quit(char *command) {
	exit_interpreter(0);
}

/*
 * check_command - Check if the command is one of the RISC OS
 * commands emulated by this code.
 * ToDo: replace with proper parser
 */
int check_command(char *text) {
  char command[12];
  int length;

  if (*text == 0) return CMD_UNKNOWN;
  if (*text == '.') return CMD_CAT;

  length = 0;
  while (length < 10 && isalpha(*text)) {
    command[length] = tolower(*text);
    length++;
    text++;
  }
  command[length] = 0;
  if (strcmp(command, "key")    == 0) return CMD_KEY;
//if (strcmp(command, "cat")    == 0) return CMD_CAT;
  if (strcmp(command, "cd")     == 0) return CMD_CD;
  if (strcmp(command, "chdir")  == 0) return CMD_CD;
  if (strcmp(command, "quit")   == 0) return CMD_QUIT;
//if (strcmp(command, "window") == 0) return CMD_WINDOW;
  if (strcmp(command, "fx")     == 0) return CMD_FX;
//if (strcmp(command, "title")  == 0) return CMD_TITLE;
  if (strcmp(command, "help")   == 0) return CMD_HELP;
  if (strcmp(command, "ver")    == 0) return CMD_VER;
  if (strcmp(command, "basic")  == 0) return HELP_BASIC;
  if (strcmp(command, "host")   == 0) return HELP_HOST;
  if (strcmp(command, "mos")    == 0) return HELP_MOS;
  if (strcmp(command, "os")     == 0) return HELP_MOS;
  return CMD_UNKNOWN;
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
void mos_oscli(char *command, char *respfile) {
  int cmd, clen;
  char *cmdbuf, *cmdbufbase;

  while (*command == ' ' || *command == '*') command++;
  if (*command == 0) return;					/* Null string */
  if (*command == (char)124 || *command == (char)221) return;	/* Comment     */
//if (*command == '\\') { }					/* Extension   */

  clen=strlen(command) + 256;
  cmdbufbase=malloc(clen);
  cmdbuf=cmdbufbase;
  strncpy(cmdbuf, command, clen-1);

  if (!basicvars.runflags.ignore_starcmd) {
/*
 * Check if command is one of the *commands implemented
 * by this code.
 */
  cmd = check_command(cmdbuf);
  if (cmd == CMD_KEY)  { cmd_key(cmdbuf+3); return; } 
  if (cmd == CMD_CAT)  { cmd_cat(cmdbuf); return; }
  if (cmd == CMD_QUIT) { cmd_quit(cmdbuf+4); return; }
  if (cmd == CMD_HELP) { cmd_help(cmdbuf+4); return; }
  if (cmd == CMD_CD)   { cmd_cd(cmdbuf+2); return; }
  if (cmd == CMD_FX)   { cmd_fx(cmdbuf+2); return; }
//if (cmd == CMD_VER)  { cmd_ver(); return; }
  }

  if (*cmdbuf == '/') {		/* Run file, so just pass to OS     */
    cmdbuf++;				/* Step past '/'                    */
    while (*cmdbuf == ' ') cmdbuf++;	/* And skip any more leading spaces */
  }

#if defined(TARGET_DJGPP) | defined(TARGET_WIN32) | defined(TARGET_BCC32) | defined(TARGET_MINGW)
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

#elif defined(TARGET_NETBSD) | defined(TARGET_LINUX) | defined(TARGET_MACOSX)\
 | defined(TARGET_UNIX) | defined(TARGET_FREEBSD) | defined(TARGET_OPENBSD)\
 | defined(TARGET_AMIGA) | defined(TARGET_GNUKFREEBSD)
/* Command is to be sent to underlying Unix-style OS */
/* This is the Unix version of the function, where both stdout
** and stderr can be redirected to a file
*/
  if (respfile == NIL) {		/* Command output goes to normal place */
#ifdef USE_SDL
    FILE *sout;
    char buf;
    strcat(cmdbuf, " 2>&1");
    sout = popen(cmdbuf, "r");
    if (sout == NULL) error(ERR_CMDFAIL);
    echo_off();
    while (fread(&buf, 1, 1, sout) > 0) {
      if (buf == '\n') emulate_vdu('\r');
      emulate_vdu(buf);
    }
    echo_on();
    pclose(sout);
#else
    fflush(stdout);			/* Make sure everything has been output */
    fflush(stderr);
    basicvars.retcode = system(cmdbuf);
    find_cursor();			/* Figure out where the cursor has gone to */
    if (basicvars.retcode < 0) error(ERR_CMDFAIL);
#endif
  } else {				/* Want response back from command */
    strcat(cmdbuf, " >");
    strcat(cmdbuf, respfile);
    strcat(cmdbuf, " 2>&1");
    basicvars.retcode = system(cmdbuf);
    find_cursor();			/* Figure out where the cursor has gone to */
    if (basicvars.retcode<0) {
      remove(respfile);
      error(ERR_CMDFAIL);
    }
  }
  free(cmdbufbase);
#else
#error There is no mos_oscli() function for this target
#endif
}


/*
** 'mos_get_swinum' returns the SWI number corresponding to
** SWI 'name'
*/
int32 mos_getswinum(char *name, int32 length) {
  error(ERR_UNSUPPORTED);
  return 0;
}

/*
** 'mos_sys' issues a SWI call and returns the result. This is
** not supported under any OS other than RISC OS
*/
void mos_sys(int32 swino, int32 inregs[], int32 outregs[], int32 *flags) {
  error(ERR_UNSUPPORTED);
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
#if defined(TARGET_WIN32) | defined(TARGET_BCC32) | defined(TARGET_MINGW)
  startime = 0;
#else
  mos_wrtime(0);
#endif
  return TRUE;
}

/*
** 'mos_final' is called to tidy up the emulation at the end
** of the run
*/
void mos_final(void) {
}


int32 mos_osbyte(int32 areg, int32 xreg, int32 yreg)
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
* OSBYTE &0A   0 Second Colour Flash Duration
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
* OSBYTE &2A  42
* OSBYTE &2B  43
* OSBYTE &2C  44
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
* OSBYTE &96 150 Read SHELIA
* OSBYTE &97 151 Write SHELIA
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
* OSBYTE &A3 163 Reserved for applications software
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
* OSBYTE &DD 221 Read/Write Intrepretation ASCII 197-207
* OSBYTE &DE 222 Read/Write Interpretation ASCII 208-223
* OSBYTE &DF 223 Read/Write Interpretation ASCII 224-239
* OSBYTE &E0 224 Read/Write Interpretation ASCII 240-255
* OSBYTE &E1 225 Read/Write Interpretation of F-Keys
* OSBYTE &E2 226 Read/Write Interpretation of Shift-F-Keys
* OSBYTE &E3 227 Read/Write Interpretation of Ctrl-F-Keys
* OSBYTE &E4 228 Read/Write Interpretation of Ctrl-Shift-Fkeys
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
{
switch (areg) {
	case 0:			// OSBYTE 0 - Return machine type
		if (xreg!=0) return MACTYPE;
		else error(ERR_MOSVERSION);
		break;

	case 128:		// OSBYTE 128 - ADVAL
		return (mos_adval((yreg << 8) | xreg) << 8) | 128;

//	case 129:		// OSBYTE 129 - INKEY

	case 130:		// OSBYTE 130 - High word of user memory
		areg = basicvars.workspace - basicvars.offbase;
		return ((areg & 0xFFFF0000) >> 8) | 130;

	case 131:		// OSBYTE 132 - Bottom of user memory
		areg = basicvars.workspace - basicvars.offbase;
		if (areg < 0xFFFF)	return (areg << 8) | 131;
		else			return ((areg & 0xFF0000) >> 16) | ((areg & 0xFFFF) << 8);

	case 132:		// OSBYTE 132 - Top of user memory
		areg = basicvars.slotend - basicvars.offbase;
		if (areg < 0xFFFF)	return (areg << 8) | 132;
		else			return ((areg & 0xFF0000) >> 16) | ((areg & 0xFFFF) << 8);

//	case 133:		// OSBYTE 133 - Read screen start for MODE - inapplicable?
//	case 134:		// OSBYTE 134 - Read POS and VPOS

	case 160:		// OSBYTE 160 - Read VDU variable
		return emulate_vdufn(xreg) << 8 | 160;

	}
if (areg < 128 && areg > 25)
	return (3 << 30) | (yreg << 16) | (0xFF00) | areg;		// Default null return
else
	return (0 << 30) | (yreg << 16) | (xreg << 8) | areg;	// Default null return
}
#endif
