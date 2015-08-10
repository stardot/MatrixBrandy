/*
** This file is part of the Brandy Basic V Interpreter.
** Copyright (C) 2000, 2001, 2002, 2003, 2004 David Daniels
**
** SDL additions by Colin Tuckley
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
**	used when graphics output is possible. It uses the SDL graphics library.
**
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <SDL.h>
#include "common.h"
#include "target.h"
#include "errors.h"
#include "basicdefs.h"
#include "scrcommon.h"
#include "screen.h"

/*
** Notes
** -----
**  This is one of the five versions of the VDU driver emulation.
**  It is used by versions of the interpreter where graphics are
**  supported as well as text output using the SDL library.
**  The five versions of the VDU driver code are in:
**	riscos.c
**	textgraph.c
**  graphsdl.c
**	textonly.c
**	simpletext.c
**
**  Graphics support for operating systems other than RISC OS is
**  provided using the platform-independent library 'SDL'.
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
**  The graphics library that was originally used, jlib, limited
**  the range of RISC OS graphics facilities supported quite considerably.
**  The new graphics library SDL overcomes some of those restrictions
**  although some new geometric routines have had to be added. The graphics
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

/*
** SDL related defines, Variables and params
*/
#define SCREEN_WIDTH  800
#define SCREEN_HEIGHT 600

static SDL_Surface *screen0, *screen1, *sdl_fontbuf;
static SDL_Surface *modescreen;	/* Buffer used when screen mode is scaled to fit real screen */

static SDL_Rect font_rect, place_rect, scroll_rect, line_rect, scale_rect;

Uint32 tf_colour,       /* text foreground SDL rgb triple */
       tb_colour,       /* text background SDL rgb triple */
       gf_colour,       /* graphics foreground SDL rgb triple */
       gb_colour;       /* graphics background SDL rgb triple */

Uint32 xor_mask;

/*
** function definitions
*/

extern void draw_line(SDL_Surface *, int32, int32, int32, int32, int32, int32, Uint32);
extern void filled_triangle(SDL_Surface *, int32, int32, int32, int32, int32, int32, int32, int32, Uint32);
extern void draw_ellipse(SDL_Surface *, int32, int32, int32, int32, int32, int32, Uint32);
extern void filled_ellipse(SDL_Surface *, int32, int32, int32, int32, int32, int32, Uint32);
static void toggle_cursor(void);
static void switch_graphics(void);
static void vdu_cleartext(void);

static Uint8 palette[768];		/* palette for screen */

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
** pixel coordinates
*/
#define GXTOPX(x) ((x) / xgupp + xbufoffset)
#define GYTOPY(y) ((ygraphunits - 1 -(y)) / ygupp + ybufoffset)

static graphics graphmode;	/* Says whether graphics are possible or not */

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
/* € */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/*  */  {0x1C, 0x63, 0x6B, 0x6B, 0x7F, 0x77, 0x63, 0u},
/* ‚ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ƒ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* „ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* … */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* † */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ‡ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ˆ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ‰ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Š */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ‹ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Œ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/*  */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ž */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/*  */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
	/* 90 */
/*  */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ‘ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ’ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* “ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ” */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* • */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* – */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* — */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ˜ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ™ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* š */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* › */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* œ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/*  */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ž */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ÿ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
	/* a0 */
/*   */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ¡ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ¢ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* £ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ¤ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ¥ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ¦ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* § */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ¨ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* © */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ª */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* « */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ¬ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ­ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ® */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ¯ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
	/* b0 */
/* ° */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ± */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ² */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ³ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ´ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* µ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ¶ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* · */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ¸ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ¹ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* º */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* » */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ¼ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ½ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ¾ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ¿ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
	/* c0 */
/* À */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Á */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Â */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ã */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ä */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Å */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Æ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ç */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* È */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* É */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ê */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ë */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ì */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Í */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Î */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ï */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
	/* d0 */
/* Ð */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ñ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ò */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ó */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ô */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Õ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ö */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* × */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ø */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ù */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ú */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Û */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ü */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Ý */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* Þ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ß */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
	/* e0 */
/* à */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* á */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* â */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ã */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ä */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* å */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* æ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ç */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* è */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* é */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ê */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ë */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ì */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* í */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* î */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ï */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
	/* f0 */
/* ð */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ñ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ò */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ó */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ô */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* õ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ö */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ÷ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ø */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ù */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ú */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* û */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ü */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/*   */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* þ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
/* ÿ */  {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u}
};

#define XPPC 8		/* Size of character in pixels in X direction */
#define YPPC 8		/* Size of character in pixels in Y direction */

/*
** 'find_cursor' locates the cursor on the text screen and ensures that
** its position is valid, that is, lies within the text window
*/
void find_cursor(void) {
//  if (graphmode!=FULLSCREEN) {
//    xtext = wherex()-1;
//    ytext = wherey()-1;
//  }
}

void set_rgb(void) {
  int j;
  j = text_physforecol*3;
  tf_colour = SDL_MapRGB(sdl_fontbuf->format, palette[j], palette[j+1], palette[j+2]);
  j = text_physbackcol*3;
  tb_colour = SDL_MapRGB(sdl_fontbuf->format, palette[j], palette[j+1], palette[j+2]);

  j = graph_physforecol*3;
  gf_colour = SDL_MapRGB(sdl_fontbuf->format, palette[j], palette[j+1], palette[j+2]);
  j = graph_physbackcol*3;
  gb_colour = SDL_MapRGB(sdl_fontbuf->format, palette[j], palette[j+1], palette[j+2]);
}

void sdlchar(char ch) {
  int32 y, line;
  if (cursorstate == ONSCREEN) cursorstate = SUSPENDED;
  place_rect.x = xtext * XPPC;
  place_rect.y = ytext * YPPC;
  SDL_FillRect(sdl_fontbuf, NULL, tb_colour);
  for (y = 0; y < YPPC; y++) {
    line = sysfont[ch-' '][y];
    if (line != 0) {
      if (line & 0x80) *((Uint32*)sdl_fontbuf->pixels + 0 + y*XPPC) = tf_colour;
      if (line & 0x40) *((Uint32*)sdl_fontbuf->pixels + 1 + y*XPPC) = tf_colour;
      if (line & 0x20) *((Uint32*)sdl_fontbuf->pixels + 2 + y*XPPC) = tf_colour;
      if (line & 0x10) *((Uint32*)sdl_fontbuf->pixels + 3 + y*XPPC) = tf_colour;
      if (line & 0x08) *((Uint32*)sdl_fontbuf->pixels + 4 + y*XPPC) = tf_colour;
      if (line & 0x04) *((Uint32*)sdl_fontbuf->pixels + 5 + y*XPPC) = tf_colour;
      if (line & 0x02) *((Uint32*)sdl_fontbuf->pixels + 6 + y*XPPC) = tf_colour;
      if (line & 0x01) *((Uint32*)sdl_fontbuf->pixels + 7 + y*XPPC) = tf_colour;
    }
  }
  SDL_BlitSurface(sdl_fontbuf, &font_rect, screen0, &place_rect);
  if (echo) SDL_UpdateRect(screen0, xtext * XPPC, ytext * YPPC, XPPC, YPPC);
}

/*
** 'scroll_text' is called to move the text window up or down a line.
** Note that the coordinates here are in RISC OS text coordinates which
** start at (0, 0) whereas conio's start with (1, 1) at the top left-hand
** corner of the screen.
*/
static void scroll_text(updown direction) {
  int n, xx, yy;
  if (!textwin && direction == SCROLL_UP) {	/* Text window is the whole screen and scrolling upwards */
    scroll_rect.x = 0;
    scroll_rect.y = YPPC;
    scroll_rect.w = vscrwidth;
    scroll_rect.h = YPPC * textheight-1;
    SDL_BlitSurface(screen0, &scroll_rect, screen1, NULL);
    line_rect.x = 0;
    line_rect.y = YPPC * textheight-1;
    line_rect.w = vscrwidth;
    line_rect.h = YPPC;
    SDL_FillRect(screen1, &line_rect, tb_colour);
    SDL_BlitSurface(screen1, NULL, screen0, NULL);
    SDL_Flip(screen0);
  }
  else {
    xx = xtext; yy = ytext;
    scroll_rect.x = XPPC * twinleft;
    scroll_rect.w = XPPC * (twinright - twinleft +1);
    scroll_rect.h = YPPC * (twinbottom - twintop);
    line_rect.x = 0;
    if (twintop != twinbottom) {	/* Text window is more than one line high */
      if (direction == SCROLL_UP) {	/* Scroll text up a line */
        scroll_rect.y = YPPC * (twintop + 1);
        line_rect.y = 0;
      }
      else {	/* Scroll text down a line */
        scroll_rect.y = YPPC * twintop;
        line_rect.y = YPPC;
      }
      SDL_BlitSurface(screen0, &scroll_rect, screen1, &line_rect);
      scroll_rect.x = 0;
      scroll_rect.y = 0;
      scroll_rect.w = XPPC * (twinright - twinleft +1);
      scroll_rect.h = YPPC * (twinbottom - twintop +1);
      line_rect.x = twinleft * XPPC;
      line_rect.y = YPPC * twintop;
      SDL_BlitSurface(screen1, &scroll_rect, screen0, &line_rect);
    }
    xtext = twinleft;
    echo_off();
    for (n=twinleft; n<=twinright; n++) sdlchar(' ');	/* Clear the vacated line of the window */
    xtext = xx; ytext = yy;	/* Put the cursor back where it should be */
    echo_on();
  }
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
  set_rgb();
}

