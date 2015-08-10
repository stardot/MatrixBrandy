/*
** This file is part of the Brandy Basic V Interpreter.
** Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005 David Daniels
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
** Fix for format problem in print_screen() was supplied by
** Mark de Wilde
**
**	This file contains the Basic I/O statements
*/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "common.h"
#include "target.h"
#include "basicdefs.h"
#include "tokens.h"
#include "stack.h"
#include "strings.h"
#include "errors.h"
#include "miscprocs.h"
#include "evaluate.h"
#include "convert.h"
#include "emulate.h"
#include "fileio.h"
#include "screen.h"
#include "lvalue.h"
#include "statement.h"
#include "iostate.h"

/* #define DEBUG */

/*
** 'fn_spc' emulates the 'SPC' function. It leaves 'current' pointing
** at the character after the function's  operand
*/
static void fn_spc(void) {
  int32 count;
  count = eval_intfactor();
  if (count > 0) {
    count = count & BYTEMASK;	/* Basic V only uses the low-order byte of the value */
    basicvars.printcount+=count;
    echo_off();
    while (count > 0) {
      emulate_vdu(' ');
      count--;
    }
    echo_on();
  }
}

/*
** 'fn_tab' deals with the Basic 'TAB' function
** The function returns a pointer to the character after the ')'
** at the end of the function call
*/
static void fn_tab(void) {
  int32 x, y;
  x = eval_integer();
  if (*basicvars.current == ')') {	/* 'TAB(x)' form of function */
    if (x > 0) {	/* Nothing happens is 'tab' count is less than 0 */
      x = x & BYTEMASK;
      if (x < basicvars.printcount) {	/* Tab position is to left of current cursor position */
        emulate_newline();
        basicvars.printcount = 0;
      }
      x = x-basicvars.printcount;	/* figure out how many blanks to print */
      basicvars.printcount+=x;
      echo_off();
      while (x > 0) {	/* Print enough blanks to reach tab position */
        emulate_vdu(' ');
        x--;
      }
      echo_on();
    }
  }
  else if (*basicvars.current == ',') {	/* 'TAB(x,y)' form of function */
    basicvars.current++;
    y = eval_integer();
    if (*basicvars.current != ')') error(ERR_RPMISS);
    emulate_tab(x, y);
  }
  else {	/* Error - ',' or ')' needed */
    error(ERR_CORPNEXT);
  }
  basicvars.current++;	/* Skip the ')' */
}

/*
** 'input_number' reads a number from the string starting at 'p'
** and stores it at the location given by 'destination'. It
** returns a pointer to the start of the next field or NIL if an
** error occured (to allow the calling function to try again)
*/
static char *input_number(lvalue destination, char *p) {
  boolean isint;
  int32 intvalue;
  static float64 fpvalue;
  p = tonumber(p, &isint, &intvalue, &fpvalue);
  if (p == NIL) return NIL;	/* 'tonumber' hit an error - return to caller */
  while (*p != NUL && *p != ',') p++;	/* Find the end of the field */
  if (*p == ',') p++;		/* Move to start of next field */
  switch (destination.typeinfo) {
  case VAR_INTWORD:	/* Normal integer variable */
    *destination.address.intaddr = isint ? intvalue : TOINT(fpvalue);
    break;
  case VAR_FLOAT:	/* Normal floating point variable */
    *destination.address.floataddr = isint ? TOFLOAT(intvalue) : fpvalue;
    break;
  case VAR_INTBYTEPTR:	/* Indirect reference to byte-sized integer */
    check_write(destination.address.offset, sizeof(byte));
    basicvars.offbase[destination.address.offset] = isint ? intvalue : TOINT(fpvalue);
    break;
  case VAR_INTWORDPTR:	/* Indirect reference to word-sized integer */
    store_integer(destination.address.offset, isint ? intvalue : TOINT(fpvalue));
    break;
  case VAR_FLOATPTR:		/* Indirect reference to floating point value */
    store_float(destination.address.offset, isint ? TOFLOAT(intvalue) : fpvalue);
    break;
  }
  return p;
}

/*
** 'input_string' reads a character string from the line starting
** at 'p' and storing it at the location given by 'dest'. It returns
** a pointer to the start of the next field or NIL if an error is
** detected. IF 'inputall' is TRUE then all of the line from 'p'
** to the terminating null, including preceding and trailing blanks,
** is used (this is used for 'INPUT LINE')
** Note that the string buffer only has to be large enough to hold
** the number of characters that can be fitted on the command line.
*/
static char *input_string(lvalue destination, char *p, boolean inputall) {
  char *cp, tempstring[INPUTLEN+1];
  boolean more;
  int32 index;
  index = 0;
  if (inputall) {	/* Want everything up to the end of line */
    while (*p != NUL) {
      if (index == MAXSTRING) error(ERR_STRINGLEN);
      tempstring[index] = *p;
      index++;
      p++;
    }
  }
  else {	/* Only want text as far as next delimiter */
    p = skip_blanks(p);
    if (*p == '\"') {	/* Want string up to next double quote */
      p++;
      more = *p != NUL;
      while (more) {
        if (*p == '\"') {	/* Found a '"'. See if it is followed by another one */
          p++;
          more = *p == '\"';	/* Continue if '""' found else stop */
        }
        if (more) {
          if (index == MAXSTRING) error(ERR_STRINGLEN);
          tempstring[index] = *p;
          index++;
          p++;
          if (*p == NUL) error(WARN_QUOTEMISS);
        }
      }
    }
    else {	/* Normal string */
      while (*p != NUL && *p != ',') {
        if (index == MAXSTRING) error(ERR_STRINGLEN);
        tempstring[index] = *p;
        index++;
        p++;
      }
    }
    while (*p != NUL && *p != ',') p++;
    if (*p == ',') p++;
  }
  if (destination.typeinfo == VAR_STRINGDOL) {	/* Normal string variable */
    free_string(*destination.address.straddr);
    cp = alloc_string(index);
    memmove(cp, tempstring, index);
    destination.address.straddr->stringlen = index;
    destination.address.straddr->stringaddr = cp;
  }
  else {	/* '$<addr>' variety of string */
    check_write(destination.address.offset, index+1);	/* +1 for CR character added to end */
    tempstring[index] = CR;
    memmove(&basicvars.offbase[destination.address.offset], tempstring, index+1);
  }
  return p;
}

