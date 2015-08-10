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
**	This file contains definitions of constants, types and
**	variables used by all the VDU driver emulation files
*/

#ifndef __scrcommon_h
#define __scrcommon_h

#include "common.h"

#define MAXBYTES	9	/* VDU commands need at most nine bytes of data */

/*
** Screen output modes of operation are as follows:
** NOGRAPHICS	No graphics possible at all
** TEXTONLY	RISC OS screen mode does not support graphics
** TEXTMODE	Mode support graphics but output is to text mode screen
** FULLSCREEN	All output is to a graphics screen
*/
typedef enum {NOGRAPHICS, TEXTONLY, TEXTMODE, FULLSCREEN} graphics;

/*
** Cursor type:
** BLOCK	A block is used as the cursor
** UNDERLINE	A _ is being used as the cursor
** 'curstate' is used to control whether the cursor should be drawn or
** not in graphics modes.
*/
typedef enum {BLOCK, UNDERLINE} curstype;

/*
** Cursor display states:
** NOCURSOR	Graphics text cursor is not being used (not in graphics mode)
** HIDDEN	Graphics text cursor turned off by VDU 23,1,0
** SUSPENDED	Graphics text cursor is not being displayed
** ONSCREEN	Graphics text cursor is being displayed
*/
typedef enum {NOCURSOR, HIDDEN, SUSPENDED, ONSCREEN} curstate;

typedef enum {SCROLL_UP, SCROLL_DOWN} updown;

typedef struct {
  int32 xres;			/* RISC OS screen width in pixels */
  int32 yres;			/* RISC OS screen height in pixels */
  int32 coldepth;		/* Number of colours */
  int32 xgraphunits;		/* Screen width in RISC OS graphics units */
  int32 ygraphunits;		/* Screen height in RISC OS graphics units */
  int32 xtext;			/* Screen width in characters */
  int32 ytext;			/* Screen height in characters */
  int32 xscale;			/* X direction scale factor for points */
  int32 yscale;			/* Y direction scale factor for points */
  boolean graphics;		/* TRUE if this mode supports graphics */
} modetails;

/*
** The mode details table gives details of all the numbered RISC OS
** screen modes
*/
static modetails modetable [] = {
/*  0 */  { 640, 256,   2, 1280,  1024,  80, 32, 1, 2,  TRUE},
/*  1 */  { 320, 256,   4, 1280,  1024,  40, 32, 2, 2,  TRUE},
/*  2 */  { 160, 256,  16, 1280,  1024,  20, 32, 4, 2,  TRUE},
/*  3 */  {   0,   0,   2,    0,     0,  80, 25, 1, 2, FALSE},
/*  4 */  { 320, 256,   2, 1280,  1024,  40, 32, 2, 2,  TRUE},
/*  5 */  { 160, 256,   4, 1280,  1024,  20, 32, 4, 2,  TRUE},
/*  6 */  {   0,   0,   2,    0,     0,  40, 25, 2, 2, FALSE},
/*  7 */  {   0,   0,  16,    0,     0,  40, 25, 2, 2, FALSE},
/*  8 */  { 640, 256,   4, 1280,  1024,  80, 32, 1, 2,  TRUE},
/*  9 */  { 320, 256,  16, 1280,  1024,  40, 32, 2, 2,  TRUE},
/* 10 */  { 160, 256, 256, 1280,  1024,  20, 32, 4, 2,  TRUE},
/* 11 */  { 640, 250,   4, 1280,  1000,  80, 25, 1, 2,  TRUE},
/* 12 */  { 640, 256,  16, 1280,  1024,  80, 32, 1, 2,  TRUE},
/* 13 */  { 320, 256, 256, 1280,  1024,  40, 32, 2, 2,  TRUE},
/* 14 */  { 640, 250,  16, 1280,  1000,  80, 32, 1, 2,  TRUE},
/* 15 */  { 640, 256, 256, 1280,  1024,  80, 32, 1, 2,  TRUE},
/* 16 */  {1056, 256,  16, 2112,  1024, 132, 32, 1, 2,  TRUE},
/* 17 */  {1056, 250,  16, 2112,  1000, 132, 32, 1, 2,  TRUE},
/* 18 */  { 640, 512,   2, 1280,  1024,  80, 64, 1, 1,  TRUE},
/* 19 */  { 640, 512,   4, 1280,  1024,  80, 64, 1, 1,  TRUE},
/* 20 */  { 640, 512,  16, 1280,  1024,  80, 64, 1, 1,  TRUE},
/* 21 */  { 640, 512, 256, 1280,  1024,  80, 64, 1, 1,  TRUE},
/* 22 */  { 768, 288,  16,  768,   576,  96, 36, 1, 2,  TRUE},
/* 23 */  {1152, 896,   2, 2304,  1792, 144, 56, 1, 1,  TRUE},
/* 24 */  {1056, 256, 256, 2112,  1024, 132, 32, 1, 2,  TRUE},
/* 25 */  { 640, 480,   2, 1280,   960,  80, 60, 1, 1,  TRUE},
/* 26 */  { 640, 480,   4, 1280,   960,  80, 60, 1, 1,  TRUE},
/* 27 */  { 640, 480,  16, 1280,   960,  80, 60, 1, 1,  TRUE},
/* 28 */  { 640, 480, 256, 1280,   960,  80, 60, 1, 1,  TRUE},
/* 29 */  { 800, 600,   2, 1280,  1200, 100, 75, 1, 1,  TRUE},
/* 30 */  { 800, 600,   4, 1600,  1200, 100, 75, 1, 1,  TRUE},
/* 31 */  { 800, 600,  16, 1600,  1200, 100, 75, 1, 1,  TRUE},
/* 32 */  { 800, 600, 256, 1600,  1200, 100, 75, 1, 1,  TRUE},
/* 33 */  { 768, 288,   2, 1536,  1152,  96, 36, 1, 2,  TRUE},
/* 34 */  { 768, 288,   4, 1536,  1152,  96, 36, 1, 2,  TRUE},
/* 35 */  { 768, 288,  16, 1536,  1152,  96, 36, 1, 2,  TRUE},
/* 36 */  { 768, 288, 256, 1536,  1152,  96, 36, 1, 2,  TRUE},
/* 37 */  { 896, 352,   2, 1792,  1408, 112, 44, 1, 1,  TRUE},
/* 38 */  { 896, 352,   4, 1792,  1408, 112, 44, 1, 1,  TRUE},
/* 39 */  { 896, 352,  16, 1792,  1408, 112, 44, 1, 1,  TRUE},
/* 40 */  { 896, 352, 256, 1792,  1408, 112, 44, 1, 1,  TRUE},
/* 41 */  { 640, 352,   2, 1280,  1408,  80, 44, 1, 1,  TRUE},
/* 42 */  { 640, 352,   4, 1280,  1408,  80, 44, 1, 1,  TRUE},
/* 43 */  { 640, 352,  16, 1280,  1408,  80, 44, 1, 1,  TRUE},
/* 44 */  { 640, 200,   2, 1280,   800,  80, 25, 1, 2,  TRUE},
/* 45 */  { 640, 200,   4, 1280,   800,  80, 25, 1, 2,  TRUE},
/* 46 */  { 640, 200,  16, 1280,   800,  80, 25, 1, 2,  TRUE}
};

