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
**	This file contains the VDU driver emulation for the interpreter
**	used where both text and graphics output are possible. It is
**	specific to the DOS version of the interpreter
**
*/
/*
** Crispian Daniels August 21st 2002:
**	Included Mac OS X target in conditional compilation.
**	Changed switch_text() so it isn't misled by setup_mode()'s reinitialisation of 'scaled'.
**	Stopped VDU23 changing cursorstate to HIDDEN in text mode, because VDU4 would respond by calling the fullscreen-mode toggle_cursor function.
** Crispian Daniels August 26th 2002:
**	Changed VDU23 so it removes the cursor if it is ONSCREEN before making it HIDDEN.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "common.h"
#include "target.h"
#include "errors.h"
#include "basicdefs.h"
#include "scrcommon.h"
#include "screen.h"
#include "jlib.h"

#if defined(TARGET_WIN32) | defined(TARGET_DJGPP) | defined(TARGET_BCC32) | defined(TARGET_MACOSX)
#include "conio.h"
#else
#error This code uses functions in the DOS 'conio' console I/O library
#endif

/*
** Notes
** -----
**  This is one of the three versions of the VDU driver emulation.
**  It is used by versions of the interpreter where graphics are
**  supported as well as text output.
**  The four versions of the VDU driver code are in:
**	riscos.c
**	textgraph.c
**	textonly.c
**	simpletext.c
**
**  Graphics support for operating systems other than RISC OS is
**  provided using the platform-independent library 'jlib'.
**
**  Text output uses the functions in the DOS library 'conio'. This
**  means that the code in here is really only suitable for use with
**  DOS. The file 'textonly.c' is different in that it can use
**  either conio or ANSI control sequences.
**
**  The most important functions are 'emulate_vdu' and 'emulate_plot'.
**  All text output and any VDU commands go via emulate_vdu. Graphics
**  commands go via emulate_plot. emulate_vdu corresponds to the
**  SWI OS_WriteC and emulate_plot to OS_Plot.
**
**  The program emulates RISC OS graphics in screen modes 0 to
**  46 (the RISC OS 3.1 modes). Both colour and grey scale graphics
**  are possible. Effectively this code supports RISC OS 3.1
**  (Archimedes) graphics with some small extensions.
**
**  The graphics library used, jlib, limits the range of RISC OS
**  graphics facilities supported quite considerably. The graphics
**  are written to a virtual screen with a resolution of 800 by 600.
**  RISC OS screen modes that are smaller than this, for example,
**  mode 1 (320 by 256), are scaled to make better use of this. What
**  actually happens in these screen modes is that the output is
**  written to a second virtual screen of a size more appropriate
**  for the screen mode, for example, in mode 1, text and graphics
**  go to a screen with a resolution of 320 by 256. When displaying
**  the output the second virtual screen is copied to the first
**  one, scaling it to fit to first one. In the case of mode 1,
**  everything is scaled by a factor of two in both the X and Y
**  directions (so that the displayed screen is 640 pixels by 512
**  in size).
**
**  To display the graphics the virtual screen has to be copied to
**  the real screen. The code attempts to optimise this by only
**  copying areas of the virtual screen that have changed.
*/

static buffer_rec
  *vscreen,			/* Virtual screen used for graphics */
  *modescreen;			/* Buffer used when screen mode is scaled to fit real screen */

static UBYTE *palette;		/* Pointer to palette for screen */

static int32
  vscrwidth,			/* Width of virtual screen in pixels */
  vscrheight,			/* Height of virtual screen in pixels */
  screenwidth,			/* RISC OS width of current screen mode in pixels */
  screenheight,			/* RISC OS height of current screen mode in pixels */
  xgraphunits,			/* Screen width in RISC OS graphics units */
  ygraphunits,			/* Screen height in RISC OS graphics units */
  gwinleft,			/* Left coordinate of graphics window in RISC OS graphics units */
  gwinright,			/* Right coordinate of graphics window in RISC OS graphics units */
  gwintop,			/* Top coordinate of graphics window in RISC OS graphics units */
  gwinbottom,			/* Bottom coordinate of graphics window in RISC OS graphics units */
  xgupp,			/* RISC OS graphic units per pixel in X direction */
  ygupp,			/* RISC OS graphic units per pixel in Y direction */
  graph_fore_action,		/* Foreground graphics PLOT action (ignored) */
  graph_back_action,		/* Background graphics PLOT action (ignored) */
  graph_forecol,		/* Current graphics foreground logical colour number */
  graph_backcol,		/* Current graphics background logical colour number */
  graph_physforecol,		/* Current graphics foreground physical colour number */
  graph_physbackcol,		/* Current graphics background physical colour number */
  graph_foretint,		/* Tint value added to foreground graphics colour in 256 colour modes */
  graph_backtint,		/* Tint value added to background graphics colour in 256 colour modes */
  xlast,			/* Graphics X coordinate of last point visited */
  ylast,			/* Graphics Y coordinate of last point visited */
  xlast2,			/* Graphics X coordinate of last-but-one point visited */
  ylast2,			/* Graphics Y coordinate of last-but-one point visited */
  xorigin,			/* X coordinate of graphics origin */
  yorigin,			/* Y coordinate of graphics origin */
  xscale,			/* X direction scale factor */
  yscale,			/* Y direction scale factor */
  xoffset,			/* X offset to centre screen mode in 800 by 600 screen in pixels */
  yoffset,			/* Y offset to centre screen mode in 800 by 600 screen in pixels */
  xbufoffset,			/* X offset in screen buffer used for plotting graphics */
  ybufoffset;			/* Y offset in screen buffer used for plotting graphics */

/*
** Note:
** xoffset, yoffset, xbufoffset and ybufoffset are used as follows:
** xoffset and yoffset give the offset in pixels of the top left-hand
** corner of the graphics area being displayed from the top left-hand
** corner of the virtual screen. The virtual screen has a resolution
** of 800 by 600 pixels and most of the RISC OS screen modes have a
** resolution (in pixels) of less than this. Mode 27, for example, is
** 640 by 480 pixels. The mode 27 screen is moved in and down by xoffset
** and yoffset pixels respectively to centre the graphics area within the
** virtual screen.
** 'xbufoffset' and 'ybufoffset' are related to 'xoffset' and 'yoffset'.
** They specify the offsets in pixels in the X and Y directions of the
** top left-hand corner of the rectangle in the virtual screen or the
** buffer in which graphics are plotted. There are two cases here:
**	1) The virtual screen is being used directly for graphics
**	2) Graphics are plotted to a second buffer first
** In the first case the same buffer is used for both plotting the
** graphics and displaying them. 'xbufoffset' and 'ybufoffset' will be
** the same as 'xoffset' and 'yoffset'. In the second case, the graphics
** are plotted in one buffer and then copied to the virtual screen
** buffer to be displayed. Here 'xbufoffset' and 'ybufoffset' refer to
** the buffer used for plotting whilst 'xoffset' and 'yoffset' refer to
** the virtual screen.
*/

static boolean
  scaled,			/* TRUE if screen mode is scaled to fit real screen */
  vdu5mode,			/* TRUE if text output goes to graphics cursor */
  clipping;			/* TRUE if clipping region is not full screen of a RISC OS mode */

/*
** These two macros are used to convert from RISC OS graphics coordinates to
** jlib pixel coordinates
*/
#define GXTOPX(x) ((x) / xgupp + xbufoffset)
#define GYTOPY(y) ((ygraphunits - 1 -(y)) / ygupp + ybufoffset)

static graphics graphmode;	/* Says whether graphics are possible or not */

/*
** Conio colour numbers. The conio colour table maps the RISC OS physical
** colour numbers to those used by conio in 2, 4 and 16 colour modes
*/
static byte colourmap [] = {
  BLACK, LIGHTRED, LIGHTGREEN, YELLOW, LIGHTBLUE, LIGHTMAGENTA,
  LIGHTCYAN, WHITE, DARKGRAY, RED, GREEN, BROWN, BLUE, MAGENTA,
  CYAN, LIGHTGRAY
};

