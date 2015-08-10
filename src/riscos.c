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
**	This file contains the VDU driver emulation for the interpreter.
**	The RISC OS version of the program also calls functions in here
**	but they are just wrappers for calling the real VDU driver. The
**	actions of the VDU driver are emulated under other operating
**	systems
**
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

#include "kernel.h"
#include "swis.h"

/* OS_Word and OS_Byte calls used */

#define CONTROL_MOUSE 21	/* OS_Word call number to control the mouse pointer */

#define WAIT_VSYNC 19 		/* OS_Byte call number to wait for vertical sync */
#define SELECT_MOUSE 106	/* OS_Byte call number to select a mouse pointer */
#define READ_TEXTCURSOR 134	/* OS_Byte call number to read the text cursor position */
#define READ_CHARCURSOR 135	/* OS_Byte call number to read character at cursor and screen mode */

static boolean riscos31;		/* TRUE if running under RISC OS 3.1 */

/* RISC OS 3.5 and later mode descriptor */

typedef struct {
  int flags;		/* Mode descriptor flags */
  int xres, yres;	/* X and Y resolutions in pixels */
  int pixdepth;		/* Pixel depth */
  int rate;		/* Frame rate */
  struct {
    int index;		/* Mode variable index */
    int value;		/* Mode variable value */
  } vars [10];		/* Mode variables */
} mode_desc;

/*
** 'echo_on' turns on the immediate echo of characters to the screen
*/
void echo_on(void) {
  /* Ignored under RISC OS as it has a full VDU driver */
}

/*
** 'echo_off' turns off the immediate echo of characters to the screen.
*/
void echo_off(void) {
  /* Ignored under RISC OS as it has a full VDU driver */
}

/*
** 'emulate_vdu' calls the RISC OS VDU driver
*/
void emulate_vdu(int32 charvalue) {
  _kernel_oswrch(charvalue);
}

/*
** 'emulate_vdustr' is called to write a character string to the screen
*/
void emulate_vdustr(char *string, int32 length) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  if (length == 0) length = strlen(string);
  regs.r[0] = TOINT(string);
  regs.r[1] = length;
  oserror = _kernel_swi(OS_WriteN, &regs, &regs);
  if (oserror != NIL) error(ERR_CMDFAIL, oserror->errmess);
}

/*
** 'emulate_printf' provides a more flexible way of displaying formatted
** output than calls to 'emulate_vdustr'. It is used in the same way as
** 'printf' and can take any number of parameters.
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
** returns the value of the specified VDU variable
*/
int32 emulate_vdufn(int variable) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  int vdublock[2];
  vdublock[0] = variable;
  vdublock[1] = -1; 
  regs.r[0] = regs.r[1] = (int) vdublock;
  oserror = _kernel_swi(OS_ReadVduVariables, &regs, &regs);
  if (oserror != NIL) error(ERR_CMDFAIL, oserror->errmess);
  return vdublock[0];
}

/*
** 'emulate_pos' returns the number of the column in which the text cursor
** is located.  Under RISC OS, OS_Byte 134 places the information in the
** low-order byte of its return value.
*/
int32 emulate_pos(void) {
  int32 position;
  position = _kernel_osbyte(READ_TEXTCURSOR, 0, 0);
  return position & BYTEMASK;
}

/*
** 'wherey' returns the number of the row row in which the text cursor
** is located. OS_Byte 134 is used to return this information. It is
** in the second low order byte of the value returned by the call.
*/
int32 emulate_vpos(void) {
  int32 position;
  position = _kernel_osbyte(READ_TEXTCURSOR, 0, 0);
  return (position >> BYTESHIFT) & BYTEMASK;
}

/*
** 'emulate_mode' deals with the Basic 'MODE' command when the
** parameter is a number.
*/
void emulate_mode(int32 mode) {
  _kernel_oswrch(VDU_SCRMODE);
  _kernel_oswrch(mode);
}

/*
** make_grey_palette - Create a grey scale palette with
** 'levels' intensity levels
*/
static void make_grey_palette(int levels) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  int palette[256], intensity, step, col;
  step = 255 / (levels - 1);
  col = 0;
  for (intensity = 0; intensity < 256; intensity += step) {
    palette[col] = (intensity << 8) + (intensity << 16) + (intensity << 24);
    col++;
  }
  regs.r[0] = -1;
  regs.r[1] = -1;
  regs.r[2] = (int) &palette[0];
  regs.r[3] = 0;
  regs.r[4] = 0;
  oserror = _kernel_swi(ColourTrans_WritePalette, &regs, &regs);
  if (oserror != NIL) error(ERR_CMDFAIL, oserror->errmess);
}