/*
** 'read_input' is the function called to deal with both 'INPUT' and
** 'INPUT LINE' statements. 'inputline' is set to 'TRUE' if dealing
** with 'INPUT LINE' (where the value for each variable to be read
** is taken from a new line)
*/
static void read_input(boolean inputline) {
  byte token;
  char *cp, line[INPUTLEN];
  lvalue destination;
  boolean bad, prompted;
  int n, length;
  do {	/* Loop around prompts and items to read */
    while (*basicvars.current == ',' || *basicvars.current == ';') basicvars.current++;
    token = *basicvars.current;
    line [0] = NUL;
    prompted = FALSE;
/* Deal with prompt */
    while (token == TOKEN_STRINGCON || token == TOKEN_QSTRINGCON || token == '\'' || token == TYPE_PRINTFN) {
      prompted = TRUE;
      switch(token) {
      case TOKEN_STRINGCON:	/* Got a prompt string */
        length =GET_SIZE(basicvars.current+1+OFFSIZE);
        if (length>0) emulate_vdustr(TOSTRING(get_srcaddr(basicvars.current)), length);
        basicvars.current = skip_token(basicvars.current);
        break;
      case TOKEN_QSTRINGCON:	/* Prompt string with '""' in it */
        cp = TOSTRING(get_srcaddr(basicvars.current));
        length = GET_SIZE(basicvars.current+1+OFFSIZE);
        for (n=0; n<length; n++) {
          emulate_vdu(*cp);
          if (*cp == '"') cp++;	/* Print only '"' when string contains '""' */
          cp++;
        }
        basicvars.current = skip_token(basicvars.current);
        break;
      case '\'':		/* Got a "'" - Skip to new line */
        emulate_newline();
        basicvars.current++;
        break;
      case TYPE_PRINTFN:	/* 'SPC()' and 'TAB()' */
        switch (*(basicvars.current+1)) {
        case TOKEN_SPC:
          basicvars.current+=2;		/* Skip two byte function token */
          fn_spc();
          break;
        case TOKEN_TAB:
          basicvars.current+=2;		/* Skip two byte function token */
          fn_tab();
          break;
        default:
          bad_token();
        }
      }
      while (*basicvars.current == ',' || *basicvars.current == ';') {	/* An arbitrary number of these can appear here */
        prompted = FALSE;
        basicvars.current++;
      }
      token = *basicvars.current;
    }
    cp = &line[0];	/* Point at start of input buffer */
			/* This points at a NUL here */
/* Now go through all the variables listed and attempt to assign values to them */
    while (!ateol[*basicvars.current]
     && *basicvars.current != TOKEN_STRINGCON && *basicvars.current != TOKEN_QSTRINGCON
     && *basicvars.current != '\'' && *basicvars.current != TYPE_PRINTFN) {
      get_lvalue(&destination);
      if (*cp == NUL) {	 /* There be nowt left to read on the line */
        if (!prompted) emulate_vdu('?');
        prompted = FALSE;
        if (!read_line(line, INPUTLEN)) error(ERR_ESCAPE);
        cp = &line[0];
      }
      switch (destination.typeinfo) {
      case VAR_INTWORD: case VAR_FLOAT: case VAR_INTBYTEPTR:	/* Numeric items */
      case VAR_INTWORDPTR: case VAR_FLOATPTR:
        do {
          cp = input_number(destination, cp);	/* Try to read a number */
          bad = cp == NIL;
          if (bad) {	/* Hit an error - Try again */
            emulate_vdu('?');
            if (!read_line(line, INPUTLEN)) error(ERR_ESCAPE);
            cp = &line[0];
          }
        } while (bad);
        break;
      case VAR_STRINGDOL: case VAR_DOLSTRPTR:
        do {
          cp = input_string(destination, cp, inputline);
          bad = cp == NIL;
          if (bad) {	/* Hit an error - Try again */
            emulate_vdu('?');
            if (!read_line(line, INPUTLEN)) error(ERR_ESCAPE);
            cp = &line[0];
          }
        } while (bad);
        break;
      default:
        error(ERR_VARNUMSTR);	/* Numeric or string variable required */
      }
      while (*basicvars.current == ',' || *basicvars.current == ';') basicvars.current++;
      if (inputline) {	/* Signal that another line is required for 'INPUT LINE' */
        line[0] = NUL;
        cp = &line[0];
      }
    }
  } while (!ateol[*basicvars.current]);
  basicvars.printcount = 0;	/* Line will have been ended by a newline */
}

/*
** 'exec_beats' handles the Basic statement 'BEATS'
*/
void exec_beats(void) {
  int32 beats;
  basicvars.current++;
  beats = eval_integer();
  check_ateol();
  emulate_beats(beats);
}

/*
** 'exec_bput' deals with the 'BPUT' statement
** This is an extended version of the statement that allows a
** number of values to be output at a time
*/
void exec_bput(void) {
  int32 handle;
  stackitem stringtype;
  basicstring descriptor;
  basicvars.current++;		/* Skip BPUT token */
  if (*basicvars.current != '#') error(ERR_HASHMISS);
  basicvars.current++;
  handle = eval_intfactor();	/* Get the file handle */
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  do {
    expression();		/* Now fetch the value to be written */
    switch (GET_TOPITEM) {
    case STACK_INT:
      fileio_bput(handle, pop_int());
      break;
    case STACK_FLOAT:
      fileio_bput(handle, TOINT(pop_float()));
      break;
    case STACK_STRING: case STACK_STRTEMP:
      stringtype = GET_TOPITEM;
      descriptor = pop_string();
      fileio_bputstr(handle, descriptor.stringaddr, descriptor.stringlen);
/* If string is last item on line, output a newline as well */
      if (ateol[*basicvars.current]) fileio_bput(handle, '\n');
      if (stringtype == STACK_STRTEMP) free_string(descriptor);
      break;
    default:	/* Item is neither a number nor a string */
      error(ERR_VARNUMSTR);
    }
    if (*basicvars.current == ',')	/* Anything more to come? */
      basicvars.current++;		/* Yes */
    else if (*basicvars.current == ';') {
      basicvars.current++;
      if (ateol[*basicvars.current]) break;	/* Nothing after ';' - End of statement */
    }
    else if (ateol[*basicvars.current])		/* Anything else - Check for end of statement */
      break;
    else {
      error(ERR_SYNTAX);
    }
  } while (TRUE);
}

/*
** 'exec_circle' deals with the Basic statement 'CIRCLE'
*/
void exec_circle(void) {
  int32 x, y, radius;
  boolean filled;
  basicvars.current++;		/* Skip CIRCLE token */
  filled = *basicvars.current == TOKEN_FILL;
  if (filled) basicvars.current++;
  x = eval_integer();		/* Get x coordinate of centre */
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  y = eval_integer();		/* Get y coordinate of centre */
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  radius = eval_integer();	/* Get radius of circle */
  check_ateol();
  emulate_circle(x, y, radius, filled);
}

/*
** 'exec_clg' handles the Basic statement 'CLG'
*/
void exec_clg(void) {
  basicvars.current++;
  check_ateol();
  emulate_vdu(VDU_CLEARGRAPH);
}

