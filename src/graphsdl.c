/*
** This file is part of the Matrix Brandy Basic VI Interpreter.
** Copyright (C) 2000-2014 David Daniels
** Copyright (C) 2018-2019 Michael McConnell and contributors
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
**	MODE 7 implementation by Michael McConnell.
**
*/
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <SDL.h>
#include <sys/time.h>
#include <math.h>
#include "common.h"
#include "target.h"
#include "errors.h"
#include "basicdefs.h"
#include "scrcommon.h"
#include "screen.h"
#include "mos.h"
#include "keyboard.h"
#include "graphsdl.h"
#include "textfonts.h"

/*
** Notes
** -----
**  This is one of the four versions of the VDU driver emulation.
**  It is used by versions of the interpreter where graphics are
**  supported as well as text output using the SDL library.
**  The four versions of the VDU driver code are in:
**	riscos.c
**	graphsdl.c
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

static int displaybank=0;
static int writebank=0;
#define MAXBANKS 4

/*
** SDL related defines, Variables and params
*/
static SDL_Surface *screenbank[MAXBANKS];
static SDL_Surface *screen1, *screen2, *screen2A, *screen3, *screen3A;
static SDL_Surface *modescreen;	/* Buffer used when screen mode is scaled to fit real screen */
static SDL_Surface *sdl_fontbuf, *sdl_m7fontbuf;

static SDL_Rect font_rect, place_rect, scroll_rect, line_rect, scale_rect, m7_rect;

Uint32 tf_colour,       /* text foreground SDL rgb triple */
       tb_colour,       /* text background SDL rgb triple */
       gf_colour,       /* graphics foreground SDL rgb triple */
       gb_colour;       /* graphics background SDL rgb triple */

Uint32 xor_mask;

/*
** function definitions
*/

static void reveal_cursor(void);
static void plot_pixel(SDL_Surface *, int64, Uint32, Uint32);
static void draw_line(SDL_Surface *, int32, int32, int32, int32, Uint32, int32, Uint32);
static void filled_triangle(SDL_Surface *, int32, int32, int32, int32, int32, int32, Uint32, Uint32);
static void draw_ellipse(SDL_Surface *, int32, int32, int32, int32, int32, Uint32, Uint32);
static void filled_ellipse(SDL_Surface *, int32, int32, int32, int32, int32, Uint32, Uint32);
static void toggle_cursor(void);
static void vdu_cleartext(void);
static void set_text_colour(boolean background, int colnum);
static void set_graphics_colour(boolean background, int colnum);

static void mode7renderline(int32 ypos);

static Uint8 palette[768];		/* palette for screen */
static Uint8 hardpalette[24];		/* palette for screen */

static Uint8 vdu2316byte = 1;		/* Byte set by VDU23,16. */

static unsigned int vdu14lines = 0;	/* Line counter for VDU14 page mode */

static int autorefresh=1;		/* Refresh screen on updates? */

/* From geom.c */
#define MAX_YRES 16384
#define MAX_XRES 16384
static int32 geom_left[MAX_YRES], geom_right[MAX_YRES];
#define FAST_2_MUL(x) ((x)<<1)
#define FAST_3_MUL(x) (((x)<<1)+x)
#define FAST_4_MUL(x) ((x)<<2)
#define FAST_4_DIV(x) ((x)>>2)

/* Data stores for controlling MODE 7 operation */
Uint8 mode7frame[25][40];		/* Text frame buffer for Mode 7, akin to BBC screen memory at &7C00 */
Uint8 mode7changed[26];			/* Marks changed lines */
static int32 mode7prevchar = 0;		/* Placeholder for storing previous char */
static int64 mode7timer = 0;		/* Timer for bank switching */
static Uint8 vdu141track[27];		/* Track use of Double Height in Mode 7 *
					 * First line is [1] */

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
  graph_fore_action,		/* Foreground graphics PLOT action */
  graph_back_action,		/* Background graphics PLOT action (ignored) */
  graph_forecol,		/* Current graphics foreground logical colour number */
  graph_backcol,		/* Current graphics background logical colour number */
  graph_physforecol,		/* Current graphics foreground physical colour number */
  graph_physbackcol,		/* Current graphics background physical colour number */
  graph_foretint,		/* Tint value added to foreground graphics colour in 256 colour modes */
  graph_backtint,		/* Tint value added to background graphics colour in 256 colour modes */
  plot_inverse,			/* PLOT in inverse colour? */
  xlast,			/* Graphics X coordinate of last point visited */
  ylast,			/* Graphics Y coordinate of last point visited */
  xlast2,			/* Graphics X coordinate of last-but-one point visited */
  ylast2,			/* Graphics Y coordinate of last-but-one point visited */
  xorigin,			/* X coordinate of graphics origin */
  yorigin,			/* Y coordinate of graphics origin */
  xscale,			/* X direction scale factor */
  yscale;			/* Y direction scale factor */

static boolean
  scaled,			/* TRUE if screen mode is scaled to fit real screen */
  clipping;			/* TRUE if clipping region is not full screen of a RISC OS mode */

/*
** These two macros are used to convert from RISC OS graphics coordinates to
** pixel coordinates
*/
#define GXTOPX(x) ((x) / xgupp)
#define GYTOPY(y) ((ygraphunits - 1 -(y)) / ygupp)

unsigned int XPPC=8;		/* Size of character in pixels in X direction */
unsigned int YPPC=8;		/* Size of character in pixels in Y direction */
unsigned int M7XPPC=16;		/* Size of Mode 7 characters in X direction */
unsigned int M7YPPC=20;		/* Size of Mode 7 characters in Y direction */

//static unsigned int vduflag(unsigned int flags) {
//  return (vduflags & flags) ? 1 : 0;
//}
#define vduflag(flags) ((vduflags & flags) ? 1 : 0)

static void write_vduflag(unsigned int flags, int yesno) {
  vduflags = yesno ? vduflags | flags : vduflags & ~flags;
}

static void reset_mode7() {
  int p, q;
  write_vduflag(MODE7_VDU141MODE,1);
  write_vduflag(MODE7_VDU141ON,0);
  write_vduflag(MODE7_HIGHBIT,0);
  write_vduflag(MODE7_SEPGRP,0);
  write_vduflag(MODE7_SEPREAL,0);
  write_vduflag(MODE7_CONCEAL,0);
  write_vduflag(MODE7_HOLD,0);
  write_vduflag(MODE7_FLASH,0);
  write_vduflag(MODE7_BANK,0);
  mode7timer=0;
  mode7prevchar=32;
  place_rect.h=M7YPPC;
  font_rect.h=M7YPPC;

  for(p=0;p<26;p++) mode7changed[p]=vdu141track[p]=0;
  for (p=0; p<25; p++) {
    for (q=0; q<40; q++) mode7frame[p][q]=32;
  }
}

void reset_vdu14lines(void) {
  vdu14lines=0;
}

void reset_sysfont(int x) {
  int p, c, i;

  if (!x) {
    memcpy(sysfont, sysfontbase, sizeof(sysfont));
    memcpy(mode7font, mode7fontro5, sizeof(mode7font));
    if (screenmode == 7) mode7renderscreen();
    return;
  }
  if ((x>=1) && (x<= 7)) {
    p=(x-1)*32;
    for (c=0; c<= 31; c++) 
      for (i=0; i<= 7; i++) sysfont[p+c][i]=sysfontbase[p+c][i];
  }
  if (x ==8){
    for (c=0; c<=95; c++)
      for (i=0; i<= 7; i++) sysfont[c][i]=sysfontbase[c][i];
  }
  if (x == 16) {
    memcpy(mode7font, mode7fontro5, sizeof(mode7font));
    if (screenmode == 7) mode7renderscreen();
  }
}

static void do_sdl_flip(SDL_Surface *layer) {
  if (autorefresh==1) SDL_Flip(layer);
}

static void do_sdl_updaterect(SDL_Surface *layer, Sint32 x, Sint32 y, Sint32 w, Sint32 h) {
  if (autorefresh==1) SDL_UpdateRect(layer, x, y, w, h);
}

static int istextonly(void) {
  return ((screenmode == 3 || screenmode == 6 || screenmode == 7));
}

static int32 riscoscolour(int32 colour) {
  return (((colour & 0xFF) <<16) + (colour & 0xFF00) + ((colour & 0xFF0000) >> 16));
}

static int32 tint24bit(int32 colour, int32 tint) {
  colour=(colour & 0xC0C0C0);
  colour += ((colour & 0xF0) ? (tint << 4) : 0)+((colour & 0xF000) ? (tint << 12) : 0)+((colour & 0xF00000) ? (tint << 20) : 0);
  if (colour == 0) colour+=(tint << 4)+(tint << 12)+(tint << 20);
  return colour + (colour >> 4);
}

static int32 colour24bit(int32 colour, int32 tint) {
  int32 col=(((colour & 1) << 6) + ((colour & 2) << 6)) +
	 (((colour & 4) << 12) + ((colour & 8) << 12)) +
	 (((colour & 16) << 18) + ((colour & 32) << 18));
  col = tint24bit(col, tint);
  return col;
}

static int32 textxhome(void) {
  if (vdu2316byte & 2) return twinright;
  return twinleft;
}

static int32 textxedge(void) {
  if (vdu2316byte & 2) return twinleft;
  return twinright;
}

static int32 textxinc(void) {
  if (vdu2316byte & 32) return 0;
  if (vdu2316byte & 2) return -1;
  return 1;
}

static int32 textyhome(void) {
  if (vdu2316byte & 4) return twinbottom;
  return twintop;
}

static int32 textyedge(void) {
  if (vdu2316byte & 4) return twintop;
  return twinbottom;
}

static int32 textyinc(void) {
  if (vdu2316byte & 4) return -1;
  return 1;
}

/*
** 'find_cursor' locates the cursor on the text screen and ensures that
** its position is valid, that is, lies within the text window.
** For the SDL display, this is a no-op.
*/
void find_cursor(void) {
  return;
}

static void set_rgb(void) {
  int j;
  if (colourdepth == COL24BIT) {
    tf_colour = SDL_MapRGB(sdl_fontbuf->format, (text_physforecol & 0xFF), ((text_physforecol & 0xFF00) >> 8), ((text_physforecol & 0xFF0000) >> 16));
    tb_colour = SDL_MapRGB(sdl_fontbuf->format, (text_physbackcol & 0xFF), ((text_physbackcol & 0xFF00) >> 8), ((text_physbackcol & 0xFF0000) >> 16));
    gf_colour = SDL_MapRGB(sdl_fontbuf->format, (graph_physforecol & 0xFF), ((graph_physforecol & 0xFF00) >> 8), ((graph_physforecol & 0xFF0000) >> 16));
    gb_colour = SDL_MapRGB(sdl_fontbuf->format, (graph_physbackcol & 0xFF), ((graph_physbackcol & 0xFF00) >> 8), ((graph_physbackcol & 0xFF0000) >> 16));
  } else {
    j = text_physforecol*3;
    tf_colour = SDL_MapRGB(sdl_fontbuf->format, palette[j], palette[j+1], palette[j+2]) + (text_forecol << 24);
    j = text_physbackcol*3;
    tb_colour = SDL_MapRGB(sdl_fontbuf->format, palette[j], palette[j+1], palette[j+2]) + (text_backcol << 24);

    j = graph_physforecol*3;
    gf_colour = SDL_MapRGB(sdl_fontbuf->format, palette[j], palette[j+1], palette[j+2]) + (graph_forecol << 24);
    j = graph_physbackcol*3;
    gb_colour = SDL_MapRGB(sdl_fontbuf->format, palette[j], palette[j+1], palette[j+2]) + (graph_backcol << 24);
  }
}

