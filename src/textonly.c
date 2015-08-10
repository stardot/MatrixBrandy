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
**      This version of the code is used on targets which do not
**      support graphics
*/
/*
** Crispian Daniels August 20th 2002:
**      Included Mac OS X target in conditional compilation.
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
#include "keyboard.h"

#if defined(TARGET_WIN32) | defined(TARGET_DJGPP) | defined(TARGET_BCC32) | defined(TARGET_MACOSX) | defined(TARGET_MINGW)
#include "conio.h"
#else
#define USE_ANSI        /* Have to use ANSI control sequences, not conio */
#endif

#if defined(TARGET_MINGW)
#include <windows.h>
#endif

#if defined(TARGET_LINUX) | defined(TARGET_NETBSD) | defined(TARGET_DJGPP)\
 | defined(TARGET_FREEBSD) | defined(TARGET_OPENBSD) | defined(TARGET_GNUKFREEBSD)
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <termios.h>
#endif

/*
** Notes
** -----
**  This is one of the four versions of the VDU driver emulation.
**  It is used by versions of the interpreter where only text
**  output is possible. All graphics commands are flagged as errors
**  by this code.
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
**
**  The code supports two different methods for such things as
**  positioning the cursor. It can use either ANSI control
**  sequences or the functions in the 'conio' library. 'conio'
**  is used by the DOS version of the program and ANSI sequences
**  by the Linux and NetBSD ones. Conio is better in that it
**  supports a greater range of facilities and allows the program
**  to more closely emulate what the RISC OS VDU drivers can do.
**  On the other hand, some features such as the text window are
**  not implemented properly using ANSI control sequences. This
**  probably can be improved.
**  If 'USE_ANSI' is defined then ANSI control sequences are used;
**  otherwise the code uses conio.
**
**  The code takes special action if stdout is being redirected.
**  In this case the control sequences to carry out such functions
**  as changing the text output colour are either ignored or their
**  use flagged as an error. This allows output to be sent to a
**  file, for example, without the file being polluted with the
**  characters sent as escape sequences.
*/


/*
** Note: SCRHEIGHT is really used to flag to indicate that that height
** of the screen is not known. The screen height can be found when
** using the conio library under DOS or when running under NetBSD and
** Linux but this code allows for it to be left unspecified
*/
#define SCRWIDTH 80             /* Assumed width of normal text screen */
#define SCRHEIGHT 0             /* Pretend height of normal text screen */

#ifdef USE_ANSI

/*
** ANSI colour numbers. The ANSI colour table maps RISC OS physical to ANSI
** colour numbers in 2, 4 and 16 colour modes
*/
#define ANSI_BLACK      0
#define ANSI_RED        1
#define ANSI_GREEN      2
#define ANSI_YELLOW     3
#define ANSI_BLUE       4
#define ANSI_MAGENTA    5
#define ANSI_CYAN       6
#define ANSI_WHITE      7

/*
** In the SGR ANSI escape sequence:
**      colour number+30 = change foreground
**      colour number+40 = change background
*/
#define ANSI_FOREGROUND 30
#define ANSI_BACKGROUND 40
#define ANSI_BOLD       1
#define ANSI_BLINK      5
#define ANSI_REVERSE    7

static byte colourmap [] = {
  ANSI_BLACK, ANSI_RED, ANSI_GREEN, ANSI_YELLOW, ANSI_BLUE, ANSI_MAGENTA,
  ANSI_CYAN, ANSI_WHITE, ANSI_BLACK, ANSI_RED, ANSI_GREEN, ANSI_YELLOW,
  ANSI_BLUE, ANSI_MAGENTA, ANSI_CYAN, ANSI_WHITE
};

#else

/*
** Conio colour numbers. The conio colour table maps the RISC OS physical
** colour numbers to those used by conio in 2, 4 and 16 colour modes
*/
#ifdef TARGET_MINGW
static void gotoxy(int32, int32);
static int wherex(void);
static int wherey(void);
#define FG_TEXT_ATTRIB_SHIFT 0
#define BG_TEXT_ATTRIB_SHIFT 4
#define BLACK                0
#if (FOREGROUND_RED << BG_TEXT_ATTRIB_SHIFT) != (BACKGROUND_RED)
#error "Check assumption about foreground & background"
#endif
static byte colourmap [] = {
  BLACK, FOREGROUND_RED | FOREGROUND_INTENSITY, FOREGROUND_GREEN | FOREGROUND_INTENSITY,
  FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY, FOREGROUND_BLUE | FOREGROUND_INTENSITY,
  FOREGROUND_BLUE | FOREGROUND_RED | FOREGROUND_INTENSITY,
  FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY,
  FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
  FOREGROUND_INTENSITY, FOREGROUND_RED, FOREGROUND_GREEN, FOREGROUND_RED | FOREGROUND_GREEN,
  FOREGROUND_BLUE, FOREGROUND_BLUE | FOREGROUND_RED,
  FOREGROUND_BLUE | FOREGROUND_GREEN, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE 
};
#else
static byte colourmap [] = {
  BLACK, LIGHTRED, LIGHTGREEN, YELLOW, LIGHTBLUE, LIGHTMAGENTA,
  LIGHTCYAN, WHITE, DARKGRAY, RED, GREEN, BROWN, BLUE, MAGENTA,
  CYAN, LIGHTGRAY
};
#endif

#endif

#ifdef USE_ANSI
/*
** 'find_cursor' reads the position of the cursor on the text
** screen. It can only do this if input is coming from the
** keyboard and output is going to the screen, that is, stdin
** and stdout have not been redirected
** -- ANSI --
*/
void find_cursor(void) {
  int ch, column, row;
  column = row = 0;
  if (!basicvars.runflags.outredir && !basicvars.runflags.inredir) {
    printf("\033[6n");  /* ANSI/VTxxx sequence to find the position of the cursor */
    fflush(stdout);
    ch = read_key();    /* Sequence expected back is ' ESC[<no>;<no>R' */
    if (ch!='\033') return;
    ch = read_key();
    if (ch!='[') return;
    ch = read_key();
    while (isdigit(ch)) {
      row = row*10+ch-'0';
      ch = read_key();
    }
    if (ch!=';') return;
    ch = read_key();
    while (isdigit(ch)) {
      column = column*10+ch-'0';
      ch = read_key();
    }
    if (ch!='R') return;
    xtext = column-1;   /* The programs wants RISC OS text coordinates, */
    ytext = row-1;      /* not ANSI ones */
    if (xtext<twinleft) /* Ensure that the cursor lies within the text window */
      xtext = twinleft;
    else if (xtext>twinright)
      xtext = twinright;
    else if (ytext<twintop)
      ytext = twintop;
    else if (SCRHEIGHT!=0 && ytext>twinbottom) {
      ytext = twinbottom;
    }
  }
}