/*
** Built-in ISO Latin-1 font for graphics mode. The first character
** in the table is a blank.
*/
static byte sysfont [224][8] = {
/*   */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ! */  {0x18u, 0x18u, 0x18u, 0x18u, 0x18u, 0u, 0x18u, 0u},
/* " */  {0x6cu, 0x6cu, 0x6cu, 0u, 0u, 0u, 0u, 0u},
/* # */  {0x6cu, 0x6cu, 0xfeu, 0x6cu, 0xfeu, 0x6cu, 0x6cu, 0u},
/* $ */  {0x18u, 0x3eu, 0x78u, 0x3cu, 0x1eu, 0x7cu, 0x18u, 0u},
/* % */  {0x62u, 0x66u, 0x0cu, 0x18u, 0x30u, 0x66u, 0x46u, 0u},
/* & */  {0x70u, 0xd8u, 0xd8u, 0x70u, 0xdau, 0xccu, 0x76u, 0u},
/* ' */  {0x0cu, 0x0cu, 0x18u, 0u, 0u, 0u, 0u, 0u},
/* ( */  {0x0cu, 0x18u, 0x30u, 0x30u, 0x30u, 0x18u, 0x0cu, 0u},
/* ) */  {0x30u, 0x18u, 0x0cu, 0x0cu, 0x0cu, 0x18u, 0x30u, 0u},
/* * */  {0x44u, 0x6cu, 0x38u, 0xfeu, 0x38u, 0x6cu, 0x44u, 0u},
/* + */  {0u, 0x18u, 0x18u, 0x7eu, 0x18u, 0x18u, 0u, 0u},
/* , */  {0u, 0u, 0u, 0u, 0u, 0x18u, 0x18u, 0x30u},
/* - */  {0u, 0u, 0u, 0xfeu, 0u, 0u, 0u, 0u},
/* . */  {0u, 0u, 0u, 0u, 0u, 0x18u, 0x18u, 0u},
/* / */  {0u, 0x6u, 0x0cu, 0x18u, 0x30u, 0x60u, 0u, 0u},
/* 0 */  {0x7cu, 0xc6u, 0xceu, 0xd6u, 0xe6u, 0xc6u, 0x7cu, 0u},
/* 1 */  {0x18u, 0x38u, 0x18u, 0x18u, 0x18u, 0x18u, 0x7eu, 0u},
/* 2 */  {0x7cu, 0xc6u, 0x0cu, 0x18u, 0x30u, 0x60u, 0xfeu, 0u},
/* 3 */  {0x7cu, 0xc6u, 0x6u, 0x1cu, 0x6u, 0xc6u, 0x7cu, 0u},
/* 4 */  {0x1cu, 0x3cu, 0x6cu, 0xccu, 0xfeu, 0x0cu, 0x0cu, 0u},
/* 5 */  {0xfeu, 0xc0u, 0xfcu, 0x6u, 0x6u, 0xc6u, 0x7cu, 0u},
/* 6 */  {0x3cu, 0x60u, 0xc0u, 0xfcu, 0xc6u, 0xc6u, 0x7cu, 0u},
/* 7 */  {0xfeu, 0x6u, 0x0cu, 0x18u, 0x30u, 0x30u, 0x30u, 0u},
/* 8 */  {0x7cu, 0xc6u, 0xc6u, 0x7cu, 0xc6u, 0xc6u, 0x07cu, 0u},
/* 9 */  {0x7cu, 0xc6u, 0xc6u, 0x7eu, 0x6u, 0x0cu, 0x78u, 0u},
/* : */  {0u, 0u, 0x18u, 0x18u, 0u, 0x18u, 0x18u, 0u},
/* ; */  {0u, 0u, 0x18u, 0x18u, 0u, 0x18u, 0x18u, 0x30u},
/* < */  {0x6u, 0x1cu, 0x70u, 0xc0u, 0x70u, 0x1cu, 0x6u, 0u},
/* = */  {0u, 0u, 0xfeu, 0u, 0xfeu, 0u, 0u, 0u},
/* > */  {0xc0u, 0x70u, 0x1cu, 0x6u, 0x1cu, 0x70u, 0xc0u, 0u},
/* ? */  {0x7cu, 0xc6u, 0xc6u, 0x0cu, 0x18u, 0u, 0x18u, 0u},
/* @ */  {0x7cu, 0xc6u, 0xdeu, 0xd6u, 0xdcu, 0xc0u, 0x7cu, 0u},
/* A */  {0x7cu, 0xc6u, 0xc6u, 0xfeu, 0xc6u, 0xc6u, 0xc6u, 0u},
/* B */  {0xfcu, 0xc6u, 0xc6u, 0xfcu, 0xc6u, 0xc6u, 0xfcu, 0u},
/* C */  {0x7cu, 0xc6u, 0xc0u, 0xc0u, 0xc0u, 0xc6u, 0x7cu, 0u},
/* D */  {0xf8u, 0xccu, 0xc6u, 0xc6u, 0xc6u, 0xccu, 0xf8u, 0u},
/* E */  {0xfeu, 0xc0u, 0xc0u, 0xfcu, 0xc0u, 0xc0u, 0xfeu, 0u},
/* F */  {0xfeu, 0xc0u, 0xc0u, 0xfcu, 0xc0u, 0xc0u, 0xc0u, 0u},
/* G */  {0x7cu, 0xc6u, 0xc0u, 0xceu, 0xc6u, 0xc6u, 0x7cu, 0u},
/* H */  {0xc6u, 0xc6u, 0xc6u, 0xfeu, 0xc6u, 0xc6u, 0xc6u, 0u},
/* I */  {0x7eu, 0x18u, 0x18u, 0x18u, 0x18u, 0x18u, 0x7eu, 0u},
/* J */  {0x3eu, 0x0cu, 0x0cu, 0x0cu, 0x0cu, 0xccu, 0x78u, 0u},
/* K */  {0xc6u, 0xccu, 0xd8u, 0xf0u, 0xd8u, 0xccu, 0xc6u, 0u},
/* L */  {0xc0u, 0xc0u, 0xc0u, 0xc0u, 0xc0u, 0xc0u, 0xfeu, 0u},
/* M */  {0xc6u, 0xeeu, 0xfeu, 0xd6u, 0xd6u, 0xc6u, 0xc6u, 0u},
/* N */  {0xc6u, 0xe6u, 0xf6u, 0xdeu, 0xceu, 0xc6u, 0xc6u, 0u},
/* O */  {0x7cu, 0xc6u, 0xc6u, 0xc6u, 0xc6u, 0xc6u, 0x7cu, 0u},
/* P */  {0xfcu, 0xc6u, 0xc6u, 0xfcu, 0xc0u, 0xc0u, 0xc0u, 0u},
/* Q */  {0x7cu, 0xc6u, 0xc6u, 0xc6u, 0xcau, 0xccu, 0x76u, 0u},
/* R */  {0xfcu, 0xc6u, 0xc6u, 0xfcu, 0xccu, 0xc6u, 0xc6u, 0u},
/* S */  {0x7cu, 0xc6u, 0xc0u, 0x7cu, 0x06u, 0xc6u, 0x7cu, 0u},
/* T */  {0xfeu, 0x18u, 0x18u, 0x18u, 0x18u, 0x18u, 0x18u, 0u},
/* U */  {0xc6u, 0xc6u, 0xc6u, 0xc6u, 0xc6u, 0xc6u, 0x7cu, 0u},
/* V */  {0xc6u, 0xc6u, 0x6cu, 0x6cu, 0x38u, 0x38u, 0x10u, 0u},
/* W */  {0xc6u, 0xc6u, 0xd6u, 0xd6u, 0xfeu, 0xeeu, 0xc6u, 0u},
/* X */  {0xc6u, 0x6cu, 0x38u, 0x10u, 0x38u, 0x6cu, 0xc6u, 0u},
/* Y */  {0xc6u, 0xc6u, 0x6cu, 0x38u, 0x18u, 0x18u, 0x18u, 0u},
/* Z */  {0xfeu, 0x0cu, 0x18u, 0x30u, 0x60u, 0xc0u, 0xfeu, 0u},
/* [ */  {0x7cu, 0x60u, 0x60u, 0x60u, 0x60u, 0x60u, 0x7cu, 0u},
/* \ */  {0u, 0x60u, 0x30u, 0x18u, 0x0cu, 0x6u, 0u, 0u},
/* ] */  {0x3eu, 0x6u, 0x6u, 0x6u, 0x6u, 0x6u, 0x3eu, 0u},
/* ^ */  {0x10u, 0x38u, 0x6cu, 0xc6u, 0x82u, 0u, 0u, 0u},
/* _ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0xffu},
/* ` */  {0x3cu, 0x66u, 0x60u, 0xfcu, 0x60u, 0x60u, 0xfeu, 0u},
/* a */  {0u, 0u, 0x7cu, 0x6u, 0x7eu, 0xc6u, 0x7eu, 0u},
/* b */  {0xc0u, 0xc0u, 0xfcu, 0xc6u, 0xc6u, 0xc6u, 0xfcu, 0u},
/* c */  {0u, 0u, 0x7cu, 0xc6u, 0xc0u, 0xc6u, 0x7cu, 0u},
/* d */  {0x6u, 0x6u, 0x7eu, 0xc6u, 0xc6u, 0xc6u, 0x7eu, 0u},
/* e */  {0u, 0u, 0x7cu, 0xc6u, 0xfeu, 0xc0u, 0x7cu, 0u},
/* f */  {0x3eu, 0x60u, 0x60u, 0xfcu, 0x60u, 0x60u, 0x60u, 0u},
/* g */  {0u, 0u, 0x7eu, 0xc6u, 0xc6u, 0x7eu, 0x6u, 0x7cu},
/* h */  {0xc0u, 0xc0u, 0xfcu, 0xc6u, 0xc6u, 0xc6u, 0xc6u, 0u},
/* i */  {0x18u, 0u, 0x78u, 0x18u, 0x18u, 0x18u, 0x7eu, 0u},
/* j */  {0x18u, 0u, 0x38u, 0x18u, 0x18u, 0x18u, 0x18u, 0x70u},
/* k */  {0xc0u, 0xc0u, 0xc6u, 0xccu, 0xf8u, 0xccu, 0xc6u, 0u},
/* l */  {0x78u, 0x18u, 0x18u, 0x18u, 0x18u, 0x18u, 0x7eu, 0u},
/* m */  {0u, 0u, 0xecu, 0xfeu, 0xd6u, 0xd6u, 0xc6u, 0u},
/* n */  {0u, 0u, 0xfcu, 0xc6u, 0xc6u, 0xc6u, 0xc6u, 0u},
/* o */  {0u, 0u, 0x7cu, 0xc6u, 0xc6u, 0xc6u, 0x7cu, 0u},
/* p */  {0u, 0u, 0xfcu, 0xc6u, 0xc6u, 0xfcu, 0xc0u, 0xc0u},
/* q */  {0u, 0u, 0x7eu, 0xc6u, 0xc6u, 0x7eu, 0x6u, 0x7u},
/* r */  {0u, 0u, 0xdcu, 0xf6u, 0xc0u, 0xc0u, 0xc0u, 0u},
/* s */  {0u, 0u, 0x7eu, 0xc0u, 0x7cu, 0x6u, 0xfcu, 0u},
/* t */  {0x30u, 0x30u, 0xfcu, 0x30u, 0x30u, 0x30u, 0x1eu, 0u},
/* u */  {0u, 0u, 0xc6u, 0xc6u, 0xc6u, 0xc6u, 0x7eu, 0u},
/* v */  {0u, 0u, 0xc6u, 0xc6u, 0x6cu, 0x38u, 0x10u, 0u},
/* w */  {0u, 0u, 0xc6u, 0xd6u, 0xd6u, 0xfeu, 0xc6u, 0u},
/* x */  {0u, 0u, 0xc6u, 0x6cu, 0x38u, 0x6cu, 0xc6u, 0u},
/* y */  {0u, 0u, 0xc6u, 0xc6u, 0xc6u, 0x7eu, 0x6u, 0x7cu},
/* z */  {0u, 0u, 0xfeu, 0x0cu, 0x38u, 0x60u, 0xfeu, 0u},
/* { */  {0x0cu, 0x18u, 0x18u, 0x70u, 0x18u, 0x18u, 0x0cu, 0u},
/* | */  {0x18u, 0x18u, 0x18u, 0u, 0x18u, 0x18u, 0x18u, 0u},
/* } */  {0x30u, 0x18u, 0x18u, 0xeu, 0x18u, 0x18u, 0x30u, 0u},
/* ~ */  {0x31u, 0x6bu, 0x46u, 0u, 0u, 0u, 0u, 0u},
/* DEL */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
	/* 0x80 */
/* Ä */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Å */  {0x1C, 0x63, 0x6B, 0x6B, 0x7F, 0x77, 0x63, 0u},
/* Ç */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* É */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ñ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ö */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ü */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* á */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* à */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* â */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ä */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ã */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* å */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ç */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* é */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* è */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
	/* 90 */
/* ê */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ë */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* í */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ì */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* î */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ï */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ñ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ó */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ò */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ô */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ö */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* õ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ú */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ù */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* û */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ü */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
	/* a0 */
/* † */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ° */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ¢ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* £ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* § */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* • */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ¶ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ß */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ® */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* © */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ™ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ´ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ¨ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ≠ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Æ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ø */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
	/* b0 */
/* ∞ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ± */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ≤ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ≥ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ¥ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* µ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ∂ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ∑ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ∏ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* π */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ∫ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ª */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* º */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ω */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* æ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ø */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
	/* c0 */
/* ¿ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ¡ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ¬ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* √ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ƒ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ≈ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ∆ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* « */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* » */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* … */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/*   */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* À */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ã */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Õ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Œ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* œ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
	/* d0 */
/* – */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* — */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* “ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ” */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ‘ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ’ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ÷ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ◊ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ÿ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ÿ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ⁄ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* € */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ‹ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* › */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ﬁ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ﬂ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
	/* e0 */
/* ‡ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* · */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ‚ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* „ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ‰ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Â */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ê */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Á */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ë */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* È */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Í */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Î */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ï */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ì */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ó */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ô */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
	/* f0 */
/*  */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ò */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ú */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Û */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ù */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ı */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ˆ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ˜ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ¯ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ˘ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ˙ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ˚ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ¸ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/*   */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ˛ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ˇ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u}
};

#define XPPC 8		/* Size of character in pixels in X direction */
#define YPPC 8		/* Size of character in pixels in Y direction */

/*
** 'find_cursor' locates the cursor on the text screen and ensures that
** its position is valid, that is, lies within the text window
*/
void find_cursor(void) {
  if (graphmode!=FULLSCREEN) {
    xtext = wherex()-1;
    ytext = wherey()-1;
  }
}

/*
** 'scroll_text' is called to move the text window up or down a line.
** Note that the coordinates here are in RISC OS text coordinates which
** start at (0, 0) whereas conio's start with (1, 1) at the top left-hand
** corner of the screen.
*/
static void scroll_text(updown direction) {
  int n;
  if (!textwin && direction==SCROLL_UP)		/* Text window is the whole screen and scrolling upwards */
    putch('\012');	/* Output a linefeed */
  else {
    if (twintop!=twinbottom) {	/* Text window is more than one line high */
      if (direction==SCROLL_UP)	/* Scroll text up a line */
        (void) movetext(twinleft+1, twintop+2, twinright+1, twinbottom+1, twinleft+1, twintop+1);
      else {	/* Scroll text down a line */
        (void) movetext(twinleft+1, twintop+1, twinright+1, twinbottom, twinleft+1, twintop+2);
      }
    }
    gotoxy(twinleft+1, ytext+1);
    echo_off();
    for (n=twinleft; n<=twinright; n++) putch(' ');	/* Clear the vacated line of the window */
    echo_on();
  }
  gotoxy(xtext+1, ytext+1);	/* Put the cursor back where it should be */
}

/*
** 'vdu_2317' deals with various flavours of the sequence VDU 23,17,...
*/
static void vdu_2317(void) {
  int32 temp;
  switch (vduqueue[1]) {	/* vduqueue[1] is the byte after the '17' and says what to change */
  case TINT_FORETEXT:
    text_foretint = (vduqueue[2] & TINTMASK)>>TINTSHIFT;	/* Third byte in queue is new TINT value */
    if (colourdepth==256) text_physforecol = (text_forecol<<COL256SHIFT)+text_foretint;
    break;
  case TINT_BACKTEXT:
    text_backtint = (vduqueue[2] & TINTMASK)>>TINTSHIFT;
    if (colourdepth==256) text_physbackcol = (text_backcol<<COL256SHIFT)+text_backtint;
    break;
  case TINT_FOREGRAPH:
    graph_foretint = (vduqueue[2] & TINTMASK)>>TINTSHIFT;
    if (colourdepth==256) graph_physforecol = (graph_forecol<<COL256SHIFT)+graph_foretint;
    break;
  case TINT_BACKGRAPH:
    graph_backtint = (vduqueue[2] & TINTMASK)>>TINTSHIFT;
    if (colourdepth==256) graph_physbackcol = (graph_backcol<<COL256SHIFT)+graph_backtint;
    break;
  case EXCH_TEXTCOLS:	/* Exchange text foreground and background colours */
    temp = text_forecol; text_forecol = text_backcol; text_backcol = temp;
    temp = text_physforecol; text_physforecol = text_physbackcol; text_physbackcol = temp;
    temp = text_foretint; text_foretint = text_backtint; text_backtint = temp;
    break;
  default:		/* Ignore bad value */
    break;
  }
}

/* forward reference - added by Crispian Daniels */
static void toggle_cursor(void);

/*
** 'vdu_23command' emulates some of the VDU 23 command sequences
*/
static void vdu_23command(void) {
  switch (vduqueue[0]) {	/* First byte in VDU queue gives the command type */
  case 1:	/* Control the appear of the text cursor */
    if (graphmode==FULLSCREEN)
    {
      if (vduqueue[1]==0) {
        if (cursorstate==ONSCREEN) toggle_cursor();
        cursorstate = HIDDEN;	/* 0 = hide, 1 = show */
      }
      if (vduqueue[1]==1 && cursorstate!=NOCURSOR) cursorstate = ONSCREEN;
    }
    break;
  case 8:	/* Clear part of the text window */
    break;
  case 17:	/* Set the tint value for a colour in 256 colour modes, etc */
    vdu_2317();
  }
}

/*
** 'toggle_cursor' draws the text cursor at the current text position.
** It draws (and removes) the cursor by inverting the colours of the
** pixels at the current text cursor position. Two different styles
** of cursor can be drawn, an underline and a block
*/
static void toggle_cursor(void) {
  int32 left, right, top, bottom, x, y, pixel;
  if (cursorstate!=SUSPENDED && cursorstate!=ONSCREEN) return;	/* Cursor is not being displayed so give up now */
  if (cursorstate==ONSCREEN)	/* Toggle the cursor state */
    cursorstate = SUSPENDED;
  else {
    cursorstate = ONSCREEN;
  }
  if (cursmode==UNDERLINE) {
    left = xoffset+xtext*xscale*XPPC;	/* Calculate pixel coordinates of ends of cursor */
    y = yoffset+(ytext+1)*yscale*YPPC-1;
    right = left+xscale*XPPC-1;
    for (x=left; x<=right; x++) {
      pixel = buff_get_pointNC(vscreen, x, y);
      buff_draw_pointNC(vscreen, x, y, colourdepth-1-pixel);
    }
    screen_blit_buff_toNC(left, y, vscreen, left, y, right, y);
  }
  else if (cursmode==BLOCK) {
    left = xoffset+xtext*xscale*XPPC;
    top = yoffset+ytext*yscale*YPPC;
    right = left+xscale*XPPC-1;
    bottom = top+YPPC-1;
    for (y=top; y<=bottom; y++) {
      for (x=left; x<=right; x++) {
        pixel = buff_get_pointNC(vscreen, x, y);
        buff_draw_pointNC(vscreen, x, y, colourdepth-1-pixel);
      }
    }
    screen_blit_buff_toNC(left, top, vscreen, left, top, right, bottom);
  }
}

/*
** 'blit_scaled' is called when working in one of the 'scaled'
** screen modes to copy the scaled rectangle defined by (x1, y1) and
** (x2, y2) to the screen buffer and then to display it.
** This function is used when one of the RISC OS screen modes that has
** to be scaled to fit the screen is being used, for example, mode 0.
** Everything is written to a buffer of a size appropriate to the
** resolution of that screen mode, for example, mode 0 is written to a
** 640 by 256 buffer and mode 1 to a 320 by 256 buffer. When the buffer
** is displayed it is scaled to fit the screen. This means that the
** buffer being used for that screen mode has to be copied in an
** enlarged form to the main screen buffer and then blit'ed to the
** screen itself.
** (x1, y1) and (x2, y2) define the rectangle to be displayed. These
** are given in terms of what could be called the pseudo pixel
** coordinates of the buffer. These pseudo pixel coordinates are
** converted to real pixel coordinates by multiplying them by 'xscale'
** and 'yscale'. 'xoffset' and 'yoffset' are added to the coordinates
** to position the screen buffer in the middle of the screen.
*/
static void blit_scaled(int32 left, int32 top, int32 right, int32 bottom) {
  int32 dleft, dtop, dright, dbottom;
/*
** Start by clipping the rectangle to be blit'ed if it extends off the
** screen.
** Note that 'screenwidth' and 'screenheight' give the dimensions of the
** RISC OS screen mode in pixels
*/
  if (left>=screenwidth || right<0 || top<0 || bottom>=screenheight) return;	/* Is off screen completely */
  if (left<0) left = 0;		/* Clip the rectangle as necessary */
  if (right>=screenwidth) right = screenwidth-1;
  if (top<0) top = 0;
  if (bottom>=screenheight) bottom = screenheight-1;
  dleft = left*xscale+xoffset;		/* Calculate pixel coordinates in the */
  dtop = top*yscale+yoffset;		/* screen buffer of the rectangle to be */
  dright = (right+1)*xscale+xoffset-1;	/* overwritten by the scaled buffer */
  dbottom = (bottom+1)*yscale+yoffset-1;
  buff_scale_buff_toNC(vscreen, dleft, dtop, dright, dbottom, modescreen, left, top, right, bottom);
  screen_blit_buff_toNC(dleft, dtop, vscreen, dleft, dtop, dright, dbottom);
}