#ifndef __riscos
static int32 vdubytes [] = {	/* Number of bytes of data needed for each VDU command */
  0, 1, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 1, 2, 5, 0, 0, 1, 9,
  8, 5, 0, 0, 4, 4, 0, 2
};
#endif

#define MODEMASK 0x7F		/* Mode numbers are in the range 0..127 */
#define HIGHMODE 46		/* Standard RISC OS 3.1 modes are in the range 0..46 */
				/* Modes 47..63 are undefined, as are user modes 64..127 */
#define USERMODE 127		/* Pretend mode used before specific mode given */
#define COL256MASK 0x3F		/* Mask to extract colour number in 256 colour modes */
#define COL256SHIFT 2		/* Shift to move colour number to make room for tint */
#define TEXTCOLMASK 0x0F	/* Mask to limit logical colour number when not in full screen mode */

#define DEL 0x7F		/* ASCII code for the 'delete' character */

/* Command values to set 'tint' options and so on used in VDU 23,17 */

#define TINT_FORETEXT 0
#define TINT_BACKTEXT 1
#define TINT_FOREGRAPH 2
#define TINT_BACKGRAPH 3
#define EXCH_TEXTCOLS 5

#define MAXTINT 3		/* Highest value for TINT */
#define TINTMASK 0xC0		/* Mask to extract TINT value */
#define TINTSHIFT 6		/* Shift to move TINT value to least significant bits of byte */
#define C256_REDBIT 0x02	/* Mask for most sig bit of red component in 256 colour colour number */
#define C256_GREENBIT 0x08
#define C256_BLUEBIT 0x20

#ifndef __riscos

/* Variables for basic VDU driver operation and RISC OS text output */


static int32
  vducmd,			/* Current VDU command */
  vdunext,			/* Index of next entry in VDU queue */
  vduneeded,			/* Number of bytes needed for current VDU command */
  screenmode,			/* Current screen mode */
  colourdepth,			/* Number of colours allowed in current screen mode */
  colourmask,			/* Mask to isolate logical colour number */
  text_forecol,			/* Current text foreground logical colour number */
  text_backcol,			/* Current text background logical colour number */
  text_physforecol,		/* Current text foreground physical colour number */
  text_physbackcol,		/* Current text background physical colour number */
  text_foretint,		/* Tint value added to foreground text colour in 256 colour modes */
  text_backtint,		/* Tint value added to background text colour in 256 colour modes */
#ifndef USE_SDL
  realwidth,			/* Width of the real screen in characters */
  realheight,			/* Height of the real screen in characters */
#endif
  textwidth,			/* Width of current screen mode in characters */
  textheight,			/* Height of current screen mode in characters */
  twinleft,			/* Column number of left-hand side of text window */
  twinright,			/* Column number of right-hand side of text window */
  twintop,			/* Row number of top of text window */
  twinbottom,			/* Row number of bottom of text window */
  xtext,			/* Text cursor X coordinate (real on-screen location) */
  ytext;			/* Text cursor Y coordinate (real on-screen location) */

static boolean
  enable_vdu,			/* TRUE if VDU driver is enable */
  enable_print,			/* TRUE if sending characters to the printer stream is enabled */
  echo,				/* TRUE if character should be echoed on screen immediately */
  textwin;			/* TRUE if a text window has been defined */

static curstype cursmode;	/* Type of cursor being displayed in graphics mode */

static curstate cursorstate;	/* Whether cursor is shown */

static byte vduqueue[MAXBYTES];	/* Queue to hold data for VDU commands */

/*
** The logical-to-physical table maps the RISC OS logical colours to
** RISC OS physical colours. This is used for 2, 4 and 16 colour modes.
** Under operating systems other than RISC OS a second level of mapping
** is required to go from RISC OS physical colour numbers to their
** equivalents under the other OSes.
*/
static int32 logtophys[16];

#endif

#endif

