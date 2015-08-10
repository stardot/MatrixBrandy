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
**      This file contains the VDU driver emulation for the interpreter.
**      This version of the code is very basic and does nothing apart from
**      output text. It does not support colour, positioning the cursor nor
**      even clearing the screen. All output is via the C function
**      putchar().
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include "common.h"
#include "target.h"
#include "errors.h"
#include "basicdefs.h"
#include "scrcommon.h"
#include "screen.h"

/*
** Notes
** -----
**  This is one of the four versions of the VDU driver emulation.
**  It is used by versions of the interpreter where only text
**  output is possible and restricted to the standard C functions
**  such as putchar() and printf().
**
**  The four versions of the VDU driver code are in:
**      riscos.c
**      textgraph.c
**      textonly.c
**      simpletext.c
**
**  The most important function is 'emulate_vdu'. All text output
**  and any VDU commands go via this function. It corresponds to the
**  SWI OS_WriteC.
*/

/*
** 'find_cursor' ensures that the position of the text cursor
** is known and valid as far as the interpreter is concerned  
*/
void find_cursor(void) {
}

/*
** 'set_cursor' sets the type of the text cursor
*/
void set_cursor(boolean underline) {
}

/*
** 'echo_on' turns on the immediate echo of characters to the screen
*/
void echo_on(void) {
  echo = TRUE;
  fflush(stdout);
}

/*
** 'echo_off' turns off the immediate echo of characters to the screen.
*/
void echo_off(void) {
  echo = FALSE;
}

/*
** 'nogo' is called to handle unsupported VDU driver features
** that are seen as cosmetic, that is, they affect the look
** of the program's output but not the running of the program.
** If the flag 'flag_cosmetic' is set then an error is flagged;
** the feature is otherwise silently ignored
*/
static nogo(void) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
}
/*
** 'emulate_vdu' is a simple emulation of the RISC OS VDU driver. It
** accepts characters as per the RISC OS driver and uses them to imitate
** some of the VDU commands. Most of these are ignored
*/
void emulate_vdu(int32 charvalue) {
  charvalue = charvalue & BYTEMASK;     /* Deal with any signed char type problems */
  if (vduneeded==0) {                   /* VDU queue is empty */
    if (charvalue>=' ') {               /* Most common case - print something */
      if (charvalue==DEL) charvalue = ' ';
      putchar(charvalue);
      if (echo) fflush(stdout);
      return;
    }
    else {      /* Control character - Found start of new VDU command */
      if (!echo) fflush(stdout);
      vducmd = charvalue;
      vduneeded = vdubytes[charvalue];
      vdunext = 0;
    }
  }
  else {        /* Add character to VDU queue for current command */
    vduqueue[vdunext] = charvalue;
    vdunext++;
  }
  if (vdunext<vduneeded) return;
  vduneeded = 0;

/* There are now enough entries in the queue for the current command */

  switch (vducmd) {     /* Emulate the various control codes */
  case VDU_NULL:        /* 0 - Do nothing */
  case VDU_PRINT:       /* 1 - Send next character to the print stream */
  case VDU_ENAPRINT:    /* 2 - Enable the sending of characters to the printer (ignored) */
  case VDU_DISPRINT:    /* 3 - Disable the sending of characters to the printer (ignored) */
  case VDU_TEXTCURS:    /* 4 - Print text at text cursor (ignored) */
  case VDU_ENABLE:      /* 6 - Enable the VDU driver (ignored) */
  case VDU_ENAPAGE:     /* 14 - Enable page mode (ignored) */
  case VDU_DISPAGE:     /* 15 - Disable page mode (ignored) */
  case VDU_DISABLE:     /* 21 - Disable the VDU driver (ignored) */
    break;
  case VDU_GRAPHICURS:  /* 5 - Print text at graphics cursor */
  case VDU_CLEARGRAPH:  /* 16 - Clear graphics window */
  case VDU_GRAPHCOL:    /* 18 - Change current graphics colour */
  case VDU_DEFGRAPH:    /* 24 - Define graphics window */
  case VDU_PLOT:        /* 25 - Issue graphics command */
  case VDU_ORIGIN:      /* 29 - Define graphics origin */
    error(ERR_NOGRAPHICS);
    break;
  case VDU_CURFORWARD:  /* 9 - Move cursor right one character */
  case VDU_CURUP:       /* 11 - Move cursor up one line */
  case VDU_CLEARTEXT:   /* 12 - Clear text window (formfeed) */
  case VDU_TEXTCOL:     /* 17 - Change current text colour */
  case VDU_LOGCOL:      /* 19 - Map logical colour to physical colour */
  case VDU_RESTCOL:     /* 20 - Restore logical colours to default values */
  case VDU_SCRMODE:     /* 22 - Change screen mode */
  case VDU_COMMAND:     /* 23 - Assorted VDU commands */
  case VDU_RESTWIND:    /* 26 - Restore default windows */
  case VDU_DEFTEXT:     /* 28 - Define text window */
  case VDU_HOMETEXT:    /* 30 - Send cursor to top left-hand corner of screen */
  case VDU_MOVETEXT:    /* 31 - Send cursor to column x, row y on screen */
    nogo();
    break;
  case VDU_BEEP:        /* 7 - Sound the bell */
  case VDU_CURBACK:     /* 8 - Move cursor left one character */
  case VDU_CURDOWN:     /* 10 - Move cursor down one line (linefeed) */
  case VDU_RETURN:      /* 13 - Carriage return */
  case VDU_ESCAPE:      /* 27 - Do nothing (but char is sent to screen anyway) */
    putchar(vducmd);
    break;
  }
}