#define COLOURSTEP 68		/* RGB colour value increment used in 256 colour modes */
#define TINTSTEP 17		/* RGB colour value increment used for tints */

/*
** 'init_palette' is called to initialise the palette used for the
** screen when operating in full screen graphics mode. It sets up
** a 'jlib' palette. This is just a 768 byte block of memory with
** three bytes for each colour. The table is initialised with RGB
** values so that it corresponds directly to the RISC OS default
** palettes in 2, 4, 16 and 256 colour screen modes. This means we
** can go directly from a RISC OS GCOL or COLOUR number to the
** physical colour without an extra layer of mapping to convert a
** RISC OS physical colour to its equivalent under foreign operating
** systems
*/
static void init_palette(void) {
  switch (colourdepth) {
  case 2:	/* Two colour - Black and white only */
    palette[0] = palette[1] = palette[2] = 0;
    palette[3] = palette[4] = palette[5] = 255;
    break;
  case 4:	/* Four colour - Bloack, red, yellow and white */
    palette[0] = palette[1] = palette[2] = 0;		/* Black */
    palette[3] = 255; palette[4] = palette[5] = 0;	/* Red */
    palette[6] = palette[7] = 255; palette[8] = 0;	/* Yellow */
    palette[9] = palette[10] = palette[11] = 255;	/* White */
    break;
  case 16:	/* Sixteen colour */
    palette[0] = palette[1] = palette[2] = 0;		/* Black */
    palette[3] = 255; palette[4] = palette[5] = 0;	/* Red */
    palette[6] = 0; palette[7] = 255; palette[8] = 0;	/* Green */
    palette[9] = palette[10] = 255; palette[11] = 0;	/* Yellow */
    palette[12] = palette[13] = 0; palette[14] = 255;	/* Blue */
    palette[15] = 255; palette[16] = 0; palette[17] = 255;	/* Magenta */
    palette[18] = 0; palette[19] = palette[20] = 255;	/* Cyan */
    palette[21] = palette[22] = palette[23] = 255;	/* White */
    palette[24] = palette[25] = palette[26] = 0;	/* Black */
    palette[27] = 160; palette[28] = palette[29] = 0;	/* Dark red */
    palette[30] = 0; palette[31] = 160; palette[32] = 0;/* Dark green */
    palette[33] = palette[34] = 160; palette[35] = 0;	/* Khaki */
    palette[36] = palette[37] = 0; palette[38] = 160;	/* Navy blue */
    palette[39] = 160; palette[40] = 0; palette[41] = 160;	/* Purple */
    palette[42] = 0; palette[43] = palette[44] = 160;	/* Cyan */
    palette[45] = palette[46] = palette[47] = 160;	/* Grey */
    break;
  case 256: {	/* 256 colour */
    int red, green, blue, tint, colour;
/*
** The colour number in 256 colour modes can be seen as a bit map as
** follows:
**	bb gg rr tt
** where 'rr' is a two-bit red component, 'gg' is the green component,
** 'bb' is the blue component and 'tt' is the 'tint', a value that
** affects the brightness of the three component colours. The two-bit
** component numbers correspond to RGB values of 0, 68, 136 and 204
** for the brightness of that component. The tint values increase the
** RGB values by 0, 17, 34 or 51. Note that the tint value is added
** to *all three* colour components. An example colour number where
** rr = 2, gg = 0, bb = 3 and tt = 1 would have colour components
** red: 136+17 = 153, green: 0+17 = 17 and blue: 204+17 = 221.
** The RISC OS logical colour number provides the 'rr gg bb' bits.
** THe tint value can be supplied at the same time as the colour (via
** the 'TINT' parameter of the COLOUR and GCOL statements) or changed
** separated by using 'TINT' as a statement in its own right.
*/
    colour = 0;
    for (blue=0; blue<=COLOURSTEP*3; blue+=COLOURSTEP) {
      for (green=0; green<=COLOURSTEP*3; green+=COLOURSTEP) {
        for (red=0; red<=COLOURSTEP*3; red+=COLOURSTEP) {
          for (tint=0; tint<=TINTSTEP*3; tint+=TINTSTEP) {
            palette[colour] = red+tint;
            palette[colour+1] = green+tint;
            palette[colour+2] = blue+tint;
            colour+=3;
          }
        }
      }
    }
    break;
  }
  default:	/* 32K and 16M colour modes are not supported */
    error(ERR_UNSUPPORTED);
  }
  if (colourdepth==256) {
    text_physforecol = (text_forecol<<COL256SHIFT)+text_foretint;
    text_physbackcol = (text_backcol<<COL256SHIFT)+text_backtint;
    graph_physforecol = (graph_forecol<<COL256SHIFT)+graph_foretint;
    graph_physbackcol = (graph_backcol<<COL256SHIFT)+graph_backtint;
  }
  else {
    text_physforecol = text_forecol;
    text_physbackcol = text_backcol;
    graph_physforecol = graph_forecol;
    graph_physbackcol = graph_backcol;
  }
}

/*
** 'change_palette' is called to change the palette entry for physical
** colour 'colour' to the colour defined by the RGB values red, green
** and blue. The screen is updated by this call
*/
static void change_palette(int32 colour, int32 red, int32 green, int32 blue) {
  if (graphmode!=FULLSCREEN) return;	/* There be no palette to change */
  palette[colour*3] = red;	/* The palette is not structured */
  palette[colour*3+1] = green;
  palette[colour*3+2] = blue;
  screen_put_pal(colour, red, green, blue);	/* Update colour on screen */
}

static void switch_graphics(void);	/* Forward reference */

/*
 * emulate_colourfn - This performs the function COLOUR(). It
 * Returns the entry in the palette for the current screen mode
 * that most closely matches the colour with red, green and
 * blue components passed to it.
 * It is assumed that this function will be used for graphics
 * so the screen is switched to graphics mode if it is used
 */
int32 emulate_colourfn(int32 red, int32 green, int32 blue) {
  int32 n, distance, test, best, dr, dg, db;

  if (graphmode < TEXTMODE)
    return colourdepth - 1;	/* There is no palette */
  else if (graphmode==TEXTMODE) {
    switch_graphics();
  }
  distance = 0x7fffffff;
  best = 0;
  for (n = 0; n < colourdepth && distance != 0; n++) {
    dr = palette[n * 3] - red;
    dg = palette[n * 3 + 1] - green;
    db = palette[n * 3 + 2] - blue;
    test = 2 * dr * dr + 4 * dg * dg + db * db;
    if (test < distance) {
      distance = test;
      best = n;
    }
  }
  return best;
}

/*
 * set_text_colour - Set either the text foreground colour
 * or the background colour to the supplied colour number
 * (palette entry number). This is used when a colour has
 * been matched with an entry in the palette via COLOUR()
 */
static void set_text_colour(boolean background, int colnum) {
  if (background)
    text_physbackcol = text_backcol = (colnum & colourdepth - 1);
  else {
    text_physforecol = text_forecol = (colnum & colourdepth - 1);
  }
}

/*
 * set_graphics_colour - Set either the graphics foreground
 * colour or the background colour to the supplied colour
 * number (palette entry number). This is used when a colour
 * has been matched with an entry in the palette via COLOUR()
 */
static void set_graphics_colour(boolean background, int colnum) {
  if (background)
    graph_physbackcol = graph_backcol = (colnum & colourdepth - 1);
  else {
    graph_physforecol = graph_forecol = (colnum & colourdepth - 1);
  }
}


static void vdu_cleartext(void);	/* Forward reference */

/*
** 'switch_graphics' switches from text output mode to fullscreen graphics
** mode. Unless the option '-graphics' is specified on the command line,
** the interpreter remains in fullscreen mode until the next mode change
** when it switches back to text output mode. If '-graphics' is given
** then it remains in fullscreen mode
*/
static void switch_graphics(void) {
  if (!screen_set_video_mode()) {	/* Attempt to initialise screen failed */
    graphmode = NOGRAPHICS;
    error(ERR_NOGRAPHICS);
  }
  vscreen = buff_init(vscrwidth, vscrheight);		/* Create the virtual screen */
  if (vscreen==NIL) {		/* Could not create virtual screen */
    screen_restore_video_mode();
    graphmode = NOGRAPHICS;
    error(ERR_NOGRAPHICS);
  }
  if (scaled) {	/* Using a scaled screen mode */
    modescreen = buff_init(screenwidth, screenheight);
    if (modescreen==NIL) {		/* Could not create screen buffer */
      modescreen = vscreen;		/* So use main buffer */
      scaled = FALSE;			/* And do not scale the graphics screen */
    }
  }
  else {	/* Write directly to screen buffer */
    modescreen = vscreen;
  }
  palette = pal_init();		/* Create the palette and initialise it */
  init_palette();
  screen_block_set_pal(palette);
  graphmode = FULLSCREEN;
  xtext = twinleft;		/* Send the text cursor to the home position */
  ytext = twintop;
#if defined(TARGET_DJGPP) | defined(TARGET_MACOSX)
  textwidth = modetable[screenmode & MODEMASK].xtext;	/* Hack to set the depth of the graphics screen */
  textheight = modetable[screenmode & MODEMASK].ytext;
  if (!textwin) {	/* Text window is the whole screen */
    twinright = textwidth-1;
    twinbottom = textheight-1;
  }
#endif
  if (xoffset!=0) {	/* Only part of the screen is used */
    buff_set_clip_region(vscreen, xoffset-1, yoffset-1, vscrwidth-xoffset-1, vscrheight-yoffset-1);
  }
  vdu_cleartext();	/* Clear the graphics screen */
  if (cursorstate==NOCURSOR) {	/* 'cursorstate' might be set to 'HIDDEN' if OFF used */
    cursorstate = SUSPENDED;
    toggle_cursor();
  }
}

/*
** 'switch_text' switches from fullscreen graphics back to text output mode.
** It does this on a mode change
*/
static void switch_text(void) {
  pal_free(palette);
  if (modescreen != vscreen) buff_free(modescreen);
  vscreen = buff_free(vscreen);
  screen_restore_video_mode();
}

/*
** 'scroll' scrolls the graphics screen up or down by the number of
** rows equivalent to one line of text on the screen. Depending on
** the RISC OS mode being used, this can be either eight or sixteen
** rows. 'direction' says whether the screen is moved up or down.
** The screen is redrawn by this call
*/
static void scroll(updown direction) {
  int left, right, top, bottom, dest, topwin;
  topwin = ybufoffset+twintop*YPPC;		/* Y coordinate of top of text window */
  if (direction==SCROLL_UP) {	/* Shifting screen up */
    dest = ybufoffset+twintop*YPPC;		/* Move screen up to this point */
    left = xbufoffset+twinleft*XPPC;
    right = xbufoffset+twinright*XPPC+XPPC-1;
    top = dest+YPPC;				/* Top of block to move starts here */
    bottom = ybufoffset+twinbottom*YPPC+YPPC-1;	/* End of block is here */
    buff_blit_buff_toNC(modescreen, left, dest, modescreen, left, top, right, bottom);
    buff_draw_rectNC(modescreen, left, bottom-YPPC+1, right, bottom, text_physbackcol);
  }
  else {	/* Shifting screen down */
    dest = ybufoffset+(twintop+1)*YPPC;
    left = xbufoffset+twinleft*XPPC;
    right = xbufoffset+(twinright+1)*XPPC-1;
    top = ybufoffset+twintop*YPPC;
    bottom = ybufoffset+twinbottom*YPPC-1;
    buff_blit_buff_toNC(modescreen, left, dest, modescreen, left, top, right, bottom);
    buff_draw_rectNC(modescreen, left, topwin, right, dest-1, text_physbackcol);
  }
  if (scaled)
    blit_scaled(left, topwin, right, twinbottom*YPPC+YPPC-1);
  else if (textwin)	/* Scrolling a text window */
    screen_blit_buff_toNC(left, topwin, vscreen, left, topwin, right, ybufoffset+(twinbottom+1)*YPPC-1);
  else {	/* Scrolling the entire screen */
    screen_blit_fs_buffer(vscreen);
  }
}

/*
** 'echo_text' is called to display text held in the screen buffer on the
** graphics screen when working in 'no echo' mode. If displays from the
** start of the line to the current value of the text cursor
*/
static void echo_text(void) {
  int sx, ex, sy, ey;
  if (xtext==0) return;	/* Return if nothing has changed */
  if (scaled)
    blit_scaled(0, ytext*YPPC, xtext*XPPC-1, ytext*YPPC+YPPC-1);
  else {
    sx = xoffset;
    sy = yoffset+ytext*YPPC;
    ex = xoffset+xtext*XPPC-1;
    ey = sy+YPPC-1;
    screen_blit_buff_toNC(sx, sy, vscreen, sx, sy, ex, ey);
  }
}

/*
** 'write_char' draws a character when in fullscreen graphics mode
** when output is going to the text cursor. It assumes that the
** screen in is fullscreen graphics mode.
** The line or block representing the text cursor is overwritten
** by this code so the cursor state is automatically set to
** 'suspended' (if the cursor is being displayed)
*/
static void write_char(int32 ch) {
  int32 y, topx, topy, line;
  if (cursorstate==ONSCREEN) cursorstate = SUSPENDED;
  topx = xbufoffset+xtext*XPPC;
  topy = ybufoffset+ytext*YPPC;
  for (y=topy; y<topy+YPPC; y++) {
    line = sysfont[ch-' '][y-topy];
    buff_draw_h_lineNC(modescreen, topx, y, topx+XPPC-1, text_physbackcol);
    if (line!=0) {
      if (line & 0x80) buff_draw_pointNC(modescreen, topx+0, y, text_physforecol);
      if (line & 0x40) buff_draw_pointNC(modescreen, topx+1, y, text_physforecol);
      if (line & 0x20) buff_draw_pointNC(modescreen, topx+2, y, text_physforecol);
      if (line & 0x10) buff_draw_pointNC(modescreen, topx+3, y, text_physforecol);
      if (line & 0x08) buff_draw_pointNC(modescreen, topx+4, y, text_physforecol);
      if (line & 0x04) buff_draw_pointNC(modescreen, topx+5, y, text_physforecol);
      if (line & 0x02) buff_draw_pointNC(modescreen, topx+6, y, text_physforecol);
      if (line & 0x01) buff_draw_pointNC(modescreen, topx+7, y, text_physforecol);
    }
  }
  if (echo) {
    if (!scaled)
      screen_blit_buff_toNC(topx, topy, vscreen, topx, topy, topx+XPPC-1, topy+YPPC-1);
    else {
      blit_scaled(topx, topy, topx+XPPC-1, topy+YPPC-1);
    }
  }
  xtext++;
  if (xtext>twinright) {
    if (!echo) echo_text();	/* Line is full so flush buffered characters */
    xtext = twinleft;
    ytext++;
    if (ytext>twinbottom) {	/* Text cursor was on the last line of the text window */
      scroll(SCROLL_UP);	/* So scroll window up */
      ytext--;
    }
  }
}