/*
** set_mode31 - Set screen mode using resolution figures when
** running under RISC OS 3.1
*/
static void set_mode31(int32 xres, int32 yres, int32 bpp) {
  int n, coldepth;
  switch (bpp) {	/* Find number of colours */
  case 1: coldepth = 2; break;
  case 2: coldepth = 4; break;
  case 4: coldepth = 16; break;
  default:
    coldepth = 256;
  }
/* See if there is a suitable mode */
  for (n = 0; n <= HIGHMODE; n++) {
    if (modetable[n].xres == xres && modetable[n].yres == yres && modetable[n].coldepth == coldepth) break;
  }
  if (n>HIGHMODE) error(ERR_BADMODE);
  emulate_mode(n);
}

/*
** set_modedesc - Set the screen mode according to the figures
** given. This is used for versions of RISC OS that use mode
** descriptors, that is, RISC OS 3.5 and later
**/
static void set_modedesc(int32 xres, int32 yres, int32 bpp, int32 rate) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  mode_desc mode;

/* Set up the mode descriptor */

  mode.flags = 1;
  mode.xres = xres;
  mode.yres = yres;
  switch (bpp) {
  case 1: mode.pixdepth = 0; break;
  case 2: mode.pixdepth = 1; break;
  case 4: mode.pixdepth = 2; break;
  case 6: case 8: mode.pixdepth = 3; break;
  case 15: case 16: mode.pixdepth = 4; break;
  case 24: case 32: mode.pixdepth = 5; break;
  default:
    error(ERR_BADMODE);		/* Bad number of bits per pixel */
  }
  mode.rate = rate;
/*
** bpp == 6 means we want an Archimedes-style 256 colour mode
** with a limited ability to change the palette.
** bpp == 8 means we want a RISC OS 3.5 plus 256 colour mode
** where the full 256 colour palette can be changed
** Setting up the latter type of screen mode is not as simple
** as it could be as it requires extra information to be
** passed in the mode descriptor to tell RISC OS that a full
** 256 colour palette is wanted (the mode variables ModeFlags
** and NColour have to be set to 128 and 255 respectively).
*/
  if (bpp == 6)
    mode.vars[0].index = -1;		/* No mode variables needed */
  else if (bpp == 8) {
    mode.vars[0].index = 0;		/* ModeFlags value */
    mode.vars[0].value = 128;
    mode.vars[1].index = 3;		/* NColour */
    mode.vars[1].value = 255;
    mode.vars[2].index = -1;
  }
  regs.r[0] = 0;	/* Use OS_ScreenMode 0 - set screen mode */
  regs.r[1] = (int) &mode;
  oserror = _kernel_swi(OS_ScreenMode, &regs, &regs);
  if (oserror != NIL) error(ERR_CMDFAIL, oserror->errmess);
}

/*
 * emulate_newmode - Change the screen mode using OS_ScreenMode.
 * This is for the new form of the MODE statement
 */
void emulate_newmode(int32 xres, int32 yres, int32 bpp, int32 rate) {
  if (riscos31)
    set_mode31(xres, yres, bpp);
  else {
    set_modedesc(xres, yres, bpp, rate);
  }
}

/*
** 'emulate_modestr' deals with the Basic 'MODE' command when the
** parameter is a string. The function is passed the various 
** parameters required for a RISC OS mode selector.
** Colour or grey scale: colours == 0 if grey scale wanted
*/
void emulate_modestr(int32 xres, int32 yres, int32 colours, int32 greys, int32 xeig, int32 yeig, int32 rate) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  boolean greyscale;
  mode_desc mode;

  if (greys > 256) error(ERR_BADMODE);		/* Cannot have more than 256 level greyscale */
  greyscale = colours == 0;
  if (greyscale) colours = greys;

/* Set up the mode descriptor */

  mode.flags = 1;
  mode.xres = xres;
  mode.yres = yres;
  if (colours == 2)
    mode.pixdepth = 0;
  else if (colours == 4)
    mode.pixdepth = 1;
  else if (colours == 16)
    mode.pixdepth = 2;
  else if (colours == 256)
    mode.pixdepth = 3;
  else if (colours == 32*1024 || colours == 64*1024)
    mode.pixdepth = 4;
  else if (colours == 16*1024*1024)
    mode.pixdepth = 5;
  else {
    error(ERR_BADMODESC);		/* Bad number of bits per pixel */
  }
  mode.rate = rate;