/*
** 'emulate_vdustr' is called to print a string via the 'VDU driver'
*/
void emulate_vdustr(char string[], int32 length) {
  int32 n;
  if (length==0) length = strlen(string);
  echo_off();
  for (n=0; n<length; n++) emulate_vdu(string[n]);      /* Send the string to the VDU driver */
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
  int n;
  va_start(parms, format);
  length = vsprintf(text, format, parms);
  va_end(parms);
  echo_off();
  for (n = 0; n < length; n++) emulate_vdu(text[n]);
  echo_on();
}

/*
** 'emulate_newline' skips to a new line on the screen.
*/
void emulate_newline(void) {
  emulate_vdu(CR);
  emulate_vdu(LF);
}

/*
** emulate_vdufn - Emulates the Basic VDU function. This
** returns the value of the specified VDU variable
*/
int32 emulate_vdufn(int variable) {
  return 0;
}

/*
 * emulate_colourfn - This performs the function COLOUR().
 */
int32 emulate_colourfn(int32 red, int32 green, int32 blue) {
  return 1;
}


/*
** 'emulate_pos' returns the number of the column in which the text cursor
** is located in the text window
*/
int32 emulate_pos(void) {
  nogo();
  return 0;
}

/*
** 'emulate_vpos' returns the number of the row in which the text cursor
** is located in the text window
*/
int32 emulate_vpos(void) {
  nogo();
  return 0;
}

/*
** 'emulate_mode' deals with the Basic 'MODE' command when the parameter
** is a number. This version does nothing
*/
void emulate_mode(int32 mode) {
  nogo();
}

/*
 * emulate_newmode - Change the screen mode using OS_ScreenMode.
 * This is for the new form of the MODE statement
 */
void emulate_newmode(int32 xres, int32 yres, int32 bpp, int32 rate) {
  nogo();
}

/*
** 'emulate_modestr' deals with the Basic 'MODE' command when the
** parameter is a string. This version does nothing
*/
void emulate_modestr(int32 xres, int32 yres, int32 colours, int32 greys, int32 xeig, int32 yeig, int32 rate) {
  nogo();
}

/*
** 'emulate_modefn' emulates the Basic function 'MODE'
*/
int32 emulate_modefn(void) {
  return screenmode;
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
  nogo();
}

/*
** 'emulate_tab' moves the text cursor to the position column 'x' row 'y'
** in the current text window
*/
void emulate_tab(int32 x, int32 y) {
  nogo();
}

/*
** 'emulate_off' deals with the Basic 'OFF' statement which turns
** off the text cursor
*/
void emulate_off(void) {
  nogo();
}

/*
** 'emulate_on' emulates the Basic 'ON' statement, which turns on
** the text cursor
*/
void emulate_on(void) {
  nogo();
}

/*
** 'exec_tint' is called to handle the Basic 'TINT' statement which
** sets the 'tint' value for the current text or graphics foreground
** or background colour to 'tint'.
*/
void emulate_tint(int32 action, int32 tint) {
  nogo();
}

/*
** Version of 'emulate_plot' used when interpreter does not
** include any graphics support
*/
void emulate_plot(int32 code, int32 x, int32 y) {
  error(ERR_NOGRAPHICS);
}

/*
** Version of 'emulate_pointfn' used when interpreter does not
** include any graphics support
*/
int32 emulate_pointfn(int32 x, int32 y) {
  error(ERR_NOGRAPHICS);
  return 0;
}