/*
** 'plot_char' draws a character when in fullscreen graphics mode
** when output is going to the graphics cursor. It will scale the
** character if necessary. It assumes that the screen in is
** fullscreen graphics mode.
** Note that characters can be scaled in the 'y' direction or the
** 'x' and 'y' direction but never in just the 'x' direction.
*/
static void plot_char(int32 ch) {
  int32 y, y2, topx, topy, line;
  topx = GXTOPX(xlast);		/* X and Y coordinates are those of the */
  y2 = topy = GYTOPY(ylast);	/* top left-hand corner of the character */
  if (ch==DEL)	/* DEL is a special case */
    buff_draw_rect(modescreen, topx, topy, topx+XPPC-1, topy+YPPC-1, graph_physbackcol);
  else {
    for (y=0; y<YPPC; y++) {
      line = sysfont[ch-' '][y];
      if (line!=0) {
        if (line & 0x80) buff_draw_point(modescreen, topx+0, y2, graph_physforecol);
        if (line & 0x40) buff_draw_point(modescreen, topx+1, y2, graph_physforecol);
        if (line & 0x20) buff_draw_point(modescreen, topx+2, y2, graph_physforecol);
        if (line & 0x10) buff_draw_point(modescreen, topx+3, y2, graph_physforecol);
        if (line & 0x08) buff_draw_point(modescreen, topx+4, y2, graph_physforecol);
        if (line & 0x04) buff_draw_point(modescreen, topx+5, y2, graph_physforecol);
        if (line & 0x02) buff_draw_point(modescreen, topx+6, y2, graph_physforecol);
        if (line & 0x01) buff_draw_point(modescreen, topx+7, y2, graph_physforecol);
      }
      y2++;
    }
  }
  if (!scaled)
    screen_blit_buff_toNC(topx, topy, vscreen, topx, topy, topx+XPPC-1, topy+YPPC-1);
  else {
    blit_scaled(topx, topy, topx+XPPC-1, topy+YPPC-1);
  }
  xlast+=XPPC*xgupp;	/* Move to next character position in X direction */
  if (xlast>gwinright) {	/* But position is outside the graphics window */
    xlast = gwinleft;
    ylast-=YPPC*ygupp;
    if (ylast<gwinbottom) ylast = gwintop;	/* Below bottom of graphics window - Wrap around to top */
  }
}

/*
** 'echo_on' turns on cursor (if in graphics mode) and the immediate
** echo of characters to the screen
*/
void echo_on(void) {
  echo = TRUE;
  if (graphmode==FULLSCREEN) {
    echo_text();			/* Flush what is in the graphics buffer */
    if (cursorstate==SUSPENDED) toggle_cursor();	/* Display cursor again */
  }
  else {
    fflush(stdout);
  }
}

/*
** 'echo_off' turns off the cursor (if in graphics mode) and the
** immediate echo of characters to the screen. This is used to
** make character output more efficient
*/
void echo_off(void) {
  echo = FALSE;
  if (graphmode==FULLSCREEN) {
    if (cursorstate==ONSCREEN) toggle_cursor();	/* Remove the cursor if it is being displayed */
  }
}

/*
** 'move_cursor' sends the text cursor to the position (column, row)
** on the screen.  The function updates the cursor position as well.
** The column and row are given in RISC OS text coordinates, that
** is, (0,0) is the top left-hand corner of the screen. These values
** are the true coordinates on the screen. The code that uses this
** function has to allow for the text window.
*/
static void move_cursor(int32 column, int32 row) {
  if (graphmode==FULLSCREEN) {
    if (cursorstate==ONSCREEN) toggle_cursor();	/* Remove cursor if in graphics mode */
    xtext = column;
    ytext = row;
    if (cursorstate==SUSPENDED) toggle_cursor();	/* Redraw cursor if in graphics mode */
  }
  else {
    gotoxy(column+1, row+1);
    xtext = column;
    ytext = row;
  }
}

/*
** 'set_cursor' sets the type of the text cursor used on the graphic
** screen to either a block or an underline. 'underline' is set to
** TRUE if an underline is required. Underline is used by the program
** when keyboard input is in 'insert' mode and a block when it is in
** 'overwrite'.
*/
void set_cursor(boolean underline) {
  if (cursorstate==ONSCREEN) toggle_cursor();	/* Remove old style cursor */
  cursmode = underline ? UNDERLINE : BLOCK;
  if (cursorstate==SUSPENDED) toggle_cursor();	/* Draw new style cursor */
  if (cursmode==UNDERLINE)
    _setcursortype(_NORMALCURSOR);
  else {
    _setcursortype(_SOLIDCURSOR);
  }
}

/*
** 'vdu_setpalette' changes one of the logical to physical colour map
** entries (VDU 19). When the interpreter is in full screen mode it
** can also redefine colours for in the palette.
** Note that when working in text mode, this function should have the
** side effect of changing all pixels of logical colour number 'logcol'
** to the physical colour given by 'mode' but the code does not do this.
*/
static void vdu_setpalette(void) {
  int32 logcol, mode;
  logcol = vduqueue[0] & colourmask;
  mode = vduqueue[1];
  if (mode<16 && colourdepth<=16)	/* Just change the RISC OS logical to physical colour mapping */
    logtophys[logcol] = mode;
  else if (mode==16)	/* Change the palette entry for colour 'logcol' */
    change_palette(logcol, vduqueue[2], vduqueue[3], vduqueue[4]);
  else {
    if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
  }
}

/*
** 'map_colour' maps a RISC OS logical colour number to the corresponding
** colour number used by the underlying OS. It returns the physical
** colour number. The 256 colour colour number really only has a range
** of 0 to 63 and can be seen as a bit pattern 'rrggbb'. The code
** has to convert this to a four bit colour number using the table
** 'c256map'
*/
static int32 map_colour(int32 colour) {
  if (graphmode==FULLSCREEN) return colour;
  if (colourdepth<=16)
    return colourmap[logtophys[colour]];
  else {	/* Map a 256 colour colour number to a sixteen bit one */
    int32 temp = 0;
    if (colour & C256_REDBIT) temp+=VDU_RED;
    if (colour & C256_GREENBIT) temp+=VDU_GREEN;
    if (colour & C256_BLUEBIT) temp+=VDU_BLUE;
    return colourmap[temp];
  }
}

/*
** 'move_down' moves the text cursor down a line within the text
** window, scrolling the window up if the cursor is on the last line
** of the window. This only works in full screen graphics mode.
*/
static void move_down(void) {
  ytext++;
  if (ytext>twinbottom) {	/* Cursor was on last line in window - Scroll window up */
    ytext--;
    scroll(SCROLL_UP);
  }
}

/*
** 'move_up' moves the text cursor up a line within the text window,
** scrolling the window down if the cursor is on the top line of the
** window
*/
static void move_up(void) {
  ytext--;
  if (ytext<twintop) {	/* Cursor was on top line in window - Scroll window down */
    ytext++;
    scroll(SCROLL_DOWN);
  }
}

/*
** 'move_curback' moves the cursor back one character on the screen (VDU 8)
*/
static void move_curback(void) {
  if (vdu5mode) {	/* VDU 5 mode - Move graphics cursor back one character */
    xlast-=XPPC*xgupp;
    if (xlast<gwinleft) {		/* Cursor is outside the graphics window */
      xlast = gwinright-XPPC*xgupp+1;	/* Move back to right edge of previous line */
      ylast+=YPPC*ygupp;
      if (ylast>gwintop) {		/* Move above top of window */
        ylast = gwinbottom+YPPC*ygupp-1;	/* Wrap around to bottom of window */
      }
    }
  }
  else if (graphmode==FULLSCREEN) {
    if (cursorstate==ONSCREEN) toggle_cursor();	/* Remove cursor */
    xtext--;
    if (xtext<twinleft) {	/* Cursor is at left-hand edge of text window so move up a line */
      xtext = twinright;
      move_up();
    }
    if (cursorstate==SUSPENDED) toggle_cursor();	/* Redraw cursor */
  }
  else {	/* Writing to the text screen */
    xtext--;
    if (xtext>=twinleft)	/* Cursor is still within window */
      putch('\b');	/* Output a 'backspace' character */
    else {		/* Cursor is outside window - Move up a line */
      xtext = twinright;
      ytext--;
      if (ytext>=twintop)	/* Cursor is still within window */
        gotoxy(xtext+1, ytext+1);
      else {		/* Cursor is above window - Scroll screen down a line */
        ytext++;
        scroll_text(SCROLL_DOWN);
      }
    }
  }
}

/*
** 'move_curforward' moves the cursor forwards one character on the screen (VDU 9)
*/
static void move_curforward(void) {
  if (vdu5mode) {	/* VDU 5 mode - Move graphics cursor back one character */
    xlast+=XPPC*xgupp;
    if (xlast>gwinright) {	/* Cursor is outside the graphics window */
      xlast = gwinleft;		/* Move to left side of window on next line */
      ylast-=YPPC*ygupp;
      if (ylast<gwinbottom) ylast = gwintop;	/* Moved below bottom of window - Wrap around to top */
    }
  }
  else if (graphmode==FULLSCREEN) {
    if (cursorstate==ONSCREEN) toggle_cursor();	/* Remove cursor */
    xtext++;
    if (xtext>twinright) {	/* Cursor is at right-hand edge of text window so move down a line */
      xtext = twinleft;
      move_down();
    }
    if (cursorstate==SUSPENDED) toggle_cursor();	/* Redraw cursor */
  }
  else {	/* Writing to text screen */
    xtext++;
    if (xtext<=twinright)	/* Cursor is still within the text window */
      gotoxy(xtext+1, ytext+1);
    else {	/* Cursor has moved outside window - Move to next line */
      ytext++;
      if (ytext<=twinbottom)	/* Cursor is within the text window still */
        gotoxy(xtext+1, ytext+1);
      else {	/* Cursor is outside the window - Move the text window up one line */
        ytext--;
        scroll_text(SCROLL_UP);
      }
    }
  }
}

/*
** 'move_curdown' moves the cursor down the screen, that is, it
** performs the linefeed operation (VDU 10)
*/
static void move_curdown(void) {
  if (vdu5mode) {
    ylast-=YPPC*ygupp;
    if (ylast<gwinbottom) ylast = gwintop;	/* Moved below bottom of window - Wrap around to top */
  }
  else if (graphmode==FULLSCREEN) {
    if (cursorstate==ONSCREEN) toggle_cursor();	/* Remove cursor */
    move_down();
    if (cursorstate==SUSPENDED) toggle_cursor();	/* Redraw cursor */
  }
  else {		/* Writing to a text window */
    ytext++;
    if (ytext<=twinbottom)	/* Cursor is still within the confines of the window */
      gotoxy(xtext+1, ytext+1);
    else  {	/* At bottom of screen. Move screen up a line */
      ytext--;
      scroll_text(SCROLL_UP);
    }
  }
}

/*
** 'move_curup' moves the cursor up a line on the screen (VDU 11)
*/
static void move_curup(void) {
  if (vdu5mode) {
    ylast+=YPPC*ygupp;
    if (ylast>gwintop) ylast = gwinbottom+YPPC*ygupp-1;	/* Move above top of window - Wrap around to bottow */
  }
  else if (graphmode==FULLSCREEN) {
    if (cursorstate==ONSCREEN) toggle_cursor();	/* Remove cursor */
    move_up();
    if (cursorstate==SUSPENDED) toggle_cursor();	/* Redraw cursor */
  }
  else {	/* Writing to text screen */
    ytext--;
    if (ytext>=twintop)		/* Cursor still lies within the text window */
      gotoxy(xtext+1, ytext+1);
    else {	/* Moved cursor above text top of window */
      ytext++;
      scroll_text(SCROLL_DOWN);	/* Scroll the screen down a line */
    }
  }
}

/*
** 'vdu_cleartext' clears the text window. Normally this is the
** entire screen (VDU 12). This is the version of the function used
** when the interpreter supports graphics
*/
static void vdu_cleartext(void) {
  int32 left, right, top, bottom;
  if (graphmode==FULLSCREEN) {
    if (cursorstate==ONSCREEN) toggle_cursor();	/* Remove cursor if it is being displayed */
    if (scaled) {	/* Using a screen mode that has to be scaled when displayed */
      left = twinleft*XPPC;
      right = twinright*XPPC+XPPC-1;
      top = twintop*YPPC;
      bottom = twinbottom*YPPC+YPPC-1;
      buff_draw_rectNC(modescreen, left, top, right, bottom, text_physbackcol);
      blit_scaled(left, top, right, bottom);
      xtext = twinleft;
      ytext = twintop;
      if (cursorstate==SUSPENDED) toggle_cursor();	/* Redraw cursor */
    }
    else {	/* Screen is not scaled */
      if (textwin) {	/* Text window defined that does not occupy the whole screen */
        left = xbufoffset+twinleft*XPPC;
        right = xbufoffset+twinright*XPPC+XPPC-1;
        top = ybufoffset+twintop*YPPC;
        bottom = ybufoffset+twinbottom*YPPC+YPPC-1;
        buff_draw_rectNC(modescreen, left, top, right, bottom, text_physbackcol);
        screen_blit_buff_toNC(left, top, vscreen, left, top, right, bottom);
      }
      else {	/* Text window is not being used */
        buff_fillNC(vscreen, text_physbackcol);
        screen_blit_fs_buffer(vscreen);
      }
      xtext = twinleft;
      ytext = twintop;
      if (cursorstate==SUSPENDED) toggle_cursor();	/* Redraw cursor */
    }
  }
  else if (textwin) {	/* Text window defined that does not occupy the whole screen */
    int32 column, row;
    echo_off();
    for (row = twintop; row<=twinbottom; row++) {
      gotoxy(twinleft+1, row+1);	/* Go to start of line on screen */
      for (column = twinleft; column<=twinright;  column++) putch(' ');
    }
    echo_on();
    move_cursor(twinleft, twintop);	/* Send cursor to home position in window */
  }
  else {
    clrscr();
    xtext = twinleft;
    ytext = twintop;
  }
}

/*
** 'vdu_return' deals with the carriage return character (VDU 13)
*/
static void vdu_return(void) {
  if (vdu5mode)
    xlast = gwinleft;
  else if (graphmode==FULLSCREEN) {
    if (cursorstate==ONSCREEN) toggle_cursor();	/* Remove cursor */
    xtext = twinleft;
    if (cursorstate==SUSPENDED) toggle_cursor();	/* Redraw cursor */
  }
  else {
    move_cursor(twinleft, ytext);
  }
}