/*
** 'exec_close' handles the 'CLOSE' statement.
*/
void exec_close(void) {
  int32 handle = 0;
  basicvars.current++;		/* Skip CLOSE token */
  if (*basicvars.current != '#') error(ERR_HASHMISS);
  basicvars.current++;
  expression();	/* Get the file handle */
  check_ateol();
  switch (GET_TOPITEM) {
  case STACK_INT:
    handle = pop_int();
    break;
  case STACK_FLOAT:
    handle = TOINT(pop_float());
    break;
  default:
    error(ERR_TYPENUM);
  }
  fileio_close(handle);
}

/*
** 'exec_cls' clears the screen. How it does this is an operating
** system-dependent.
*/
void exec_cls(void) {
  basicvars.current++;
  check_ateol();
  emulate_vdu(VDU_CLEARTEXT);
  basicvars.printcount = 0;
}

/*
** exec_colofon - Handle new style 'COLOUR OF' statement
*/
static void exec_colofon(void) {
  int32 red = 0, green = 0, blue = 0, backred = 0;
  int32 backgreen = 0, backblue = 0, form = 0;
/*
** form says what the statement contains:
** Bit 0: Foreground  0 = colour number, 1 = RGB value
** Bit 1: Background  0 = colour number, 1 = RGB value
** Bit 2: 1 = Change foreground
** Bit 3: 1 = Change background
*/
  if (*basicvars.current == TOKEN_OF) {
    basicvars.current++;	/* Skip OF */
    form += 4;
    red = eval_integer();
    if (*basicvars.current == ',') {
      form += 1;
      basicvars.current++;
      green = eval_integer();
      if (*basicvars.current != ',') error(ERR_COMISS);
      basicvars.current++;
      blue = eval_integer();
    }
  }
  if (*basicvars.current == TOKEN_ON) {		/* COLOUR OF ... ON */
    basicvars.current++;
    form += 8;
    backred = eval_integer();
    if (*basicvars.current == ',') {
      form += 2;
      basicvars.current++;
      backgreen = eval_integer();
      if (*basicvars.current != ',') error(ERR_COMISS);
      basicvars.current++;
      backblue = eval_integer();
    }
  }
  check_ateol();
  if ((form & 4) != 0) {	/* Set foreground colour */
    if ((form & 1) != 0)	/*Set foreground RGB value */
      emulate_setcolour(FALSE, red, green, blue);
    else {
      emulate_setcolnum(FALSE, red);
    }
  }
  if ((form & 8) != 0) {	/* Set background colour */
    if ((form & 2) != 0)	/*Set background RGB value */
      emulate_setcolour(TRUE, backred, backgreen, backblue);
    else {
      emulate_setcolnum(TRUE, backred);
    }
  }
}

/*
** exec_colnum - Handle old style COLOUR statement
*/
static void exec_colnum(void) {
  int32 colour, tint, parm2, parm3, parm4;
  colour = eval_integer();
  switch (*basicvars.current) {
  case TOKEN_TINT:		/* Got 'COLOUR ... TINT' */
    basicvars.current++;
    tint = eval_integer();
    check_ateol();
    emulate_colourtint(colour, tint);
    break;
  case ',':	/* Got 'COLOUR <n>,...' */
    basicvars.current++;
    parm2 = eval_integer();	/* Assume 'COLOUR <colour>,<physical colour>' */
    if (*basicvars.current != ',') {
      check_ateol();
      emulate_mapcolour(colour, parm2);
    }
    else {	/* Have got at least three parameters */
      basicvars.current++;
      parm3 = eval_integer();	/* Assume 'COLOUR <red>,<green>,<blue>' */
      if (*basicvars.current != ',') {
        check_ateol();
        emulate_setcolour(FALSE, colour, parm2, parm3);
      }
      else {
        basicvars.current++;
        parm4 = eval_integer();	/* Assume 'COLOUR <colour>,<red>,<green>,<blue> */
        check_ateol();
        emulate_defcolour(colour, parm2, parm3, parm4);
      }
    }
    break;
  default:
    check_ateol();
    emulate_vdu(VDU_TEXTCOL);
    emulate_vdu(colour);
  }
}

/*
** 'exec_colour' deals with the Basic statement 'COLOUR'
*/
void exec_colour(void) {
  basicvars.current++;
  if (*basicvars.current == TOKEN_OF || *basicvars.current == TOKEN_ON)
    exec_colofon();
  else {
    exec_colnum();
  }
}

/*
** 'exec_draw' handles the Basic 'DRAW' statement
*/
void exec_draw(void) {
  int32 x, y;
  basicvars.current++;
  x = eval_integer();		/* Get x coordinate of end point */
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  y = eval_integer();		/* Get y coordinate of end point */
  check_ateol();
  emulate_draw(x, y);
}

/*
** 'exec_drawby' handles the Basic 'DRAW BY' statement
*/
void exec_drawby(void) {
  int32 x, y;
  basicvars.current++;
  x = eval_integer();		/* Get relative x coordinate of end point */
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  y = eval_integer();		/* Get relative y coordinate of end point */
  check_ateol();
  emulate_drawby(x, y);
}

/*
** 'exec_ellipse' is called to deal with the Basic 'ELLIPSE' statement
*/
void exec_ellipse(void) {
  int32 x, y, majorlen, minorlen;
  static float64 angle;
  boolean isfilled;
  basicvars.current++;		/* Skip ELLIPSE token */
  isfilled = *basicvars.current == TOKEN_FILL;
  if (isfilled) basicvars.current++;
  x = eval_integer();		/* Get x coordinate of centre */
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  y = eval_integer();		/* Get y coordinate of centre */
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  majorlen = eval_integer();	/* Get length of semi-major axis */
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  minorlen = eval_integer();	/* Get length of semi-minor axis */
  if (*basicvars.current == ',') {	/* Get angle at which ellipse is inclined */
    basicvars.current++;
    expression();
    switch (GET_TOPITEM) {
    case STACK_INT:
      angle = TOFLOAT(pop_int());
      break;
    case STACK_FLOAT:
      angle = pop_float();
      break;
    default:
      error(ERR_TYPENUM);
    }
  }
  else {
    angle = 0;
  }
  check_ateol();
  emulate_ellipse(x, y, majorlen, minorlen, angle, isfilled);
}

/*
** 'exec_envelope' deals with the Basic 'ENVELOPE' statement. Under
** Basic V, this statement has no effect and appears to be supported
** only for backwards compatibilty with the BBC Micro
*/
void exec_envelope(void) {
  int32 n;
  basicvars.current++;
  for (n=1; n<14; n++) {	/* Do the first 13 parameters */
    (void) eval_integer();
    if (*basicvars.current != ',') error(ERR_COMISS);
    basicvars.current++;
  }
  (void) eval_integer();
  check_ateol();
}