/* Set up the mode variables needed */

  mode.vars[0].index = 4;		/* XEigFactor */
  mode.vars[0].value = xeig;
  mode.vars[1].index = 5;		/* YEigFactor */
  mode.vars[1].value = yeig;
  mode.vars[2].index = -1;

/* Need to have full access to 256 colour palette for 256 level grey scale */
  
  if (colours == 256 && greyscale) {
    mode.vars[2].index = 0;		/* ModeFlags value */
    mode.vars[2].value = 128;
    mode.vars[3].index = 3;		/* NColour */
    mode.vars[3].value = 255;
    mode.vars[4].index = -1;
  }
  regs.r[0] = 0;	/* Use OS_ScreenMode 0 - set screen mode */
  regs.r[1] = (int) &mode;
  oserror = _kernel_swi(OS_ScreenMode, &regs, &regs);
  if (oserror != NIL) error(ERR_CMDFAIL, oserror->errmess);

/* If grey scale, set up grey scale palette */

  if (greyscale) make_grey_palette(colours);
}

/*
** 'emulate_modefn' emulates the Basic function 'MODE'. This
** returns either the current mode number or a pointer to the
** mode descriptor block
*/
int32 emulate_modefn(void) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  if (riscos31) return (_kernel_osbyte(READ_CHARCURSOR, 0, 0) >> BYTESHIFT) & BYTEMASK;

/* RISC OS 3.5 or later */

  regs.r[0] = 1;	/* Return mode specifier for the current screen mode */
  oserror = _kernel_swi(OS_ScreenMode, &regs, &regs);
  if (oserror != NIL) error(ERR_CMDFAIL, oserror->errmess);
  return regs.r[1];	/* Mode specifier is returned in R1 */
}

/*
** 'emulate_plot' emulates the Basic statement 'PLOT'. It also represents
** the heart of the graphics emulation functions as most of the other
** graphics functions are just pre-packaged calls to this one
*/
void emulate_plot(int32 code, int32 x, int32 y) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = code;
  regs.r[1] = x;
  regs.r[2] = y;
  oserror = _kernel_swi(OS_Plot, &regs, &regs);
  if (oserror!=NIL) error(ERR_CMDFAIL, oserror->errmess);
}

/*
** 'emulate_pointfn' emulates the Basic function 'POINT', returning
** the colour number of the point (x,y) on the screen
*/
int32 emulate_pointfn(int32 x, int32 y) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = x;
  regs.r[1] = y;
  oserror = _kernel_swi(OS_ReadPoint, &regs, &regs);
  if (oserror != NIL) error(ERR_CMDFAIL, oserror->errmess);
  return regs.r[2];	    /* OS_ReadPoint returns the colour number in R2 */
}

/*
** 'emulate_tintfn' deals with the Basic keyword 'TINT' when used as
** a function
*/
int32 emulate_tintfn(int32 x, int32 y) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = x;
  regs.r[1] = y;
  oserror = _kernel_swi(OS_ReadPoint, &regs, &regs);
  if (oserror != NIL) error(ERR_CMDFAIL, oserror->errmess);
  return regs.r[3];	    /* OS_ReadPoint returns the tint number in R3 */
}

/*
** 'emulate_pointto' deals with the Basic 'POINT TO' statement which
** sets the position of the 'position pointer'. I am not 100% convinced
** that this is the right OS_Word call to use
*/
void emulate_pointto(int32 x, int32 y) {
  struct {byte call_number, x_lsb, x_msb, y_lsb, y_msb;} osword_parms;
  osword_parms.call_number = 5;		/* OS_Word 21 call 5 sets the pointer position */
  osword_parms.x_lsb = x & BYTEMASK;
  osword_parms.x_msb = x >> BYTESHIFT;
  osword_parms.y_lsb = y & BYTEMASK;
  osword_parms.y_msb = y >> BYTESHIFT;
  (void) _kernel_osword(CONTROL_MOUSE, TOINTADDR(&osword_parms));
}

/*
** 'emulate_wait' deals with the Basic 'WAIT' statement
*/
void emulate_wait(void) {
  (void) _kernel_osbyte(WAIT_VSYNC, 0, 0);	/* Use OS_Byte 19 (wait for vertical sync) */
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
  for (n= 1 ; n <= 7; n++) emulate_vdu(0);
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
  for (n = 1; n <= 7; n++) emulate_vdu(0);
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
  if (tint <= MAXTINT) tint = tint << TINTSHIFT;	/* Assume value is in the wrong place */
  emulate_vdu(tint);
  for (n = 1; n <= 7; n++) emulate_vdu(0);
}

