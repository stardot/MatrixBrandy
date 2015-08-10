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
#include "emulate.h"
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

#ifdef USE_SDL
void sdlchar(char);
#endif

/* Address range used to identify emulated calls to the BBC Micro MOS */

#define LOW_MOS 0xFFCE
#define HIGH_MOS 0xFFF7

/* Emulated BBC MOS calls */

#define BBC_OSWRCH 0xFFEE
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
  int32 areg, xreg;
  areg = basicvars.staticvars[A_PERCENT].varentry.varinteger;
  xreg = basicvars.staticvars[X_PERCENT].varentry.varinteger;
  switch (address) {
  case BBC_OSBYTE:	/* Emulate OSBYTE 0 */
    if (areg==0 && xreg!=0) return MACTYPE;	/* OSBYTE 0 - Return machine type */
    error(ERR_UNSUPPORTED);
    break;
  case BBC_OSWRCH:	/* Emulate OSWRCH - Output a character */
    emulate_vdu(areg);
    return areg;
    break;
  default:
    error(ERR_UNSUPPORTED);
  }
  return 0;
}

/*
** 'emulate_call' deals with the Basic 'CALL' statement. This is
** unsupported except to provide limited support for the BBC MOS
** calls supported by the Acorn interpreter
*/
void emulate_call(int32 address, int32 parmcount, int32 parameters []) {
  if (parmcount==0 && address>=LOW_MOS && address<=HIGH_MOS)
    emulate_mos(address);
  else {
    error(ERR_UNSUPPORTED);
  }
}

/*
** 'emulate_usr' deals with the Basic function 'USR'. It
** provides some limited support for the BBC MOS calls
** emulated by USR in the Acorn interpreter (where the
** called address is in the range 0xFFF4 to 0xFFC5)
*/
int32 emulate_usr(int32 address) {
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

/* Processor flag bits used in emulate_sys() */

#define OVERFLOW_FLAG 1
#define CARRY_FLAG 2
#define ZERO_FLAG 4
#define NEGATIVE_FLAG 8

/*
** 'emulate_time' is called to deal with the Basic pseudo-variable
** 'TIME' to return its current value. Under RISC OS, the C function
** 'clock' returns the current value of the centisecond clock, which
** is just what we want
*/
int32 emulate_time(void) {
  return clock()-startime;
}

/*
** 'emulate_setime' handles assignments to the Basic pseudo variable 'TIME'.
** As adjusting 'TIME' is frowned upon the code emulates the effects
** of doing this
*/
void emulate_setime(int32 time) {
  startime = clock()-time;
}

/*
** 'emulate_setimedol' is called to handle assignments to the Basic
** pseudo variable 'TIME$'. This is used to set the computer's clock.
** It does not seem to be worth the effort of parsing the string, nor
** does it seem worth rejecting the assignment so this code just
** quietly ignores it
*/
void emulate_setimedol(char *time) {
}

/*
** 'emulate_mouse_on' turns on the mouse pointer
*/
void emulate_mouse_on(int32 pointer) {
  (void) _kernel_osbyte(SELECT_MOUSE, 1, 0);	/* R1 = 1 = select mouse pointer 1 */
}

/*
** 'emulate_mouse_off' turns off the mouse pointer
*/
void emulate_mouse_off(void) {
  (void) _kernel_osbyte(SELECT_MOUSE, 0, 0);	/* R1 = 0 = do not show the mouse pointer */
}

/*
** 'emulate_mouse_to' moves the mouse pointer to position (x,y) on the screen
*/
void emulate_mouse_to(int32 x, int32 y) {
  struct {byte call_number, x_lsb, x_msb, y_lsb, y_msb;} osword_parms;
  osword_parms.call_number = 3;		/* OS_Word 21 call 3 sets the mouse position */
  osword_parms.x_lsb = x & BYTEMASK;
  osword_parms.x_msb = x>>BYTESHIFT;
  osword_parms.y_lsb = y & BYTEMASK;
  osword_parms.y_msb = y>>BYTESHIFT;
  (void) _kernel_osword(CONTROL_MOUSE, TOINTADDR(&osword_parms));
}

/*
** 'emulate_mouse_step' defines the mouse movement multipliers
*/
void emulate_mouse_step(int32 xmult, int32 ymult) {
  struct {byte call_number, xmult, ymult;} osword_parms;
  osword_parms.call_number = 2;		/* OS_Word 21 call 2 defines the mouse movement multipliers */
  osword_parms.xmult = xmult;
  osword_parms.ymult = ymult;
  (void) _kernel_osword(CONTROL_MOUSE, TOINTADDR(&osword_parms));
}

/*
** 'emulate_mouse_colour' sets one of the colours used for the mouse pointer
*/
void emulate_mouse_colour(int32 colour, int32 red, int32 green, int32 blue) {
  struct {byte ptrcol, mode, red, green, blue;} osword_parms;
  osword_parms.ptrcol = colour;
  osword_parms.mode = 25;	/* Setting mouse pointer colour */
  osword_parms.red = red;
  osword_parms.green = green;
  osword_parms.blue = blue;
  (void) _kernel_osword(WRITE_PALETTE, TOINTADDR(&osword_parms));
}

/*
** 'emulate_mouse_rectangle' defines the mouse bounding box
*/
void emulate_mouse_rectangle(int32 left, int32 bottom, int32 right, int32 top) {
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
** 'emulate_mouse' returns the current position of the mouse
*/
void emulate_mouse(int32 values[]) {
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
** 'emulate_adval' emulates the Basic function 'ADVAL' which returns
** either the number of entries free in buffer 'x' or the current
** value of the A/D convertor depending on whether x<0 (free entries)
** or x>=0 (A/D convertor reading)
*/
int32 emulate_adval(int32 x) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = 128;	/* Use OS_Byte 128 for this */
  regs.r[1] = x;
  oserror = _kernel_swi(OS_Byte, &regs, &regs);
  if (oserror!=NIL) error(ERR_CMDFAIL, oserror->errmess);
  return regs.r[1]+(regs.r[2]<<BYTESHIFT);
}

/*
** 'emulate_sound_on' handles the Basic 'SOUND ON' statement
*/
void emulate_sound_on(void) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = 2;	/* Turn on the sound system */
  oserror = _kernel_swi(Sound_Enable, &regs, &regs);
  if (oserror!=NIL) error(ERR_CMDFAIL, oserror->errmess);
}

/*
** 'emulate_sound_off' handles the Basic 'SOUND OFF' statement
*/
void emulate_sound_off(void) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = 1;	/* Turn off the sound system */
  oserror = _kernel_swi(Sound_Enable, &regs, &regs);
  if (oserror!=NIL) error(ERR_CMDFAIL, oserror->errmess);
}