/*
** 'emulate_tintfn' deals with the Basic keyword 'TINT' when used as
** a function. This is the version used when the interpreter does not
** support graphics
*/
int32 emulate_tintfn(int32 x, int32 y) {
  error(ERR_NOGRAPHICS);
  return 0;
}

/*
** 'emulate_gcol' deals with both forms of the Basic 'GCOL' statement
*/
void emulate_gcol(int32 action, int32 colour, int32 tint) {
  error(ERR_NOGRAPHICS);
}

/*
** emulate_gcolrgb - Called to deal with the 'GCOL <red>,<green>,
** <blue>' version of the GCOL statement. 'background' is set
** to true if the graphics background colour is to be changed
** otherwise the foreground colour is altered
*/
void emulate_gcolrgb(int32 action, int32 background, int32 red, int32 green, int32 blue) {
  error(ERR_NOGRAPHICS);
}

/*
** emulate_gcolnum - Called to set the graphics foreground or
** background colour to the colour number 'colnum'. This code
** assumes that the colour number here is the same as the GCOL
** number, which it probably is not. This needs to be checked
*/
void emulate_gcolnum(int32 action, int32 background, int32 colnum) {
  error(ERR_NOGRAPHICS);
}

/*
** 'emulate_colourtint' deals with the Basic 'COLOUR <colour> TINT' statement
*/
void emulate_colourtint(int32 colour, int32 tint) {
  nogo();
}

/*
** 'emulate_mapcolour' handles the Basic 'COLOUR <colour>,<physical colour>'
** statement.
*/
void emulate_mapcolour(int32 colour, int32 physcolour) {
  nogo();
}

/*
** 'emulate_setcolour' handles the Basic 'COLOUR <red>,<green>,<blue>'
** statement
*/
void emulate_setcolour(int32 background, int32 red, int32 green, int32 blue) {
  nogo();
}

/*
** emulate_setcolnum - Called to set the text forground or
** background colour to the colour number 'colnum'. 
*/
void emulate_setcolnum(int32 background, int32 colnum) {
  nogo();
}

/*
** 'emulate_defcolour' handles the Basic 'COLOUR <colour>,<red>,<green>,<blue>'
** statement
*/
void emulate_defcolour(int32 colour, int32 red, int32 green, int32 blue) {
  nogo();
}

/*
** Following are the functions that emulate graphics statements.
** None of these are supported so they are just flagged as errors
*/

void emulate_move(int32 x, int32 y) {
  error(ERR_NOGRAPHICS);
}

void emulate_moveby(int32 x, int32 y) {
  error(ERR_NOGRAPHICS);
}

void emulate_draw(int32 x, int32 y) {
  error(ERR_NOGRAPHICS);
}

void emulate_drawby(int32 x, int32 y) {
  error(ERR_NOGRAPHICS);
}

void emulate_line(int32 x1, int32 y1, int32 x2, int32 y2) {
  error(ERR_NOGRAPHICS);
}

void emulate_point(int32 x, int32 y) {
  error(ERR_NOGRAPHICS);
}

void emulate_pointby(int32 x, int32 y) {
  error(ERR_NOGRAPHICS);
}

void emulate_ellipse(int32 x, int32 y, int32 majorlen, int32 minorlen, float64 angle, boolean isfilled) {
  error(ERR_NOGRAPHICS);
}

void emulate_circle(int32 x, int32 y, int32 radius, boolean isfilled) {
  error(ERR_NOGRAPHICS);
}

void emulate_drawrect(int32 x1, int32 y1, int32 width, int32 height, boolean isfilled) {
  error(ERR_NOGRAPHICS);
}

void emulate_moverect(int32 x1, int32 y1, int32 width, int32 height, int32 x2, int32 y2, boolean ismove) {
  error(ERR_NOGRAPHICS);
}

void emulate_fill(int32 x, int32 y) {
  error(ERR_NOGRAPHICS);
}

void emulate_fillby(int32 x, int32 y) {
  error(ERR_NOGRAPHICS);
}

void emulate_origin(int32 x, int32 y) {
  error(ERR_NOGRAPHICS);
}

/*
** 'init_screen' is called to initialise the RISC OS VDU driver
** emulation code for the versions of this program that do not run
** under RISC OS. It returns 'TRUE' if initialisation was okay or
** 'FALSE' if it failed (in which case it is not safe for the
** interpreter to run)
*/
boolean init_screen(void) {
  screenmode = USERMODE;
  vdunext = 0;
  vduneeded = 0;
  return TRUE;
}

/*
** 'end_screen' is called to tidy up the VDU emulation at the end
** of the run
*/
void end_screen(void) {
}