/*
** 'exec_fill' handles the Basic 'FILL' statement
*/
void exec_fill(void) {
  int32 x, y;
  basicvars.current++;	/* Skip the FILL token */
  x = eval_integer();		/* Get x coordinate of start of fill */
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  y = eval_integer();		/* Get y coordinate of start of fill */
  check_ateol();
  emulate_fill(x, y);
}

/*
** 'exec_fillby' handles the Basic 'FILL BY' statement
*/
void exec_fillby(void) {
  int32 x, y;
  basicvars.current++;
  x = eval_integer();		/* Get relative x coordinate of start of fill */
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  y = eval_integer();		/* Get relative y coordinate of start of fill */
  check_ateol();
  emulate_fillby(x, y);
}

/*
** exec_gcolofon - Handle the GCOL OF ... ON statement:
**   GCOL OF <action>, <colour> ON <action>, <colour>
**   GCOL OF <action>, <red>, <green>, <blue> ON <action>, <red>, <green>, <blue>
** where <action> and either the OF or the ON clauses are optional
*/
static void exec_gcolofon(void) {
  int32 red = 0, green = 0, blue = 0, backact = 0, backred = 0;
  int32 action = 0, backgreen = 0, backblue = 0, form = 0;
/*
** form says what the statement contains:
** Bit 0: Foreground  0 = colour number, 1 = RGB value
** Bit 1: Background  0 = colour number, 1 = RGB value
** Bit 2: 1 = Change foreground
** Bit 3: 1 = Change background
*/
  if (*basicvars.current == TOKEN_OF) {
    form += 4;
    basicvars.current++;
    red = eval_integer();
    if (*basicvars.current == ',') {	/* Assume OF <red>, <green> */
      basicvars.current++;
      green = eval_integer();
      if (*basicvars.current == ',') {	/* Assume OF <red>, <green>, <blue> */
        basicvars.current++;
        form += 1;	/* Set RGB flag */
        blue = eval_integer();
        if (*basicvars.current == ',') { /* Got OF <action>, <red>, <green>, <blue> */
          basicvars.current++;
          action = red;
          red = green;
          green = blue;
          blue = eval_integer();
        }
      }
      else {	/* Only got two parameters - Assume OF <action>, <colour> */
        action = red;
        red = green;
      }
    }
  }
/* Now look for background colour details */
  if (*basicvars.current == TOKEN_ON) {
    form += 8;
    basicvars.current++;
    backred = eval_integer();
    if (*basicvars.current == ',') {	/* Assume OF <red>, <green> */
      basicvars.current++;
      backgreen = eval_integer();
      if (*basicvars.current == ',') {	/* Assume OF <red>, <green>, <blue> */
        basicvars.current++;
        form += 2;	/* Set RGB flag */
        backblue = eval_integer();
        if (*basicvars.current == ',') { /* Got OF <action>, <red>, <green>, <blue> */
          basicvars.current++;
          backact = backred;
          backred = backgreen;
          backgreen = backblue;
          backblue = eval_integer();
        }
      }
      else {	/* Only got two parameters - Assume OF <action>, <colour> */
        backact = backred;
        backred = backgreen;
      }
    }
  }
  check_ateol();
  if ((form & 4) != 0) {	/* Set graphics foreground colour */
    if ((form & 1) != 0)	/* Set foreground RGB colour */
      emulate_gcolrgb(action, FALSE, red, green, blue);
    else {
      emulate_gcolnum(action, FALSE, red);
    }
  }
  if ((form & 8) != 0) {	/* Set graphics background colour */
    if ((form & 2) != 0)	/* Set foreground RGB colour */
      emulate_gcolrgb(backact, TRUE, backred, backgreen, backblue);
    else {
      emulate_gcolnum(backact, TRUE, backred);
    }
  }
}

/*
** exec_gcolnum - Called to handle an old-style GCOL statement:
**   GCOL <action>, <number> TINT <value>
**   GCOL <action>, <red>, <green>, <blue>
** where <action> and TINT are optional
*/
static void exec_gcolnum(void) {
  int32 colour, action, tint, gotrgb, green, blue;
  action = 0;
  tint = 0;
  gotrgb = FALSE;
  colour = eval_integer();
  if (*basicvars.current == ',') {
    basicvars.current++;
    action = colour;
    colour = eval_integer();
  }
  if (*basicvars.current == TOKEN_TINT) {
    basicvars.current++;
    tint = eval_integer();
  }
  else if (*basicvars.current == ',') {	/* > 2 parameters - Got GCOL <red>, <green>, <blue> */
    gotrgb = TRUE;
    basicvars.current++;
    green = eval_integer();
    if (*basicvars.current == ',') {	/* GCOL <action>, <red>, <green>, <blue> */
      basicvars.current++;
      blue = eval_integer();
    }
    else {	/* Only three values supplied. Form is GCOL <red>, <green>, <blue> */
      blue = green;
      green = colour;
      colour = action;
      action = 0;
    }
  }
  check_ateol();
  if (gotrgb)
    emulate_gcolrgb(action, FALSE, colour, green, blue);
  else {
    emulate_gcol(action, colour, tint);
  }
}

/*
** 'exec_gcol' deals with all forms of the Basic 'GCOL' statement
*/
void exec_gcol(void) {
  basicvars.current++;
  if (*basicvars.current == TOKEN_OF || *basicvars.current == TOKEN_ON)
    exec_gcolofon();
  else {
    exec_gcolnum();
  }
}