/*
** 'vdu_cleargraph' set the entire graphics window to the current graphics
** background colour (VDU 16)
*/
static void vdu_cleargraph(void) {
  if (graphmode==TEXTONLY) return;	/* Ignore command in text-only modes */
  if (graphmode==TEXTMODE) switch_graphics();
  if (cursorstate==ONSCREEN) toggle_cursor();	/* Remove cursor */
  buff_fill(modescreen, graph_physbackcol);
  if (!scaled)
    screen_blit_buff_toNC(GXTOPX(gwinleft), GYTOPY(gwintop), vscreen,
     GXTOPX(gwinleft), GYTOPY(gwintop), GXTOPX(gwinright), GYTOPY(gwinbottom));
  else {
    blit_scaled(GXTOPX(gwinleft), GYTOPY(gwintop), GXTOPX(gwinright), GYTOPY(gwinbottom));
  }
  if (cursorstate==SUSPENDED) toggle_cursor();	/* Redraw cursor */
}

/*
** 'vdu_textcol' changes the text colour to the value in the VDU queue
** (VDU 17). It handles both foreground and background colours at any
** colour depth. The RISC OS physical colour number is mapped to the
** equivalent as used by conio
*/
static void vdu_textcol(void) {
  int32 colnumber;
  colnumber = vduqueue[0];
  if (colnumber<128) {	/* Setting foreground colour */
    if (graphmode==FULLSCREEN) {	/* Operating in full screen graphics mode */
      if (colourdepth==256) {
        text_forecol = colnumber & COL256MASK;
        text_physforecol = (text_forecol<<COL256SHIFT)+text_foretint;
      }
      else {
        text_physforecol = text_forecol = colnumber & colourmask;
      }
    }
    else {	/* Operating in text mode */
      text_forecol = colnumber & colourmask;
      text_physforecol = map_colour(text_forecol);
      textcolor(text_physforecol);
    }
  }
  else {	/* Setting background colour */
    if (graphmode==FULLSCREEN) {	/* Operating in full screen graphics mode */
      if (colourdepth==256) {
        text_backcol = colnumber & COL256MASK;
        text_physbackcol = (text_backcol<<COL256SHIFT)+text_backtint;
      }
      else {	/* Operating in text mode */
        text_physbackcol = text_backcol = colnumber & colourmask;
      }
    }
    else {
      text_backcol = (colnumber-128) & colourmask;
      text_physbackcol = map_colour(text_backcol);
      textbackground(text_physbackcol);
    }
  }
}

/*
** 'reset_colours' initialises the RISC OS logical to physical colour
** map for the current screen mode and sets the default foreground
** and background text and graphics colours to white and black
** respectively (VDU 20)
*/
static void reset_colours(void) {
  switch (colourdepth) {	/* Initialise the text mode colours */
  case 2:
    logtophys[0] = VDU_BLACK;
    logtophys[1] = VDU_WHITE;
    text_forecol = graph_forecol = 1;
    break;
  case 4:
    logtophys[0] = VDU_BLACK;
    logtophys[1] = VDU_RED;
    logtophys[2] = VDU_YELLOW;
    logtophys[3] = VDU_WHITE;
    text_forecol = graph_forecol = 3;
    break;
  case 16:
    logtophys[0] = VDU_BLACK;
    logtophys[1] = VDU_RED;
    logtophys[2] = VDU_GREEN;
    logtophys[3] = VDU_YELLOW;
    logtophys[4] = VDU_BLUE;
    logtophys[5] = VDU_MAGENTA;
    logtophys[6] = VDU_CYAN;
    logtophys[7]  = VDU_WHITE;
    logtophys[8] = FLASH_BLAWHITE;
    logtophys[9] = FLASH_REDCYAN;
    logtophys[10] = FLASH_GREENMAG;
    logtophys[11] = FLASH_YELBLUE;
    logtophys[12] = FLASH_BLUEYEL;
    logtophys[13] = FLASH_MAGREEN;
    logtophys[14] = FLASH_CYANRED;
    logtophys[15]  = FLASH_WHITEBLA;
    text_forecol = graph_forecol = 7;
    break;
  case 256:
    text_forecol = graph_forecol = 63;
    graph_foretint = text_foretint = MAXTINT;
    graph_backtint = text_backtint = 0;
    break;
  default:
    error(ERR_UNSUPPORTED);
  }
  if (colourdepth==256)
    colourmask = COL256MASK;
  else {
    colourmask = colourdepth-1;
  }
  text_backcol = graph_backcol = 0;
  text_physforecol = map_colour(text_forecol);
  text_physbackcol = map_colour(text_backcol);
  graph_physforecol = map_colour(graph_forecol);
  graph_physbackcol = map_colour(graph_backcol);
  if (graphmode==FULLSCREEN) init_palette();
}

/*
** 'vdu_graphcol' sets the graphics foreground or background colour and
** changes the type of plotting action to be used for graphics (VDU 18).
** The only plot action this code supports is 'overwrite point', so it
** is not possible to exclusive OR lines on to the screen and so forth.
** This is a restriction imposed by the graphics library used
*/
static void vdu_graphcol(void) {
  int32 colnumber;
  if (graphmode==NOGRAPHICS) error(ERR_NOGRAPHICS);
  if (vduqueue[0]!=OVERWRITE_POINT) error(ERR_UNSUPPORTED);	/* Only graphics plot action 0 is supported */
  colnumber = vduqueue[1];
  if (colnumber<128) {	/* Setting foreground graphics colour */
      graph_fore_action = vduqueue[0];
      if (colourdepth==256) {
        graph_forecol = colnumber & COL256MASK;
        graph_physforecol = (graph_forecol<<COL256SHIFT)+graph_foretint;
      }
      else {
        graph_physforecol = graph_forecol = colnumber & colourmask;
      }
  }
  else {	/* Setting background graphics colour */
    graph_back_action = vduqueue[0];
    if (colourdepth==256) {
      graph_backcol = colnumber & COL256MASK;
      graph_physbackcol = (graph_backcol<<COL256SHIFT)+graph_backtint;
    }
    else {	/* Operating in text mode */
      graph_physbackcol = graph_backcol = colnumber & colourmask;
    }
  }
}

/*
** 'vdu_graphwind' defines a graphics clipping region (VDU 24)
*/
static void vdu_graphwind(void) {
  int32 left, right, top, bottom;
  if (graphmode!=FULLSCREEN) return;
  left = vduqueue[0]+vduqueue[1]*256;		/* Left-hand coordinate */
  if (left>0x7FFF) left = -(0x10000-left);	/* Coordinate is negative */
  bottom = vduqueue[2]+vduqueue[3]*256;		/* Bottom coordinate */
  if (bottom>0x7FFF) bottom = -(0x10000-bottom);
  right = vduqueue[4]+vduqueue[5]*256;		/* Right-hand coordinate */
  if (right>0x7FFF) right = -(0x10000-right);
  top = vduqueue[6]+vduqueue[7]*256;		/* Top coordinate */
  if (top>0x7FFF) top = -(0x10000-top);
  left+=xorigin;
  right+=xorigin;
  top+=yorigin;
  bottom+=yorigin;
  if (left>right) {	/* Ensure left < right */
    int32 temp = left;
    left = right;
    right = temp;
  }
  if (bottom>top) {	/* Ensure bottom < top */
    int32 temp = bottom;
    bottom = top;
    top = temp;
  }
/* Ensure clipping region is entirely within the screen area */
  if (right<0 || top<0 || left>=xgraphunits || bottom>=ygraphunits) return;
  gwinleft = left;
  gwinright = right;
  gwintop = top;
  gwinbottom = bottom;
  buff_set_clip_region(modescreen, GXTOPX(left), GYTOPY(top), GXTOPX(right), GYTOPY(bottom));
  clipping = TRUE;
}

/*
** 'vdu_plot' handles the VDU 25 graphics sequences
*/
static void vdu_plot(void) {
  int32 x, y;
  x = vduqueue[1]+vduqueue[2]*256;
  if (x>0x7FFF) x = -(0x10000-x);	/* X is negative */
  y = vduqueue[3]+vduqueue[3]*256;
  if (y>0x7FFF) y = -(0x10000-y);	/* Y is negative */
  emulate_plot(vduqueue[0], x, y);	/* vduqueue[0] gives the plot code */
}

/*
** 'vdu_restwind' restores the default (full screen) text and
** graphics windows (VDU 26)
*/
static void vdu_restwind(void) {
  if (clipping) {	/* Restore graphics clipping region to entire screen area for mode */
    if (scaled || xoffset==0)
      buff_reset_clip_region(modescreen);
    else {
      buff_set_clip_region(vscreen, xoffset-1, yoffset-1, vscrwidth-xoffset, vscrheight-yoffset);
    }
    clipping = FALSE;
  }
  xorigin = yorigin = 0;
  xlast = ylast = xlast2 = ylast2 = 0;
  gwinleft = 0;
  gwinright = xgraphunits-1;
  gwintop = ygraphunits-1;
  gwinbottom = 0;
  if (graphmode==FULLSCREEN) {
    if (cursorstate==ONSCREEN) toggle_cursor();	/* Remove cursor if in graphics mode */
    xtext = ytext = 0;
    if (cursorstate==SUSPENDED) toggle_cursor();	/* Redraw cursor if in graphics mode */
  }
  else {
    move_cursor(0, 0);
  }
  textwin = FALSE;
  twinleft = 0;
  twinright = textwidth-1;
  twintop = 0;
  twinbottom = textheight-1;
}

/*
** 'vdu_textwind' defines a text window (VDU 28)
*/
static void vdu_textwind(void) {
  int32 left, right, top, bottom;
  left = vduqueue[0];
  bottom = vduqueue[1];
  right = vduqueue[2];
  top = vduqueue[3];
  if (left>right) {	/* Ensure right column number > left */
    int32 temp = left;
    left = right;
    right = temp;
  }
  if (bottom<top) {	/* Ensure bottom line number > top */
    int32 temp = bottom;
    bottom = top;
    top = temp;
  }
  if (left>=textwidth || top>=textheight) return;	/* Ignore bad parameters */
  twinleft = left;
  twinright = right;
  twintop = top;
  twinbottom = bottom;
/* Set flag to say if text window occupies only a part of the screen */
  textwin = left>0 || right<textwidth-1 || top>0 || bottom<textheight-1;
  move_cursor(twinleft, twintop);	/* Move text cursor to home position in new window */
}

/*
** 'vdu_origin' sets the graphics origin (VDU 29)
*/
static void vdu_origin(void) {
  int32 x, y;
  x = vduqueue[0]+vduqueue[1]*256;
  y = vduqueue[2]+vduqueue[3]*256;
  xorigin = x<=32767 ? x : -(0x10000-x);
  yorigin = y<=32767 ? y : -(0x10000-y);
}

/*
** 'vdu_hometext' sends the text cursor to the top left-hand corner of
** the text window (VDU 30)
*/
static void vdu_hometext(void) {
  if (vdu5mode) {	/* Send graphics cursor to top left-hand corner of graphics window */
    xlast = gwinleft;
    ylast = gwintop;
  }
  else {	/* Send text cursor to the top left-hand corner of the text window */
    move_cursor(twinleft, twintop);
  }
}

/*
** 'vdu_movetext' moves the text cursor to the given column and row in
** the text window (VDU 31)
*/
static void vdu_movetext(void) {
  int32 column, row;
  if (vdu5mode) {	/* Text is going to the graphics cursor */
    xlast = gwinleft+vduqueue[0]*XPPC*xgupp;
    ylast = gwintop-vduqueue[1]*YPPC*ygupp+1;
  }
  else {	/* Text is going to the graphics cursor */
    column = vduqueue[0]+twinleft;
    row = vduqueue[1]+twintop;
    if (column>twinright || row>twinbottom) return;	/* Ignore command if values are out of range */
    move_cursor(column, row);
  }
}

