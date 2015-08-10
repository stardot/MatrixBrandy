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
**	Functions that emulate the OS-specific screen output of the
**	interpreter live here
*/

#ifndef __screen_h
#define __screen_h

#include "common.h"

/* RISC OS 'plot' codes for graphics */

#define ABSCOORD_MASK	4	/* Mask to check relative/absolute coordinate bit */
#define PLOT_COLMASK	3	/* Mask to extract colour type to use */

#define PLOT_MOVEONLY	0	/* Move graphics cursor only */
#define PLOT_FOREGROUND	1	/* Use graphics foreground colour */
#define PLOT_INVERSE	2	/* Use logical inverse colour */
#define PLOT_BACKGROUND	3	/* Use graphics background colour */

#define MOVE_RELATIVE	0	/* Move cursor relative to last graphics position */
#define DRAW_RELATIVE	1	/* Draw line relative to last graphics position */
#define MOVE_ABSOLUTE	4	/* Move cursor to actual coordinate given */
#define DRAW_ABSOLUTE	5	/* Draw line to actual coordinate given */

#define DRAW_SOLIDLINE	0	/* Draw a solid line including both end points */
#define PLOT_POINT	0x40	/* Plot a single point */
#define FILL_TRIANGLE	0x50	/* Plot a filled triangle */
#define FILL_RECTANGLE	0x60	/* Plot a filled rectangle */
#define FILL_PARALLELOGRAM 0x70	/* Plot a filled parallelogram */
#define FLOOD_BACKGROUND 0x80	/* Flood fill as far as background colour */
#define PLOT_CIRCLE	0x90	/* Plot a circle outline */
#define FILL_CIRCLE	0x98	/* Plot a filled circle */
#define SHIFT_RECTANGLE	0xB8	/* Move or copy rectangle */
#define MOVE_RECTANGLE	0xBD	/* Move rectangle absolute */
#define COPY_RECTANGLE	0xBE	/* Copy rectangle absolute */
#define PLOT_ELLIPSE	0xC0	/* Plot an ellipse outline */
#define FILL_ELLIPSE	0xC8	/* Plot a filled ellipse */
#define GRAPHOP_MASK	0xF8	/* Mask to extract graphics operation */
#define GRAPHHOW_MASK	0x07	/* Mask to extract details of operation */

/* RISC OS plot action codes (set by VDU 18) */

#define OVERWRITE_POINT 0	/* Overwrite point on screen */
#define OR_POINT	1	/* OR with point */
#define AND_POINT	2	/* AND with point */
#define EOR_POINT	3	/* Exclusive OR with point */
#define INVERT_POINT	4	/* Invert colour on screen */
#define LEAVE_POINT	5	/* Leave point untouched */
#define ANDNOT_POINT	6	/* AND point with NOT graphics foreground colour */
#define ORNOT_POINT	7	/* OR point with NOT graphics foreground colour */

/* RISC OS 'VDU' control codes */

#define VDU_NULL	0	/* Do nothing */
#define VDU_PRINT	1	/* Send next character to printer */
#define VDU_ENAPRINT	2	/* Enable printer */
#define VDU_DISPRINT	3	/* Disable printer */
#define VDU_TEXTCURS	4	/* Write text at text cursor position */
#define VDU_GRAPHICURS	5	/* Write text at graphics position */
#define VDU_ENABLE	6	/* Enable VDU driver */
#define VDU_BEEP	7	/* Generate 'bell' sound */
#define VDU_CURBACK	8	/* Move cursor back one position */
#define VDU_CURFORWARD	9	/* Move cursor forwards one position */
#define VDU_CURDOWN	10	/* Move cursor down one line */
#define VDU_CURUP	11	/* Move cursor up one line */
#define VDU_CLEARTEXT	12	/* Clear text window */
#define VDU_RETURN	13	/* Move cursor to start of line */
#define VDU_ENAPAGE	14	/* Enable 'page' mode */
#define VDU_DISPAGE	15	/* Disable 'page' mode */
#define VDU_CLEARGRAPH	16	/* Clear graphics window */
#define VDU_TEXTCOL	17	/* Define text colour to use */
#define VDU_GRAPHCOL	18	/* Define graphics colour and plot action */
#define VDU_LOGCOL	19	/* Define logical colour */
#define VDU_RESTCOL	20	/* Restore logical colours to default values */
#define VDU_DISABLE	21	/* Disable VDU driver */
#define VDU_SCRMODE	22	/* Select screen mode */
#define VDU_COMMAND	23	/* Multitudenous VDU commands */
#define VDU_DEFGRAPH	24	/* Define graphics window */
#define VDU_PLOT	25	/* PLOT command */
#define VDU_RESTWIND	26	/* Restore default windows */
#define VDU_ESCAPE	27	/* Does nothing */
#define VDU_DEFTEXT	28	/* Define text window */
#define VDU_ORIGIN	29	/* Define graphics origin */
#define VDU_HOMETEXT	30	/* Send text cursor to home position */
#define VDU_MOVETEXT	31	/* Move text cursor */