/*
** 'input_file' is called to deal with an 'INPUT#' statement which is
** used to read binary values from a file. On entry, 'basicvars.current'
** points at the '#' of 'INPUT#'.
**
** This function needs to be revised as the type checking is carried
** out in the 'fileio' module. It should be done here.
*/
static void input_file(void) {
  int32 handle, length, intvalue;
  float64 floatvalue;
  char *cp;
  boolean isint;
  lvalue destination;
  basicvars.current++;	/* Skip '#' token */
  handle = eval_intfactor();	/* Find handle of file */
  if (ateol[*basicvars.current]) return;	/* Nothing to do */
  if (*basicvars.current != ',') error(ERR_SYNTAX);
  do {	/* Now read the values from the file */
    basicvars.current++;	/* Skip the ',' token */
    get_lvalue(&destination);
    switch (destination.typeinfo & PARMTYPEMASK) {
    case VAR_INTWORD:
      fileio_getnumber(handle, &isint, &intvalue, &floatvalue);
      *destination.address.intaddr = isint ? intvalue : TOINT(floatvalue);
      break;
    case VAR_FLOAT:
      fileio_getnumber(handle, &isint, &intvalue, &floatvalue);
      *destination.address.floataddr = isint ? TOFLOAT(intvalue) : floatvalue;
      break;
    case VAR_STRINGDOL:
      free_string(*destination.address.straddr);
      length = fileio_getstring(handle, basicvars.stringwork);
      cp = alloc_string(length);
      if (length>0) memmove(cp, basicvars.stringwork, length);
      destination.address.straddr->stringlen = length;
      destination.address.straddr->stringaddr = cp;
      break;
    case VAR_INTBYTEPTR:
      fileio_getnumber(handle, &isint, &intvalue, &floatvalue);
      check_write(destination.address.offset, sizeof(byte));
      basicvars.offbase[destination.address.offset] = isint ? intvalue : TOINT(floatvalue);
      break;
    case VAR_INTWORDPTR:
      fileio_getnumber(handle, &isint, &intvalue, &floatvalue);
      store_integer(destination.address.offset, isint ? intvalue : TOINT(floatvalue));
      break;
    case VAR_FLOATPTR:
      fileio_getnumber(handle, &isint, &intvalue, &floatvalue);
      store_float(destination.address.offset, isint ? TOFLOAT(intvalue) : floatvalue);
      break;
    case VAR_DOLSTRPTR:
      check_write(destination.address.offset, MAXSTRING);
      length = fileio_getstring(handle, CAST(&basicvars.offbase[destination.address.offset], char *));
      basicvars.offbase[destination.address.offset+length] = CR;
      break;
    default:
      error(ERR_VARNUMSTR);
    }
    if (*basicvars.current != ',') break;
  } while (TRUE);
  check_ateol();
}

/*
** 'exec_input' deals with 'INPUT' statements. At the moment it can
** handle 'INPUT' and 'INPUT LINE' but not 'INPUT#' (code is written
** but untested)
*/
void exec_input(void) {
  basicvars.current++;		/* Skip INPUT token */
  switch (*basicvars.current) {
  case TOKEN_LINE:	/* Got 'INPUT LINE' - Read from keyboard */
    basicvars.current++;	/* Skip LINE token */
    read_input(TRUE);	/* TRUE = handling 'INPUT LINE' */
    break;
  case '#':	/* Got 'INPUT#' - Read from file */
    input_file();
    break;
  default:
    read_input(FALSE);	/* FALSE = handling 'INPUT' */
  }
}

/*
** 'exec_line' deals with the Basic 'LINE' statement. This comes it two
** varieties: 'LINE INPUT' and 'draw a line' LINE graphics command
*/
void exec_line(void) {
  basicvars.current++;	/* Skip LINE token */
  if (*basicvars.current == TOKEN_INPUT) {	/* Got 'LINE INPUT' - Read from keyboard */
    basicvars.current++;	/* Skip INPUT token */
    read_input(TRUE);
  }
  else {	/* Graphics command version of 'LINE' */
    int32 x1, y1, x2, y2;
    x1 = eval_integer();	/* Get first x coordinate */
    if (*basicvars.current != ',') error(ERR_COMISS);
    basicvars.current++;
    y1 = eval_integer();	/* Get first y coordinate */
    if (*basicvars.current != ',') error(ERR_COMISS);
    basicvars.current++;
    x2 = eval_integer();	/* Get second x coordinate */
    if (*basicvars.current != ',') error(ERR_COMISS);
    basicvars.current++;
    y2 = eval_integer();	/* Get second y coordinate */
    check_ateol();
    emulate_line(x1, y1, x2, y2);
  }
}

/*
 * exec_modenum - Called when MODE is followed by a numeric
 * value. Two versions of the MODE statement are supported:
 *   MODE <n>
 *   MODE <x>,<y>,<bpp> [, <rate>]
 */
static void exec_modenum(stackitem itemtype) {
  int xres, yres, bpp, rate;
  rate = -1;		/* Use best rate */
  bpp = 6;		/* 6 bpp - Marks old type RISC OS 256 colour mode */
  if (*basicvars.current == ',') {
    xres = itemtype == STACK_INT ? pop_int() : TOINT(pop_float());
    basicvars.current++;
    yres = eval_integer();	/* Y resolution */
    if (*basicvars.current == ',') {
      basicvars.current++;
      bpp = eval_integer();	/* Bits per pixel */
      if (*basicvars.current == ',') {
        basicvars.current++;
        rate = eval_integer();	/* Frame rate */
      }
    }
    check_ateol();
    emulate_newmode(xres, yres, bpp, rate);
  }
  else {	/* MODE statement with mode number */
    check_ateol();
    if (itemtype == STACK_INT)
      emulate_mode(pop_int());
    else {
      emulate_mode(TOINT(pop_float()));
    }
  }
}

/*
 * exec_modestr - Called when the MODE keyword is followed by a
 * string. This is interepreted as a mode descriptor
 */
static void exec_modestr(stackitem itemtype) {
  basicstring descriptor;
  char *cp;
  check_ateol();

  descriptor = pop_string();
  cp = descriptor.stringaddr;
  if (descriptor.stringlen > 0) memmove(basicvars.stringwork, descriptor.stringaddr, descriptor.stringlen);
  *(basicvars.stringwork+descriptor.stringlen) = NUL;
  if (itemtype == STACK_STRTEMP) free_string(descriptor);
  cp = basicvars.stringwork;
/* Parse the mode descriptor string */
  while (*cp == ' ' || *cp == ',') cp++;
  if (*cp == NUL) return;	/* There is nothing to do */
  if (isdigit(*cp)) {	/* String contains a numeric mode number */
    int32 mode = 0;
    do {
      mode = mode * 10 + *cp - '0';
      cp++;
    } while (isdigit(*cp));
    emulate_mode(mode);
  }
  else {	/* Extract details from mode string */
    int32 xres, yres, colours, greys, xeig, yeig, rate, value;
    char what;
    xres = yres = 0;		/* Set up default values */
    colours = greys = 0;
    xeig = yeig = 1;
    rate = -1;		/* Use highest frame rate possible */
    do {
      value = 0;
      switch (toupper(*cp)) {
      case 'X': case 'Y': case 'G':	/* X and Y size and number of grey scale levels */
        what = toupper(*cp);
        cp++;
        while (isdigit(*cp)) {
          value = value * 10 + *cp - '0';
          cp++;
        }
        if (value < 1) error(ERR_BADMODESC);
        if (what == 'X')
          xres = value;
        else if (what == 'Y')
          yres = value;
        else {
          if (colours > 0) error(ERR_BADMODESC);	/* Colour depth already given */
          greys = value;
        }
        break;
      case 'C':	/* Number of colours */
        if (greys > 0) error(ERR_BADMODESC);	/* Grey scale already specified */
        cp++;
        while (isdigit(*cp)) {
          colours = colours * 10 + *cp - '0';
          cp++;
        }
        if (colours < 1) error(ERR_BADMODESC);
        if (toupper(*cp) == 'K') {	/* 32K colours */
          if (colours != 32) error(ERR_BADMODESC);
          colours = 32 * 1024;
          cp++;
        }
        else if (toupper(*cp) == 'M') {	/* 16M colours */
          if (colours != 16) error(ERR_BADMODESC);
          colours = 16 * 1024 * 1024;
          cp++;
        }
        break;
      case 'F':	/* Frame rate */
        cp++;
        if (*cp == '-' && *(cp+1) == '1')	/* Frame rate = -1 = use max available */
          cp+=2;	/* -1 is the default value, so do nothing */
        else {
          rate = 0;
          while (isdigit(*cp)) {
            rate = rate * 10 + *cp - '0';
            cp++;
          }
          if (rate < 1) error(ERR_BADMODESC);
        }
        break;
      case 'E':	/* X and Y eigenvalues */
        cp++;
        if (toupper(*cp) == 'X')
          xeig = *(cp+1) - '0';	/* Allow only one digit for the eigenvalue */
        else if (toupper(*cp) == 'Y')
          yeig = *(cp+1) - '0';
        else {
          error(ERR_BADMODESC);
        }
        cp+=2;
        break;
      default:
        error(ERR_BADMODESC);
      }
      while (*cp == ' ' || *cp == ',') cp++;
    } while (*cp != NUL);
    emulate_modestr(xres, yres, colours, greys, xeig, yeig, rate);
  }
}