/*
** 'emulate_sound' handles the Basic 'SOUND' statement.
** Note that this code ignores the 'delay' parameter
*/
void emulate_sound(int32 channel, int32 amplitude, int32 pitch, int32 duration, int32 delay) {
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
** 'emulate_beats' emulates the Basic statement 'BEATS'
*/
void emulate_beats(int32 barlength) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = barlength;
  oserror = _kernel_swi(Sound_QBeat, &regs, &regs);
  if (oserror!=NIL) error(ERR_CMDFAIL, oserror->errmess);
}

/*
** 'emulate_beatfn' emulates the Basic functions 'BEAT' and 'BEATS', both
** of which appear to return the same information
*/
int32 emulate_beatfn(void) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = 0;
  oserror = _kernel_swi(Sound_QBeat, &regs, &regs);
  if (oserror!=NIL) error(ERR_CMDFAIL, oserror->errmess);
  return regs.r[0];
}

/*
** 'emulate_tempo' emulates the Basic statement 'TEMPO'
*/
void emulate_tempo(int32 x) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = x;
  oserror = _kernel_swi(Sound_QTempo, &regs, &regs);
  if (oserror!=NIL) error(ERR_CMDFAIL, oserror->errmess);
}

/*
** 'emulate_tempofn' emulates the Basic function 'TEMPO'
*/
int32 emulate_tempofn(void) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = 0;
  oserror = _kernel_swi(Sound_QTempo, &regs, &regs);
  if (oserror!=NIL) error(ERR_CMDFAIL, oserror->errmess);
  return regs.r[0];
}