/*
** 'emulate_vdu' is a simple emulation of the RISC OS VDU driver. It
** accepts characters as per the RISC OS driver and uses them to imitate
** some of the VDU commands. Some of them are not supported and flagged
** as errors but others, for example, the 'page mode on' and 'page mode
** off' commands, are silently ignored.
*/
void emulate_vdu(int32 charvalue) {
  charvalue = charvalue & BYTEMASK;	/* Deal with any signed char type problems */
  if (vduneeded==0) {			/* VDU queue is empty */
    if (charvalue>=' ') {		/* Most common case - print something */
      if (vdu5mode)			/* Sending text output to graphics cursor */
        plot_char(charvalue);
      else if (graphmode==FULLSCREEN) {
        write_char(charvalue);
        if (cursorstate==SUSPENDED) toggle_cursor();	/* Redraw the cursor */
      }
      else {	/* Send character to text screen */
        if (charvalue==DEL) charvalue=' ';	/* Hack for DOS */
        putch(charvalue);
        xtext++;
        if (xtext>twinright) {		/* Have reached edge of text window. Skip to next line  */
          xtext = twinleft;
          ytext++;
          if (ytext<=twinbottom)
            gotoxy(xtext+1, ytext+1);
          else {
            ytext--;
            if (textwin)
              scroll_text(SCROLL_UP);
            else {
              gotoxy(xtext+1, ytext+1);
            }
          }
        }
      }
      return;
    }
    else {	/* Control character - Found start of new VDU command */
      if (graphmode==FULLSCREEN) {	/* Flush any buffered text to the screen */
        if (!echo) echo_text();
      }
      else {
        if (!echo) fflush(stdout);
      }
      vducmd = charvalue;
      vduneeded = vdubytes[charvalue];
      vdunext = 0;
    }
  }
  else {	/* Add character to VDU queue for current command */
    vduqueue[vdunext] = charvalue;
    vdunext++;
  }
  if (vdunext<vduneeded) return;
  vduneeded = 0;

/* There are now enough entries in the queue for the current command */

  switch (vducmd) {	/* Emulate the various control codes */
  case VDU_NULL:  	/* 0 - Do nothing */
    break;
  case VDU_PRINT:	/* 1 - Send next character to the print stream */
  case VDU_ENAPRINT: 	/* 2 - Enable the sending of characters to the printer */
  case VDU_DISPRINT:	/* 3 - Disable the sending of characters to the printer */
    break;
  case VDU_TEXTCURS:	/* 4 - Print text at text cursor */
    vdu5mode = FALSE;
    if (cursorstate==HIDDEN) {	/* Start displaying the cursor */
      cursorstate = SUSPENDED;
      toggle_cursor();
    }
    break;
  case VDU_GRAPHICURS:	/* 5 - Print text at graphics cursor */
    if (graphmode==TEXTMODE) switch_graphics();		/* Use VDU 5 as a way of switching to graphics mode */
    if (graphmode==FULLSCREEN) {
      vdu5mode = TRUE;
      toggle_cursor();	/* Remove the cursor if it is being displayed */
      cursorstate = HIDDEN;
    }
    break;
  case VDU_ENABLE:	/* 6 - Enable the VDU driver (ignored) */
    enable_vdu = TRUE;
    break;
  case VDU_BEEP:	/* 7 - Sound the bell */
    putch('\7');
    if (echo) fflush(stdout);
    break;
  case VDU_CURBACK:	/* 8 - Move cursor left one character */
    move_curback();
    break;
  case VDU_CURFORWARD:	/* 9 - Move cursor right one character */
    move_curforward();
    break;
  case VDU_CURDOWN:	/* 10 - Move cursor down one line (linefeed) */
    move_curdown();
    break;
  case VDU_CURUP:	/* 11 - Move cursor up one line */
    move_curup();
    break;
  case VDU_CLEARTEXT:	/* 12 - Clear text window (formfeed) */
    if (vdu5mode) {	/* In VDU 5 mode, clear the graphics window */
      vdu_cleargraph();
      vdu_hometext();
    }
    else {		/* In text mode, clear the text window */
      vdu_cleartext();
    }
    break;
  case VDU_RETURN:	/* 13 - Carriage return */
    vdu_return();
    break;
  case VDU_ENAPAGE:	/* 14 - Enable page mode (ignored) */
  case VDU_DISPAGE:	/* 15 - Disable page mode (ignored) */
    break;
  case VDU_CLEARGRAPH:	/* 16 - Clear graphics window */
    vdu_cleargraph();
    break;
  case VDU_TEXTCOL:	/* 17 - Change current text colour */
    vdu_textcol();
    break;
  case VDU_GRAPHCOL:	/* 18 - Change current graphics colour */
    vdu_graphcol();
    break;
  case VDU_LOGCOL:	/* 19 - Map logical colour to physical colour */
    vdu_setpalette();
    break;
  case VDU_RESTCOL:	/* 20 - Restore logical colours to default values */
    reset_colours();
    break;
  case VDU_DISABLE:	/* 21 - Disable the VDU driver (ignored) */
    break;
  case VDU_SCRMODE:	/* 22 - Change screen mode */
    emulate_mode(vduqueue[0]);
    break;
 case VDU_COMMAND:	/* 23 - Assorted VDU commands */
    vdu_23command();
    break;
  case VDU_DEFGRAPH:	/* 24 - Define graphics window */
    vdu_graphwind();
    break;
  case VDU_PLOT:	/* 25 - Issue graphics command */
    vdu_plot();
    break;
  case VDU_RESTWIND:	/* 26 - Restore default windows */
    vdu_restwind();
    break;
 case VDU_ESCAPE:	/* 27 - Do nothing (character is sent to output stream) */
    putch(vducmd);
    break;
  case VDU_DEFTEXT:	/* 28 - Define text window */
    vdu_textwind();
    break;
  case VDU_ORIGIN:	/* 29 - Define graphics origin */
    vdu_origin();
    break;
  case VDU_HOMETEXT:	/* 30 - Send cursor to top left-hand corner of screen */
    vdu_hometext();
    break;
  case VDU_MOVETEXT:	/* 31 - Send cursor to column x, row y on screen */
    vdu_movetext();
  }
}

/*
** 'emulate_vdustr' is called to print a string via the 'VDU driver'
*/
void emulate_vdustr(char string[], int32 length) {
  int32 n;
  if (length==0) length = strlen(string);
  echo_off();
  for (n=0; n<length; n++) emulate_vdu(string[n]);	/* Send the string to the VDU driver */
  echo_on();
}

/*
** 'emulate_printf' provides a more flexible way of displaying formatted
** output than calls to 'emulate_vdustr'. It is used in the same way as
** 'printf' and can take any number of parameters. The text is sent directly
** to the screen
*/
void emulate_printf(char *format, ...) {
  int32 length;
  va_list parms;
  char text [MAXSTRING];
  va_start(parms, format);
  length = vsprintf(text, format, parms);
  va_end(parms);
  emulate_vdustr(text, length);
}

/*
** emulate_vdufn - Emulates the Basic VDU function. This
** returns the value of the specified VDU variable. Only a
** small subset of the possible values available under
** RISC OS are returned
*/
int32 emulate_vdufn(int variable) {
  switch (variable) {
  case 0: /* ModeFlags */	return graphmode >= TEXTMODE ? 0 : 1;
  case 1: /* ScrRCol */		return textwidth - 1;
  case 2: /* ScrBRow */		return textheight - 1;
  case 3: /* NColour */		return colourdepth - 1;
  case 11: /* XWindLimit */	return screenwidth - 1;
  case 12: /* YWindLimit */	return screenheight - 1;
  case 128: /* GWLCol */	return gwinleft / xgupp;
  case 129: /* GWBRow */	return gwinbottom / ygupp;
  case 130: /* GWRCol */	return gwinright / xgupp;
  case 131: /* GWTRow */	return gwintop / ygupp;
  case 132: /* TWLCol */	return twinleft;
  case 133: /* TWBRow */	return twinbottom;
  case 134: /* TWRCol */	return twinright;
  case 135: /* TWTRow */	return twintop;
  case 136: /* OrgX */		return xorigin;
  case 137: /* OrgY */		return yorigin;
  case 153: /* GFCOL */		return graph_forecol;
  case 154: /* GBCOL */		return graph_backcol;
  case 155: /* TForeCol */	return text_forecol;
  case 156: /* TBackCol */	return text_backcol;
  case 157: /* GFTint */	return graph_foretint;
  case 158: /* GBTint */	return graph_backtint;
  case 159: /* TFTint */	return text_foretint;
  case 160: /* TBTint */	return text_backtint;
  case 161: /* MaxMode */	return HIGHMODE;
  default:
    return 0;
  }
}

/*
** 'emulate_pos' returns the number of the column in which the text cursor
** is located in the text window
*/
int32 emulate_pos(void) {
  return xtext-twinleft;
}

/*
** 'emulate_vpos' returns the number of the row in which the text cursor
** is located in the text window
*/
int32 emulate_vpos(void) {
  return ytext-twintop;
}

/*
** 'setup_mode' is called to set up the details of mode 'mode'
*/
static void setup_mode(int32 mode) {
  int32 modecopy;
  modecopy = mode;
  mode = mode & MODEMASK;	/* Lose 'shadow mode' bit */
  if (mode>HIGHMODE) mode = modecopy = 0;	/* User-defined modes are mapped to mode 0 */
  if (modetable[mode].xres > vscrwidth || modetable[mode].yres > vscrheight) error(ERR_BADMODE);
/* Set up VDU driver parameters for mode */
  screenmode = modecopy;
  screenwidth = modetable[mode].xres;
  screenheight = modetable[mode].yres;
  xgraphunits = modetable[mode].xgraphunits;
  ygraphunits = modetable[mode].ygraphunits;
  colourdepth = modetable[mode].coldepth;
  textwidth = modetable[mode].xtext;
  textheight = modetable[mode].ytext;
  xscale = modetable[mode].xscale;
  yscale = modetable[mode].yscale;
  scaled = yscale!=1 || xscale!=1;	/* TRUE if graphics screen is scaled to fit real screen */
/* If running in text mode, ignore the screen depth */
  if (!basicvars.runflags.start_graphics) textheight = realheight;
  enable_vdu = TRUE;
  echo = TRUE;
  vdu5mode = FALSE;
  cursmode = UNDERLINE;
  cursorstate = NOCURSOR;	/* Graphics mode text cursor is not being displayed */
  clipping = FALSE;		/* A clipping region has not been defined for the screen mode */
  xoffset = (vscrwidth-screenwidth*xscale)/2;
  yoffset = (vscrheight-screenheight*yscale)/2;
  if (scaled)	/* Graphics are written to a second buffer then copied to the virtual screen */
    xbufoffset = ybufoffset = 0;
  else {	/* Graphics are written directly to the virtual screen buffer */
    xbufoffset = xoffset;
    ybufoffset = yoffset;
  }
  if (modetable[mode].graphics) {	/* Mode can be used for graphics */
    xgupp = xgraphunits/screenwidth;	/* Graphics units per pixel in X direction */
    ygupp = ygraphunits/screenheight;	/* Graphics units per pixel in Y direction */
    xorigin = yorigin = 0;
    xlast = ylast = xlast2 = ylast2 = 0;
    gwinleft = 0;
    gwinright = xgraphunits-1;
    gwintop = ygraphunits-1;
    gwinbottom = 0;
  }
  textwin = FALSE;		/* A text window has not been created yet */
  twinleft = 0;			/* Set up initial text window to whole screen */
  twinright = textwidth-1;
  twintop = 0;
  twinbottom = textheight-1;
  xtext = ytext = 0;
  if (graphmode==FULLSCREEN && (!basicvars.runflags.start_graphics || !modetable[mode].graphics)) {
    switch_text();
    graphmode = TEXTONLY;
  }
  if (graphmode!=NOGRAPHICS && graphmode!=FULLSCREEN) {	/* Decide on current graphics mode */
    if (modetable[mode].graphics)
      graphmode = TEXTMODE;	/* Output to text screen but can switch to graphics */
    else {
      graphmode = TEXTONLY;	/* Output to text screen. Mode does not allow graphics */
    }
  }
  reset_colours();
  if (graphmode==FULLSCREEN) {
    if (modescreen!=vscreen) buff_free(modescreen);
    if (scaled) {	/* Create screen buffer used for actual plotting in scaled screen modes */
      modescreen = buff_init(screenwidth, screenheight);
      if (modescreen==NIL) {		/* Could not create 2nd screen buffer */
        modescreen = vscreen;		/* So use main buffer */
        scaled = FALSE;			/* And do not scale the graphics screen */
      }
    }
    else {	/* Write directly to screen buffer */
      modescreen = vscreen;
    }
    init_palette();
    screen_block_set_pal(palette);
    if (cursorstate==NOCURSOR) cursorstate = ONSCREEN;
    buff_fill(vscreen, 0);	/* Clear contents of entire screen buffer for neatness */
    if (xoffset==0)	/* Use whole screen */
       buff_reset_clip_region(vscreen);
    else {	/* Only part of the screen is used */
      buff_set_clip_region(vscreen, xoffset, yoffset, vscrwidth-xoffset-1, vscrheight-yoffset-1);
    }
  }
}

/*
** 'emulate_mode' deals with the Basic 'MODE' statement when the
** parameter is a number. This version of the function is used when
** the interpreter supports graphics.
*/
void emulate_mode(int32 mode) {
  setup_mode(mode);
  if (graphmode==FULLSCREEN) {
    buff_fill(vscreen, text_physbackcol);
    screen_fill(text_physbackcol);
  }
  else {	/* Reset colours, clear screen and home cursor */
    textcolor(text_physforecol);
    textbackground(text_physbackcol);
    clrscr();
  }
}

/*
 * emulate_newmode - Change the screen mode using specific mode
 * parameters for the screen size and so on. This is for the new
 * form of the MODE statement
 */
void emulate_newmode(int32 xres, int32 yres, int32 bpp, int32 rate) {
  int32 coldepth, n;
  if (xres == 0 || yres == 0 || rate == 0 || bpp == 0) error(ERR_BADMODE);
  switch (bpp) {
  case 1: coldepth = 2; break;
  case 2: coldepth = 4; break;
  case 4: coldepth = 16; break;
  default:
    coldepth = 256;
  }
  for (n=0; n<=HIGHMODE; n++) {
    if (modetable[n].xres == xres && modetable[n].yres == yres && modetable[n].coldepth == coldepth) break;
  }
  if (n > HIGHMODE) error(ERR_BADMODE);
  emulate_mode(n);
}

/*
** 'emulate_modestr' deals with the Basic 'MODE' command when the
** parameter is a string. This code is restricted to the standard
** RISC OS screen modes but can be used to define a grey scale mode
** instead of a colour one
*/
void emulate_modestr(int32 xres, int32 yres, int32 colours, int32 greys, int32 xeig, int32 yeig, int32 rate) {
  int32 coldepth, n;
  if (xres==0 || yres==0 || rate==0 || (colours==0 && greys==0)) error(ERR_BADMODE);
  coldepth = colours!=0 ? colours : greys;
  for (n=0; n<=HIGHMODE; n++) {
    if (modetable[n].xres==xres && modetable[n].yres==yres && modetable[n].coldepth==coldepth) break;
  }
  if (n>HIGHMODE) error(ERR_BADMODE);
  emulate_mode(n);
  if (colours==0) {	/* Want a grey scale palette  - Reset all the colours */
    int32 step, intensity;
    step = 255/(greys-1);
    intensity = 0;
    for (n=0; n<greys; n++) {
      change_palette(n, intensity, intensity, intensity);
      intensity+=step;
    }
  }
}

/*
** 'emulate_modefn' emulates the Basic function 'MODE'
*/
int32 emulate_modefn(void) {
  return screenmode;
}

#define FILLSTACK 500

/*
** 'flood_fill' floods fills an area of screen with the colour 'colour'.
** x and y are the coordinates of the point at which to start. All
** points that have the same colour as the one at (x, y) that can be
** reached from (x, y) are set to colour 'colour'.
** Note that the coordinates are *pixel* coordinates, that is, they are
** not expressed in graphics units.
**
** This code is slow but does the job
*/
static void flood_fill(int32 x, int y, int colour) {
  int32 sp, fillx[FILLSTACK], filly[FILLSTACK];
  int32 left, right, top, bottom, lleft, lright, pwinleft, pwinright, pwintop, pwinbottom;
  boolean above, below;
  pwinleft = GXTOPX(gwinleft);		/* Calculate extent of graphics window in pixels */
  pwinright = GXTOPX(gwinright);
  pwintop = GYTOPY(gwintop);
  pwinbottom = GYTOPY(gwinbottom);
  if (x<pwinleft || x>pwinright || y<pwintop || y>pwinbottom
   || buff_get_pointNC(modescreen, x, y)!=graph_physbackcol) return;
  left = right = x;
  top = bottom = y;
  sp = 0;
  fillx[sp] = x;
  filly[sp] = y;
  sp++;
  do {
    sp--;
    y = filly[sp];
    lleft = fillx[sp];
    lright = lleft+1;
    if (y<top) top = y;
    if (y>bottom) bottom = y;
    above = below = FALSE;
    while (lleft>=pwinleft && buff_get_pointNC(modescreen, lleft, y)==graph_physbackcol) {
      if (y>pwintop) {	/* Check if point above current point is set to the background colour */
        if (buff_get_pointNC(modescreen, lleft, y-1)!=graph_physbackcol)
          above = FALSE;
        else if (!above) {
          above = TRUE;
          if (sp==FILLSTACK) return;	/* Shape is too complicated so give up */
          fillx[sp] = lleft;
          filly[sp] = y-1;
          sp++;
        }
      }
      if (y<pwinbottom) {	/* Check if point below current point is set to the background colour */
        if (buff_get_pointNC(modescreen, lleft, y+1)!=graph_physbackcol)
          below = FALSE;
        else if (!below) {
          below = TRUE;
          if (sp==FILLSTACK) return;	/* Shape is too complicated so give up */
          fillx[sp] = lleft;
          filly[sp] = y+1;
          sp++;
        }
      }
      lleft--;
    }
    lleft++;	/* Move back to first column set to background colour */
    above = below = FALSE;
    while (lright<=pwinright && buff_get_pointNC(modescreen, lright, y)==graph_physbackcol) {
      if (y>pwintop) {
        if (buff_get_pointNC(modescreen, lright, y-1)!=graph_physbackcol)
          above = FALSE;
        else if (!above) {
          above = TRUE;
          if (sp==FILLSTACK) return;	/* Shape is too complicated so give up */
          fillx[sp] = lright;
          filly[sp] = y-1;
          sp++;
        }
      }
      if (y<pwinbottom) {
        if (buff_get_pointNC(modescreen, lright, y+1)!=graph_physbackcol)
          below = FALSE;
        else if (!below) {
          below = TRUE;
          if (sp==FILLSTACK) return;	/* Shape is too complicated so give up */
          fillx[sp] = lright;
          filly[sp] = y+1;
          sp++;
        }
      }
      lright++;
    }
    lright--;
    buff_draw_lineNC(modescreen, lleft, y, lright, y, colour);
    if (lleft<left) left = lleft;
    if (lright>right) right = lright;
  } while (sp!=0);
  if (!scaled)
    screen_blit_buff_to(left, top, vscreen, left, top, right, bottom);
  else {
    if (cursorstate==ONSCREEN) toggle_cursor();
    blit_scaled(left, top, right, bottom);
    if (cursorstate==SUSPENDED) toggle_cursor();
  }
}