/*
** 'reset_screen' is called to carry out anything needed to reset
** the screen to its default settings
** -- ANSI --
*/
static void reset_screen(void) {
  if (textwin) printf("\033[%d;%dr", 1, textheight);    /* Set scrolling region to whole screen */
}

/*
** The following functions duplicate the actions of functions
** in the 'conio' text output library. They are used in versions
** of the code where conio is not available and ANSI control
** sequences have to be used
*/

/*
** 'putch' displays a character.
** -- ANSI --
*/
static void putch(int32 ch) {
  putchar(ch);
  if (echo) fflush(stdout);
}

/*
** 'gotoxy' moves the text cursor to column 'x' row 'y' on the screen.
** The top left-hand corner of the screen is (1, 1). This function is
** called with RISC OS screen coordinates, which start at (0, 0). It
** is up to the calling code to allow for this.
** -- ANSI --
*/
static void gotoxy(int32 x, int32 y) {
  printf("\033[%d;%dH", y, x);  /* VTxxx/ANSI sequence to move cursor */
  fflush(stdout);
}

/*
** 'scroll_text' is called to move the text window up or down a line.
** This version of the code can only scroll the entire screen up or
** down. It does not support the text window
** -- ANSI --
*/
static void scroll_text(updown direction) {
  if (textwin) return;
  if (direction==SCROLL_UP)     /* Move screen up - Output a linefeed */
    printf("\n\033[%d;%dH", ytext+1, xtext+1);  /* Move screen up and move cursor to correct place */
  else {        /* Move screen down */
    printf("\033[L");
  }
  fflush(stdout);
}

/*
** 'textcolor' sets the text foreground colour
** -- ANSI --
*/
static void textcolor(int32 colour) {
  printf("\033[1;%dm", colour+ANSI_FOREGROUND); /* VTxxx/ANSI sequence to set the foreground colour */
}

/*
** 'textbackground' sets the text background colour
** -- ANSI --
*/
static void textbackground(int32 colour) {
  printf("\033[%dm", colour+ANSI_BACKGROUND);   /* VTxxx/ANSI sequence to set the background colour */
}

/*
** 'clrscr' clears the screen and sends the cursor to the top
** left-hand corner of the screen
** -- ANSI --
*/
static void clrscr(void) {
  printf("\033[2J\033[H");      /* VTxxx/ANSI sequence for clearing the screen and to 'home' the cursor */
  fflush(stdout);
}

/*
** 'set_cursor' sets the type of the text cursor used on the graphic
** screen to either a block or an underline. There is no ANSI
** equivalent of this so the function is just a no-op
** -- ANSI --
*/
void set_cursor(boolean underline) {
}

/*
** 'echo_on' turns on the immediate echo of characters to the screen
** -- ANSI --
*/
void echo_on(void) {
  echo = TRUE;
  fflush(stdout);
}

/*
** 'echo_off' turns off the immediate echo of characters to the screen.
** -- ANSI --
*/
void echo_off(void) {
  echo = FALSE;
}

#else

/* ========== conio functions ========== */

/*
** 'find_cursor' locates the cursor on the text screen and ensures that
** its position is valid, that is, that it lies within the text window
** -- conio --
*/
void find_cursor(void) {
  if (!basicvars.runflags.outredir) {
    xtext = wherex()-1;
    ytext = wherey()-1;
    if (xtext<twinleft)
      xtext = twinleft;
    else if (xtext>twinright)
      xtext = twinright;
    else if (ytext<twintop)
      ytext = twintop;
    else if (ytext>twinbottom) {
      ytext = twinbottom;
    }
    gotoxy(xtext+1, ytext+1);
  }
}

/*
** 'set_cursor' sets the type of the text cursor used on the graphic
** screen to either a block or an underline. 'underline' is set to
** TRUE if an underline is required. Underline is used by the program
** when keyboard input is in 'insert' mode and a block when it is in
** 'overwrite'. Transitioning to block display overrides any VDU23 hidden
** state.
** -- conio --
*/
void set_cursor(boolean underline) {
  if (!basicvars.runflags.outredir) {
#ifdef TARGET_MINGW
    CONSOLE_CURSOR_INFO cursor;

    cursmode = underline ? UNDERLINE : BLOCK;
    cursor.dwSize = underline ? 1 : 100; /* Percent */
    cursor.bVisible = (cursorstate != HIDDEN);
    SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &cursor);
#else
    cursmode = underline ? UNDERLINE : BLOCK;
    if (cursmode==UNDERLINE)
      _setcursortype((cursorstate == HIDDEN) ? _NOCURSOR : _NORMALCURSOR);
    else {
      _setcursortype((cursorstate == HIDDEN) ? _NOCURSOR : _SOLIDCURSOR);
    }
#endif
  }
}

/*
** 'reset_screen' is called to carry out anything needed to reset
** the screen to its default settings
** -- conio --
*/
static void reset_screen(void) {
}

#ifdef TARGET_MINGW
/*
** 'gotoxy' moves the text cursor to column 'x' row 'y' on the screen.
** This function is called with RISC OS screen coordinates, which start
** at (0, 0).
** -- conio (Win32) --
*/
static void gotoxy(int32 x, int32 y) {
  COORD pos;

  pos.X = x - 1;
  pos.Y = y - 1;
  SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), pos);
}

/*
** 'wherex' returns the cursor position with the origin at (1, 1).
** This function is replicated here for 32 bit console apps.
** -- conio (Win32) --
*/
static int wherex(void) {
  CONSOLE_SCREEN_BUFFER_INFO    info;

  GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info);
  return info.dwCursorPosition.X;
}

/*
** 'wherey' returns the cursor position with the origin at (1, 1).
** This function is replicated here for 32 bit console apps.
** -- conio (Win32) --
*/
static int wherey(void) {
  CONSOLE_SCREEN_BUFFER_INFO    info;

  GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info);
  return info.dwCursorPosition.Y;
}