/* RISC OS physical colour numbers (modes up to 16 colours) */

#define VDU_BLACK	0
#define VDU_RED		1
#define VDU_GREEN	2
#define VDU_YELLOW	3
#define VDU_BLUE	4
#define VDU_MAGENTA	5
#define VDU_CYAN	6
#define VDU_WHITE	7
#define FLASH_BLAWHITE	8
#define FLASH_REDCYAN	9
#define FLASH_GREENMAG	10
#define FLASH_YELBLUE	11
#define FLASH_BLUEYEL	12
#define FLASH_MAGREEN	13
#define FLASH_CYANRED	14
#define FLASH_WHITEBLA	15

extern void emulate_tab(int32, int32);
extern void echo_on(void);
extern void echo_off(void);
extern void set_cursor(boolean);
extern void emulate_vdu(int32);
extern void emulate_vdustr(char *, int32);
extern void emulate_printf(char *, ...);
extern void emulate_newline(void);
extern int32 emulate_vdufn(int32);
extern int32 emulate_pos(void);
extern int32 emulate_vpos(void);
extern void emulate_mode(int32);
extern void emulate_modestr(int32, int32, int32, int32, int32, int32, int32);
extern void emulate_newmode(int32, int32, int32, int32);
extern void emulate_off(void);
extern void emulate_on(void);
extern int32 emulate_modefn(void);
extern int32 emulate_colourfn(int32, int32, int32);
extern void emulate_colourtint(int32, int32);
extern void emulate_mapcolour(int32, int32);
extern void emulate_setcolour(int32, int32, int32, int32);
extern void emulate_setcolnum(int32, int32);
extern void emulate_defcolour(int32, int32, int32, int32);
extern void emulate_gcol(int32, int32, int32);
extern void emulate_gcolrgb(int32, int32, int32, int32, int32);
extern void emulate_gcolnum(int32, int32, int32);
extern void emulate_tint(int32, int32);
extern int32 emulate_tintfn(int32, int32);
extern void emulate_plot(int32, int32, int32);
extern int32 emulate_pointfn(int32, int32);
extern void emulate_move(int32, int32);
extern void emulate_moveby(int32, int32);
extern void emulate_draw(int32, int32);
extern void emulate_drawby(int32, int32);
extern void emulate_point(int32, int32);
extern void emulate_pointby(int32, int32);
extern void emulate_pointto(int32, int32);
extern void emulate_line(int32, int32, int32, int32);
extern void emulate_circle(int32, int32, int32, boolean);
extern void emulate_ellipse(int32, int32, int32, int32, float64, boolean);
extern void emulate_drawrect(int32, int32, int32, int32, boolean);
extern void emulate_moverect(int32, int32, int32, int32, int32, int32, boolean);
extern void emulate_fill(int32, int32);
extern void emulate_fillby(int32, int32);
extern void emulate_origin(int32, int32);
extern void emulate_wait(void);
extern void find_cursor(void);
extern boolean init_screen(void);
extern void end_screen(void);

#endif