/*
** 'vdu_23command' emulates some of the VDU 23 command sequences
*/
static void vdu_23command(void) {
  int codeval, n;
  switch (vduqueue[0]) {	/* First byte in VDU queue gives the command type */
  case 1:	/* Control the appear of the text cursor */
    if (graphmode == FULLSCREEN) {
      if (vduqueue[1] == 0) {
        if (cursorstate == ONSCREEN) toggle_cursor();
        cursorstate = HIDDEN;	/* 0 = hide, 1 = show */
      }
      if (vduqueue[1] == 1 && cursorstate != NOCURSOR) cursorstate = ONSCREEN;
    }
    if (vduqueue[1] == 1) cursorstate = ONSCREEN;
    else cursorstate = HIDDEN;
    break;
  case 8:	/* Clear part of the text window */
    break;
  case 17:	/* Set the tint value for a colour in 256 colour modes, etc */
    vdu_2317();
  default:
    codeval = vduqueue[0] & 0x00FF;
    if (codeval < 32 ) break;   /* Ignore unhandled commands */
    /* codes 32 to 255 are user-defined character setup commands */
    for (n=0; n < 8; n++) sysfont[codeval-32][n] = vduqueue[n+1];
  }
}

/*
** 'toggle_cursor' draws the text cursor at the current text position
** in graphics modes.
** It draws (and removes) the cursor by inverting the colours of the
** pixels at the current text cursor position. Two different styles
** of cursor can be drawn, an underline and a block
*/
static void toggle_cursor(void) {
  int32 left, right, top, bottom, x, y;
  if ((cursorstate != SUSPENDED) && (cursorstate != ONSCREEN)) return;	/* Cursor is not being displayed so give up now */
  if (cursorstate == ONSCREEN)	/* Toggle the cursor state */
    cursorstate = SUSPENDED;
  else
    cursorstate = ONSCREEN;
  left = xoffset + xtext*xscale*XPPC;	/* Calculate pixel coordinates of ends of cursor */
  right = left + xscale*XPPC -1;
  if (cursmode == UNDERLINE) {
    y = (yoffset + (ytext+1)*yscale*YPPC - yscale) * vscrwidth;
    for (x=left; x <= right; x++) {
      *((Uint32*)screen0->pixels + x + y) ^= xor_mask;
      if (yscale != 1) *((Uint32*)screen0->pixels + x + y + vscrwidth) ^= xor_mask;
    }
  }
  else if (cursmode == BLOCK) {
    top = yoffset + ytext*yscale*YPPC;
    bottom = top + YPPC*yscale -1;
    for (y = top; y <= bottom; y++) {
      for (x = left; x <= right; x++)
        *((Uint32*)screen0->pixels + x + y*vscrwidth) ^= xor_mask;
    }
  }
  if (echo) SDL_UpdateRect(screen0, xoffset + xtext*xscale*XPPC, yoffset + ytext*yscale*YPPC, xscale*XPPC, yscale*YPPC);
}

static void toggle_tcursor(void) {
  int32 x, y, top, bottom, left, right;
  if (cursorstate == ONSCREEN)	/* Toggle the cursor state */
    cursorstate = SUSPENDED;
  else
    cursorstate = ONSCREEN;
  left = xtext*XPPC;
  right = left + XPPC -1;
  if (cursmode == UNDERLINE) {
    y = ((ytext+1)*YPPC -1) * vscrwidth;
    for (x=left; x <= right; x++)
      *((Uint32*)screen0->pixels + x + y) ^= xor_mask;
  }
  else if (cursmode == BLOCK) {
    top = ytext*YPPC;
    bottom = top + YPPC -1;
    for (y = top; y <= bottom; y++) {
      for (x = left; x <= right; x++)
        *((Uint32*)screen0->pixels + x + y*vscrwidth) ^= xor_mask;
    }
  }
  if (echo) SDL_UpdateRect(screen0, xtext * XPPC, ytext * YPPC, XPPC, YPPC);
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
** enlarged form to the main screen buffer.
** (x1, y1) and (x2, y2) define the rectangle to be displayed. These
** are given in terms of what could be called the pseudo pixel
** coordinates of the buffer. These pseudo pixel coordinates are
** converted to real pixel coordinates by multiplying them by 'xscale'
** and 'yscale'. 'xoffset' and 'yoffset' are added to the coordinates
** to position the screen buffer in the middle of the screen.
*/
static void blit_scaled(int32 left, int32 top, int32 right, int32 bottom) {
  int32 dleft, dtop, xx, yy, i, j, ii, jj;
/*
** Start by clipping the rectangle to be blit'ed if it extends off the
** screen.
** Note that 'screenwidth' and 'screenheight' give the dimensions of the
** RISC OS screen mode in pixels
*/
  if (left >= screenwidth || right < 0 || top >= screenheight || bottom < 0) return;	/* Is off screen completely */
  if (left < 0) left = 0;		/* Clip the rectangle as necessary */
  if (right >= screenwidth) right = screenwidth-1;
  if (top < 0) top = 0;
  if (bottom >= screenheight) bottom = screenheight-1;
  dleft = left*xscale + xoffset;		    /* Calculate pixel coordinates in the */
  dtop  = top*yscale + yoffset;		        /* screen buffer of the rectangle */
  yy = dtop;
  for (j = top; j <= bottom; j++) {
    for (jj = 1; jj <= yscale; jj++) {
      xx = dleft;
      for (i = left; i <= right; i++) {
        for (ii = 1; ii <= xscale; ii++) {
          *((Uint32*)screen0->pixels + xx + yy*vscrwidth) = *((Uint32*)modescreen->pixels + i + j*vscrwidth);
          xx++;
        }
      }
      yy++;
    } 
  }
  scale_rect.x = dleft;
  scale_rect.y = dtop;
  scale_rect.w = (right+1 - left) * xscale;
  scale_rect.h = (bottom+1 - top) * yscale;
  SDL_UpdateRect(screen0, scale_rect.x, scale_rect.y, scale_rect.w, scale_rect.h);
}

#define COLOURSTEP 68		/* RGB colour value increment used in 256 colour modes */
#define TINTSTEP 17		/* RGB colour value increment used for tints */

/*
** 'init_palette' is called to initialise the palette used for the
** screen. This is just a 768 byte block of memory with
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
  case 4:	/* Four colour - Black, red, yellow and white */
    palette[0] = palette[1] = palette[2] = 0;		/* Black */
    palette[3] = 255; palette[4] = palette[5] = 0;	/* Red */
    palette[6] = palette[7] = 255; palette[8] = 0;	/* Yellow */
    palette[9] = palette[10] = palette[11] = 255;	/* White */
    break;
  case 16:	/* Sixteen colour */
    palette[0] = palette[1] = palette[2] = 0;		    /* Black */
    palette[3] = 255; palette[4] = palette[5] = 0;	    /* Red */
    palette[6] = 0; palette[7] = 255; palette[8] = 0;	/* Green */
    palette[9] = palette[10] = 255; palette[11] = 0;	/* Yellow */
    palette[12] = palette[13] = 0; palette[14] = 255;	/* Blue */
    palette[15] = 255; palette[16] = 0; palette[17] = 255;	/* Magenta */
    palette[18] = 0; palette[19] = palette[20] = 255;	/* Cyan */
    palette[21] = palette[22] = palette[23] = 255;	    /* White */
    palette[24] = palette[25] = palette[26] = 0;	    /* Black */
    palette[27] = 160; palette[28] = palette[29] = 0;	/* Dark red */
    palette[30] = 0; palette[31] = 160; palette[32] = 0;/* Dark green */
    palette[33] = palette[34] = 160; palette[35] = 0;	/* Khaki */
    palette[36] = palette[37] = 0; palette[38] = 160;	/* Navy blue */
    palette[39] = 160; palette[40] = 0; palette[41] = 160;	/* Purple */
    palette[42] = 0; palette[43] = palette[44] = 160;	/* Cyan */
    palette[45] = palette[46] = palette[47] = 160;	    /* Grey */
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
    for (blue = 0; blue <= COLOURSTEP * 3; blue += COLOURSTEP) {
      for (green = 0; green <= COLOURSTEP * 3; green += COLOURSTEP) {
        for (red = 0; red <= COLOURSTEP * 3; red += COLOURSTEP) {
          for (tint = 0; tint <= TINTSTEP * 3; tint += TINTSTEP) {
            palette[colour] = red+tint;
            palette[colour+1] = green+tint;
            palette[colour+2] = blue+tint;
            colour += 3;
          }
        }
      }
    }
    break;
  }
  default:	/* 32K and 16M colour modes are not supported */
    error(ERR_UNSUPPORTED);
  }
  if (colourdepth == 256) {
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
  set_rgb();
}