/*
** 'exec_mode' deals with the Basic 'MODE' statement. It
** handles both mode numbers and mode descriptors.
*/
void exec_mode(void) {
  stackitem itemtype;
  basicvars.current++;
  expression();
  itemtype = GET_TOPITEM;
  switch (itemtype) {
  case STACK_INT: case STACK_FLOAT:
    exec_modenum(itemtype);
    break;
  case STACK_STRING: case STACK_STRTEMP:
    exec_modestr(itemtype);
    break;
  default:
    error(ERR_VARNUMSTR);
  }
  basicvars.printcount = 0;
}

/*
** 'exec_mouse_on' deals with the 'MOUSE ON' statement, which
** turns on the mouse pointer
*/
static void exec_mouse_on(void) {
  int32 pointer;
  basicvars.current++;
  if (!ateol[*basicvars.current])	/* Pointer number specified */
    pointer = eval_integer();
  else {	/* Use default pointer */
    pointer = 0;
  }
  check_ateol();
  emulate_mouse_on(pointer);
}

/*
** 'exec_mouse_off' deals with the 'MOUSE OFF' statement, which
** turns off the mouse pointer
*/
static void exec_mouse_off(void) {
  basicvars.current++;
  check_ateol();
  emulate_mouse_off();
}

/*
** 'exec_mouse_to' handles the 'MOUSE TO' statement, which
** moves the mouse pointer to the given position on the screen
*/
static void exec_mouse_to(void) {
  int32 x, y;
  basicvars.current++;
  x = eval_integer();
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  y = eval_integer();
  check_ateol();
  emulate_mouse_to(x, y);
}

/*
** 'exec_mouse_step' defines the mouse step multiplier, the number of
** screen units by which the mouse pointer is moved for each unit the
** mouse moves. The multiplier can be set to zero
*/
static void exec_mouse_step(void) {
  int32 x, y;
  basicvars.current++;
  x = eval_integer();
  if (*basicvars.current == ',') {	/* 'y' multiplier supplied */
    basicvars.current++;
    y = eval_integer();
  }
  else {	/* Use same multiplier for 'x' and 'y' */
    y = x;
  }
  check_ateol();
  emulate_mouse_step(x, y);
}

/*
** 'exec_mouse_colour' sets one of the mouse pointer colours
*/
static void exec_mouse_colour(void) {
  int32 colour, red, green, blue;
  basicvars.current++;
  colour = eval_integer();
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  red = eval_integer();
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  green = eval_integer();
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  blue = eval_integer();
  check_ateol();
  emulate_mouse_colour(colour, red, green, blue);
}

/*
** 'exec_mouse_rectangle' defines the mouse bounding box
*/
static void exec_mouse_rectangle(void) {
  int32 left, bottom, right, top;
  basicvars.current++;
  left = eval_integer();
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  bottom = eval_integer();
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  right = eval_integer();
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  top = eval_integer();
  check_ateol();
  emulate_mouse_rectangle(left, bottom, right, top);
}

/*
** 'exec_mouse_position' reads the current position of the mouse
*/
static void exec_mouse_position(void) {
  int32 mousevalues[4];
  lvalue destination;
  emulate_mouse(mousevalues);		/* Note: this code does not check the type of the variable to receive the values */
  get_lvalue(&destination);
  if (*basicvars.current != ',') error(ERR_COMISS);
  store_value(destination, mousevalues[0]);	/* Mouse x coordinate */
  basicvars.current++;	/* Skip ',' token */
  get_lvalue(&destination);
  if (*basicvars.current != ',') error(ERR_COMISS);
  store_value(destination, mousevalues[1]);	/* Mouse y coordinate */
  basicvars.current++;	/* Skip ',' token */
  get_lvalue(&destination);
  store_value(destination, mousevalues[2]);	/* Mouse button state */
  if (*basicvars.current == ',') {	/* Want timestamp as well */
    basicvars.current++;	/* Skip ',' token */
    get_lvalue(&destination);
    store_value(destination, mousevalues[3]);	/* Timestamp */
  }
  check_ateol();
}


/*
** 'exec_mouse' handles the Basic 'MOUSE' statement
*/
void exec_mouse(void) {
  basicvars.current++;		/* Skip MOUSE token */
  switch (*basicvars.current) {
  case TOKEN_ON:	/* MOUSE ON */
    exec_mouse_on();
    break;
  case TOKEN_OFF:	/* MOUSE OFF */
    exec_mouse_off();
    break;
  case TOKEN_TO:	/* MOUSE TO */
    exec_mouse_to();
    break;
  case TOKEN_STEP:	/* MOUSE STEP */
    exec_mouse_step();
    break;
  case TOKEN_COLOUR:	/* MOUSE COLOUR */
    exec_mouse_colour();
    break;
  case TOKEN_RECTANGLE:	/* MOUSE RECTANGLE */
    exec_mouse_rectangle();
    break;
  default:
    exec_mouse_position();
  }
}

/*
** 'exec_move' deals with the Basic 'MOVE' statement
*/
void exec_move(void) {
  int32 x, y;
  basicvars.current++;
  x = eval_integer();		/* Get x coordinate of end point */
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  y = eval_integer();		/* Get y coordinate of end point */
  check_ateol();
  emulate_move(x, y);
}