/*
** 'emulate_gcol' deals with the simple forms of the Basic 'GCOL'
** statement where is it used to either set the graphics colour or
** to define how the VDU drivers carry out graphics operations.
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
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = (blue << 24) + (green << 16) + (red << 8);
  regs.r[4] = action;
  regs.r[3] = 0;
  if (background) regs.r[3] = 128;
  oserror = _kernel_swi(ColourTrans_SetGCOL, &regs, &regs);
  if (oserror != NIL) error(ERR_CMDFAIL, oserror->errmess);
}

/*
** emulate_gcolnum - Called to set the graphics foreground or
** background colour to the colour number 'colnum'. This code
** assumes that the colour number here is the same as the GCOL
** number, which it probably is not. This needs to be checked
*/
void emulate_gcolnum(int32 action, int32 background, int32 colnum) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = colnum;
  regs.r[3] = 0;
  regs.r[4] = action;
  if (background) regs.r[3] = 128;	/* Set background colour */
  oserror = _kernel_swi(ColourTrans_SetColour, &regs, &regs);
  if (oserror != NIL) error(ERR_CMDFAIL, oserror->errmess);
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
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = (blue << 24) + (green << 16) + (red << 8);
  regs.r[3] = 0;
  if (background) regs.r[3] = 128;	/* Set background colour */
  oserror = _kernel_swi(ColourTrans_SetTextColour, &regs, &regs);
  if (oserror != NIL) error(ERR_CMDFAIL, oserror->errmess);
}

/*
** emulate_setcolnum - Called to set the text forground or
** background colour to the colour number 'colnum'. Problem: this
** function is passed a colour number. There is a SWI to convert
** a colour number to a GCOL number (ColourNumberToGCOL) but the
** docs say it only works in 256 colour modes. This code assumes
** that the colour number here is the same as the GCOL number but
** this is probably incorrect
*/
void emulate_setcolnum(int32 background, int32 colnum) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = colnum;
  regs.r[3] = 512;		/* Set text colour */
  regs.r[4] = 0;
  if (background) regs.r[3] = 128;	/* Set background colour */
  oserror = _kernel_swi(ColourTrans_SetColour, &regs, &regs);
  if (oserror != NIL) error(ERR_CMDFAIL, oserror->errmess);
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
 * emulate_colourfn - This performs the function COLOUR(). It
 * Returns the entry in the palette for the current screen mode
 * that most closely matches the colour with red, green and
 * blue components passed to it.
 */
int32 emulate_colourfn(int32 red, int32 green, int32 blue) {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  regs.r[0] = (blue << 24) + (green << 16) + (red << 8);
  oserror = _kernel_swi(ColourTrans_ReturnGCOL, &regs, &regs);
  if (oserror != NIL) error(ERR_CMDFAIL, oserror->errmess);
  return regs.r[0];
}

/*
** 'emulate_move' moves the graphics cursor to the absolute
** position (x,y) on the screen
*/
void emulate_move(int32 x, int32 y) {
  emulate_plot(DRAW_SOLIDLINE + MOVE_ABSOLUTE, x, y);
}

/*
** 'emulate_moveby' move the graphics cursor by the offsets 'x'
** and 'y' relative to its current position
*/
void emulate_moveby(int32 x, int32 y) {
  emulate_plot(DRAW_SOLIDLINE + MOVE_RELATIVE, x, y);
}

/*
** 'emulate_draw' draws a solid line from the current graphics
** cursor position to the absolute position (x,y) on the screen
*/
void emulate_draw(int32 x, int32 y) {
  emulate_plot(DRAW_SOLIDLINE + DRAW_ABSOLUTE, x, y);
}

/*
** 'emulate_drawby' draws a solid line from the current graphics
** cursor position to that at offsets 'x' and 'y' relative to that
** position
*/
void emulate_drawby(int32 x, int32 y) {
  emulate_plot(DRAW_SOLIDLINE + DRAW_RELATIVE, x, y);
}

/*
** 'emulate_line' draws a line from the absolute position (x1,y1)
** on the screen to (x2,y2)
*/
void emulate_line(int32 x1, int32 y1, int32 x2, int32 y2) {
  emulate_plot(DRAW_SOLIDLINE + MOVE_ABSOLUTE, x1, y1);
  emulate_plot(DRAW_SOLIDLINE + DRAW_ABSOLUTE, x2, y2);
}