/*
** 'clrscr' clears the screen and sends the cursor to the top
** left-hand corner of the screen
** -- conio (Win32) --
*/
void clrscr(void) {
  system("cls");
}

/*
** 'textcolor' and
** 'textbackground' set the foreground and background colours, though as the console text
** attributes are a bitmask of both these functions just mix the parameter with the
** global for the opposite of their own name
** -- conio (Win32) --
*/
textcolor(int32 colour) {
  SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), (colour << FG_TEXT_ATTRIB_SHIFT) |
                                                           (text_physbackcol << BG_TEXT_ATTRIB_SHIFT));
}

textbackground(int32 colour) {
  SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), (text_physforecol << FG_TEXT_ATTRIB_SHIFT) |
                                                           (colour << BG_TEXT_ATTRIB_SHIFT));
}
#endif

/*
** 'scroll_text' is called to move the text window up or down a line.
** Note that the coordinates here are in RISC OS text coordinates which
** start at (0, 0) whereas conio's start with (1, 1) at the top left-hand
** corner of the screen.
** -- conio --
*/
static void scroll_text(updown direction) {
  int n;
  if (!textwin && direction==SCROLL_UP)         /* Text window is the whole screen and scrolling is upwards */
    putch('\n');        /* Output a linefeed */
  else {        /* Writing to a text window */
#ifdef TARGET_MINGW
    SMALL_RECT  scroll;
    SMALL_RECT  clip;
    COORD       dest;
    CHAR_INFO   clear;

    scroll.Left = clip.Left = twinleft;
    scroll.Right = clip.Right = twinright;
    scroll.Top = clip.Top = twintop;
    scroll.Bottom = clip.Bottom = twinbottom;
    dest.X = twinleft;
    dest.Y = twintop - 1;
    clear.Char.AsciiChar = ' ';        /* No foreground colour needed as it's cleared with space */
    clear.Attributes = text_physbackcol << BG_TEXT_ATTRIB_SHIFT;
    ScrollConsoleScreenBuffer(GetStdHandle(STD_OUTPUT_HANDLE), &scroll, &clip, dest, &clear);
#else
    if (twintop!=twinbottom) {  /* Text window is more than one line high */
      if (direction==SCROLL_UP) /* Scroll text up a line */
        (void) movetext(twinleft+1, twintop+2, twinright+1, twinbottom+1, twinleft+1, twintop+1);
      else {    /* Scroll text down a line */
        (void) movetext(twinleft+1, twintop+1, twinright+1, twinbottom, twinleft+1, twintop+2);
      }
    }
    gotoxy(twinleft+1, ytext+1);
    echo_off();
    for (n=twinleft; n<=twinright; n++) putch(' ');     /* Clear the vacated line of the window */
    echo_on();
#endif
  }
  gotoxy(xtext+1, ytext+1);     /* Put the cursor back where it should be */
}

/*
** 'echo_on' turns on the immediate echo of characters to the screen
** -- conio --
*/
void echo_on(void) {
}

/*
** 'echo_off' turns off the immediate echo of characters to the screen.
** -- conio --
*/
void echo_off(void) {
}

#endif

/*
** 'vdu_2317' deals with various flavours of the sequence VDU 23,17,...
*/
static void vdu_2317(void) {
  int32 temp;
  switch (vduqueue[1]) {        /* vduqueue[1] is the byte after the '17' and says what to change */
  case TINT_FORETEXT:           /* Foreground text colour tint */
    text_foretint = (vduqueue[2] & TINTMASK)>>TINTSHIFT;        /* Third byte in queue is new TINT value */
    if (colourdepth==256) text_physforecol = (text_forecol<<COL256SHIFT)+text_foretint;
    break;
  case TINT_BACKTEXT:           /* Background text colour tint */
    text_backtint = (vduqueue[2] & TINTMASK)>>TINTSHIFT;
    if (colourdepth==256) text_physbackcol = (text_backcol<<COL256SHIFT)+text_backtint;
    break;
  case TINT_FOREGRAPH:          /* Foreground and background graphics colour tints - Just ignore these two */
  case TINT_BACKGRAPH:
    break;
  case EXCH_TEXTCOLS:   /* Exchange text foreground and background colours */
    temp = text_forecol; text_forecol = text_backcol; text_backcol = temp;
    temp = text_physforecol; text_physforecol = text_physbackcol; text_physbackcol = temp;
    temp = text_foretint; text_foretint = text_backtint; text_backtint = temp;
    break;
  default:              /* Ignore bad value */
    break;
  }
}

/*
** 'vdu_23command' emulates some of the VDU 23 command sequences
*/
static void vdu_23command(void) {
  switch (vduqueue[0]) {        /* First byte in VDU queue gives the command type */
  case 1:       /* Control the appear of the text cursor */
    if (vduqueue[1]==0) {
      cursorstate = HIDDEN;    /* 0 = hide, 1 = show */
      set_cursor(cursmode);
    }
    if (vduqueue[1]==1 && cursorstate!=NOCURSOR) {
      cursorstate = ONSCREEN;
      set_cursor(cursmode);    /* Unhiding always starts as an underscore */
    }
    break;
  case 8:       /* Clear part of the text window */
    break;
  case 17:      /* Set the tint value for a colour in 256 colour modes, etc */
    vdu_2317();
  }
}

/*
** 'move_cursor' sends the text cursor to the position (column, row)
** on the screen.  It updates the cursor position as well.
** The column and row are given in RISC OS text coordinates, that
** is, (0,0) is the top left-hand corner of the screen. These values
** are the true coordinates on the screen. The code that uses this
** function has to allow for the text window.
*/
static void move_cursor(int32 column, int32 row) {
  xtext = column;
  ytext = row;
  gotoxy(column+1, row+1);
}

/*
** 'map_colour' for non-graphics (text only) operation. The number
** of colours available is limited to at most sixteen. 256 colour
** modes are dealt with by taking the six bit colour number and
** interpreting it as a bit pattern of the form 'bbggrr' where
** 'bb', 'gg' and 'rr' are two bit colour component values. It
** uses the most significant bit of each component to form a
** RISC OS physical colour number. It is a hack but it does the
** job
*/
static int32 map_colour(int32 colour) {
  if (colourdepth<=16)
    return colourmap[logtophys[colour]];
  else {        /* Map a 256 colour colour number to a sixteen bit one */
    int32 temp = 0;
    if (colour & C256_REDBIT) temp+=VDU_RED;
    if (colour & C256_GREENBIT) temp+=VDU_GREEN;
    if (colour & C256_BLUEBIT) temp+=VDU_BLUE;
    return colourmap[temp];
  }
}