/*
** 'vdu_2316' deals with various flavours of the sequence VDU 23,16,...
** The spec is awkward: Given VDU 23,16,x,y,0,0,0,0,0,0 the control byte is changed using:
** ((current byte) AND y) EOR x
*/
static void vdu_2316(void) {
  vdu2316byte =(vdu2316byte & vduqueue[2]) ^ vduqueue[1];
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
    if (colourdepth==COL24BIT) text_physforecol=tint24bit(text_forecol, text_foretint);
    break;
  case TINT_BACKTEXT:
    text_backtint = (vduqueue[2] & TINTMASK)>>TINTSHIFT;
    if (colourdepth==256) text_physbackcol = (text_backcol<<COL256SHIFT)+text_backtint;
    if (colourdepth==COL24BIT) text_physbackcol=tint24bit(text_backcol, text_backtint);
    break;
  case TINT_FOREGRAPH:
    graph_foretint = (vduqueue[2] & TINTMASK)>>TINTSHIFT;
    if (colourdepth==256) graph_physforecol = (graph_forecol<<COL256SHIFT)+graph_foretint;
    if (colourdepth==COL24BIT) graph_physforecol=tint24bit(graph_forecol, graph_foretint);
    break;
  case TINT_BACKGRAPH:
    graph_backtint = (vduqueue[2] & TINTMASK)>>TINTSHIFT;
    if (colourdepth==256) graph_physbackcol = (graph_backcol<<COL256SHIFT)+graph_backtint;
    if (colourdepth==COL24BIT) graph_physbackcol=tint24bit(graph_backcol, graph_backtint);
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

/* RISC OS 5 - Set Teletext characteristics */
static void vdu_2318(void) {
  if (vduqueue[1] == 1) {
    write_vduflag(MODE7_UPDATE_HIGHACC, vduqueue[2] & 2);
    write_vduflag(MODE7_UPDATE, (vduqueue[2] & 1)==0);
  }
  if (vduqueue[1] == 2) {
    write_vduflag(MODE7_REVEAL, vduqueue[2] & 1);
  }
  if (vduqueue[1] == 3) {
    write_vduflag(MODE7_BLACK, vduqueue[2] & 1);
  }
  mode7renderscreen();
}

/* BB4W/BBCSDL - Define and select custom mode */
/* Implementation not likely to be exact, char width and height are fixed in Brandy so are /8 to generate xscale and yscale */
static void vdu_2322(void) {
  int32 mwidth, mheight, mxscale, myscale, cols, charset;
  
  mwidth=(vduqueue[1] + (vduqueue[2]<<8));
  mheight=(vduqueue[3] + (vduqueue[4]<<8));
  mxscale=vduqueue[5]/8;
  myscale=vduqueue[6]/8;
  cols=vduqueue[7];
  charset=vduqueue[8];
  if ((cols != 0) && (cols != 2) && (cols != 4) && (cols != 16)) return; /* Invalid colours, do nothing */
  if (0 == cols) cols=256;
  if (0 == mxscale) mxscale=1;
  if (0 == myscale) myscale=1;
  setupnewmode(126,mwidth/mxscale,mheight/myscale,cols,mxscale,myscale,1,1);
  emulate_mode(126);
  if (charset & 0x80) {
    text_physforecol = text_forecol = 0;
    if(cols==256) {
      text_backcol = 63;
      text_physbackcol = (text_backcol << COL256SHIFT)+text_foretint;
    } else {
      text_physbackcol = text_backcol = 63 & colourmask;
    }
    set_rgb();
    vdu_cleartext();
  }
}

/*
** 'vdu_23command' emulates some of the VDU 23 command sequences
*/
static void vdu_23command(void) {
  int codeval, n;
  switch (vduqueue[0]) {	/* First byte in VDU queue gives the command type */
  case 0:       /* More cursor stuff - this only handles VDU23;{8202,29194};0;0;0; */
    if (vduqueue[1] == 10) {
      if (vduqueue[2] == 32) {
	hide_cursor();
        cursorstate = HIDDEN;	/* 0 = hide, 1 = show */
      } else if (vduqueue[2] == 114) {
        cursorstate = SUSPENDED;
	toggle_cursor();
        cursorstate = ONSCREEN;
      }
    }
    break;
  case 1:	/* Control the appear of the text cursor */
    if (vduqueue[1] == 0) {
      hide_cursor();
      cursorstate = HIDDEN;	/* 0 = hide, 1 = show */
    }
    if (vduqueue[1] == 1 && cursorstate != NOCURSOR) cursorstate = ONSCREEN;
    if (vduqueue[1] == 1) cursorstate = ONSCREEN;
    else cursorstate = HIDDEN;
    break;
  case 8:	/* Clear part of the text window */
    break;
  case 16:	/* Controls the movement of the cursor after printing */
    vdu_2316();
    break;
  case 17:	/* Set the tint value for a colour in 256 colour modes, etc */
    vdu_2317();
    break;
  case 18:	/* RISC OS 5 set Teletext characteristics */
    vdu_2318();
    break;
  case 22:	/* BB4W/BBCSDL Custom Mode */
    vdu_2322();
    break;
  default:
    codeval = vduqueue[0] & 0x00FF;
    if ((codeval < 32) || (codeval == 127)) break;   /* Ignore unhandled commands */
    /* codes 32 to 255 are user-defined character setup commands */
    for (n=0; n < 8; n++) sysfont[codeval-32][n] = vduqueue[n+1];
  }
}

void hide_cursor() {
  if (cursorstate == ONSCREEN) toggle_cursor();
}

static void reveal_cursor() {
  if (cursorstate==SUSPENDED) toggle_cursor();
}

/*
** 'toggle_cursor' draws the text cursor at the current text position
** in graphics modes.
** It draws (and removes) the cursor by inverting the colours of the
** pixels at the current text cursor position. Two different styles
** of cursor can be drawn, an underline and a block
*/
static void toggle_cursor(void) {
  int32 left, right, top, bottom, x, y, mxppc, myppc, xtemp;

  if (screenmode==7) {
    mxppc=M7XPPC;
    myppc=M7YPPC;
  } else {
    mxppc=XPPC;
    myppc=YPPC;
  }
  xtemp = xtext;
  if (xtemp > twinright) xtemp=twinright;
  if (displaybank != writebank) return;
  curstate instate=cursorstate;
  if ((cursorstate != SUSPENDED) && (cursorstate != ONSCREEN)) return;	/* Cursor is not being displayed so give up now */
  if (cursorstate == ONSCREEN)	/* Toggle the cursor state */
    cursorstate = SUSPENDED;
  else
    if (!vduflag(VDU_FLAG_GRAPHICURS)) cursorstate = ONSCREEN;
  left = xtemp*xscale*mxppc;	/* Calculate pixel coordinates of ends of cursor */
  right = left + xscale*mxppc -1;
  if (cursmode == UNDERLINE) {
    y = ((ytext+1)*yscale*myppc - yscale) * vscrwidth;
    for (x=left; x <= right; x++) {
      *((Uint32*)matrixflags.surface->pixels + x + y) ^= xor_mask;
      if (yscale != 1) *((Uint32*)matrixflags.surface->pixels + x + y + vscrwidth) ^= xor_mask;
      if ((screenmode == 3) || (screenmode==6) || (screenmode == 11) || (screenmode == 14) || (screenmode == 17)) {
        *((Uint32*)matrixflags.surface->pixels + x + y - vscrwidth) ^= xor_mask;
        *((Uint32*)matrixflags.surface->pixels + x + y - (vscrwidth*2)) ^= xor_mask;
        *((Uint32*)matrixflags.surface->pixels + x + y - (vscrwidth*3)) ^= xor_mask;
        *((Uint32*)matrixflags.surface->pixels + x + y - (vscrwidth*4)) ^= xor_mask;
      }
      if (screenmode ==7) *((Uint32*)matrixflags.surface->pixels + x + y - vscrwidth) ^= xor_mask;
    }
  }
  else if (cursmode == BLOCK) {
    top = ytext*yscale*myppc;
    bottom = top + myppc*yscale -1;
    for (y = top; y <= bottom; y++) {
      for (x = left; x <= right; x++)
        *((Uint32*)matrixflags.surface->pixels + x + y*vscrwidth) ^= xor_mask;
    }
  }
  if (instate != cursorstate) do_sdl_updaterect(matrixflags.surface, xtemp*xscale*mxppc, ytext*yscale*myppc, xscale*mxppc, yscale*myppc);
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
** and 'yscale'.
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
  if(!scaled) {
    scale_rect.x = left;
    scale_rect.y = top;
    scale_rect.w = (right+1 - left);
    scale_rect.h = (bottom+1 - top);
    SDL_BlitSurface(modescreen, &scale_rect, screenbank[writebank], &scale_rect);
    if ((autorefresh==1) && (displaybank == writebank)) SDL_BlitSurface(modescreen, &scale_rect, matrixflags.surface, &scale_rect);
  } else {
    dleft = left*xscale;			/* Calculate pixel coordinates in the */
    dtop  = top*yscale;			/* screen buffer of the rectangle */
    yy = dtop;
    for (j = top; j <= bottom; j++) {
      for (jj = 1; jj <= yscale; jj++) {
	xx = dleft;
	for (i = left; i <= right; i++) {
          for (ii = 1; ii <= xscale; ii++) {
            *((Uint32*)screenbank[writebank]->pixels + xx + yy*vscrwidth) = *((Uint32*)modescreen->pixels + i + j*vscrwidth);
            if ((autorefresh==1) && (displaybank == writebank)) {
	      *((Uint32*)matrixflags.surface->pixels + xx + yy*vscrwidth) = *((Uint32*)modescreen->pixels + i + j*vscrwidth);
            }
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
  }
  if ((screenmode == 3) || (screenmode == 6)) {
    int p;
    hide_cursor();
    scroll_rect.x=0;
    scroll_rect.w=screenwidth*xscale;
    scroll_rect.h=4;
    for (p=0; p<25; p++) {
      scroll_rect.y=16+(p*20);
      SDL_FillRect(matrixflags.surface, &scroll_rect, 0);
    }
  }
  if ((autorefresh==1) && (displaybank == writebank)) SDL_UpdateRect(matrixflags.surface, scale_rect.x, scale_rect.y, scale_rect.w, scale_rect.h);
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
  hardpalette[0] = hardpalette[1] = hardpalette[2] = 0;		    /* Black */
  hardpalette[3] = 255; hardpalette[4] = hardpalette[5] = 0;	    /* Red */
  hardpalette[6] = 0; hardpalette[7] = 255; hardpalette[8] = 0;	/* Green */
  hardpalette[9] = hardpalette[10] = 255; hardpalette[11] = 0;	/* Yellow */
  hardpalette[12] = hardpalette[13] = 0; hardpalette[14] = 255;	/* Blue */
  hardpalette[15] = 255; hardpalette[16] = 0; hardpalette[17] = 255;	/* Magenta */
  hardpalette[18] = 0; hardpalette[19] = hardpalette[20] = 255;	/* Cyan */
  hardpalette[21] = hardpalette[22] = hardpalette[23] = 255;	    /* White */
  switch (colourdepth) {
  case 2:	/* Two colour - Black and white only */
    palette[0] = palette[1] = palette[2] = 0;
    palette[3] = palette[4] = palette[5] = 255;
    break;
  case 4:	/* Four colour - Black, red, yellow and white */
    palette[0] =      palette[1]  =      palette[2]  = 0;	/* Black */
    palette[3] = 255; palette[4]  =      palette[5]  = 0;	/* Red */
    palette[6] =      palette[7]  = 255; palette[8]  = 0;	/* Yellow */
    palette[9] =      palette[10] =      palette[11] = 255;	/* White */
    break;
  case 8:	/* Eight colour */
    palette[0]  =      palette[1]  =      palette[2]  = 0;	/* Black */
    palette[3]  = 255; palette[4]  =      palette[5]  = 0;	/* Red */
    palette[6]  = 0;   palette[7]  = 255; palette[8]  = 0;	/* Green */
    palette[9]  =      palette[10] = 255; palette[11] = 0;	/* Yellow */
    palette[12] =      palette[13] = 0;   palette[14] = 255;	/* Blue */
    palette[15] = 255; palette[16] = 0;   palette[17] = 255;	/* Magenta */
    palette[18] = 0;   palette[19] =      palette[20] = 255;	/* Cyan */
    palette[21] =      palette[22] =      palette[23] = 255;	/* White */
    break;
  case 16:	/* Sixteen colour */
    palette[0]  =      palette[1]  =      palette[2]  = 0;	/* Black */
    palette[3]  = 255; palette[4]  =      palette[5]  = 0;	/* Red */
    palette[6]  = 0;   palette[7]  = 255; palette[8]  = 0;	/* Green */
    palette[9]  =      palette[10] = 255; palette[11] = 0;	/* Yellow */
    palette[12] =      palette[13] = 0;   palette[14] = 255;	/* Blue */
    palette[15] = 255; palette[16] = 0;   palette[17] = 255;	/* Magenta */
    palette[18] = 0;   palette[19] =      palette[20] = 255;	/* Cyan */
    palette[21] =      palette[22] =      palette[23] = 255;	/* White */
    palette[24] =      palette[25] =      palette[26] = 0;	/* Black */
    palette[27] = 160; palette[28] =      palette[29] = 0;	/* Dark red */
    palette[30] = 0;   palette[31] = 160; palette[32] = 0;	/* Dark green */
    palette[33] =      palette[34] = 160; palette[35] = 0;	/* Khaki */
    palette[36] =      palette[37] = 0;   palette[38] = 160;	/* Navy blue */
    palette[39] = 160; palette[40] = 0;   palette[41] = 160;	/* Purple */
    palette[42] = 0;   palette[43] =      palette[44] = 160;	/* Cyan */
    palette[45] =      palette[46] =      palette[47] = 160;	/* Grey */
    break;
  case 256:
  case COL24BIT: {	/* >= 256 colour */
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
            palette[colour+0] = red+tint;
            palette[colour+1] = green+tint;
            palette[colour+2] = blue+tint;
            colour += 3;
          }
        }
      }
    }
    break;
  }
  default:	/* 32K colour modes are not supported */
    error(ERR_UNSUPPORTED);
  }
  if (colourdepth >= 256) {
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
  palette[colour*3+0] = red;	/* The palette is not structured */
  palette[colour*3+1] = green;
  palette[colour*3+2] = blue;
}

/*
 * emulate_colourfn - This performs the function COLOUR(). It
 * Returns the entry in the palette for the current screen mode
 * that most closely matches the colour with red, green and
 * blue components passed to it.
 */
int32 emulate_colourfn(int32 red, int32 green, int32 blue) {
  int32 n, distance, test, best, dr, dg, db;

  if (colourdepth == COL24BIT) return (red + (green << 8) + (blue << 16));
  distance = 0x7fffffff;
  best = 0;
  for (n = 0; n < colourdepth && distance != 0; n++) {
    dr = palette[n * 3 + 0] - red;
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
  graph_fore_action = graph_back_action = 0;
  set_rgb();
}

/*
** 'scroll' scrolls the graphics screen up or down by the number of
** rows equivalent to one line of text on the screen. Depending on
** the RISC OS mode being used, this can be either eight or sixteen
** rows. 'direction' says whether the screen is moved up or down.
** The screen is redrawn by this call
*/
static void scroll(updown direction) {
  int left, right, top, dest, topwin, m, n, mxppc, myppc;
  if (screenmode == 7) {
    mxppc = M7XPPC;
    myppc = M7YPPC;
  } else {
    mxppc=XPPC;
    myppc=YPPC;
  }
  topwin = twintop*myppc;		/* Y coordinate of top of text window */
  if (direction == SCROLL_UP) {	/* Shifting screen up */
    dest = twintop*myppc;		/* Move screen up to this point */
    left = twinleft*mxppc;
    right = twinright*mxppc+mxppc-1;
    top = dest+myppc;				/* Top of block to move starts here */
    scroll_rect.x = twinleft*mxppc;
    scroll_rect.y = myppc * (twintop + 1);
    scroll_rect.w = mxppc * (twinright - twinleft +1);
    scroll_rect.h = myppc * (twinbottom - twintop);
    if (screenmode != 7) SDL_BlitSurface(modescreen, &scroll_rect, screen1, NULL);
    if (screenmode == 7 && vduflag(MODE7_UPDATE)) {
      SDL_BlitSurface(matrixflags.surface, &scroll_rect, screen1, NULL);
      SDL_BlitSurface(screen3, &scroll_rect, screen3A, NULL);
      SDL_BlitSurface(screen2, &scroll_rect, screen2A, NULL);
    }
    line_rect.x = 0;
    line_rect.y = myppc * (twinbottom - twintop);
    line_rect.w = mxppc * (twinright - twinleft +1);
    line_rect.h = myppc;
    if (screenmode != 7 || vduflag(MODE7_UPDATE)) SDL_FillRect(screen1, &line_rect, tb_colour);
    if (screenmode == 7) {
      if (vduflag(MODE7_UPDATE)) {
        SDL_FillRect(screen2A, &line_rect, tb_colour);
        SDL_FillRect(screen3A, &line_rect, tb_colour);
      }
      for(n=2; n<=25; n++) { 
	vdu141track[n-1]=vdu141track[n];
	mode7changed[n-1]=mode7changed[n];
      }
      vdu141track[25]=0;
      vdu141track[0]=0;
      /* Scroll the Mode 7 text buffer */
      for (m=twintop+1; m<=twinbottom; m++) {
	for (n=twinleft; n<=twinright; n++) mode7frame[m-1][n] = mode7frame[m][n];
      }
      /* Blank the bottom line */
      for (n=twinleft; n<=twinright; n++) mode7frame[twinbottom][n] = 32;
    }
  }
  else {	/* Shifting screen down */
    dest = (twintop+1)*myppc;
    left = twinleft*mxppc;
    right = (twinright+1)*mxppc-1;
    top = twintop*myppc;
    scroll_rect.x = left;
    scroll_rect.y = top;
    scroll_rect.w = mxppc * (twinright - twinleft +1);
    scroll_rect.h = myppc * (twinbottom - twintop);
    line_rect.x = 0;
    line_rect.y = myppc;
    if (screenmode != 7) SDL_BlitSurface(modescreen, &scroll_rect, screen1, &line_rect);
    if (screenmode == 7 && vduflag(MODE7_UPDATE)) {
      SDL_BlitSurface(matrixflags.surface, &scroll_rect, screen1, &line_rect);
      SDL_BlitSurface(screen3, &scroll_rect, screen3A, NULL);
      SDL_BlitSurface(screen2, &scroll_rect, screen2A, NULL);
    }
    line_rect.x = 0;
    line_rect.y = 0;
    line_rect.w = mxppc * (twinright - twinleft +1);
    line_rect.h = myppc;
    if (screenmode != 7 || vduflag(MODE7_UPDATE)) SDL_FillRect(screen1, &line_rect, tb_colour);
    if (screenmode == 7) {
      if (vduflag(MODE7_UPDATE)) {
        SDL_FillRect(screen2A, &line_rect, tb_colour);
        SDL_FillRect(screen3A, &line_rect, tb_colour);
      }
      for(n=0; n<=24; n++) {
	vdu141track[n+1]=vdu141track[n];
	mode7changed[n+1]=mode7changed[n];
      }
      vdu141track[0]=0; vdu141track[1]=0;
      /* Scroll the Mode 7 text buffer */
      for (m=twintop; m<=twinbottom-1; m++) {
	for (n=twinleft; n<=twinright; n++) mode7frame[m+1][n] = mode7frame[m][n];
      }
      /* Blank the bottom line */
      for (n=twinleft; n<=twinright; n++) mode7frame[twintop][n] = 32;
    }
  }
  line_rect.x = 0;
  line_rect.y = 0;
  line_rect.w = mxppc * (twinright - twinleft +1);
  line_rect.h = myppc * (twinbottom - twintop +1);
  scroll_rect.x = left;
  scroll_rect.y = dest;
  if (screenmode != 7) SDL_BlitSurface(screen1, &line_rect, modescreen, &scroll_rect);
  if (screenmode == 7 && vduflag(MODE7_UPDATE)) {
    SDL_BlitSurface(screen2A, &line_rect, screen2, &scroll_rect);
    SDL_BlitSurface(screen3A, &line_rect, screen3, &scroll_rect);
  }
  if (screenmode == 7) {
    if (vduflag(MODE7_UPDATE)) SDL_BlitSurface(screen1, &line_rect, matrixflags.surface, &scroll_rect);
  } else {
    blit_scaled(left, topwin, right, twinbottom*myppc+myppc-1);
  }
  do_sdl_flip(matrixflags.surface);
}

/*
** 'echo_text' is called to display text held in the screen buffer on the
** graphics screen when working in 'no echo' mode. If displays from the
** start of the line to the current value of the text cursor
*/
static void echo_text(void) {
  if (xtext == 0) return;	/* Return if nothing has changed */
  if (screenmode == 7) {
    do_sdl_flip(matrixflags.surface);
    return;
  }
    blit_scaled(0, ytext*YPPC, xtext*XPPC-1, ytext*YPPC+YPPC-1);
}

unsigned long m7updatetimer=0;
void mode7flipbank() {
  int64 mytime;
  int32 ypos;
  
  if (screenmode == 7) {
    mytime=basicvars.centiseconds;
    if (vduflag(MODE7_UPDATE) && ((mytime-m7updatetimer) > 2)) {
      for (ypos=0; ypos<=24; ypos++) if (mode7changed[ypos]) mode7renderline(ypos);
      do_sdl_updaterect(matrixflags.surface, 0, 0, 0, 0);
      m7updatetimer=mytime;
    }
    if ((mode7timer - mytime) <= 0) {
      hide_cursor();
      if (!vduflag(MODE7_UPDATE)) mode7renderscreen();
      if (vduflag(MODE7_BANK)) {
	SDL_BlitSurface(screen2, NULL, matrixflags.surface, NULL);
	write_vduflag(MODE7_BANK,0);
	mode7timer=mytime + 100;
      } else {
	SDL_BlitSurface(screen3, NULL, matrixflags.surface, NULL);
	write_vduflag(MODE7_BANK,1);
	mode7timer=mytime + 33;
      }
      reveal_cursor();
    }
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
  
  if (cursorstate == ONSCREEN) toggle_cursor();
  if ((vdu2316byte & 1) && ((xtext > twinright) || (xtext < twinleft))) {  /* Scroll before character if scroll protect enabled */
    if (!vduflag(VDU_FLAG_ECHO)) echo_text();	/* Line is full so flush buffered characters */
    xtext = textxhome();
    ytext+=textyinc();
    /* VDU14 check here */
    if (vduflag(VDU_FLAG_ENAPAGE)) {
      vdu14lines++;
      if (vdu14lines > (twinbottom-twintop)) {
#ifdef NEWKBD
	while (kbd_modkeys(1)==0 && kbd_escpoll()==0) {
//	  if (basicvars.escape_enabled) checkforescape();
	  usleep(5000);
	}
#else
	while (!emulate_inkey(-4) && !emulate_inkey2(-7)) {
	  if (basicvars.escape_enabled) checkforescape();
	  usleep(5000);
	}
#endif
	vdu14lines=0;
      }
    }
    if (ytext > twinbottom) {	/* Text cursor was on the last line of the text window */
      if (vdu2316byte & 16) {
	ytext=textyhome();
      } else {
	scroll(SCROLL_UP);	/* So scroll window up */
	ytext--;
      }
    }
    if (ytext < twintop) {	/* Text cursor was on the first line of the text window */
      if (vdu2316byte & 16) {
	ytext=textyedge();
      } else {
	scroll(SCROLL_DOWN);	/* So scroll window down */
	ytext++;
      }
    }
  }
  topx = xtext*XPPC;
  topy = ytext*YPPC;
  place_rect.x = topx;
  place_rect.y = topy;
  SDL_FillRect(sdl_fontbuf, NULL, tb_colour);
  for (y=0; y < 8; y++) {
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
  if (vduflag(VDU_FLAG_ECHO) || (vdu2316byte & 0xFE)) {
    blit_scaled(topx, topy, topx+XPPC-1, topy+YPPC-1);
  }
  xtext+=textxinc();
  if ((!(vdu2316byte & 1)) && ((xtext > twinright) || (xtext < twinleft))) {  /* Scroll before character if scroll protect enabled */
    if (!vduflag(VDU_FLAG_ECHO)) echo_text();	/* Line is full so flush buffered characters */
    xtext = textxhome();
    ytext+=textyinc();
    /* VDU14 check here */
    if (vduflag(VDU_FLAG_ENAPAGE)) {
      vdu14lines++;
      if (vdu14lines > (twinbottom-twintop)) {
#ifdef NEWKBD
	while (kbd_modkeys(1)==0 && kbd_escpoll()==0) {
//	  if (basicvars.escape_enabled) checkforescape();
	  usleep(5000);
	}
#else
	while (!emulate_inkey(-4) && !emulate_inkey2(-7)) {
	  if (basicvars.escape_enabled) checkforescape();
	  usleep(5000);
	}
#endif
	vdu14lines=0;
      }
    }
    if (ytext > twinbottom) {	/* Text cursor was on the last line of the text window */
      if (vdu2316byte & 16) {
	ytext=textyhome();
      } else {
	scroll(SCROLL_UP);	/* So scroll window up */
	ytext--;
      }
    }
    if (ytext < twintop) {	/* Text cursor was on the first line of the text window */
      if (vdu2316byte & 16) {
	ytext=textyedge();
      } else {
	scroll(SCROLL_DOWN);	/* So scroll window down */
	ytext++;
      }
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
  SDL_Rect clip_rect;
  if (clipping) {
    clip_rect.x = GXTOPX(gwinleft);
    clip_rect.y = GYTOPY(gwintop);
    clip_rect.w = gwinright - gwinleft +1;
    clip_rect.h = gwinbottom - gwintop +1;
    SDL_SetClipRect(modescreen, &clip_rect);
  }
  topx = GXTOPX(xlast);		/* X and Y coordinates are those of the */
  topy = GYTOPY(ylast);	/* top left-hand corner of the character */
  place_rect.x = topx;
  place_rect.y = topy;
  for (y=0; y<YPPC; y++) {
    if ((topy+y) >= modetable[screenmode].yres) break;
    line = sysfont[ch-' '][y];
    if (line!=0) {
      if (line & 0x80) plot_pixel(modescreen, (topx + 0 + (topy+y)*vscrwidth), gf_colour, graph_fore_action);
      if (line & 0x40) plot_pixel(modescreen, (topx + 1 + (topy+y)*vscrwidth), gf_colour, graph_fore_action);
      if (line & 0x20) plot_pixel(modescreen, (topx + 2 + (topy+y)*vscrwidth), gf_colour, graph_fore_action);
      if (line & 0x10) plot_pixel(modescreen, (topx + 3 + (topy+y)*vscrwidth), gf_colour, graph_fore_action);
      if (line & 0x08) plot_pixel(modescreen, (topx + 4 + (topy+y)*vscrwidth), gf_colour, graph_fore_action);
      if (line & 0x04) plot_pixel(modescreen, (topx + 5 + (topy+y)*vscrwidth), gf_colour, graph_fore_action);
      if (line & 0x02) plot_pixel(modescreen, (topx + 6 + (topy+y)*vscrwidth), gf_colour, graph_fore_action);
      if (line & 0x01) plot_pixel(modescreen, (topx + 7 + (topy+y)*vscrwidth), gf_colour, graph_fore_action);
    }
  }
  blit_scaled(topx, topy, topx+XPPC-1, topy+YPPC-1);

  cursorstate = SUSPENDED; /* because we just overwrote it */
  xlast += XPPC*xgupp;	/* Move to next character position in X direction */
  if ((xlast > gwinright) && (!(vdu2316byte & 64))) {	/* But position is outside the graphics window */
    xlast = gwinleft;
    ylast -= YPPC*ygupp;
    if (ylast < gwinbottom) ylast = gwintop;	/* Below bottom of graphics window - Wrap around to top */
  }
  if (clipping) {
    SDL_SetClipRect(modescreen, NULL);
  }
}

static void plot_space_opaque(void) {
  int32 topx, topy;
  topx = GXTOPX(xlast);		/* X and Y coordinates are those of the */
  topy = GYTOPY(ylast);	/* top left-hand corner of the character */
  place_rect.x = topx;
  place_rect.y = topy;
  SDL_FillRect(sdl_fontbuf, NULL, gb_colour);
  SDL_BlitSurface(sdl_fontbuf, &font_rect, modescreen, &place_rect);
  blit_scaled(topx, topy, topx+XPPC-1, topy+YPPC-1);

  cursorstate = SUSPENDED; /* because we just overwrote it */
  xlast += XPPC*xgupp;	/* Move to next character position in X direction */
  if ((xlast > gwinright) && (!(vdu2316byte & 64))) {	/* But position is outside the graphics window */
    xlast = gwinleft;
    ylast -= YPPC*ygupp;
    if (ylast < gwinbottom) ylast = gwintop;	/* Below bottom of graphics window - Wrap around to top */
  }
}

/*
** 'echo_on' turns on cursor and the immediate echo of characters to the screen
*/
void echo_on(void) {
  write_vduflag(VDU_FLAG_ECHO,1);
  echo_text();		/* Flush what is in the graphics buffer */
  reveal_cursor();	/* Display cursor again */
}

/*
** 'echo_off' turns off the cursor and the immediate echo of characters
** to the screen. This is used to make character output more efficient
*/
void echo_off(void) {
  write_vduflag(VDU_FLAG_ECHO,0);
  hide_cursor();	/* Remove the cursor if it is being displayed */
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
  hide_cursor();	/* Remove cursor */
  xtext = column;
  ytext = row;
  reveal_cursor();	/* Redraw cursor */
}

/*
** 'set_cursor' sets the type of the text cursor used on the graphic
** screen to either a block or an underline. 'underline' is set to
** TRUE if an underline is required. Underline is used by the program
** when keyboard input is in 'insert' mode and a block when it is in
** 'overwrite'.
*/
void set_cursor(boolean underline) {
  hide_cursor();	/* Remove old style cursor */
  cursmode = underline ? UNDERLINE : BLOCK;
  reveal_cursor();	/* Draw new style cursor */
}

/*
** 'vdu_setpalette' changes one of the logical to physical colour map
** entries (VDU 19). When the interpreter is in full screen mode it
** can also redefine colours for in the palette.
*/
static void vdu_setpalette(void) {
  int32 logcol, pmode, mode, offset, c, newcol;
  if (screenmode == 7) return;
  logcol = vduqueue[0] & colourmask;
  mode = vduqueue[1];
  pmode = mode % 16;
  if (mode < 16 && colourdepth <= 16) {	/* Just change the RISC OS logical to physical colour mapping */
    logtophys[logcol] = mode;
    palette[logcol*3+0] = hardpalette[pmode*3+0];
    palette[logcol*3+1] = hardpalette[pmode*3+1];
    palette[logcol*3+2] = hardpalette[pmode*3+2];
  } else if (mode == 16)	/* Change the palette entry for colour 'logcol' */
    change_palette(logcol, vduqueue[2], vduqueue[3], vduqueue[4]);
  set_rgb();
  /* Now, go through the framebuffer and change the pixels */
  if (colourdepth <= 256) {
    c = logcol * 3;
    newcol = SDL_MapRGB(sdl_fontbuf->format, palette[c+0], palette[c+1], palette[c+2]) + (logcol << 24);
    for (offset=0; offset < (screenheight*screenwidth*xscale); offset++) {
      if ((*((Uint32*)modescreen->pixels + offset) >> 24) == logcol) *((Uint32*)modescreen->pixels + offset) = newcol;
    }
    blit_scaled(0,0,screenwidth-1,screenheight-1);
  }
}

/*
** 'move_down' moves the text cursor down a line within the text
** window, scrolling the window up if the cursor is on the last line
** of the window.
*/
static void move_down(void) {
  ytext+=textyinc();
  if (ytext > twinbottom) {	/* Cursor was on last line in window - Scroll window up */
    if (vdu2316byte & 16) {
      ytext=textyhome();
    } else {
      scroll(SCROLL_UP);	/* So scroll window up */
      ytext--;
    }
  }
  if (ytext < twintop) {	/* Cursor was on top line in window - Scroll window down */
    if (vdu2316byte & 16) {
      ytext=textyedge();
    } else {
      scroll(SCROLL_DOWN);	/* So scroll window down */
      ytext++;
    }
  }
}

/*
** 'move_up' moves the text cursor up a line within the text window,
** scrolling the window down if the cursor is on the top line of the
** window
*/
static void move_up(void) {
  ytext-=textyinc();
  if (ytext < twintop) {	/* Cursor was on top line in window - Scroll window down */
    if (vdu2316byte & 16) {
      ytext=textyedge();
    } else {
      scroll(SCROLL_DOWN);	/* So scroll window down */
      ytext++;
    }
  }
  if (ytext > twinbottom) {	/* Cursor was on last line in window - Scroll window up */
    if (vdu2316byte & 16) {
      ytext=textyhome();
    } else {
      scroll(SCROLL_UP);	/* So scroll window up */
      ytext--;
    }
  }
}

/*
** 'move_curback' moves the cursor back one character on the screen (VDU 8)
*/
static void move_curback(void) {
  if (vduflag(VDU_FLAG_GRAPHICURS)) {	/* VDU 5 mode - Move graphics cursor back one character */
    xlast -= XPPC*xgupp;
    if (xlast < gwinleft) {		/* Cursor is outside the graphics window */
      xlast = gwinright-XPPC*xgupp+1;	/* Move back to right edge of previous line */
      ylast += YPPC*ygupp;
      if (ylast > gwintop) {		/* Move above top of window */
        ylast = gwinbottom+YPPC*ygupp-1;	/* Wrap around to bottom of window */
      }
    }
  } else {
    hide_cursor();	/* Remove cursor */
    xtext-=textxinc();
    if ((xtext < twinleft) || (xtext > twinright)) {	/* Cursor is at left-hand edge of text window so move up a line */
      xtext = textxedge();
      move_up();
    }
    reveal_cursor();	/* Redraw cursor */
  }
}

/*
** 'move_curforward' moves the cursor forwards one character on the screen (VDU 9)
*/
static void move_curforward(void) {
  if (vduflag(VDU_FLAG_GRAPHICURS)) {	/* VDU 5 mode - Move graphics cursor back one character */
    xlast += XPPC*xgupp;
    if (xlast > gwinright) {	/* Cursor is outside the graphics window */
      xlast = gwinleft;		/* Move to left side of window on next line */
      ylast -= YPPC*ygupp;
      if (ylast < gwinbottom) ylast = gwintop;	/* Moved below bottom of window - Wrap around to top */
    }
  }
  hide_cursor();	/* Remove cursor */
  xtext+=textxinc();
  if ((xtext < twinleft) || (xtext > twinright)) {	/* Cursor is at right-hand edge of text window so move down a line */
    xtext = textxhome();
    move_down();
  }
  reveal_cursor();	/* Redraw cursor */
}

/*
** 'move_curdown' moves the cursor down the screen, that is, it
** performs the linefeed operation (VDU 10)
*/
static void move_curdown(void) {
  if (vduflag(VDU_FLAG_GRAPHICURS)) {
    ylast -= YPPC*ygupp;
    if (ylast < gwinbottom) ylast = gwintop;	/* Moved below bottom of window - Wrap around to top */
  } else {
    /* VDU14 check here - all these should be optimisable */
    if (vduflag(VDU_FLAG_ENAPAGE)) {
      vdu14lines++;
      if (vdu14lines > (twinbottom-twintop)) {
#ifdef NEWKBD
	while (kbd_modkeys(1)==0 && kbd_escpoll()==0) {
//	  if (basicvars.escape_enabled) checkforescape();
	  usleep(5000);
	}
#else
	while (!emulate_inkey(-4) && !emulate_inkey2(-7)) {
	  if (basicvars.escape_enabled) checkforescape();
	  usleep(5000);
	}
#endif
	vdu14lines=0;
      }
    }
    hide_cursor();	/* Remove cursor */
    move_down();
    reveal_cursor();	/* Redraw cursor */
  }
}

/*
** 'move_curup' moves the cursor up a line on the screen (VDU 11)
*/
static void move_curup(void) {
  if (vduflag(VDU_FLAG_GRAPHICURS)) {
    ylast += YPPC*ygupp;
    if (ylast > gwintop) ylast = gwinbottom+YPPC*ygupp-1;	/* Move above top of window - Wrap around to bottow */
  } else {
    /* VDU14 check here */
    if (vduflag(VDU_FLAG_ENAPAGE)) {
      vdu14lines++;
// BUG: paged mode should not stop scrolling upwards
      if (vdu14lines > (twinbottom-twintop)) {
#ifdef NEWKBD
	while (kbd_modkeys(1)==0 && kbd_escpoll()==0) {
//	  if (basicvars.escape_enabled) checkforescape();
	  usleep(5000);
	}
#else
	while (!emulate_inkey(-4) && !emulate_inkey2(-7)) {
	  if (basicvars.escape_enabled) checkforescape();
	  usleep(5000);
	}
#endif
	vdu14lines=0;
      }
    }
    hide_cursor();	/* Remove cursor */
    move_up();
    reveal_cursor();	/* Redraw cursor */
  }
}

/*
** 'vdu_cleartext' clears the text window. Normally this is the
** entire screen (VDU 12).
*/
static void vdu_cleartext(void) {
  int32 left, right, top, bottom, mxppc, myppc, lx, ly;
  if (screenmode == 7) {
    mxppc=M7XPPC;
    myppc=M7YPPC;
  } else {
    mxppc=XPPC;
    myppc=YPPC;
  }
  hide_cursor();	/* Remove cursor if it is being displayed */
  if (vduflag(VDU_FLAG_TEXTWIN)) {	/* Text window defined that does not occupy the whole screen */
    if (screenmode == 7) {
      for (ly=twintop; ly <= twinbottom; ly++) {
	for (lx=twinleft; lx <=twinright; lx++) {
	  mode7frame[ly][lx]=32;
	}
      }
    }
    left = twinleft*mxppc;
    right = twinright*mxppc+mxppc-1;
    top = twintop*myppc;
    bottom = twinbottom*myppc+myppc-1;
    line_rect.x = left;
    line_rect.y = top;
    line_rect.w = right - left +1;
    line_rect.h = bottom - top +1;
    SDL_FillRect(modescreen, &line_rect, tb_colour);
    SDL_FillRect(screen2, &line_rect, tb_colour);
    SDL_FillRect(screen3, &line_rect, tb_colour);
    blit_scaled(0,0,screenwidth-1,screenheight-1);
    mode7renderscreen();
  }
  else {	/* Text window is not being used */
    reset_mode7();
    left = twinleft*mxppc;
    right = twinright*mxppc+mxppc-1;
    top = twintop*myppc;
    bottom = twinbottom*myppc+myppc-1;
    SDL_FillRect(modescreen, NULL, tb_colour);
    // blit_scaled(left, top, right, bottom);
    blit_scaled(0, 0, screenwidth-1, screenheight-1);
    SDL_FillRect(screen2, NULL, tb_colour);
    SDL_FillRect(screen3, NULL, tb_colour);
    xtext = textxhome();
    ytext = textyhome();
    reveal_cursor();	/* Redraw cursor */
  }
  do_sdl_flip(matrixflags.surface);
}

/*
** 'vdu_return' deals with the carriage return character (VDU 13)
*/
static void vdu_return(void) {
  if (vduflag(VDU_FLAG_GRAPHICURS)) {
    xlast = gwinleft;
  } else {
    hide_cursor();	/* Remove cursor */
    xtext = textxhome();
    reveal_cursor();	/* Redraw cursor */
    if (screenmode == 7) {
      write_vduflag(MODE7_VDU141ON,0);
      write_vduflag(MODE7_HIGHBIT,0);
      write_vduflag(MODE7_FLASH,0);
      write_vduflag(MODE7_SEPGRP,0);
      write_vduflag(MODE7_SEPREAL,0);
      mode7prevchar=32;
      text_physforecol = text_forecol = 7;
      text_physbackcol = text_backcol = 0;
      set_rgb();
      mode7flipbank();
    }
  }
}

static void fill_rectangle(Uint32 left, Uint32 top, Uint32 right, Uint32 bottom, Uint32 colour, Uint32 action) {
  Uint32 xloop, yloop, pxoffset, prevcolour, a, altcolour = 0;
  int32 rox = 0, roy = 0;

  colour=emulate_colourfn((colour >> 16) & 0xFF, (colour >> 8) & 0xFF, (colour & 0xFF));
  for (yloop=top;yloop<=bottom; yloop++) {
    if (clipping) {
      roy=modetable[screenmode].ygraphunits - ((yloop+1) * modetable[screenmode].yscale * 2);
      if ((roy < gwinbottom) || (roy > gwintop)) continue;
    }
    for (xloop=left; xloop<=right; xloop++) {
      if (clipping) {
        rox=xloop * modetable[screenmode].xscale * 2;
        if ((rox < gwinleft) || (rox > gwinright)) continue;
      }
      pxoffset = xloop + yloop*vscrwidth;
      prevcolour=*((Uint32*)modescreen->pixels + pxoffset);
      prevcolour=emulate_colourfn((prevcolour >> 16) & 0xFF, (prevcolour >> 8) & 0xFF, (prevcolour & 0xFF));
      if (colourdepth == 256) prevcolour = prevcolour >> COL256SHIFT;
      switch (action) {
	case 0:
	  altcolour=colour;
	  break;
	case 1:
	  altcolour=(prevcolour | colour);
	  break;
	case 2:
	  altcolour=(prevcolour & colour);
	  break;
	case 3:
	  altcolour=(prevcolour ^ colour);
	  break;
	case 4:
	  altcolour=(prevcolour ^ (colourdepth-1));
	  break;
	default:
	  altcolour=colour;
      }
      if (colourdepth == COL24BIT) {
        altcolour = altcolour & 0xFFFFFF;
      } else {
        a=altcolour;
	altcolour=altcolour*3;
        altcolour=SDL_MapRGB(sdl_fontbuf->format, palette[altcolour+0], palette[altcolour+1], palette[altcolour+2]) + (a << 24);
      }
      *((Uint32*)modescreen->pixels + pxoffset) = altcolour;
    }
  }

}

/*
** 'vdu_cleargraph' set the entire graphics window to the current graphics
** background colour (VDU 16)
*/
static void vdu_cleargraph(void) {
  if (istextonly()) return;
  hide_cursor();	/* Remove cursor */
  if (graph_back_action == 0 && !clipping) {
    SDL_FillRect(modescreen, NULL, gb_colour);
  } else {
    fill_rectangle(GXTOPX(gwinleft), GYTOPY(gwintop), GXTOPX(gwinright), GYTOPY(gwinbottom), gb_colour, graph_back_action);
  }
  blit_scaled(GXTOPX(gwinleft), GYTOPY(gwintop), GXTOPX(gwinright), GYTOPY(gwinbottom));
  if (!vduflag(VDU_FLAG_GRAPHICURS)) reveal_cursor();	/* Redraw cursor */
  do_sdl_flip(matrixflags.surface);
}

/*
** 'vdu_textcol' changes the text colour to the value in the VDU queue
** (VDU 17). It handles both foreground and background colours at any
** colour depth. The RISC OS physical colour number is mapped to the
** equivalent as used by conio
*/
static void vdu_textcol(void) {
  int32 colnumber;
  if (screenmode == 7) return;
  colnumber = vduqueue[0];
  if (colnumber < 128) {	/* Setting foreground colour */
    if (colourdepth == 256) {
      text_forecol = colnumber & COL256MASK;
      text_physforecol = (text_forecol << COL256SHIFT)+text_foretint;
    } else if (colourdepth == COL24BIT) {
      text_physforecol = text_forecol = colour24bit(colnumber, text_foretint);
    } else {
      text_physforecol = text_forecol = colnumber & colourmask;
    }
  }
  else {	/* Setting background colour */
    if (colourdepth == 256) {
      text_backcol = colnumber & COL256MASK;
      text_physbackcol = (text_backcol << COL256SHIFT)+text_backtint;
    } else if (colourdepth == COL24BIT) {
      text_physbackcol = text_backcol = colour24bit(colnumber, text_backtint);
    } else {	/* Operating in text mode */
      text_physbackcol = text_backcol = colnumber & colourmask;
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
    logtophys[7] = VDU_WHITE;
    logtophys[8] = FLASH_BLAWHITE;
    logtophys[9] = FLASH_REDCYAN;
    logtophys[10] = FLASH_GREENMAG;
    logtophys[11] = FLASH_YELBLUE;
    logtophys[12] = FLASH_BLUEYEL;
    logtophys[13] = FLASH_MAGREEN;
    logtophys[14] = FLASH_CYANRED;
    logtophys[15] = FLASH_WHITEBLA;
    text_forecol = graph_forecol = 7;
    break;
  case 256:
    text_forecol = graph_forecol = 63;
    graph_foretint = text_foretint = MAXTINT;
    graph_backtint = text_backtint = 0;
    break;
  case COL24BIT:
    text_forecol = graph_forecol = 0xFFFFFF;
    graph_foretint = text_foretint = MAXTINT;
    graph_backtint = text_backtint = 0;
    break;
  default:
    error(ERR_UNSUPPORTED); /* 32K colour modes not supported */
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
*/
static void vdu_graphcol(void) {
  int32 colnumber;
  colnumber = vduqueue[1];
  if (colnumber < 128) {	/* Setting foreground graphics colour */
      graph_fore_action = vduqueue[0];
      if (colourdepth == 256) {
        graph_forecol = colnumber & COL256MASK;
        graph_physforecol = (graph_forecol<<COL256SHIFT)+graph_foretint;
      } else if (colourdepth == COL24BIT) {
        graph_physforecol = graph_forecol = colour24bit(colnumber, graph_foretint);
      } else {
        graph_physforecol = graph_forecol = colnumber & colourmask;
      }
  }
  else {	/* Setting background graphics colour */
    graph_back_action = vduqueue[0];
    if (colourdepth == 256) {
      graph_backcol = colnumber & COL256MASK;
      graph_physbackcol = (graph_backcol<<COL256SHIFT)+graph_backtint;
    } else if (colourdepth == COL24BIT) {
      graph_physbackcol = graph_backcol = colour24bit(colnumber, graph_backtint);
    } else {	/* Operating in text mode */
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
  clipping = TRUE;
}

/*
** 'vdu_plot' handles the VDU 25 graphics sequences
*/
static void vdu_plot(void) {
  int32 x, y;
  x = vduqueue[1]+vduqueue[2]*256;
  if (x > 0x7FFF) x = -(0x10000-x);	/* X is negative */
  y = vduqueue[3]+vduqueue[4]*256;
  if (y > 0x7FFF) y = -(0x10000-y);	/* Y is negative */
  emulate_plot(vduqueue[0], x, y);	/* vduqueue[0] gives the plot code */
}

/*
** 'vdu_restwind' restores the default (full screen) text and
** graphics windows (VDU 26)
*/
static void vdu_restwind(void) {
  clipping = FALSE;
  write_vduflag(MODE7_HIGHBIT,0);
  xorigin = yorigin = 0;
  xlast = ylast = xlast2 = ylast2 = 0;
  gwinleft = 0;
  gwinright = xgraphunits-1;
  gwintop = ygraphunits-1;
  gwinbottom = 0;
  hide_cursor();	/* Remove cursor */
  xtext = ytext = 0;
  reveal_cursor();	/* Redraw cursor */
  write_vduflag(VDU_FLAG_TEXTWIN,0);
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
  write_vduflag(MODE7_HIGHBIT,0);
  left = vduqueue[0];
  bottom = vduqueue[1];
  right = vduqueue[2];
  top = vduqueue[3];
  if ((left >= textwidth) || (right >= textwidth) || (top >= textheight) || (bottom >= textheight)) return; /* Ignore bad parameters */
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
  twinleft = left;
  twinright = right;
  twintop = top;
  twinbottom = bottom;
/* Set flag to say if text window occupies only a part of the screen */
  write_vduflag(VDU_FLAG_TEXTWIN,(left > 0 || right < textwidth-1 || top > 0 || bottom < textheight-1));
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
  if (vduflag(VDU_FLAG_GRAPHICURS)) {	/* Send graphics cursor to top left-hand corner of graphics window */
    xlast = gwinleft;
    ylast = gwintop;
  }
  else {	/* Send text cursor to the top left-hand corner of the text window */
    move_cursor(textxhome(), textyhome());
  }
}

/*
** 'vdu_movetext' moves the text cursor to the given column and row in
** the text window (VDU 31)
*/
static void vdu_movetext(void) {
  int32 column, row;
  if (vduflag(VDU_FLAG_GRAPHICURS)) {	/* Text is going to the graphics cursor */
    xlast = gwinleft+vduqueue[0]*XPPC*xgupp;
    ylast = gwintop-vduqueue[1]*YPPC*ygupp+1;
  }
  else {	/* Text is going to the graphics cursor */
    column = vduqueue[0] + twinleft;
    row = vduqueue[1] + twintop;
    if (column > twinright || row > twinbottom) return;	/* Ignore command if values are out of range */
    move_cursor(column, row);
  }
  if (screenmode == 7) {
    write_vduflag(MODE7_VDU141ON,0);
    write_vduflag(MODE7_HIGHBIT,0);
    write_vduflag(MODE7_SEPGRP,0);
    write_vduflag(MODE7_CONCEAL,0);
    write_vduflag(MODE7_HOLD,0);
    write_vduflag(MODE7_FLASH,0);
    text_physforecol = text_forecol = 7;
    text_physbackcol = text_backcol = 0;
    set_rgb();
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
  if (matrixflags.dospool) fprintf(matrixflags.dospool, "%c", charvalue);
  if (vduneeded == 0) {			/* VDU queue is empty */
    if (vduflag(VDU_FLAG_DISABLE)) {
      if (charvalue == VDU_ENABLE) write_vduflag(VDU_FLAG_DISABLE,0);
      return;
    }
    if (charvalue >= ' ') {		/* Most common case - print something */
      /* Handle Mode 7 */
      if (screenmode == 7) {
	if ((vdu2316byte & 1) && ((xtext > twinright) || (xtext < twinleft))) { /* Have reached edge of text window. Skip to next line  */
	  xtext = textxhome();
	  ytext+=textyinc();
	  /* VDU14 check here */
	  if (vduflag(VDU_FLAG_ENAPAGE)) {
	    vdu14lines++;
	    if (vdu14lines > (twinbottom-twintop)) {
#ifdef NEWKBD
	while (kbd_modkeys(1)==0 && kbd_escpoll()==0) {
//	  if (basicvars.escape_enabled) checkforescape();
	  usleep(5000);
	}
#else
	while (!emulate_inkey(-4) && !emulate_inkey2(-7)) {
	  if (basicvars.escape_enabled) checkforescape();
	  usleep(5000);
	}
#endif
	      vdu14lines=0;
	    }
	  }
	  if (ytext > twinbottom) {
	    if (vdu2316byte & 16) {
	      ytext=textyhome();
	    } else {
	      ytext--;
	      scroll(SCROLL_UP);
	    }
	    mode7renderline(ytext);
	  }
	  if (ytext < twintop) {
	    if (vdu2316byte & 16) {
	      ytext=textyedge();
	    } else {
	      ytext++;
	      scroll(SCROLL_DOWN);
	    }
	    mode7renderline(ytext);
	  }
	}
	if (charvalue == 127) {
	  move_curback();
	  mode7frame[ytext][xtext]=32;
	  move_curback();
	} else {
	  mode7frame[ytext][xtext]=charvalue;
	}
	mode7changed[ytext]=1;
	if (vduflag(MODE7_UPDATE_HIGHACC)) mode7renderline(ytext);
	xtext+=textxinc();
	if ((!(vdu2316byte & 1)) && ((xtext > twinright) || (xtext < twinleft))) {
	  xtext = textxhome();
	  ytext+=textyinc();
	  /* VDU14 check here */
	  if (vduflag(VDU_FLAG_ENAPAGE)) {
	    vdu14lines++;
	    if (vdu14lines > (twinbottom-twintop)) {
#ifdef NEWKBD
	while (kbd_modkeys(1)==0 && kbd_escpoll()==0) {
//	  if (basicvars.escape_enabled) checkforescape();
	  usleep(5000);
	}
#else
	while (!emulate_inkey(-4) && !emulate_inkey2(-7)) {
	  if (basicvars.escape_enabled) checkforescape();
	  usleep(5000);
	}
#endif
	      vdu14lines=0;
	    }
	  }
	  if (ytext > twinbottom) {
	    ytext--;
	    scroll(SCROLL_UP);
	    mode7renderline(ytext);
	  }
	  if (ytext < twintop) {
	    ytext++;
	    scroll(SCROLL_DOWN);
	    mode7renderline(ytext);
	  }
	}
	return; /* End of MODE 7 block */
      } else {
	if (vduflag(VDU_FLAG_GRAPHICURS)) {			    /* Sending text output to graphics cursor */
	  if (charvalue == 127) {
	    move_curback();
	    plot_space_opaque();
	    move_curback();
	  } else {
	    plot_char(charvalue);
	  }
	} else {
	  if (charvalue == 127) {
	    move_curback();
	    write_char(32);
	    move_curback();
	  } else {
	    write_char(charvalue);
	    reveal_cursor();	/* Redraw the cursor */
	  }
	}
      }
      return;
    }
    else {	/* Control character - Found start of new VDU command */
      if (!vduflag(VDU_FLAG_ECHO)) echo_text();
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
    break;
  case VDU_ENAPRINT: 	/* 2 - Enable the sending of characters to the printer */
    write_vduflag(VDU_FLAG_ENAPRINT,1);
    break;
  case VDU_DISPRINT:	/* 3 - Disable the sending of characters to the printer */
    write_vduflag(VDU_FLAG_ENAPRINT,0);
    break;
  case VDU_TEXTCURS:	/* 4 - Print text at text cursor */
    write_vduflag(VDU_FLAG_GRAPHICURS,0);
    if (cursorstate == HIDDEN) {	/* Start displaying the cursor */
      cursorstate = SUSPENDED;
      toggle_cursor();
    }
    break;
  case VDU_GRAPHICURS:	/* 5 - Print text at graphics cursor */
    if (!istextonly()) {
      write_vduflag(VDU_FLAG_GRAPHICURS,1);
      toggle_cursor();	/* Remove the cursor if it is being displayed */
      cursorstate = HIDDEN;
    }
    break;
  case VDU_ENABLE:	/* 6 - Enable the VDU driver */
    write_vduflag(VDU_FLAG_DISABLE,0);
    break;
  case VDU_BEEP:	/* 7 - Sound the bell */
    putchar('\7');
    if (vduflag(VDU_FLAG_ECHO)) fflush(stdout);
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
    if (vduflag(VDU_FLAG_GRAPHICURS))	/* In VDU 5 mode, clear the graphics window */
      vdu_cleargraph();
    else		/* In text mode, clear the text window */
      vdu_cleartext();
    vdu_hometext();
    break;
  case VDU_RETURN:	/* 13 - Carriage return */
    vdu_return();
    break;
  case VDU_ENAPAGE:	/* 14 - Enable page mode */
    write_vduflag(VDU_FLAG_ENAPAGE,1);
    break;
  case VDU_DISPAGE:	/* 15 - Disable page mode */
    write_vduflag(VDU_FLAG_ENAPAGE,0);
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
  case VDU_DISABLE:	/* 21 - Disable the VDU driver */
    write_vduflag(VDU_FLAG_DISABLE,1);
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
** RISC OS are returned.
** Now combined with the readmodevariable call which
** supports SWIs OS_ReadVduVariables and OS_ReadModeVariable
*/
int32 emulate_vdufn(int variable) {
  return readmodevariable(-1,variable);
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
  Uint32 sx, sy, ox, oy;
  int p;

  mode = mode & MODEMASK;	/* Lose 'shadow mode' bit */
  modecopy = mode;
  if (mode > HIGHMODE) mode = modecopy = 0;	/* Out of range modes are mapped to MODE 0 */
  ox=vscrwidth;
  oy=vscrheight;
  /* Try to catch an undefined mode */
  hide_cursor();
  if (modetable[mode].xres == 0) {
    if (matrixflags.failovermode == 255) {
      error(ERR_BADMODE);
    } else {
      modecopy = mode = matrixflags.failovermode;
    }
  }
  sx=(modetable[mode].xres * modetable[mode].xscale);
  sy=(modetable[mode].yres * modetable[mode].yscale);
  SDL_BlitSurface(matrixflags.surface, NULL, screen1, NULL);
  SDL_FreeSurface(matrixflags.surface);
  matrixflags.surface = SDL_SetVideoMode(sx, sy, 32, matrixflags.sdl_flags);
  if (!matrixflags.surface) {
    /* Reinstate previous display mode */
    sx=ox; sy=oy;
    matrixflags.surface = SDL_SetVideoMode(ox, oy, 32, matrixflags.sdl_flags);
    SDL_BlitSurface(screen1, NULL, matrixflags.surface, NULL);
    do_sdl_updaterect(matrixflags.surface, 0, 0, 0, 0);
    if (matrixflags.failovermode == 255) error(ERR_BADMODE);
  }
  autorefresh=1;
  vscrwidth = sx;
  vscrheight = sy;
  for (p=0; p<4; p++) {
    SDL_FreeSurface(screenbank[p]);
    screenbank[p]=SDL_DisplayFormat(matrixflags.surface);
  }
  SDL_FreeSurface(modescreen);
  modescreen = SDL_DisplayFormat(matrixflags.surface);
  matrixflags.modescreen_ptr = modescreen->pixels;
  matrixflags.modescreen_sz = modetable[mode].xres * modetable[mode].yres * 4;
  displaybank=0;
  writebank=0;
  SDL_FreeSurface(screen1);
  screen1 = SDL_DisplayFormat(matrixflags.surface);
  SDL_FreeSurface(screen2);
  screen2 = SDL_DisplayFormat(matrixflags.surface);
  SDL_FreeSurface(screen2A);
  screen2A = SDL_DisplayFormat(matrixflags.surface);
  SDL_FreeSurface(screen3);
  screen3 = SDL_DisplayFormat(matrixflags.surface);
  SDL_FreeSurface(screen3A);
  screen3A = SDL_DisplayFormat(matrixflags.surface);
/* Set up VDU driver parameters for mode */
  screenmode = modecopy;
  YPPC=8; if ((mode == 3) || (mode == 6) || (mode == 11) || (mode == 14) || (mode == 17)) YPPC=10;
  place_rect.h = font_rect.h = YPPC;
  reset_mode7();
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
  write_vduflag(VDU_FLAG_ECHO,1);
  write_vduflag(VDU_FLAG_GRAPHICURS,0);
  cursmode = UNDERLINE;
  cursorstate = ONSCREEN;	/* Graphics mode text cursor is not being displayed */
  clipping = FALSE;		/* A clipping region has not been defined for the screen mode */
  xgupp = xgraphunits/screenwidth;	/* Graphics units per pixel in X direction */
  ygupp = ygraphunits/screenheight;	/* Graphics units per pixel in Y direction */
  xorigin = yorigin = 0;
  xlast = ylast = xlast2 = ylast2 = 0;
  gwinleft = 0;
  gwinright = xgraphunits-1;
  gwintop = ygraphunits-1;
  gwinbottom = 0;
  write_vduflag(VDU_FLAG_TEXTWIN,0);		/* A text window has not been created yet */
  twinleft = 0;			/* Set up initial text window to whole screen */
  twinright = textwidth-1;
  twintop = 0;
  twinbottom = textheight-1;
  xtext = textxhome();
  ytext = textyhome();
  graph_fore_action = graph_back_action = 0;
  reset_colours();
  init_palette();
  write_vduflag(VDU_FLAG_ENAPAGE,0);
  if (cursorstate == NOCURSOR) cursorstate = ONSCREEN;
  SDL_FillRect(matrixflags.surface, NULL, tb_colour);
  SDL_FillRect(modescreen, NULL, tb_colour);
  SDL_FillRect(screen2, NULL, tb_colour);
  SDL_FillRect(screen3, NULL, tb_colour);
  SDL_SetClipRect(matrixflags.surface, NULL);
  sdl_mouse_onoff((matrixflags.surface->flags & SDL_FULLSCREEN) ? 0 : 1);
  if (screenmode == 7) {
    font_rect.w = place_rect.w = M7XPPC;
    font_rect.h = place_rect.h = M7YPPC;
  } else {
    font_rect.w = place_rect.w = XPPC;
    font_rect.h = place_rect.h = YPPC;
  }
  hide_cursor();
}

/*
** 'emulate_mode' deals with the Basic 'MODE' statement when the
** parameter is a number. This version of the function is used when
** the interpreter supports graphics.
*/
void emulate_mode(int32 mode) {
  setup_mode(mode);
/* Reset colours, clear screen and home cursor */
  SDL_FillRect(matrixflags.surface, NULL, tb_colour);
  SDL_FillRect(modescreen, NULL, tb_colour);
  xtext = textxhome();
  ytext = textyhome();
  do_sdl_flip(matrixflags.surface);
  emulate_vdu(VDU_CLEARGRAPH);
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
  case 8: coldepth = 256; break;
  default:
    coldepth = COL24BIT;
  }
  for (n=0; n<=HIGHMODE; n++) {
    if (modetable[n].xres == xres && modetable[n].yres == yres && modetable[n].coldepth == coldepth) break;
  }
  if (n > HIGHMODE) {
    /* Mode isn't predefined. So, let's make it. */
    n=126;
    setupnewmode(n, xres, yres, coldepth, 1, 1, 1, 1);
  }
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
    if (xeig==1 && yeig==1 && modetable[n].xres == xres && modetable[n].yres == yres && modetable[n].coldepth == coldepth) break;
  }
  if (n > HIGHMODE) {
    /* Mode isn't predefined. So, let's make it. */
    n=126;
    setupnewmode(n, xres, yres, coldepth, 1, 1, xeig, yeig);
  }
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

/* The plot_pixel function plots pixels for the drawing functions, and
   takes into account the GCOL foreground action code */
static void plot_pixel(SDL_Surface *surface, int64 offset, Uint32 colour, Uint32 action) {
  Uint32 altcolour = 0, prevcolour = 0, drawcolour, a;
  int32 rox = 0, roy = 0;

  if (clipping) {
    rox = (offset % screenwidth)*xgupp;
    roy = ygraphunits - ygupp - (offset / vscrwidth)*ygupp;
    if ((rox < gwinleft) || (rox > gwinright) || (roy < gwinbottom) || (roy > gwintop)) return;
  }
  if (plot_inverse ==1) {
    action=3;
    drawcolour=(colourdepth-1);
  } else {
    drawcolour=graph_physforecol;
  }

  if ((action==0) && (plot_inverse == 0)) {
    altcolour = colour;
  } else {
    prevcolour=*((Uint32*)surface->pixels + offset);
    prevcolour=emulate_colourfn((prevcolour >> 16) & 0xFF, (prevcolour >> 8) & 0xFF, (prevcolour & 0xFF));
    if (colourdepth == 256) prevcolour = prevcolour >> COL256SHIFT;
    switch (action) {
      case 1:
	altcolour=(prevcolour | drawcolour);
	break;
      case 2:
	altcolour=(prevcolour & drawcolour);
	break;
      case 3:
	altcolour=(prevcolour ^ drawcolour);
	break;
      case 4:
	altcolour=(prevcolour ^ (colourdepth-1));
	break;
      default:
	altcolour=drawcolour; /* Invalid GCOL action code handled as 0 */
    }
    if (colourdepth == COL24BIT) {
      altcolour = altcolour & 0xFFFFFF;
    } else {
      a=altcolour;
      altcolour=altcolour*3;
      altcolour=SDL_MapRGB(sdl_fontbuf->format, palette[altcolour+0], palette[altcolour+1], palette[altcolour+2]) + (a << 24);
    }
  }
  *((Uint32*)surface->pixels + offset) = altcolour;
}

/*
** 'flood_fill' floods fills an area of screen with the colour 'colour'.
** x and y are the coordinates of the point at which to start. All
** points that have the same colour as the one at (x, y) that can be
** reached from (x, y) are set to colour 'colour'.
** Note that the coordinates are *pixel* coordinates, that is, they are
** not expressed in graphics units.
**
** This code is slow but does the job, and is HIGHLY recursive (and memory hungry).
*/

static void flood_fill_inner(int32 x, int y, int colour, Uint32 action) {
  if (*((Uint32*)modescreen->pixels + x + y*vscrwidth) != gb_colour) return;
  plot_pixel(modescreen, x + y*vscrwidth, colour, action); /* Plot this pixel */
  if (x >= 1) /* Left */
    if (*((Uint32*)modescreen->pixels + (x-1) + y*vscrwidth) == gb_colour)
      flood_fill_inner(x-1, y, colour, action);
  if (x < (vscrwidth-1)) /* Right */
    if (*((Uint32*)modescreen->pixels + (x+1) + y*vscrwidth) == gb_colour)
      flood_fill_inner(x+1, y, colour, action);
  if (y >= 1) /* Up */
    if (*((Uint32*)modescreen->pixels + x + (y-1)*vscrwidth) == gb_colour)
      flood_fill_inner(x, y-1, colour, action);
  if (y < (vscrheight-1)) /* Down */
    if (*((Uint32*)modescreen->pixels + x + (y+1)*vscrwidth) == gb_colour)
      flood_fill_inner(x, y+1, colour, action);
}

static void flood_fill(int32 x, int y, int colour, Uint32 action) {
  int32 pwinleft, pwinright, pwintop, pwinbottom;
  if (colour == gb_colour) return;
  pwinleft = GXTOPX(gwinleft);		/* Calculate extent of graphics window in pixels */
  pwinright = GXTOPX(gwinright);
  pwintop = GYTOPY(gwintop);
  pwinbottom = GYTOPY(gwinbottom);
  if (x < pwinleft || x > pwinright || y < pwintop || y > pwinbottom) return;
  if (*((Uint32*)modescreen->pixels + x + y*vscrwidth) == gb_colour)
    flood_fill_inner(x, y, colour, action);
  hide_cursor();
  blit_scaled(0,0,screenwidth-1,screenheight-1);
  reveal_cursor();
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
  int32 xlast3, ylast3, sx, sy, ex, ey, action;
  Uint32 colour = 0;
  SDL_Rect plot_rect, temp_rect;
  if (istextonly()) return;
/* Decode the command */
  plot_inverse = 0;
  action = graph_fore_action;
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
      plot_inverse=1;
      break;
    case PLOT_BACKGROUND:	/* Use graphics background colour */
      colour = gb_colour;
      action = graph_back_action;
    }
  }
/* Now carry out the operation */
  switch (code & GRAPHOP_MASK) {
  case DRAW_SOLIDLINE:
  case DRAW_SOLIDLINE+8:
  case DRAW_DOTLINE:
  case DRAW_DOTLINE+8:
  case DRAW_SOLIDLINE2:
  case DRAW_SOLIDLINE2+8:
  case DRAW_DOTLINE2:
  case DRAW_DOTLINE2+8: {	/* Draw line */
    int32 top, left;
    left = sx;	/* Find top left-hand corner of rectangle containing line */
    top = sy;
    if (ex < sx) left = ex;
    if (ey < sy) top = ey;
    draw_line(modescreen, sx, sy, ex, ey, colour, (code & DRAW_STYLEMASK), action);
    hide_cursor();
    blit_scaled(left, top, sx+ex-left, sy+ey-top);
    reveal_cursor();
    break;
  }
  case PLOT_POINT:	/* Plot a single point */
    hide_cursor();
    if ((ex < 0) || (ex >= screenwidth) || (ey < 0) || (ey >= screenheight)) break;
    plot_pixel(modescreen, ex + ey*vscrwidth, colour, action);
    blit_scaled(ex, ey, ex, ey);
    reveal_cursor();
    break;
  case FILL_TRIANGLE: {		/* Plot a filled triangle */
    int32 left, right, top, bottom;
    filled_triangle(modescreen, GXTOPX(xlast3), GYTOPY(ylast3), sx, sy, ex, ey, colour, action);
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
    hide_cursor();
    blit_scaled(GXTOPX(left), GYTOPY(top), GXTOPX(right), GYTOPY(bottom));
    reveal_cursor();
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
    if (action==0 && !clipping) {
      SDL_FillRect(modescreen, &plot_rect, colour);
    } else {
      fill_rectangle(left, top, right, bottom, colour, action);
    }
    hide_cursor();
    blit_scaled(left, top, right, bottom);
    reveal_cursor();
    break;
  }
  case FILL_PARALLELOGRAM: {	/* Plot a filled parallelogram */
    int32 vx, vy, left, right, top, bottom;
    filled_triangle(modescreen, GXTOPX(xlast3), GYTOPY(ylast3), sx, sy, ex, ey, colour, action);
    vx = xlast3-xlast2+xlast;
    vy = ylast3-ylast2+ylast;
    filled_triangle(modescreen, ex, ey, GXTOPX(vx), GYTOPY(vy), GXTOPX(xlast3), GYTOPY(ylast3), colour, action);
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
    hide_cursor();
    blit_scaled(GXTOPX(left), GYTOPY(top), GXTOPX(right), GYTOPY(bottom));
    reveal_cursor();
    break;
  }
  case FLOOD_BACKGROUND:	/* Flood fill background with graphics foreground colour */
    flood_fill(ex, ey, colour, action);
    break;
  case PLOT_CIRCLE:		/* Plot the outline of a circle */
  case FILL_CIRCLE: {		/* Plot a filled circle */
    int32 xradius, yradius, xr;
/*
** (xlast2, ylast2) is the centre of the circle. (xlast, ylast) is a
** point on the circumference, specifically the left-most point of the
** circle.
*/
    xradius = abs(xlast2-xlast)/xgupp;
    yradius = abs(xlast2-xlast)/ygupp;
    xr=xlast2-xlast;
    if ((code & GRAPHOP_MASK) == PLOT_CIRCLE)
      draw_ellipse(modescreen, sx, sy, xradius, yradius, 0, colour, action);
    else {
      filled_ellipse(modescreen, sx, sy, xradius, yradius, 0, colour, action);
    }
    /* To match RISC OS, xlast needs to be the right-most point not left-most. */
    xlast+=(xr*2);
    ex = sx-xradius;
    ey = sy-yradius;
/* (ex, ey) = coordinates of top left hand corner of the rectangle that contains the ellipse */
    hide_cursor();
    blit_scaled(ex, ey, ex+2*xradius, ey+2*yradius);
    reveal_cursor();
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
    hide_cursor();
    blit_scaled(destleft, destop, destleft+(right-left), destop+(bottom-top));
    reveal_cursor();
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
      hide_cursor();
      blit_scaled(left, top, right, bottom);
      reveal_cursor();
    }
    break;
  }
  case PLOT_ELLIPSE:		/* Draw an ellipse outline */
  case FILL_ELLIPSE: {		/* Draw a filled ellipse */
    int32 semimajor, semiminor, shearx;
/*
** (xlast3, ylast3) is the centre of the ellipse. (xlast2, ylast2) is a
** point on the circumference in the +ve X direction and (xlast, ylast)
** is a point on the circumference in the +ve Y direction
*/
    semimajor = abs(xlast2-xlast3)/xgupp;
    semiminor = abs(ylast-ylast3)/ygupp;
    sx = GXTOPX(xlast3);
    sy = GYTOPY(ylast3);
    shearx=GXTOPX(xlast)-sx;

    if ((code & GRAPHOP_MASK) == PLOT_ELLIPSE)
      draw_ellipse(modescreen, sx, sy, semimajor, semiminor, shearx, colour, action);
    else {
      filled_ellipse(modescreen, sx, sy, semimajor, semiminor, shearx, colour, action);
    }
    ex = sx-semimajor;
    ey = sy-semiminor;
/* (ex, ey) = coordinates of top left hand corner of the rectangle that contains the ellipse */
    hide_cursor();
    blit_scaled(0,0,vscrwidth,vscrheight);
    //blit_scaled(ex, ey, ex+2*semimajor, ey+2*semiminor);
    reveal_cursor();
    break;
  }
  //default:
    //error(ERR_UNSUPPORTED); /* switch this off, make unhandled plots a no-op*/
  }
}

/*
** 'emulate_pointfn' emulates the Basic function 'POINT', returning
** the colour number of the point (x,y) on the screen
*/
int32 emulate_pointfn(int32 x, int32 y) {
  int32 colour, colnum;
  colour = *((Uint32*)modescreen->pixels + GXTOPX(x+xorigin) + GYTOPY(y+yorigin)*vscrwidth);
  if (colourdepth == COL24BIT) return riscoscolour(colour);
  colnum = emulate_colourfn((colour >> 16) & 0xFF, (colour >> 8) & 0xFF, (colour & 0xFF));
  if (colourdepth == 256) colnum = colnum >> COL256SHIFT;
  return colnum;
}

/*
** 'emulate_tintfn' deals with the Basic keyword 'TINT' when used as
** a function. It returns the 'TINT' value of the point (x, y) on the
** screen. This is one of 0, 0x40, 0x80 or 0xC0
*/
int32 emulate_tintfn(int32 x, int32 y) {
  if (colourdepth < 256) return 0;
  return *((Uint32*)modescreen->pixels + GXTOPX(x+xorigin) + GYTOPY(y+yorigin)*vscrwidth)<<TINTSHIFT;
}

/*
** 'emulate_pointto' emulates the 'POINT TO' statement
*/
void emulate_pointto(int32 x, int32 y) {
  error(ERR_UNSUPPORTED);
}

/*
** 'emulate_wait' deals with the Basic 'WAIT' statement.
** From SDL 1.2.15 manual SDL_Flip waits for vertical retrace before updating the screen.
** This doesn't always work, but better this than a no-op or an Unsupported error message.
*/
void emulate_wait(void) {
  SDL_Flip(matrixflags.surface);
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
  emulate_vdu(asc_CR);
  emulate_vdu(asc_LF);
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
int emulate_gcolrgb(int32 action, int32 background, int32 red, int32 green, int32 blue) {
  int32 colnum = emulate_colourfn(red & 0xFF, green & 0xFF, blue & 0xFF);
  emulate_gcolnum(action, background, colnum);
  return(colnum);
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
int32 emulate_setcolour(int32 background, int32 red, int32 green, int32 blue) {
  int32 colnum = emulate_colourfn(red & 0xFF, green & 0xFF, blue & 0xFF);
  set_text_colour(background, colnum);
  return(colnum);
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
** ellipse with the semi-major axis at any angle.
** The RISC OS 5 BASIC source code has the algorithm as:
** cos = COS(angle)
** sin = SIN(angle)
** slicet = min*maj
** temp = (min*cos)^2
** temp2 = (maj*sin)^2
** maxy = SQR(temp+temp2)
** slicew = slicet/maxy
** sheart = cos*sin*((maj^2)-(min^2))
** shearx = sheart/maxy
** MOVE x,y
** MOVE x+slicew,y
** PLOT XXX,x+shearx,y+maxy
*/
void emulate_ellipse(int32 x, int32 y, int32 majorlen, int32 minorlen, float64 angle, boolean isfilled) {
  int32 slicew, shearx, maxy;
  
  float64 cosv, sinv;
  
  cosv = cos(angle);
  sinv = sin(angle);
  maxy = sqrt(((minorlen*cosv)*(minorlen*cosv))+((majorlen*sinv)*(majorlen*sinv)));
  slicew = (minorlen*majorlen)/maxy;
  shearx = (cosv*sinv*((majorlen*majorlen)-(minorlen*minorlen)))/maxy;

  emulate_plot(DRAW_SOLIDLINE+MOVE_ABSOLUTE, x, y);	   /* Move to centre of ellipse */
  emulate_plot(DRAW_SOLIDLINE+MOVE_ABSOLUTE, x+slicew, y);	/* Find a point on the circumference */
  if (isfilled)
    emulate_plot(FILL_ELLIPSE+DRAW_ABSOLUTE, x+shearx, y+maxy);
  else {
    emulate_plot(PLOT_ELLIPSE+DRAW_ABSOLUTE, x+shearx, y+maxy);
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
  static SDL_Surface *fontbuf, *m7fontbuf;
  int p;

  matrixflags.sdl_flags = SDL_DOUBLEBUF | SDL_HWSURFACE;
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
    fprintf(stderr, "Unable to init SDL: %s\n", SDL_GetError());
    return FALSE;
  }

  reset_sysfont(0);
  if (basicvars.runflags.swsurface) matrixflags.sdl_flags = SDL_SWSURFACE;
  if (basicvars.runflags.startfullscreen) matrixflags.sdl_flags |= SDL_FULLSCREEN;
  matrixflags.surface = SDL_SetVideoMode(640, 512, 32, matrixflags.sdl_flags); /* MODE 0 */
  if (!matrixflags.surface) {
    fprintf(stderr, "Failed to open screen: %s\n", SDL_GetError());
    return FALSE;
  }
  for (p=0; p<4; p++) {
    SDL_FreeSurface(screenbank[p]);
    screenbank[p]=SDL_DisplayFormat(matrixflags.surface);
  }
  modescreen = SDL_DisplayFormat(matrixflags.surface);
  displaybank=0;
  writebank=0;
  screen1 = SDL_DisplayFormat(matrixflags.surface);
  screen2 = SDL_DisplayFormat(matrixflags.surface);
  screen2A = SDL_DisplayFormat(matrixflags.surface);
  screen3 = SDL_DisplayFormat(matrixflags.surface);
  screen3A = SDL_DisplayFormat(matrixflags.surface);
  fontbuf = SDL_CreateRGBSurface(SDL_SWSURFACE,   XPPC,   YPPC, 32, 0xff000000, 0x00ff0000, 0x0000ff00, 0x000000ff);
  m7fontbuf = SDL_CreateRGBSurface(SDL_SWSURFACE, M7XPPC, M7YPPC, 32, 0xff000000, 0x00ff0000, 0x0000ff00, 0x000000ff);
  sdl_fontbuf = SDL_ConvertSurface(fontbuf, matrixflags.surface->format, 0);  /* copy surface to get same format as main windows */
  sdl_m7fontbuf = SDL_ConvertSurface(m7fontbuf, matrixflags.surface->format, 0);  /* copy surface to get same format as main windows */
  SDL_FreeSurface(fontbuf);
  SDL_FreeSurface(m7fontbuf);

  vdunext = 0;
  vduneeded = 0;
  write_vduflag(VDU_FLAG_ENAPRINT,0);
  write_vduflag(MODE7_UPDATE,1);
  write_vduflag(MODE7_UPDATE_HIGHACC,1);
  xgupp = ygupp = 1;
  SDL_WM_SetCaption("Matrix Brandy Basic VI Interpreter", "Matrix Brandy");
  SDL_EnableUNICODE(SDL_ENABLE);
  SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
  setup_mode(0);

  xor_mask = SDL_MapRGB(sdl_fontbuf->format, 0xff, 0xff, 0xff);

  font_rect.x = font_rect.y = 0;

  font_rect.w = place_rect.w = XPPC;
  font_rect.h = place_rect.h = YPPC;
  place_rect.x = place_rect.y = 0;
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

static unsigned int teletextgraphic(unsigned int ch, unsigned int y) {
  unsigned int left, right, hmask, val;

  if (y > 19) return(0); /* out of range */
  val = 0;
  left=0xFF00u;
  right=0x00FFu;
  hmask=0x7E7Eu;

/* Row sets - 0 to 6, 7 to 13, and 14 to 19 */
  if ((y >= 0) && (y <= 6)) {
    if (ch & 1) val=left; 
    if (ch & 2) val+=right;
  } else if ((y >= 7) && (y <= 13)) {
    if (ch & 4) val=left;
    if (ch & 8) val+=right;
  } else if ((y >= 14) && (y <= 19)) {
    if (ch & 16) val=left;
    if (ch & 64) val+=right;
  }
  if (vduflag(MODE7_SEPREAL)) {
    if (y == 0 || y == 6 || y == 7 || y == 13 || y==14 || y == 19) val = 0;
    val = val & hmask;
  }
  return(val);
}

static void mode7renderline(int32 ypos) {
  int32 ch, l_text_physbackcol, l_text_backcol, l_text_physforecol, l_text_forecol, xt, yt;
  int32 y=0, yy=0, topx=0, topy=0, line=0, xch=0;
  int32 vdu141used = 0;
  
  if (!vduflag(MODE7_UPDATE) || (screenmode != 7)) return;
  /* Preserve values */
  l_text_physbackcol=text_physbackcol;
  l_text_backcol=text_backcol;
  l_text_physforecol=text_physforecol;
  l_text_forecol=text_forecol;
  xt=xtext;
  yt=ytext;

  text_physbackcol=text_backcol=0;
  text_physforecol=text_forecol=7;
  set_rgb();

  write_vduflag(MODE7_VDU141MODE,1);
  write_vduflag(MODE7_VDU141ON,0);
  write_vduflag(MODE7_HIGHBIT,0);
  write_vduflag(MODE7_SEPGRP,0);
  write_vduflag(MODE7_CONCEAL,0);
  write_vduflag(MODE7_HOLD,0);
  write_vduflag(MODE7_FLASH,0);
  mode7prevchar=32;

  m7_rect.x=0;
  m7_rect.w=40*M7XPPC;
  m7_rect.y=ypos*M7YPPC;
  m7_rect.h=M7YPPC;

  if (cursorstate == ONSCREEN) cursorstate = SUSPENDED;
  for (xtext=0; xtext<=39; xtext++) {
    ch=mode7frame[ypos][xtext];
    if (ch < 32) ch = ch | 0x80;
    /* Check the Set At codes here */
    switch (ch) {
      case TELETEXT_FLASH_OFF:
	write_vduflag(MODE7_FLASH,0);
	break;
      case TELETEXT_SIZE_NORMAL:
	write_vduflag(MODE7_VDU141ON,0);
	break;
      case TELETEXT_CONCEAL:
	write_vduflag(MODE7_CONCEAL,1);
	break;
      case TELETEXT_GRAPHICS_CONTIGUOUS:
	write_vduflag(MODE7_SEPGRP,0);
	break;
      case TELETEXT_GRAPHICS_SEPARATE:
	write_vduflag(MODE7_SEPGRP,1);
	break;
      case TELETEXT_BACKGROUND_BLACK:
	text_physbackcol = text_backcol = 0;
	set_rgb();
	break;
      case TELETEXT_BACKGROUND_SET:
	text_physbackcol = text_backcol = text_physforecol;
	set_rgb();
	break;
      case TELETEXT_GRAPHICS_HOLD:
	write_vduflag(MODE7_HOLD,1);
	break;
    }
    /* Now we write the character. Copied and optimised from write_char() above */
    topx = xtext*M7XPPC;
    topy = ypos*M7YPPC;
    place_rect.x = topx;
    place_rect.y = topy;
    SDL_FillRect(sdl_m7fontbuf, NULL, tb_colour);
    xch=ch;
    if (vduflag(MODE7_HOLD) && ((ch >= 128 && ch <= 140) || (ch >= 142 && ch <= 151 ) || (ch == 152 && vduflag(MODE7_REVEAL)) || (ch >= 153 && ch <= 159))) {
      ch=mode7prevchar;
    } else {
      if (vduflag(MODE7_HIGHBIT)) {
	if (ch==35) ch = 223;
	if (ch==96) ch = 163;
	if (ch==95) ch = 224;
	ch = ch | 0x80;
	if (ch==223) ch=35;
	if ((ch >= 0xC0) && (ch <= 0xDF)) ch = ch & 0x7F;
	write_vduflag(MODE7_SEPREAL,vduflag(MODE7_SEPGRP));
      } else {
	if (ch==163) ch=96;
	if (ch==223) ch=35;
	if (ch==224) ch=95;
	ch = ch & 0x7F;
	if (ch < 32) ch=32;
      }
    }
    for (y=0; y < M7YPPC; y++) {
      if (vduflag(MODE7_CONCEAL) && !vduflag(MODE7_REVEAL)) {
	line=0;
      } else {
	if (vduflag(MODE7_VDU141ON)) {
	  yy=((y/2)+(M7YPPC*vduflag(MODE7_VDU141MODE)/2));
	  if ((ch >= 160 && ch <= 191) || (ch >= 224 && ch <= 255)) {
	    line = teletextgraphic(ch, yy);
	  } else if ((ch >= 128) && (ch <= 159)) line = 0;
	  else line = mode7font[ch-' '][yy];
	} else {
	  if (vdu141track[ypos] == 2) line = 0;
	    else {
	    if ((ch >= 160 && ch <= 191) || (ch >= 224 && ch <= 255)) {
	      line = teletextgraphic(ch, y);
	    } else if ((ch >= 128) && (ch <= 159)) line = 0;
	    else line = mode7font[ch-' '][y];
	  }
	}
	if (vduflag(MODE7_HIGHBIT) && ((ch >= 160 && ch <= 191) || (ch >= 224 && ch <= 255)))
	  mode7prevchar=ch;
      }
      if (line!=0) {
	if (line & 0x8000) *((Uint32*)sdl_m7fontbuf->pixels +  0 + y*M7XPPC) = tf_colour;
	if (line & 0x4000) *((Uint32*)sdl_m7fontbuf->pixels +  1 + y*M7XPPC) = tf_colour;
	if (line & 0x2000) *((Uint32*)sdl_m7fontbuf->pixels +  2 + y*M7XPPC) = tf_colour;
	if (line & 0x1000) *((Uint32*)sdl_m7fontbuf->pixels +  3 + y*M7XPPC) = tf_colour;
	if (line & 0x0800) *((Uint32*)sdl_m7fontbuf->pixels +  4 + y*M7XPPC) = tf_colour;
	if (line & 0x0400) *((Uint32*)sdl_m7fontbuf->pixels +  5 + y*M7XPPC) = tf_colour;
	if (line & 0x0200) *((Uint32*)sdl_m7fontbuf->pixels +  6 + y*M7XPPC) = tf_colour;
	if (line & 0x0100) *((Uint32*)sdl_m7fontbuf->pixels +  7 + y*M7XPPC) = tf_colour;
	if (line & 0x0080) *((Uint32*)sdl_m7fontbuf->pixels +  8 + y*M7XPPC) = tf_colour;
	if (line & 0x0040) *((Uint32*)sdl_m7fontbuf->pixels +  9 + y*M7XPPC) = tf_colour;
	if (line & 0x0020) *((Uint32*)sdl_m7fontbuf->pixels + 10 + y*M7XPPC) = tf_colour;
	if (line & 0x0010) *((Uint32*)sdl_m7fontbuf->pixels + 11 + y*M7XPPC) = tf_colour;
	if (line & 0x0008) *((Uint32*)sdl_m7fontbuf->pixels + 12 + y*M7XPPC) = tf_colour;
	if (line & 0x0004) *((Uint32*)sdl_m7fontbuf->pixels + 13 + y*M7XPPC) = tf_colour;
	if (line & 0x0002) *((Uint32*)sdl_m7fontbuf->pixels + 14 + y*M7XPPC) = tf_colour;
	if (line & 0x0001) *((Uint32*)sdl_m7fontbuf->pixels + 15 + y*M7XPPC) = tf_colour;
      }
    }
    SDL_BlitSurface(sdl_m7fontbuf, &font_rect, screen2, &place_rect);
    if (vduflag(MODE7_FLASH)) SDL_FillRect(sdl_m7fontbuf, NULL, tb_colour);
    SDL_BlitSurface(sdl_m7fontbuf, &font_rect, screen3, &place_rect);
    ch=xch; /* restore value */
    /* And now handle the Set After codes */
    switch (ch) {
      case TELETEXT_ALPHA_BLACK:
        if (vduflag(MODE7_BLACK)) {
	  write_vduflag(MODE7_HIGHBIT,0);
	  write_vduflag(MODE7_CONCEAL,0);
	  mode7prevchar=32;
	  text_physforecol = text_forecol = 0;
	  set_rgb();
	}
	break;
      case TELETEXT_ALPHA_RED:
      case TELETEXT_ALPHA_GREEN:
      case TELETEXT_ALPHA_YELLOW:
      case TELETEXT_ALPHA_BLUE:
      case TELETEXT_ALPHA_MAGENTA:
      case TELETEXT_ALPHA_CYAN:
      case TELETEXT_ALPHA_WHITE:
	write_vduflag(MODE7_HIGHBIT,0);
	write_vduflag(MODE7_CONCEAL,0);
	mode7prevchar=32;
	text_physforecol = text_forecol = (ch - 128);
	set_rgb();
	break;
      case TELETEXT_FLASH_ON:
	write_vduflag(MODE7_FLASH,1);
	break;
      case TELETEXT_SIZE_DOUBLEHEIGHT:
	write_vduflag(MODE7_VDU141ON,1);
	vdu141used=1;
	if (vdu141track[ypos] < 2) {
	  vdu141track[ypos] = 1;
	  vdu141track[ypos+1]=2;
	  write_vduflag(MODE7_VDU141MODE,0);
	} else {
	  write_vduflag(MODE7_VDU141MODE,1);
	}
	break;
      case TELETEXT_GRAPHICS_BLACK:
	if (vduflag(MODE7_BLACK)) {
	  write_vduflag(MODE7_HIGHBIT,1);
	  write_vduflag(MODE7_CONCEAL,0);
	  text_physforecol = text_forecol = 0;
	  set_rgb();
	}
	break;
      case TELETEXT_GRAPHICS_RED:
      case TELETEXT_GRAPHICS_GREEN:
      case TELETEXT_GRAPHICS_YELLOW:
      case TELETEXT_GRAPHICS_BLUE:
      case TELETEXT_GRAPHICS_MAGENTA:
      case TELETEXT_GRAPHICS_CYAN:
      case TELETEXT_GRAPHICS_WHITE:
	write_vduflag(MODE7_HIGHBIT,1);
	write_vduflag(MODE7_CONCEAL,0);
	text_physforecol = text_forecol = (ch - 144);
	set_rgb();
	 break;
      /* These two break the teletext spec, but matches the behaviour in the SAA5050 and RISC OS */
      case TELETEXT_BACKGROUND_BLACK:
      case TELETEXT_BACKGROUND_SET:
	mode7prevchar=32;
	break;
      case TELETEXT_GRAPHICS_RELEASE:
	write_vduflag(MODE7_HOLD,0);
	break;
    }
  }
  SDL_BlitSurface(vduflag(MODE7_BANK) ? screen3 : screen2, &m7_rect, matrixflags.surface, &m7_rect);
  do_sdl_updaterect(matrixflags.surface, 0, topy, 40*M7XPPC, M7YPPC);

  write_vduflag(MODE7_VDU141ON,0);
  write_vduflag(MODE7_HIGHBIT,0);
  write_vduflag(MODE7_SEPGRP,0);
  write_vduflag(MODE7_SEPREAL,0);
  write_vduflag(MODE7_CONCEAL,0);
  write_vduflag(MODE7_HOLD,0);
  write_vduflag(MODE7_FLASH,0);
  text_physbackcol=l_text_physbackcol;
  text_backcol=l_text_backcol;
  text_physforecol=l_text_physforecol;
  text_forecol=l_text_forecol;
  set_rgb();
  xtext=xt;
  ytext=yt;
  mode7changed[ypos]=0;

  /* Cascade VDU141 changes */
  if ((!vdu141used) && vdu141track[ypos]==1) vdu141track[ypos]=0;
  if ((ypos < 24) && vdu141track[ypos+1]) {
    if ((vdu141track[ypos] == 0) || (vdu141track[ypos] == 2)) vdu141track[ypos+1]=1;
    mode7renderline(ypos+1);
  }
}

void mode7renderscreen(void) {
  int32 ypos;
  Uint8 bmpstate=vduflag(MODE7_UPDATE);
  
  if (screenmode != 7) return;
  
  write_vduflag(MODE7_UPDATE,1);
  for (ypos=0; ypos < 26;ypos++) vdu141track[ypos]=0;
  for (ypos=0; ypos<=24; ypos++) mode7renderline(ypos);
  write_vduflag(MODE7_UPDATE,bmpstate);
}

static void trace_edge(int32 x1, int32 y1, int32 x2, int32 y2) {
  int32 dx, dy, xf, yf, a, b, t, i;

  if (x1 == x2 && y1 == y2) return;

  if (x2 > x1) {
    dx = x2 - x1;
    xf = 1;
  }
  else {
    dx = x1 - x2;
    xf = -1;
  }

  if (y2 > y1) {
    dy = y2 - y1;
    yf = 1;
  }
  else {
    dy = y1 - y2;
    yf = -1;
  }

  if (dx > dy) {
    a = dy + dy;
    t = a - dx;
    b = t - dx;
    for (i = 0; i <= dx; i++) {
      if (y1 >= 0 && x1 < geom_left[y1]) geom_left[y1] = x1;
      if (y1 >= 0 && x1 > geom_right[y1]) geom_right[y1] = x1;
      x1 += xf;
      if (t < 0)
        t += a;
      else {
        t += b;
        y1 += yf;
      }
    }
  }
  else {
    a = dx + dx;
    t = a - dy;
    b = t - dy;
    for (i = 0; i <= dy; i++) {
      if (y1 >= 0 && x1 < geom_left[y1]) geom_left[y1] = x1;
      if (y1 >= 0 && x1 > geom_right[y1]) geom_right[y1] = x1;
      y1 += yf;
      if (t < 0)
        t += a;
      else {
        t += b;
        x1 += xf;
      }
    }
  }
}

/*
** Draw a horizontal line
*/
static void draw_h_line(SDL_Surface *sr, int32 x1, int32 y, int32 x2, Uint32 col, Uint32 action) {
  int32 tt, i;
  if ((x1 < 0 && x2 < 0) || (x1 >= vscrwidth && x2 >= vscrwidth )) return;
  if (x1 > x2) {
    tt = x1; x1 = x2; x2 = tt;
  }
  if ( y >= 0 && y < vscrheight ) {
    if (x1 < 0) x1 = 0;
    if (x1 >= vscrwidth) x1 = vscrwidth-1;
    if (x2 < 0) x2 = 0;
    if (x2 >= vscrwidth) x2 = vscrwidth-1;
    for (i = x1; i <= x2; i++)
      plot_pixel(sr, i + y*vscrwidth, col, action);
  }
}

/*
** Draw a filled polygon of n vertices
*/
static void buff_convex_poly(SDL_Surface *sr, int32 n, int32 *x, int32 *y, Uint32 col, Uint32 action) {
  int32 i, iy;
  int32 low = 32767, high = 0;

  /* set highest and lowest points to visit */
  for (i = 0; i < n; i++) {
#if 1
    if (y[i] > high) high = y[i];
    if (y[i] < low) low = y[i];
#else
    if (y[i] > MAX_YRES)
      y[i] = high = MAX_YRES;
    else if (y[i] > high)
      high = y[i];
    if (y[i] < 0)
      y[i] = low = 0;
    else if (y[i] < low)
      low = y[i];
#endif
  }
  /* reset the minumum amount of the edge tables */
  for (iy = (low < 0) ? 0: low; iy <= high; iy++) {
    geom_left[iy] = MAX_XRES + 1;
    geom_right[iy] = - 1;
  }

  /* define edges */
  trace_edge(x[n - 1], y[n - 1], x[0], y[0]);

  for (i = 0; i < n - 1; i++)
    trace_edge(x[i], y[i], x[i + 1], y[i + 1]);

  /* fill horizontal spans of pixels from geom_left[] to geom_right[] */
  for (iy = low; iy <= high; iy++)
    draw_h_line(sr, geom_left[iy], iy, geom_right[iy], col, action);
}

/*
** 'draw_line' draws an arbitary line in the graphics buffer 'sr'.
** clipping for x & y is implemented.
** Style is the bitmasked with 0x38 from the PLOT code:
** Bit 0x08: Don't plot end point
** Bit 0x10: Draw a dotted line, skipping every other point.
** Bit 0x20: Don't plot the start point.
*/
static void draw_line(SDL_Surface *sr, int32 x1, int32 y1, int32 x2, int32 y2, Uint32 col, int32 style, Uint32 action) {
  int d, x, y, ax, ay, sx, sy, dx, dy, tt, skip=0;
  if (x1 > x2) {
    tt = x1; x1 = x2; x2 = tt;
    tt = y1; y1 = y2; y2 = tt;
  }
  dx = x2 - x1;
  ax = abs(dx) << 1;
  sx = ((dx < 0) ? -1 : 1);
  dy = y2 - y1;
  ay = abs(dy) << 1;
  sy = ((dy < 0) ? -1 : 1);

  x = x1;
  y = y1;
  if (style & 0x20) skip=1;

  if (ax > ay) {
    d = ay - (ax >> 1);
    while (x != x2) {
      if (skip) {
        skip=0;
      } else {
	if ((x >= 0) && (x < screenwidth) && (y >= 0) && (y < screenheight))
	  plot_pixel(sr, x + y*vscrwidth, col, action);
	if (style & 0x10) skip=1;
      }
      if (d >= 0) {
        y += sy;
        d -= ax;
      }
      x += sx;
      d += ay;
    }
  } else {
    d = ax - (ay >> 1);
    while (y != y2) {
      if (skip) {
        skip=0;
      } else {
	if ((x >= 0) && (x < screenwidth) && (y >= 0) && (y < screenheight))
	  plot_pixel(sr, x + y*vscrwidth, col, action);
	if (style & 0x10) skip=1;
      }
      if (d >= 0) {
        x += sx;
        d -= ay;
      }
      y += sy;
      d += ax;
    }
  }
  if ( ! (style & 0x08)) {
    if ((x >= 0) && (x < screenwidth) && (y >= 0) && (y < screenheight))
      plot_pixel(sr, x + y*vscrwidth, col, action);
  }
}

/*
** 'filled_triangle' draws a filled triangle in the graphics buffer 'sr'.
*/
static void filled_triangle(SDL_Surface *sr, int32 x1, int32 y1, int32 x2, int32 y2,
                     int32 x3, int32 y3, Uint32 col, Uint32 action) {
  int x[3], y[3];

  x[0]=x1;
  x[1]=x2;
  x[2]=x3;

  y[0]=y1;
  y[1]=y2;
  y[2]=y3;

  buff_convex_poly(sr, 3, x, y, col, action);
}

/*
** Draw an ellipse into a buffer
*/
static void draw_ellipse(SDL_Surface *sr, int32 x0, int32 y0, int32 a, int32 b, int32 shearx, Uint32 c, Uint32 action) {
  int32 x, y, y1, aa, bb, d, g, h, ym, si;
  float64 s;

  aa = a * a;
  bb = b * b;

  h = (FAST_4_DIV(aa)) - b * aa + bb;
  g = (FAST_4_DIV(9 * aa)) - (FAST_3_MUL(b * aa)) + bb;
  x = 0;
  ym = y = b;

  while (g < 0) {
    s=shearx*(1.0*y/ym);
    si=s;
    if (((y0 - y) >= 0) && ((y0 - y) < vscrheight)) {
      if (((x0 - x + si) >= 0) && ((x0 - x + si) < vscrwidth)) plot_pixel(sr, x0 + (y0 - y)*vscrwidth - x + si, c, action);
      if (((x0 + x + si) >= 0) && ((x0 + x + si) < vscrwidth)) plot_pixel(sr, x0 + (y0 - y)*vscrwidth + x + si, c, action);
    }
    if (((y0 + y) >= 0) && ((y0 + y) < vscrheight)) {
      if (((x0 - x - si) >= 0) && ((x0 - x - si) < vscrwidth)) plot_pixel(sr, x0 + (y0 + y)*vscrwidth - x - si, c, action);
      if (((x0 + x - si) >= 0) && ((x0 + x - si) < vscrwidth)) plot_pixel(sr, x0 + (y0 + y)*vscrwidth + x - si, c, action);
    }

    if (h < 0) {
      d = ((FAST_2_MUL(x)) + 3) * bb;
      g += d;
    }
    else {
      d = ((FAST_2_MUL(x)) + 3) * bb - FAST_2_MUL((y - 1) * aa);
      g += (d + (FAST_2_MUL(aa)));
      --y;
    }

    h += d;
    ++x;
  }

  y1 = y;
  h = (FAST_4_DIV(bb)) - a * bb + aa;
  x = a;
  y = 0;

  while (y <= y1) {
    s=shearx*(1.0*y/ym);
    si=s;
    if (((y0 - y) >= 0) && ((y0 - y) < vscrheight)) {
      if (((x0 - x + si) >= 0) && ((x0 - x + si) < vscrwidth)) plot_pixel(sr, x0 + (y0 - y)*vscrwidth - x + si, c, action);
      if (((x0 + x + si) >= 0) && ((x0 + x + si) < vscrwidth)) plot_pixel(sr, x0 + (y0 - y)*vscrwidth + x + si, c, action);
    } 
    if (((y0 + y) >= 0) && ((y0 + y) < vscrheight)) {
      if (((x0 - x - si) >= 0) && ((x0 - x - si) < vscrwidth)) plot_pixel(sr, x0 + (y0 + y)*vscrwidth - x - si, c, action);
      if (((x0 + x - si) >= 0) && ((x0 + x - si) < vscrwidth)) plot_pixel(sr, x0 + (y0 + y)*vscrwidth + x - si, c, action);
    }

    if (h < 0)
      h += ((FAST_2_MUL(y)) + 3) * aa;
    else {
      h += (((FAST_2_MUL(y) + 3) * aa) - (FAST_2_MUL(x - 1) * bb));
      --x;
    }
    ++y;
  }
}

/*
** Draw a filled ellipse into a buffer
*/

static void filled_ellipse(SDL_Surface *sr, 
  int32 x0, /* Centre X */
  int32 y0, /* Centre Y */
  int32 a, /* Width */
  int32 b, /* Height */
  int32 shearx, /* X shear */
  Uint32 c, Uint32 action
) {

  int32 x, y, width, aa, bb, aabb, ym, dx, si;
  float64 s;

  aa = a * a;
  bb = b * b;
  aabb=aa*bb;

  x = 0;
  width=a;
  dx = 0;
  ym = y = b;

  draw_h_line(sr, x0-a, y0, x0 + a, c, action);

  for (y=1; y <= b; y++) {
    s=shearx*(1.0*y/ym);
    si=s;
    x=width-(dx-1);
    for (;x>0; x--)
      if (x*x*bb + y*y*aa < aabb) break;
    dx = width -x;
    width = x;
    draw_h_line(sr,x0-width+si, y0-y, x0+width+si, c, action);
    draw_h_line(sr,x0-width-si, y0+y, x0+width-si, c, action);
  }

}

void get_sdl_mouse(int64 values[]) {
  int x, y;
  Uint8 b, xb;
  SDL_Event ev;

  SDL_PumpEvents();
  b=SDL_GetMouseState(&x, &y);
  x=(x*2);
  if (x < 0) x = 0;
  if (x >= xgraphunits) x = (xgraphunits - 1);

  y=(2*(vscrheight-y));
  if (y < 0) y = 0;
  if (y >= ygraphunits) y = (ygraphunits - 1);

  /* Swap button bits around */
  xb = FAST_4_DIV(b & 4) + (b & 2) + FAST_4_MUL(b & 1);

  values[0]=x;
  values[1]=y;
  values[2]=xb;
  values[3]=basicvars.centiseconds - basicvars.monotonictimebase;
  while(SDL_PollEvent(&ev)) {
    if (ev.type == SDL_QUIT) exit_interpreter(EXIT_SUCCESS);
  }
}

void warp_sdlmouse(int32 x, int32 y) {
  SDL_WarpMouse(x/2,vscrheight-(y/2));
}

void sdl_mouse_onoff(int state) {
  if (state) SDL_ShowCursor(SDL_ENABLE);
  else SDL_ShowCursor(SDL_DISABLE);
}

void set_wintitle(char *title) {
  SDL_WM_SetCaption(title, title);
}

void fullscreenmode(int onoff) {
  if (onoff == 1) {
    matrixflags.sdl_flags |= SDL_FULLSCREEN;
  } else if (onoff == 2) {
    matrixflags.sdl_flags ^= SDL_FULLSCREEN;
  } else {
    matrixflags.sdl_flags &= ~SDL_FULLSCREEN;
  }
  SDL_BlitSurface(matrixflags.surface, NULL, screen1, NULL);
  matrixflags.surface = SDL_SetVideoMode(matrixflags.surface->w, matrixflags.surface->h, matrixflags.surface->format->BitsPerPixel, matrixflags.sdl_flags);
  SDL_BlitSurface(screen1, NULL, matrixflags.surface, NULL);
  do_sdl_updaterect(matrixflags.surface, 0, 0, 0, 0);
}

void setupnewmode(int32 mode, int32 xres, int32 yres, int32 cols, int32 mxscale, int32 myscale, int32 xeig, int32 yeig) {
  if ((mode < 64) || (mode > HIGHMODE)) {
    emulate_printf("Warning: Can only define modes in the range 64 to %d.\r\n", HIGHMODE);
    return;
  }
  if ((cols != 2) && (cols != 4) && (cols != 16) && (cols != 256) && (cols != COL24BIT)) {
    emulate_printf("Warning: Can only define modes with 2, 4, 16, 256 or 16777216 colours.\r\n");
    return;
  }
  if ((mxscale==0) || (myscale==0)) {
    emulate_printf("Warning: pixel scaling can't be zero.\r\n");
    return;
  }
  if ((xres < 8) || (yres < 8)) {
    emulate_printf("Warning: Display size can't be smaller than 8x8 pixels.\r\n");
    return;
  }
  modetable[mode].xres = xres;
  modetable[mode].yres = yres;
  modetable[mode].coldepth = cols;
  modetable[mode].xgraphunits = (xres * (1<<xeig) * mxscale);
  modetable[mode].ygraphunits = (yres * (1<<yeig) * myscale);
  modetable[mode].xtext = (xres / 8);
  modetable[mode].ytext = (yres / 8);
  modetable[mode].xscale = mxscale;
  modetable[mode].yscale = myscale;
}

void refresh_location(uint32 offset) {
  uint32 ox,oy;

  ox=offset % screenwidth;
  oy=offset / screenwidth;
  blit_scaled(ox,oy,ox,oy);
}

void star_refresh(int flag) {
  if ((flag == 0) || (flag == 1) || (flag==2)) {
    autorefresh=flag;
  }
  if (flag & 1) {
    if (screenmode == 7) {
      mode7renderscreen();
    } else {
      SDL_BlitSurface(screenbank[displaybank], NULL, matrixflags.surface, NULL);
      if ((screenmode == 3) || (screenmode == 6)) {
	int p;
	hide_cursor();
	scroll_rect.x=0;
	scroll_rect.w=screenwidth*xscale;
	scroll_rect.h=4;
	for (p=0; p<25; p++) {
	  scroll_rect.y=16+(p*20);
	  SDL_FillRect(matrixflags.surface, &scroll_rect, 0);
	}
      }
      SDL_Flip(matrixflags.surface);
    }
  }
}

int get_refreshmode(void) {
  return autorefresh;
}

int32 osbyte42(int x) {
  int fullscreen=0, ref=(x & 3), fsc=((x & 12) >> 2);
  int outx;
  
  if (matrixflags.surface->flags & SDL_FULLSCREEN) fullscreen=8;
  if (x == 0) {
    outx = fullscreen + (autorefresh+1);
    return ((outx << 8) + 42);
  }
  if (x == 255) {
    star_refresh(1);
    osbyte112(1);
    osbyte113(1);
    emulate_vdu(6);
    return 0xFF2A;
  }
  /* Handle the lowest 2 bits - REFRESH state */
  if (ref) star_refresh(ref-1);
  /* Handle the next 2 bits - FULLSCREEN state */
  if (fsc) fullscreenmode(fsc-1);
  /* If bit 4 set, do immediate refrsh */
  if (x & 16) star_refresh(3);
  return((x << 8) + 42);
}

void osbyte112(int x) {
  /* OSBYTE 112 selects which bank of video memory is to be written to */
  sysvar[250]=x;
  if (screenmode == 7) return;
  if (x==0) x=1;
  if (x <= MAXBANKS) writebank=(x-1);
}

void osbyte113(int x) {
  /* OSBYTE 113 selects which bank of video memory is to be displayed */
  sysvar[251]=x;
  if (screenmode == 7) return;
  if (x==0) x=1;
  if (x <= MAXBANKS) displaybank=(x-1);
  SDL_BlitSurface(screenbank[displaybank], NULL, matrixflags.surface, NULL);
  SDL_Flip(matrixflags.surface);
}

void screencopy(int32 src, int32 dst) {
  SDL_BlitSurface(screenbank[src-1],NULL,screenbank[dst-1],NULL);
  if (dst==(displaybank+1)) {
    SDL_BlitSurface(screenbank[displaybank], NULL, matrixflags.surface, NULL);
    SDL_Flip(matrixflags.surface);
  }
}

int32 get_maxbanks(void) {
  return MAXBANKS;
}

int32 osbyte134_165(int32 a) {
  return ((ytext << 16) + (xtext << 8) + a);
}

int32 osbyte135() {
  if (screenmode == 7) {
    return ((screenmode << 16) + (mode7frame[ytext][xtext] << 8) + 135);
  } else {
    return ((screenmode << 16) + 135);
  }
}

int32 osbyte250() {
  return (((displaybank+1) << 16) + ((writebank+1) << 8) + 250);
}

int32 osbyte251() {
  return (((displaybank+1) << 8) + 251);
}

void osword09(int64 x) {
  unsigned char *block;
  int32 px, py;

  block=(unsigned char *)(basicvars.offbase+x);
  px=block[0] + (block[1] << 8);
  py=block[2] + (block[3] << 8);
  block[4] = emulate_pointfn(px, py);
}

void osword0A(int64 x) {
  unsigned char *block;
  int32 offset, i;
  
  block=(unsigned char *)(basicvars.offbase+x);
  offset = block[0]-32;
  if (offset < 0) return;
  for (i=0; i<= 7; i++) block[i+1]=sysfont[offset][i];
}

/* Like OSWORD 10 but for the MODE 7 16x20 font
 */
void osword8B(int64 x) {
  unsigned char *block;
  int32 offset, i, ch, chbank;

  block=(unsigned char *)(basicvars.offbase+x);
  if ( ( block[0] < 3 ) || ( block[1] < 43 ) ) return;
  if ( ( block[0] == 4 ) && ( block[1] < 44 ) ) return;
  if ( block[1] == 43 ) {
    ch=block[2];
    if (ch==163) ch=96;
    if (ch==223) ch=35;
    if (ch==224) ch=95;
    ch = ch & 0x7F;
    offset = ch -32;
    if ((offset < 0) || (offset > 95)) return;
    for (i=0; i<= 19; i++) {
      block[(2*i)+3]=mode7font[offset][i] / 256;
      block[(2*i)+4]=mode7font[offset][i] % 256;
    }
  } else {
    ch=block[2];
    chbank=block[3];
    if (chbank == 0) {
      if (ch==163) ch=96;
      if (ch==223) ch=35;
      if (ch==224) ch=95;
      ch = ch & 0x7F;
      offset = ch -32;
      if ((offset < 0) || (offset > 95)) return;
      for (i=0; i<= 19; i++) {
        block[(2*i)+4]=mode7font[offset][i] / 256;
        block[(2*i)+5]=mode7font[offset][i] % 256;
      }
    } else {
      for (i=0; i<= 19; i++) {
        block[(2*i)+4]=mode7fontbanks[chbank-1][ch][i] / 256;
        block[(2*i)+5]=mode7fontbanks[chbank-1][ch][i] % 256;
      }
    }
  }
}

/* Write a character definition to the current MODE 7 font.
 */
void osword8C(int64 x) {
  unsigned char *block;
  int32 offset, i, ch;

  block=(unsigned char *)(basicvars.offbase+x);
  if (block[0] < 43) return;
  ch=block[2];
  if (ch==163) ch=96;
  if (ch==223) ch=35;
  if (ch==224) ch=95;
  ch = ch & 0x7F;
  offset = ch -32;
  if ((offset < 0) || (offset > 95)) return;
  for (i=0; i<= 19; i++) {
    mode7font[offset][i] = block[(2*i)+4] + (256*block[(2*i)+3]);
  }
  if (screenmode == 7) mode7renderscreen();
}

void sdl_screensave(char *fname) {
  /* Strip quote marks, where appropriate */
  if ((fname[0] == '"') && (fname[strlen(fname)-1] == '"')) {
    fname[strlen(fname)-1] = '\0';
    fname++;
  }

  if (screenmode == 7) {
    if (SDL_SaveBMP(screen2, fname)) {
      error(ERR_CANTWRITE);
    }
  } else {
    SDL_BlitSurface(screenbank[displaybank], NULL, screen1, NULL);
    if (SDL_SaveBMP(screen1, fname)) {
      error(ERR_CANTWRITE);
    }
  }
}

void sdl_screenload(char *fname) {
  SDL_Surface *placeholder;
  
  /* Strip quote marks, where appropriate */
  if ((fname[0] == '"') && (fname[strlen(fname)-1] == '"')) {
    fname[strlen(fname)-1] = '\0';
    fname++;
  }

  placeholder=SDL_LoadBMP(fname);
  if(!placeholder) {
    error(ERR_CANTREAD);
  } else {
    SDL_BlitSurface(placeholder, NULL, screenbank[writebank], NULL);
    if (displaybank == writebank) {
      SDL_BlitSurface(placeholder, NULL, matrixflags.surface, NULL);
      SDL_Flip(matrixflags.surface);
    }
    SDL_FreeSurface(placeholder);
  }
}

void swi_swap16palette() {
  Uint8 place = 0;
  int ptr = 0;
  if (colourdepth != 16) return;
  for (ptr=0; ptr < 24; ptr++) {
    place=palette[ptr];
    palette[ptr]=palette[ptr+24];
    palette[ptr+24]=place;
  }
  set_rgb();
}

/* Only a few flags are relevant to the emulation in Brandy */
static int32 getmodeflags(int32 scrmode) {
  int32 flags=0;
  if ((scrmode == 3) || (scrmode == 6) || (scrmode == 7)) flags |=1;
  if (scrmode == 7) flags |= 6;
  if ((scrmode == 3) || (scrmode == 6)) flags |=12;
  return flags;
}
static int32 mode_divider(int32 scrmode) {
  switch (modetable[scrmode].coldepth) {
    case 2:	   return 32;
    case 4:	   return 16;
    case 16:	   return 8;
    case 256:	   return 4;
    case COL15BIT: return 2;
    default:	   return 1;
  }
}
static int32 log2bpp(int32 scrmode) {
  switch (modetable[scrmode].coldepth) {
    case 2:	   return 0;
    case 4:	   return 1;
    case 16:	   return 2;
    case 256:	   return 3;
    case COL15BIT: return 4;
    default:	   return 5;
  }
}
/* Using values returned by RISC OS 3.7 */
int32 readmodevariable(int32 scrmode, int32 var) {
  int tmp=0;
  if (scrmode == -1) scrmode = screenmode;
  switch (var) {
    case 0:	return (getmodeflags(scrmode));
    case 1:	return (modetable[scrmode].xtext-1);
    case 2:	return (modetable[scrmode].ytext-1);
    case 3:
      tmp=modetable[scrmode].coldepth;
      if (tmp==256) tmp=64;
      if (tmp==COL15BIT) tmp=65536;
      if (tmp==COL24BIT) tmp=0;
      return tmp-1;
    case 4:	return (modetable[scrmode].xscale);
    case 5:	return (modetable[scrmode].yscale);
    case 6:	return (modetable[scrmode].xres * 4 / mode_divider(scrmode));
    case 7:	return (modetable[scrmode].xres * modetable[scrmode].yres * 4 / mode_divider(scrmode));
    case 9:	/* Fall through to 10 */
    case 10:	return (log2bpp(scrmode));
    case 11:	return (modetable[scrmode].xres-1);
    case 12:	return (modetable[scrmode].yres-1);
    case 128: /* GWLCol */	return gwinleft / xgupp;
    case 129: /* GWBRow */	return gwinbottom / ygupp;
    case 130: /* GWRCol */	return gwinright / xgupp;
    case 131: /* GWTRow */	return gwintop / ygupp;
    case 132: /* TWLCol */	return twinleft;
    case 133: /* TWBRow */	return twinbottom;
    case 134: /* TWRCol */	return twinright;
    case 135: /* TWTRow */	return twintop;
    case 136: /* OrgX */	return xorigin;
    case 137: /* OrgY */	return yorigin;
    case 153: /* GFCOL */	return graph_forecol;
    case 154: /* GBCOL */	return graph_backcol;
    case 155: /* TForeCol */	return text_forecol;
    case 156: /* TBackCol */	return text_backcol;
    case 157: /* GFTint */	return graph_foretint;
    case 158: /* GBTint */	return graph_backtint;
    case 159: /* TFTint */	return text_foretint;
    case 160: /* TBTint */	return text_backtint;
    case 161: /* MaxMode */	return HIGHMODE;
    default:	return 0;
  }
}