/*
** 'change_palette' is called to change the palette entry for physical
** colour 'colour' to the colour defined by the RGB values red, green
** and blue. The screen is updated by this call
*/
static void change_palette(int32 colour, int32 red, int32 green, int32 blue) {
  if (graphmode != FULLSCREEN) return;	/* There be no palette to change */
  palette[colour*3] = red;	/* The palette is not structured */
  palette[colour*3+1] = green;
  palette[colour*3+2] = blue;
}

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
  else if (graphmode == TEXTMODE) {
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
    text_physbackcol = text_backcol = (colnum & (colourdepth - 1));
  else {
    text_physforecol = text_forecol = (colnum & (colourdepth - 1));
  }
  set_rgb();
}

/*
 * set_graphics_colour - Set either the graphics foreground
 * colour or the background colour to the supplied colour
 * number (palette entry number). This is used when a colour
 * has been matched with an entry in the palette via COLOUR()
 */
static void set_graphics_colour(boolean background, int colnum) {
  if (background)
    graph_physbackcol = graph_backcol = (colnum & (colourdepth - 1));
  else {
    graph_physforecol = graph_forecol = (colnum & (colourdepth - 1));
  }
  set_rgb();
}

/*
** 'switch_graphics' switches from text output mode to fullscreen graphics
** mode. Unless the option '-graphics' is specified on the command line,
** the interpreter remains in fullscreen mode until the next mode change
** when it switches back to text output mode. If '-graphics' is given
** then it remains in fullscreen mode
*/
static void switch_graphics(void) {
  SDL_SetClipRect(screen0, NULL);
  SDL_SetClipRect(modescreen, NULL);
  SDL_FillRect(screen0, NULL, tb_colour);
  SDL_FillRect(screen1, NULL, tb_colour);
  SDL_FillRect(modescreen, NULL, tb_colour);
  init_palette();
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
  if (xoffset != 0) {	/* Only part of the screen is used */
    line_rect.x = xoffset-1;
    line_rect.y = yoffset-1;
    line_rect.w = vscrwidth;
    line_rect.h = vscrheight;
    SDL_SetClipRect(screen0, &line_rect);
  }
  vdu_cleartext();	/* Clear the graphics screen */
  if (cursorstate == NOCURSOR) {	/* 'cursorstate' might be set to 'HIDDEN' if OFF used */
    cursorstate = SUSPENDED;
    toggle_cursor();
  }
}

/*
** 'switch_text' switches from fullscreen graphics back to text output mode.
** It does this on a mode change
*/
static void switch_text(void) {
  SDL_SetClipRect(screen0, NULL);
  SDL_SetClipRect(modescreen, NULL);
  SDL_FillRect(screen0, NULL, tb_colour);
  SDL_FillRect(screen1, NULL, tb_colour);
  SDL_FillRect(modescreen, NULL, tb_colour);
}

/*
** 'scroll' scrolls the graphics screen up or down by the number of
** rows equivalent to one line of text on the screen. Depending on
** the RISC OS mode being used, this can be either eight or sixteen
** rows. 'direction' says whether the screen is moved up or down.
** The screen is redrawn by this call
*/
static void scroll(updown direction) {
  int left, right, top, dest, topwin;
/*int bottom; */
  topwin = ybufoffset+twintop*YPPC;		/* Y coordinate of top of text window */
  if (direction == SCROLL_UP) {	/* Shifting screen up */
    dest = ybufoffset + twintop*YPPC;		/* Move screen up to this point */
    left = xbufoffset + twinleft*XPPC;
    right = xbufoffset + twinright*XPPC+XPPC-1;
    top = dest+YPPC;				/* Top of block to move starts here */
/*  bottom = ybufoffset+twinbottom*YPPC+YPPC-1;	   End of block is here */
    scroll_rect.x = xbufoffset + twinleft*XPPC;
    scroll_rect.y = ybufoffset + YPPC * (twintop + 1);
    scroll_rect.w = XPPC * (twinright - twinleft +1);
    scroll_rect.h = YPPC * (twinbottom - twintop);
    SDL_BlitSurface(modescreen, &scroll_rect, screen1, NULL);
    line_rect.x = 0;
    line_rect.y = YPPC * (twinbottom - twintop);
    line_rect.w = XPPC * (twinright - twinleft +1);
    line_rect.h = YPPC;
    SDL_FillRect(screen1, &line_rect, tb_colour);
  }
  else {	/* Shifting screen down */
    dest = ybufoffset+(twintop+1)*YPPC;
    left = xbufoffset+twinleft*XPPC;
    right = xbufoffset+(twinright+1)*XPPC-1;
    top = ybufoffset+twintop*YPPC;
/*  bottom = ybufoffset+twinbottom*YPPC-1; */
    scroll_rect.x = left;
    scroll_rect.y = top;
    scroll_rect.w = XPPC * (twinright - twinleft +1);
    scroll_rect.h = YPPC * (twinbottom - twintop);
    line_rect.x = 0;
    line_rect.y = YPPC;
    SDL_BlitSurface(modescreen, &scroll_rect, screen1, &line_rect);
    line_rect.x = 0;
    line_rect.y = 0;
    line_rect.w = XPPC * (twinright - twinleft +1);
    line_rect.h = YPPC;
    SDL_FillRect(screen1, &line_rect, tb_colour);
  }
  line_rect.x = 0;
  line_rect.y = 0;
  line_rect.w = XPPC * (twinright - twinleft +1);
  line_rect.h = YPPC * (twinbottom - twintop +1);
  scroll_rect.x = left;
  scroll_rect.y = dest;
  SDL_BlitSurface(screen1, &line_rect, modescreen, &scroll_rect);
  if (scaled)
    blit_scaled(left, topwin, right, twinbottom*YPPC+YPPC-1);
  else { 	/* Scrolling the entire screen */
    SDL_BlitSurface(screen1, &line_rect, screen0, &scroll_rect);
    SDL_Flip(screen0);
  }
}

/*
** 'echo_ttext' is called to display text held in the screen buffer on the
** text screen when working in 'no echo' mode. If does the buffer flip.
*/
static void echo_ttext(void) {
  if (xtext != 0) SDL_UpdateRect(screen0, 0, ytext*YPPC, xtext*XPPC, YPPC);
}