/*
** 'vdu_setpalette' changes one of the logical to physical colour map
** entries (VDU 19).
** Note that this function should have the side effect of changing all
** pixels of logical colour number 'logcol' to the physical colour
** given by 'mode' but the code does not do this.
*/
static void vdu_setpalette(void) {
  int32 logcol, mode;
  logcol = vduqueue[0] & colourmask;
  mode = vduqueue[1];
  if (mode>15) {
    if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
    return;             /* Silently ignore command as it is 'cosmetic' */
  }
  if (colourdepth<=16) logtophys[logcol] = mode;
}

#ifdef USE_ANSI

/* ========== ANSI ========== */

/*
** 'echo_char' sends the character in the VDU command queue to the
** output stream. This is used to provide an escape mechanism to allow
** any character to be sent rather than having characters with codes
** less than 32 treated as VDU commands. VDU 1 is hijacked to do this.
** -- ANSI --
*/
static void echo_char(void) {
  putchar(vduqueue[0]);
  if (echo) fflush(stdout);
}

/*
** 'move_curback' moves the cursor back one character on the screen (VDU 8).
** -- ANSI --
*/
static void move_curback(void) {
  xtext--;
  if (xtext>=twinleft)  /* Cursor is still within the text window */
    printf("\033[D");   /* ANSI sequence to move the cursor back one char */
  else {        /* Cursor is at left-hand edge of text window so move up a line */
    xtext = twinright;
    ytext--;
    if (ytext>=twintop) /* Cursor is still within the confines of the text window */
      printf("\033[A\033[%dG", xtext+1);        /* Move cursor up and to last column */
    else {      /* Cursor is now above the top of the window */
      ytext++;  /* Scroll window down a line */
      scroll_text(SCROLL_DOWN);
      printf("\033[%dG", xtext+1);      /* Move cursor to last column */
    }
  }
  fflush(stdout);
}

/*
** 'move_curforward' moves the cursor forwards one character on the
** screen (VDU 9)
** -- ANSI --
*/
static void move_curforward(void) {
  xtext++;
  if (xtext<=twinright) /* Cursor is still within the text window */
    printf("\033[C");   /* ANSI sequence to move the cursor forwards one char */
  else {        /* Cursor is at right-hand edge of text window so move down a line */
    xtext = twinleft;
    ytext++;
    printf("\n\033[%dG", xtext+1);      /* Move cursor down and to first column */
  }
  fflush(stdout);
}

/*
** 'move_curdown' moves the cursor down the screen, that is, it
** performs the linefeed operation (VDU 10)
** -- ANSI --
*/
static void move_curdown(void) {
  ytext++;
  printf("\n\033[%dG", xtext+1);
  fflush(stdout);
}

/*
** 'move_curup' moves the cursor up a line on the screen (VDU 11)
** -- ANSI --
*/
static void move_curup(void) {
  ytext--;
  if (ytext>=twintop)           /* Cursor is still within the window */
    printf("\033[A");   /* ANSI sequence to move the cursor up one line */
  else {                /* Cursor is above the top of the window */
    ytext++;            /* Scroll window down a line */
    scroll_text(SCROLL_DOWN);
  }
  fflush(stdout);
}

/*
** 'vdu_cleartext' clears the text window. Normally this is the
** entire screen (VDU 12)
** -- ANSI --
*/
static void vdu_cleartext(void) {
  if (textwin) {        /* Text window defined that does not occupy the whole screen */
    int32 row;
    for (row = twintop; row<=twinbottom; row++) {
      printf("\033[%d;%dH\033[%dX", row+1, twinleft+1, twinright-twinleft+1);   /* Clear the line */
    }
    fflush(stdout);
    move_cursor(twinleft, twintop);     /* Send cursor to home position in window */
  }
  else {    /* No text window has been defined */
    clrscr();
    xtext = twinleft;
    ytext = twintop;
  }
}

/*
** 'vdu_return' deals with the carriage return character (VDU 13).
** -- ANSI --
*/
static void vdu_return(void) {
  printf("\033[%dG", twinleft+1);
  fflush(stdout);
  xtext = twinleft;
}

/*
** 'vdu_textwind' defines a text window (VDU 28)
** -- ANSI --
*/
static void vdu_textwind(void) {
  int32 left, right, top, bottom;
  left = vduqueue[0];
  bottom = vduqueue[1];
  right = vduqueue[2];
  top = vduqueue[3];
  if (left>right) {     /* Ensure right column number > left */
    int32 temp = left;
    left = right;
    right = temp;
  }
  if (bottom<top) {     /* Ensure bottom line number > top */
    int32 temp = bottom;
    bottom = top;
    top = temp;
  }
  if (left>=textwidth || SCRHEIGHT!=0 && top>=textheight) return;       /* Ignore bad parameters */
  twinleft = left;
  twinright = right;
  twintop = top;
  twinbottom = bottom;
/* Set flag to say if text window occupies only a part of the screen */
  textwin = left>0 || right<textwidth-1 || top>0 || bottom<textheight-1;
/*
** If the text window is the full width of the screen then it is
** possible to set a 'scrolling region' for that part of the screen
** using an ANSI control sequence so that the contents of the window
** can be scrolled up or down
*/
  if (textwin && left==0 && right==textwidth-1) printf("\033[%d;%dr", twintop+1, twinbottom+1);
  move_cursor(twinleft, twintop);       /* Move text cursor to home position in new window */
}

#else

/* ========== conio ========== */

/*
** 'echo_char' does nothing in the conio version of the code
** -- conio --
*/
static void echo_char(void) {
}

/*
** 'move_curback' moves the cursor back one character on the screen (VDU 8).
** -- conio --
*/
static void move_curback(void) {
  xtext--;
  if (xtext>=twinleft)  /* Cursor is still within the text window */
    putch('\b');
  else {        /* Cursor is at left-hand edge of text window so move up a line */
    xtext = twinright;
    ytext--;
    if (ytext>=twintop)         /* Cursor is still within the confines of the text window */
      gotoxy(xtext+1, ytext+1);
    else {      /* Cursor is now above the top of the window */
      ytext++;
      scroll_text(SCROLL_DOWN);         /* Scroll the window down a line */
    }
  }
}