/*
**'emulate_voice' attaches the voice named to a voice channel
*/
void emulate_voice(int32 channel, char *name) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = channel;
  regs.r[1] = TOINT(name);
  oserror = _kernel_swi(Sound_AttachNamedVoice, &regs, &regs);
  if (oserror!=NIL) error(ERR_CMDFAIL, oserror->errmess);
}

/*
** 'emulate_voices' handles the Basic statement 'VOICES' which
** is define to set the number of voice channels to be used
*/
void emulate_voices(int32 count) {
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

void emulate_stereo(int32 channel, int32 position) {
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
** 'emulate_waitdelay' emulate the Basic statement 'WAIT <time>'
*/
void emulate_waitdelay(int32 delay) {
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
** 'emulate_endeq' emulates the 'END=' form of the 'END' statement. This
** can be used to extend the Basic workspace to. It is not supported by
** this version of the interpreter
*/
void emulate_endeq(int32 newend) {
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
void emulate_oscli(char *command, char *respfile) {
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
** 'emulate_getswino' returns the SWI number corresponding to
** SWI 'name'
*/
int32 emulate_getswino(char *name, int32 length) {
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
** 'emulate_sys' issues a SWI call and returns the result.
** The code fakes the processor flags returned in 'flags'. The carry
** flag is returned by the call to _kernel_swi_c(). The state of the
** overflow bit can be determined by checking if the swi function
** returns null. The Acorn interpreter always seems to set the zero
** flag and the sign flag can probably be ignored.
** The layout of the flags bits are:
**  N (sign) Z (zero) C (carry) V (overflow)
*/
void emulate_sys(int32 swino, int32 inregs[], int32 outregs[], int32 *flags) {
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
** init_emulation - Called to initialise the emulation code
*/
boolean init_emulation(void) {
  startime = 0;
  return TRUE;
}

void end_emulation(void) {
}

#else

/* ====================================================================== */
/* ================== Non-RISC OS versions of functions ================== */
/* ====================================================================== */

#if defined(TARGET_WIN32) | defined(TARGET_BCC32) | defined(TARGET_AMIGA) | defined(TARGET_MINGW)
/*
** 'emulate_time' is called to deal with the Basic pseudo-variable
** 'TIME' to return its current value. This should be the current
** value of the centisecond clock, but how accurate the value is
** depends on the underlying OS.
*/
int32 emulate_time(void) {
  return (clock() - startime) * 100 / CLOCKS_PER_SEC;
}

/*
** 'emulate_setime' handles assignments to the Basic pseudo variable 'TIME'.
** The effects of 'TIME=' are emulated here
*/
void emulate_setime(int32 time) {
  startime = clock() -(time * CLOCKS_PER_SEC / 100);
}

#else
/*
** 'emulate_time' is called to deal with the Basic pseudo-variable
** 'TIME' to return its current value. This should be the current
** value of the centisecond clock, but how accurate the value is
** depends on the underlying OS.
** This code was supplied by Jeff Doggett
*/
int32 emulate_time(void) {
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
** 'emulate_setime' handles assignments to the Basic pseudo variable 'TIME'.
** The effects of 'TIME=' are emulated here
*/
void emulate_setime (int32 time) {
  startime = time;
  startime = emulate_time();
}

#endif

/*
** 'emulate_setimedol' is called to handle assignments to the Basic
** pseudo variable 'TIME$'. This is used to set the computer's clock.
** It does not seem to be worth the effort of parsing the string, nor
** does it seem worth rejecting the assignment so this code just
** quietly ignores it
*/
void emulate_setimedol(char *time) {
}

/*
** 'emulate_mouse_on' turns on the mouse pointer
*/
void emulate_mouse_on(int32 pointer) {
  error(ERR_UNSUPPORTED);
}

/*
** 'emulate_mouse_off' turns off the mouse pointer
*/
void emulate_mouse_off(void) {
  error(ERR_UNSUPPORTED);
}

/*
** 'emulate_mouse_to' moves the mouse pointer to (x,y) on the
** screen
*/
void emulate_mouse_to(int32 x, int32 y) {
  error(ERR_UNSUPPORTED);
}

/*
** 'emulate_mouse_step' changes the number of graphics units moved
** per step of the mouse to 'x' and 'y'.
*/
void emulate_mouse_step(int32 x, int32 y) {
  error(ERR_UNSUPPORTED);
}

/*
** 'emulate_mouse_colour' sets colour 'colour' of the mouse sprite
** to the specified values 'red', 'green' and 'blue'
*/
void emulate_mouse_colour(int32 colour, int32 red, int32 green, int32 blue) {
  error(ERR_UNSUPPORTED);
}

/*
** 'emulate_mouse_rectangle' restricts the mouse pointer to move
** in the rectangle defined by (left, bottom) and (right, top).
*/
void emulate_mouse_rectangle(int32 left, int32 bottom, int32 right, int32 top) {
  error(ERR_UNSUPPORTED);
}

/*
** 'emulate_mouse' emulates the Basic 'MOUSE' statement
*/
void emulate_mouse(int32 values[]) {
  error(ERR_UNSUPPORTED);
}

/*
** 'emulate_adval' emulates the Basic function 'ADVAL' which
** either returns the number of entries free in buffer 'x' or
** the current value of the A/D convertor
*/
int32 emulate_adval(int32 x) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
  return 0;
}

/*
** 'emulate_sound_on' handles the Basic 'SOUND ON' statement
*/
void emulate_sound_on(void) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
}

/*
** 'emulate_sound_off' handles the Basic 'SOUND OFF' statement
*/
void emulate_sound_off(void) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
}

/*
** 'emulate_sound' handles the Basic 'SOUND' statement
*/
void emulate_sound(int32 channel, int32 amplitude, int32 pitch, int32 duration, int32 delay) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
}

/*
** 'emulate_beats' emulates the Basic statement 'BEATS'
*/
void emulate_beats(int32 x) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
}

/*
** 'emulate_beatfn' emulates the Basic functions 'BEAT' and 'BEATS', both
** of which appear to return the same information
*/
int32 emulate_beatfn(void) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
  return 0;
}

/*
** 'emulate_tempo' emulates the Basic statement version of 'TEMPO'
*/
void emulate_tempo(int32 x) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
}

/*
** 'emulate_tempofn' emulates the Basic function version of 'TEMPO'
*/
int32 emulate_tempofn(void) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
  return 0;
}