/*
** 'exec_moveby' deals with the Basic 'MOVE BY' statement
*/
void exec_moveby(void) {
  int32 x, y;
  basicvars.current++;
  x = eval_integer();		/* Get relative x coordinate of end point */
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  y = eval_integer();		/* Get relative y coordinate of end point */
  check_ateol();
  emulate_moveby(x, y);
}

/*
** 'exec_off' handles the Basic 'OFF' statement. This turns off the
** text cursor
*/
void exec_off(void) {
  basicvars.current++;
  check_ateol();
  emulate_off();
}

/*
** 'exec_origin' deals with the Basic 'ORIGIN' statement, which
** is used to change the graphics origin
*/
void exec_origin(void) {
  int32 x, y;
  basicvars.current++;
  x = eval_integer();		/* Get x coordinate of new origin */
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  y = eval_integer();		/* Get y coordinate of new origin */
  check_ateol();
  emulate_origin(x, y);
}

/*
** 'exec_plot' handles the Basic 'PLOT' statement
*/
void exec_plot(void) {
  int32 code, x, y;
  basicvars.current++;
  code = eval_integer();	/* Get 'PLOT' code */
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  x = eval_integer();		/* Get x coordinate for 'plot' command */
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  y = eval_integer();		/* Get y coordinate for 'plot' command */
  check_ateol();
  emulate_plot(code, x, y);
}

/*
** 'exec_point' deals with the Basic 'POINT' statement
*/
void exec_point(void) {
  int32 x, y;
  basicvars.current++;		/* Skip POINT token */
  x = eval_integer();		/* Get x coordinate of point */
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  y = eval_integer();		/* Get y coordinate of point */
  check_ateol();
  emulate_point(x, y);
}

/*
** 'exec_pointby' deals with the Basic 'POINT BY' statement
*/
void exec_pointby(void) {
  int32 x, y;
  basicvars.current++;
  x = eval_integer();		/* Get x coordinate of point */
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  y = eval_integer();		/* Get y coordinate of point */
  check_ateol();
  emulate_pointby(x, y);
}

/*
** 'exec_pointto' deals with the Basic 'POINT TO' statement
*/
void exec_pointto(void) {
  int32 x, y;
  basicvars.current++;
  x = eval_integer();		/* Get x coordinate of point */
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  y = eval_integer();		/* Get y coordinate of point */
  check_ateol();
  emulate_pointto(x, y);
}

/*
** 'print_screen deals with the Basic 'PRINT' statement when output is
** to the screen. This code still needs some improvement
*/
static void print_screen(void) {
  stackitem resultype;
  boolean hex, rightjust, newline;
  int32 format, fieldwidth, numdigits, size;
  char *leftfmt, *rightfmt;
  hex = FALSE;
  rightjust = TRUE;
  newline = TRUE;
  format = basicvars.staticvars[ATPERCENT].varentry.varinteger;
  if (format == 0) format = STDFORMAT;
  fieldwidth = format & BYTEMASK;
  numdigits = (format>>BYTESHIFT) & BYTEMASK;
  if (numdigits == 0) numdigits = DEFDIGITS;	/* Use default of 10 digits if value is 0 */
  switch ((format>>2*BYTESHIFT) & BYTEMASK) {	/* Determine format of floating point values */
  case FORMAT_E:
    leftfmt = "%.*e"; rightfmt = "%*.*e";
    break;
  case FORMAT_F:
    leftfmt = "%.*f"; rightfmt = "%*.*f";
    break;
  default:	/* Assume anything else will be general format */
    leftfmt = "%.*g"; rightfmt = "%*.*g";
    break;
  }
  while (!ateol[*basicvars.current]) {
    newline = TRUE;
    while (*basicvars.current == '~' || *basicvars.current == ',' || *basicvars.current == ';'
     || *basicvars.current == '\'' || *basicvars.current == TYPE_PRINTFN) {
      if (*basicvars.current == TYPE_PRINTFN) {	/* Have to use an 'if' here as LCC generate bad code for a switch */
        if (*(basicvars.current+1) == TOKEN_TAB) {
          basicvars.current+=2;		/* Skip two byte function token */
          fn_tab();
        }
        else if (*(basicvars.current+1) == TOKEN_SPC) {
          basicvars.current+=2;		/* Skip two byte function token */
          fn_spc();
        }
        else {
          bad_token();
        }
      }
      else {
        switch (*basicvars.current) {
        case '~':
          hex = TRUE;
          basicvars.current++;
          break;
        case ',':
          hex = FALSE;
          rightjust = TRUE;
          size = basicvars.printcount%fieldwidth;	/* Tab to next multiple of <fieldwidth> chars */
          if (size != 0) {	/* Already at tab position */
            do {
              emulate_vdu(' ');
              size++;
              basicvars.printcount++;
            } while (size<fieldwidth);
          }
          basicvars.current++;
          break;
        case ';':
          hex = FALSE;
          rightjust = FALSE;
          newline = FALSE;	/* If ';' is last item on line do not skip to a new line */
          basicvars.current++;
          break;
        case '\'':
          hex = FALSE;
          emulate_newline();
          basicvars.printcount = 0;
          basicvars.current++;
          break;
        default:
          error(ERR_BROKEN, __LINE__, "iostate");
        }
      }
    }
    if (ateol[*basicvars.current]) break;
    newline = TRUE;
    expression();
    resultype = GET_TOPITEM;
    switch (resultype) {
    case STACK_INT:
      if (rightjust) {
        if (hex)
          size = sprintf(basicvars.stringwork, "%*X", fieldwidth, pop_int());
        else {
          size = sprintf(basicvars.stringwork, "%*d", fieldwidth, pop_int());
        }
      }
      else {	/* Left justify the value */
        if (hex)
          size = sprintf(basicvars.stringwork, "%X", pop_int());
        else {
          size = sprintf(basicvars.stringwork, "%d", pop_int());
        }
      }
      emulate_vdustr(basicvars.stringwork, size);
      basicvars.printcount+=size;
      break;
    case STACK_FLOAT:
      if (rightjust) {	/* Value is printed right justified */
        if (hex)
          size = sprintf(basicvars.stringwork, "%*X", fieldwidth, TOINT(pop_float()));
        else {
          size = sprintf(basicvars.stringwork, rightfmt, fieldwidth, numdigits, pop_float());
        }
      }
      else {	/* Left justify the value */
        if (hex)
          size = sprintf(basicvars.stringwork, "%X", TOINT(pop_float()));
        else {
          size = sprintf(basicvars.stringwork, leftfmt, numdigits, pop_float());
        }
      }
      emulate_vdustr(basicvars.stringwork, size);
      basicvars.printcount+=size;
      break;
    case STACK_STRING: case STACK_STRTEMP: {
      basicstring descriptor;
      descriptor = pop_string();
      if (descriptor.stringlen>0) {
        emulate_vdustr(descriptor.stringaddr, descriptor.stringlen);
        basicvars.printcount+=descriptor.stringlen;
      }
      if (resultype == STACK_STRTEMP) free_string(descriptor);
      break;
    }
    default:
      error(ERR_VARNUMSTR);
    }
  }
  if (newline) {
    emulate_newline();
    basicvars.printcount = 0;
  }
}