/*
** 'move_curforward' moves the cursor forwards one character on the
** screen (VDU 9)
** -- conio --
*/
static void move_curforward(void) {
  xtext++;
  if (xtext<=twinright) /* Cursor is still within the text window */
    gotoxy(xtext+1, ytext+1);
  else {        /* Cursor is at right-hand edge of text window so move down a line */
    xtext = twinleft;
    ytext++;
    if (ytext<=twinbottom)      /* Text cursor is still within the confines of the window */
      gotoxy(xtext+1, ytext+1);
    else {      /* Cursor has moved below bottom of window, so scroll screen up a line */
      ytext--;
      scroll_text(SCROLL_UP);
    }
  }
}

/*
** 'move_curdown' moves the cursor down the screen, that is, it
** performs the linefeed operation (VDU 10)
** -- conio --
*/
static void move_curdown(void) {
  ytext++;
  if (ytext<=twinbottom)
    gotoxy(xtext+1, ytext+1);
  else {        /* At bottom of window. Move window up a line */
    ytext--;
    scroll_text(SCROLL_UP);
  }
}

/*
** 'move_curup' moves the cursor up a line on the screen (VDU 11)
** -- conio --
*/
static void move_curup(void) {
  ytext--;
  if (ytext>=twintop)           /* Cursor is still within the window */
    gotoxy(xtext+1, ytext+1);
  else {                /* Cursor is above the top of the window */
    ytext++;
    scroll_text(SCROLL_DOWN);   /* Scroll the window down a line */
  }
}

/*
** 'vdu_cleartext' clears the text window. Normally this is the
** entire screen (VDU 12)
** -- conio --
*/
static void vdu_cleartext(void) {
  if (textwin) {        /* Text window defined that does not occupy the whole screen */
    int32 column, row;
    for (row = twintop; row<=twinbottom; row++) {
      gotoxy(twinleft+1, row+1);        /* Go to start of line on screen */
      for (column = twinleft; column<=twinright; column++) putch(' ');
    }
    move_cursor(twinleft, twintop);     /* Send cursor to home position in window */
  }
  else {    /* No text window has been defined */
    clrscr();
    xtext = twinleft;
    ytext = twintop;
  }
}

/*
** 'vdu_return' deals with the carriage return character (VDU 13).
** -- conio --
*/
static void vdu_return(void) {
  move_cursor(twinleft, ytext);
}

/*
** 'vdu_textwind' defines a text window (VDU 28)
** -- conio --
*/
static void vdu_textwind(void) {
  int32 left, right, top, bottom;
  left = vduqueue[0];
  bottom = vduqueue[1];
  right = vduqueue[2];
  top = vduqueue[3];
  if (left>right) {     /* Ensure right column number > left */
    int32 temp = left;
    left = right;
    right = temp;
  }
  if (bottom<top) {     /* Ensure bottom line number > top */
    int32 temp = bottom;
    bottom = top;
    top = temp;
  }
  if (left>=textwidth || top>=textheight) return;       /* Ignore bad parameters */
  twinleft = left;
  twinright = right;
  twintop = top;
  twinbottom = bottom;
/* Set flag to say if text window occupies only a part of the screen */
  textwin = left>0 || right<textwidth-1 || top>0 || bottom<textheight-1;
  move_cursor(twinleft, twintop);       /* Move text cursor to home position in new window */
}

#endif

/*
** 'vdu_textcol' changes the text colour to the value in the VDU queue
** (VDU 17). It handles both foreground and background colours at any
** colour depth. The RISC OS physical colour number is mapped to either
** a conio or an ANSI equivalent. In the case of the ANSI colour numbers,
** the 'bold' attribute is also sent in the escape sequence otherwise the
** colours are too dark
*/
static void vdu_textcol(void) {
  int32 colnumber;
  colnumber = vduqueue[0];
  if (colnumber<128) {  /* Setting foreground colour */
    text_forecol = colnumber & colourmask;
    text_physforecol = map_colour(text_forecol);
    textcolor(text_physforecol);
  }
  else {        /* Setting background colour */
    text_backcol = (colnumber-128) & colourmask;
    text_physbackcol = map_colour(text_backcol);
    textbackground(text_physbackcol);
  }
}

/*
** 'reset_colours' initialises the RISC OS logical to physical colour
** map for the current screen mode and sets the default foreground
** and background text colours to white and black respectively (VDU 20)
*/
static void reset_colours(void) {
  switch (colourdepth) {        /* Initialise the text mode colours */
  case 2:
    logtophys[0] = VDU_BLACK;
    logtophys[1] = VDU_WHITE;
    text_forecol = 1;
    break;
  case 4:
    logtophys[0] = VDU_BLACK;
    logtophys[1] = VDU_RED;
    logtophys[2] = VDU_YELLOW;
    logtophys[3] = VDU_WHITE;
    text_forecol = 3;
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
    text_forecol = 7;
    break;
  case 256:
    text_forecol = 63;
    text_foretint = MAXTINT;
    text_backtint = 0;
    break;
  default:      /* 32K and 16M colour depths are not supported */
    error(ERR_UNSUPPORTED);
  }
  if (colourdepth==256)
    colourmask = COL256MASK;
  else {
    colourmask = colourdepth-1;
  }
  text_backcol = 0;
  text_physforecol = map_colour(text_forecol);
  text_physbackcol = map_colour(text_backcol);
}

/*
** 'vdu_restwind' restores the default (full screen) text window (VDU 26).
*/
static void vdu_restwind(void) {
  twinleft = 0;
  twinright = textwidth-1;
  twintop = 0;
  twinbottom = textheight-1;
  reset_screen();
  move_cursor(0, 0);
}

/*

** 'vdu_hometext' sends the text cursor to the top left-hand corner of
** the text window (VDU 30). This is the version of the function used
** when the interpreter does not support graphics
*/
static void vdu_hometext(void) {
  move_cursor(twinleft, twintop);
}

/*
** 'vdu_movetext' moves the text cursor to the given column and row in
** the text window (VDU 31). This is the version of the function used
** when the interpreter does not support graphics
*/
static void vdu_movetext(void) {
  int32 column, row;
  column = vduqueue[0]+twinleft;
  row = vduqueue[1]+twintop;
  if (column>twinright || SCRHEIGHT!=0 && row>twinbottom) return;       /* Ignore command if values are out of range */
  move_cursor(column, row);
}