/*
** 'emulate_voice' emulates the Basic statement 'VOICE'
*/
void emulate_voice(int32 channel, char *name) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
}

/*
** 'emulate_voices' emulates the Basic statement 'VOICES'
*/
void emulate_voices(int32 count) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
}

/*
** 'emulate_stereo' emulates the Basic statement 'STEREO'
*/
void emulate_stereo(int32 channels, int32 position) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
}

/*
** 'emulate_endeq' emulates the 'END=' form of the 'END' statement. This
** can be used to extend the Basic workspace to. It is not supported by
** this version of the interpreter
*/
void emulate_endeq(int32 newend) {
  error(ERR_UNSUPPORTED);
}

#if defined(TARGET_DJGPP)
/*
** 'emulate_waitdelay' emulate the Basic statement 'WAIT <time>'
** 'time' is the time to wait in centiseconds.
** Note that the actual delay is about 20% longer than the
** time specified.
*/
void emulate_waitdelay(int32 time) {
  if (time <= 0) return		/* Nothing to do */
  delay(time * 10);		/* delay() takes the time in ms */
}

#elif defined(TARGET_WIN32) | defined(TARGET_BCC32) | defined(TARGET_MINGW)

/*
** 'emulate_waitdelay' emulate the Basic statement 'WAIT <time>'
** This is not supported under Windows
*/
void emulate_waitdelay(int32 time) {
  error(ERR_UNSUPPORTED);	/* Not supported under windows */
}

#elif defined(TARGET_AMIGA)

/*
** 'emulate_waitdelay' emulate the Basic statement 'WAIT <time>'
** 'time' is the time to wait in centiseconds.
*/
void emulate_waitdelay(int32 time) {
  if (time<=0) return;		/* Nothing to do */
  Delay(time*2);		/* Delay() takes the time in 1/50 s */
}

#else

/*
** 'emulate_waitdelay' emulate the Basic statement 'WAIT <time>'
** 'time' is the time to wait in centiseconds.
*/
void emulate_waitdelay(int32 time) {
  struct timeval delay;
  if (time<=0) return;			/* Nothing to do */
  delay.tv_sec = time/100;		/* Time to wait (seconds) */
  delay.tv_usec = time%100*10000;	/* Time to wait (microseconds) */
  (void) select(0, NIL, NIL, NIL, &delay);
}