/*
** 'emulate_point' plots a single point at the absolute position
** (x,y) on the screen
*/
void emulate_point(int32 x, int32 y) {
  emulate_plot(PLOT_POINT + DRAW_ABSOLUTE, x, y);
}

/*
** 'emulate_pointby' plots a single point at the offsets 'x' and
** 'y' from the current graphics position
*/
void emulate_pointby(int32 x, int32 y) {
  emulate_plot(PLOT_POINT + DRAW_RELATIVE, x, y);
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
  emulate_plot(DRAW_SOLIDLINE + MOVE_ABSOLUTE, x, y);	   /* Move to centre of ellipse */
  emulate_plot(DRAW_SOLIDLINE + MOVE_ABSOLUTE, x + majorlen, y);	/* Find a point on the circumference */
  if (isfilled)
    emulate_plot(FILL_ELLIPSE + DRAW_ABSOLUTE, x, y + minorlen);
  else {
    emulate_plot(PLOT_ELLIPSE + DRAW_ABSOLUTE, x, y + minorlen);
  }

}

void emulate_circle(int32 x, int32 y, int32 radius, boolean isfilled) {
  emulate_plot(DRAW_SOLIDLINE + MOVE_ABSOLUTE, x, y);	   /* Move to centre of circle */
  if (isfilled)
    emulate_plot(FILL_CIRCLE + DRAW_ABSOLUTE, x - radius, y);	/* Plot to a point on the circumference */
  else {
    emulate_plot(PLOT_CIRCLE + DRAW_ABSOLUTE, x - radius, y);
  }
}

/*
** 'emulate_drawrect' draws either an outline of a rectangle or a
** filled rectangle
*/
void emulate_drawrect(int32 x1, int32 y1, int32 width, int32 height, boolean isfilled) {
  emulate_plot(DRAW_SOLIDLINE + MOVE_ABSOLUTE, x1, y1);
  if (isfilled)
    emulate_plot(FILL_RECTANGLE + DRAW_RELATIVE, width, height);
  else {
    emulate_plot(DRAW_SOLIDLINE + DRAW_RELATIVE, width, 0);
    emulate_plot(DRAW_SOLIDLINE + DRAW_RELATIVE, 0, height);
    emulate_plot(DRAW_SOLIDLINE + DRAW_RELATIVE, -width, 0);
    emulate_plot(DRAW_SOLIDLINE + DRAW_RELATIVE, 0, -height);
  }
}

/*
** 'emulate_moverect' is called to either copy an area of the graphics screen
** from one place to another or to move it, clearing its old location to the
** current background colour
*/
void emulate_moverect(int32 x1, int32 y1, int32 width, int32 height, int32 x2, int32 y2, boolean ismove) {
  emulate_plot(DRAW_SOLIDLINE + MOVE_ABSOLUTE, x1, y1);
  emulate_plot(DRAW_SOLIDLINE + MOVE_RELATIVE, width, height);
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
  emulate_plot(FLOOD_BACKGROUND + DRAW_ABSOLUTE, x, y);
}

/*
** 'emulate_fillby' flood-fills an area of the graphics screen in
** the current foreground colour starting at the position at offsets
** 'x' and 'y' relative to the current graphics cursor position
*/
void emulate_fillby(int32 x, int32 y) {
  emulate_plot(FLOOD_BACKGROUND + DRAW_RELATIVE, x, y);
}

/*
** 'emulate_origin' emulates the Basic statement 'ORIGIN' which
** sets the absolute location of the origin on the graphics screen
*/
void emulate_origin(int32 x, int32 y) {
  emulate_vdu(VDU_ORIGIN);
  emulate_vdu(x & BYTEMASK);
  emulate_vdu((x >> BYTESHIFT) & BYTEMASK);
  emulate_vdu(y & BYTEMASK);
  emulate_vdu((y >> BYTESHIFT) & BYTEMASK);
}

/*
** 'init_screen' carries out any initialisation needed for the 
** screen output functions.
** The flag 'riscos31' is set if we are running under 
** RISC OS 3.1 and so cannot use SWIs such as OS_ScreenMode.
*/
boolean init_screen(void) {
  _kernel_swi_regs regs;
  regs.r[0] = 129;
  regs.r[1] = 0;
  regs.r[2] = 255;
  _kernel_swi(OS_Byte, &regs, &regs);
  riscos31 = regs.r[1] < 0xA5;		/* OS Version is returned in R1 */
  return TRUE;
}

void end_screen(void) {
}