/*
** 'nogo' is called in cases where VDU commands cannot be used
** such as when stdout is a file. If 'flag_cosmetic' is set then
** the program is abandoned with an error. If not the VDU command
** is silently ignored
*/
static void nogo(void) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_NOVDUCMDS);
}

#ifdef USE_ANSI

/*
** 'print_char' is called to display a character on the screen
** -- ANSI --
*/
static void print_char(int32 charvalue) {
  if (charvalue==DEL) charvalue=' ';    /* Hack for DOS */
  if (!basicvars.runflags.outredir) {           /* Output is going to screen */
/* ANSI control sequence code */
    putchar(charvalue);
    xtext++;
    if (xtext>twinright) {              /* Have reached edge of text window. Skip to next line  */
      xtext = twinleft;
      ytext++;
      printf("\n\033[%dG", xtext+1);
    }
    if (echo) fflush(stdout);
  }
  else {        /* Output is going elsewhere, probably a file */
    putchar(charvalue);
  }
}

#else

/*
** 'print_char' is called to display a character on the screen
*/
static void print_char(int32 charvalue) {
  if (charvalue==DEL) charvalue=' ';    /* Hack for DOS */
  if (!basicvars.runflags.outredir) {           /* Output is going to screen */
    putch(charvalue);
    xtext++;
    if (xtext>twinright) {              /* Have reached edge of text window. Skip to next line  */
      xtext = twinleft;
      ytext++;
      if (ytext<=twinbottom)            /* Cursor is still somewhere on the screen */
        gotoxy(xtext+1, ytext+1);
      else {    /* Cursor is on bottom line of screen - Scroll screen up */
        ytext--;
        if (textwin)
          scroll_text(SCROLL_UP);
        else {
          gotoxy(xtext+1, ytext+1);
        }
      }
    }
  }
  else {        /* Output is going elsewhere, probably a file */
    putchar(charvalue);
  }
}

#endif