#endif

/*
 * List of RISC OS commands emulated by this code
 */
#define CMD_UNKNOWN 0
#define CMD_KEY 1

#define HIGH_FNKEY 15		/* Highest funcrion key number */
/*
 * emulate_key - emulate the 'key' command to define a function
 * key string. On entry 'command' points at the start of the
 * command name. The '|' escape sequence is partially supported,
 * mainly so that '|m' can be used at the end of the line. There
 * ar a couple of other limitations: firstly, it does not allow
 * null strings and, secondly, it does not check for silly
 * commands where the string is a 'key' command that redefines
 * the key just pressed.
 * On entry, 'command' points at the start of the command.
 */
static void emulate_key(char *command) {
  int key, length, quoted;
  char ch, text[256]; 
  command+=3;		/* Skip the word 'key' */
/* Find the key number */
  do
    command++;
  while (isspace(*command));
  if (!isdigit(*command)) error(ERR_EMUCMDFAIL, "Key number is missing");
  length = 0;
  key = 0;
  do {
    key = key * 10 + *command - '0';
    if (key > HIGH_FNKEY) error(ERR_EMUCMDFAIL, "Key number is outside range 0 to 15");
    command++;
  } while (isdigit(*command));
  while (isspace(*command)) command++;
  if (*command == 0) error(ERR_EMUCMDFAIL, "Key string is missing");
  quoted = *command == '"';
  if (quoted) command++;
  while (length < 255) {
    ch = *command;
    if ((quoted && ch == '"') || ch == 0) break;
    command++;
/*
 * Check for '|' escape character. The code has to check for
 * both the ASCII | (code 124) and Windows version Ý (code 221) 
 * hence the stupid check for the same character twice.
 * Note that the decoding of escape characters is incomplete
 */
    if (ch == '|' || ch == 'Ý') {
      ch = *command;
      command++;
      if (ch == 0) error(ERR_EMUCMDFAIL, "Character missing after '|' in key string");
      if (isalpha(ch) || ch == '@') ch = toupper(ch) - '@';
    }
    text[length] = ch;
    length++;
  }
  text[length] = 0;
  set_fn_string(key, text, length);
}

/*
 * check_command - Check if the command is one of the RISC OS
 * commands emulated by this code. At the moment only the
 * 'key' command is supported
 */
static int check_command(char *text) {
  char command[12];
  int length;
  if (*text == 0) return CMD_UNKNOWN;
  length = 0;
  while (length < 10 && isalnum(*text)) {
    command[length] = tolower(*text);
    length++;
    text++;
  }
  command[length] = 0;
  if (strcmp(command, "key") == 0) return CMD_KEY;
  return CMD_UNKNOWN;
}

#if defined(TARGET_DJGPP) | defined(TARGET_WIN32) | defined(TARGET_BCC32) | defined(TARGET_MINGW)
/*
** emulate_oscli issues the operating system command 'command'.
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
** This is the DOS version of the function. Under DOS only stdout
** can be redirected to a file.
*/
void emulate_oscli(char *command, char *respfile) {
  int cmd;
  while (*command == ' ' || *command == '*') command++;
  if (!basicvars.runflags.ignore_starcmd) {
/*
 * Check if command is one of the RISC OS commands emulated
 * by this code. Only 'key' is supported at present
 */
    cmd = check_command(command);
    if (cmd == CMD_KEY) {
      emulate_key(command);
      return;
    }
  }

/* Command is to be sent to underlying OS */

  if (respfile==NIL) {	/* Command output goes to normal place */
    basicvars.retcode = system(command);
    find_cursor();	/* Figure out where the cursor has gone to */
    if (basicvars.retcode < 0) error(ERR_CMDFAIL);
  }
  else {	/* Want response back from command */
    strcat(command, " >");
    strcat(command, respfile);
    basicvars.retcode = system(command);
    find_cursor();	/* Figure out where the cursor has gone to */
    if (basicvars.retcode < 0) {
      remove(respfile);
      error(ERR_CMDFAIL);
    }
  }
}