/*
** 'echo_text' is called to display text held in the screen buffer on the
** graphics screen when working in 'no echo' mode. If displays from the
** start of the line to the current value of the text cursor
*/
static void echo_text(void) {
  if (xtext == 0) return;	/* Return if nothing has changed */
  if (scaled)
    blit_scaled(0, ytext*YPPC, xtext*XPPC-1, ytext*YPPC+YPPC-1);
  else {
    line_rect.x = xoffset;
    line_rect.y = yoffset+ytext*YPPC;
    line_rect.w = xtext*XPPC;
    line_rect.h = YPPC;
    scroll_rect.x = xoffset;
    scroll_rect.y = yoffset+ytext*YPPC;
    SDL_BlitSurface(modescreen, &line_rect, screen0, &scroll_rect);
    SDL_UpdateRect(screen0, xoffset, yoffset+ytext*YPPC, xtext*XPPC, YPPC);
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
  if (cursorstate == ONSCREEN) cursorstate = SUSPENDED;
  topx = xbufoffset +xtext*XPPC;
  topy = ybufoffset +ytext*YPPC;
  place_rect.x = topx;
  place_rect.y = topy;
  SDL_FillRect(sdl_fontbuf, NULL, tb_colour);
  for (y=0; y < YPPC; y++) {
    line = sysfont[ch-' '][y];
    if (line!=0) {
      if (line & 0x80) *((Uint32*)sdl_fontbuf->pixels + 0 + y*XPPC) = tf_colour;
      if (line & 0x40) *((Uint32*)sdl_fontbuf->pixels + 1 + y*XPPC) = tf_colour;
      if (line & 0x20) *((Uint32*)sdl_fontbuf->pixels + 2 + y*XPPC) = tf_colour;
      if (line & 0x10) *((Uint32*)sdl_fontbuf->pixels + 3 + y*XPPC) = tf_colour;
      if (line & 0x08) *((Uint32*)sdl_fontbuf->pixels + 4 + y*XPPC) = tf_colour;
      if (line & 0x04) *((Uint32*)sdl_fontbuf->pixels + 5 + y*XPPC) = tf_colour;
      if (line & 0x02) *((Uint32*)sdl_fontbuf->pixels + 6 + y*XPPC) = tf_colour;
      if (line & 0x01) *((Uint32*)sdl_fontbuf->pixels + 7 + y*XPPC) = tf_colour;
    }
  }
  SDL_BlitSurface(sdl_fontbuf, &font_rect, modescreen, &place_rect);
  if (echo) {
    if (!scaled) {
      SDL_BlitSurface(sdl_fontbuf, &font_rect, screen0, &place_rect);
      SDL_UpdateRect(screen0, place_rect.x, place_rect.y, XPPC, YPPC);
    }
    else blit_scaled(topx, topy, topx+XPPC-1, topy+YPPC-1);
  }
  xtext++;
  if (xtext > twinright) {
    if (!echo) echo_text();	/* Line is full so flush buffered characters */
    xtext = twinleft;
    ytext++;
    if (ytext > twinbottom) {	/* Text cursor was on the last line of the text window */
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
  int32 y, topx, topy, line;
  topx = GXTOPX(xlast);		/* X and Y coordinates are those of the */
  topy = GYTOPY(ylast);	/* top left-hand corner of the character */
  place_rect.x = topx;
  place_rect.y = topy;
  SDL_FillRect(sdl_fontbuf, NULL, gb_colour);
  for (y=0; y<YPPC; y++) {
    line = sysfont[ch-' '][y];
    if (line!=0) {
      if (line & 0x80) *((Uint32*)sdl_fontbuf->pixels + 0 + y*XPPC) = gf_colour;
      if (line & 0x40) *((Uint32*)sdl_fontbuf->pixels + 1 + y*XPPC) = gf_colour;
      if (line & 0x20) *((Uint32*)sdl_fontbuf->pixels + 2 + y*XPPC) = gf_colour;
      if (line & 0x10) *((Uint32*)sdl_fontbuf->pixels + 3 + y*XPPC) = gf_colour;
      if (line & 0x08) *((Uint32*)sdl_fontbuf->pixels + 4 + y*XPPC) = gf_colour;
      if (line & 0x04) *((Uint32*)sdl_fontbuf->pixels + 5 + y*XPPC) = gf_colour;
      if (line & 0x02) *((Uint32*)sdl_fontbuf->pixels + 6 + y*XPPC) = gf_colour;
      if (line & 0x01) *((Uint32*)sdl_fontbuf->pixels + 7 + y*XPPC) = gf_colour;
    }
  }
  SDL_BlitSurface(sdl_fontbuf, &font_rect, modescreen, &place_rect);
  if (!scaled) {
    SDL_BlitSurface(sdl_fontbuf, &font_rect, screen0, &place_rect);
    SDL_UpdateRect(screen0, place_rect.x, place_rect.y, XPPC, YPPC);
  }
  else blit_scaled(topx, topy, topx+XPPC-1, topy+YPPC-1);

  cursorstate = SUSPENDED; /* because we just overwrote it */
  xlast += XPPC*xgupp;	/* Move to next character position in X direction */
  if (xlast > gwinright) {	/* But position is outside the graphics window */
    xlast = gwinleft;
    ylast -= YPPC*ygupp;
    if (ylast < gwinbottom) ylast = gwintop;	/* Below bottom of graphics window - Wrap around to top */
  }
}

/*
** 'echo_on' turns on cursor (if in graphics mode) and the immediate
** echo of characters to the screen
*/
void echo_on(void) {
  echo = TRUE;
  if (graphmode == FULLSCREEN) {
    echo_text();			/* Flush what is in the graphics buffer */
    if (cursorstate == SUSPENDED) toggle_cursor();	/* Display cursor again */
  }
  else {
    echo_ttext();			/* Flush what is in the text buffer */
  }
}

/*
** 'echo_off' turns off the cursor (if in graphics mode) and the
** immediate echo of characters to the screen. This is used to
** make character output more efficient
*/
void echo_off(void) {
  echo = FALSE;
  if (graphmode == FULLSCREEN) {
    if (cursorstate == ONSCREEN) toggle_cursor();	/* Remove the cursor if it is being displayed */
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
  if (graphmode == FULLSCREEN) {
    if (cursorstate == ONSCREEN) toggle_cursor();	/* Remove cursor if in graphics mode */
    xtext = column;
    ytext = row;
    if (cursorstate == SUSPENDED) toggle_cursor();	/* Redraw cursor if in graphics mode */
  }
  else {
    toggle_tcursor();
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
  if (graphmode == FULLSCREEN) {
    if (cursorstate == ONSCREEN) toggle_cursor();	/* Remove old style cursor */
    cursmode = underline ? UNDERLINE : BLOCK;
    if (cursorstate == SUSPENDED) toggle_cursor();	/* Draw new style cursor */
  }
  else {
    if (cursorstate == ONSCREEN) toggle_tcursor();	/* Remove old style cursor */
    cursmode = underline ? UNDERLINE : BLOCK;
    if (cursorstate == SUSPENDED) toggle_tcursor();	/* Draw new style cursor */
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
  if (mode < 16 && colourdepth <= 16)	/* Just change the RISC OS logical to physical colour mapping */
    logtophys[logcol] = mode;
  else if (mode == 16)	/* Change the palette entry for colour 'logcol' */
    change_palette(logcol, vduqueue[2], vduqueue[3], vduqueue[4]);
  else {
    if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
  }
}

/*
** 'move_down' moves the text cursor down a line within the text
** window, scrolling the window up if the cursor is on the last line
** of the window. This only works in full screen graphics mode.
*/
static void move_down(void) {
  ytext++;
  if (ytext > twinbottom) {	/* Cursor was on last line in window - Scroll window up */
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
  if (ytext < twintop) {	/* Cursor was on top line in window - Scroll window down */
    ytext++;
    scroll(SCROLL_DOWN);
  }
}

/*
** 'move_curback' moves the cursor back one character on the screen (VDU 8)
*/
static void move_curback(void) {
  if (vdu5mode) {	/* VDU 5 mode - Move graphics cursor back one character */
    xlast -= XPPC*xgupp;
    if (xlast < gwinleft) {		/* Cursor is outside the graphics window */
      xlast = gwinright-XPPC*xgupp+1;	/* Move back to right edge of previous line */
      ylast += YPPC*ygupp;
      if (ylast > gwintop) {		/* Move above top of window */
        ylast = gwinbottom+YPPC*ygupp-1;	/* Wrap around to bottom of window */
      }
    }
  }
  else if (graphmode == FULLSCREEN) {
    if (cursorstate == ONSCREEN) toggle_cursor();	/* Remove cursor */
    xtext--;
    if (xtext < twinleft) {	/* Cursor is at left-hand edge of text window so move up a line */
      xtext = twinright;
      move_up();
    }
    if (cursorstate == SUSPENDED) toggle_cursor();	/* Redraw cursor */
  }
  else {	/* Writing to the text screen */
    toggle_tcursor();
    xtext--;
    if (xtext < twinleft) {	/* Cursor is outside window */
      xtext = twinright;
      ytext--;
      if (ytext < twintop) {	/* Cursor is outside window */
        ytext++;
        scroll_text(SCROLL_DOWN);
      }
    }
    toggle_tcursor();
  }
}

/*
** 'move_curforward' moves the cursor forwards one character on the screen (VDU 9)
*/
static void move_curforward(void) {
  if (vdu5mode) {	/* VDU 5 mode - Move graphics cursor back one character */
    xlast += XPPC*xgupp;
    if (xlast > gwinright) {	/* Cursor is outside the graphics window */
      xlast = gwinleft;		/* Move to left side of window on next line */
      ylast -= YPPC*ygupp;
      if (ylast < gwinbottom) ylast = gwintop;	/* Moved below bottom of window - Wrap around to top */
    }
  }
  else if (graphmode == FULLSCREEN) {
    if (cursorstate == ONSCREEN) toggle_cursor();	/* Remove cursor */
    xtext++;
    if (xtext > twinright) {	/* Cursor is at right-hand edge of text window so move down a line */
      xtext = twinleft;
      move_down();
    }
    if (cursorstate == SUSPENDED) toggle_cursor();	/* Redraw cursor */
  }
  else {	/* Writing to text screen */
    xtext++;
    if (xtext > twinright) {	/* Cursor has moved outside window - Move to next line */
      ytext++;
      if (ytext > twinbottom) {	/* Cursor is outside the window - Move the text window up one line */
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
    ylast -= YPPC*ygupp;
    if (ylast < gwinbottom) ylast = gwintop;	/* Moved below bottom of window - Wrap around to top */
  }
  else if (graphmode == FULLSCREEN) {
    if (cursorstate == ONSCREEN) toggle_cursor();	/* Remove cursor */
    move_down();
    if (cursorstate == SUSPENDED) toggle_cursor();	/* Redraw cursor */
  }
  else {		/* Writing to a text window */
    ytext++;
    if (ytext > twinbottom)	{ /* Cursor is outside the confines of the window */
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
    ylast += YPPC*ygupp;
    if (ylast > gwintop) ylast = gwinbottom+YPPC*ygupp-1;	/* Move above top of window - Wrap around to bottow */
  }
  else if (graphmode == FULLSCREEN) {
    if (cursorstate == ONSCREEN) toggle_cursor();	/* Remove cursor */
    move_up();
    if (cursorstate == SUSPENDED) toggle_cursor();	/* Redraw cursor */
  }
  else {	/* Writing to text screen */
    ytext--;
    if (ytext < twintop) {		/* Cursor lies above the text window */
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
  if (graphmode == FULLSCREEN) {
    if (cursorstate == ONSCREEN) toggle_cursor();	/* Remove cursor if it is being displayed */
    if (scaled) {	/* Using a screen mode that has to be scaled when displayed */
      left = twinleft*XPPC;
      right = twinright*XPPC+XPPC-1;
      top = twintop*YPPC;
      bottom = twinbottom*YPPC+YPPC-1;
      SDL_FillRect(modescreen, NULL, tb_colour);
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
        line_rect.x = left;
        line_rect.y = top;
        line_rect.w = right - left +1;
        line_rect.h = bottom - top +1;
        SDL_FillRect(modescreen, &line_rect, tb_colour);
        SDL_FillRect(screen0, &line_rect, tb_colour);
      }
      else {	/* Text window is not being used */
        SDL_FillRect(modescreen, NULL, tb_colour);
        SDL_FillRect(screen0, NULL, tb_colour);
      }
      xtext = twinleft;
      ytext = twintop;
      if (cursorstate==SUSPENDED) toggle_cursor();	/* Redraw cursor */
    }
  }
  else if (textwin) {	/* Text window defined that does not occupy the whole screen */
    int32 column, row;
    echo_off();
    for (row = twintop; row <= twinbottom; row++) {
      xtext = twinleft;  /* Go to start of line on screen */
      ytext = row;
      for (column = twinleft; column <= twinright;  column++) sdlchar(' ');
    }
    echo_on();
    xtext = twinleft;
    ytext = twintop;
  }
  else {
    SDL_FillRect(screen0, NULL, tb_colour);
    xtext = twinleft;
    ytext = twintop;
  }
  SDL_Flip(screen0);
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
  if (graphmode == TEXTONLY) return;	/* Ignore command in text-only modes */
  if (graphmode == TEXTMODE) switch_graphics();
  if (cursorstate == ONSCREEN) toggle_cursor();	/* Remove cursor */
  SDL_FillRect(modescreen, NULL, gb_colour);
  if (!scaled)
    SDL_FillRect(screen0, NULL, gb_colour);
  else {
    blit_scaled(GXTOPX(gwinleft), GYTOPY(gwintop), GXTOPX(gwinright), GYTOPY(gwinbottom));
  }
  if (cursorstate == SUSPENDED) toggle_cursor();	/* Redraw cursor */
  SDL_Flip(screen0);
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
  if (colnumber < 128) {	/* Setting foreground colour */
    if (graphmode == FULLSCREEN) {	/* Operating in full screen graphics mode */
      if (colourdepth == 256) {
        text_forecol = colnumber & COL256MASK;
        text_physforecol = (text_forecol << COL256SHIFT)+text_foretint;
      }
      else {
        text_physforecol = text_forecol = colnumber & colourmask;
      }
    }
    else {	/* Operating in text mode */
      text_physforecol = text_forecol = colnumber & colourmask;
    }
  }
  else {	/* Setting background colour */
    if (graphmode == FULLSCREEN) {	/* Operating in full screen graphics mode */
      if (colourdepth == 256) {
        text_backcol = colnumber & COL256MASK;
        text_physbackcol = (text_backcol << COL256SHIFT)+text_backtint;
      }
      else {	/* Operating in text mode */
        text_physbackcol = text_backcol = colnumber & colourmask;
      }
    }
    else {
      text_physbackcol = text_backcol = (colnumber-128) & colourmask;
    }
  }
  set_rgb();
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
  init_palette();
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
  if (graphmode == NOGRAPHICS) error(ERR_NOGRAPHICS);
  if (vduqueue[0] != OVERWRITE_POINT) error(ERR_UNSUPPORTED);	/* Only graphics plot action 0 is supported */
  colnumber = vduqueue[1];
  if (colnumber < 128) {	/* Setting foreground graphics colour */
      graph_fore_action = vduqueue[0];
      if (colourdepth == 256) {
        graph_forecol = colnumber & COL256MASK;
        graph_physforecol = (graph_forecol<<COL256SHIFT)+graph_foretint;
      }
      else {
        graph_physforecol = graph_forecol = colnumber & colourmask;
      }
  }
  else {	/* Setting background graphics colour */
    graph_back_action = vduqueue[0];
    if (colourdepth == 256) {
      graph_backcol = colnumber & COL256MASK;
      graph_physbackcol = (graph_backcol<<COL256SHIFT)+graph_backtint;
    }
    else {	/* Operating in text mode */
      graph_physbackcol = graph_backcol = colnumber & colourmask;
    }
  }
  set_rgb();
}

/*
** 'vdu_graphwind' defines a graphics clipping region (VDU 24)
*/
static void vdu_graphwind(void) {
  int32 left, right, top, bottom;
  if (graphmode != FULLSCREEN) return;
  left = vduqueue[0]+vduqueue[1]*256;		/* Left-hand coordinate */
  if (left > 0x7FFF) left = -(0x10000-left);	/* Coordinate is negative */
  bottom = vduqueue[2]+vduqueue[3]*256;		/* Bottom coordinate */
  if (bottom > 0x7FFF) bottom = -(0x10000-bottom);
  right = vduqueue[4]+vduqueue[5]*256;		/* Right-hand coordinate */
  if (right > 0x7FFF) right = -(0x10000-right);
  top = vduqueue[6]+vduqueue[7]*256;		/* Top coordinate */
  if (top > 0x7FFF) top = -(0x10000-top);
  left += xorigin;
  right += xorigin;
  top += yorigin;
  bottom += yorigin;
  if (left > right) {	/* Ensure left < right */
    int32 temp = left;
    left = right;
    right = temp;
  }
  if (bottom > top) {	/* Ensure bottom < top */
    int32 temp = bottom;
    bottom = top;
    top = temp;
  }
/* Ensure clipping region is entirely within the screen area */
  if (right < 0 || top < 0 || left >= xgraphunits || bottom >= ygraphunits) return;
  gwinleft = left;
  gwinright = right;
  gwintop = top;
  gwinbottom = bottom;
  line_rect.x = GXTOPX(left);
  line_rect.y = GYTOPY(top);
  line_rect.w = right - left +1;
  line_rect.h = bottom - top +1;
  SDL_SetClipRect(modescreen, &line_rect);
  clipping = TRUE;
}

/*
** 'vdu_plot' handles the VDU 25 graphics sequences
*/
static void vdu_plot(void) {
  int32 x, y;
  x = vduqueue[1]+vduqueue[2]*256;
  if (x > 0x7FFF) x = -(0x10000-x);	/* X is negative */
  y = vduqueue[3]+vduqueue[3]*256;
  if (y > 0x7FFF) y = -(0x10000-y);	/* Y is negative */
  emulate_plot(vduqueue[0], x, y);	/* vduqueue[0] gives the plot code */
}

/*
** 'vdu_restwind' restores the default (full screen) text and
** graphics windows (VDU 26)
*/
static void vdu_restwind(void) {
  if (clipping) {	/* Restore graphics clipping region to entire screen area for mode */
    if (scaled || xoffset == 0)
      SDL_SetClipRect(modescreen, NULL);
    else {
      line_rect.x = xoffset-1;
      line_rect.y = yoffset-1;
      line_rect.w = vscrwidth;
      line_rect.h = vscrheight;
      SDL_SetClipRect(screen0, &line_rect);
    }
    clipping = FALSE;
  }
  xorigin = yorigin = 0;
  xlast = ylast = xlast2 = ylast2 = 0;
  gwinleft = 0;
  gwinright = xgraphunits-1;
  gwintop = ygraphunits-1;
  gwinbottom = 0;
  if (graphmode == FULLSCREEN) {
    if (cursorstate == ONSCREEN) toggle_cursor();	/* Remove cursor if in graphics mode */
    xtext = ytext = 0;
    if (cursorstate == SUSPENDED) toggle_cursor();	/* Redraw cursor if in graphics mode */
  }
  else {
    xtext = ytext = 0;
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
  if (left > right) {	/* Ensure right column number > left */
    int32 temp = left;
    left = right;
    right = temp;
  }
  if (bottom < top) {	/* Ensure bottom line number > top */
    int32 temp = bottom;
    bottom = top;
    top = temp;
  }
  if (left >= textwidth || top >= textheight) return;	/* Ignore bad parameters */
  twinleft = left;
  twinright = right;
  twintop = top;
  twinbottom = bottom;
/* Set flag to say if text window occupies only a part of the screen */
  textwin = left > 0 || right < textwidth-1 || top > 0 || bottom < textheight-1;
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
    column = vduqueue[0] + twinleft;
    row = vduqueue[1] + twintop;
    if (column > twinright || row > twinbottom) return;	/* Ignore command if values are out of range */
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
  if (vduneeded == 0) {			/* VDU queue is empty */
    if (charvalue >= ' ') {		/* Most common case - print something */
      if (vdu5mode)			    /* Sending text output to graphics cursor */
        plot_char(charvalue);
      else if (graphmode == FULLSCREEN) {
        write_char(charvalue);
        if (cursorstate == SUSPENDED) toggle_cursor();	/* Redraw the cursor */
      }
      else {	/* Send character to text screen */
        sdlchar(charvalue);
        xtext++;
        if (xtext > twinright) {		/* Have reached edge of text window. Skip to next line  */
          xtext = twinleft;
          ytext++;
          if (ytext > twinbottom) {
            ytext--;
            if (textwin)
              scroll_text(SCROLL_UP);
          }
        }
        toggle_tcursor();
      }
      return;
    }
    else {	/* Control character - Found start of new VDU command */
      if (graphmode==FULLSCREEN) {	/* Flush any buffered text to the screen */
        if (!echo) echo_text();
      }
      else {
        if (!echo) echo_ttext();
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
  if (vdunext < vduneeded) return;
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
    if (cursorstate == HIDDEN) {	/* Start displaying the cursor */
      cursorstate = SUSPENDED;
      toggle_cursor();
    }
    break;
  case VDU_GRAPHICURS:	/* 5 - Print text at graphics cursor */
    if (graphmode == TEXTMODE) switch_graphics();		/* Use VDU 5 as a way of switching to graphics mode */
    if (graphmode == FULLSCREEN) {
      vdu5mode = TRUE;
      toggle_cursor();	/* Remove the cursor if it is being displayed */
      cursorstate = HIDDEN;
    }
    break;
  case VDU_ENABLE:	/* 6 - Enable the VDU driver (ignored) */
    enable_vdu = TRUE;
    break;
  case VDU_BEEP:	/* 7 - Sound the bell */
    putchar('\7');
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
//    putch(vducmd);
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
  if (length == 0) length = strlen(string);
  echo_off();
  for (n = 0; n < length-1; n++) emulate_vdu(string[n]);	/* Send the string to the VDU driver */
  echo_on();
  emulate_vdu(string[length-1]);        /* last char sent after echo turned back on */
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
  if (mode > HIGHMODE) mode = modecopy = 0;	/* User-defined modes are mapped to mode 0 */
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
  scaled = yscale != 1 || xscale != 1;	/* TRUE if graphics screen is scaled to fit real screen */
/* If running in text mode, ignore the screen depth */
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
  if (graphmode == FULLSCREEN && (!basicvars.runflags.start_graphics || !modetable[mode].graphics)) {
    switch_text();
    graphmode = TEXTONLY;
  }
  if (graphmode != NOGRAPHICS && graphmode != FULLSCREEN) {	/* Decide on current graphics mode */
    if (modetable[mode].graphics)
      graphmode = TEXTMODE;	/* Output to text screen but can switch to graphics */
    else {
      graphmode = TEXTONLY;	/* Output to text screen. Mode does not allow graphics */
    }
  }
  reset_colours();
  if (graphmode == FULLSCREEN) {
    init_palette();
    if (cursorstate == NOCURSOR) cursorstate = ONSCREEN;
    SDL_FillRect(screen0, NULL, tb_colour);
    SDL_FillRect(modescreen, NULL, tb_colour);
    if (xoffset == 0)	/* Use whole screen */
      SDL_SetClipRect(screen0, NULL);
    else {	/* Only part of the screen is used */
      line_rect.x = xoffset;
      line_rect.y = yoffset;
      line_rect.w = vscrwidth;
      line_rect.h = vscrheight;
      SDL_SetClipRect(screen0, &line_rect);
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
/* Reset colours, clear screen and home cursor */
  SDL_FillRect(screen0, NULL, tb_colour);
  SDL_FillRect(modescreen, NULL, tb_colour);
  xtext = twinleft;
  ytext = twintop;
  SDL_Flip(screen0);
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
  if (xres == 0 || yres == 0 || rate == 0 || (colours == 0 && greys == 0)) error(ERR_BADMODE);
  coldepth = colours!=0 ? colours : greys;
  for (n=0; n <= HIGHMODE; n++) {
    if (modetable[n].xres == xres && modetable[n].yres == yres && modetable[n].coldepth == coldepth) break;
  }
  if (n > HIGHMODE) error(ERR_BADMODE);
  emulate_mode(n);
  if (colours == 0) {	/* Want a grey scale palette  - Reset all the colours */
    int32 step, intensity;
    step = 255/(greys-1);
    intensity = 0;
    for (n=0; n < greys; n++) {
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
  SDL_Rect plot_rect;
  boolean above, below;
  pwinleft = GXTOPX(gwinleft);		/* Calculate extent of graphics window in pixels */
  pwinright = GXTOPX(gwinright);
  pwintop = GYTOPY(gwintop);
  pwinbottom = GYTOPY(gwinbottom);
  if (x < pwinleft || x > pwinright || y < pwintop || y > pwinbottom
   || *((Uint32*)modescreen->pixels + x + y*vscrwidth) != gb_colour) return;
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
    if (y < top) top = y;
    if (y > bottom) bottom = y;
    above = below = FALSE;
    while (lleft >= pwinleft && *((Uint32*)modescreen->pixels + lleft + y*vscrwidth) == gb_colour) {
      if (y > pwintop) {	/* Check if point above current point is set to the background colour */
        if (*((Uint32*)modescreen->pixels + lleft + (y-1)*vscrwidth) != gb_colour)
          above = FALSE;
        else if (!above) {
          above = TRUE;
          if (sp == FILLSTACK) return;	/* Shape is too complicated so give up */
          fillx[sp] = lleft;
          filly[sp] = y-1;
          sp++;
        }
      }
      if (y < pwinbottom) {	/* Check if point below current point is set to the background colour */
        if (*((Uint32*)modescreen->pixels + lleft + (y+1)*vscrwidth) != gb_colour)
          below = FALSE;
        else if (!below) {
          below = TRUE;
          if (sp == FILLSTACK) return;	/* Shape is too complicated so give up */
          fillx[sp] = lleft;
          filly[sp] = y+1;
          sp++;
        }
      }
      lleft--;
    }
    lleft++;	/* Move back to first column set to background colour */
    above = below = FALSE;
    while (lright <= pwinright && *((Uint32*)modescreen->pixels + lright + y*vscrwidth) == gb_colour) {
      if (y > pwintop) {
        if (*((Uint32*)modescreen->pixels + lright + (y-1)*vscrwidth) != gb_colour)
          above = FALSE;
        else if (!above) {
          above = TRUE;
          if (sp == FILLSTACK) return;	/* Shape is too complicated so give up */
          fillx[sp] = lright;
          filly[sp] = y-1;
          sp++;
        }
      }
      if (y < pwinbottom) {
        if (*((Uint32*)modescreen->pixels + lright + (y+1)*vscrwidth) != gb_colour)
          below = FALSE;
        else if (!below) {
          below = TRUE;
          if (sp == FILLSTACK) return;	/* Shape is too complicated so give up */
          fillx[sp] = lright;
          filly[sp] = y+1;
          sp++;
        }
      }
      lright++;
    }
    lright--;
    draw_line(modescreen, vscrwidth, vscrheight, lleft, y, lright, y, colour);
    if (lleft < left) left = lleft;
    if (lright > right) right = lright;
  } while (sp != 0);
  if (!scaled) {
    plot_rect.x = left;
    plot_rect.y = top;
    plot_rect.w = right - left +1;
    plot_rect.h = bottom - top +1;
    SDL_BlitSurface(modescreen, &plot_rect, screen0, &plot_rect);
  }
  else {
    if (cursorstate == ONSCREEN) toggle_cursor();
    blit_scaled(left, top, right, bottom);
    if (cursorstate == SUSPENDED) toggle_cursor();
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
  int32 xlast3, ylast3, sx, sy, ex, ey;
  Uint32 colour = 0;
  SDL_Rect plot_rect, temp_rect;
  if (graphmode == TEXTONLY) return;
  if (graphmode == TEXTMODE) switch_graphics();
/* Decode the command */
  xlast3 = xlast2;
  ylast3 = ylast2;
  xlast2 = xlast;
  ylast2 = ylast;
  if ((code & ABSCOORD_MASK) != 0 ) {		/* Coordinate (x,y) is absolute */
    xlast = x+xorigin;	/* These probably have to be treated as 16-bit values */
    ylast = y+yorigin;
  }
  else {	/* Coordinate (x,y) is relative */
    xlast+=x;	/* These probably have to be treated as 16-bit values */
    ylast+=y;
  }
  if ((code & PLOT_COLMASK) == PLOT_MOVEONLY) return;	/* Just moving graphics cursor, so finish here */
  sx = GXTOPX(xlast2);
  sy = GYTOPY(ylast2);
  ex = GXTOPX(xlast);
  ey = GYTOPY(ylast);
  if ((code & GRAPHOP_MASK) != SHIFT_RECTANGLE) {		/* Move and copy rectangle are a special case */
    switch (code & PLOT_COLMASK) {
    case PLOT_FOREGROUND:	/* Use graphics foreground colour */
      colour = gf_colour;
      break;
    case PLOT_INVERSE:		/* Use logical inverse of colour at each point */
      error(ERR_UNSUPPORTED);
      break;
    case PLOT_BACKGROUND:	/* Use graphics background colour */
      colour = gb_colour;
    }
  }
/* Now carry out the operation */
  switch (code & GRAPHOP_MASK) {
  case DRAW_SOLIDLINE: {	/* Draw line */
    int32 top, left;
    left = sx;	/* Find top left-hand corner of rectangle containing line */
    top = sy;
    if (ex < sx) left = ex;
    if (ey < sy) top = ey;
    draw_line(modescreen, vscrwidth, vscrheight, sx, sy, ex, ey, colour);
    if (!scaled) {
      plot_rect.x = left;
      plot_rect.y = top;
      plot_rect.w = sx + ex - 2*left +1;
      plot_rect.h = sy + ey - 2*top +1;
      SDL_BlitSurface(modescreen, &plot_rect, screen0, &plot_rect);
    }
    else {
      if (cursorstate == ONSCREEN) toggle_cursor();
      blit_scaled(left, top, sx+ex-left, sy+ey-top);
      if (cursorstate == SUSPENDED) toggle_cursor();
    }
    break;
  }
  case PLOT_POINT:	/* Plot a single point */
    if (!scaled) {
      plot_rect.x = ex; /* we need to set this for the UpdateRect */
      plot_rect.y = ey;
      plot_rect.w = 1;
      plot_rect.h = 1;
      *((Uint32*)screen0->pixels + ex + ey*vscrwidth) = colour;
    }
    else {
      if (cursorstate == ONSCREEN) toggle_cursor();
      *((Uint32*)modescreen->pixels + ex + ey*vscrwidth) = colour;
      blit_scaled(ex, ey, ex, ey);
      if (cursorstate == SUSPENDED) toggle_cursor();
    }
    break;
  case FILL_TRIANGLE: {		/* Plot a filled triangle */
    int32 left, right, top, bottom;
    filled_triangle(modescreen, vscrwidth, vscrheight, GXTOPX(xlast3), GYTOPY(ylast3), sx, sy, ex, ey, colour);
/*  Now figure out the coordinates of the rectangle that contains the triangle */
    left = right = xlast3;
    top = bottom = ylast3;
    if (xlast2 < left) left = xlast2;
    if (xlast < left) left = xlast;
    if (xlast2 > right) right = xlast2;
    if (xlast > right) right = xlast;
    if (ylast2 > top) top = ylast2;
    if (ylast > top) top = ylast;
    if (ylast2 < bottom) bottom = ylast2;
    if (ylast < bottom) bottom = ylast;
    if (!scaled) {
      plot_rect.x = GXTOPX(left);
      plot_rect.y = GYTOPY(top);
      plot_rect.w = GXTOPX(right) - GXTOPX(left) +1;
      plot_rect.h = GYTOPY(bottom) - GYTOPY(top) +1;
      SDL_BlitSurface(modescreen, &plot_rect, screen0, &plot_rect);
    }
    else {
      if (cursorstate == ONSCREEN) toggle_cursor();
      blit_scaled(GXTOPX(left), GYTOPY(top), GXTOPX(right), GYTOPY(bottom));
      if (cursorstate == SUSPENDED) toggle_cursor();
    }
    break;
  }
  case FILL_RECTANGLE: {		/* Plot a filled rectangle */
    int32 left, right, top, bottom;
    left = sx;
    top = sy;
    if (ex < sx) left = ex;
    if (ey < sy) top = ey;
    right = sx+ex-left;
    bottom = sy+ey-top;
/* sx and sy give the bottom left-hand corner of the rectangle */
/* x and y are its width and height */
    plot_rect.x = left;
    plot_rect.y = top;
    plot_rect.w = right - left +1;
    plot_rect.h = bottom - top +1;
    SDL_FillRect(modescreen, &plot_rect, colour);
    if (!scaled)
      SDL_FillRect(screen0, &plot_rect, colour);
    else {
      if (cursorstate == ONSCREEN) toggle_cursor();
      blit_scaled(left, top, right, bottom);
      if (cursorstate == SUSPENDED) toggle_cursor();
    }
    break;
  }
  case FILL_PARALLELOGRAM: {	/* Plot a filled parallelogram */
    int32 vx, vy, left, right, top, bottom;
    filled_triangle(modescreen, vscrwidth, vscrheight, GXTOPX(xlast3), GYTOPY(ylast3), sx, sy, ex, ey, colour);
    vx = xlast3-xlast2+xlast;
    vy = ylast3-ylast2+ylast;
    filled_triangle(modescreen, vscrwidth, vscrheight, ex, ey, GXTOPX(vx), GYTOPY(vy), GXTOPX(xlast3), GYTOPY(ylast3), colour);
/*  Now figure out the coordinates of the rectangle that contains the parallelogram */
    left = right = xlast3;
    top = bottom = ylast3;
    if (xlast2 < left) left = xlast2;
    if (xlast < left) left = xlast;
    if (vx < left) left = vx;
    if (xlast2 > right) right = xlast2;
    if (xlast > right) right = xlast;
    if (vx > right) right = vx;
    if (ylast2 > top) top = ylast2;
    if (ylast > top) top = ylast;
    if (vy > top) top = vy;
    if (ylast2 < bottom) bottom = ylast2;
    if (ylast < bottom) bottom = ylast;
    if (vy < bottom) bottom = vy;
    if (!scaled) {
      plot_rect.x = GXTOPX(left);
      plot_rect.y = GYTOPY(top);
      plot_rect.w = GXTOPX(right) - GXTOPX(left) +1;
      plot_rect.h = GYTOPY(bottom) - GYTOPY(top) +1;
      SDL_BlitSurface(modescreen, &plot_rect, screen0, &plot_rect);
    }
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
*/
    xradius = abs(xlast2-xlast)/xgupp;
    yradius = abs(xlast2-xlast)/ygupp;
    if ((code & GRAPHOP_MASK) == PLOT_CIRCLE)
      draw_ellipse(modescreen, vscrwidth, vscrheight, sx, sy, xradius, yradius, colour);
    else {
      filled_ellipse(modescreen, vscrwidth, vscrheight, sx, sy, xradius, yradius, colour);
    }
    ex = sx-xradius;
    ey = sy-yradius;
/* (ex, ey) = coordinates of top left hand corner of the rectangle that contains the ellipse */
    if (!scaled) {
      plot_rect.x = ex;
      plot_rect.y = ey;
      plot_rect.w = 2*xradius +1; /* Add one to fix rounding errors */
      plot_rect.h = 2*yradius +1;
      SDL_BlitSurface(modescreen, &plot_rect, screen0, &plot_rect);
    }
    else {
      if (cursorstate == ONSCREEN) toggle_cursor();
      blit_scaled(ex, ey, ex+2*xradius, ey+2*yradius);
      if (cursorstate == SUSPENDED) toggle_cursor();
    }
    break;
  }
  case SHIFT_RECTANGLE: {	/* Move or copy a rectangle */
    int32 destleft, destop, left, right, top, bottom;
    if (xlast3 < xlast2) {	/* Figure out left and right hand extents of rectangle */
      left = GXTOPX(xlast3);
      right = GXTOPX(xlast2);
    }
    else {
      left = GXTOPX(xlast2);
      right = GXTOPX(xlast3);
    }
    if (ylast3 > ylast2) {	/* Figure out upper and lower extents of rectangle */
      top = GYTOPY(ylast3);
      bottom = GYTOPY(ylast2);
    }
    else {
      top = GYTOPY(ylast2);
      bottom = GYTOPY(ylast3);
    }
    destleft = GXTOPX(xlast);		/* X coordinate of top left-hand corner of destination */
    destop = GYTOPY(ylast)-(bottom-top);	/* Y coordinate of top left-hand corner of destination */
    plot_rect.x = destleft;
    plot_rect.y = destop;
    temp_rect.x = left;
    temp_rect.y = top;
    temp_rect.w = plot_rect.w = right - left +1;
    temp_rect.h = plot_rect.h = bottom - top +1;
    SDL_BlitSurface(modescreen, &temp_rect, screen1, &plot_rect); /* copy to temp buffer */
    SDL_BlitSurface(screen1, &plot_rect, modescreen, &plot_rect);
    if (!scaled)
      SDL_BlitSurface(modescreen, &plot_rect, screen0, &plot_rect);
    else {
      if (cursorstate == ONSCREEN) toggle_cursor();
      blit_scaled(destleft, destop, destleft+(right-left), destop+(bottom-top));
      if (cursorstate == SUSPENDED) toggle_cursor();
    }
    if (code == MOVE_RECTANGLE) {	/* Move rectangle - Set original rectangle to the background colour */
      int32 destright, destbot;
      destright = destleft+right-left;
      destbot = destop+bottom-top;
/* Check if source and destination rectangles overlap */
      if (((destleft >= left && destleft <= right) || (destright >= left && destright <= right)) &&
       ((destop >= top && destop <= bottom) || (destbot >= top && destbot <= bottom))) {	/* Overlap found */
        int32 xdiff, ydiff;
/*
** The area of the original rectangle that is not overlapped can be
** broken down into one or two smaller rectangles. Figure out the
** coordinates of those rectangles and plot filled rectangles over
** them set to the graphics background colour
*/
        xdiff = left-destleft;
        ydiff = top-destop;
        if (ydiff > 0) {	/* Destination area is higher than the original area on screen */
          if (xdiff > 0) {
            plot_rect.x = destright+1;
            plot_rect.y = top;
            plot_rect.w = right - (destright+1) +1;
            plot_rect.h = destbot - top +1;
            SDL_FillRect(modescreen, &plot_rect, gb_colour);
          }
          else if (xdiff < 0) {
            plot_rect.x = left;
            plot_rect.y = top;
            plot_rect.w = (destleft-1) - left +1;
            plot_rect.h = destbot - top +1;
            SDL_FillRect(modescreen, &plot_rect, gb_colour);
          }
          plot_rect.x = left;
          plot_rect.y = destbot+1;
          plot_rect.w = right - left +1;
          plot_rect.h = bottom - (destbot+1) +1;
          SDL_FillRect(modescreen, &plot_rect, gb_colour);
        }
        else if (ydiff == 0) {	/* Destination area is on same level as original area */
          if (xdiff > 0) {	/* Destination area lies to left of original area */
            plot_rect.x = destright+1;
            plot_rect.y = top;
            plot_rect.w = right - (destright+1) +1;
            plot_rect.h = bottom - top +1;
            SDL_FillRect(modescreen, &plot_rect, gb_colour);
          }
          else if (xdiff < 0) {
            plot_rect.x = left;
            plot_rect.y = top;
            plot_rect.w = (destleft-1) - left +1;
            plot_rect.h = bottom - top +1;
            SDL_FillRect(modescreen, &plot_rect, gb_colour);
          }
        }
        else {	/* Destination area is lower than original area on screen */
          if (xdiff > 0) {
            plot_rect.x = destright+1;
            plot_rect.y = destop;
            plot_rect.w = right - (destright+1) +1;
            plot_rect.h = bottom - destop +1;
            SDL_FillRect(modescreen, &plot_rect, gb_colour);
          }
          else if (xdiff < 0) {
            plot_rect.x = left;
            plot_rect.y = destop;
            plot_rect.w = (destleft-1) - left +1;
            plot_rect.h = bottom - destop +1;
            SDL_FillRect(modescreen, &plot_rect, gb_colour);
          }
          plot_rect.x = left;
          plot_rect.y = top;
          plot_rect.w = right - left +1;
          plot_rect.h = (destop-1) - top +1;
          SDL_FillRect(modescreen, &plot_rect, gb_colour);
        }
      }
      else {	/* No overlap - Simple case */
        plot_rect.x = left;
        plot_rect.y = top;
        plot_rect.w = right - left +1;
        plot_rect.h = bottom - top +1;
        SDL_FillRect(modescreen, &plot_rect, gb_colour);
      }
      if (!scaled) {
        plot_rect.x = left;
        plot_rect.y = top;
        plot_rect.w = right - left +1;
        plot_rect.h = bottom - top +1;
        SDL_BlitSurface(modescreen, &plot_rect, screen0, &plot_rect);
      }
      else {
        if (cursorstate == ONSCREEN) toggle_cursor();
        blit_scaled(left, top, right, bottom);
        if (cursorstate == SUSPENDED) toggle_cursor();
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
    if ((code & GRAPHOP_MASK) == PLOT_ELLIPSE)
      draw_ellipse(modescreen, vscrwidth, vscrheight, sx, sy, semimajor, semiminor, colour);
    else {
      filled_ellipse(modescreen, vscrwidth, vscrheight, sx, sy, semimajor, semiminor, colour);
    }
    ex = sx-semimajor;
    ey = sy-semiminor;
/* (ex, ey) = coordinates of top left hand corner of the rectangle that contains the ellipse */
    if (!scaled) {
      plot_rect.x = ex;
      plot_rect.y = ey;
      plot_rect.w = 2*semimajor;
      plot_rect.h = 2*semiminor;
      SDL_BlitSurface(modescreen, &plot_rect, screen0, &plot_rect);
    }
    else {
      if (cursorstate == ONSCREEN) toggle_cursor();
      blit_scaled(ex, ey, ex+2*semimajor, ey+2*semiminor);
      if (cursorstate == SUSPENDED) toggle_cursor();
    }
    break;
  }
  default:
    error(ERR_UNSUPPORTED);
  }
  if (!scaled) SDL_UpdateRect(screen0, plot_rect.x, plot_rect.y, plot_rect.w, plot_rect.h);
}

/*
** 'emulate_pointfn' emulates the Basic function 'POINT', returning
** the colour number of the point (x,y) on the screen
*/
int32 emulate_pointfn(int32 x, int32 y) {
  int32 colour;
  if (graphmode == FULLSCREEN) {
    colour = *((Uint32*)modescreen->pixels + GXTOPX(x+xorigin) + GYTOPY(y+yorigin)*vscrwidth);
    if (colourdepth == 256) colour = colour>>COL256SHIFT;
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
  if (graphmode != FULLSCREEN || colourdepth != 256) return 0;
  return *((Uint32*)modescreen->pixels + GXTOPX(x+xorigin) + GYTOPY(y+yorigin)*vscrwidth)<<TINTSHIFT;
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
  emulate_tint(colour < 128 ? TINT_FOREGRAPH : TINT_BACKGRAPH, tint);
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
  if (angle != 0.0) error(ERR_UNSUPPORTED);	/* Graphics library limitation */
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

  static SDL_Surface *fontbuf;
  int flags = SDL_DOUBLEBUF | SDL_HWSURFACE;

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
    fprintf(stderr, "Unable to init SDL: %s\n", SDL_GetError());
    return FALSE;
  }

  screen0 = SDL_SetVideoMode(SCREEN_WIDTH, SCREEN_HEIGHT, 32, flags);
  if (!screen0) {
    fprintf(stderr, "Failed to open screen: %s\n", SDL_GetError());
    return FALSE;
  }
  screen1 = SDL_DisplayFormat(screen0);
  modescreen = SDL_DisplayFormat(screen0);
  fontbuf = SDL_CreateRGBSurface(SDL_SWSURFACE,   XPPC,   YPPC, 32, 0xff000000, 0x00ff0000, 0x0000ff00, 0x000000ff);
  sdl_fontbuf = SDL_ConvertSurface(fontbuf, screen0->format, 0);  /* copy surface to get same format as main windows */
  SDL_FreeSurface(fontbuf);

  vdunext = 0;
  vduneeded = 0;
  enable_print = FALSE;
  graphmode = TEXTMODE;         /* Say mode is capable of graphics output but currently set to text */
  vscrwidth = SCREEN_WIDTH;	    /* vscrwidth and vscrheight are constants for now but they */
  vscrheight = SCREEN_HEIGHT;   /* might be variables in the future if we have resizeable windows */
  xgupp = ygupp = 1;
  SDL_WM_SetCaption("Brandy Basic V Interpreter", "Brandy");
  SDL_EnableUNICODE(SDL_ENABLE);
  SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
  if (basicvars.runflags.start_graphics) {
    setup_mode(31);		/* Mode 31 - 800 by 600 by 16 colours */
    switch_graphics();
  }
  else {
    setup_mode(46);	/* Mode 46 - 80 columns by 25 lines by 16 colours */
  }

  xor_mask = SDL_MapRGB(sdl_fontbuf->format, 0xff, 0xff, 0xff);

  font_rect.x = font_rect.y = 0;
  font_rect.w = XPPC;
  font_rect.h = YPPC;

  place_rect.x = place_rect.y = 0;
  place_rect.w = XPPC;
  place_rect.h = YPPC;

  scale_rect.x = scale_rect.y = 0;
  scale_rect.w = 1;
  scale_rect.h = 1;

  return TRUE;
}

/*
** 'end_screen' is called to tidy up the VDU emulation at the end
** of the run
*/
void end_screen(void) {
  SDL_EnableUNICODE(SDL_DISABLE);
  SDL_Quit();
}