/*
** 'print_file' is called to deal with the Basic 'PRINT#' statement
*/
static void print_file(void) {
  basicstring descriptor;
  int32 handle;
  boolean more;
  basicvars.current++;	/* Skip '#' token */
  handle = eval_intfactor();	/* Find handle of file */
  more = !ateol[*basicvars.current];
  while (more) {
    if (*basicvars.current != ',') error(ERR_SYNTAX);
    basicvars.current++;
    expression();
    switch (GET_TOPITEM) {
    case STACK_INT:
      fileio_printint(handle, pop_int());
      break;
    case STACK_FLOAT:
      fileio_printfloat(handle, pop_float());
      break;
    case STACK_STRING:
      descriptor = pop_string();
      fileio_printstring(handle, descriptor.stringaddr, descriptor.stringlen);
      break;
    case STACK_STRTEMP:
      descriptor = pop_string();
      fileio_printstring(handle, descriptor.stringaddr, descriptor.stringlen);
      free_string(descriptor);
      break;
    default:
      error(ERR_VARNUMSTR);
    }
    more = !ateol[*basicvars.current];
  }
}

/*
** 'exec_print' deals with the Basic 'PRINT' statement
*/
void exec_print(void) {
  basicvars.current++;
  if (*basicvars.current == '#')
    print_file();
  else {
    print_screen();
  }
}

/*
** 'exec_rectangle' is called to deal with the Basic 'RECTANGLE'
** statement
*/
void exec_rectangle(void) {
  int32 x1, y1, width, height, x2, y2;
  boolean filled;
  basicvars.current++;		/* Skip RECTANGLE token */
  filled = *basicvars.current == TOKEN_FILL;
  if (filled) basicvars.current++;
  x1 = eval_integer();		/* Get x coordinate of a corner */
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  y1 = eval_integer();		/* Get y coordinate of a corner */
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  width = eval_integer();		/* Get width of rectangle */
  if (*basicvars.current == ',') {	/* Height is specified */
    basicvars.current++;
    height = eval_integer();		/* Get height of rectangle */
  }
  else {	/* Height is not specified - Assume height = width */
    height = width;
  }
  if (*basicvars.current == TOKEN_TO) {	/* Got 'RECTANGLE ... TO' form of statement */
    basicvars.current++;
    x2 = eval_integer();		/* Get destination x coordinate */
    if (*basicvars.current != ',') error(ERR_COMISS);
    basicvars.current++;
    y2 = eval_integer();		/* Get destination y coordinate */
    check_ateol();
    emulate_moverect(x1, y1, width, height, x2, y2, filled);
  }
  else {	/* Just draw a rectangle */
    check_ateol();
    emulate_drawrect(x1, y1, width, height, filled);
  }
}

/*
** 'exec_sound' deals with the Basic statement 'SOUND'
*/
void exec_sound(void) {
  int32 channel, amplitude, pitch, duration, delay;
  basicvars.current++;		/* Skip sound token */
  switch (*basicvars.current) {
  case TOKEN_ON:
    basicvars.current++;
    check_ateol();
    emulate_sound_on();
    break;
  case TOKEN_OFF:
    basicvars.current++;
    check_ateol();
    emulate_sound_off();
    break;
  default:
    delay = 0;
    channel = eval_integer();
    if (*basicvars.current != ',') error(ERR_COMISS);
    basicvars.current++;
    amplitude = eval_integer();
    if (*basicvars.current != ',') error(ERR_COMISS);
    basicvars.current++;
    pitch = eval_integer();
    if (*basicvars.current != ',') error(ERR_COMISS);
    basicvars.current++;
    duration = eval_integer();
    if (*basicvars.current == ',') {
      basicvars.current++;
      delay = eval_integer();
    }
    check_ateol();
    emulate_sound(channel, amplitude, pitch, duration, delay);
  }
}

/*
** 'exec_stereo' handles the Basic 'STEREO' statement
*/
void exec_stereo(void) {
  int32 channel, position;
  basicvars.current++;
  channel = eval_integer();
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  position = eval_integer();
  check_ateol();
  emulate_stereo(channel, position);
}

/*
** 'exec_tempo' deals with the Basic 'TEMPO' statement
*/
void exec_tempo(void) {
  int32 tempo;
  basicvars.current++;
  tempo = eval_integer();
  check_ateol();
  emulate_tempo(tempo);
}

/*
** 'exec_tint' is called to handle the 'TINT' command. The documentation
** is not very clear as to what effect this has.
*/
void exec_tint(void) {
  int32 colour, tint;
  basicvars.current++;
  colour = eval_integer();
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  tint = eval_integer();
  check_ateol();
  emulate_tint(colour, tint);
}

/*
** 'exec_vdu' handles the Basic 'VDU' statement
*/
void exec_vdu(void) {
  int32 n, value;
  basicvars.current++;		/* Skip VDU token */
  do {
    value = eval_integer();
    if (*basicvars.current == ';') {	/* Send value as two bytes */
      emulate_vdu(value);
      emulate_vdu(value>>BYTESHIFT);
      basicvars.current++;
    }
    else {
      emulate_vdu(value);
      if (*basicvars.current == ',')
        basicvars.current++;
      else if (*basicvars.current == '|') {	/* Got a '|' - Send nine nulls */
        for (n=1; n<=9; n++) emulate_vdu(0);
        basicvars.current++;
      }
    }
  } while (!ateol[*basicvars.current]);
}

/*
** 'exec_voice' handles the Basic statement 'VOICE'
*/
void exec_voice(void) {
  int32 channel;
  basicstring name;
  stackitem stringtype;
  basicvars.current++;
  channel = eval_integer();
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  expression();
  check_ateol();
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) error(ERR_TYPESTR);
  name = pop_string();
  emulate_voice(channel, tocstring(name.stringaddr, name.stringlen));
  if (stringtype == STACK_STRTEMP) free_string(name);
}

/*
** 'exec_voices' handles the Basic statement 'VOICES'
*/
void exec_voices(void) {
  int32 count;
  basicvars.current++;
  count = eval_integer();
  check_ateol();
  emulate_voices(count);
}

/*
** 'exec_width' handles the Basic statement 'WIDTH'
*/
void exec_width(void) {
  int32 width;
  basicvars.current++;
  width = eval_integer();
  check_ateol();
  basicvars.printwidth = (width>=0 ? width : 0);
}

