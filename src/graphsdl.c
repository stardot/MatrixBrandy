/*
** This file is part of the Matrix Brandy Basic VI Interpreter.
** Copyright (C) 2000-2014 David Daniels
** Copyright (C) 2018-2024 Michael McConnell and contributors
**
** SDL additions by Colin Tuckley,
**     heavily modified by Michael McConnell.
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
**      This file contains the VDU driver emulation for the interpreter
**      used when graphics output is possible. It uses the SDL 1.2 graphics library.
**
**      MODE 7 implementation by Michael McConnell.
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
#if defined(TARGET_UNIX) && defined(USE_X11)
#include <X11/Xlib.h>
#endif
#include "errors.h"
#include "basicdefs.h"
#include "scrcommon.h"
#include "screen.h"
#include "mos.h"
#include "keyboard.h"
#include "graphsdl.h"
#include "miscprocs.h"
#include "textfonts.h"
#include "iostate.h"

#ifdef TARGET_MACOSX
#if SDL_PATCHLEVEL < 16
#error "Latest snapshot from SDL 1.2 mercurial required for MacOS X, suitable tarball available at http://brandy.matrixnetwork.co.uk/testing/SDL-1.2.16pre-20200707.tar.bz2"
#endif
#endif /* MACOSX */
/*
** Notes
** -----
**  This is one of the four versions of the VDU driver emulation.
**  It is used by versions of the interpreter where graphics are
**  supported as well as text output using the SDL library.
**  The four versions of the VDU driver code are in:
**      riscos.c
**      graphsdl.c
**      textonly.c
**      simpletext.c
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
**  53 (the RISC OS 3.7 modes). Both colour and grey scale graphics
**  are possible. Effectively this code supports RISC OS 3.7
**  (Risc PC) graphics with some small extensions.
**
**  The graphics library that was originally used, jlib, limited
**  the range of RISC OS graphics facilities supported quite considerably.
**  The new graphics library SDL overcomes some of those restrictions
**  although some new geometric routines have had to be added.
**
**  To display the graphics the virtual screen has to be copied to
**  the real screen. The code attempts to optimise this by only
**  copying areas of the virtual screen that have changed.
*/

#define MAX_YRES 16384
#define MAX_XRES 16384
#define FAST_2_MUL(x) ((x)<<1)
#define FAST_3_MUL(x) (((x)<<1)+x)
#define FAST_4_MUL(x) ((x)<<2)
#define FAST_4_DIV(x) ((x)>>2)

#define is_teletextctrl(x) ((x >= 0x80) && (x <= 0x9F))
#define vduflag(flags) ((vduflags & flags) ? 1 : 0)

#ifdef TARGET_MACOSX
#define SWAPENDIAN(x) (((x>>24)&0xFF)|((x<<8)&0xFF0000)|((x>>8)&0xFF00)|((x<<24)&0xFF000000))
#else
#define SWAPENDIAN(x) x
#endif

/*
** These two macros are used to convert from RISC OS graphics coordinates to
** pixel coordinates
*/
#define GXTOPX(x) ((x) / ds.xgupp)
#define GYTOPY(y) (((32768 + ds.ygraphunits - 1 -(y)) / ds.ygupp) - (32768 / ds.ygupp))

#define MOVFLAG ((vdu2316byte & 14) >> 1)
#define SCROLLPROT (vdu2316byte & 1)

/* Size of character in pixels in X direction */
#define XPPC 8
/* Size of character in pixels in Y direction - this can change in a few modes e.g. 3 and 6 */
static unsigned int YPPC=8;
/* Size of Mode 7 characters in X direction */
#define M7XPPC 16
/* Size of Mode 7 characters in Y direction */
#define M7YPPC 20

/*
** SDL related defines, Variables and params
*/
static SDL_Surface *screenbank[MAXBANKS];
static SDL_Surface *screen1, *screen2, *screen3;
static SDL_Surface *sdl_fontbuf, *sdl_m7fontbuf;
#ifdef TARGET_MACOSX
static SDL_PixelFormat pixfmt;
#endif

static SDL_Rect font_rect, place_rect, line_rect, scale_rect;
#ifndef BRANDY_MODE7ONLY
static SDL_Rect scroll_rect;
#endif

static Uint8 palette[768];              /* palette for screen */
static Uint8 hardpalette[48]            /* palette for screen */
       = { 0, 0,  0,  255,0,  0,  0,255,  0,  255,255,  0, /* Black, Red, Green, Yellow */
           0, 0,255,  255,0,255,  0,255,255,  255,255,255, /* Blue, Magenta, Cyan, White */
          80,80, 80,  160,0,  0,  0,160,  0,  160,160,  0, /* Dimmer versions of the above */
           0, 0,160,  160,0,160,  0,160,160,  160,160,160 };


static Uint8 vdu2316byte = 1;           /* Byte set by VDU23,16. Defaults to Scroll Protect On.*/

mousequeue *mousebuffer = NULL;
uint32 mousequeuelength = 0;
uint32 mouseqexpire = 0;
#define MOUSEQUEUEMAX 7

#ifndef BRANDY_MODE7ONLY
static int32 geom_left[MAX_YRES], geom_right[MAX_YRES];

// Default Dot Patterns
static uint8_t DEFAULT_DOT_PATTERN[] = {0xAA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// Dot Pattern state
static byte dot_pattern[64];
static byte dot_pattern_packed[8];
static int  dot_pattern_len;
static int  dot_pattern_index;

#endif /* ! BRANDY_MODE7ONLY */

/* Data stores for controlling MODE 7 operation */
Uint8 mode7frame[26][40];               /* Text frame buffer for Mode 7, akin to BBC screen memory at &7C00. Extra row just to be safe */
Uint8 mode7cloneframe[26][40];
static int64 mode7timer = 0;            /* Timer for bank switching */
static Uint8 vdu141track[27];           /* Track use of Double Height in Mode 7 *
                                         * First line is [1] */
threadmsg tmsg;

int32 titlestringLen = 256;
char titlestring[256];

/* The "virtual screen" below is the size of the display in uniquely addressable pixels,
   for example MODE 0 is 640x256, MODE 2 is 160x256 and MODE 27 is 640x480 - see scrcommon.h */

static struct {
  int32 vscrwidth;                      /* Width of virtual screen in pixels */
  int32 vscrheight;                     /* Height of virtual screen in pixels */
  int32 screenwidth;                    /* RISC OS width of current screen mode in pixels */
  int32 screenheight;                   /* RISC OS height of current screen mode in pixels */
  int32 xgraphunits;                    /* Screen width in RISC OS graphics units */
  int32 ygraphunits;                    /* Screen height in RISC OS graphics units */
  int32 gwinleft;                       /* Left coordinate of graphics window in RISC OS graphics units */
  int32 gwinright;                      /* Right coordinate of graphics window in RISC OS graphics units */
  int32 gwintop;                        /* Top coordinate of graphics window in RISC OS graphics units */
  int32 gwinbottom;                     /* Bottom coordinate of graphics window in RISC OS graphics units */
  int32 xgupp;                          /* RISC OS graphic units per pixel in X direction */
  int32 ygupp;                          /* RISC OS graphic units per pixel in Y direction */
  int32 graph_fore_action;              /* Foreground graphics PLOT action */
  int32 graph_back_action;              /* Background graphics PLOT action (ignored) */
  int32 graph_forecol;                  /* Current graphics foreground logical colour number */
  int32 graph_backcol;                  /* Current graphics background logical colour number */
  int32 graph_forelog;                  /* Current graphics foreground palette colour number */
  int32 graph_backlog;                  /* Current graphics background palette colour number */
  int32 graph_physforecol;              /* Current graphics foreground physical colour number */
  int32 graph_physbackcol;              /* Current graphics background physical colour number */
  int32 graph_foretint;                 /* Tint value added to foreground graphics colour in 256 colour modes */
  int32 graph_backtint;                 /* Tint value added to background graphics colour in 256 colour modes */
  int32 plot_inverse;                   /* PLOT in inverse colour? */
  int32 xlast;                          /* Graphics X coordinate of last point visited */
  int32 ylast;                          /* Graphics Y coordinate of last point visited */
  int32 xlast2;                         /* Graphics X coordinate of last-but-one point visited */
  int32 ylast2;                         /* Graphics Y coordinate of last-but-one point visited */
  int32 xlast3;                         /* Graphics X coordinate of last-but-two point visited */
  int32 ylast3;                         /* Graphics Y coordinate of last-but-two point visited */
  int32 xorigin;                        /* X coordinate of graphics origin */
  int32 yorigin;                        /* Y coordinate of graphics origin */
  int32 xscale;                         /* X direction scale factor */
  int32 yscale;                         /* Y direction scale factor */
  int32 autorefresh;                    /* Refresh screen on updates? */
  uint32 tf_colour;                     /* text foreground SDL rgb triple */
  uint32 tb_colour;                     /* text background SDL rgb triple */
  uint32 gf_colour;                     /* graphics foreground SDL rgb triple */
  uint32 gb_colour;                     /* graphics background SDL rgb triple */
  uint32 displaybank;                   /* Video bank to be displayed */
  uint32 writebank;                     /* Video bank to be written to */
  uint32 xor_mask;                      
  int64 videorefresh;                   /* Centisecond reference of when screen was last updated */
  boolean scaled;                       /* TRUE if screen mode is scaled to fit real screen */
  boolean clipping;                     /* TRUE if clipping region is not full screen of a RISC OS mode */
} ds;

/*
** function definitions
*/

static void scroll_up(int32 windowed);
static void scroll_down(int32 windowed);
static void scroll_left(int32 windowed);
static void scroll_right(int32 windowed);
static void scroll_xpos(int32 windowed);
static void scroll_xneg(int32 windowed);
static void scroll_ypos(int32 windowed);
static void scroll_yneg(int32 windowed);
//static void cursor_move_sp(int32 *curx, int32 *cury, int32 incx, int32 incy);
//static void cursor_move(int32 *curx, int32 *cury, int32 incx, int32 incy);
static void reveal_cursor(void);
#ifndef BRANDY_MODE7ONLY
static void plot_pixel(SDL_Surface *, int32, int32, Uint32, Uint32);
static void draw_line(SDL_Surface *, int32, int32, int32, int32, Uint32, int32, Uint32);
static void filled_triangle(SDL_Surface *, int32, int32, int32, int32, int32, int32, Uint32, Uint32);
static void draw_ellipse(SDL_Surface *, int32, int32, int32, int32, int32, Uint32, Uint32);
static void draw_arc(SDL_Surface *, int32, int32, float, float, int32, int32, int32, int32, Uint32, Uint32);
static void filled_ellipse(SDL_Surface *, int32, int32, int32, int32, int32, Uint32, Uint32);
static void set_text_colour(boolean background, int colnum);
static void set_graphics_colour(boolean background, int colnum);
#endif
static void toggle_cursor(void);
static void vdu_cleartext(void);
static void mode7renderline(int32 ypos, int32 fast);

static void write_vduflag(unsigned int flags, int yesno) {
  vduflags = yesno ? vduflags | flags : vduflags & ~flags;
}

static void reset_mode7() {
  int p, q;
  mode7timer=0;
  place_rect.h=M7YPPC;
  font_rect.h=M7YPPC;

  for(p=0;p<26;p++) vdu141track[p]=0;
  for (p=0; p<25; p++) {
    for (q=0; q<40; q++) mode7frame[p][q]=32;
  }
}

void add_mouseitem(int x, int y, int b, int64 c) {
  mousequeue *m, *p;
  
  /* Drop any item that would make the queue length too long */
  if ((mouseqexpire == 0) && (mousequeuelength >= MOUSEQUEUEMAX)) return;
  
  m=malloc(sizeof(mousequeue));
  if (m == NULL) {
    fprintf(stderr,"Unable to allocate memory for mouse queue item\n");
    return;
  }
  m->x = x;
  m->y = y;
  m->buttons = b;
  m->timestamp = basicvars.centiseconds;
  m->next=NULL;

  if (mousebuffer == NULL) {
    mousebuffer=m;
  } else {
  /* If we got here, then we already have entries in the queue.
  ** We need to step through and find the last one.
  */
    p = mousebuffer;
    while (p->next != NULL) p = p->next;
    p->next=m;
  }
  mousequeuelength++;
}

void drain_mousebuffer() {
  mousequeue *p;
  while (mousebuffer != NULL) {
    p=mousebuffer->next;
    free(mousebuffer);
    mousebuffer=p;
    mousequeuelength--;
  }
}

void set_mouseevent_expiry(uint32 expire) {
  mouseqexpire=expire;
}

static void drain_mouse_expired() {
  mousequeue *p;
  if (mouseqexpire == 0) return;
  while (mousebuffer != NULL) {
    if ((mousebuffer->timestamp + mouseqexpire) > basicvars.centiseconds) break;
    p=mousebuffer->next;
    free(mousebuffer);
    mousebuffer=p;
    mousequeuelength--;
  }
}

static void ttxtcopychar(int32 in, int32 out, int32 srcbank) {
  int32 iloop = 0;
  for(iloop=0; iloop<20; iloop++) {
    mode7font[out-32][iloop]=mode7fontbanks[srcbank][in][iloop];
  }
}

static void saa505xregset(int32 set, int32 alt) {
  int32 iloop = 0;
  if (alt) alt=0x80;
  switch(set & 0x7F) {
    case 0: /* SAA5050 - British */
      ttxtcopychar(0xA3, alt+0x23, 0); 
      ttxtcopychar(0x8F, alt+0x5B, 0);
      ttxtcopychar(0xBD, alt+0x5C, 0);
      ttxtcopychar(0x90, alt+0x5D, 0);
      ttxtcopychar(0x8D, alt+0x5E, 0);
      ttxtcopychar(0x23, alt+0x5F, 0);
      ttxtcopychar(0x96, alt+0x60, 0);
      ttxtcopychar(0xBC, alt+0x7B, 0);
      ttxtcopychar(0x9D, alt+0x7C, 0);
      ttxtcopychar(0xBE, alt+0x7D, 0);
      ttxtcopychar(0xF7, alt+0x7E, 0);
      break;
    case 1: /* SAA5051 - German */
      ttxtcopychar(0xA7, alt+0x40, 0);
      ttxtcopychar(0xC4, alt+0x5B, 0);
      ttxtcopychar(0xD6, alt+0x5C, 0);
      ttxtcopychar(0xDC, alt+0x5D, 0);
      ttxtcopychar(0xB0, alt+0x60, 0);
      ttxtcopychar(0xE4, alt+0x7B, 0);
      ttxtcopychar(0xF6, alt+0x7C, 0);
      ttxtcopychar(0xFC, alt+0x7D, 0);
      ttxtcopychar(0xDF, alt+0x7E, 0);
      break;
    case 2: /* SAA5052 - Swedish */
      ttxtcopychar(0xA4, alt+0x24, 0);
      ttxtcopychar(0xC9, alt+0x40, 0);
      ttxtcopychar(0xC4, alt+0x5B, 0);
      ttxtcopychar(0xD6, alt+0x5C, 0);
      ttxtcopychar(0xC5, alt+0x5D, 0);
      ttxtcopychar(0xDC, alt+0x5E, 0);
      ttxtcopychar(0xE9, alt+0x60, 0);
      ttxtcopychar(0xE4, alt+0x7B, 0);
      ttxtcopychar(0xF6, alt+0x7C, 0);
      ttxtcopychar(0xE5, alt+0x7D, 0);
      ttxtcopychar(0xFC, alt+0x7E, 0);
      break;
    case 3: /* SAA5053 - Italian */
      ttxtcopychar(0xA3, alt+0x23, 0); 
      ttxtcopychar(0xE9, alt+0x40, 0);
      ttxtcopychar(0xB0, alt+0x5B, 0);
      ttxtcopychar(0xE7, alt+0x5C, 0);
      ttxtcopychar(0x90, alt+0x5D, 0);
      ttxtcopychar(0x8D, alt+0x5E, 0);
      ttxtcopychar(0x23, alt+0x5F, 0);
      ttxtcopychar(0xD9, alt+0x60, 0);
      ttxtcopychar(0xE0, alt+0x7B, 0);
      ttxtcopychar(0xD2, alt+0x7C, 0);
      ttxtcopychar(0xE8, alt+0x7D, 0);
      ttxtcopychar(0xEC, alt+0x7E, 0);
      break;
    case 4: /* SAA5054 - Belgian / French */
      ttxtcopychar(0xE9, alt+0x23, 0); 
      ttxtcopychar(0xEF, alt+0x24, 0); 
      ttxtcopychar(0xE0, alt+0x40, 0);
      ttxtcopychar(0xEB, alt+0x5B, 0);
      ttxtcopychar(0xEA, alt+0x5C, 0);
      ttxtcopychar(0xD9, alt+0x5D, 0);
      ttxtcopychar(0xEE, alt+0x5E, 0);
      ttxtcopychar(0x23, alt+0x5F, 0);
      ttxtcopychar(0xE8, alt+0x60, 0);
      ttxtcopychar(0xE2, alt+0x7B, 0);
      ttxtcopychar(0xF4, alt+0x7C, 0);
      ttxtcopychar(0xFB, alt+0x7D, 0);
      ttxtcopychar(0xE7, alt+0x7E, 0);
      break;
    case 5: /* SAA5055 - US-ASCII */
      /* Do nothing, that's the position after reset */
      break;
    case 6: /* SAA5056 - Hebrew */
      ttxtcopychar(0xA3, alt+0x23, 0); 
      ttxtcopychar(0x8F, alt+0x5B, 0);
      ttxtcopychar(0xBD, alt+0x5C, 0);
      ttxtcopychar(0x90, alt+0x5D, 0);
      ttxtcopychar(0x8D, alt+0x5E, 0);
      ttxtcopychar(0x23, alt+0x5F, 0);
      ttxtcopychar(0x9D, alt+0x7C, 0);
      ttxtcopychar(0xBE, alt+0x7D, 0);
      ttxtcopychar(0xF7, alt+0x7E, 0);
      for(iloop=0;iloop<=27;iloop++) ttxtcopychar(iloop,alt+iloop+0x60,0);
      break;
    case 7: /* SAA5057 - Cyrillic */
      for(iloop=0;iloop<=62;iloop++) ttxtcopychar(iloop,alt+iloop+0x40,1);
      ttxtcopychar(0x3F, alt+0x26,1);
      break;
  }
  /* Is bit 7 set? If so replace block with Euro */
  if (set & 0x80) ttxtcopychar(0x80, alt+0x7F, 0);
    else ttxtcopychar(0x81, alt+0x7F, 0);
}

static void saa505xregion(int32 pri, int32 alt) {
  int32 loop=0;
  /* First, reset the font to a known state. Pri = SAA5050, Alt = SAA5055 */
  for(loop=32; loop<127; loop++) {
    ttxtcopychar(loop,loop,0);
    ttxtcopychar(loop,128+loop,0);
  }
  saa505xregset(pri,0);
  saa505xregset(alt,1);
}

void reset_sysfont(int x) {
#ifndef BRANDY_MODE7ONLY
  int p, c, i;
#endif
  if (!x) {
#ifndef BRANDY_MODE7ONLY
    memcpy(sysfont, sysfontbase, sizeof(sysfont));
#endif
    saa505xregion(0,5);
    return;
  }
#ifndef BRANDY_MODE7ONLY
  if ((x>=1) && (x<= 7)) {
    p=(x-1)*32;
    for (c=0; c<= 31; c++) 
      for (i=0; i<= 7; i++) sysfont[p+c][i]=sysfontbase[p+c][i];
  }
  if (x ==8){
    for (c=0; c<=95; c++)
      for (i=0; i<= 7; i++) sysfont[c][i]=sysfontbase[c][i];
  }
#endif
  if (x == 16) {
    saa505xregion(0,5);
  }
}

static int istextonly(void) {
  return ((screenmode == 3 || screenmode == 6 || screenmode == 7));
}

#ifndef BRANDY_MODE7ONLY
#define riscoscolour(c) (((c & 0xFF) <<16) + (c & 0xFF00) + ((c & 0xFF0000) >> 16))

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

static void set_dot_pattern_len(int32 len);
static void set_dot_pattern(byte *pattern) {
  // Expand the pattern into one byte per pixel for efficient access
  byte *ptr = pattern;
  int i;
  uint8_t mask = 0x80;
  int last_dot = -1;
  for (i = 0; i < sizeof(dot_pattern); i++) {
    if (*ptr & mask) {
      dot_pattern[i] = 1;
      last_dot = i;
    } else {
      dot_pattern[i] = 0;
    }
    mask >>= 1;
    if (!mask) {
      mask = 0x80;
      ptr++;
    }
  }
  // Extend the pattern length if necessary
  // Note: this is non-standard behaviour
  last_dot++;
  if (last_dot > dot_pattern_len) {
    set_dot_pattern_len(last_dot);
  }
  for (i = 0; i < 8; i++) 
    dot_pattern_packed[i]=pattern[i];
}

static void set_dot_pattern_len(int32 len) {
  if (len == 0) {
    set_dot_pattern(DEFAULT_DOT_PATTERN);
    dot_pattern_len = 8;
  } else if (len <= 64) {
    dot_pattern_len = len;
  }
  dot_pattern_index = 0;
}


#endif /* ! BRANDY_MODE7ONLY */

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

static int32 gtextxhome(void) {
  if (vdu2316byte & 2) return ds.gwinright-XPPC*ds.xgupp+2;
  return ds.gwinleft;
}

static int32 gtextyhome(void) {
  if (vdu2316byte & 4) return ds.gwinbottom+YPPC*ds.ygupp-(ds.yscale);
  return ds.gwintop;
}

#if 0 /* Currently don't need these */
static int32 gtextxedge(void) {
  if (vdu2316byte & 2) return ds.gwinleft;
  return ds.gwinright-XPPC*ds.xgupp+2;
}

static int32 gtextyedge(void) {
  if (vdu2316byte & 4) return ds.gwintop;
  return ds.gwinbottom+YPPC*ds.ygupp;
}
#endif

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
    ds.tf_colour = SDL_MapRGB(sdl_fontbuf->format, (text_physforecol & 0xFF), ((text_physforecol & 0xFF00) >> 8), ((text_physforecol & 0xFF0000) >> 16));
    ds.tb_colour = SDL_MapRGB(sdl_fontbuf->format, (text_physbackcol & 0xFF), ((text_physbackcol & 0xFF00) >> 8), ((text_physbackcol & 0xFF0000) >> 16));
    ds.gf_colour = SDL_MapRGB(sdl_fontbuf->format, (ds.graph_physforecol & 0xFF), ((ds.graph_physforecol & 0xFF00) >> 8), ((ds.graph_physforecol & 0xFF0000) >> 16));
    ds.gb_colour = SDL_MapRGB(sdl_fontbuf->format, (ds.graph_physbackcol & 0xFF), ((ds.graph_physbackcol & 0xFF00) >> 8), ((ds.graph_physbackcol & 0xFF0000) >> 16));
  } else {
    j = text_physforecol*3;
    ds.tf_colour = SDL_MapRGB(sdl_fontbuf->format, palette[j], palette[j+1], palette[j+2]) + (text_forecol << 24);
    j = text_physbackcol*3;
    ds.tb_colour = SDL_MapRGB(sdl_fontbuf->format, palette[j], palette[j+1], palette[j+2]) + (text_backcol << 24);

    j = ds.graph_physforecol*3;
    ds.gf_colour = SDL_MapRGB(sdl_fontbuf->format, palette[j], palette[j+1], palette[j+2]) + (ds.graph_forecol << 24);
    j = ds.graph_physbackcol*3;
    ds.gb_colour = SDL_MapRGB(sdl_fontbuf->format, palette[j], palette[j+1], palette[j+2]) + (ds.graph_backcol << 24);
  }
#ifdef TARGET_MACOSX
  if(screenmode!=7) {
    ds.tf_colour = SWAPENDIAN(ds.tf_colour);
    ds.tb_colour = SWAPENDIAN(ds.tb_colour);
  }
#endif
}