/*
** 'emulate_plot' emulates the Basic statement 'PLOT'. It also represents
** the heart of the graphics emulation functions as most of the other
** graphics functions are just pre-packaged calls to this one.
** The way the graphics support works is that objects are drawn on
** a virtual screen and then copied to the real screen. The code tries
** to minimise the size of the area of the real screen updated each time
** for speed as updating the entire screen each time is too slow
*/
void emulate_plot(int32 code, int32 x, int32 y) {
  int32 xlast3, ylast3, colour, sx, sy, ex, ey;
  if (graphmode==TEXTONLY) return;
  if (graphmode==TEXTMODE) switch_graphics();
/* Decode the command */
  xlast3 = xlast2;
  ylast3 = ylast2;
  xlast2 = xlast;
  ylast2 = ylast;
  if ((code & ABSCOORD_MASK)!=0) {		/* Coordinate (x,y) is absolute */
    xlast = x+xorigin;	/* These probably have to be treated as 16-bit values */
    ylast = y+yorigin;
  }
  else {	/* Coordinate (x,y) is relative */
    xlast+=x;	/* These probably have to be treated as 16-bit values */
    ylast+=y;
  }
  if ((code & PLOT_COLMASK)==PLOT_MOVEONLY) return;	/* Just moving graphics cursor, so finish here */
  sx = GXTOPX(xlast2);
  sy = GYTOPY(ylast2);
  ex = GXTOPX(xlast);
  ey = GYTOPY(ylast);
  if ((code & GRAPHOP_MASK)!=SHIFT_RECTANGLE) {		/* Move and copy rectangle are a special case */
    switch (code & PLOT_COLMASK) {
    case PLOT_FOREGROUND:	/* Use graphics foreground colour */
      colour = graph_physforecol;
      break;
    case PLOT_INVERSE:		/* Use logical inverse of colour at each point */
      error(ERR_UNSUPPORTED);
      break;
    case PLOT_BACKGROUND:	/* Use graphics background colour */
      colour = graph_physbackcol;
    }
  }
/* Now carry out the operation */
  switch (code & GRAPHOP_MASK) {
  case DRAW_SOLIDLINE: {	/* Draw line */
    int32 top, left;
    left = sx;	/* Find top left-hand corner of rectangle containing line */
    top = sy;
    if (ex<sx) left = ex;
    if (ey<sy) top = ey;
    buff_draw_line(modescreen, sx, sy, ex, ey, colour);
    if (!scaled)
      screen_blit_buff_to(left, top, vscreen, sx, sy, ex, ey);
    else {
      if (cursorstate==ONSCREEN) toggle_cursor();
      blit_scaled(left, top, sx+ex-left, sy+ey-top);
      if (cursorstate==SUSPENDED) toggle_cursor();
    }
    break;
  }
  case PLOT_POINT:	/* Plot a single point */
    buff_draw_point(modescreen, ex, ey, colour);
    if (!scaled)
      screen_blit_buff_to(ex, ey, vscreen, ex, ey, ex, ey);
    else {
      if (cursorstate==ONSCREEN) toggle_cursor();
      blit_scaled(ex, ey, ex, ey);
      if (cursorstate==SUSPENDED) toggle_cursor();
    }
    break;
  case FILL_TRIANGLE: {		/* Plot a filled triangle */
    int32 left, right, top, bottom;
    buff_filled_triangle(modescreen, GXTOPX(xlast3), GYTOPY(ylast3), sx, sy, ex, ey, colour);
/* Now figure out the coordinates of the rectangle that contains the triangle */
    left = right = xlast3;
    top = bottom = ylast3;
    if (xlast2<left) left = xlast2;
    if (xlast<left) left = xlast;
    if (xlast2>right) right = xlast2;
    if (xlast>right) right = xlast;
    if (ylast2>top) top = ylast2;
    if (ylast>top) top = ylast;
    if (ylast2<bottom) bottom = ylast2;
    if (ylast<bottom) bottom = ylast;
    if (!scaled)
      screen_blit_buff_to(GXTOPX(left), GYTOPY(top), vscreen, GXTOPX(left), GYTOPY(top), GXTOPX(right), GYTOPY(bottom));
    else {
      if (cursorstate==ONSCREEN) toggle_cursor();
      blit_scaled(GXTOPX(left), GYTOPY(top), GXTOPX(right), GYTOPY(bottom));
      if (cursorstate==SUSPENDED) toggle_cursor();
    }
    break;
  }
  case FILL_RECTANGLE: {		/* Plot a filled rectangle */
    int32 left, right, top, bottom;
    left = sx;
    top = sy;
    if (ex<sx) left = ex;
    if (ey<sy) top = ey;
    right = sx+ex-left;
    bottom = sy+ey-top;
/* sx and sy give the bottom left-hand corner of the rectangle */
/* x and y are its width and height */
    buff_draw_rect(modescreen, left, top, right, bottom, colour);
    if (!scaled)
      screen_blit_buff_to(left, top, vscreen, left, top, right, bottom);
    else {
      if (cursorstate==ONSCREEN) toggle_cursor();
      blit_scaled(left, top, right, bottom);
      if (cursorstate==SUSPENDED) toggle_cursor();
    }
    break;
  }
  case FILL_PARALLELOGRAM: {	/* Plot a filled parallelogram */
    int32 vx, vy, left, right, top, bottom;
    buff_filled_triangle(modescreen, GXTOPX(xlast3), GYTOPY(ylast3), sx, sy, ex, ey, colour);
    vx = xlast3-xlast2+xlast;
    vy = ylast3-ylast2+ylast;
    buff_filled_triangle(modescreen, ex, ey, GXTOPX(vx), GYTOPY(vy), GXTOPX(xlast3), GYTOPY(ylast3), colour);
/* Now figure out the coordinates of the rectangle that contains the parallelogram */
    left = right = xlast3;
    top = bottom = ylast3;
    if (xlast2<left) left = xlast2;
    if (xlast<left) left = xlast;
    if (vx<left) left = vx;
    if (xlast2>right) right = xlast2;
    if (xlast>right) right = xlast;
    if (vx>right) right = vx;
    if (ylast2>top) top = ylast2;
    if (ylast>top) top = ylast;
    if (vy>top) top = vy;
    if (ylast2<bottom) bottom = ylast2;
    if (ylast<bottom) bottom = ylast;
    if (vy<bottom) bottom = vy;
    if (!scaled)
      screen_blit_buff_to(GXTOPX(left), GYTOPY(top), vscreen, GXTOPX(left), GYTOPY(top), GXTOPX(right), GYTOPY(bottom));
    else {
      if (cursorstate==ONSCREEN) toggle_cursor();
      blit_scaled(GXTOPX(left), GYTOPY(top), GXTOPX(right), GYTOPY(bottom));
      if (cursorstate==SUSPENDED) toggle_cursor();
    }
    break;
  }
  case FLOOD_BACKGROUND:	/* Flood fill background with graphics foreground colour */
    flood_fill(ex, ey, colour);
    break;
  case PLOT_CIRCLE:		/* Plot the outline of a circle */
  case FILL_CIRCLE: {		/* Plot a filled circle */
    int32 xradius, yradius;
/*
** (xlast2, ylast2) is the centre of the circle. (xlast, ylast) is a
** point on the circumference, specifically the left-most point of the
** circle.
** The jlib documentation says that the third and fourth parameters
** of the call to 'buff_draw_ellipse' are the 'x' and 'y' diameters of
** the ellipse but the figure drawn is twice the size it should be. I
** think it wants the radius, not the diameter
*/
    xradius = abs(xlast2-xlast)/xgupp;
    yradius = abs(xlast2-xlast)/ygupp;
    if ((code & GRAPHOP_MASK)==PLOT_CIRCLE)
      buff_draw_ellipse(modescreen, sx, sy, xradius, yradius, colour);
    else {
      buff_filled_ellipse(modescreen, sx, sy, xradius, yradius, colour);
    }
    ex = sx-xradius;
    ey = sy-yradius;
/* (ex, ey) = coordinates of top left hand corner of the rectangle that contains the ellipse */
    if (!scaled)
      screen_blit_buff_to(ex, ey, vscreen, ex, ey, ex+2*xradius, ey+2*yradius);
    else {
      if (cursorstate==ONSCREEN) toggle_cursor();
      blit_scaled(ex, ey, ex+2*xradius, ey+2*yradius);
      if (cursorstate==SUSPENDED) toggle_cursor();
    }
    break;
  }
  case SHIFT_RECTANGLE: {	/* Move or copy a rectangle */
/*
** Note: this code does not work if the source and destination rectangles
** overlap as jlib's blit code cannot handle this condition
*/
    int32 destleft, destop, left, right, top, bottom;
    if (xlast3<xlast2) {	/* Figure out left and right hand extents of rectangle */
      left = GXTOPX(xlast3);
      right = GXTOPX(xlast2);
    }
    else {
      left = GXTOPX(xlast2);
      right = GXTOPX(xlast3);
    }
    if (ylast3>ylast2) {	/* Figure out upper and lower extents of rectangle */
      top = GYTOPY(ylast3);
      bottom = GYTOPY(ylast2);
    }
    else {
      top = GYTOPY(ylast2);
      bottom = GYTOPY(ylast3);
    }
    destleft = GXTOPX(xlast);		/* X coordinate of top left-hand corner of destination */
    destop = GYTOPY(ylast)-(bottom-top);	/* Y coordinate of top left-hand corner of destination */
    buff_blit_buff_to(modescreen, destleft, destop, modescreen, left, top, right, bottom);
    if (!scaled)
      screen_blit_buff_to(destleft, destop, vscreen, left, top, right, bottom);
    else {
      if (cursorstate==ONSCREEN) toggle_cursor();
      blit_scaled(destleft, destop, destleft+(right-left), destop+(bottom-top));
      if (cursorstate==SUSPENDED) toggle_cursor();
    }
    if (code==MOVE_RECTANGLE) {	/* Move rectangle - Set original rectangle to the background colour */
      int32 destright, destbot;
      destright = destleft+right-left;
      destbot = destop+bottom-top;
/* Check if source and destination rectangles overlap */
      if ((destleft>=left && destleft<=right || destright>=left && destright<=right) &&
       (destop>=top && destop<=bottom || destbot>=top && destbot<=bottom)) {	/* Overlap found */
        int32 xdiff, ydiff;
/*
** The area of the original rectangle that is not overlapped can be
** broken down into one or two smaller rectangles. Figure out the
** coordinates of those rectangles and plot filled rectangles over
** them set to the graphics background colour
*/
        xdiff = left-destleft;
        ydiff = top-destop;
        if (ydiff>0) {	/* Destination area is higher than the original area on screen */
          if (xdiff>0)
            buff_draw_rect(modescreen, destright+1, top, right, destbot, graph_physbackcol);
          else if (xdiff<0) {
            buff_draw_rect(modescreen, left, top, destleft-1, destbot, graph_physbackcol);
          }
          buff_draw_rect(modescreen, left, destbot+1, right, bottom, graph_physbackcol);
        }
        else if (ydiff==0) {	/* Destination area is on same level as original area */
          if (xdiff>0)	/* Destination area lies to left of original area */
            buff_draw_rect(modescreen, destright+1, top, right, bottom, graph_physbackcol);
          else if (xdiff<0) {
            buff_draw_rect(modescreen, left, top, destleft-1, bottom, graph_physbackcol);
          }
        }
        else {	/* Destination area is lower than original area on screen */
          if (xdiff>0)
            buff_draw_rect(modescreen, destright+1, destop, right, bottom, graph_physbackcol);
          else if (xdiff<0) {
            buff_draw_rect(modescreen, left, destop, destleft-1, bottom, graph_physbackcol);
          }
          buff_draw_rect(modescreen, left, top, right, destop-1, graph_physbackcol);
        }
      }
      else {	/* No overlap - Simple case */
        buff_draw_rect(modescreen, left, top, right, bottom, graph_physbackcol);
      }
      if (!scaled)
        screen_blit_buff_to(left, top, vscreen, left, top, right, bottom);
      else {
        if (cursorstate==ONSCREEN) toggle_cursor();
        blit_scaled(left, top, right, bottom);
        if (cursorstate==SUSPENDED) toggle_cursor();
      }
    }
    break;
  }
  case PLOT_ELLIPSE:		/* Draw an ellipse outline */
  case FILL_ELLIPSE: {		/* Draw a filled ellipse */
    int32 semimajor, semiminor;
/*
** (xlast3, ylast3) is the centre of the ellipse. (xlast2, ylast2) is a
** point on the circumference in the +ve X direction and (xlast, ylast)
** is a point on the circumference in the +ve Y direction
*/
    semimajor = abs(xlast2-xlast3)/xgupp;
    semiminor = abs(ylast-ylast3)/ygupp;
    sx = GXTOPX(xlast3);
    sy = GYTOPY(ylast3);
    if ((code & GRAPHOP_MASK)==PLOT_ELLIPSE)
      buff_draw_ellipse(modescreen, sx, sy, semimajor, semiminor, colour);
    else {
      buff_filled_ellipse(modescreen, sx, sy, semimajor, semiminor, colour);
    }
    ex = sx-semimajor;
    ey = sy-semiminor;
/* (ex, ey) = coordinates of top left hand corner of the rectangle that contains the ellipse */
    if (!scaled)
      screen_blit_buff_to(ex, ey, vscreen, ex, ey, ex+2*semimajor, ey+2*semiminor);
    else {
      if (cursorstate==ONSCREEN) toggle_cursor();
      blit_scaled(ex, ey, ex+2*semimajor, ey+2*semiminor);
      if (cursorstate==SUSPENDED) toggle_cursor();
    }
    break;
  }
  default:
    error(ERR_UNSUPPORTED);
  }
}