#elif defined(TARGET_NETBSD) | defined(TARGET_LINUX) | defined(TARGET_MACOSX)\
 | defined(TARGET_UNIX) | defined(TARGET_FREEBSD) | defined(TARGET_OPENBSD)\
 | defined(TARGET_AMIGA) | defined(TARGET_GNUKFREEBSD)
/*
** emulate_oscli issued the operating system command 'command'.
** 'respfile' is set to NIL if the command output is to displayed
** in the normal way. If it is not NIL, the command output is
** redirected to it. The command can be preceded by any number
** of blanks and '*'s so these are removed first
**
** Brandy 1.15 (and later) supports built-in '*' commands. This
** is controlled by the flag runflags.ignore_starcmd. The program
** passes all commands to the underlying OS if it is set, otherwise
** the command is checked to see if it is one the program emulates
** and if so this program handles it. Unrecognised commands are
** passed to the OS unchanged.
**
** Note that it is assumed that the buffer holding the command
** string is large enough to have the redirection commands
** appended to it. (basicvars.stringwork is used for this and so
** it should not be a problem unless the size of stringwork is
** drastically reduced.)
**
** This is the Unix version of the function, where both stdout
** and stderr can be redirected to a file
*/
void emulate_oscli(char *command, char *respfile) {
  int cmd;
  while (*command == ' ' || *command == '*') command++;
  if (!basicvars.runflags.ignore_starcmd) {
/*
 * Check if command is one of the RISC OS commands emulated
 * by this code. Only 'key' is supported at present
 */
    cmd = check_command(command);
    if (cmd == CMD_KEY) {
      emulate_key(command);
      return;
    }
  }

/* Command is to be sent to underlying OS */

  if (respfile == NIL) {	/* Command output goes to normal place */
#ifdef USE_SDL
    {
    FILE *sout;
    char buf;
    strcat(command, " 2>&1");
    sout = popen(command, "r");
    if (sout == NULL) error(ERR_CMDFAIL);
    echo_off();
    while (fread(&buf, 1, 1, sout) > 0) {
      if (buf == '\n') emulate_vdu('\r');
      emulate_vdu(buf);
    }
    echo_on();
    pclose(sout);
    }
#else
    fflush(stdout);	/* Make sure everything has been output */
    fflush(stderr);
    basicvars.retcode = system(command);
    find_cursor();	/* Figure out where the cursor has gone to */
    if (basicvars.retcode < 0) error(ERR_CMDFAIL);
#endif
  }
  else {	/* Want response back from command */
    strcat(command, " >");
    strcat(command, respfile);
    strcat(command, " 2>&1");
    basicvars.retcode = system(command);
    find_cursor();	/* Figure out where the cursor has gone to */
    if (basicvars.retcode<0) {
      remove(respfile);
      error(ERR_CMDFAIL);
    }
  }
}
#else
#error There is no emulate_oscli() function for this target
#endif

/*
** 'emulate_get_swino' returns the SWI number corresponding to
** SWI 'name'
*/
int32 emulate_getswino(char *name, int32 length) {
  error(ERR_UNSUPPORTED);
  return 0;
}

/*
** 'emulate_sys' issues a SWI call and returns the result. This is
** not supported under any OS other than RISC OS
*/
void emulate_sys(int32 swino, int32 inregs[], int32 outregs[], int32 *flags) {
  error(ERR_UNSUPPORTED);
}

/*
** 'init_emulation' is called to initialise the RISC OS emulation
** code for the versions of this program that do not run under
** RISC OS. The function returns 'TRUE' if initialisation was okay
** or 'FALSE' if it failed (in which case it is not safe for the
** interpreter to run)
*/

#if defined(TARGET_WIN32) | defined(TARGET_BCC32) | defined(TARGET_MINGW)
boolean init_emulation(void) {
  (void) clock();	/* This might be needed to start the clock */
  startime = 0;
  return TRUE;
}

#else

boolean init_emulation(void) {
  (void) clock();	/* This might be needed to start the clock */
  emulate_setime(0);
  return TRUE;
}

#endif
/*
** 'end_emulation' is called to tidy up the emulation at the end
** of the run
*/
void end_emulation(void) {
}

#endif