static void vdu_2307(void) {
  int32 windowed;
  
  if (vduqueue[1] == 0) {
    windowed = 1;
  } else {
    windowed = 0;
  }
  switch (vduqueue[2]) {
    case 0: /* Scroll right */
      scroll_right(windowed);
      break;
    case 1: /* Scroll left */
      scroll_left(windowed);
      break;
    case 2: /* Scroll down */
      scroll_down(windowed);
      break;
    case 3: /* Scroll up */
      scroll_up(windowed);
      break;
    case 4: /* Scroll in positive X direction (default right) */
      scroll_xpos(windowed);
      break;
    case 5: /* Scroll in negative X direction (default left) */
      scroll_xneg(windowed);
      break;
    case 6: /* Scroll in positive Y direction (default down) */
      scroll_ypos(windowed);
      break;
    case 7: /* Scroll in negative Y direction (default up) */
      scroll_yneg(windowed);
      break;
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
** '
' deals with various flavours of the sequence VDU 23,17,...
*/
#ifndef BRANDY_MODE7ONLY
static void vdu_2317(void) {
  int32 temp;
  switch (vduqueue[1]) {        /* vduqueue[1] is the byte after the '17' and says what to change */
  case TINT_FORETEXT:
    text_foretint = (vduqueue[2] & TINTMASK)>>TINTSHIFT;        /* Third byte in queue is new TINT value */
    if (colourdepth==256) text_physforecol = (text_forecol<<COL256SHIFT)+text_foretint;
    if (colourdepth==COL24BIT) text_physforecol=tint24bit(text_forecol, text_foretint);
    break;
  case TINT_BACKTEXT:
    text_backtint = (vduqueue[2] & TINTMASK)>>TINTSHIFT;
    if (colourdepth==256) text_physbackcol = (text_backcol<<COL256SHIFT)+text_backtint;
    if (colourdepth==COL24BIT) text_physbackcol=tint24bit(text_backcol, text_backtint);
    break;
  case TINT_FOREGRAPH:
    ds.graph_foretint = (vduqueue[2] & TINTMASK)>>TINTSHIFT;
    if (colourdepth==256) ds.graph_physforecol = (ds.graph_forecol<<COL256SHIFT)+ds.graph_foretint;
    if (colourdepth==COL24BIT) ds.graph_physforecol=tint24bit(ds.graph_forecol, ds.graph_foretint);
    break;
  case TINT_BACKGRAPH:
    ds.graph_backtint = (vduqueue[2] & TINTMASK)>>TINTSHIFT;
    if (colourdepth==256) ds.graph_physbackcol = (ds.graph_backcol<<COL256SHIFT)+ds.graph_backtint;
    if (colourdepth==COL24BIT) ds.graph_physbackcol=tint24bit(ds.graph_backcol, ds.graph_backtint);
    break;
  case EXCH_TEXTCOLS:   /* Exchange text foreground and background colours */
    temp = text_forecol; text_forecol = text_backcol; text_backcol = temp;
    temp = text_physforecol; text_physforecol = text_physbackcol; text_physbackcol = temp;
    temp = text_foretint; text_foretint = text_backtint; text_backtint = temp;
    break;
  default:              /* Ignore bad value */
    break;
  }
  set_rgb();
}
#endif

/* RISC OS 5 - Set Teletext characteristics */
static void vdu_2318(void) {
  if (vduqueue[1] == 2) {
    write_vduflag(MODE7_REVEAL, vduqueue[2] & 1);
  }
  if (vduqueue[1] == 3) {
    write_vduflag(MODE7_BLACK, vduqueue[2] & 1);
  }
  if (vduqueue[1] == 4) {
    saa505xregion(vduqueue[2], vduqueue[3]);
  }
  tmsg.mode7forcerefresh=1;
}

/* BB4W/BBCSDL - Define and select custom mode */
/* Implementation not likely to be exact, char width and height are fixed in Brandy so are /8 to generate xscale and yscale */
#ifndef BRANDY_MODE7ONLY
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
#endif

/*
** 'vdu_23command' emulates some of the VDU 23 command sequences
*/
static void vdu_23command(void) {
#ifndef BRANDY_MODE7ONLY
  int codeval, n;
#endif
  switch (vduqueue[0]) {        /* First byte in VDU queue gives the command type */
  case 0:       /* More cursor stuff - this only handles VDU23;{8202,29194};0;0;0; */
    if (vduqueue[1] == 10) {
      if ((vduqueue[2] & 96) == 32) {
        hide_cursor();
        cursorstate = HIDDEN;   /* 0 = hide, 1 = show */
      } else {
        cursorstate = SUSPENDED;
        toggle_cursor();
        cursorstate = ONSCREEN;
      }
      tmsg.crtc6845r10 = vduqueue[2];
    }
    break;
  case 1:       /* Control the appear of the text cursor */
    if (vduqueue[1] == 0) {
      hide_cursor();
      cursorstate = HIDDEN;     /* 0 = hide, 1 = show */
    }
    if (vduqueue[1] == 1) cursorstate = ONSCREEN;
    else cursorstate = HIDDEN;
    break;
  case 5:
    break; /* ECF not supported */
#ifndef BRANDY_MODE7ONLY
  case 6:
    set_dot_pattern(vduqueue);
    break;
#endif
  case 7:       /* Scroll the screen or text window */
    vdu_2307();
    break;
  case 8:       /* Clear part of the text window */
    break;
  case 16:      /* Controls the movement of the cursor after printing */
    vdu_2316();
    break;
#ifndef BRANDY_MODE7ONLY
  case 17:      /* Set the tint value for a colour in 256 colour modes, etc */
    vdu_2317();
    break;
#endif
  case 18:      /* RISC OS 5 set Teletext characteristics */
    vdu_2318();
    break;
#ifndef BRANDY_MODE7ONLY
  case 22:      /* BB4W/BBCSDL Custom Mode */
    vdu_2322();
    break;
  default:
    codeval = vduqueue[0] & 0x00FF;
    if ((codeval < 32) || (codeval == 127)) break;   /* Ignore unhandled commands */
    /* codes 32 to 255 are user-defined character setup commands */
    for (n=0; n < 8; n++) sysfont[codeval-32][n] = vduqueue[n+1];
#endif
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
  int32 left, right, x, y, mxppc, myppc, xtemp = xtext, startpt, ysc = ds.yscale, ys, curheight = 0;
  int32 csroffset = (ds.yscale)-1;
  if (
        vduflag(VDU_FLAG_GRAPHICURS)                                /* Never display the cursor in VDU5 mode */
  ||
        ((cursorstate != SUSPENDED) && (cursorstate != ONSCREEN))   /* Cursor is not being displayed so give up */
  ||
        (ds.autorefresh != 1)                                       /* We're not autoupdating (*Refresh Off or OnError) */
  ||
        (ds.displaybank != ds.writebank)                            /* *FX112 / 113 pointing at different banks */
  ||
        (matrixflags.cursorbusy)                                    /* Have we flagged the cursor as being busy? */
  ||
        (cursorstate == HIDDEN)
  ) return;

  if ((tmsg.crtc6845r10 & 96) == 32) {                              /* After VDU 23;8202;0;0;0; */
    cursorstate = HIDDEN;
    return;
  }

  if (screenmode==7) {
    mxppc=M7XPPC; myppc=M7YPPC;
  } else {
    mxppc=XPPC;   myppc=YPPC;
  }
  startpt = myppc;
  if (xtemp > twinright) xtemp=twinright;
  cursorstate = (cursorstate == ONSCREEN) ? SUSPENDED : ONSCREEN;       /* Toggle the cursor state */
  left = xtemp*ds.xscale*mxppc;                                 /* Calculate pixel coordinates of ends of cursor */
  right = left + ds.xscale*mxppc -1;
  if (cursmode == UNDERLINE) curheight=(tmsg.crtc6845r10 & 31);
  y = ((ytext+1)*ds.yscale*myppc - ds.yscale) * ds.vscrwidth;
  while (startpt > curheight) {
    for (ys=0; ys < ysc; ys++) {
      for (x=left; x <= right; x++) {
        if ((x + y + (ds.vscrwidth * csroffset)) >= 0 && (x + y + (ds.vscrwidth * csroffset)) < (ds.vscrwidth * ds.vscrheight)) {      /* Prevent offscreen drawing */
          *((Uint32*)matrixflags.surface->pixels + x + y + (ds.vscrwidth * csroffset)) ^= SWAPENDIAN(ds.xor_mask);
        }
      }
      csroffset--;
    }
    startpt--;
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
** enlarged form to the main screen buffer.
** (x1, y1) and (x2, y2) define the rectangle to be displayed. These
** are given in terms of what could be called the pseudo pixel
** coordinates of the buffer. These pseudo pixel coordinates are
** converted to real pixel coordinates by multiplying them by 'xscale'
** and 'yscale'.
*/
#ifndef BRANDY_MODE7ONLY
static void blit_scaled_actual(int32 left, int32 top, int32 right, int32 bottom) {
  int32 xx, yy;
/*
** Start by clipping the rectangle to be blit'ed if it extends off the
** screen.
** Note that 'screenwidth' and 'screenheight' give the dimensions of the
** RISC OS screen mode in pixels
*/
  if(screenmode == 7) return;
  if (right < 0 || bottom < 0 || left >= ds.screenwidth || top >= ds.screenheight) return;      /* Is off screen completely */
  if (left < 0) left = 0;                               /* Clip the rectangle as necessary */
  if (right >= ds.screenwidth) right = ds.screenwidth-1;
  if (top < 0) top = 0;
  if (bottom >= ds.screenheight) bottom = ds.screenheight-1;
  if (!ds.scaled) {
    if ((top == 0) && (left == 0) && (right == ds.screenwidth-1) && (bottom == ds.screenheight-1)) {
      /* Special high-speed memory copy for full-screen non-scaled blits */
      uint64 *dptr, *sptr, lptr, scrsz;
      sptr = screenbank[ds.displaybank]->pixels;
      dptr = matrixflags.surface->pixels;
      scrsz = (ds.screenwidth*ds.screenheight)/2;
      for (lptr = 0; lptr < scrsz; lptr++) *(dptr++) = *(sptr++);
    } else {
      for (xx=left; xx <= right; xx++) {
        for (yy=top; yy <= bottom; yy++) {
          int32 pxoffset = xx + yy*ds.vscrwidth;
          *((Uint32*)matrixflags.surface->pixels + pxoffset) = *((Uint32*)screenbank[ds.displaybank]->pixels + pxoffset);
        }
      }
    }
  } else {
    int32 dleft = left*ds.xscale;                               /* Calculate pixel coordinates in the */
    int32 dtop  = top*ds.yscale;                                /* screen buffer of the rectangle */
    int32 i, j, ii, jj;

    yy = dtop;
    for (j = top; j <= bottom; j++) {
      for (jj = 1; jj <= ds.yscale; jj++) {
        xx = dleft;
        for (i = left; i <= right; i++) {
          for (ii = 1; ii <= ds.xscale; ii++) {
            *((Uint32*)matrixflags.surface->pixels + xx + yy*ds.vscrwidth) = *((Uint32*)screenbank[ds.displaybank]->pixels + i + j*ds.vscrwidth);
            xx++;
          }
        }
        yy++;
      } 
    }
    if ((screenmode == 3) || (screenmode == 6)) {       /* Paint on the black bars over the background */
      int p;
      hide_cursor();
      for (p=0; p<25; p++) {
        yy=16+(p*20);
        memset(matrixflags.surface->pixels + 4*yy*ds.vscrwidth, 0, 16*ds.screenwidth*ds.xscale);
      }
    }
  }
}

static void blit_scaled(int32 left, int32 top, int32 right, int32 bottom) {
  if ((ds.autorefresh != 1) || (ds.displaybank != ds.writebank)) return;
  blit_scaled_actual(left, top, right, bottom);
}
#endif

#define COLOURSTEP 68           /* RGB colour value increment used in 256 colour modes */
#define TINTSTEP 17             /* RGB colour value increment used for tints */

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
#ifndef BRANDY_MODE7ONLY
  case 2:       /* Two colour - Black and white only */
    palette[0] = palette[1] = palette[2] = 0;
    palette[3] = palette[4] = palette[5] = 255;
    break;
  case 4:       /* Four colour - Black, red, yellow and white */
    palette[0] =      palette[1]  =      palette[2]  = 0;       /* Black */
    palette[3] = 255; palette[4]  =      palette[5]  = 0;       /* Red */
    palette[6] =      palette[7]  = 255; palette[8]  = 0;       /* Yellow */
    palette[9] =      palette[10] =      palette[11] = 255;     /* White */
    break;
  case 8:       /* Eight colour */
    memcpy(palette, hardpalette, 24);
    break;
#endif
  case 16:      /* Sixteen colour */
    memcpy(palette, hardpalette, 48);
    break;
#ifndef BRANDY_MODE7ONLY
  case 256:
  case COL24BIT: {      /* >= 256 colour */
    int red, green, blue, tint, colour;
/*
** The colour number in 256 colour modes can be seen as a bit map as
** follows:
**      bb gg rr tt
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
#endif
  default:      /* 32K colour modes are not supported */
    error(ERR_UNSUPPORTED);
    return;
  }
#ifndef BRANDY_MODE7ONLY
  if (colourdepth >= 256) {
    text_physforecol = (text_forecol<<COL256SHIFT)+text_foretint;
    text_physbackcol = (text_backcol<<COL256SHIFT)+text_backtint;
    ds.graph_physforecol = (ds.graph_forecol<<COL256SHIFT)+ds.graph_foretint;
    ds.graph_physbackcol = (ds.graph_backcol<<COL256SHIFT)+ds.graph_backtint;
  }
  else {
    text_physforecol = text_forecol;
    text_physbackcol = text_backcol;
    ds.graph_physforecol = ds.graph_forecol;
    ds.graph_physbackcol = ds.graph_backcol;
  }
#endif
  set_rgb();
}

/*
** 'change_palette' is called to change the palette entry for physical
** colour 'colour' to the colour defined by the RGB values red, green
** and blue. The screen is updated by this call
*/
#ifndef BRANDY_MODE7ONLY
static void change_palette(int32 colour, int32 red, int32 green, int32 blue) {
  palette[colour*3+0] = red;    /* The palette is not structured */
  palette[colour*3+1] = green;
  palette[colour*3+2] = blue;
}
#endif

/*
 * emulate_colourfn - This performs the function COLOUR(). It
 * Returns the entry in the palette for the current screen mode
 * that most closely matches the colour with red, green and
 * blue components passed to it.
 */
int32 emulate_colourfn(int32 red, int32 green, int32 blue) {
#ifndef BRANDY_MODE7ONLY
  int32 n, distance = 0x7fffffff;
  int32 best = 0;

  if (colourdepth == COL24BIT) return (red + (green << 8) + (blue << 16));
  for (n = 0; n < colourdepth && distance != 0; n++) {
    int32 dr = palette[n * 3 + 0] - red;
    int32 dg = palette[n * 3 + 1] - green;
    int32 db = palette[n * 3 + 2] - blue;
    int32 test = 2 * dr * dr + 4 * dg * dg + db * db;
    if (test < distance) {
      distance = test;
      best = n;
    }
  }
  if (colourdepth == 256) best = best >> COL256SHIFT;
  return best;
#else
  return 0;
#endif
}

/*
 * set_text_colour - Set either the text foreground colour
 * or the background colour to the supplied colour number
 * (palette entry number). This is used when a colour has
 * been matched with an entry in the palette via COLOUR()
 */
#ifndef BRANDY_MODE7ONLY
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
    ds.graph_physbackcol = ds.graph_backlog = ds.graph_backcol = (colnum & (colourdepth - 1));
  else {
    ds.graph_physforecol = ds.graph_forelog = ds.graph_forecol = (colnum & (colourdepth - 1));
  }
  ds.graph_fore_action = ds.graph_back_action = 0;
  set_rgb();
}
#endif /* BRANDY_MODE7ONLY */

/*
** 'scroll' scrolls the graphics screen up or down by the number of
** rows equivalent to one line of text on the screen. Depending on
** the RISC OS mode being used, this can be either eight or sixteen
** rows. 'direction' says whether the screen is moved up or down.
** The screen is redrawn by this call
*/

/* Let's try to pick apart this scroll monolith into its constituent parts.
** It does make for a bit of duplicated code, but should make it easier
** to follow what's going on in each direction.
*/
static void scroll_up_mode7(int32 windowed) {
  int m, n, t, b, l, r;
  if (windowed) {
    t=twintop;
    b=twinbottom;
    l=twinleft;
    r=twinright;
  } else {
    t=l=0;
    b=24;
    r=39;
  }
  /* Scroll the Mode 7 text buffer */
  for (m=t+1; m<=b; m++) {
    for (n=l; n<=r; n++) mode7frame[m-1][n] = mode7frame[m][n];
  }
  /* Blank the bottom line */
  for (n=l; n<=r; n++) mode7frame[b][n] = 32;
}

static void scroll_up(int32 windowed) {
#ifndef BRANDY_MODE7ONLY
  int left, right, dest, topwin;
  if (screenmode == 7) { 
#endif
    scroll_up_mode7(windowed);
#ifndef BRANDY_MODE7ONLY
    return;
  }
  toggle_cursor();
  if (windowed) {
    topwin = twintop*YPPC;                              /* Y coordinate of top of text window */
    dest = twintop*YPPC;                                /* Move screen up to this point */
    left = twinleft*XPPC;
    right = twinright*XPPC+XPPC-1;
    scroll_rect.x = left;
    scroll_rect.y = YPPC * (twintop + 1);
    scroll_rect.w = XPPC * (twinright - twinleft +1);
    scroll_rect.h = YPPC * (twinbottom - twintop);
    SDL_BlitSurface(screenbank[ds.writebank], &scroll_rect, screen1, NULL);
    line_rect.x = 0;
    line_rect.y = YPPC * (twinbottom - twintop);
    line_rect.w = XPPC * (twinright - twinleft +1);
    line_rect.h = YPPC;
    SDL_FillRect(screen1, &line_rect, ds.tb_colour);
    line_rect.x = line_rect.y = 0;
    line_rect.w = XPPC * (twinright - twinleft +1);
    line_rect.h = YPPC * (twinbottom - twintop +1);
    scroll_rect.x = left;
    scroll_rect.y = dest;
    SDL_BlitSurface(screen1, &line_rect, screenbank[ds.writebank], &scroll_rect);
    blit_scaled(left, topwin, right, twinbottom*YPPC+YPPC-1);
  } else {
    int loop;
    int top = 4*ds.screenwidth*YPPC*ds.xscale;            /* First, get size of one line. */
    /* Screen size minus size of one line (calculated above) */
    dest=(ds.screenwidth * ds.screenheight * 4 * ds.xscale) - top;
    memmove((byte *)screenbank[ds.writebank]->pixels, (const byte *)(screenbank[ds.writebank]->pixels)+top, dest);

    memmove((byte *)matrixflags.surface->pixels, (const byte *)(matrixflags.surface->pixels)+(top*ds.yscale), dest*ds.yscale);
    /* Need to do it this way, as memset() works on bytes only */
    for (loop=0;loop<top;loop+=4) {
      *(uint32 *)(screenbank[ds.writebank]->pixels+dest+loop) = SWAPENDIAN(ds.tb_colour);
    }
    for (loop=0;loop<(top*ds.yscale);loop+=4) {
      *(uint32 *)(matrixflags.surface->pixels+(dest*ds.yscale)+loop) = SWAPENDIAN(ds.tb_colour);
    }
  }
  toggle_cursor();
#endif
}

static void scroll_down_mode7(int32 windowed) {
  int m, n, t, b, l, r;
  if (windowed) {
    t=twintop;
    b=twinbottom;
    l=twinleft;
    r=twinright;
  } else {
    t=l=0;
    b=24;
    r=39;
  }
  /* Scroll the Mode 7 text buffer */
  for (m=b-1; m>=t; m--) {
    for (n=l; n<=r; n++) mode7frame[m+1][n] = mode7frame[m][n];
  }
  /* Blank the top line */
  for (n=l; n<=r; n++) mode7frame[t][n] = 32;
}

static void scroll_down(int32 windowed) {
#ifndef BRANDY_MODE7ONLY
  int left, right, top, dest, topwin;
  if (screenmode == 7) { 
#endif
    scroll_down_mode7(windowed);
#ifndef BRANDY_MODE7ONLY
    return;
  }
  toggle_cursor();
  if (windowed) {
    topwin = twintop*YPPC;              /* Y coordinate of top of text window */
    dest = (twintop)*YPPC;
    left = twinleft*XPPC;
    right = (twinright+1)*XPPC-1;
    top = twintop*YPPC;
    scroll_rect.x = left;
    scroll_rect.y = top;
    scroll_rect.w = XPPC * (twinright - twinleft +1);
    scroll_rect.h = YPPC * (twinbottom - twintop);
    line_rect.x = 0;
    line_rect.y = YPPC;
    SDL_BlitSurface(screenbank[ds.writebank], &scroll_rect, screen1, &line_rect);
    line_rect.x = line_rect.y = 0;
    line_rect.w = XPPC * (twinright - twinleft +1);
    line_rect.h = YPPC;
    SDL_FillRect(screen1, &line_rect, ds.tb_colour);
    line_rect.x = line_rect.y = 0;
    line_rect.w = XPPC * (twinright - twinleft +1);
    line_rect.h = YPPC * (twinbottom - twintop +1);
    scroll_rect.x = left;
    scroll_rect.y = dest;
    SDL_BlitSurface(screen1, &line_rect, screenbank[ds.writebank], &scroll_rect);
    blit_scaled(left, topwin, right, twinbottom*YPPC+YPPC-1);
  } else {
    int loop;
    /* First, get size of one line. */
    top=4*ds.screenwidth*YPPC*ds.xscale;
    /* Screen size minus size of one line (calculated above) */
    dest=(ds.screenwidth * ds.screenheight * 4 * ds.xscale) - top;
    memmove((byte *)screenbank[ds.writebank]->pixels+top, (const byte *)screenbank[ds.writebank]->pixels, dest);
    memmove((byte *)(matrixflags.surface->pixels)+(top*ds.yscale), (const byte *)matrixflags.surface->pixels, dest*ds.yscale);
    /* Need to do it this way, as memset() works on bytes only */
    for (loop=0;loop<top;loop+=4) {
      *(uint32 *)(screenbank[ds.writebank]->pixels+loop) = SWAPENDIAN(ds.tb_colour);
    }
    for (loop=0;loop<(top*ds.yscale);loop+=4) {
      *(uint32 *)(matrixflags.surface->pixels+loop) = SWAPENDIAN(ds.tb_colour);
    }
  }
  toggle_cursor();
#endif
}

static void scroll_left_mode7(int32 windowed) {
  int m, n, t, b, l, r;
  if (windowed) {
    t=twintop;
    b=twinbottom;
    l=twinleft;
    r=twinright;
  } else {
    t=l=0;
    b=24;
    r=39;
  }
  /* Scroll the Mode 7 text buffer */
  for (m=l+1; m<=r; m++) {
    for (n=t; n<=b; n++) mode7frame[n][m-1] = mode7frame[n][m];
  }
  /* Blank the right line */
  for (n=t; n<=b; n++) mode7frame[n][r] = 32;
}

static void scroll_left(int32 windowed) {
#ifndef BRANDY_MODE7ONLY
  int left, right, dest, topwin;
  if (screenmode == 7) { 
#endif
    scroll_left_mode7(windowed);
#ifndef BRANDY_MODE7ONLY
    return;
  }
  toggle_cursor();
  if (windowed) {
    topwin = twintop*YPPC;                              /* Y coordinate of top of text window */
    dest = twintop*YPPC;                                /* Move screen up to this point */
    left = twinleft*XPPC;
    right = twinright*XPPC+XPPC-1;
    scroll_rect.x = (twinleft+1)*XPPC;
    scroll_rect.y = YPPC * (twintop);
    scroll_rect.w = XPPC * (twinright - twinleft);
    scroll_rect.h = YPPC * (twinbottom - twintop +1);
    SDL_BlitSurface(screenbank[ds.writebank], &scroll_rect, screen1, NULL);
    line_rect.x = line_rect.y = 0;
    line_rect.w = XPPC * (twinright - twinleft +1);
    line_rect.h = YPPC * (twinbottom - twintop +1);
    scroll_rect.x = left;
    scroll_rect.y = dest;
    SDL_BlitSurface(screen1, &line_rect, screenbank[ds.writebank], &scroll_rect);
    line_rect.x = XPPC * (twinright-twinleft);
    line_rect.y = YPPC * twintop;
    line_rect.w = XPPC;
    line_rect.h = YPPC * (twinbottom - twintop +1);
    SDL_FillRect(screenbank[ds.writebank], &line_rect, ds.tb_colour);
    blit_scaled(left, topwin, right, twinbottom*YPPC+YPPC-1);
  } else {
    int lx, ly;
    int top = 4*XPPC;
    /* Screen size minus size of one line (calculated above) */
    dest=(ds.screenwidth * ds.screenheight * 4 * ds.xscale) - top;
    memmove((byte *)screenbank[ds.writebank]->pixels, (const byte *)(screenbank[ds.writebank]->pixels)+top, dest);
  
    for (ly=1; ly<=(ds.screenheight*ds.xscale); ly++) {
      for (lx=0; lx < (4*XPPC); lx+=4) {
        *(uint32 *)(screenbank[ds.writebank]->pixels+((ds.screenwidth)*4*ly)+lx-(4*XPPC)) = SWAPENDIAN(ds.tb_colour);
      }
    }
    blit_scaled(0, 0, ds.screenwidth, ds.screenheight);
  }
  toggle_cursor();
#endif
}

static void scroll_right_mode7(int32 windowed) {
  int m, n, t, b, l, r;
  if (windowed) {
    t=twintop;
    b=twinbottom;
    l=twinleft;
    r=twinright;
  } else {
    t=l=0;
    b=24;
    r=39;
  }
  /* Scroll the Mode 7 text buffer */
  for (m=r-1; m>=l; m--) {
    for (n=t; n<=b; n++) mode7frame[n][m+1] = mode7frame[n][m];
  }
  /* Blank the right line */
  for (n=t; n<=b; n++) mode7frame[n][l] = 32;
}

static void scroll_right(int32 windowed) {
#ifndef BRANDY_MODE7ONLY
  int left, right, dest, topwin;
  if (screenmode == 7) { 
#endif
    scroll_right_mode7(windowed);
#ifndef BRANDY_MODE7ONLY
    return;
  }
  toggle_cursor();
  if (windowed) {
    topwin = twintop*YPPC;                              /* Y coordinate of top of text window */
    dest = twintop*YPPC;                                /* Move screen up to this point */
    left = twinleft*XPPC;
    right = twinright*XPPC+XPPC-1;
    scroll_rect.x = (twinleft)*XPPC;
    scroll_rect.y = YPPC * (twintop);
    scroll_rect.w = XPPC * (twinright - twinleft);
    scroll_rect.h = YPPC * (twinbottom - twintop +1);
    SDL_BlitSurface(screenbank[ds.writebank], &scroll_rect, screen1, NULL);
    line_rect.x = 0;
    line_rect.y = 0;
    line_rect.w = XPPC * (twinright - twinleft +1);
    line_rect.h = YPPC * (twinbottom - twintop +1);
    scroll_rect.x = left+XPPC;
    scroll_rect.y = dest;
    SDL_BlitSurface(screen1, &line_rect, screenbank[ds.writebank], &scroll_rect);
    line_rect.x = left;
    line_rect.y = YPPC * twintop;
    line_rect.w = XPPC;
    line_rect.h = YPPC * (twinbottom - twintop +1);
    SDL_FillRect(screenbank[ds.writebank], &line_rect, ds.tb_colour);
    blit_scaled(left, topwin, right, twinbottom*YPPC+YPPC-1);
  } else {
    int lx, ly;
    int top = 4*XPPC;
    /* Screen size minus size of one line (calculated above) */
    dest=(ds.screenwidth * ds.screenheight * 4 * ds.xscale) - top;
    memmove((byte *)screenbank[ds.writebank]->pixels+top, (const byte *)screenbank[ds.writebank]->pixels, dest);
  
    for (ly=0; ly<=(ds.screenheight*ds.xscale)-1; ly++) {
      for (lx=0; lx < (4*XPPC); lx+=4) {
        *(uint32 *)(screenbank[ds.writebank]->pixels+((ds.screenwidth)*4*ly)+lx) = SWAPENDIAN(ds.tb_colour);
      }
    }
    blit_scaled(0, 0, ds.screenwidth, ds.screenheight);
  }
  toggle_cursor();
#endif
}

/*
** 'scroll' scrolls the graphics screen up or down by the number of
** rows equivalent to one line of text on the screen. Depending on
** the RISC OS mode being used, this can be either eight or sixteen
** rows. 'direction' says whether the screen is moved up or down.
** The screen is redrawn by this call
*/
static void scroll(updown direction) {
  if (direction == SCROLL_UP) { /* Shifting screen up */
    scroll_up(vduflag(VDU_FLAG_TEXTWIN));
  } else {      /* Shifting screen down */
    scroll_down(vduflag(VDU_FLAG_TEXTWIN));
  }
}

static void scroll_xpos(int32 windowed) {
  switch (MOVFLAG) {
    case 0: case 2: scroll_right(windowed); break;
    case 1: case 3: scroll_left(windowed); break;
    case 4: case 6: scroll_down(windowed); break;
    case 5: case 7: scroll_up(windowed); break;
  }
}

static void scroll_xneg(int32 windowed) {
  switch (MOVFLAG) {
    case 0: case 2: scroll_left(windowed); break;
    case 1: case 3: scroll_right(windowed); break;
    case 4: case 6: scroll_up(windowed); break;
    case 5: case 7: scroll_down(windowed); break;
  }
}

static void scroll_ypos(int32 windowed) {
  switch (MOVFLAG) {
    case 0: case 2: scroll_down(windowed); break;
    case 1: case 3: scroll_up(windowed); break;
    case 4: case 6: scroll_right(windowed); break;
    case 5: case 7: scroll_left(windowed); break;
  }
}

static void scroll_yneg(int32 windowed) {
  switch (MOVFLAG) {
    case 0: case 2: scroll_up(windowed); break;
    case 1: case 3: scroll_down(windowed); break;
    case 4: case 6: scroll_left(windowed); break;
    case 5: case 7: scroll_right(windowed); break;
  }
}

#if 0
static void cursor_move_sp(int32 *curx, int32 *cury, int32 incx, int32 incy) {
  int32 lx, ly;
  lx = *curx;
  ly = *cury;
  switch (MOVFLAG) {
    case 0:
      lx += incx;
      break;
  }
  *curx = lx;
  *cury = ly;
}

static void cursor_move(int32 *curx, int32 *cury, int32 incx, int32 incy) {
  int32 lx, ly;
  lx = *curx;
  ly = *cury;
  switch (MOVFLAG) {
    case 0:
      lx += incx;
      if (lx > twinright) {
        lx=twinleft;
        ly ++;
        if (*cury > twinbottom) {
          scroll(SCROLL_UP);
          ly --;
        }
      }
      if (lx < twinleft) {
        lx = twinright;
        ly --;
        if (ly < twintop) {
          scroll(SCROLL_DOWN);
          ly ++;
        }
      }
      break;
  }
  *curx = lx;
  *cury = ly;
}
#endif

/*
** 'write_char' draws a character when in fullscreen graphics mode
** when output is going to the text cursor. It assumes that the
** screen in is fullscreen graphics mode.
** The line or block representing the text cursor is overwritten
** by this code so the cursor state is automatically set to
** 'suspended' (if the cursor is being displayed)
*/
#ifndef BRANDY_MODE7ONLY
static void write_char(int32 ch) {
  int32 y, topx, topy, r;
  
  if (cursorstate == ONSCREEN) toggle_cursor();
  matrixflags.cursorbusy = 1;
  if (SCROLLPROT && ((xtext > twinright) || (xtext < twinleft))) {  /* Scroll before character if scroll protect enabled */
    xtext = textxhome();
    ytext+=textyinc();
    /* VDU14 check here */
    if (vduflag(VDU_FLAG_ENAPAGE)) {
      matrixflags.vdu14lines++;
      if (matrixflags.vdu14lines > (twinbottom-twintop)) {
        while (kbd_modkeys(1)==0 && kbd_escpoll()==0) usleep(5000);
        matrixflags.vdu14lines=0;
      }
    }
    if (ytext > twinbottom) {   /* Text cursor was on the last line of the text window */
      if (vdu2316byte & 16) {
        ytext=textyhome();
      } else {
        scroll(SCROLL_UP);      /* So scroll window up */
        ytext--;
      }
    }
    if (ytext < twintop) {      /* Text cursor was on the first line of the text window */
      if (vdu2316byte & 16) {
        ytext=textyedge();
      } else {
        scroll(SCROLL_DOWN);    /* So scroll window down */
        ytext++;
      }
    }
  }
  topx = xtext*XPPC;
  topy = ytext*YPPC;
  for (y=0; y < 8; y++) {
    int32 line = sysfont[ch-' '][y];
    for (r=0; r < 8; r++)
      *((Uint32*)screenbank[ds.writebank]->pixels + topx + r + ((topy+y)*ds.vscrwidth)) = (line & 1<<(7-r)) ? ds.tf_colour : ds.tb_colour;
  }
  blit_scaled(topx, topy, topx+XPPC-1, topy+YPPC-1);
  xtext+=textxinc();
  if (!SCROLLPROT && ((xtext > twinright) || (xtext < twinleft))) {  /* Scroll before character if scroll protect enabled */
    xtext = textxhome();
    ytext+=textyinc();
    /* VDU14 check here */
    if (vduflag(VDU_FLAG_ENAPAGE)) {
      matrixflags.vdu14lines++;
      if (matrixflags.vdu14lines > (twinbottom-twintop)) {
        while (kbd_modkeys(1)==0 && kbd_escpoll()==0) usleep(5000);
        matrixflags.vdu14lines=0;
      }
    }
    if (ytext > twinbottom) {   /* Text cursor was on the last line of the text window */
      if (vdu2316byte & 16) {
        ytext=textyhome();
      } else {
        scroll(SCROLL_UP);      /* So scroll window up */
        ytext--;
      }
    }
    if (ytext < twintop) {      /* Text cursor was on the first line of the text window */
      if (vdu2316byte & 16) {
        ytext=textyedge();
      } else {
        scroll(SCROLL_DOWN);    /* So scroll window down */
        ytext++;
      }
    }
  }
  matrixflags.cursorbusy = 0;
}

static void vdu5_cursorup(void) {
  switch (MOVFLAG) {
    case 0: case 1:
      if (ds.ylast > ds.gwintop)  ds.ylast = ds.gwinbottom+YPPC*ds.ygupp-1;
      break;
    case 2: case 3:
      if (ds.ylast < ds.gwinbottom) ds.ylast = ds.gwintop-1;
      break;
    case 4: case 6:
      if (ds.xlast < ds.gwinleft) ds.xlast = ds.gwinright-XPPC*ds.xgupp-1;
      break;
    case 5: case 7:
      if (ds.xlast > ds.gwinright) ds.xlast = ds.gwinleft;
      break;
  }
}

static void vdu5_cursordown(void) {
  switch (MOVFLAG) {
    case 0: case 1:
      ds.ylast -= YPPC*ds.ygupp*textyinc();
      if (ds.ylast < ds.gwinbottom) ds.ylast = ds.gwintop-1;
      break;
    case 2: case 3:
      ds.ylast -= YPPC*ds.ygupp*textyinc();
      if (ds.ylast > ds.gwintop)  ds.ylast = ds.gwinbottom+YPPC*ds.ygupp-1;
      break;
    case 4: case 6:
      ds.xlast += XPPC*ds.xgupp;
      if (ds.xlast > ds.gwinright) ds.xlast = ds.gwinleft;
      break;
    case 5: case 7:
      ds.xlast -= XPPC*ds.xgupp;
      if (ds.xlast < ds.gwinleft) ds.xlast = ds.gwinright-XPPC*ds.xgupp-1;
      break;
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
  int32 y, topx, topy, r;

  topx = GXTOPX(ds.xlast);              /* X and Y coordinates are those of the */
  topy = GYTOPY(ds.ylast);      /* top left-hand corner of the character */
  place_rect.x = topx;
  place_rect.y = topy;
  for (y=0; y<YPPC; y++) {
    int32 line;
    if ((topy+y) >= modetable[screenmode].yres) break;
    line = sysfont[ch-' '][y];
    if (line!=0) {
      for (r=0;r<8;r++)
        if (line & 1<<(7-r)) plot_pixel(screenbank[ds.writebank], (topx + r), (topy+y), ds.gf_colour, ds.graph_fore_action);
    }
  }
  blit_scaled(topx, topy, topx+XPPC-1, topy+YPPC-1);

  cursorstate = SUSPENDED; /* because we just overwrote it */
  if (!(vdu2316byte & 8)) ds.xlast += XPPC*ds.xgupp * textxinc();       /* Move to next character position in X direction */
  if ((vdu2316byte & 8)) ds.ylast -= YPPC*ds.ygupp * textyinc();        /* Move to next character position in X direction */
  if ((!(vdu2316byte & 64)) && (!(vdu2316byte & 8)) &&
    ((((vdu2316byte & 2) == 0) && (ds.xlast > ds.gwinright)) ||
     (((vdu2316byte & 2) == 2) && (ds.xlast < ds.gwinleft)))) { /* But position is outside the graphics window */
    ds.xlast = gtextxhome();
    vdu5_cursordown();
  }
  if ((!(vdu2316byte & 64)) && ((vdu2316byte & 8)) && 
    ((((vdu2316byte & 4) == 0) && (ds.ylast < ds.gwinbottom)) ||
     (((vdu2316byte & 4) == 4) && (ds.ylast > ds.gwintop)))) {  /* But position is outside the graphics window */
    ds.ylast = gtextyhome();
    vdu5_cursordown();
  }
}

static void plot_space_opaque(void) {
  int32 topx, topy;
  topx = GXTOPX(ds.xlast);              /* X and Y coordinates are those of the */
  topy = GYTOPY(ds.ylast);      /* top left-hand corner of the character */
  place_rect.x = topx;
  place_rect.y = topy;
  SDL_FillRect(sdl_fontbuf, NULL, ds.gb_colour);
  SDL_BlitSurface(sdl_fontbuf, &font_rect, screenbank[ds.writebank], &place_rect);
  blit_scaled(topx, topy, topx+XPPC-1, topy+YPPC-1);

  cursorstate = SUSPENDED; /* because we just overwrote it */
}
#endif /* BRANDY_MODE7ONLY */

/*
** 'move_cursor' sends the text cursor to the position (column, row)
** on the screen.  The function updates the cursor position as well.
** The column and row are given in RISC OS text coordinates, that
** is, (0,0) is the top left-hand corner of the screen. These values
** are the true coordinates on the screen. The code that uses this
** function has to allow for the text window.
*/
static void move_cursor(int32 column, int32 row) {
  hide_cursor();        /* Remove cursor */
  xtext = column;
  ytext = row;
  if (!(MOVFLAG & 4)) {
    if (MOVFLAG & 1)
      xtext = twinright + twinleft - column;
    else
      xtext = column;
    if (MOVFLAG & 2)
      ytext = twinbottom + twintop - row;
    else
      ytext = row;
  }
  reveal_cursor();      /* Redraw cursor */
}

/*
** 'set_cursor' sets the type of the text cursor used on the graphic
** screen to either a block or an underline. 'underline' is set to
** TRUE if an underline is required. Underline is used by the program
** when keyboard input is in 'insert' mode and a block when it is in
** 'overwrite'.
*/
void set_cursor(boolean underline) {
  hide_cursor();        /* Remove old style cursor */
  cursmode = underline ? UNDERLINE : BLOCK;
  reveal_cursor();      /* Draw new style cursor */
}

/*
** 'vdu_setpalette' changes one of the logical to physical colour map
** entries (VDU 19). When the interpreter is in full screen mode it
** can also redefine colours for in the palette.
*/
#ifndef BRANDY_MODE7ONLY
static void vdu_setpalette(void) {
  int32 logcol, pmode, mode;
  if (screenmode == 7) return;
  logcol = vduqueue[0] & colourmask;
  mode = vduqueue[1];
  pmode = mode % 16;
  if (mode < 16 && colourdepth <= 16) { /* Just change the RISC OS logical to physical colour mapping */
    logtophys[logcol] = mode;
    palette[logcol*3+0] = hardpalette[pmode*3+0];
    palette[logcol*3+1] = hardpalette[pmode*3+1];
    palette[logcol*3+2] = hardpalette[pmode*3+2];
  } else if (mode == 16)        /* Change the palette entry for colour 'logcol' */
    change_palette(logcol, vduqueue[2], vduqueue[3], vduqueue[4]);
  set_rgb();
  /* Now, go through the framebuffer and change the pixels */
  if (colourdepth <= 256) {
    int32 offset;
    int32 c = logcol * 3;
    int32 newcol = SDL_MapRGB(sdl_fontbuf->format, palette[c+0], palette[c+1], palette[c+2]) + (logcol << 24);
    for (offset=0; offset < (ds.screenheight*ds.screenwidth*ds.xscale); offset++) {
      if ((SWAPENDIAN(*((Uint32*)screenbank[ds.writebank]->pixels + offset)) >> 24) == logcol) *((Uint32*)screenbank[ds.writebank]->pixels + offset) = SWAPENDIAN(newcol);
    }
    blit_scaled(0,0,ds.screenwidth-1,ds.screenheight-1);
  }
}
#endif

/*
** 'move_down' moves the text cursor down a line within the text
** window, scrolling the window up if the cursor is on the last line
** of the window.
*/
static void move_down(void) {
  ytext+=textyinc();
  if (ytext > twinbottom) {     /* Cursor was on last line in window - Scroll window up */
    if (vdu2316byte & 16) {
      ytext=textyhome();
    } else {
      scroll(SCROLL_UP);        /* So scroll window up */
      ytext--;
    }
  }
  if (ytext < twintop) {        /* Cursor was on top line in window - Scroll window down */
    if (vdu2316byte & 16) {
      ytext=textyedge();
    } else {
      scroll(SCROLL_DOWN);      /* So scroll window down */
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
  if (ytext < twintop) {        /* Cursor was on top line in window - Scroll window down */
    if (vdu2316byte & 16) {
      ytext=textyedge();
    } else {
      scroll(SCROLL_DOWN);      /* So scroll window down */
      ytext++;
    }
  }
  if (ytext > twinbottom) {     /* Cursor was on last line in window - Scroll window up */
    if (vdu2316byte & 16) {
      ytext=textyhome();
    } else {
      scroll(SCROLL_UP);        /* So scroll window up */
      ytext--;
    }
  }
}

/*
** 'move_curback' moves the cursor back one character on the screen (VDU 8)
*/
static void move_curback(void) {
#ifndef BRANDY_MODE7ONLY
  if (vduflag(VDU_FLAG_GRAPHICURS)) {   /* VDU 5 mode - Move graphics cursor back one character */
    if (MOVFLAG & 4) {
      ds.ylast += YPPC*ds.ygupp*textyinc();
      if ((MOVFLAG & 2) == 0) {
        if (ds.ylast < ds.gwinbottom) {         /* Cursor is outside the graphics window */
          ds.ylast = ds.gwintop-1;      /* Move back to right edge of previous line */
          ds.xlast += XPPC*ds.xgupp*textxinc();
          vdu5_cursorup();
        }
      } else {
        if (ds.ylast > ds.gwintop) {            /* Cursor is outside the graphics window */
          ds.ylast = ds.gwinbottom+YPPC*ds.ygupp;       /* Move back to right edge of previous line */
          ds.xlast += XPPC*ds.xgupp*textxinc();
          vdu5_cursorup();
        }
      }
    } else {
      ds.xlast -= XPPC*ds.xgupp*textxinc();
      if ((MOVFLAG & 1) == 0) {
        if (ds.xlast < ds.gwinleft) {           /* Cursor is outside the graphics window */
          ds.xlast = ds.gwinright-XPPC*ds.xgupp+(2*ds.xscale);  /* Move back to right edge of previous line */
          ds.ylast += YPPC*ds.ygupp*textyinc();
          vdu5_cursorup();
        }
      } else {
        if (ds.xlast > ds.gwinright) {          /* Cursor is outside the graphics window */
          ds.xlast = ds.gwinleft;       /* Move back to right edge of previous line */
          ds.ylast += YPPC*ds.ygupp*textyinc();
          vdu5_cursorup();
        }
      }
    }

  } else {      /* VDU4 mode */
#endif
    hide_cursor();      /* Remove cursor */
    xtext-=textxinc();
    if ((xtext < twinleft) || (xtext > twinright)) {    /* Cursor is at left-hand edge of text window so move up a line */
      xtext = textxedge();
      move_up();
    }
    reveal_cursor();    /* Redraw cursor */
#ifndef BRANDY_MODE7ONLY
  }
#endif
}

/*
** 'move_curforward' moves the cursor forwards one character on the screen (VDU 9)
*/
static void move_curforward(void) {
  if (vduflag(VDU_FLAG_GRAPHICURS)) {   /* VDU 5 mode - Move graphics cursor back one character */
    if (MOVFLAG & 4) {
      ds.ylast -= YPPC*ds.ygupp*textyinc();
      /* Wraparound is handled in plot_char() */
    } else {
      ds.xlast += XPPC*ds.xgupp*textxinc();
      if ((MOVFLAG & 1) == 0) {
        if (ds.xlast > ds.gwinright) {  /* Cursor is outside the graphics window */
          ds.xlast = ds.gwinleft;               /* Move to left side of window on next line */
          ds.ylast -= YPPC*ds.ygupp*textyinc();
          if (ds.ylast < ds.gwinbottom) ds.ylast = ds.gwintop;  /* Moved below bottom of window - Wrap around to top */
        }
      } else {
        if (ds.xlast < ds.gwinleft) {   /* Cursor is outside the graphics window */
          ds.xlast = ds.gwinright-XPPC*ds.xgupp+(2*ds.xscale);          /* Move to left side of window on next line */
          ds.ylast -= YPPC*ds.ygupp*textyinc();
          if (ds.ylast < ds.gwinbottom) ds.ylast = ds.gwintop;  /* Moved below bottom of window - Wrap around to top */
        }
      }
    }
  } else {      /* VDU4 mode */
    hide_cursor();      /* Remove cursor */
    /* Do this check twice, as in scroll protect mode xtext may already be off the edge */
    if ((xtext < twinleft) || (xtext > twinright)) {    /* Cursor is at right-hand edge of text window so move down a line */
      xtext = textxhome();
      move_down();
    }
    xtext+=textxinc();
    if ((xtext < twinleft) || (xtext > twinright)) {    /* Cursor is at right-hand edge of text window so move down a line */
      xtext = textxhome();
      move_down();
    }
    reveal_cursor();    /* Redraw cursor */
  }
}

/*
** 'move_curdown' moves the cursor down the screen, that is, it
** performs the linefeed operation (VDU 10)
*/
static void move_curdown(void) {
#ifndef BRANDY_MODE7ONLY
  if (vduflag(VDU_FLAG_GRAPHICURS)) {
    vdu5_cursordown();
  } else {
#endif
    /* VDU14 check here - all these should be optimisable */
    if (vduflag(VDU_FLAG_ENAPAGE)) {
      matrixflags.vdu14lines++;
      if (matrixflags.vdu14lines > (twinbottom-twintop)) {
        while (kbd_modkeys(1)==0 && kbd_escpoll()==0) usleep(5000);
        matrixflags.vdu14lines=0;
      }
    }
    hide_cursor();      /* Remove cursor */
    move_down();
    reveal_cursor();    /* Redraw cursor */
#ifndef BRANDY_MODE7ONLY
  }
#endif
}

/*
** 'move_curup' moves the cursor up a line on the screen (VDU 11)
*/
static void move_curup(void) {
#ifndef BRANDY_MODE7ONLY
  if (vduflag(VDU_FLAG_GRAPHICURS)) {
    ds.ylast += YPPC*ds.ygupp*textyinc();
    vdu5_cursorup();
  } else {
#endif
    /* VDU14 check here */
    if (vduflag(VDU_FLAG_ENAPAGE)) {
      matrixflags.vdu14lines++;
// BUG: paged mode should not stop scrolling upwards
      if (matrixflags.vdu14lines > (twinbottom-twintop)) {
        while (kbd_modkeys(1)==0 && kbd_escpoll()==0) usleep(5000);
        matrixflags.vdu14lines=0;
      }
    }
    hide_cursor();      /* Remove cursor */
    move_up();
    reveal_cursor();    /* Redraw cursor */
#ifndef BRANDY_MODE7ONLY
  }
#endif
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
  hide_cursor();        /* Remove cursor if it is being displayed */
  if (vduflag(VDU_FLAG_TEXTWIN)) {      /* Text window defined that does not occupy the whole screen */
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
    SDL_FillRect(screenbank[ds.writebank], &line_rect, ds.tb_colour);
    SDL_FillRect(screen2, &line_rect, ds.tb_colour);
    SDL_FillRect(screen3, &line_rect, ds.tb_colour);
#ifndef BRANDY_MODE7ONLY
    blit_scaled(0,0,ds.screenwidth-1,ds.screenheight-1);
#endif
  }
  else {        /* Text window is not being used */
    reset_mode7();
    SDL_FillRect(screenbank[ds.writebank], NULL, ds.tb_colour);
#ifndef BRANDY_MODE7ONLY
    blit_scaled(0, 0, ds.screenwidth-1, ds.screenheight-1);
#endif
    SDL_FillRect(screen2, NULL, ds.tb_colour);
    SDL_FillRect(screen3, NULL, ds.tb_colour);
    xtext = textxhome();
    ytext = textyhome();
    reveal_cursor();    /* Redraw cursor */
  }
}

/*
** 'vdu_return' deals with the carriage return character (VDU 13)
*/
static void vdu_return(void) {
  if (vduflag(VDU_FLAG_GRAPHICURS)) {
    if (MOVFLAG & 4) {
      ds.ylast = gtextyhome();
    } else {
      ds.xlast = gtextxhome();
    }
  } else {
    hide_cursor();      /* Remove cursor */
    xtext = textxhome();
    reveal_cursor();    /* Redraw cursor */
    if (screenmode == 7) {
      write_vduflag(MODE7_GRAPHICS,0);
      write_vduflag(MODE7_SEPGRP,0);
      write_vduflag(MODE7_SEPREAL,0);
      text_physforecol = text_forecol = 7;
      text_physbackcol = text_backcol = 0;
      set_rgb();
    }
  }
}

#ifndef BRANDY_MODE7ONLY
static void fill_rectangle(int32 left, int32 top, int32 right, int32 bottom, Uint32 colour, int32 action) {
  int32 xloop, yloop;

  if (action == 5) return;

  for (yloop=top;yloop<=bottom; yloop++) {
    for (xloop=left; xloop<=right; xloop++) {
      plot_pixel(screenbank[ds.writebank], xloop, yloop, colour, action);
    }
  }

}

/*
** 'vdu_cleargraph' set the entire graphics window to the current graphics
** background colour (VDU 16)
*/
static void vdu_cleargraph(void) {
  if (istextonly()) return;
  hide_cursor();        /* Remove cursor */
  if (ds.graph_back_action == 0 && !ds.clipping) {
    SDL_FillRect(screenbank[ds.writebank], NULL, ds.gb_colour);
  } else {
    fill_rectangle(GXTOPX(ds.gwinleft), GYTOPY(ds.gwintop), GXTOPX(ds.gwinright), GYTOPY(ds.gwinbottom), ds.gb_colour, ds.graph_back_action);
  }
  blit_scaled(GXTOPX(ds.gwinleft), GYTOPY(ds.gwintop), GXTOPX(ds.gwinright), GYTOPY(ds.gwinbottom));
  if (!vduflag(VDU_FLAG_GRAPHICURS)) reveal_cursor();   /* Redraw cursor */
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
  if (colnumber < 128) {        /* Setting foreground colour */
    if (colourdepth == 256) {
      text_forecol = colnumber & COL256MASK;
      text_physforecol = (text_forecol << COL256SHIFT)+text_foretint;
    } else if (colourdepth == COL24BIT) {
      text_physforecol = text_forecol = colour24bit(colnumber, text_foretint);
    } else {
      text_physforecol = text_forecol = colnumber & colourmask;
    }
  }
  else {        /* Setting background colour */
    if (colourdepth == 256) {
      text_backcol = colnumber & COL256MASK;
      text_physbackcol = (text_backcol << COL256SHIFT)+text_backtint;
    } else if (colourdepth == COL24BIT) {
      text_physbackcol = text_backcol = colour24bit(colnumber, text_backtint);
    } else {    /* Operating in text mode */
      text_physbackcol = text_backcol = colnumber & colourmask;
    }
  }
  set_rgb();
}
#endif /* BRANDY_MODE7ONLY */

/*
** 'reset_colours' initialises the RISC OS logical to physical colour
** map for the current screen mode and sets the default foreground
** and background text and graphics colours to white and black
** respectively (VDU 20)
*/
#ifndef BRANDY_MODE7ONLY
static void resetpixels(int32 numcols) {
  int32 logcol, offset;
  for (logcol=0; logcol < numcols; logcol++) {
    int32 c = logcol * 3;
    int32 newcol = SDL_MapRGB(sdl_fontbuf->format, palette[c+0], palette[c+1], palette[c+2]) + (logcol << 24);
    for (offset=0; offset < (ds.screenheight*ds.screenwidth*ds.xscale); offset++) {
      if ((SWAPENDIAN(*((Uint32*)screenbank[ds.writebank]->pixels + offset)) >> 24) == logcol) *((Uint32*)screenbank[ds.writebank]->pixels + offset) = SWAPENDIAN(newcol);
    }
  }
  blit_scaled(0,0,ds.screenwidth-1,ds.screenheight-1);
}

static void reset_colours(void) {
  switch (colourdepth) {        /* Initialise the text mode colours */
  case 2:
    logtophys[0] = VDU_BLACK;
    logtophys[1] = VDU_WHITE;
    text_forecol = ds.graph_forecol = ds.graph_forelog = 1;
    break;
  case 4:
    logtophys[0] = VDU_BLACK;
    logtophys[1] = VDU_RED;
    logtophys[2] = VDU_YELLOW;
    logtophys[3] = VDU_WHITE;
    text_forecol = ds.graph_forecol = ds.graph_forelog = 3;
    break;
  case 16:
    logtophys[0]  = VDU_BLACK;
    logtophys[1]  = VDU_RED;
    logtophys[2]  = VDU_GREEN;
    logtophys[3]  = VDU_YELLOW;
    logtophys[4]  = VDU_BLUE;
    logtophys[5]  = VDU_MAGENTA;
    logtophys[6]  = VDU_CYAN;
    logtophys[7]  = VDU_WHITE;
    logtophys[8]  = FLASH_BLAWHITE;
    logtophys[9]  = FLASH_REDCYAN;
    logtophys[10] = FLASH_GREENMAG;
    logtophys[11] = FLASH_YELBLUE;
    logtophys[12] = FLASH_BLUEYEL;
    logtophys[13] = FLASH_MAGREEN;
    logtophys[14] = FLASH_CYANRED;
    logtophys[15] = FLASH_WHITEBLA;
    text_forecol = ds.graph_forecol = ds.graph_forelog = 7;
    break;
  case 256:
    text_forecol = ds.graph_forecol = ds.graph_forelog = 63;
    ds.graph_foretint = text_foretint = MAXTINT;
    ds.graph_backtint = text_backtint = 0;
    break;
  case COL24BIT:
    text_forecol = ds.graph_forecol = ds.graph_forelog = 0xFFFFFF;
    ds.graph_foretint = text_foretint = MAXTINT;
    ds.graph_backtint = text_backtint = 0;
    break;
  default:
    error(ERR_UNSUPPORTED); /* 32K colour modes not supported */
    return;
  }
  if (colourdepth==256)
    colourmask = COL256MASK;
  else {
    colourmask = colourdepth-1;
  }
  text_backcol = ds.graph_backcol = ds.graph_backlog = 0;
  init_palette();
  if (colourdepth <= 16) resetpixels(colourdepth);
}
#endif

/*
** 'vdu_graphcol' sets the graphics foreground or background colour and
** changes the type of plotting action to be used for graphics (VDU 18).
*/
#ifndef BRANDY_MODE7ONLY
static void vdu_graphcol(void) {
  int32 colnumber;
  colnumber = vduqueue[1];
  if (colnumber < 128) {        /* Setting foreground graphics colour */
      ds.graph_fore_action = vduqueue[0];
      if (colourdepth == 256) {
        ds.graph_forecol = ds.graph_forelog = colnumber & COL256MASK;
        ds.graph_physforecol = (ds.graph_forecol<<COL256SHIFT)+ds.graph_foretint;
      } else if (colourdepth == COL24BIT) {
        ds.graph_forelog = colnumber & COL256MASK;
        ds.graph_physforecol = ds.graph_forecol = colour24bit(colnumber, ds.graph_foretint);
      } else {
        ds.graph_physforecol = ds.graph_forecol = ds.graph_forelog = colnumber & colourmask;
      }
  }
  else {        /* Setting background graphics colour */
    ds.graph_back_action = vduqueue[0];
    if (colourdepth == 256) {
      ds.graph_backcol = ds.graph_backlog = colnumber & COL256MASK;
      ds.graph_physbackcol = (ds.graph_backcol<<COL256SHIFT)+ds.graph_backtint;
    } else if (colourdepth == COL24BIT) {
      ds.graph_backlog = colnumber & COL256MASK;
      ds.graph_physbackcol = ds.graph_backcol = colour24bit(colnumber, ds.graph_backtint);
    } else {    /* Operating in text mode */
      ds.graph_physbackcol = ds.graph_backcol = ds.graph_backlog = colnumber & colourmask;
    }
  }
  set_rgb();
}

/*
** 'vdu_graphwind' defines a graphics clipping region (VDU 24)
*/
static void vdu_graphwind(void) {
  int32 left, right, top, bottom;
  left = vduqueue[0]+vduqueue[1]*256;           /* Left-hand coordinate */
  if (left > 0x7FFF) left = -(0x10000-left);    /* Coordinate is negative */
  bottom = vduqueue[2]+vduqueue[3]*256;         /* Bottom coordinate */
  if (bottom > 0x7FFF) bottom = -(0x10000-bottom);
  right = vduqueue[4]+vduqueue[5]*256;          /* Right-hand coordinate */
  if (right > 0x7FFF) right = -(0x10000-right);
  top = vduqueue[6]+vduqueue[7]*256;            /* Top coordinate */
  if (top > 0x7FFF) top = -(0x10000-top);
  left += ds.xorigin;
  right += ds.xorigin;
  top += ds.yorigin;
  bottom += ds.yorigin;
#ifdef DEBUG
  if (basicvars.debug_flags.vdu) {
    fprintf(stderr, "VDU24: left=%d, right=%d, top=%d, bottom=%d\n", left, right, top, bottom);
    fprintf(stderr, "VDU24: xgraphunits=%d, ygraphunits=%d\n", ds.xgraphunits, ds.ygraphunits);
  }
#endif
  /* If window dimensions are negative, do nothing */
  if (left > right) return;
  if (bottom > top) return;

/* If any edge is off screen, do nothing */
  if (left < 0 || bottom < 0 || right >= ds.xgraphunits || top >= ds.ygraphunits) return;
#ifdef DEBUG
  if (basicvars.debug_flags.vdu) fprintf(stderr, "VDU24: Graphics window set\n");
#endif
  ds.gwinleft = left;
  ds.gwinright = right;
  ds.gwintop = top;
  ds.gwinbottom = bottom;
  ds.clipping = TRUE;
}

/*
** 'vdu_plot' handles the VDU 25 graphics sequences
*/
static void vdu_plot(void) {
  int32 x, y;
  x = vduqueue[1]+vduqueue[2]*256;
  if (x > 0x7FFF) x = -(0x10000-x);     /* X is negative */
  y = vduqueue[3]+vduqueue[4]*256;
  if (y > 0x7FFF) y = -(0x10000-y);     /* Y is negative */
  emulate_plot(vduqueue[0], x, y);      /* vduqueue[0] gives the plot code */
}
#endif /* BRANDY_MODE7ONLY */

/*
** 'vdu_restwind' restores the default (full screen) text and
** graphics windows (VDU 26)
*/
static void vdu_restwind(void) {
  ds.clipping = FALSE;
  write_vduflag(MODE7_GRAPHICS,0);
  ds.xorigin = ds.yorigin = 0;
  ds.xlast = ds.ylast = ds.xlast2 = ds.ylast2 = 0;
  ds.gwinleft = 0;
  ds.gwinright = ds.xgraphunits-1;
  ds.gwintop = ds.ygraphunits-1;
  ds.gwinbottom = 0;
  hide_cursor();        /* Remove cursor */
  xtext = ytext = 0;
  reveal_cursor();      /* Redraw cursor */
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
  write_vduflag(MODE7_GRAPHICS,0);
  left = vduqueue[0];
  bottom = vduqueue[1];
  right = vduqueue[2];
  top = vduqueue[3];
  if ((left >= textwidth) || (right >= textwidth) || (top >= textheight) || (bottom >= textheight)) return; /* Ignore bad parameters */
  if (left > right) {   /* Ensure right column number > left */
    int32 temp = left;
    left = right;
    right = temp;
  }
  if (bottom < top) {   /* Ensure bottom line number > top */
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
  move_cursor(twinleft, twintop);       /* Move text cursor to home position in new window */
}

/*
** 'vdu_origin' sets the graphics origin (VDU 29)
*/
#ifndef BRANDY_MODE7ONLY
static void vdu_origin(void) {
  int32 x, y;
  x = vduqueue[0]+vduqueue[1]*256;
  y = vduqueue[2]+vduqueue[3]*256;
  ds.xorigin = x<=32767 ? x : -(0x10000-x);
  ds.yorigin = y<=32767 ? y : -(0x10000-y);
}
#endif

/*
** 'vdu_hometext' sends the text cursor to the top left-hand corner of
** the text window (VDU 30)
*/
static void vdu_hometext(void) {
  if (vduflag(VDU_FLAG_GRAPHICURS)) {   /* Send graphics cursor to top left-hand corner of graphics window */
    ds.xlast = (gtextxhome()/(2*ds.xscale))*2*ds.xscale;
    ds.ylast = (gtextyhome()/(2*ds.yscale))*2*ds.yscale;
  }
  else {        /* Send text cursor to the top left-hand corner of the text window */
    move_cursor(twinleft, twintop);
  }
}

/*
** 'vdu_movetext' moves the text cursor to the given column and row in
** the text window (VDU 31)
*/
static void vdu_movetext(void) {
  if (vduflag(VDU_FLAG_GRAPHICURS)) {   /* Text is going to the graphics cursor */
    ds.xlast = ds.gwinleft+vduqueue[0]*XPPC*ds.xgupp;
    ds.ylast = ds.gwintop-vduqueue[1]*YPPC*ds.ygupp;
  }
  else {        /* Text is going to the text cursor */
    int32 column = vduqueue[0] + twinleft;
    int32 row = vduqueue[1] + twintop;
    if (column > (twinright + SCROLLPROT) || row > twinbottom) return;  /* Ignore command if values are out of range */
    move_cursor(column, row);
  }
}

static void printer_char(void) {
  if (matrixflags.printer) fputc(vduqueue[0], matrixflags.printer);
}

/*
** 'emulate_vdu' is a simple emulation of the RISC OS VDU driver. It
** accepts characters as per the RISC OS driver and uses them to imitate
** some of the VDU commands. Some of them are not supported and flagged
** as errors but others, for example, the 'page mode on' and 'page mode
** off' commands, are silently ignored.
*/
void emulate_vdu(int32 charvalue) {
  charvalue = charvalue & BYTEMASK;     /* Deal with any signed char type problems */
  if (matrixflags.dospool) fputc(charvalue, matrixflags.dospool);
  if (matrixflags.printer) printout_character(charvalue);
  if (vduneeded == 0) {                 /* VDU queue is empty */
    if (vduflag(VDU_FLAG_DISABLE)) {
      if (charvalue == VDU_ENABLE) write_vduflag(VDU_FLAG_DISABLE,0);
      return;
    }
    if (charvalue >= ' ') {             /* Most common case - print something */
      /* Handle Mode 7 */
#ifndef BRANDY_MODE7ONLY
      if (screenmode == 7) {
#endif
        if (SCROLLPROT && ((xtext > twinright) || (xtext < twinleft))) { /* Have reached edge of text window. Skip to next line  */
          xtext = textxhome();
          ytext+=textyinc();
          /* VDU14 check here */
          if (vduflag(VDU_FLAG_ENAPAGE)) {
            matrixflags.vdu14lines++;
            if (matrixflags.vdu14lines > (twinbottom-twintop)) {
              while (kbd_modkeys(1)==0 && kbd_escpoll()==0) usleep(5000);
              matrixflags.vdu14lines=0;
            }
          }
          if (ytext > twinbottom) {
            if (vdu2316byte & 16) {
              ytext=textyhome();
            } else {
              ytext--;
              scroll(SCROLL_UP);
            }
          }
          if (ytext < twintop) {
            if (vdu2316byte & 16) {
              ytext=textyedge();
            } else {
              ytext++;
              scroll(SCROLL_DOWN);
            }
          }
        }
        if (charvalue == 127) {
          move_curback();
          mode7frame[ytext][xtext]=32;
          move_curback();
        } else {
          /* The character shuffle that used to be in mode7renderline() */
          switch (charvalue) {
            case 35: charvalue=95; break;
            case 95: charvalue=96; break;
            case 96: charvalue=35; break;
          }
          mode7frame[ytext][xtext]=charvalue;
        }
        xtext+=textxinc();
        if ((!SCROLLPROT) && ((xtext > twinright) || (xtext < twinleft))) {
          xtext = textxhome();
          ytext+=textyinc();
          /* VDU14 check here */
          if (vduflag(VDU_FLAG_ENAPAGE)) {
            matrixflags.vdu14lines++;
            if (matrixflags.vdu14lines > (twinbottom-twintop)) {
              while (kbd_modkeys(1)==0 && kbd_escpoll()==0) usleep(5000);
              matrixflags.vdu14lines=0;
            }
          }
          if (ytext > twinbottom) {
            ytext--;
            scroll(SCROLL_UP);
          }
          if (ytext < twintop) {
            ytext++;
            scroll(SCROLL_DOWN);
          }
        }
        return; /* End of MODE 7 block */
#ifndef BRANDY_MODE7ONLY
      } else {
        if (vduflag(VDU_FLAG_GRAPHICURS)) {                         /* Sending text output to graphics cursor */
          if (charvalue == 127) {
            move_curback();
            plot_space_opaque();
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
            reveal_cursor();    /* Redraw the cursor */
          }
        }
      }
#endif
      return;
    }
    else {      /* Control character - Found start of new VDU command */
      vducmd = charvalue;
      vduneeded = vdubytes[charvalue];
      vdunext = 0;
    }
  }
  else {        /* Add character to VDU queue for current command */
    vduqueue[vdunext] = charvalue;
    vdunext++;
  }
  if (vdunext < vduneeded) return;
  vduneeded = 0;

/* There are now enough entries in the queue for the current command */

  switch (vducmd) {     /* Emulate the various control codes */
  case VDU_NULL:        /* 0 - Do nothing */
    break;
  case VDU_PRINT:       /* 1 - Send next character to the print stream */
    printer_char();
    break;
  case VDU_ENAPRINT:    /* 2 - Enable the sending of characters to the printer */
    open_printer();
    break;
  case VDU_DISPRINT:    /* 3 - Disable the sending of characters to the printer */
    close_printer();
    break;
  case VDU_TEXTCURS:    /* 4 - Print text at text cursor */
    write_vduflag(VDU_FLAG_GRAPHICURS,0);
    if (cursorstate == HIDDEN) {        /* Start displaying the cursor */
      cursorstate = SUSPENDED;
      toggle_cursor();
    }
    break;
  case VDU_GRAPHICURS:  /* 5 - Print text at graphics cursor */
    if (!istextonly()) {
      toggle_cursor();  /* Remove the cursor if it is being displayed */
      cursorstate = HIDDEN;
      write_vduflag(VDU_FLAG_GRAPHICURS,1);
    }
    break;
  case VDU_ENABLE:      /* 6 - Enable the VDU driver */
    write_vduflag(VDU_FLAG_DISABLE,0);
    break;
  case VDU_BEEP:        /* 7 - Sound the bell */
    putchar('\7');
    fflush(stdout);
    break;
  case VDU_CURBACK:     /* 8 - Move cursor left one character */
    move_curback();
    break;
  case VDU_CURFORWARD:  /* 9 - Move cursor right one character */
    move_curforward();
    break;
  case VDU_CURDOWN:     /* 10 - Move cursor down one line (linefeed) */
    move_curdown();
    break;
  case VDU_CURUP:       /* 11 - Move cursor up one line */
    move_curup();
    break;
  case VDU_CLEARTEXT:   /* 12 - Clear text window (formfeed) */
#ifndef BRANDY_MODE7ONLY
    if (vduflag(VDU_FLAG_GRAPHICURS))   /* In VDU 5 mode, clear the graphics window */
      vdu_cleargraph();
    else                /* In text mode, clear the text window */
#endif
      vdu_cleartext();
    vdu_hometext();
    break;
  case VDU_RETURN:      /* 13 - Carriage return */
    vdu_return();
    break;
  case VDU_ENAPAGE:     /* 14 - Enable page mode */
    write_vduflag(VDU_FLAG_ENAPAGE,1);
    break;
  case VDU_DISPAGE:     /* 15 - Disable page mode */
    write_vduflag(VDU_FLAG_ENAPAGE,0);
    break;
#ifndef BRANDY_MODE7ONLY
  case VDU_CLEARGRAPH:  /* 16 - Clear graphics window */
    vdu_cleargraph();
    break;
  case VDU_TEXTCOL:     /* 17 - Change current text colour */
    vdu_textcol();
    break;
  case VDU_GRAPHCOL:    /* 18 - Change current graphics colour */
    vdu_graphcol();
    break;
  case VDU_LOGCOL:      /* 19 - Map logical colour to physical colour */
    vdu_setpalette();
    break;
  case VDU_RESTCOL:     /* 20 - Restore logical colours to default values */
    reset_colours();
    break;
#endif
  case VDU_DISABLE:     /* 21 - Disable the VDU driver */
    write_vduflag(VDU_FLAG_DISABLE,1);
    break;
  case VDU_SCRMODE:     /* 22 - Change screen mode */
    emulate_mode(vduqueue[0]);
    break;
  case VDU_COMMAND:     /* 23 - Assorted VDU commands */
    vdu_23command();
    break;
#ifndef BRANDY_MODE7ONLY
  case VDU_DEFGRAPH:    /* 24 - Define graphics window */
    vdu_graphwind();
    break;
  case VDU_PLOT:        /* 25 - Issue graphics command */
    vdu_plot();
    break;
#endif
  case VDU_RESTWIND:    /* 26 - Restore default windows */
    vdu_restwind();
    break;
  case VDU_ESCAPE:      /* 27 - Do nothing (character is sent to output stream) */
//    putch(vducmd);
    break;
  case VDU_DEFTEXT:     /* 28 - Define text window */
    vdu_textwind();
    break;
#ifndef BRANDY_MODE7ONLY
  case VDU_ORIGIN:      /* 29 - Define graphics origin */
    vdu_origin();
    break;
#endif
  case VDU_HOMETEXT:    /* 30 - Send cursor to top left-hand corner of screen */
    vdu_hometext();
    break;
  case VDU_MOVETEXT:    /* 31 - Send cursor to column x, row y on screen */
    vdu_movetext();
  }
}

/*
** 'emulate_vdustr' is called to print a string via the 'VDU driver'
*/
void emulate_vdustr(char string[], int32 length) {
  int32 n;
  if (length == 0) length = strlen(string);
  for (n = 0; n < length; n++) {
    emulate_vdu(string[n]);        /* Send the string to the VDU driver */
    if (basicvars.printwidth > 0) {
      if (emulate_pos() == basicvars.printwidth) {
        emulate_vdu(13);
        emulate_vdu(10);
      }
    }
  }
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
  length = vsnprintf(text, MAXSTRING, format, parms);
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
size_t emulate_vdufn(int variable) {
  return readmodevariable(-1,variable);
}

/*
** 'emulate_pos' returns the number of the column in which the text cursor
** is located in the text window
*/
int32 emulate_pos(void) {
  int32 ret=0;
  switch(MOVFLAG) {
    case 0: case 2:
      ret = xtext - twinleft;
      break;
    case 1: case 3:
      ret = twinright - xtext;
      break;
  }
  return ret;
}

/*
** 'emulate_vpos' returns the number of the row in which the text cursor
** is located in the text window
*/
int32 emulate_vpos(void) {
  int32 ret=0;
  switch(MOVFLAG) {
    case 0: case 1:
      ret = ytext - twintop;
      break;
    case 2: case 3:
      ret = twinbottom - ytext;
      break;
  }
  return ret;
}

/*
** 'setup_mode' is called to set up the details of mode 'mode'
*/
static void setup_mode(int32 mode) {
  int32 modecopy;
  Uint32 sx, sy, ox, oy;
  int p;

  ds.videorefresh = 0;
  mode = mode & MODEMASK;       /* Lose 'shadow mode' bit */
  modecopy = mode;
  if (mode > HIGHMODE) mode = modecopy = 0;     /* Out of range modes are mapped to MODE 0 */
  ox=ds.vscrwidth;
  oy=ds.vscrheight;
  /* Try to catch an undefined mode */
  hide_cursor();
  if (modetable[mode].xres == 0) {
    if (matrixflags.failovermode == 255) {
      tmsg.modechange = -2;
      return;
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
    if (matrixflags.failovermode == 255) {
      tmsg.modechange = -2;
      return;
    }
  }
  memset(matrixflags.surface->pixels, 0, sx*sy*4);
  ds.autorefresh=1;
  ds.vscrwidth = sx;
  ds.vscrheight = sy;
  for (p=0; p<MAXBANKS; p++) {
    SDL_FreeSurface(screenbank[p]);
    screenbank[p]=SDL_DisplayFormat(matrixflags.surface);
  }
  matrixflags.modescreen_ptr = screenbank[ds.writebank]->pixels;
  matrixflags.modescreen_sz = modetable[mode].xres * modetable[mode].yres * 4;
  ds.displaybank=0;
  ds.writebank=0;
  SDL_FreeSurface(screen1);
  screen1 = SDL_DisplayFormat(matrixflags.surface);
  SDL_FreeSurface(screen2);
  screen2 = SDL_DisplayFormat(matrixflags.surface);
  SDL_FreeSurface(screen3);
  screen3 = SDL_DisplayFormat(matrixflags.surface);
/* Set up VDU driver parameters for mode */
  screenmode = modecopy;
  YPPC=8; if ((mode == 3) || (mode == 6) || (mode == 11) || (mode == 14) || (mode == 17)) YPPC=10;
  place_rect.h = font_rect.h = YPPC;
  reset_mode7();
  ds.screenwidth = modetable[mode].xres;
  ds.screenheight = modetable[mode].yres;
  ds.xgraphunits = modetable[mode].xgraphunits;
  ds.ygraphunits = modetable[mode].ygraphunits;
  colourdepth = modetable[mode].coldepth;
  textwidth = modetable[mode].xtext;
  textheight = modetable[mode].ytext;
  ds.xscale = modetable[mode].xscale;
  ds.yscale = modetable[mode].yscale;
  ds.scaled = ds.yscale != 1 || ds.xscale != 1; /* TRUE if graphics screen is scaled to fit real screen */
  write_vduflag(VDU_FLAG_GRAPHICURS,0);
  cursmode = UNDERLINE;
  cursorstate = SUSPENDED;      /* Cursor will be switched on later */
  ds.clipping = FALSE;          /* A clipping region has not been defined for the screen mode */
  ds.xgupp = ds.xgraphunits/ds.screenwidth;     /* Graphics units per pixel in X direction */
  ds.ygupp = ds.ygraphunits/ds.screenheight;    /* Graphics units per pixel in Y direction */
  ds.xorigin = ds.yorigin = 0;
  ds.xlast = ds.ylast = ds.xlast2 = ds.ylast2 = 0;
  ds.gwinleft = 0;
  ds.gwinright = ds.xgraphunits-1;
  ds.gwintop = ds.ygraphunits-1;
  ds.gwinbottom = 0;
  write_vduflag(VDU_FLAG_TEXTWIN,0);            /* A text window has not been created yet */
  twinleft = 0;                 /* Set up initial text window to whole screen */
  twinright = textwidth-1;
  twintop = 0;
  twinbottom = textheight-1;
  xtext = textxhome();
  ytext = textyhome();
  ds.graph_fore_action = ds.graph_back_action = 0;
#ifndef BRANDY_MODE7ONLY
  reset_colours();
  set_dot_pattern(DEFAULT_DOT_PATTERN);
#endif
  init_palette();
  write_vduflag(VDU_FLAG_ENAPAGE,0);
  SDL_FillRect(matrixflags.surface, NULL, ds.tb_colour);
  for (p=0; p<MAXBANKS; p++) {
    SDL_FillRect(screenbank[p], NULL, ds.tb_colour);
  }
  SDL_FillRect(screen2, NULL, ds.tb_colour);
  SDL_FillRect(screen3, NULL, ds.tb_colour);
  SDL_SetClipRect(matrixflags.surface, NULL);
  /* Are we full screen? If so, turn the mouse off */
  if (matrixflags.alwaysfullscreen || (matrixflags.surface->flags & SDL_FULLSCREEN)) {
    sdl_mouse_onoff(0);
  } else {
    sdl_mouse_onoff(1);
  }
  if (screenmode == 7) {
    font_rect.w = place_rect.w = M7XPPC;
    font_rect.h = place_rect.h = M7YPPC;
    tmsg.crtc6845r10 = 114;
  } else {
    font_rect.w = place_rect.w = XPPC;
    font_rect.h = place_rect.h = YPPC;
    tmsg.crtc6845r10 = 103;
  }
  tmsg.modechange = -1;
}

/*
** 'emulate_mode' deals with the Basic 'MODE' statement when the
** parameter is a number. This version of the function is used when
** the interpreter supports graphics.
*/
void emulate_mode(int32 mode) {
  int p;

  /* Signal the display update thread to change mode */
  tmsg.modechange = mode;
  while (tmsg.modechange >= 0) usleep(1000);
  if (tmsg.modechange == -2) {
    error(ERR_BADMODE);
    return;
  }
/* Reset colours, clear screen and home cursor */
  SDL_FillRect(matrixflags.surface, NULL, ds.tb_colour);
  for (p=0; p<MAXBANKS; p++) {
    SDL_FillRect(screenbank[p], NULL, ds.tb_colour);
  }
  xtext = textxhome();
  ytext = textyhome();
  emulate_vdu(VDU_CLEARGRAPH);
}

/*
 * emulate_newmode - Change the screen mode using specific mode
 * parameters for the screen size and so on. This is for the new
 * form of the MODE statement
 */
void emulate_newmode(int32 xres, int32 yres, int32 bpp, int32 rate) {
#ifndef BRANDY_MODE7ONLY
  int32 coldepth, n;
  if (xres == 0 || yres == 0 || rate == 0 || bpp == 0) {
    error(ERR_BADMODE);
    return;
  }
  switch (bpp) {
  case 1: coldepth = 2; break;
  case 2: coldepth = 4; break;
  case 4: coldepth = 16; break;
  case 8: coldepth = 256; break;
  default:
    coldepth = COL24BIT;
  }
  for (n=0; n<=HIGHMODE; n++) {
    if (modetable[n].xres == xres && modetable[n].yres == yres && modetable[n].coldepth == coldepth && modetable[n].graphics==TRUE) break;
  }
  if (n > HIGHMODE) {
    /* Mode isn't predefined. So, let's make it. */
    n=126;
    setupnewmode(n, xres, yres, coldepth, 1, 1, 1, 1);
  }
  emulate_mode(n);
#endif
}

/*
** 'emulate_modestr' deals with the Basic 'MODE' command when the
** parameter is a string. This code is restricted to the standard
** RISC OS screen modes but can be used to define a grey scale mode
** instead of a colour one
*/
void emulate_modestr(int32 xres, int32 yres, int32 colours, int32 greys, int32 xeig, int32 yeig, int32 rate) {
#ifndef BRANDY_MODE7ONLY
  int32 coldepth, n;
  if (xres == 0 || yres == 0 || rate == 0 || (colours == 0 && greys == 0)) {
    error(ERR_BADMODE);
    return;
  }
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
  if (colours == 0) {   /* Want a grey scale palette  - Reset all the colours */
    int32 step, intensity;
    step = 255/(greys-1);
    intensity = 0;
    for (n=0; n < greys; n++) {
      change_palette(n, intensity, intensity, intensity);
      intensity+=step;
    }
  }
#endif
}

/*
** 'emulate_modefn' emulates the Basic function 'MODE'
*/
int32 emulate_modefn(void) {
  return screenmode;
}

/* The plot_pixel function plots pixels for the drawing functions, and
   takes into account the GCOL foreground action code */
#ifndef BRANDY_MODE7ONLY
static void plot_pixel(SDL_Surface *surface, int32 x, int32 y, Uint32 colour, Uint32 action) {
  Uint32 altcolour = 0, prevcolour, drawcolour, a;
  int32 rox = 0, roy = 0;
  int64 offset = x + (y*ds.vscrwidth);

  if (action == 5) return;

  if ((x < 0) || (x >= ds.screenwidth) || (y < 0) || (y >= ds.screenheight)) return;

  if (ds.clipping) {
    rox = x * ds.xgupp;
    roy = ds.ygraphunits - (y+1)*ds.ygupp;
#ifdef DEBUG
    if (basicvars.debug_flags.vdu) {
      fprintf(stderr, "plot_pixel [VDU24]: x=%d, y=%d, rox=%d, roy=%d\n", x, y, rox, roy);
      fprintf(stderr, "                    gwinleft=%d, gwinright=%d, gwintop=%d, gwinbottom=%d\n", ds.gwinleft, ds.gwinright, ds.gwintop, ds.gwinbottom);
    }
#endif
    if ((rox < ds.gwinleft) || (rox > ds.gwinright) || (roy < ds.gwinbottom) || (roy > ds.gwintop)) return;
#ifdef DEBUG
    if (basicvars.debug_flags.vdu) {
      fprintf(stderr, "                    Pixel will be plotted.\n");
    }
#endif
  }
  if (ds.plot_inverse ==1) {
    action=3;
    drawcolour=(colourdepth-1);
  } else {
    drawcolour=ds.graph_physforecol;
  }

  if ((action==0) && (ds.plot_inverse == 0)) {
    altcolour = colour;
  } else {
    prevcolour=SWAPENDIAN(*((Uint32*)surface->pixels + offset));
    prevcolour=emulate_colourfn((prevcolour >> 16) & 0xFF, (prevcolour >> 8) & 0xFF, (prevcolour & 0xFF));
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
      case 6:
        altcolour=(prevcolour & ((~drawcolour) & (colourdepth-1)));
        break;
      case 7:
        altcolour=(prevcolour | ((~drawcolour) & (colourdepth-1)));
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
  *((Uint32*)surface->pixels + offset) = SWAPENDIAN(altcolour);
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
// Attempt to reduce stack usage..
int32 ffcolour, ffaction;

static void flood_fill_inner(uint32 coord) {
  int32 x=(coord & 0xFFFF);
  int32 y=(coord >> 16);
  if(basicvars.recdepth == basicvars.maxrecdepth) return;
  if (*((Uint32*)screenbank[ds.writebank]->pixels + x + y*ds.vscrwidth) != ds.gb_colour) return;
  basicvars.recdepth++;
  plot_pixel(screenbank[ds.writebank], x, y, ffcolour, ffaction); /* Plot this pixel */
  if (x >= 1) /* Left */
    if (*((Uint32*)screenbank[ds.writebank]->pixels + (x-1) + y*ds.vscrwidth) == ds.gb_colour)
      flood_fill_inner(x-1+(y<<16));
  if (x < (ds.screenwidth-1)) /* Right */
    if (*((Uint32*)screenbank[ds.writebank]->pixels + (x+1) + y*ds.vscrwidth) == ds.gb_colour)
      flood_fill_inner(x+1+(y<<16));
  if (y >= 1) /* Up */
    if (*((Uint32*)screenbank[ds.writebank]->pixels + x + (y-1)*ds.vscrwidth) == ds.gb_colour)
      flood_fill_inner(x+((y-1)<<16));
  if (y < (ds.screenheight-1)) /* Down */
    if (*((Uint32*)screenbank[ds.writebank]->pixels + x + (y+1)*ds.vscrwidth) == ds.gb_colour)
      flood_fill_inner(x+((y+1)<<16));
  basicvars.recdepth--;
}

static void flood_fill(int32 x, int32 y, int colour, Uint32 action) {
  int32 pwinleft, pwinright, pwintop, pwinbottom;
  if (colour == ds.gb_colour) return;
  pwinleft = GXTOPX(ds.gwinleft);               /* Calculate extent of graphics window in pixels */
  pwinright = GXTOPX(ds.gwinright);
  pwintop = GYTOPY(ds.gwintop);
  pwinbottom = GYTOPY(ds.gwinbottom);
  if (x < pwinleft || x > pwinright || y < pwintop || y > pwinbottom) return;
  ffcolour=colour;
  ffaction=action;
  if (*((Uint32*)screenbank[ds.writebank]->pixels + x + y*ds.vscrwidth) == ds.gb_colour)
    flood_fill_inner(x+(y<<16));
  hide_cursor();
  blit_scaled(0,0,ds.screenwidth-1,ds.screenheight-1);
  reveal_cursor();
}
#endif /* BRANDY_MODE7ONLY */

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
#ifndef BRANDY_MODE7ONLY
  int32 tx, ty, sx, sy, ex, ey, action;
  Uint32 colour = 0;
  SDL_Rect plot_rect, temp_rect;
  if (istextonly()) return;
/* Decode the command */
  ds.plot_inverse = 0;
  action = ds.graph_fore_action;
  ds.xlast3 = ds.xlast2;
  ds.ylast3 = ds.ylast2;
  ds.xlast2 = ds.xlast;
  ds.ylast2 = ds.ylast;
  if ((code & ABSCOORD_MASK) != 0 ) {           /* Coordinate (x,y) is absolute */
    ds.xlast = x+ds.xorigin;    /* These probably have to be treated as 16-bit values */
    ds.ylast = y+ds.yorigin;
  }
  else {        /* Coordinate (x,y) is relative */
    ds.xlast+=x;        /* These probably have to be treated as 16-bit values */
    ds.ylast+=y;
  }
  if ((code & PLOT_COLMASK) == PLOT_MOVEONLY) return;   /* Just moving graphics cursor, so finish here */
  sx = GXTOPX(ds.xlast2);
  sy = GYTOPY(ds.ylast2);
  ex = GXTOPX(ds.xlast);
  ey = GYTOPY(ds.ylast);
  if ((code & GRAPHOP_MASK) != SHIFT_RECTANGLE) {               /* Move and copy rectangle are a special case */
    switch (code & PLOT_COLMASK) {
    case PLOT_FOREGROUND:       /* Use graphics foreground colour */
      colour = ds.gf_colour;
      break;
    case PLOT_INVERSE:          /* Use logical inverse of colour at each point */
      ds.plot_inverse=1;
      break;
    case PLOT_BACKGROUND:       /* Use graphics background colour */
      colour = ds.gb_colour;
      action = ds.graph_back_action;
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
    case DRAW_DOTLINE2+8: {       /* Draw line */
      int32 top, left;
      left = sx;  /* Find top left-hand corner of rectangle containing line */
      top = sy;
      if (ex < sx) left = ex;
      if (ey < sy) top = ey;
      draw_line(screenbank[ds.writebank], sx, sy, ex, ey, colour, (code & DRAW_STYLEMASK), action);
      hide_cursor();
      blit_scaled(left, top, sx+ex-left, sy+ey-top);
      reveal_cursor();
      break;
    }
    case PLOT_POINT:      /* Plot a single point */
      hide_cursor();
      if ((ex < 0) || (ex >= ds.screenwidth) || (ey < 0) || (ey >= ds.screenheight)) break;
      plot_pixel(screenbank[ds.writebank], ex, ey, colour, action);
      blit_scaled(ex, ey, ex, ey);
      reveal_cursor();
      break;
    case FILL_TRIANGLE: {         /* Plot a filled triangle */
      int32 left, right, top, bottom;
      filled_triangle(screenbank[ds.writebank], GXTOPX(ds.xlast3), GYTOPY(ds.ylast3), sx, sy, ex, ey, colour, action);
  /*  Now figure out the coordinates of the rectangle that contains the triangle */
      left = right = ds.xlast3;
      top = bottom = ds.ylast3;
      if (ds.xlast2 < left) left = ds.xlast2;
      if (ds.xlast < left) left = ds.xlast;
      if (ds.xlast2 > right) right = ds.xlast2;
      if (ds.xlast > right) right = ds.xlast;
      if (ds.ylast2 > top) top = ds.ylast2;
      if (ds.ylast > top) top = ds.ylast;
      if (ds.ylast2 < bottom) bottom = ds.ylast2;
      if (ds.ylast < bottom) bottom = ds.ylast;
      hide_cursor();
      blit_scaled(GXTOPX(left), GYTOPY(top), GXTOPX(right), GYTOPY(bottom));
      reveal_cursor();
      break;
    }
    case FILL_RECTANGLE: {                /* Plot a filled rectangle */
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
      if (action==0 && !ds.clipping) {
        SDL_FillRect(screenbank[ds.writebank], &plot_rect, SWAPENDIAN(colour));
      } else {
        fill_rectangle(left, top, right, bottom, colour, action);
      }
      hide_cursor();
      blit_scaled(left, top, right, bottom);
      reveal_cursor();
      break;
    }
    case FILL_PARALLELOGRAM: {    /* Plot a filled parallelogram */
      int32 vx, vy, left, right, top, bottom;
      filled_triangle(screenbank[ds.writebank], GXTOPX(ds.xlast3), GYTOPY(ds.ylast3), sx, sy, ex, ey, colour, action);
      vx = ds.xlast3-ds.xlast2+ds.xlast;
      vy = ds.ylast3-ds.ylast2+ds.ylast;
      filled_triangle(screenbank[ds.writebank], ex, ey, GXTOPX(vx), GYTOPY(vy), GXTOPX(ds.xlast3), GYTOPY(ds.ylast3), colour, action);
  /*  Now figure out the coordinates of the rectangle that contains the parallelogram */
      left = right = ds.xlast3;
      top = bottom = ds.ylast3;
      if (ds.xlast2 < left) left = ds.xlast2;
      if (ds.xlast < left) left = ds.xlast;
      if (vx < left) left = vx;
      if (ds.xlast2 > right) right = ds.xlast2;
      if (ds.xlast > right) right = ds.xlast;
      if (vx > right) right = vx;
      if (ds.ylast2 > top) top = ds.ylast2;
      if (ds.ylast > top) top = ds.ylast;
      if (vy > top) top = vy;
      if (ds.ylast2 < bottom) bottom = ds.ylast2;
      if (ds.ylast < bottom) bottom = ds.ylast;
      if (vy < bottom) bottom = vy;
      hide_cursor();
      blit_scaled(GXTOPX(left), GYTOPY(top), GXTOPX(right), GYTOPY(bottom));
      reveal_cursor();
      break;
    }
    case FLOOD_BACKGROUND:        /* Flood fill background with graphics foreground colour */
      flood_fill(ex, ey, colour, action);
      break;
    case SHIFT_RECTANGLE: {       /* Move or copy a rectangle */
      int32 destleft, destop, left, right, top, bottom;
      if (ds.xlast3 < ds.xlast2) {        /* Figure out left and right hand extents of rectangle */
        left = GXTOPX(ds.xlast3);
        right = GXTOPX(ds.xlast2);
      }
      else {
        left = GXTOPX(ds.xlast2);
        right = GXTOPX(ds.xlast3);
      }
      if (ds.ylast3 > ds.ylast2) {        /* Figure out upper and lower extents of rectangle */
        top = GYTOPY(ds.ylast3);
        bottom = GYTOPY(ds.ylast2);
      }
      else {
        top = GYTOPY(ds.ylast2);
        bottom = GYTOPY(ds.ylast3);
      }
      destleft = GXTOPX(ds.xlast);                /* X coordinate of top left-hand corner of destination */
      destop = GYTOPY(ds.ylast)-(bottom-top);     /* Y coordinate of top left-hand corner of destination */
      plot_rect.x = destleft;
      plot_rect.y = destop;
      temp_rect.x = left;
      temp_rect.y = top;
      temp_rect.w = plot_rect.w = right - left +1;
      temp_rect.h = plot_rect.h = bottom - top +1;
      SDL_BlitSurface(screenbank[ds.writebank], &temp_rect, screen1, &plot_rect); /* copy to temp buffer */
      SDL_BlitSurface(screen1, &plot_rect, screenbank[ds.writebank], &plot_rect);
      hide_cursor();
      blit_scaled(destleft, destop, destleft+(right-left), destop+(bottom-top));
      reveal_cursor();
      if (code == MOVE_RECTANGLE) {       /* Move rectangle - Set original rectangle to the background colour */
        int32 destright, destbot;
        destright = destleft+right-left;
        destbot = destop+bottom-top;
  /* Check if source and destination rectangles overlap */
        if (((destleft >= left && destleft <= right) || (destright >= left && destright <= right)) &&
         ((destop >= top && destop <= bottom) || (destbot >= top && destbot <= bottom))) {        /* Overlap found */
          int32 xdiff, ydiff;
  /*
  ** The area of the original rectangle that is not overlapped can be
  ** broken down into one or two smaller rectangles. Figure out the
  ** coordinates of those rectangles and plot filled rectangles over
  ** them set to the graphics background colour
  */
          xdiff = left-destleft;
          ydiff = top-destop;
          if (ydiff > 0) {        /* Destination area is higher than the original area on screen */
            if (xdiff > 0) {
              plot_rect.x = destright+1;
              plot_rect.y = top;
              plot_rect.w = right - (destright+1) +1;
              plot_rect.h = destbot - top +1;
              SDL_FillRect(screenbank[ds.writebank], &plot_rect, ds.gb_colour);
            }
            else if (xdiff < 0) {
              plot_rect.x = left;
              plot_rect.y = top;
              plot_rect.w = (destleft-1) - left +1;
              plot_rect.h = destbot - top +1;
              SDL_FillRect(screenbank[ds.writebank], &plot_rect, ds.gb_colour);
            }
            plot_rect.x = left;
            plot_rect.y = destbot+1;
            plot_rect.w = right - left +1;
            plot_rect.h = bottom - (destbot+1) +1;
            SDL_FillRect(screenbank[ds.writebank], &plot_rect, ds.gb_colour);
          }
          else if (ydiff == 0) {  /* Destination area is on same level as original area */
            if (xdiff > 0) {      /* Destination area lies to left of original area */
              plot_rect.x = destright+1;
              plot_rect.y = top;
              plot_rect.w = right - (destright+1) +1;
              plot_rect.h = bottom - top +1;
              SDL_FillRect(screenbank[ds.writebank], &plot_rect, ds.gb_colour);
            }
            else if (xdiff < 0) {
              plot_rect.x = left;
              plot_rect.y = top;
              plot_rect.w = (destleft-1) - left +1;
              plot_rect.h = bottom - top +1;
              SDL_FillRect(screenbank[ds.writebank], &plot_rect, ds.gb_colour);
            }
          }
          else {  /* Destination area is lower than original area on screen */
            if (xdiff > 0) {
              plot_rect.x = destright+1;
              plot_rect.y = destop;
              plot_rect.w = right - (destright+1) +1;
              plot_rect.h = bottom - destop +1;
              SDL_FillRect(screenbank[ds.writebank], &plot_rect, ds.gb_colour);
            }
            else if (xdiff < 0) {
              plot_rect.x = left;
              plot_rect.y = destop;
              plot_rect.w = (destleft-1) - left +1;
              plot_rect.h = bottom - destop +1;
              SDL_FillRect(screenbank[ds.writebank], &plot_rect, ds.gb_colour);
            }
            plot_rect.x = left;
            plot_rect.y = top;
            plot_rect.w = right - left +1;
            plot_rect.h = (destop-1) - top +1;
            SDL_FillRect(screenbank[ds.writebank], &plot_rect, ds.gb_colour);
          }
        }
        else {    /* No overlap - Simple case */
          plot_rect.x = left;
          plot_rect.y = top;
          plot_rect.w = right - left +1;
          plot_rect.h = bottom - top +1;
          SDL_FillRect(screenbank[ds.writebank], &plot_rect, ds.gb_colour);
        }
        hide_cursor();
        blit_scaled(left, top, right, bottom);
        reveal_cursor();
      }
      break;
    }
    case PLOT_CIRCLE:             /* Plot the outline of a circle */
    case FILL_CIRCLE: {           /* Plot a filled circle */
      int32 xradius, yradius, xr;
  /*
  ** (xlast2, ylast2) is the centre of the circle. (xlast, ylast) is a
  ** point on the circumference, specifically the left-most point of the
  ** circle.
  */
      // xradius = abs(ds.xlast2-ds.xlast)/ds.xgupp;
      // yradius = abs(ds.xlast2-ds.xlast)/ds.ygupp;
      float fradius=sqrtf((ds.xlast-ds.xlast2)*(ds.xlast-ds.xlast2)+(ds.ylast-ds.ylast2)*(ds.ylast-ds.ylast2));
      xradius = (int)(fradius/ds.xgupp);
      yradius = (int)(fradius/ds.ygupp);
  
      xr=ds.xlast2-ds.xlast;
      if ((code & GRAPHOP_MASK) == PLOT_CIRCLE)
        draw_ellipse(screenbank[ds.writebank], sx, sy, xradius, yradius, 0, colour, action);
      else {
        filled_ellipse(screenbank[ds.writebank], sx, sy, xradius, yradius, 0, colour, action);
      }
      /* To match RISC OS, xlast needs to be the right-most point not left-most. */
      ds.xlast+=(xr*2);
      ex = sx-xradius;
      ey = sy-yradius;
  /* (ex, ey) = coordinates of top left hand corner of the rectangle that contains the ellipse */
      hide_cursor();
      blit_scaled(ex, ey, ex+2*xradius, ey+2*yradius);
      reveal_cursor();
      break;
    }
    case PLOT_ELLIPSE:            /* Draw an ellipse outline */
    case FILL_ELLIPSE: {          /* Draw a filled ellipse */
      int32 semimajor, semiminor, shearx;
  /*
  ** (xlast3, ylast3) is the centre of the ellipse. (xlast2, ylast2) is a
  ** point on the circumference in the +ve X direction and (xlast, ylast)
  ** is a point on the circumference in the +ve Y direction
  */
      semimajor = abs(ds.xlast2-ds.xlast3)/ds.xgupp;
      semiminor = abs(ds.ylast-ds.ylast3)/ds.ygupp;
      tx = GXTOPX(ds.xlast3);
      ty = GYTOPY(ds.ylast3);
      shearx=(GXTOPX(ds.xlast)-tx)*(ds.ylast3 > ds.ylast ? 1 : -1); /* Hopefully this corrects some incorrectly plotted ellipses? */
  
      if ((code & GRAPHOP_MASK) == PLOT_ELLIPSE)
        draw_ellipse(screenbank[ds.writebank], tx, ty, semimajor, semiminor, shearx, colour, action);
      else {
        filled_ellipse(screenbank[ds.writebank], tx, ty, semimajor, semiminor, shearx, colour, action);
      }
      /*
      ** ex = sx-semimajor;  ey = sy-semiminor;
      ** (ex, ey) = coordinates of top left hand corner of the rectangle that contains the ellipse
      */
      hide_cursor();
      blit_scaled(0,0,ds.vscrwidth,ds.vscrheight);
      reveal_cursor();
      break;
    }
    case PLOT_ARC: {
      float xradius,yradius;
  /*
  ** (xlast3, ylast3) is the centre of the arc. (xlast2, ylast2) is a
  ** point on the circumference, and (xlast, ylast) is a point that indicates the end-angle of the arc.
  ** Note that the arc is drawn anti-clockwise from the start point to end angle
  */
      tx = GXTOPX(ds.xlast3);
      ty = GYTOPY(ds.ylast3);
      int xlast3=ds.xlast3/ds.xgupp; // performs integer division
      int ylast3=ds.ylast3/ds.ygupp; // performs integer division
      int xlast2=ds.xlast2/ds.xgupp; // performs integer division
      int ylast2=ds.ylast2/ds.ygupp; // performs integer division
      int xlast=ds.xlast/ds.xgupp; // performs integer division
      int ylast=ds.ylast/ds.ygupp; // performs integer division
      float fradius=sqrtf((xlast3-xlast2)*(xlast3-xlast2)*ds.xgupp*ds.xgupp+(ylast3-ylast2)*(ylast3-ylast2)*ds.ygupp*ds.ygupp)+0.5;
      xradius = (fradius/ds.xgupp);
      yradius = (fradius/ds.ygupp);
      float fradius_end=sqrtf((xlast3-xlast)*(xlast3-xlast)*ds.xgupp*ds.xgupp+(ylast3-ylast)*(ylast3-ylast)*ds.ygupp*ds.ygupp);
      if (fradius_end<1e-9) {
        // the end point is on top of the centre, so there's not much we can do here.
        // Don't do anything in this case, to avoid a division by zero
      } else {
        int end_dx,end_dy;
        int start_dx=xlast2-xlast3;//displacement to start point from centre
        int start_dy=ylast2-ylast3;//displacement to start point from centre
        //fprintf(stderr,"start_dx %d=%d-%d\n",start_dx,ds.xlast2,ds.xlast3);
        end_dx=(int)((xlast-xlast3)*fradius/fradius_end);//projects end point onto circle circumference
        //end_dy=ds.ylast-ds.ylast3;
        end_dy=floor(((ylast-ylast3)*fradius/fradius_end));//projects end point onto circle circumference
  
        // check that the rounding above does not cause end_dx, end_dy to lie off the rasterised curve.
        float axis_ratio = (float) xradius / (float) yradius;
        int xnext_rasterised=abs(end_dy)<yradius?(int)(axis_ratio*sqrt(yradius*yradius-(abs(end_dy)+1)*(abs(end_dy)+1)))+1:0; // uses same formula as used by draw_arc function
        int xthis_rasterised=(int)(axis_ratio*sqrt(yradius*yradius-(abs(end_dy))*(abs(end_dy)) )); // uses same formula as used by draw_arc function
        
        if (abs(end_dy)==yradius && end_dy>0 &&0)
          xnext_rasterised=1;
        if (abs(end_dx)<xnext_rasterised ) {
          // like xnext to be smaller, so like abs(end_dy) to be larger
          //fprintf(stderr,"Going to have error... end point too short xnext_rasterised %d xthis_rasterised %d\n",xnext_rasterised,xthis_rasterised);
          end_dy+=end_dy>0?1:-1;
          //fprintf(stderr,"A xr %f yr %f sdx %d sdy %d edx %d edy %d\n", xradius, yradius, start_dx,start_dy,end_dx,end_dy);
        } 
        if (abs(end_dx)>xthis_rasterised) {
          // like xthis to be bigger, so end_dy to be smaller
          //fprintf(stderr,"Going to have error... end point too long xnext_rasterised %d xthis_rasterised %d\n",xnext_rasterised,xthis_rasterised);
          end_dy-=end_dy>0?1:-1; 
          //fprintf(stderr,"A xr %f yr %f sdx %d sdy %d edx %d edy %d\n", xradius, yradius, start_dx,start_dy,end_dx,end_dy);
        }
  
        int xnext_srasterised=abs(start_dy)<yradius?(int)(axis_ratio*sqrt(yradius*yradius-(abs(start_dy)+1)*(abs(start_dy)+1)))+1:0; // uses same formula as used by draw_arc function
        int xthis_srasterised=(int)(axis_ratio*sqrt(yradius*yradius-(abs(start_dy))*(abs(start_dy)))); // uses same formula as used by draw_arc function
        if (abs(start_dx)<xnext_srasterised ) {
          // like xnext to be smaller, so like abs(end_dy) to be larger
          //fprintf(stderr,"Going to have error... start point too short xnext_srasterised %d xthis_srasterised %d\n",xnext_rasterised,xthis_rasterised);
          start_dy+=start_dy>0?1:-1;
          //fprintf(stderr,"A xr %f yr %f sdx %d sdy %d edx %d edy %d\n", xradius, yradius, start_dx,start_dy,end_dx,end_dy);
        } 
        if (abs(start_dx)>xthis_srasterised) {
          // like xthis to be bigger, so end_dy to be smaller
          //fprintf(stderr,"Going to have error... start point too long xnext_srasterised %d xthis_srasterised %d\n",xnext_rasterised,xthis_rasterised);
          start_dy-=start_dy>0?1:-1; 
          //fprintf(stderr,"A xr %f yr %f sdx %d sdy %d edx %d edy %d\n", xradius, yradius, start_dx,start_dy,end_dx,end_dy);
        }
  
        draw_arc(screenbank[ds.writebank], tx, ty, xradius, yradius, start_dx,start_dy,end_dx,end_dy, colour, action);    
        //plot_pixel(screenbank[ds.writebank], tx, ty, colour, action);
        //plot_pixel(screenbank[ds.writebank], tx+start_dx, ty-start_dy, colour, action);
        hide_cursor();
        blit_scaled(0,0,ds.vscrwidth,ds.vscrheight);
        reveal_cursor();
      }
      break;
    }
    case PLOT_SEGMENT: {
      
      break;
    }
    case PLOT_SECTOR: {
      
      break;
    }
    //default:
      //error(ERR_UNSUPPORTED); /* switch this off, make unhandled plots a no-op*/
  }
#endif
}

/*
** 'emulate_pointfn' emulates the Basic function 'POINT', returning
** the colour number of the point (x,y) on the screen
*/
int32 emulate_pointfn(int32 x, int32 y) {
#ifndef BRANDY_MODE7ONLY
  int32 colour, colnum;
  x += ds.xorigin;
  y += ds.yorigin;
  if ((x < 0) || (x >= ds.screenwidth*ds.xgupp) || (y < 0) || (y >= ds.screenheight*ds.ygupp)) return -1;
  colour = SWAPENDIAN(*((Uint32*)screenbank[ds.writebank]->pixels + (GXTOPX(x) + GYTOPY(y)*ds.vscrwidth)));
  if (colourdepth == COL24BIT) return riscoscolour(colour);
  colnum = emulate_colourfn((colour >> 16) & 0xFF, (colour >> 8) & 0xFF, (colour & 0xFF));
  return colnum;
#else
  return 0;
#endif
}

/*
** 'emulate_tintfn' deals with the Basic keyword 'TINT' when used as
** a function. It returns the 'TINT' value of the point (x, y) on the
** screen. This is one of 0, 0x40, 0x80 or 0xC0
*/
int32 emulate_tintfn(int32 x, int32 y) {
#ifndef BRANDY_MODE7ONLY
  int32 colour;

  if (colourdepth != 256) return 0;
  x += ds.xorigin;
  y += ds.yorigin;
  if ((x < 0) || (x >= ds.screenwidth*ds.xgupp) || (y < 0) || (y >= ds.screenheight*ds.ygupp)) return 0;
  colour = SWAPENDIAN(*((Uint32*)screenbank[ds.writebank]->pixels + (GXTOPX(x) + GYTOPY(y)*ds.vscrwidth))) & 0xFFFFFF;
  return(colour & 3)<<TINTSHIFT;
#else
  return 0;
#endif
}

/*
** 'emulate_pointto' emulates the 'POINT TO' statement.
** Repointed to MOUSE TO as that's a bit more useful.
*/
void emulate_pointto(int32 x, int32 y) {
  //error(ERR_UNSUPPORTED);
  mos_mouse_to(x,y);
}

/*
** 'emulate_wait' deals with the Basic 'WAIT' statement.
** From SDL 1.2.15 manual SDL_Flip waits for vertical retrace before updating the screen.
** This doesn't always work, but better this than a no-op or an Unsupported error message.
*/
void emulate_wait(void) {
  /* Synchronise with the video refresh thread */
  tmsg.videothread=1;
  while (tmsg.videothread) usleep(1000);
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
  for (n=1; n<=8; n++) emulate_vdu(0);
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
#ifndef BRANDY_MODE7ONLY
  int32 n;
  emulate_vdu(VDU_COMMAND);             /* Use VDU 23,17 */
  emulate_vdu(17);
  emulate_vdu(action);  /* Says which colour to modify */
  //if (tint<=MAXTINT) tint = tint<<TINTSHIFT;  /* Assume value is in the wrong place */
  emulate_vdu(tint);
  for (n=1; n<=7; n++) emulate_vdu(0);
#endif
}

/*
** 'emulate_gcol' deals with both forms of the Basic 'GCOL' statement,
** where is it used to either set the graphics colour or to define
** how the VDU drivers carry out graphics operations.
*/
void emulate_gcol(int32 action, int32 colour, int32 tint) {
#ifndef BRANDY_MODE7ONLY
  emulate_vdu(VDU_GRAPHCOL);
  emulate_vdu(action);
  emulate_vdu(colour);
  emulate_tint(colour < 128 ? TINT_FOREGRAPH : TINT_BACKGRAPH, tint);
#endif
}

/*
** emulate_gcolrgb - Called to deal with the 'GCOL <red>,<green>,
** <blue>' version of the GCOL statement. 'background' is set
** to true if the graphics background colour is to be changed
** otherwise the foreground colour is altered
*/
int emulate_gcolrgb(int32 action, int32 background, int32 red, int32 green, int32 blue) {
#ifndef BRANDY_MODE7ONLY
  int32 colnum = emulate_colourfn(red & 0xFF, green & 0xFF, blue & 0xFF);
  if (colourdepth == 256) colnum = colnum << COL256SHIFT;
  emulate_gcolnum(action, background, colnum);
  return(colnum);
#else
  return 0;
#endif
}

/*
** emulate_gcolnum - Called to set the graphics foreground or
** background colour to the colour number 'colnum'. This code
** is a bit of a hack
*/
void emulate_gcolnum(int32 action, int32 background, int32 colnum) {
#ifndef BRANDY_MODE7ONLY
  if (background)
    ds.graph_back_action = action;
  else {
    ds.graph_fore_action = action;
  }
  set_graphics_colour(background, colnum);
#endif
}

/*
** 'emulate_colourtint' deals with the Basic 'COLOUR <colour> TINT' statement
*/
void emulate_colourtint(int32 colour, int32 tint) {
#ifndef BRANDY_MODE7ONLY
  emulate_vdu(VDU_TEXTCOL);
  emulate_vdu(colour);
  emulate_tint(colour<128 ? TINT_FORETEXT : TINT_BACKTEXT, tint);
#endif
}

/*
** 'emulate_mapcolour' handles the Basic 'COLOUR <colour>,<physical colour>'
** statement.
*/
void emulate_mapcolour(int32 colour, int32 physcolour) {
#ifndef BRANDY_MODE7ONLY
  emulate_vdu(VDU_LOGCOL);
  emulate_vdu(colour);
  emulate_vdu(physcolour);      /* Set logical logical colour to given physical colour */
  emulate_vdu(0);
  emulate_vdu(0);
  emulate_vdu(0);
#endif
}

/*
** 'emulate_setcolour' handles the Basic 'COLOUR <red>,<green>,<blue>'
** statement
*/
int32 emulate_setcolour(int32 background, int32 red, int32 green, int32 blue) {
#ifndef BRANDY_MODE7ONLY
  int32 colnum = emulate_colourfn(red & 0xFF, green & 0xFF, blue & 0xFF);
  if (colourdepth == 256) colnum = colnum << COL256SHIFT;
  set_text_colour(background, colnum);
  return(colnum);
#else
  return 0;
#endif
}

/*
** emulate_setcolnum - Called to set the text forground or
** background colour to the colour number 'colnum'
*/
void emulate_setcolnum(int32 background, int32 colnum) {
#ifndef BRANDY_MODE7ONLY
  set_text_colour(background, colnum);
#endif
}

/*
** 'emulate_defcolour' handles the Basic 'COLOUR <colour>,<red>,<green>,<blue>'
** statement
*/
void emulate_defcolour(int32 colour, int32 red, int32 green, int32 blue) {
#ifndef BRANDY_MODE7ONLY
  emulate_vdu(VDU_LOGCOL);
  emulate_vdu(colour);
  emulate_vdu(16);      /* Set both flash palettes for logical colour to given colour */
  emulate_vdu(red);
  emulate_vdu(green);
  emulate_vdu(blue);
#endif
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
#ifndef BRANDY_MODE7ONLY
  int32 slicew, shearx, maxy;
  
  float64 cosv, sinv;
  
  cosv = cos(angle);
  sinv = sin(angle);
  maxy = sqrt(((minorlen*cosv)*(minorlen*cosv))+((majorlen*sinv)*(majorlen*sinv)));
  if (maxy == 0) {
    slicew = majorlen;
    shearx = 0;
  } else {
    slicew = (minorlen*majorlen)/maxy;
    shearx = (cosv*sinv*((majorlen*majorlen)-(minorlen*minorlen)))/maxy;
  }

  emulate_plot(DRAW_SOLIDLINE+MOVE_ABSOLUTE, x, y);        /* Move to centre of ellipse */
  emulate_plot(DRAW_SOLIDLINE+MOVE_ABSOLUTE, x+slicew, y);      /* Find a point on the circumference */
  if (isfilled)
    emulate_plot(FILL_ELLIPSE+DRAW_ABSOLUTE, x+shearx, y+maxy);
  else {
    emulate_plot(PLOT_ELLIPSE+DRAW_ABSOLUTE, x+shearx, y+maxy);
  }
#endif
}

void emulate_circle(int32 x, int32 y, int32 radius, boolean isfilled) {
#ifndef BRANDY_MODE7ONLY
  emulate_plot(DRAW_SOLIDLINE+MOVE_ABSOLUTE, x, y);        /* Move to centre of circle */
  if (isfilled)
    emulate_plot(FILL_CIRCLE+DRAW_ABSOLUTE, x-radius, y);       /* Plot to a point on the circumference */
  else {
    emulate_plot(PLOT_CIRCLE+DRAW_ABSOLUTE, x-radius, y);
  }
#endif
}

/*
** 'emulate_drawrect' draws either an outline of a rectangle or a
** filled rectangle
*/
void emulate_drawrect(int32 x1, int32 y1, int32 width, int32 height, boolean isfilled) {
#ifndef BRANDY_MODE7ONLY
  emulate_plot(DRAW_SOLIDLINE+MOVE_ABSOLUTE, x1, y1);
  if (isfilled)
    emulate_plot(FILL_RECTANGLE+DRAW_RELATIVE, width, height);
  else {
    emulate_plot(DRAW_SOLIDLINE+DRAW_RELATIVE, width, 0);
    emulate_plot(DRAW_SOLIDLINE+DRAW_RELATIVE, 0, height);
    emulate_plot(DRAW_SOLIDLINE+DRAW_RELATIVE, -width, 0);
    emulate_plot(DRAW_SOLIDLINE+DRAW_RELATIVE, 0, -height);
  }
#endif
}

/*
** 'emulate_moverect' is called to either copy an area of the graphics screen
** from one place to another or to move it, clearing its old location to the
** current background colour
*/
void emulate_moverect(int32 x1, int32 y1, int32 width, int32 height, int32 x2, int32 y2, boolean ismove) {
#ifndef BRANDY_MODE7ONLY
  emulate_plot(DRAW_SOLIDLINE+MOVE_ABSOLUTE, x1, y1);
  emulate_plot(DRAW_SOLIDLINE+MOVE_RELATIVE, width, height);
  if (ismove)   /* Move the area just marked */
    emulate_plot(MOVE_RECTANGLE, x2, y2);
  else {
    emulate_plot(COPY_RECTANGLE, x2, y2);
  }
#endif
}

/*
** 'emulate_origin' emulates the Basic statement 'ORIGIN' which
** sets the absolute location of the origin on the graphics screen
*/
void emulate_origin(int32 x, int32 y) {
#ifndef BRANDY_MODE7ONLY
  emulate_vdu(VDU_ORIGIN);
  emulate_vdu(x & BYTEMASK);
  emulate_vdu((x>>BYTESHIFT) & BYTEMASK);
  emulate_vdu(y & BYTEMASK);
  emulate_vdu((y>>BYTESHIFT) & BYTEMASK);
#endif
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
#ifdef TARGET_UNIX
  char *videodriver;
#endif

#ifdef TARGET_MACOSX
  /* Populate the pixfmt structure */
  memset(&pixfmt, 0, sizeof(SDL_PixelFormat));
  pixfmt.BitsPerPixel = 32;
  pixfmt.BytesPerPixel = 4;
  pixfmt.Aloss=8;
  pixfmt.Rshift = 16;
  pixfmt.Gshift = 8;
  pixfmt.Bshift = 0;
  pixfmt.Rmask=0xFF0000;
  pixfmt.Gmask=0xFF00;
  pixfmt.Bmask=0xFF;
  pixfmt.colorkey=0;
  pixfmt.alpha=255;
#endif

#if defined(TARGET_UNIX) && defined(USE_X11)
  XInitThreads();
#endif

  matrixflags.alwaysfullscreen = 0;

  ds.autorefresh=1;
  ds.displaybank=0;
  ds.writebank=0;
  ds.videorefresh=0;

  tmsg.titlepointer = NULL;
  tmsg.modechange = -1;
  tmsg.mousecmd = 0;
  tmsg.x = 0;
  tmsg.y = 0;
  tmsg.crtc6845r10 = 103;
  tmsg.bailout = -1;

  matrixflags.sdl_flags = SDL_DOUBLEBUF | SDL_HWSURFACE | SDL_ASYNCBLIT;
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
    fprintf(stderr, "Unable to init SDL: %s\n", SDL_GetError());
    return FALSE;
  }

#ifdef TARGET_UNIX
  videodriver=malloc(64);
  SDL_VideoDriverName(videodriver, 64);
  /* Are we running on a framebuffer console? */
  if (!strncmp("fbcon", videodriver, 64)) {
    /* Yep. On FBCON the backspace key returns 0x7F (Delete) */
    matrixflags.delcandelete = 1;
    matrixflags.alwaysfullscreen = 1;
  }
  free(videodriver);
#endif
  
  reset_sysfont(0);
  if (basicvars.runflags.swsurface) matrixflags.sdl_flags = SDL_SWSURFACE | SDL_ASYNCBLIT;
  if (!matrixflags.neverfullscreen && basicvars.runflags.startfullscreen) matrixflags.sdl_flags |= SDL_FULLSCREEN;
  matrixflags.surface = SDL_SetVideoMode(640, 512, 32, matrixflags.sdl_flags); /* MODE 0 */
  if (!matrixflags.surface) {
    fprintf(stderr, "Failed to open screen: %s\n", SDL_GetError());
    return FALSE;
  }
  for (p=0; p<MAXBANKS; p++) {
    SDL_FreeSurface(screenbank[p]);
#ifdef TARGET_MACOSX
    screenbank[p] = SDL_ConvertSurface(matrixflags.surface, &pixfmt,0);
#else
    screenbank[p] = SDL_DisplayFormat(matrixflags.surface);
#endif
  }
  ds.displaybank=0;
  ds.writebank=0;
  screen1 = SDL_DisplayFormat(screenbank[0]);
  screen2 = SDL_DisplayFormat(screenbank[0]);
  screen3 = SDL_DisplayFormat(screenbank[0]);
  fontbuf = SDL_CreateRGBSurface(SDL_SWSURFACE,   XPPC,   YPPC, 32, 0xff000000, 0x00ff0000, 0x0000ff00, 0x000000ff);
  m7fontbuf = SDL_CreateRGBSurface(SDL_SWSURFACE, M7XPPC, M7YPPC, 32, 0xff000000, 0x00ff0000, 0x0000ff00, 0x000000ff);
#ifdef TARGET_MACOSX
  sdl_fontbuf = SDL_ConvertSurface(fontbuf, &pixfmt, 0);  /* copy surface to get same format as main windows */
  sdl_m7fontbuf = SDL_ConvertSurface(m7fontbuf, &pixfmt, 0);  /* copy surface to get same format as main windows */
#else
  sdl_fontbuf = SDL_ConvertSurface(fontbuf, matrixflags.surface->format, 0);  /* copy surface to get same format as main windows */
  sdl_m7fontbuf = SDL_ConvertSurface(m7fontbuf, matrixflags.surface->format, 0);  /* copy surface to get same format as main windows */
#endif
  SDL_FreeSurface(fontbuf);
  SDL_FreeSurface(m7fontbuf);

  vdunext = 0;
  vduneeded = 0;
  write_vduflag(VDU_FLAG_ENAPRINT,0);
  ds.xgupp = ds.ygupp = 1;
#ifndef BRANDY_MODE7ONLY
  set_dot_pattern_len(0);
#endif
#if defined(BRANDY_GITCOMMIT) && !defined(BRANDY_RELEASE)
  SDL_WM_SetCaption("Matrix Brandy Basic VI - git " BRANDY_GITCOMMIT, "Matrix Brandy");
#else
  SDL_WM_SetCaption("Matrix Brandy Basic VI", "Matrix Brandy");
#endif
  SDL_EnableUNICODE(SDL_ENABLE);
  SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);

#ifdef TARGET_MACOSX
  ds.xor_mask = SDL_MapRGB(&pixfmt, 0xff, 0xff, 0xff);
#else
  ds.xor_mask = SDL_MapRGB(sdl_fontbuf->format, 0xff, 0xff, 0xff);
#endif

  font_rect.x = font_rect.y = 0;
  font_rect.w = place_rect.w = XPPC;
  font_rect.h = place_rect.h = YPPC;

  place_rect.x = place_rect.y = 0;
  scale_rect.x = scale_rect.y = 0;
  scale_rect.w = scale_rect.h = 1;

#ifdef BRANDY_MODE7ONLY
  for (p=0; p<=6; p++) modetable[p].xres = 0;
  for (p=8; p<=126; p++) modetable[p].xres = 0;
#endif

  if (modetable[matrixflags.startupmode].xres == 0) matrixflags.startupmode=0;
  setup_mode(matrixflags.startupmode);
  if (matrixflags.startupmode != 7) star_refresh(3);

  return TRUE;
}

/*
** 'end_screen' is called to tidy up the VDU emulation at the end
** of the run
*/
void end_screen(void) {
  while (matrixflags.videothreadbusy) usleep(1000);
  matrixflags.noupdate = 1;
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
  if (y <= 6) {
    if (ch & 1) val=left; 
    if (ch & 2) val+=right;
  } else if (y <= 13) {     /* 7-13 */
    if (ch & 4) val=left;
    if (ch & 8) val+=right;
  } else {                  /* 14-19 */
    if (ch & 16) val=left;
    if (ch & 64) val+=right;
  }
  if (vduflag(MODE7_SEPREAL)) {
    if (y == 0 || y == 6 || y == 7 || y == 13 || y==14 || y == 19) val = 0;
    val = val & hmask;
  }
  return(val);
}

static void mode7renderline(int32 ypos, int32 fast) {
  int32 xt;
  int32 mode7prevchar=32;
  Uint8 mode7flash=0, mode7vdu141on=0, mode7vdu141mode=1;
  
  text_physbackcol=text_backcol=0;
  text_physforecol=text_forecol=7;
  set_rgb();

  vduflags &=0x0000FFFF; /* Clear the teletext flags which are reset on a new line */

  if (cursorstate == ONSCREEN) cursorstate = SUSPENDED;
  for (xt=0; xt<=39; xt++) {
    int32 xch=0;
    int32 ch=mode7frame[ypos][xt];
    if (ch < 32) ch |= 0x80;
    /* Check the Set At codes here */
    if (is_teletextctrl(ch)) switch (ch) {
      case TELETEXT_FLASH_OFF:
        mode7flash = 0;
        break;
      case TELETEXT_SIZE_NORMAL:
        if (mode7vdu141on) mode7prevchar=32;
        mode7vdu141on=0;
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
    /* This is the so-called SAA5050 hold bug */
    if(!vduflag(MODE7_HOLD) && !vduflag(MODE7_BLACK)) mode7prevchar=32;

    /* Now we write the character. Copied and optimised from write_char() above */
    place_rect.x = xt*M7XPPC;
    place_rect.y = ypos*M7YPPC;
    SDL_FillRect(sdl_m7fontbuf, NULL, ds.tb_colour);
    if (mode7flash) SDL_BlitSurface(sdl_m7fontbuf, &font_rect, screen3, &place_rect);
    xch=ch;
    if (vduflag(MODE7_HOLD) && ((ch >= 0x80 && ch <= 0x8C) || (ch >= 0x8E && ch <= 0x97 ) || (ch == 0x98 && vduflag(MODE7_REVEAL)) || (ch >= 0x99 && ch <= 0x9F))) {
      ch=mode7prevchar;
    } else {
      if (ch >= 0xA0) ch = ch & 0x7F;
      if (vduflag(MODE7_GRAPHICS)) write_vduflag(MODE7_SEPREAL,vduflag(MODE7_SEPGRP));
    }
    /* Skip this chunk for control codes */
    if ((!is_teletextctrl(ch) && !vduflag(MODE7_CONCEAL)) || vduflag(MODE7_REVEAL)) {
      int32 ch7=(ch & 0x7F), y=0;
      if (vduflag(MODE7_ALTCHARS)) ch |= 0x80;
      if (vduflag(MODE7_GRAPHICS) && ((ch7 >= 0x20 && ch7 <= 0x3F) || (ch7 >= 0x60 && ch7 <= 0x7F))) mode7prevchar=ch;
      for (y=0; y < M7YPPC; y++) {
        int32 line = 0;
        if (mode7vdu141on) {
          int32 yy=((y/2)+(mode7vdu141mode ? 10 : 0));
          if (vduflag(MODE7_GRAPHICS) && ((ch7 >= 0x20 && ch7 <= 0x3F) || (ch7 >= 0x60 && ch7 <= 0x7F)))
            line = teletextgraphic(ch, yy);
          else
            line = mode7font[ch-' '][yy];
        } else {
          if (vdu141track[ypos] != 2) {
            if (vduflag(MODE7_GRAPHICS) && ((ch7 >= 0x20 && ch7 <= 0x3F) || (ch7 >= 0x60 && ch7 <= 0x7F)))
              line = teletextgraphic(ch, y);
            else
              line = mode7font[ch-' '][y];
          }
        }
        if (line!=0) {
          if (line & 0x8000) *((Uint32*)sdl_m7fontbuf->pixels +  0 + y*M7XPPC) = ds.tf_colour;
          if (line & 0x4000) *((Uint32*)sdl_m7fontbuf->pixels +  1 + y*M7XPPC) = ds.tf_colour;
          if (line & 0x2000) *((Uint32*)sdl_m7fontbuf->pixels +  2 + y*M7XPPC) = ds.tf_colour;
          if (line & 0x1000) *((Uint32*)sdl_m7fontbuf->pixels +  3 + y*M7XPPC) = ds.tf_colour;
          if (line & 0x0800) *((Uint32*)sdl_m7fontbuf->pixels +  4 + y*M7XPPC) = ds.tf_colour;
          if (line & 0x0400) *((Uint32*)sdl_m7fontbuf->pixels +  5 + y*M7XPPC) = ds.tf_colour;
          if (line & 0x0200) *((Uint32*)sdl_m7fontbuf->pixels +  6 + y*M7XPPC) = ds.tf_colour;
          if (line & 0x0100) *((Uint32*)sdl_m7fontbuf->pixels +  7 + y*M7XPPC) = ds.tf_colour;
          if (line & 0x0080) *((Uint32*)sdl_m7fontbuf->pixels +  8 + y*M7XPPC) = ds.tf_colour;
          if (line & 0x0040) *((Uint32*)sdl_m7fontbuf->pixels +  9 + y*M7XPPC) = ds.tf_colour;
          if (line & 0x0020) *((Uint32*)sdl_m7fontbuf->pixels + 10 + y*M7XPPC) = ds.tf_colour;
          if (line & 0x0010) *((Uint32*)sdl_m7fontbuf->pixels + 11 + y*M7XPPC) = ds.tf_colour;
          if (line & 0x0008) *((Uint32*)sdl_m7fontbuf->pixels + 12 + y*M7XPPC) = ds.tf_colour;
          if (line & 0x0004) *((Uint32*)sdl_m7fontbuf->pixels + 13 + y*M7XPPC) = ds.tf_colour;
          if (line & 0x0002) *((Uint32*)sdl_m7fontbuf->pixels + 14 + y*M7XPPC) = ds.tf_colour;
          if (line & 0x0001) *((Uint32*)sdl_m7fontbuf->pixels + 15 + y*M7XPPC) = ds.tf_colour;
        }
      }
    }
    if (!mode7flash) SDL_BlitSurface(sdl_m7fontbuf, &font_rect, screen3, &place_rect);
    SDL_BlitSurface(sdl_m7fontbuf, &font_rect, screen2, &place_rect);
    ch=xch; /* restore value */
    /* And now handle the Set After codes */
    if (is_teletextctrl(ch)) switch (ch) {
      case TELETEXT_ALPHA_BLACK:
        if (vduflag(MODE7_BLACK)) {
          write_vduflag(MODE7_GRAPHICS,0);
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
        write_vduflag(MODE7_GRAPHICS,0);
        write_vduflag(MODE7_CONCEAL,0);
        mode7prevchar=32;
        text_physforecol = text_forecol = (ch - 128);
        set_rgb();
        break;
      case TELETEXT_FLASH_ON:
        mode7flash=1;
        break;
      case TELETEXT_SIZE_DOUBLEHEIGHT:
        if (!mode7vdu141on) mode7prevchar=32;
        mode7vdu141on=1;
        if (vdu141track[ypos] < 2) {
          vdu141track[ypos] = 1;
          vdu141track[ypos+1]=2;
          mode7vdu141mode=0;
        } else {
          mode7vdu141mode=1;
        }
        break;
      case TELETEXT_GRAPHICS_BLACK:
        if (vduflag(MODE7_BLACK)) {
          write_vduflag(MODE7_GRAPHICS,1);
          write_vduflag(MODE7_CONCEAL,0);
          if(!vduflag(MODE7_HOLD)) mode7prevchar=32;
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
        write_vduflag(MODE7_GRAPHICS,1);
        write_vduflag(MODE7_CONCEAL,0);
        if(!vduflag(MODE7_HOLD)) mode7prevchar=32;
        text_physforecol = text_forecol = (ch - 144);
        set_rgb();
         break;
      case TELETEXT_GRAPHICS_RELEASE:
        write_vduflag(MODE7_HOLD,0);
        break;
      case TELETEXT_ESCAPE:
        if (vduflag(MODE7_BLACK)) write_vduflag(MODE7_ALTCHARS, vduflag(MODE7_ALTCHARS)? 0 : 1);
    }
  }
}

static void mode7renderscreen(void) {
  int32 ypos;
  
#ifndef BRANDY_MODE7ONLY
  if (screenmode != 7) return;
#endif
  
  for (ypos=0; ypos < 26;ypos++) vdu141track[ypos]=0;
  for (ypos=0; ypos<=24; ypos++) mode7renderline(ypos, 1);
}

#ifndef BRANDY_MODE7ONLY
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
static void draw_h_line(SDL_Surface *sr, int32 x1, int32 x2, int32 y, Uint32 col, Uint32 action) {
  if ((x1 < 0 || x1 >= ds.vscrwidth) && (x2 < 0 || x2 >= ds.vscrwidth )) return;
  if (x1 > x2) {
    int32 tt = x1;
    x1 = x2;
    x2 = tt;
  }
  if ( y >= 0 && y < ds.vscrheight ) {
    int32 i;
    if (x1 < 0) x1 = 0;
    if (x1 >= ds.vscrwidth) x1 = ds.vscrwidth-1;
    if (x2 < 0) x2 = 0;
    if (x2 >= ds.vscrwidth) x2 = ds.vscrwidth-1;
    for (i = x1; i <= x2; i++)
      plot_pixel(sr, i, y, col, action);
  }
}

/*
** Draw a filled polygon of n vertices
*/
static void buff_convex_poly(SDL_Surface *sr, int32 n, int32 *x, int32 *y, Uint32 col, Uint32 action) {
  int32 i, iy;
  int32 low = 32767, high = -1;

  /* set highest and lowest points to visit */
  for (i = 0; i < n; i++) {
    if (y[i] > high) high = y[i];
    if (y[i] < low) low = y[i];
  }
  /* reset the minimum amount of the edge tables */
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
    draw_h_line(sr, geom_left[iy], geom_right[iy], iy, col, action);
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
  int i;
  int w = x2 - x1;
  int h = y2 - y1;
  int mask = (style & 0x38);
  int dotted =     (mask == 0x10 || mask == 0x18 || mask == 0x30 || mask == 0x38); // Dotted line
  int omit_first = (mask == 0x20 || mask == 0x28 || mask == 0x30 || mask == 0x38); // Omit first
  int omit_last =  (mask == 0x08 || mask == 0x18 || mask == 0x28 || mask == 0x38); // Omit last
  int dx1 = 0, dy1 = 0, dx2 = 0, dy2 = 0;

  if (w < 0) {
    dx1 = -1;
  } else if (w > 0) {
    dx1 = 1;
  }
  if (h < 0) {
    dy1 = -1;
  } else if (h > 0) {
    dy1 = 1;
  }
  if (w < 0) {
    dx2 = -1;
  } else if (w > 0) {
    dx2 = 1;
  }
  int longest = abs(w);
  int shortest = abs(h);
  if (!(longest > shortest)) {
    longest = abs(h);
    shortest = abs(w);
    if (h < 0) {
      dy2 = -1;
    } else if (h > 0) {
      dy2 = 1;
    }
    dx2 = 0;
  }
  int numerator = longest >> 1 ;
  int x = x1;
  int y = y1;
  if (omit_last) {
    longest--;
  }
  int start = 0;
  // restart the dot pattern if the first point is plotted
  if (!omit_first) {
    dot_pattern_index = 0;
  }
  for (i = start; i <= longest; i++) {
    if (i > start || !omit_first) {
      if (dotted) {
        if (dot_pattern[dot_pattern_index++]) {
          plot_pixel(sr, x, y, col, action);
        }
        if (dot_pattern_index == dot_pattern_len) {
          dot_pattern_index = 0;
        }
      } else {
        plot_pixel(sr, x, y, col, action);
      }
    }
    numerator += shortest;
    if (!(numerator < longest)) {
      numerator -= longest;
      x += dx1;
      y += dy1;
    } else {
      x += dx2;
      y += dy2;
    }
  }
}

/*
** 'filled_triangle' draws a filled triangle in the graphics buffer 'sr'.
*/
static void filled_triangle(SDL_Surface *sr, int32 x1, int32 y1, int32 x2, int32 y2,
                     int32 x3, int32 y3, Uint32 col, Uint32 action) {
  int x[3], y[3];

  x[0]=x1; x[1]=x2; x[2]=x3;
  y[0]=y1; y[1]=y2; y[2]=y3;
  buff_convex_poly(sr, 3, x, y, col, action);
}

/*
** Draw an ellipse into a buffer
** This code is based on the RISC OS implementation, as translated from ARM to C by hoglet@Stardot for PiTubeDirect.
*/
static void draw_ellipse(SDL_Surface *screen, int32 xc, int32 yc, int32 width, int32 height, int32 shear, Uint32 colour, Uint32 action) {
  if (height == 0) {
    draw_h_line(screen,xc-width, xc+width, yc, colour, action);
  } else {
      float axis_ratio = (float) width / (float) height;
      float shear_per_line = (float) (shear) / (float) height;
      float xshear = 0.0;
      int y=0;
      int odd_sequence = 1;
      int y_squared = 0;
      int h_squared = height * height;
      // Maintain the left/right coordinated of the previous, current, and next slices
      // to allow lines to be drawn to make sure the pixels are connected
      int xl_prev = 0;
      int xr_prev = 0;
      int xl_this = 0;
      int xr_this = 0;
      // Start at -1 to allow the pipeline to fill
      for (y = -1; y < height; y++) {
         float x = axis_ratio * sqrtf(h_squared - y_squared);
         int xl_next = (int) (xshear - x);
         int xr_next = (int) (xshear + x);
         xshear += shear_per_line;
         // It's probably quicker to just use y * y
         y_squared += odd_sequence;
         odd_sequence += 2;
         // Initialize the pipeline for the first slice
         if (y == 0) {
            xl_prev = -xr_next;
            xr_prev = -xl_next;
         }
         // Draw the slice as a single horizontal line
         if (y >= 0) {
            // Left line runs from xl_this rightwards to MAX(xl_this, MAX(xl_prev, xl_next) - 1)
            int xl = MAX(xl_this, MAX(xl_prev, xl_next) - 1);
            // Right line runs from xr_this leftwards to MIN(xr_this, MIN(xr_prev, xr_next) + 1)
            int xr = MIN(xr_this, MIN(xr_prev, xr_next) + 1);
            draw_h_line(screen, xc + xl_this, xc + xl, yc + y, colour, action);
            draw_h_line(screen, xc + xr_this, xc + xr, yc + y, colour, action);
            if (y > 0) {
               draw_h_line(screen, xc - xl_this, xc - xl, yc - y, colour, action);
               draw_h_line(screen, xc - xr_this, xc - xr, yc - y, colour, action);
            }
         }
         xl_prev = xl_this;
         xr_prev = xr_this;
         xl_this = xl_next;
         xr_this = xr_next;
      }
      // Draw the final slice
      draw_h_line(screen, xc + xl_this, xc + xr_this, yc + height, colour, action);
      draw_h_line(screen, xc - xl_this, xc - xr_this, yc - height, colour, action);
  }
}

/*
** Draw a filled ellipse into a buffer
*/

static void filled_ellipse(SDL_Surface *screen, 
  int32 xc, /* Centre X */
  int32 yc, /* Centre Y */
  int32 width, /* Width */
  int32 height, /* Height */
  int32 shear, /* X shear */
  Uint32 colour, Uint32 action
) {
  if (height == 0) {
    draw_h_line(screen, xc - width, xc + width, yc, colour, action);
  } else {
    float axis_ratio = (float) width / (float) height;
    float shear_per_line = (float) (shear) / (float) height;
    float xshear = 0.0;
    int y=0;
    int odd_sequence = 1;
    int y_squared = 0;
    int h_squared = height * height;
    for (y = 0; y <= height; y++) {
      float x = axis_ratio * sqrtf(h_squared - y_squared);
      int xl = (int) (xshear - x);
      int xr = (int) (xshear + x);
      xshear += shear_per_line;
      // It's probably quicker to just use y * y
      y_squared += odd_sequence;
      odd_sequence += 2;
      // Draw the slice as a single horizontal line
      draw_h_line(screen, xc + xl, xc + xr, yc + y, colour, action);
      if (y > 0) {
         draw_h_line(screen, xc - xl, xc - xr, yc - y, colour, action);
      }
    }
  }
}

/*
** Draw an arc into a buffer
*/
static void draw_arc(SDL_Surface *screen, int32 xc, int32 yc, float xradius, float yradius, int32 start_dx, int32 start_dy, int32 end_dx, int32 end_dy, Uint32 colour, Uint32 action) {
  // Use the same logic as the draw-circle routine, i.e. we draw the circle one row at a time, both left and right sides simultaneously.
  // However for the arc, we filter exactly when to plot a point on the left and when to plot a point on the right.
  int height=(int)yradius;
  if (height == 0)
    draw_h_line(screen,xc+start_dx, xc+end_dx, yc, colour, action);
  else if (end_dy==start_dy && start_dx*end_dx>=0 && ((start_dx>=end_dx && end_dy>0)||(start_dx<=end_dx && end_dy<0))) {
    // This arc starts and finishes on the same height, and is a minor arc, so it's just a single hline.
    // Because this is a highly unusual situation which will otherwise break everything, it's best to handle it here right now and exit.
    draw_h_line(screen,xc+start_dx, xc+end_dx, yc-end_dy, colour, action);
  } else {
    // We want to find the y-coordinates to plot on the left (we find y1l,y2l,y3l such that we plot all y-coords which satisfy (y1l<=y<=y2l or y>=y3l))
    // Similarly, we want to find the y-coors to plot on the right  (we find y1r,y2r,y3r such that we plot all y-coords which satisfy (y1r<=y<=y2r or y>=y3r))
    // Here, we assume the coordiante system is such that positive y goes upwards (which is WRONG but simpler to think about for now...)
    int32 y1r,y2r,y3r,y1l,y2l,y3l;
    if (start_dx>0 || (start_dx==0 && start_dy<0)) {
      if (end_dx<0 || (end_dx==0 && end_dy<0) || end_dy>start_dy || (end_dy==start_dy && ((start_dy>0 && end_dx<start_dx) || (start_dy<0 && end_dx>start_dx)))) {
        // it definitely starts going upwards on the right, and possibly over the top
        int passes_over_the_top=(end_dx<0 || (end_dx==0 && end_dy<0));
        y1r=start_dy;
        y3r=height+1;
        y2r=passes_over_the_top?height+1:end_dy;
        if (passes_over_the_top) {
           // we're finishing off over the top
           y1l=end_dy;
           y2l=y3l=height+1;
         } else {
           // There's nothing to draw on the left.
           y1l=y2l=y3l=height+1;
         }
      } else {
        // We have something poking from bottom of right half.
        // end_dx >=0 and end_dy<=start_dy...
        y1r=-height-1;
        y2r=end_dy;
        y3r=start_dy;
        // fill in all of lhs
        y1l=-height-1;
        y2l=y3l= height+1;
      }
    } else {
      // start_dx<0...
      if (end_dx>0 || (end_dx==0 && end_dy>0) || end_dy<start_dy || (end_dy==start_dy && ((start_dy>0 && end_dx<start_dx) || (start_dy<0 && end_dx>start_dx)))) {
        // it definitely starts off going downwards on the left, and possibly around the bottom
        int passes_around_bottom=(end_dx>0 || (end_dx==0 && end_dy>0));
        y3l=height+1;
        y2l=start_dy;
        y1l=passes_around_bottom?-height-1:end_dy;
        if (passes_around_bottom) {
          // yes it goes around the bottom
          y1r=-height-1;
          y2r=end_dy;
          y3r=height+1;
        } else {
          // there's nothing to draw on the right
          y1r=y2r=y3r=height+1;
        }
      } else {
        // we have something finishing off around the top left...
        // end_dx<0 and end_dy>start_dy
        y3l=end_dy;
        y2l=start_dy;
        y1l=-height-1;
        // fill in all of rhs...
        y1r=-height-1;
        y2r=y3r=height+1;
      }
    }
    if (y1l>y2l || y2l>y3l) fprintf(stderr, "WARN, not in increasing order yL %d %d %d \n", y1l,y2l,y3l);
    if (y1r>y2r || y2r>y3r) fprintf(stderr, "WARN, not in increasing order yR %d %d %d \n", y1r,y2r,y3r);

    // the following code is copied from the draw_ellipse function, and modified slightly.
    // The main modification is that we should only draw a line on the left if the y coordinate satisfies (y1l<=y<=y2l or y>=y3l).  Similarly for the right.
    float axis_ratio = (float) xradius / (float) yradius;
    int y=0;
    float h_squared = yradius * yradius;
    // Maintain the left/right coordinated of the previous, current, and next slices
    // to allow lines to be drawn to make sure the pixels are connected
    int xl_this = 0;
    int xr_this = 0;
    // Start at -1 to allow the pipeline to fill
    for (y = -1; y < height+1; y++) {
      int y_squared=(y+1)*(y+1);
      float x_next = y<height?(axis_ratio * sqrtf(h_squared - y_squared)):0;
      int xl_next = (int) ( - x_next);
      int xr_next = (int) ( + x_next);
      // Initialize the pipeline for the first slice
      // Draw the slice as a single horizontal line
      if (y >= 0) {
        // Left line runs from xl_this rightwards to MAX(xl_this, MAX(xl_prev, xl_next) - 1)
        int xl = MAX(xl_this, xl_next - 1);
        // Right line runs from xr_this leftwards to MIN(xr_this, MIN(xr_prev, xr_next) + 1)
        int xr = MIN(xr_this, xr_next + 1);

        // should be simple to filter each point now, however there is potentially some extra clipping
        // if we are on the start of the arc (at start_dx,start_dy) or the end of the arc (at end_dx,end_dy).
        // So "clipping" is only referring to the start/end of the arc; 
        // "clipping" does not refer to the sides of the graphics window - that is handled separately by the draw_h_line function.
        if (y==height) {
          xl=-1; // the final top and bottom hlines should not meet in the middle (in case we have GCOL3,x EOR colour selected)
          xr=0;
        }
        int x1=xc + xr;
        int x2=xc + xr_this;
        // Note that earlier the filtering to generate y1l,y2l,y3l,y1r,y2r,y3r was assuming positive y is upwards.
        // As this was not true, we have a -y appearing often below....!
        
        // bottom right quadrant:
        if (-y==end_dy && end_dx>=0) {// bottom right clipping
          x2=xc + end_dx;
        }
        if (-y==start_dy && start_dx>=0) {// bottom right clipping
          x1=xc + start_dx;
        }
        if ((-y>=y1r && -y<=y2r)||-y>=y3r) {
          if (x1<=x2)
            draw_h_line(screen, x1, x2, yc + y, colour, action);
          else {
            // really unusual situation due to clipping x1 and x2 have swapped over.  Hence need two draw statements:
            if (xc+xr<=x2) draw_h_line(screen, xc+xr, x2, yc + y, colour, action);
            if (x1<=xc+xr_this) draw_h_line(screen, x1, xc + xr_this, yc + y, colour, action);
          }
        }
        //bottom left quadrant:
        x2=xc + xl;
        x1=xc + xl_this;
        if (-y==end_dy && end_dx<0) {// bottom left clipping
          x2=xc + end_dx;
        }
        if (-y==start_dy && start_dx<0) {// bottom left clipping
          x1=xc + start_dx;
        }
        if ((-y>=y1l && -y<=y2l)||-y>=y3l) {
          if (x1<=x2)
            draw_h_line(screen, x1, x2, yc + y, colour, action);
          else {
            // really unusual situation due to clipping x1 and x2 have swapped over.  Hence need two draw statements:
            if (xc+xl_this<=x2) draw_h_line(screen, xc + xl_this, x2, yc + y, colour, action);
            if (x1<xc+xl) draw_h_line(screen, x1, xc + xl, yc + y, colour, action);
          }
        }

        if (y > 0) {
          // top right quadrant
          x1=xc - xl;
          x2=xc - xl_this;
          if (y==end_dy && end_dx>=0) {// top right clipping of LHS of the hline
            x1=MAX(x1,xc + end_dx);
          }
          if (y==start_dy && start_dx>=0) {// top right clipping of RHS of the hline
            x2=MIN(x2,xc + start_dx);
          }
          if ((y>=y1r && y<=y2r)||y>=y3r) {
            if (x1<=x2)
              draw_h_line(screen, x1, x2, yc - y, colour, action);
            else {
              // really unusual situation due to clipping x1 and x2 have swapped over.  Hence need two draw statements:
              if (xc-xl<=x2) draw_h_line(screen, xc - xl, x2, yc - y, colour, action);// This is drawing the unwanted left pixel!!!!  from xc-xl=161 to x2=160
              if (x1<=xc-xl_this) draw_h_line(screen, x1, xc - xl_this, yc - y, colour, action); // this is drawing the wanted right pixel
            }
          }

          // top left quadrant
          x2=xc - xr;
          x1=xc - xr_this;
          if (y==end_dy && end_dx<0) // top left clipping
             x1=xc + end_dx;
          if (y==start_dy && start_dx<=0) // top left clipping
             x2=xc + start_dx;
          if ((y>=y1l && y<=y2l)||y>=y3l) {
            if (x1<=x2)
              draw_h_line(screen, x1,x2, yc - y, colour, action);
            else {
              // really unusual situation due to clipping x1 and x2 have swapped over.  Hence need two draw statements:
              if (xc-xr_this<=x2) draw_h_line(screen, xc - xr_this, x2, yc - y, colour, action);
              if (x1<=xc-xr) draw_h_line(screen, x1, xc - xr, yc - y, colour, action);
            }
          }
        }
      }
      xl_this = xl_next;
      xr_this = xr_next;
    }
  }
}

#endif /* BRANDY_MODE7ONLY */

Uint8 mousebuttonstate = 0;

void get_sdl_mouse(size_t values[]) {
  int x, y;
  int breakout = 0;
  SDL_Event ev;

  /* Check the mouse queue first */
  drain_mouse_expired();
  if (mousebuffer != NULL) {
    int mx, my;
    mx=(mousebuffer->x * (ds.xgupp / ds.xscale));
    if (mx < 0) mx = 0;
    if (mx >= ds.xgraphunits) mx = (ds.xgraphunits - 1);
    mx -= ds.xorigin;

    my=((ds.vscrheight - mousebuffer->y) * (ds.ygupp / ds.yscale));
    if (my < 0) my = 0;
    if (my >= ds.ygraphunits) my = (ds.ygraphunits - 1);
    my -= ds.yorigin;

    mousequeue *m;
    values[0]=mx;
    values[1]=my;
    values[2]=mousebuffer->buttons;
    values[3]=mousebuffer->timestamp - basicvars.monotonictimebase;
    m=mousebuffer->next;
    free(mousebuffer);
    mousebuffer=m;
    mousequeuelength--;
    return;
  }

  /* If we got here, there's nothing in the mouse queue, so let's
  ** pick up a fresh item from SDL.
  */

  if (mousequeuelength != 0) fprintf(stderr,"Warning: mousequeuelength out of sync (%u), correcting\n", mousequeuelength);
  mousequeuelength=0; /* Not strictly necessary, but keeps things in sync, just in case */
  while (matrixflags.videothreadbusy) usleep(1000);
  SDL_GetMouseState(&x, &y);
  while(!breakout && SDL_PeepEvents(&ev,1,SDL_GETEVENT, -1 ^ (SDL_EVENTMASK(SDL_KEYDOWN) | SDL_EVENTMASK(SDL_KEYUP)))) {
    switch (ev.type) {
      case SDL_QUIT:
        exit_interpreter(EXIT_SUCCESS);
        breakout=1;
        break;
      case SDL_MOUSEBUTTONDOWN:
        if (ev.button.button == SDL_BUTTON_LEFT) mousebuttonstate |= 4;
        if (ev.button.button == SDL_BUTTON_MIDDLE) mousebuttonstate |= 2;
        if (ev.button.button == SDL_BUTTON_RIGHT) mousebuttonstate |= 1;
        breakout=1;
        break;
      case SDL_MOUSEBUTTONUP:
        if (ev.button.button == SDL_BUTTON_LEFT) mousebuttonstate &= 3;
        if (ev.button.button == SDL_BUTTON_MIDDLE) mousebuttonstate &= 5;
        if (ev.button.button == SDL_BUTTON_RIGHT) mousebuttonstate &= 6;
        breakout=1;
        break;
    }
  }

  x=(x * (ds.xgupp / ds.xscale));
  if (x < 0) x = 0;
  if (x >= ds.xgraphunits) x = (ds.xgraphunits - 1);
  x -= ds.xorigin;

  y=((ds.vscrheight-y) * (ds.ygupp / ds.yscale));
  if (y < 0) y = 0;
  if (y >= ds.ygraphunits) y = (ds.ygraphunits - 1);
  y -= ds.yorigin;

  values[0]=x;
  values[1]=y;
  values[2]=mousebuttonstate;
  values[3]=basicvars.centiseconds - basicvars.monotonictimebase;
}

void warp_sdlmouse(int32 x, int32 y) {
  tmsg.x = x + ds.xorigin;
  tmsg.y = y + ds.yorigin;
  tmsg.mousecmd = 2;
}

void sdl_mouse_onoff(int state) {
  tmsg.x = state;
  tmsg.mousecmd = 1;
}

void set_wintitle(char *title) {
  /* This is picked up by the video update thread */
  memset(titlestring, 0, 256);
  STRLCPY(titlestring, title, titlestringLen);
  tmsg.titlepointer = titlestring;
}

void fullscreenmode(int onoff) {
  if (!matrixflags.neverfullscreen) {
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
  }
  tmsg.modechange = -1;
}

void setupnewmode(int32 mode, int32 xres, int32 yres, int32 cols, int32 mxscale, int32 myscale, int32 xeig, int32 yeig) {
#ifndef BRANDY_MODE7ONLY
  if ((mode < 64) || (mode > HIGHMODE)) {
    emulate_printf("Warning: Can only define modes in the range 64 to %d.\r\n", HIGHMODE);
    return;
  }
  if ((cols != 2) && (cols != 4) && (cols != 16) && (cols != 256) && (cols != 32768) && (cols != COL24BIT)) {
    emulate_printf("Warning: Can only define modes with 2, 4, 16, 256 or 16777216 colours.\r\n");
    return;
  }
  if (cols == 32768) cols=COL24BIT;
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
#endif
}

void refresh_location(uint32 offset) {
#ifndef BRANDY_MODE7ONLY
  uint32 ox,oy;

  ox=offset % ds.screenwidth;
  oy=offset / ds.screenwidth;
  blit_scaled(ox,oy,ox,oy);
#endif
}

/* 0=off, 1=on, 2=onerror */
void star_refresh(int flag) {
  while (matrixflags.videothreadbusy) usleep(1000);
  if ((flag == 0) || (flag == 1) || (flag==2)) {
    ds.autorefresh=flag;
  }
  if (flag & 1) {
#ifndef BRANDY_MODE7ONLY
    if (screenmode == 7) {
#endif
      int tmpflag=ds.autorefresh;
      ds.autorefresh=1;
      tmsg.mode7forcerefresh=1;
      while (!matrixflags.videothreadbusy) usleep(1000);
      while (matrixflags.videothreadbusy) usleep(1000);
      ds.autorefresh=tmpflag;
      return;
#ifndef BRANDY_MODE7ONLY
    } else {
      matrixflags.noupdate = 1;
      blit_scaled_actual(0,0,ds.screenwidth-1,ds.screenheight-1);
    }
#endif
    SDL_Flip(matrixflags.surface);
  }
  matrixflags.noupdate = 0;
}

int get_refreshmode(void) {
  return ds.autorefresh;
}

int32 get_character_at_pos(int32 cx, int32 cy) {
  if ((cx < 0) || (cy < 0) || (cx > (twinright-twinleft)) || (cy > (twinbottom-twintop))) return -1;
  cx+=twinleft;
  cy+=twintop;
#ifndef BRANDY_MODE7ONLY
  if (screenmode == 7) {
#endif
    int32 charvalue=mode7frame[cy][cx];
    switch (charvalue) {
      case 35: charvalue=96; break;
      case 96: charvalue=95; break;
      case 95: charvalue=35; break;
    }
    return (charvalue);
#ifndef BRANDY_MODE7ONLY
  } else {
    int32 topx, topy, bgc, y, match;
    unsigned char cell[8];
    /* Get address of character cell */
    topx = cx*XPPC;
    topy = cy*YPPC;
    /* Let's assume the top left pixel is in background colour */
    bgc=*((Uint32*)screenbank[ds.displaybank]->pixels + topx + 0 + ((topy)*ds.vscrwidth));
    for (y=0; y < 8; y++) {
      cell[y]=0;
      if (*((Uint32*)screenbank[ds.displaybank]->pixels + topx + 0 + ((topy+y)*ds.vscrwidth)) != bgc) cell[y] |= 0x80;
      if (*((Uint32*)screenbank[ds.displaybank]->pixels + topx + 1 + ((topy+y)*ds.vscrwidth)) != bgc) cell[y] |= 0x40;
      if (*((Uint32*)screenbank[ds.displaybank]->pixels + topx + 2 + ((topy+y)*ds.vscrwidth)) != bgc) cell[y] |= 0x20;
      if (*((Uint32*)screenbank[ds.displaybank]->pixels + topx + 3 + ((topy+y)*ds.vscrwidth)) != bgc) cell[y] |= 0x10;
      if (*((Uint32*)screenbank[ds.displaybank]->pixels + topx + 4 + ((topy+y)*ds.vscrwidth)) != bgc) cell[y] |= 0x08;
      if (*((Uint32*)screenbank[ds.displaybank]->pixels + topx + 5 + ((topy+y)*ds.vscrwidth)) != bgc) cell[y] |= 0x04;
      if (*((Uint32*)screenbank[ds.displaybank]->pixels + topx + 6 + ((topy+y)*ds.vscrwidth)) != bgc) cell[y] |= 0x02;
      if (*((Uint32*)screenbank[ds.displaybank]->pixels + topx + 7 + ((topy+y)*ds.vscrwidth)) != bgc) cell[y] |= 0x01;
    }
    /* Got the character shape of the cell. Now find it in the sysfont structure */
    match=0;
    for (y=32; y <= 255; y++) {
      if (cell[0] == sysfont[y-32][0] && cell[1] == sysfont[y-32][1] && cell[2] == sysfont[y-32][2] && cell[3] == sysfont[y-32][3] && cell[4] == sysfont[y-32][4] && cell[5] == sysfont[y-32][5] && cell[6] == sysfont[y-32][6] && cell[7] == sysfont[y-32][7]) {
        /* Match */
        match=y;
        break; 
      }
    }
    if (!match) {
      /* No match, let's try inverse video */
      for (y=0; y<8;y++) cell[y] ^= 0xFF;
      for (y=32; y <= 255; y++) {
        if (cell[0] == sysfont[y-32][0] && cell[1] == sysfont[y-32][1] && cell[2] == sysfont[y-32][2] && cell[3] == sysfont[y-32][3] && cell[4] == sysfont[y-32][4] && cell[5] == sysfont[y-32][5] && cell[6] == sysfont[y-32][6] && cell[7] == sysfont[y-32][7]) {
          /* Match */
          match=y;
          break; 
        }
      }
    }
    return (match);
  }
#endif
}

int32 osbyte163_2(int x) {
  int fullscreen=0, ref=(x & 3), fsc=((x & 12) >> 2);
  
  if (matrixflags.surface->flags & SDL_FULLSCREEN) fullscreen=8;
  if (x == 0) {
    int outx = fullscreen + (ds.autorefresh+1);
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
  if (fsc) {
    tmsg.modechange = 0x400 + (fsc-1);
    while (tmsg.modechange >= 0) usleep(1000);
  }
  /* If bit 4 set, do immediate refrsh */
  if (x & 16) star_refresh(3);
  return((x << 8) + 42);
}

void osbyte112(int x) {
#ifndef BRANDY_MODE7ONLY
  /* OSBYTE 112 selects which bank of video memory is to be written to */
  sysvar[250]=x;
  if (screenmode == 7) return;
  if (x==0) x=1;
  if (x <= MAXBANKS) ds.writebank=(x-1);
  matrixflags.modescreen_ptr = screenbank[ds.writebank]->pixels;
#endif
}

void osbyte113(int x) {
#ifndef BRANDY_MODE7ONLY
  /* OSBYTE 113 selects which bank of video memory is to be displayed */
  sysvar[251]=x;
  if (screenmode == 7) return;
  if (x==0) x=1;
  if (x <= MAXBANKS) ds.displaybank=(x-1);
  blit_scaled_actual(0, 0, ds.screenwidth-1, ds.screenheight-1);
#endif
}

void screencopy(int32 src, int32 dst) {
  SDL_BlitSurface(screenbank[src-1],NULL,screenbank[dst-1],NULL);
  if (dst==(ds.displaybank+1)) {
    SDL_BlitSurface(screenbank[ds.displaybank], NULL, matrixflags.surface, NULL);
  }
}

int32 osbyte134_165(int32 a) {
  return ((ytext << 16) + (xtext << 8) + a);
}

int32 osbyte135() {
    return ((screenmode << 16) + (get_character_at_pos(xtext-twinleft,ytext-twintop) << 8) + 135);
}

int32 osbyte163_242(int a) {
#ifndef BRANDY_MODE7ONLY
  if (a <= 64) set_dot_pattern_len(a);
  if (a <= 65) return((dot_pattern_len & 0x3F) | 0xC0);
#endif
  return(0);
}

int32 osbyte250() {
  return (((ds.displaybank+1) << 16) + ((ds.writebank+1) << 8) + 250);
}

int32 osbyte251() {
  return (((ds.displaybank+1) << 8) + 251);
}

void osword09(int64 x) {
#ifndef BRANDY_MODE7ONLY
  unsigned char *block;
  int32 px, py;

  block=(unsigned char *)(size_t)x;
  px=block[0] + (block[1] << 8);
  py=block[2] + (block[3] << 8);
  block[4] = emulate_pointfn(px, py);
#endif
}

void osword0A(int64 x) {
#ifndef BRANDY_MODE7ONLY
  unsigned char *block;
  size_t offset;
  int32 i;
  
  block=(unsigned char *)(size_t)x;
  switch(block[0]) {
    case 6:
      for (i=0; i<= 7; i++) block[i+1]=dot_pattern_packed[i];
      break;
    default:
      if (block[0] < 32) return;
      offset=block[0]-32;
      for (i=0; i<= 7; i++) block[i+1]=sysfont[offset][i];
  }
#endif
}

void osword0B(int64 x) {
  unsigned char *block;
  
  block=(unsigned char *)(size_t)x;
  block[1] = 16;
  block[2] = palette[block[0]*3+0];
  block[3] = palette[block[0]*3+1];
  block[4] = palette[block[0]*3+2];
}

int32 os_readpalette(int32 colour, int32 mode) {
  if (mode != 16) return 0;
  return 16 + (palette[colour*3+0] << 8) + (palette[colour*3+1] << 16) + (palette[colour*3+2] << 24);
}

void osword0C(int64 x) {
#ifndef BRANDY_MODE7ONLY
  unsigned char *block;
  int32 logcol, pmode, mode;
  
  block=(unsigned char *)(size_t)x;
  if (screenmode == 7) return;
  logcol = block[0] & colourmask;
  mode = block[1];
  pmode = mode % 16;
  if (mode < 16 && colourdepth <= 16) { /* Just change the RISC OS logical to physical colour mapping */
    logtophys[logcol] = mode;
    palette[logcol*3+0] = hardpalette[pmode*3+0];
    palette[logcol*3+1] = hardpalette[pmode*3+1];
    palette[logcol*3+2] = hardpalette[pmode*3+2];
  } else if (mode == 16)        /* Change the palette entry for colour 'logcol' */
    change_palette(logcol, block[2], block[3], block[4]);
  set_rgb();
  /* Now, go through the framebuffer and change the pixels */
  if (colourdepth <= 256) {
    int32 offset, c = logcol * 3;
    int32 newcol = SDL_MapRGB(sdl_fontbuf->format, palette[c+0], palette[c+1], palette[c+2]) + (logcol << 24);
    for (offset=0; offset < (ds.screenheight*ds.screenwidth*ds.xscale); offset++) {
      if ((SWAPENDIAN(*((Uint32*)screenbank[ds.writebank]->pixels + offset)) >> 24) == logcol) *((Uint32*)screenbank[ds.writebank]->pixels + offset) = SWAPENDIAN(newcol);
    }
    blit_scaled(0,0,ds.screenwidth-1,ds.screenheight-1);
  }
#endif
}

/* Like OSWORD 10 but for the MODE 7 16x20 font
 */
void osword8B(int64 x) {
  unsigned char *block;
  int32 i, ch, chbank;

  block=(unsigned char *)(size_t)x;
  if ( ( block[0] < 4 ) || ( block[1] < 44 ) ) return;
  ch=block[2];
  chbank=block[3];
  if (chbank == 0) {
    int32 offset;
    if ((ch < 0x20) || (ch > 0xFF)) return;
    offset = ch -32;
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

/* Write a character definition to the current MODE 7 font.
 */
void osword8C(int64 x) {
  unsigned char *block;
  int32 offset, i, ch;

  block=(unsigned char *)(size_t)x;
  if (block[0] < 44) return;
  ch=block[2];
  if ((ch < 0x20) || (ch > 0xFF)) return;
  offset = ch -32;
  for (i=0; i<= 19; i++) {
    mode7font[offset][i] = block[(2*i)+5] + (256*block[(2*i)+4]);
  }
}

void swi_os_setcolour(int32 r0, int32 r1) {
  /* ECF not supported, so no-op if ECF bit is set. Also, read colour bit is poorly documented. */
  int c64shuff[]={ /* The colours are shuffled, this is the pattern required to match RISC OS 3.71 */
    0,1,16,17,2,3,18,19,4,5,20,21,6,7,22,23,8,9,24,25,10,11,26,27,12,13,28,29,14,15,30,31,
    32,33,48,49,34,35,50,51,36,37,52,53,38,39,54,55,40,41,56,57,42,43,58,59,44,45,60,61,46,47,62,63};
  if (((r0 & 0x20) == 0) && ((r0 & 0x80) == 0)) { 
    int32 action = (r0 & 0x07), colour = 0, tint = 0;
    if (colourdepth >= 256) {
      tint = (r1 & 3) << TINTSHIFT;
      colour = c64shuff[((r1 & 0xFC) >> 2)];
    } else {
      colour = (r1 & 0x7F);
    }
    if (r0 & 0x10) { /* background bit */
      colour |= 0x80;
    }
    if (r0 & 0x40) { /* Text colour bit */
      emulate_colourtint(colour, tint);
    } else {
      emulate_gcol(action, colour, tint);
    }
  }
}

void sdl_screensave(char *fname) {
  /* Strip quote marks, where appropriate */
  if ((fname[0] == '"') && (fname[strlen(fname)-1] == '"')) {
    fname[strlen(fname)-1] = '\0';
    fname++;
  }

  if (SDL_SaveBMP((screenmode == 7) ? screen2 : matrixflags.surface, fname)) {
    error(ERR_CANTWRITE);
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
    SDL_BlitSurface(placeholder, NULL, screenbank[ds.writebank], NULL);
    if (ds.displaybank == ds.writebank) {
      SDL_BlitSurface(placeholder, NULL, matrixflags.surface, NULL);
    }
    SDL_FreeSurface(placeholder);
  }
}

void swi_swap16palette() {
#ifndef BRANDY_MODE7ONLY
  Uint8 place;
  int ptr;
  if (colourdepth != 16) return;
  for (ptr=0; ptr < 24; ptr++) {
    place=palette[ptr];
    palette[ptr]=palette[ptr+24];
    palette[ptr+24]=place;
  }
  set_rgb();
#endif
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
    case 2:        return 32;
    case 4:        return 16;
    case 16:       return 8;
    case 256:      return 4;
    case COL15BIT: return 2;
    default:       return 1;
  }
}
static int32 log2bpp(int32 scrmode) {
  switch (modetable[scrmode].coldepth) {
    case 2:        return 0;
    case 4:        return 1;
    case 16:       return 2;
    case 256:      return 3;
    case COL15BIT: return 4;
    default:       return 5;
  }
}
/* Using values returned by RISC OS 3.7 */
size_t readmodevariable(int32 scrmode, int32 var) {
  int tmp=0;
  if (scrmode == -1) scrmode = screenmode;
  switch (var) {
    case 0:     return (getmodeflags(scrmode));
    case 1:     return (modetable[scrmode].xtext-1);
    case 2:     return (modetable[scrmode].ytext-1);
    case 3:
      tmp=modetable[scrmode].coldepth;
#ifndef BRANDY_MODE7ONLY
      if (tmp==256) tmp=64;
      if (tmp==COL15BIT) tmp=65536;
      if (tmp==COL24BIT) tmp=0;
#endif
      return tmp-1;
    case 4:     return (modetable[scrmode].xscale);
    case 5:     return (modetable[scrmode].yscale);
    case 6:     return (modetable[scrmode].xres * 4 / mode_divider(scrmode));
    case 7:     return (modetable[scrmode].xres * modetable[scrmode].yres * 4 / mode_divider(scrmode));
    case 9:     /* Fall through to 10 */
    case 10:    return (log2bpp(scrmode));
    case 11:    return (modetable[scrmode].xres-1);
    case 12:    return (modetable[scrmode].yres-1);
    case 128: /* GWLCol */      return ds.gwinleft / ds.xgupp;
    case 129: /* GWBRow */      return ds.gwinbottom / ds.ygupp;
    case 130: /* GWRCol */      return ds.gwinright / ds.xgupp;
    case 131: /* GWTRow */      return ds.gwintop / ds.ygupp;
    case 132: /* TWLCol */      return twinleft;
    case 133: /* TWBRow */      return twinbottom;
    case 134: /* TWRCol */      return twinright;
    case 135: /* TWTRow */      return twintop;
#ifndef BRANDY_MODE7ONLY
    case 136: /* OrgX */        return ds.xorigin;
    case 137: /* OrgY */        return ds.yorigin;
    case 138: /* GCsX */  return (ds.xlast-ds.xorigin);
    case 139: /* GCsY */  return (ds.ylast-ds.yorigin);
    case 140: /* OlderCsX */    return ds.xlast3/(2*ds.xscale);
    case 141: /* OlderCsY */    return ds.ylast3/(2*ds.yscale);
    case 142: /* OldCsX */      return ds.xlast2/(2*ds.xscale);
    case 143: /* OldCsY */      return ds.ylast2/(2*ds.yscale);
    case 144: /* GCsIX */       return ds.xlast/(2*ds.xscale);
    case 145: /* GCsIY */       return ds.ylast/(2*ds.yscale);
    case 146: /* NewPtX */      return ds.xlast/(2*ds.xscale);
    case 147: /* NewPtY */      return ds.ylast/(2*ds.yscale);
    case 148: /* ScreenStart */
    case 149: /* DisplayStart */ return (size_t)matrixflags.modescreen_ptr;
    case 150: /* TotalScreenSize */ return matrixflags.modescreen_sz;
    case 151: /* GPLFMD */      return ds.graph_fore_action;
    case 152: /* GPLBMD */      return ds.graph_back_action;
    case 153: /* GFCOL */       return ds.graph_forelog;
    case 154: /* GBCOL */       return ds.graph_backlog;
    case 155: /* TForeCol */    return text_forecol;
    case 156: /* TBackCol */    return text_backcol;
    case 157: /* GFTint */      return ds.graph_foretint << 6;
    case 158: /* GBTint */      return ds.graph_backtint << 6;
    case 159: /* TFTint */      return text_foretint << 6;
    case 160: /* TBTint */      return text_backtint << 6;
#endif
    case 161: /* MaxMode */     return HIGHMODE;
    default:    return 0;
  }
}

/* Refreshes the display approximately every 15ms. Also implements MODE7 flash */
int videoupdatethread(void) {
  int64 mytime = 0;
  
  while(1) {
    if (tmsg.titlepointer) {
      SDL_WM_SetCaption(tmsg.titlepointer, tmsg.titlepointer);
      tmsg.titlepointer = NULL;
    }
    if (tmsg.mousecmd) {
      switch (tmsg.mousecmd) {
        case 1: /* Toggle mouse on and off */
          if (tmsg.x) SDL_ShowCursor(SDL_ENABLE);
          else SDL_ShowCursor(SDL_DISABLE);
          break;
        case 2:
          SDL_WarpMouse(tmsg.x/2,ds.vscrheight-(tmsg.y/2));
          break;
      }
      tmsg.mousecmd = 0;
    }
    if (tmsg.modechange >= 0) {
      if (tmsg.modechange & 0x400) {
        fullscreenmode(tmsg.modechange & 3);
      } else {
        setup_mode(tmsg.modechange);
      }
    }
    if (tmsg.bailout != -1) {
      exit_interpreter_real(tmsg.bailout);
    } else {
      mytime = basicvars.centiseconds;
      SDL_PumpEvents(); /* This is for the keyboard stuff */
      if (matrixflags.noupdate == 0 && matrixflags.videothreadbusy == 0 && ds.autorefresh == 1 && matrixflags.surface) {
        matrixflags.videothreadbusy = 1;
        if (screenmode == 7) {
          if (tmsg.mode7forcerefresh || memcmp(mode7cloneframe, mode7frame, 1000)) {
            memcpy(mode7cloneframe, mode7frame, 1000);
            tmsg.mode7forcerefresh = 0;
            mode7renderscreen();
            SDL_BlitSurface(vduflag(MODE7_BANK) ? screen3 :  screen2, NULL, matrixflags.surface, NULL);
          }
          if ((mode7timer - mytime) <= 0) {
            hide_cursor();
            if (vduflag(MODE7_BANK)) {
              SDL_BlitSurface(screen2, NULL, matrixflags.surface, NULL);
              write_vduflag(MODE7_BANK,0);
              mode7timer=mytime + 96;
            } else {
              SDL_BlitSurface(screen3, NULL, matrixflags.surface, NULL);
              write_vduflag(MODE7_BANK,1);
              mode7timer=mytime + 32;
            }
            reveal_cursor();
          }
        }
#ifndef BRANDY_MODE7ONLY
        if ((screenmode != 7) && ((mytime % 32) == 0) && (cursorstate == SUSPENDED)) blit_scaled_actual(0,0,ds.screenwidth-1,ds.screenheight-1);
#endif
        if (tmsg.crtc6845r10 & 64) {
          int cadence = (tmsg.crtc6845r10 & 32) ? 64 : 32;
          if (mytime % cadence < (cadence >>1)) {
            reveal_cursor();
          } else {
            hide_cursor();
          }
        }
        SDL_Flip(matrixflags.surface);
        matrixflags.videothreadbusy = 0;
      }
    }
    tmsg.videothread = 0;
    usleep(15000);
  }
  return 0;
}