/*
** 'emulate_vdu' is a simple emulation of the RISC OS VDU driver. It
** accepts characters as per the RISC OS driver and uses them to imitate
** some of the VDU commands. Some of them are not supported and flagged
** as errors but others, for example, the 'page mode on' and 'page mode
** off' commands, are silently ignored.
*/
void emulate_vdu(int32 charvalue) {
  charvalue = charvalue & BYTEMASK;     /* Deal with any signed char type problems */
  if (vduneeded==0) {                   /* VDU queue is empty */
    if (charvalue>=' ') {               /* Most common case - print something */
      print_char(charvalue);
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

  if (!basicvars.runflags.outredir) {   /* Output is going to a screen - Can use VDU commands */
    switch (vducmd) {   /* Emulate the various control codes */
    case VDU_NULL:      /* 0 - Do nothing */
    case VDU_ENAPRINT:  /* 2 - Enable the sending of characters to the printer (ignored) */
    case VDU_DISPRINT:  /* 3 - Disable the sending of characters to the printer (ignored) */
    case VDU_TEXTCURS:  /* 4 - Print text at text cursor (ignored) */
    case VDU_ENABLE:    /* 6 - Enable the VDU driver (ignored) */
    case VDU_ENAPAGE:   /* 14 - Enable page mode (ignored) */
    case VDU_DISPAGE:   /* 15 - Disable page mode (ignored) */
    case VDU_DISABLE:   /* 21 - Disable the VDU driver (ignored) */
      break;
    case VDU_GRAPHICURS:        /* 5 - Print text at graphics cursor */
    case VDU_CLEARGRAPH:        /* 16 - Clear graphics window */
    case VDU_GRAPHCOL:  /* 18 - Change current graphics colour */
    case VDU_DEFGRAPH:  /* 24 - Define graphics window */
    case VDU_PLOT:      /* 25 - Issue graphics command */
    case VDU_ORIGIN:    /* 29 - Define graphics origin */
      error(ERR_NOGRAPHICS);
      break;
    case VDU_PRINT:     /* 1 - Send next character to the print stream */
      echo_char();
      break;
    case VDU_BEEP:      /* 7 - Sound the bell */
      putch('\7');
      break;
    case VDU_CURBACK:   /* 8 - Move cursor left one character */
      move_curback();
      break;
    case VDU_CURFORWARD:        /* 9 - Move cursor right one character */
      move_curforward();
      break;
    case VDU_CURDOWN:   /* 10 - Move cursor down one line (linefeed) */
      move_curdown();
      break;
    case VDU_CURUP:     /* 11 - Move cursor up one line */
      move_curup();
      break;
    case VDU_CLEARTEXT: /* 12 - Clear text window (formfeed) */
      vdu_cleartext();
      break;
    case VDU_RETURN:    /* 13 - Carriage return */
      vdu_return();
      break;
    case VDU_TEXTCOL:   /* 17 - Change current text colour */
      vdu_textcol();
      break;
    case VDU_LOGCOL:    /* 19 - Map logical colour to physical colour */
      vdu_setpalette();
      break;
    case VDU_RESTCOL:   /* 20 - Restore logical colours to default values */
      reset_colours();
      break;
    case VDU_SCRMODE:   /* 22 - Change screen mode */
      emulate_mode(vduqueue[0]);
      break;
    case VDU_COMMAND:   /* 23 - Assorted VDU commands */
      vdu_23command();
      break;
    case VDU_RESTWIND:  /* 26 - Restore default windows */
      vdu_restwind();
      break;
    case VDU_ESCAPE:    /* 27 - Do nothing (but char is sent to screen anyway) */
      putch(vducmd);
      break;
    case VDU_DEFTEXT:   /* 28 - Define text window */
      vdu_textwind();
      break;
    case VDU_HOMETEXT:  /* 30 - Send cursor to top left-hand corner of screen */
      vdu_hometext();
      break;
    case VDU_MOVETEXT:  /* 31 - Send cursor to column x, row y on screen */
      vdu_movetext();
    }
  }
  else {
/*
** Output is not to the screen (it is most probably going to a
** file). The majority of the VDU commands are meaningless under
** these circumstances.
*/
    switch (vducmd) {
    case VDU_NULL:              /* 0 - Do nothing */
    case VDU_PRINT:             /* 1 - Send next character to the print stream (ignored) */
    case VDU_ENAPRINT:          /* 2 - Enable the sending of characters to the printer (ignored) */
    case VDU_DISPRINT:          /* 3 - Disable the sending of characters to the printer (ignored) */
    case VDU_TEXTCURS:          /* 4 - Print text at text cursor (ignored) */
    case VDU_ENABLE:            /* 6 - Enable the VDU driver (ignored) */
    case VDU_ENAPAGE:           /* 14 - Enable page mode (ignored) */
    case VDU_DISPAGE:           /* 15 - Disable page mode (ignored) */
    case VDU_DISABLE:           /* 21 - Disable the VDU driver (ignored) */
      break;
    case VDU_GRAPHICURS:        /* 5 - Print text at graphics cursor */
    case VDU_CLEARGRAPH:        /* 16 - Clear graphics window */
    case VDU_GRAPHCOL:          /* 18 - Change current graphics colour */
    case VDU_DEFGRAPH:          /* 24 - Define graphics window */
    case VDU_PLOT:              /* 25 - Issue graphics command */
    case VDU_ORIGIN:            /* 29 - Define graphics origin */
      error(ERR_NOGRAPHICS);
      break;
    case VDU_CURUP:             /* 11 - Move cursor up one line */
    case VDU_CLEARTEXT:         /* 12 - Clear text window (formfeed) */
    case VDU_TEXTCOL:           /* 17 - Change current text colour */
    case VDU_LOGCOL:            /* 19 - Map logical colour to physical colour */
    case VDU_RESTCOL:           /* 20 - Restore logical colours to default values */
    case VDU_SCRMODE:           /* 22 - Change screen mode */
    case VDU_COMMAND:           /* 23 - Assorted VDU commands */
    case VDU_RESTWIND:          /* 26 - Restore default windows */
    case VDU_DEFTEXT:           /* 28 - Define text window */
    case VDU_HOMETEXT:          /* 30 - Send cursor to top left-hand corner of screen */
    case VDU_MOVETEXT:          /* 31 - Send cursor to column x, row y on screen */
      nogo();
      break;
    case VDU_BEEP:              /* 7 - Sound the bell */
    case VDU_CURBACK:           /* 8 - Move cursor left one character */
    case VDU_CURFORWARD:        /* 9 - Move cursor right one character */
    case VDU_CURDOWN:           /* 10 - Move cursor down one line (linefeed) */
    case VDU_RETURN:            /* 13 - Carriage return */
    case VDU_ESCAPE:            /* 27 - Do nothing (but char is sent to screen anyway) */
      putchar(vducmd);
      break;
    }
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
  int32 n, length;
  va_list parms;
  char text [MAXSTRING];
  va_start(parms, format);
  length = vsprintf(text, format, parms);
  va_end(parms);
  echo_off();
  for (n = 0; n < length; n++) emulate_vdu(text[n]);
  echo_on();
}

/*
** emulate_vdufn - Emulates the Basic VDU function. This
** returns the value of the specified VDU variable. Only a
** small subset of the possible values are returned
*/
int32 emulate_vdufn(int variable) {
  switch (variable) {
  case 0: /* ModeFlags */       return 1;
  case 1: /* ScrRCol */         return textwidth - 1;
  case 2: /* ScrBRow */         return textheight - 1;
  case 3: /* NColour */         return colourdepth - 1;
  case 132: /* TWLCol */        return twinleft;
  case 133: /* TWBRow */        return twinbottom;
  case 134: /* TWRCol */        return twinright;
  case 135: /* TWTRow */        return twintop;
  case 155: /* TForeCol */      return text_forecol;
  case 156: /* TBackCol */      return text_backcol;
  case 159: /* TFTint */        return text_foretint;
  case 160: /* TBTint */        return text_backtint;
  case 161: /* MaxMode */       return HIGHMODE;
  default:
    return 0;
  }
}

/*
 * emulate_colourfn - This performs the function COLOUR().
 * As there is no palette in this version of the graphics
 * code it always returns white (the last colour)
 */
int32 emulate_colourfn(int32 red, int32 green, int32 blue) {
  return colourdepth - 1;
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
** 'setup_mode' is called to set up the details of mode 'mode'.
** The code supports the standard RISC OS numeric screen modes with
** the addition of a special mode that sets the screen parameters
** according to the real size of the screen. This is mode 127. The
** interpreter starts in this mode and switching to mode 127 will
** return to it
*/
static void setup_mode(int32 mode) {
  int32 modecopy;
  modecopy = mode;
  mode = mode & MODEMASK;       /* Lose 'shadow mode' bit */
  if (mode==USERMODE) {         /* Set screen parameters according to real screen size */
    screenmode = modecopy;
    colourdepth = 16;
    textwidth = realwidth;
    textheight = realheight;
  }
  else {
    if (mode>HIGHMODE) mode = modecopy = 0;     /* User-defined modes are mapped to mode 0 */
    if (modetable[mode].xtext>SCRWIDTH) error(ERR_BADMODE);
/* Set up VDU driver parameters for mode */
    screenmode = modecopy;
    colourdepth = modetable[mode].coldepth;
    textwidth = modetable[mode].xtext;
    textheight = realheight;            /* Ignore the height of the screen as given by the mode def'n */
  }
  enable_vdu = TRUE;
  echo = TRUE;
  cursmode = UNDERLINE;
  cursorstate = ONSCREEN;      /* Text mode cursor is being displayed */
  textwin = FALSE;              /* A text window has not been created yet */
  twinleft = 0;                 /* Set up initial text window to whole screen */
  twinright = textwidth-1;
  twintop = 0;
  twinbottom = textheight-1;
  xtext = ytext = 0;
  if (!basicvars.runflags.outredir) reset_colours();

#ifdef TARGET_MINGW
  {    /* For a local scope */
    COORD       newsize;
    SMALL_RECT  newrect;

    /* Now the mode is defined, adjust the console window to match */
    newrect.Left = newrect.Top = 0;
    newrect.Bottom = twinbottom;
    newrect.Right = twinright;
    SetConsoleWindowInfo(GetStdHandle(STD_OUTPUT_HANDLE), TRUE, &newrect);

    newsize.X = twinright + 1;
    newsize.Y = twinbottom + 1;
    SetConsoleScreenBufferSize(GetStdHandle(STD_OUTPUT_HANDLE), newsize);

    /* When running interactively change the console title bar too */
    if (!basicvars.runflags.loadngo) SetConsoleTitle(TEXT("Brandy"));
  }
#endif
}

/*
** 'emulate_mode' deals with the Basic 'MODE' command when the parameter
** is a number. This is the version of the function used with non-graphics
** versions of the program. Support for screen modes is very limited under
** these conditions. All that is done is that the number of colours and
** the screen width in characters are changed to values appropriate for
** that RISC OS mode
*/
void emulate_mode(int32 mode) {
  if (!!basicvars.runflags.outredir) nogo(); /* Cannot change screen mode here */
  setup_mode(mode);
  textcolor(text_physforecol);
  textbackground(text_physbackcol);
  reset_screen();
  clrscr();
}

/*
 * emulate_newmode - Change the screen mode using specific mode
 * parameters for the screen size and so on. This is for the new
 * form of the MODE statement
 */
void emulate_newmode(int32 xres, int32 yres, int32 bpp, int32 rate) {
  int32 coldepth, n;
  if (xres==0 || yres==0 || rate==0 || bpp==0) error(ERR_BADMODE);
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
** RISC OS screen modes
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
}

/*
** 'emulate_modefn' emulates the Basic function 'MODE'
*/
int32 emulate_modefn(void) {
  return screenmode;
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
  emulate_vdu(VDU_COMMAND);             /* Use VDU 23,17 */
  emulate_vdu(17);
  emulate_vdu(action);  /* Says which colour to modify */
  if (tint<=MAXTINT) tint = tint<<TINTSHIFT;    /* Assume value is in the wrong place */
  emulate_vdu(tint);
  for (n=1; n<=7; n++) emulate_vdu(0);
}

/*
** 'emulate_gcol' deals with both forms of the Basic 'GCOL' statement,
** where is it used to either set the graphics colour or to define
** how the VDU drivers carry out graphics operations.
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
** background colour to the colour number 'colnum'
*/
void emulate_gcolnum(int32 action, int32 background, int32 colnum) {
  error(ERR_NOGRAPHICS);
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
  emulate_vdu(physcolour);      /* Set logical logical colour to given physical colour */
  emulate_vdu(0);
  emulate_vdu(0);
  emulate_vdu(0);
}

/*
** 'emulate_setcolour' handles the Basic 'COLOUR <red>,<green>,<blue>'
** statement
*/
void emulate_setcolour(int32 background, int32 red, int32 green, int32 blue) {
  if (basicvars.runflags.flag_cosmetic) error(ERR_UNSUPPORTED);
}

/*
** emulate_setcolnum - Called to set the text forground or
** background colour to the colour number 'colnum'
*/
void emulate_setcolnum(int32 background, int32 colnum) {
  if (background) colnum += 128;
  emulate_vdu(17);
  emulate_vdu(colnum);
}

/*
** 'emulate_defcolour' handles the Basic 'COLOUR <colour>,<red>,<green>,<blue>'
** statement
*/
void emulate_defcolour(int32 colour, int32 red, int32 green, int32 blue) {
  emulate_vdu(VDU_LOGCOL);
  emulate_vdu(colour);
  emulate_vdu(16);      /* Set both flash palettes for logical colour to given colour */
  emulate_vdu(red);
  emulate_vdu(green);
  emulate_vdu(blue);
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
  emulate_plot(DRAW_SOLIDLINE+DRAW_RELATIVE, x, y);
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
** 'check_stdout' checks if output will be written to a screen
** or not. It does this by trying to read the terminal attributes
** of stdout. If the call works then output is assumed to be going
** to a terminal. If it fails then it is assumed to be directed at
** a file or some other device. This controls whether or not the
** RISC OS VDU commands are supported
*/
static void check_stdout(void) {
#ifdef TARGET_UNIX
  struct termios parameters;
  int errcode, screen;
  screen = fileno(stdout);
  errcode = tcgetattr(screen, &parameters);
  basicvars.runflags.outredir = errcode!=0;
#else
  basicvars.runflags.outredir = FALSE;
#endif
}


#ifdef USE_ANSI
/*
** 'find_screensize' determines the real size of the screen. Under NetBSD
** and Linux it is possible to determine this using an ioctl call but
** anywhere else default values are used
** -- ANSI --
*/
static void find_screensize(void) {
#if defined(TARGET_LINUX) | defined(TARGET_NETBSD) | defined(TARGET_FREEBSD)\
 | defined(TARGET_OPENBSD) | defined(TARGET_GNUKFREEBSD)
  struct winsize sizes;
  int rc;
  if (!basicvars.runflags.outredir)
    rc = ioctl(fileno(stdin), TIOCGWINSZ, &sizes);
  else {
    rc = -1;
  }
  if (rc<0) {   /* ioctl() called failed or output is not to screen */
    realwidth = SCRWIDTH;
    realheight = SCRHEIGHT;
  }
  else {
    realwidth = sizes.ws_col;
    realheight = sizes.ws_row;
  }
#else
  realwidth = SCRWIDTH;
  realheight = SCRHEIGHT;
#endif
}

#else

/*
** 'find_screensize' determines the real size of the DOS screen.
** The conio version of this fills in the correct details
** -- conio --
*/
static void find_screensize(void) {
#ifdef TARGET_MINGW
  CONSOLE_SCREEN_BUFFER_INFO    screen;

  GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &screen);
  realwidth = screen.srWindow.Right - screen.srWindow.Left + 1;
  realheight = screen.srWindow.Bottom - screen.srWindow.Top + 1;
#else
  struct text_info screen;
  gettextinfo(&screen);
  realwidth = screen.screenwidth;
  realheight = screen.screenheight;
#endif
}
#endif

/*
** 'init_screen' is called to initialise the RISC OS VDU driver
** emulation code for the versions of this program that do not run
** under RISC OS. It returns 'TRUE' if initialisation was okay or
** 'FALSE' if it failed (in which case it is not safe for the
** interpreter to run)
*/
boolean init_screen(void) {
  int mode;
  check_stdout();
  find_screensize();
  /* Set initial screen mode according to the screen size */
  if (realwidth>SCRWIDTH || realheight>SCRHEIGHT)       /* Larger screen mode */
    mode = USERMODE;
  else {        /* Use mode 46 - 80 columns by 25 lines by 16 colours */
    mode = 46;
  }
  vdunext = 0;
  vduneeded = 0;
  enable_print = FALSE;
  setup_mode(mode);
  find_cursor();
 
  return TRUE;
}

/*
** 'end_screen' is called to tidy up the VDU emulation at the end
** of the run
*/
void end_screen(void) {
  if (textwin) reset_screen();
}