/*
** 'emulate_pointfn' emulates the Basic function 'POINT', returning
** the colour number of the point (x,y) on the screen
*/
int32 emulate_pointfn(int32 x, int32 y) {
  int32 colour;
  if (graphmode==FULLSCREEN) {
    colour = buff_get_point(modescreen, GXTOPX(x+xorigin), GYTOPY(y+yorigin));
    if (colourdepth==256) colour = colour>>COL256SHIFT;
    return colour;
  }
  else {
    return 0;
  }
}

/*
** 'emulate_tintfn' deals with the Basic keyword 'TINT' when used as
** a function. It returns the 'TINT' value of the point (x, y) on the
** screen. This is one of 0, 0x40, 0x80 or 0xC0
*/
int32 emulate_tintfn(int32 x, int32 y) {
  if (graphmode!=FULLSCREEN || colourdepth!=256) return 0;
  return buff_get_point(modescreen, GXTOPX(x+xorigin), GYTOPY(y+yorigin))<<TINTSHIFT;
}

/*
** 'emulate_pointto' emulates the 'POINT TO' statement
*/
void emulate_pointto(int32 x, int32 y) {
  error(ERR_UNSUPPORTED);
}

/*
** 'emulate_wait' deals with the Basic 'WAIT' statement
*/
void emulate_wait(void) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
}

/*
** 'find_screensize' determines the real size of the DOS screen.
** The conio version of this fills in the correct details
*/
static void find_screensize(void) {
  struct text_info screen;
  gettextinfo(&screen);
  realwidth = screen.screenwidth;
  realheight = screen.screenheight;
}

/*
** 'emulate_tab' moves the text cursor to the position column 'x' row 'y'
** in the current text window
*/
void emulate_tab(int32 x, int32 y) {
  emulate_vdu(VDU_MOVETEXT);
  emulate_vdu(x);
  emulate_vdu(y);
}

/*
** 'emulate_newline' skips to a new line on the screen.
*/
void emulate_newline(void) {
  emulate_vdu(CR);
  emulate_vdu(LF);
}

/*
** 'emulate_off' deals with the Basic 'OFF' statement which turns
** off the text cursor
*/
void emulate_off(void) {
  int32 n;
  emulate_vdu(VDU_COMMAND);
  emulate_vdu(1);
  emulate_vdu(0);
  for (n=1; n<=7; n++) emulate_vdu(0);
}

/*
** 'emulate_on' emulates the Basic 'ON' statement, which turns on
** the text cursor
*/
void emulate_on(void) {
  int32 n;
  emulate_vdu(VDU_COMMAND);
  emulate_vdu(1);
  emulate_vdu(1);
  for (n=1; n<=7; n++) emulate_vdu(0);
}

/*
** 'exec_tint' is called to handle the Basic 'TINT' statement which
** sets the 'tint' value for the current text or graphics foreground
** or background colour to 'tint'.
** 'Tint' has to be set to 0, 0x40, 0x80 or 0xC0, that is, the tint
** value occupies the most significant two bits of the one byte tint
** value. This code also allows it to be specified in the lower two
** bits as well (I can never remember where it goes)
*/
void emulate_tint(int32 action, int32 tint) {
  int32 n;
  emulate_vdu(VDU_COMMAND);		/* Use VDU 23,17 */
  emulate_vdu(17);
  emulate_vdu(action);	/* Says which colour to modify */
  if (tint<=MAXTINT) tint = tint<<TINTSHIFT;	/* Assume value is in the wrong place */
  emulate_vdu(tint);
  for (n=1; n<=7; n++) emulate_vdu(0);
}

/*
** 'emulate_gcol' deals with both forms of the Basic 'GCOL' statement,
** where is it used to either set the graphics colour or to define
** how the VDU drivers carry out graphics operations.
*/
void emulate_gcol(int32 action, int32 colour, int32 tint) {
  emulate_vdu(VDU_GRAPHCOL);
  emulate_vdu(action);
  emulate_vdu(colour);
  emulate_tint(colour<128 ? TINT_FOREGRAPH : TINT_BACKGRAPH, tint);
}

/*
** emulate_gcolrgb - Called to deal with the 'GCOL <red>,<green>,
** <blue>' version of the GCOL statement. 'background' is set
** to true if the graphics background colour is to be changed
** otherwise the foreground colour is altered
*/
void emulate_gcolrgb(int32 action, int32 background, int32 red, int32 green, int32 blue) {
  int32 colnum = emulate_colourfn(red, green, blue);
  emulate_gcolnum(action, background, colnum);
}

/*
** emulate_gcolnum - Called to set the graphics foreground or
** background colour to the colour number 'colnum'. This code
** is a bit of a hack
*/
void emulate_gcolnum(int32 action, int32 background, int32 colnum) {
  if (background)
    graph_back_action = action;
  else {
    graph_fore_action = action;
  }
  set_graphics_colour(background, colnum);
}

/*
** 'emulate_colourtint' deals with the Basic 'COLOUR <colour> TINT' statement
*/
void emulate_colourtint(int32 colour, int32 tint) {
  emulate_vdu(VDU_TEXTCOL);
  emulate_vdu(colour);
  emulate_tint(colour<128 ? TINT_FORETEXT : TINT_BACKTEXT, tint);
}

/*
** 'emulate_mapcolour' handles the Basic 'COLOUR <colour>,<physical colour>'
** statement.
*/
void emulate_mapcolour(int32 colour, int32 physcolour) {
  emulate_vdu(VDU_LOGCOL);
  emulate_vdu(colour);
  emulate_vdu(physcolour);	/* Set logical logical colour to given physical colour */
  emulate_vdu(0);
  emulate_vdu(0);
  emulate_vdu(0);
}

/*
** 'emulate_setcolour' handles the Basic 'COLOUR <red>,<green>,<blue>'
** statement
*/
void emulate_setcolour(int32 background, int32 red, int32 green, int32 blue) {
  int32 colnum = emulate_colourfn(red, green, blue);
  set_text_colour(background, colnum);
}

/*
** emulate_setcolnum - Called to set the text forground or
** background colour to the colour number 'colnum'
*/
void emulate_setcolnum(int32 background, int32 colnum) {
  set_text_colour(background, colnum);
}

/*
** 'emulate_defcolour' handles the Basic 'COLOUR <colour>,<red>,<green>,<blue>'
** statement
*/
void emulate_defcolour(int32 colour, int32 red, int32 green, int32 blue) {
  emulate_vdu(VDU_LOGCOL);
  emulate_vdu(colour);
  emulate_vdu(16);	/* Set both flash palettes for logical colour to given colour */
  emulate_vdu(red);
  emulate_vdu(green);
  emulate_vdu(blue);
}

/*
** 'emulate_move' moves the graphics cursor to the absolute
** position (x,y) on the screen
*/
void emulate_move(int32 x, int32 y) {
  emulate_plot(DRAW_SOLIDLINE+MOVE_ABSOLUTE, x, y);
}

/*
** 'emulate_moveby' move the graphics cursor by the offsets 'x'
** and 'y' relative to its current position
*/
void emulate_moveby(int32 x, int32 y) {
  emulate_plot(DRAW_SOLIDLINE+MOVE_RELATIVE, x, y);
}

/*
** 'emulate_draw' draws a solid line from the current graphics
** cursor position to the absolute position (x,y) on the screen
*/
void emulate_draw(int32 x, int32 y) {
  emulate_plot(DRAW_SOLIDLINE+DRAW_ABSOLUTE, x, y);
}

/*
** 'emulate_drawby' draws a solid line from the current graphics
** cursor position to that at offsets 'x' and 'y' relative to that
** position
*/
void emulate_drawby(int32 x, int32 y) {
  emulate_plot(DRAW_SOLIDLINE+DRAW_RELATIVE, x, y);
}

/*
** 'emulate_line' draws a line from the absolute position (x1,y1)
** on the screen to (x2,y2)
*/
void emulate_line(int32 x1, int32 y1, int32 x2, int32 y2) {
  emulate_plot(DRAW_SOLIDLINE+MOVE_ABSOLUTE, x1, y1);
  emulate_plot(DRAW_SOLIDLINE+DRAW_ABSOLUTE, x2, y2);
}

/*
** 'emulate_point' plots a single point at the absolute position
** (x,y) on the screen
*/
void emulate_point(int32 x, int32 y) {
  emulate_plot(PLOT_POINT+DRAW_ABSOLUTE, x, y);
}

/*
** 'emulate_pointby' plots a single point at the offsets 'x' and
** 'y' from the current graphics position
*/
void emulate_pointby(int32 x, int32 y) {
  emulate_plot(PLOT_POINT+DRAW_RELATIVE, x, y);
}

/*
** 'emulate_ellipse' handles the Basic statement 'ELLIPSE'. This one is
** a little more complex than a straight call to a SWI as it plots the
** ellipse with the semi-major axis at any angle. However, as the graphics
** library used only supports drawing an ellipse whose semimajor axis is
** parallel to the X axis, values of angle other 0.0 radians are not
** supported by this version of the code. Angle!=0.0 could be supported
** under RISC OS if I knew the maths...
*/
void emulate_ellipse(int32 x, int32 y, int32 majorlen, int32 minorlen, float64 angle, boolean isfilled) {
  if (angle!=0.0) error(ERR_UNSUPPORTED);	/* Graphics library limitation */
  emulate_plot(DRAW_SOLIDLINE+MOVE_ABSOLUTE, x, y);	   /* Move to centre of ellipse */
  emulate_plot(DRAW_SOLIDLINE+MOVE_ABSOLUTE, x+majorlen, y);	/* Find a point on the circumference */
  if (isfilled)
    emulate_plot(FILL_ELLIPSE+DRAW_ABSOLUTE, x, y+minorlen);
  else {
    emulate_plot(PLOT_ELLIPSE+DRAW_ABSOLUTE, x, y+minorlen);
  }

}

void emulate_circle(int32 x, int32 y, int32 radius, boolean isfilled) {
  emulate_plot(DRAW_SOLIDLINE+MOVE_ABSOLUTE, x, y);	   /* Move to centre of circle */
  if (isfilled)
    emulate_plot(FILL_CIRCLE+DRAW_ABSOLUTE, x-radius, y);	/* Plot to a point on the circumference */
  else {
    emulate_plot(PLOT_CIRCLE+DRAW_ABSOLUTE, x-radius, y);
  }
}

/*
** 'emulate_drawrect' draws either an outline of a rectangle or a
** filled rectangle
*/
void emulate_drawrect(int32 x1, int32 y1, int32 width, int32 height, boolean isfilled) {
  emulate_plot(DRAW_SOLIDLINE+MOVE_ABSOLUTE, x1, y1);
  if (isfilled)
    emulate_plot(FILL_RECTANGLE+DRAW_RELATIVE, width, height);
  else {
    emulate_plot(DRAW_SOLIDLINE+DRAW_RELATIVE, width, 0);
    emulate_plot(DRAW_SOLIDLINE+DRAW_RELATIVE, 0, height);
    emulate_plot(DRAW_SOLIDLINE+DRAW_RELATIVE, -width, 0);
    emulate_plot(DRAW_SOLIDLINE+DRAW_RELATIVE, 0, -height);
  }
}

/*
** 'emulate_moverect' is called to either copy an area of the graphics screen
** from one place to another or to move it, clearing its old location to the
** current background colour
*/
void emulate_moverect(int32 x1, int32 y1, int32 width, int32 height, int32 x2, int32 y2, boolean ismove) {
  emulate_plot(DRAW_SOLIDLINE+MOVE_ABSOLUTE, x1, y1);
  emulate_plot(DRAW_SOLIDLINE+MOVE_RELATIVE, width, height);
  if (ismove)	/* Move the area just marked */
    emulate_plot(MOVE_RECTANGLE, x2, y2);
  else {
    emulate_plot(COPY_RECTANGLE, x2, y2);
  }
}

/*
** 'emulate_fill' flood-fills an area of the graphics screen in
** the current foreground colour starting at position (x,y) on the
** screen
*/
void emulate_fill(int32 x, int32 y) {
  emulate_plot(FLOOD_BACKGROUND+DRAW_ABSOLUTE, x, y);
}

/*
** 'emulate_fillby' flood-fills an area of the graphics screen in
** the current foreground colour starting at the position at offsets
** 'x' and 'y' relative to the current graphics cursor position
*/
void emulate_fillby(int32 x, int32 y) {
  emulate_plot(FLOOD_BACKGROUND+DRAW_RELATIVE, x, y);
}

/*
** 'emulate_origin' emulates the Basic statement 'ORIGIN' which
** sets the absolute location of the origin on the graphics screen
*/
void emulate_origin(int32 x, int32 y) {
  emulate_vdu(VDU_ORIGIN);
  emulate_vdu(x & BYTEMASK);
  emulate_vdu((x>>BYTESHIFT) & BYTEMASK);
  emulate_vdu(y & BYTEMASK);
  emulate_vdu((y>>BYTESHIFT) & BYTEMASK);
}

/*
** 'init_screen' is called to initialise the RISC OS VDU driver
** emulation code for the versions of this program that do not run
** under RISC OS. It returns 'TRUE' if initialisation was okay or
** 'FALSE' if it failed (in which case it is not safe for the
** interpreter to run)
*/
boolean init_screen(void) {
  find_screensize();
  vdunext = 0;
  vduneeded = 0;
  enable_print = FALSE;
  graphmode = TEXTMODE;		/* Say mode is capable of graphics output */
  vscrwidth = SCREEN_WIDTH;	/* SCREEN_WIDTH and SCREEN_HEIGHT are constants */
  vscrheight = SCREEN_HEIGHT;	/* set in jlib.h */
  vscreen = modescreen = NIL;
  xgupp = ygupp = 1;
  screen_set_app_title("Brandy");
  if (basicvars.runflags.start_graphics) {
    setup_mode(31);		/* Mode 31 - 800 by 600 by 16 colours */
    cursorstate = ONSCREEN;
    switch_graphics();
  }
  else {
    setup_mode(46);	/* Mode 46 - 80 columns by 25 lines by 16 colours */
    find_cursor();
  }
  return TRUE;
}

/*
** 'end_screen' is called to tidy up the VDU emulation at the end
** of the run
*/
void end_screen(void) {
  if (graphmode==FULLSCREEN) switch_text();
}
