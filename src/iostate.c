/*
** This file is part of the Matrix Brandy Basic VI Interpreter.
** Copyright (C) 2000-2014 David Daniels
** Copyright (C) 2018-2024 Michael McConnell and contributors
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
**      This file contains the Basic I/O statements
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
#include "mos.h"
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

  DEBUGFUNCMSGIN;
  count = eval_intfactor();
  if (count > 0) {
    count = count & BYTEMASK;   /* Basic V/VI only uses the low-order byte of the value */
    basicvars.printcount+=count;
#if !defined(USE_SDL) && !defined(TARGET_RISCOS)
    echo_off();
#endif
    while (count > 0) {
      emulate_vdu(' ');
      count--;
    }
#if !defined(USE_SDL) && !defined(TARGET_RISCOS)
    echo_on();
#endif
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_tab' deals with the Basic 'TAB' function
** The function returns a pointer to the character after the ')'
** at the end of the function call
*/
static void fn_tab(void) {
  int32 x, y;

  DEBUGFUNCMSGIN;
  x = eval_integer();
  if (*basicvars.current == ')') {      /* 'TAB(x)' form of function */
    if (x > 0) {        /* Nothing happens is 'tab' count is less than 0 */
      x = x & BYTEMASK;
      if (x < basicvars.printcount) {   /* Tab position is to left of current cursor position */
        emulate_newline();
        basicvars.printcount = 0;
      }
      x = x-basicvars.printcount;       /* figure out how many blanks to print */
      basicvars.printcount+=x;
#if !defined(USE_SDL) && !defined(TARGET_RISCOS)
      echo_off();
#endif
      while (x > 0) {   /* Print enough blanks to reach tab position */
        emulate_vdu(' ');
        x--;
      }
#if !defined(USE_SDL) && !defined(TARGET_RISCOS)
      echo_on();
#endif
    }
  }
  else if (*basicvars.current == ',') { /* 'TAB(x,y)' form of function */
    basicvars.current++;
    y = eval_integer();
    if (*basicvars.current != ')') {
      error(ERR_RPMISS);
      return;
    }
    emulate_tab(x, y);
  }
  else {        /* Error - ',' or ')' needed */
    DEBUGFUNCMSGOUT;
    error(ERR_CORPNEXT);
    return;
  }
  basicvars.current++;  /* Skip the ')' */
  DEBUGFUNCMSGOUT;
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
  int64 int64value;
  static float64 fpvalue;

  DEBUGFUNCMSGIN;
  p = tonumber(p, &isint, &intvalue, &int64value, &fpvalue);
  if (p == NIL) return NIL;     /* 'tonumber' hit an error - return to caller */
  while (*p != asc_NUL && *p != ',') p++;       /* Find the end of the field */
  if (*p == ',') p++;           /* Move to start of next field */
  switch (destination.typeinfo) {
  case VAR_INTWORD:     /* Normal integer variable */
    *destination.address.intaddr = isint ? intvalue : TOINT(fpvalue);
    break;
  case VAR_UINT8:       /* unsigned 8-bit integer variable */
    *destination.address.uint8addr = isint ? intvalue : TOINT(fpvalue);
    break;
  case VAR_INTLONG:     /* 64-bit integer variable */
    *destination.address.int64addr = isint ? int64value : TOINT64(fpvalue);
    break;
  case VAR_FLOAT:       /* Normal floating point variable */
    *destination.address.floataddr = isint ? TOFLOAT(intvalue) : fpvalue;
    break;
  case VAR_INTBYTEPTR:  /* Indirect reference to byte-sized integer */
    basicvars.memory[destination.address.offset] = isint ? intvalue : TOINT(fpvalue);
    break;
  case VAR_INTWORDPTR:  /* Indirect reference to word-sized integer */
    store_integer(destination.address.offset, isint ? intvalue : TOINT(fpvalue));
    break;
  case VAR_FLOATPTR:            /* Indirect reference to floating point value */
    store_float(destination.address.offset, isint ? TOFLOAT(intvalue) : fpvalue);
    break;
  }
  DEBUGFUNCMSGOUT;
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
  int32 index;

  DEBUGFUNCMSGIN;
  index = 0;
  if (inputall) {       /* Want everything up to the end of line */
    while (*p != asc_NUL) {
      if (index == MAXSTRING) {
        error(ERR_STRINGLEN);
        return NULL;
      }
      tempstring[index] = *p;
      index++;
      p++;
    }
  }
  else {        /* Only want text as far as next delimiter */
    p = skip_blanks(p);
    if (*p == '\"') {   /* Want string up to next double quote */
      boolean more;
      p++;
      more = *p != asc_NUL;
      while (more) {
        if (*p == '\"') {       /* Found a '"'. See if it is followed by another one */
          p++;
          more = *p == '\"';    /* Continue if '""' found else stop */
        }
        if (more) {
          if (index == MAXSTRING) {
            DEBUGFUNCMSGOUT;
            error(ERR_STRINGLEN);
            return NULL;
          }
          tempstring[index] = *p;
          index++;
          p++;
          if (*p == asc_NUL) {
            DEBUGFUNCMSGOUT;
            error(WARN_QUOTEMISS);
          }
        }
      }
    }
    else {      /* Normal string */
      while (*p != asc_NUL && *p != ',') {
        if (index == MAXSTRING) {
          DEBUGFUNCMSGOUT;
          error(ERR_STRINGLEN);
          return NULL;
        }
        tempstring[index] = *p;
        index++;
        p++;
      }
    }
    while (*p != asc_NUL && *p != ',') p++;
    if (*p == ',') p++;
  }
  if (destination.typeinfo == VAR_STRINGDOL) {  /* Normal string variable */
    free_string(*destination.address.straddr);
    cp = alloc_string(index);
    memmove(cp, tempstring, index);
    destination.address.straddr->stringlen = index;
    destination.address.straddr->stringaddr = cp;
  }
  else {        /* '$<addr>' variety of string */
    tempstring[index] = asc_CR;
    memmove(&basicvars.memory[destination.address.offset], tempstring, index+1);
  }
  DEBUGFUNCMSGOUT;
  return p;
}

/*
** 'read_input' is the function called to deal with both 'INPUT' and
** 'INPUT LINE' statements. 'inputline' is set to 'TRUE' if dealing
** with 'INPUT LINE' (where the value for each variable to be read
** is taken from a new line)
*/
static void read_input(boolean inputline) {
  char *cp, line[INPUTLEN];
  lvalue destination;
  boolean bad;
  int n, length;

  DEBUGFUNCMSGIN;
  do {  /* Loop around prompts and items to read */
    boolean prompted = FALSE;
    byte token;
    while (*basicvars.current == ',' || *basicvars.current == ';') basicvars.current++;
    token = *basicvars.current;
    line [0] = asc_NUL;
/* Deal with prompt */
    while (token == BASTOKEN_STRINGCON || token == BASTOKEN_QSTRINGCON || token == '\'' || token == TYPE_PRINTFN) {
      prompted = TRUE;
      switch(token) {
      case BASTOKEN_STRINGCON:       /* Got a prompt string */
        length =GET_SIZE(basicvars.current+1+OFFSIZE);
        if (length>0) emulate_vdustr(TOSTRING(GET_SRCADDR(basicvars.current)), length);
        basicvars.current = skip_token(basicvars.current);
        break;
      case BASTOKEN_QSTRINGCON:      /* Prompt string with '""' in it */
        cp = TOSTRING(GET_SRCADDR(basicvars.current));
        length = GET_SIZE(basicvars.current+1+OFFSIZE);
        for (n=0; n<length; n++) {
          emulate_vdu(*cp);
          if (*cp == '"') cp++; /* Print only '"' when string contains '""' */
          cp++;
        }
        basicvars.current = skip_token(basicvars.current);
        break;
      case '\'':                /* Got a "'" - Skip to new line */
        emulate_newline();
        basicvars.current++;
        break;
      case TYPE_PRINTFN:        /* 'SPC()' and 'TAB()' */
        switch (*(basicvars.current+1)) {
        case BASTOKEN_SPC:
          basicvars.current+=2;         /* Skip two byte function token */
          fn_spc();
          break;
        case BASTOKEN_TAB:
          basicvars.current+=2;         /* Skip two byte function token */
          fn_tab();
          break;
        default:
          bad_token();
        }
      }
      while (*basicvars.current == ',' || *basicvars.current == ';') {  /* An arbitrary number of these can appear here */
        prompted = FALSE;
        basicvars.current++;
      }
      token = *basicvars.current;
    }
    cp = &line[0];      /* Point at start of input buffer */
                        /* This points at a NUL here */
/* Now go through all the variables listed and attempt to assign values to them */
    while (!ateol[*basicvars.current]
     && *basicvars.current != BASTOKEN_STRINGCON && *basicvars.current != BASTOKEN_QSTRINGCON
     && *basicvars.current != '\'' && *basicvars.current != TYPE_PRINTFN) {
      get_lvalue(&destination);
      if (*cp == asc_NUL) {      /* There be nowt left to read on the line */
        if (!prompted) emulate_vdu('?');
        prompted = FALSE;
        if (!read_line(line, INPUTLEN)) {
          DEBUGFUNCMSGOUT;
          error(ERR_ESCAPE);
          return;
        }
        cp = &line[0];
      }
      switch (destination.typeinfo) {
      case VAR_INTWORD: case VAR_UINT8: case VAR_INTLONG: case VAR_FLOAT:       /* Numeric items */
      case VAR_INTBYTEPTR: case VAR_INTWORDPTR: case VAR_FLOATPTR:
        do {
          cp = input_number(destination, cp);   /* Try to read a number */
          bad = cp == NIL;
          if (bad) {    /* Hit an error - Try again */
            emulate_vdu('?');
            if (!read_line(line, INPUTLEN)) {
              DEBUGFUNCMSGOUT;
              error(ERR_ESCAPE);
            }
            cp = &line[0];
          }
        } while (bad);
        break;
      case VAR_STRINGDOL: case VAR_DOLSTRPTR:
        do {
          cp = input_string(destination, cp, inputline);
          bad = cp == NIL;
          if (bad) {    /* Hit an error - Try again */
            emulate_vdu('?');
            if (!read_line(line, INPUTLEN)) {
              DEBUGFUNCMSGOUT;
              error(ERR_ESCAPE);
              return;
            }
            cp = &line[0];
          }
        } while (bad);
        break;
      default:
        DEBUGFUNCMSGOUT;
        error(ERR_VARNUMSTR);   /* Numeric or string variable required */
        return;
      }
      while (*basicvars.current == ',' || *basicvars.current == ';') basicvars.current++;
      if (inputline) {  /* Signal that another line is required for 'INPUT LINE' */
        line[0] = asc_NUL;
        cp = &line[0];
      }
    }
  } while (!ateol[*basicvars.current]);
  basicvars.printcount = 0;     /* Line will have been ended by a newline */
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_beats' handles the Basic statement 'BEATS'
*/
void exec_beats(void) {
  int32 beats;

  DEBUGFUNCMSGIN;
  basicvars.current++;
  beats = eval_integer();
  check_ateol();
  mos_wrbeat(beats);
  DEBUGFUNCMSGOUT;
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

  DEBUGFUNCMSGIN;
  basicvars.current++;          /* Skip BPUT token */
  if (*basicvars.current != '#') {
    DEBUGFUNCMSGOUT;
    error(ERR_HASHMISS);
    return;
  }
  basicvars.current++;
  handle = eval_intfactor();    /* Get the file handle */
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  do {
    expression();               /* Now fetch the value to be written */
    switch (GET_TOPITEM) {
    case STACK_INT: case STACK_UINT8: case STACK_INT64: case STACK_FLOAT:
      fileio_bput(handle, pop_anynum32());
      break;
    case STACK_STRING: case STACK_STRTEMP:
      stringtype = GET_TOPITEM;
      descriptor = pop_string();
      fileio_bputstr(handle, descriptor.stringaddr, descriptor.stringlen);
/* If string is last item on line, output a newline as well */
      if (ateol[*basicvars.current]) fileio_bput(handle, '\n');
      if (stringtype == STACK_STRTEMP) free_string(descriptor);
      break;
    default:    /* Item is neither a number nor a string */
      DEBUGFUNCMSGOUT;
      error(ERR_VARNUMSTR);
      return;
    }
    if (*basicvars.current == ',')      /* Anything more to come? */
      basicvars.current++;              /* Yes */
    else if (*basicvars.current == ';') {
      basicvars.current++;
      if (ateol[*basicvars.current]) break;     /* Nothing after ';' - End of statement */
    }
    else if (ateol[*basicvars.current])         /* Anything else - Check for end of statement */
      break;
    else {
      DEBUGFUNCMSGOUT;
      error(ERR_SYNTAX);
      return;
    }
  } while (TRUE);
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_circle' deals with the Basic statement 'CIRCLE'
*/
void exec_circle(void) {
  int32 x, y, radius;
  boolean filled;

  DEBUGFUNCMSGIN;
  basicvars.current++;          /* Skip CIRCLE token */
  filled = *basicvars.current == BASTOKEN_FILL;
  if (filled) basicvars.current++;
  x = eval_integer();           /* Get x coordinate of centre */
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  y = eval_integer();           /* Get y coordinate of centre */
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  radius = eval_integer();      /* Get radius of circle */
  check_ateol();
  emulate_circle(x, y, radius, filled);
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_clg' handles the Basic statement 'CLG'
*/
void exec_clg(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;
  check_ateol();
  emulate_vdu(VDU_CLEARGRAPH);
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_close' handles the 'CLOSE' statement.
*/
void exec_close(void) {
  int32 handle;

  DEBUGFUNCMSGIN;
  basicvars.current++;          /* Skip CLOSE token */
  if (*basicvars.current != '#') {
    DEBUGFUNCMSGOUT;
    error(ERR_HASHMISS);
    return;
  }
  basicvars.current++;
  expression(); /* Get the file handle */
  check_ateol();
  handle = pop_anynum32();
  fileio_close(handle);
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_cls' clears the screen. How it does this is an operating
** system-dependent.
*/
void exec_cls(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;
  check_ateol();
  emulate_vdu(VDU_CLEARTEXT);
  basicvars.printcount = 0;
  DEBUGFUNCMSGOUT;
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
  DEBUGFUNCMSGIN;
  if (*basicvars.current == BASTOKEN_OF) {
    basicvars.current++;        /* Skip OF */
    form += 4;
    red = eval_integer();
    if (*basicvars.current == ',') {
      form += 1;
      basicvars.current++;
      green = eval_integer();
      if (*basicvars.current != ',') {
        DEBUGFUNCMSGOUT;
        error(ERR_COMISS);
        return;
      }
      basicvars.current++;
      blue = eval_integer();
    }
  }
  if (*basicvars.current == BASTOKEN_ON) {           /* COLOUR OF ... ON */
    basicvars.current++;
    form += 8;
    backred = eval_integer();
    if (*basicvars.current == ',') {
      form += 2;
      basicvars.current++;
      backgreen = eval_integer();
      if (*basicvars.current != ',') {
        DEBUGFUNCMSGOUT;
        error(ERR_COMISS);
        return;
      }
      basicvars.current++;
      backblue = eval_integer();
    }
  }
  check_ateol();
  if ((form & 4) != 0) {        /* Set foreground colour */
    if ((form & 1) != 0)        /*Set foreground RGB value */
      emulate_setcolour(FALSE, red, green, blue);
    else {
      emulate_setcolnum(FALSE, red);
    }
  }
  if ((form & 8) != 0) {        /* Set background colour */
    if ((form & 2) != 0)        /*Set background RGB value */
      emulate_setcolour(TRUE, backred, backgreen, backblue);
    else {
      emulate_setcolnum(TRUE, backred);
    }
  }
  DEBUGFUNCMSGOUT;
}

/*
** exec_colnum - Handle old style COLOUR statement
*/
static void exec_colnum(void) {
  int32 colour, tint, parm2;

  DEBUGFUNCMSGIN;
  colour = eval_integer();
  switch (*basicvars.current) {
  case BASTOKEN_TINT:                /* Got 'COLOUR ... TINT' */
    basicvars.current++;
    tint = eval_integer();
    check_ateol();
    emulate_colourtint(colour, tint);
    break;
  case ',':     /* Got 'COLOUR <n>,...' */
    basicvars.current++;
    parm2 = eval_integer();     /* Assume 'COLOUR <colour>,<physical colour>' */
    if (*basicvars.current != ',') {
      check_ateol();
      emulate_mapcolour(colour, parm2);
    } else {      /* Have got at least three parameters */
      int32 parm3;
      basicvars.current++;
      parm3 = eval_integer();   /* Assume 'COLOUR <red>,<green>,<blue>' */
      if (*basicvars.current != ',') {
        check_ateol();
        emulate_setcolour(FALSE, colour, parm2, parm3);
      } else {
        int32 parm4;
        basicvars.current++;
        parm4 = eval_integer(); /* Assume 'COLOUR <colour>,<red>,<green>,<blue> */
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
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_colour' deals with the Basic statement 'COLOUR'
*/
void exec_colour(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;
  if (*basicvars.current == BASTOKEN_OF || *basicvars.current == BASTOKEN_ON)
    exec_colofon();
  else {
    exec_colnum();
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_draw' handles the Basic 'DRAW [BY]' statement
*/
void exec_draw(void) {
  int32 x, y;
  int32 plotcode = DRAW_SOLIDLINE+DRAW_ABSOLUTE;

  DEBUGFUNCMSGIN;
  basicvars.current++;
  if (*basicvars.current == BASTOKEN_BY) {
    basicvars.current++;
    plotcode = DRAW_SOLIDLINE+DRAW_RELATIVE;
  }
  x = eval_integer();           /* Get x coordinate of end point */
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  y = eval_integer();           /* Get y coordinate of end point */
  check_ateol();
  emulate_plot(plotcode, x, y);
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_ellipse' is called to deal with the Basic 'ELLIPSE' statement
*/
void exec_ellipse(void) {
  int32 x, y, majorlen, minorlen;
  static float64 angle;
  boolean isfilled;

  DEBUGFUNCMSGIN;
  basicvars.current++;          /* Skip ELLIPSE token */
  isfilled = *basicvars.current == BASTOKEN_FILL;
  if (isfilled) basicvars.current++;
  x = eval_integer();           /* Get x coordinate of centre */
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  y = eval_integer();           /* Get y coordinate of centre */
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  majorlen = eval_integer();    /* Get length of semi-major axis */
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  minorlen = eval_integer();    /* Get length of semi-minor axis */
  if (*basicvars.current == ',') {      /* Get angle at which ellipse is inclined */
    basicvars.current++;
    expression();
    angle = pop_anynumfp();
  }
  else {
    angle = 0;
  }
  check_ateol();
  emulate_ellipse(x, y, majorlen, minorlen, angle, isfilled);
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_envelope' deals with the Basic 'ENVELOPE' statement. Under
** Basic V/VI, this statement calls the appropriate OS_Word call, however
** this call has no effect in RISC OS and appears to be supported
** only for backwards compatibilty with the BBC Micro.
*/
void exec_envelope(void) {
  int32 n;

  DEBUGFUNCMSGIN;
  basicvars.current++;
  for (n=1; n<14; n++) {        /* Do the first 13 parameters */
    (void) eval_integer();
    if (*basicvars.current != ',') {
      DEBUGFUNCMSGOUT;
      error(ERR_COMISS);
      return;
    }
    basicvars.current++;
  }
  (void) eval_integer();
  check_ateol();
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_fill' handles the Basic 'FILL' statement
*/
void exec_fill(void) {
  int32 x, y;

  DEBUGFUNCMSGIN;
  basicvars.current++;  /* Skip the FILL token */
  x = eval_integer();           /* Get x coordinate of start of fill */
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  y = eval_integer();           /* Get y coordinate of start of fill */
  check_ateol();
  emulate_plot(FLOOD_BACKGROUND+DRAW_ABSOLUTE, x, y);
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_fillby' handles the Basic 'FILL BY' statement
*/
void exec_fillby(void) {
  int32 x, y;

  DEBUGFUNCMSGIN;
  basicvars.current++;
  x = eval_integer();           /* Get relative x coordinate of start of fill */
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  y = eval_integer();           /* Get relative y coordinate of start of fill */
  check_ateol();
  emulate_plot(FLOOD_BACKGROUND+DRAW_RELATIVE, x, y);
  DEBUGFUNCMSGOUT;
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
  DEBUGFUNCMSGIN;
  if (*basicvars.current == BASTOKEN_OF) {
    form += 4;
    basicvars.current++;
    red = eval_integer();
    if (*basicvars.current == ',') {    /* Assume OF <red>, <green> */
      basicvars.current++;
      green = eval_integer();
      if (*basicvars.current == ',') {  /* Assume OF <red>, <green>, <blue> */
        basicvars.current++;
        form += 1;      /* Set RGB flag */
        blue = eval_integer();
        if (*basicvars.current == ',') { /* Got OF <action>, <red>, <green>, <blue> */
          basicvars.current++;
          action = red;
          red = green;
          green = blue;
          blue = eval_integer();
        }
      }
      else {    /* Only got two parameters - Assume OF <action>, <colour> */
        action = red;
        red = green;
      }
    }
  }
/* Now look for background colour details */
  if (*basicvars.current == BASTOKEN_ON) {
    form += 8;
    basicvars.current++;
    backred = eval_integer();
    if (*basicvars.current == ',') {    /* Assume OF <red>, <green> */
      basicvars.current++;
      backgreen = eval_integer();
      if (*basicvars.current == ',') {  /* Assume OF <red>, <green>, <blue> */
        basicvars.current++;
        form += 2;      /* Set RGB flag */
        backblue = eval_integer();
        if (*basicvars.current == ',') { /* Got OF <action>, <red>, <green>, <blue> */
          basicvars.current++;
          backact = backred;
          backred = backgreen;
          backgreen = backblue;
          backblue = eval_integer();
        }
      }
      else {    /* Only got two parameters - Assume OF <action>, <colour> */
        backact = backred;
        backred = backgreen;
      }
    }
  }
  check_ateol();
  if ((form & 4) != 0) {        /* Set graphics foreground colour */
    if ((form & 1) != 0)        /* Set foreground RGB colour */
      emulate_gcolrgb(action, FALSE, red, green, blue);
    else {
      emulate_gcolnum(action, FALSE, red);
    }
  }
  if ((form & 8) != 0) {        /* Set graphics background colour */
    if ((form & 2) != 0)        /* Set foreground RGB colour */
      emulate_gcolrgb(backact, TRUE, backred, backgreen, backblue);
    else {
      emulate_gcolnum(backact, TRUE, backred);
    }
  }
  DEBUGFUNCMSGOUT;
}

/*
** exec_gcolnum - Called to handle an old-style GCOL statement:
**   GCOL <action>, <number> TINT <value>
**   GCOL <action>, <red>, <green>, <blue>
** where <action> and TINT are optional
*/
static void exec_gcolnum(void) {
  int32 colour, action, tint, gotrgb, green, blue;

  DEBUGFUNCMSGIN;
  action = 0;
  tint = 0;
  gotrgb = FALSE;
  colour = eval_integer();
  if (*basicvars.current == ',') {
    basicvars.current++;
    action = colour;
    colour = eval_integer();
  }
  if (*basicvars.current == BASTOKEN_TINT) {
    basicvars.current++;
    tint = eval_integer();
  }
  else if (*basicvars.current == ',') { /* > 2 parameters - Got GCOL <red>, <green>, <blue> */
    gotrgb = TRUE;
    basicvars.current++;
    green = eval_integer();
    if (*basicvars.current == ',') {    /* GCOL <action>, <red>, <green>, <blue> */
      basicvars.current++;
      blue = eval_integer();
    }
    else {      /* Only three values supplied. Form is GCOL <red>, <green>, <blue> */
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
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_gcol' deals with all forms of the Basic 'GCOL' statement
*/
void exec_gcol(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;
  if (*basicvars.current == BASTOKEN_OF || *basicvars.current == BASTOKEN_ON)
    exec_gcolofon();
  else {
    exec_gcolnum();
  }
  DEBUGFUNCMSGOUT;
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
  int32 handle, length;
  int64 intvalue;
  float64 floatvalue;
  char *cp;
  boolean isint;
  lvalue destination;

  DEBUGFUNCMSGIN;
  basicvars.current++;  /* Skip '#' token */
  handle = eval_intfactor();    /* Find handle of file */
  if (ateol[*basicvars.current]) {   /* Nothing to do */
    DEBUGFUNCMSGOUT;
    return;
  }
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_SYNTAX);
    return;
  }
  do {  /* Now read the values from the file */
    basicvars.current++;        /* Skip the ',' token */
    get_lvalue(&destination);
    switch (destination.typeinfo & PARMTYPEMASK) {
    case VAR_INTWORD:
      fileio_getnumber(handle, &isint, &intvalue, &floatvalue);
      *destination.address.intaddr = isint ? intvalue : TOINT(floatvalue);
      break;
    case VAR_UINT8:
      fileio_getnumber(handle, &isint, &intvalue, &floatvalue);
      *destination.address.uint8addr = isint ? intvalue : TOINT(floatvalue);
      break;
    case VAR_INTLONG:
      fileio_getnumber(handle, &isint, &intvalue, &floatvalue);
      *destination.address.int64addr = isint ? intvalue : TOINT64(floatvalue);
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
      basicvars.memory[destination.address.offset] = isint ? intvalue : TOINT(floatvalue);
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
      length = fileio_getstring(handle, CAST(&basicvars.memory[destination.address.offset], char *));
      basicvars.memory[destination.address.offset+length] = asc_CR;
      break;
    default:
      DEBUGFUNCMSGOUT;
      error(ERR_VARNUMSTR);
      return;
    }
    if (*basicvars.current != ',') break;
  } while (TRUE);
  check_ateol();
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_input' deals with 'INPUT' statements. At the moment it can
** handle 'INPUT' and 'INPUT LINE' but not 'INPUT#' (code is written
** but untested)
*/
void exec_input(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;          /* Skip INPUT token */
  switch (*basicvars.current) {
  case BASTOKEN_LINE:        /* Got 'INPUT LINE' - Read from keyboard */
    basicvars.current++;        /* Skip LINE token */
    read_input(TRUE);   /* TRUE = handling 'INPUT LINE' */
    break;
  case '#':     /* Got 'INPUT#' - Read from file */
    input_file();
    break;
  default:
    read_input(FALSE);  /* FALSE = handling 'INPUT' */
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_line' deals with the Basic 'LINE' statement. This comes it two
** varieties: 'LINE INPUT' and 'draw a line' LINE graphics command
*/
void exec_line(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;  /* Skip LINE token */
  if (*basicvars.current == BASTOKEN_INPUT) {        /* Got 'LINE INPUT' - Read from keyboard */
    basicvars.current++;        /* Skip INPUT token */
    read_input(TRUE);
  }
  else {        /* Graphics command version of 'LINE' */
    int32 x1, y1, x2, y2;
    x1 = eval_integer();        /* Get first x coordinate */
    if (*basicvars.current != ',') {
      DEBUGFUNCMSGOUT;
      error(ERR_COMISS);
      return;
    }
    basicvars.current++;
    y1 = eval_integer();        /* Get first y coordinate */
    if (*basicvars.current != ',') {
      DEBUGFUNCMSGOUT;
      error(ERR_COMISS);
      return;
    }
    basicvars.current++;
    x2 = eval_integer();        /* Get second x coordinate */
    if (*basicvars.current != ',') {
      DEBUGFUNCMSGOUT;
      error(ERR_COMISS);
      return;
    }
    basicvars.current++;
    y2 = eval_integer();        /* Get second y coordinate */
    check_ateol();
    emulate_plot(DRAW_SOLIDLINE+MOVE_ABSOLUTE, x1, y1);
    emulate_plot(DRAW_SOLIDLINE+DRAW_ABSOLUTE, x2, y2);
  }
  DEBUGFUNCMSGOUT;
}

/*
 * exec_modenum - Called when MODE is followed by a numeric
 * value. Two versions of the MODE statement are supported:
 *   MODE <n>
 *   MODE <x>,<y>,<bpp> [, <rate>]
 */
static void exec_modenum(stackitem itemtype) {
  DEBUGFUNCMSGIN;
  if (*basicvars.current == ',') {
    int32 bpp = 6;              /* 6 bpp - Marks old type RISC OS 256 colour mode */
    int32 rate = -1;            /* Use best rate */
    int32 xres = pop_anynum32();
    int32 yres;
    basicvars.current++;
    yres = eval_integer();      /* Y resolution */
    if (*basicvars.current == ',') {
      basicvars.current++;
      bpp = eval_integer();     /* Bits per pixel */
      if (*basicvars.current == ',') {
        basicvars.current++;
        rate = eval_integer();  /* Frame rate */
      }
    }
    check_ateol();
    emulate_newmode(xres, yres, bpp, rate);
  }
  else {        /* MODE statement with mode number */
    check_ateol();
    emulate_mode(pop_anynum32());
  }
  DEBUGFUNCMSGOUT;
}

/*
 * exec_modestr - Called when the MODE keyword is followed by a
 * string. This is interepreted as a mode descriptor
 */
static void exec_modestr(stackitem itemtype) {
  basicstring descriptor;
  char *cp;

  DEBUGFUNCMSGIN;
  check_ateol();

  descriptor = pop_string();
  if (descriptor.stringlen > 0) memmove(basicvars.stringwork, descriptor.stringaddr, descriptor.stringlen);
  *(basicvars.stringwork+descriptor.stringlen) = asc_NUL;
  if (itemtype == STACK_STRTEMP) free_string(descriptor);
  cp = basicvars.stringwork;
/* Parse the mode descriptor string */
  while (*cp == ' ' || *cp == ',') cp++;
  if (*cp == asc_NUL) return;   /* There is nothing to do */
  if (isdigit(*cp)) {   /* String contains a numeric mode number */
    int32 mode = 0;
    do {
      mode = mode * 10 + *cp - '0';
      cp++;
    } while (isdigit(*cp));
    emulate_mode(mode);
  }
  else {        /* Extract details from mode string */
    int32 xres, yres, colours, greys, xeig, yeig, rate;
    char what;
    xres = yres = 0;            /* Set up default values */
    colours = greys = 0;
    xeig = yeig = 1;
    rate = -1;          /* Use highest frame rate possible */
    do {
      int32 value = 0;
      switch (toupper(*cp)) {
      case 'X': case 'Y': case 'G':     /* X and Y size and number of grey scale levels */
        what = toupper(*cp);
        cp++;
        while (isdigit(*cp)) {
          value = value * 10 + *cp - '0';
          cp++;
        }
        if (value < 1) {
          DEBUGFUNCMSGOUT;
          error(ERR_BADMODESC);
          return;
        }
        if (what == 'X')
          xres = value;
        else if (what == 'Y')
          yres = value;
        else {
          if (colours > 0) {       /* Colour depth already given */
            DEBUGFUNCMSGOUT;
            error(ERR_BADMODESC); 
            return;
          }
          greys = value;
        }
        break;
      case 'C': /* Number of colours */
        if (greys > 0) {    /* Grey scale already specified */
          DEBUGFUNCMSGOUT;
          error(ERR_BADMODESC);
          return;
        }
        cp++;
        while (isdigit(*cp)) {
          colours = colours * 10 + *cp - '0';
          cp++;
        }
        if (colours < 1) {
          DEBUGFUNCMSGOUT;
          error(ERR_BADMODESC);
          return;
        }
        if (toupper(*cp) == 'K') {      /* 32K colours */
          if (colours != 32) {
            DEBUGFUNCMSGOUT;
            error(ERR_BADMODESC);
            return;
          }
          colours = 32 * 1024;
          cp++;
        }
        else if (toupper(*cp) == 'M') { /* 16M colours */
          if (colours != 16) {
            DEBUGFUNCMSGOUT;
            error(ERR_BADMODESC);
            return;
          }
          colours = 16 * 1024 * 1024;
          cp++;
        }
        break;
      case 'F': /* Frame rate */
        cp++;
        if (*cp == '-' && *(cp+1) == '1')       /* Frame rate = -1 = use max available */
          cp+=2;        /* -1 is the default value, so do nothing */
        else {
          rate = 0;
          while (isdigit(*cp)) {
            rate = rate * 10 + *cp - '0';
            cp++;
          }
          if (rate < 1) {
            DEBUGFUNCMSGOUT;
            error(ERR_BADMODESC);
            return;
          }
        }
        break;
      case 'E': /* X and Y eigenvalues */
        cp++;
        if (toupper(*cp) == 'X')
          xeig = *(cp+1) - '0'; /* Allow only one digit for the eigenvalue */
        else if (toupper(*cp) == 'Y')
          yeig = *(cp+1) - '0';
        else {
          DEBUGFUNCMSGOUT;
          error(ERR_BADMODESC);
          return;
        }
        cp+=2;
        break;
      default:
        DEBUGFUNCMSGOUT;
        error(ERR_BADMODESC);
        return;
      }
      while (*cp == ' ' || *cp == ',') cp++;
    } while (*cp != asc_NUL);
    emulate_modestr(xres, yres, colours, greys, xeig, yeig, rate);
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_mode' deals with the Basic 'MODE' statement. It
** handles both mode numbers and mode descriptors.
*/
void exec_mode(void) {
  stackitem itemtype;

  DEBUGFUNCMSGIN;
  basicvars.current++;
  expression();
  itemtype = GET_TOPITEM;
  switch (itemtype) {
  case STACK_INT: case STACK_UINT8: case STACK_INT64: case STACK_FLOAT:
    exec_modenum(itemtype);
    break;
  case STACK_STRING: case STACK_STRTEMP:
    exec_modestr(itemtype);
    break;
  default:
    DEBUGFUNCMSGOUT;
    error(ERR_VARNUMSTR);
    return;
  }
  basicvars.printcount = 0;
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_mouse_on' deals with the 'MOUSE ON' statement, which
** turns on the mouse pointer
*/
static void exec_mouse_on(void) {
  int32 pointer;

  DEBUGFUNCMSGIN;
  basicvars.current++;
  if (!ateol[*basicvars.current])       /* Pointer number specified */
    pointer = eval_integer();
  else {        /* Use default pointer */
    pointer = 0;
  }
  check_ateol();
  mos_mouse_on(pointer);
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_mouse_off' deals with the 'MOUSE OFF' statement, which
** turns off the mouse pointer
*/
static void exec_mouse_off(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;
  check_ateol();
  mos_mouse_off();
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_mouse_to' handles the 'MOUSE TO' statement, which
** moves the mouse pointer to the given position on the screen
*/
static void exec_mouse_to(void) {
  int32 x, y;

  DEBUGFUNCMSGIN;
  basicvars.current++;
  x = eval_integer();
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  y = eval_integer();
  check_ateol();
  mos_mouse_to(x, y);
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_mouse_step' defines the mouse step multiplier, the number of
** screen units by which the mouse pointer is moved for each unit the
** mouse moves. The multiplier can be set to zero
*/
static void exec_mouse_step(void) {
  int32 x, y;

  DEBUGFUNCMSGIN;
  basicvars.current++;
  x = eval_integer();
  if (*basicvars.current == ',') {      /* 'y' multiplier supplied */
    basicvars.current++;
    y = eval_integer();
  }
  else {        /* Use same multiplier for 'x' and 'y' */
    y = x;
  }
  check_ateol();
  mos_mouse_step(x, y);
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_mouse_colour' sets one of the mouse pointer colours
*/
static void exec_mouse_colour(void) {
  int32 colour, red, green, blue;

  DEBUGFUNCMSGIN;
  basicvars.current++;
  colour = eval_integer();
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  red = eval_integer();
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  green = eval_integer();
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  blue = eval_integer();
  check_ateol();
  mos_mouse_colour(colour, red, green, blue);
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_mouse_rectangle' defines the mouse bounding box
*/
static void exec_mouse_rectangle(void) {
  int32 left, bottom, right, top;

  DEBUGFUNCMSGIN;
  basicvars.current++;
  left = eval_integer();
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  bottom = eval_integer();
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  right = eval_integer();
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  top = eval_integer();
  check_ateol();
  mos_mouse_rectangle(left, bottom, right, top);
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_mouse_position' reads the current position of the mouse
*/
static void exec_mouse_position(void) {
  size_t mousevalues[4];
  lvalue destination;

  DEBUGFUNCMSGIN;
  mos_mouse(mousevalues);               /* Note: this code does not check the type of the variable to receive the values */
  get_lvalue(&destination);
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  store_value(destination, mousevalues[0], NOSTRING);   /* Mouse x coordinate */
  basicvars.current++;  /* Skip ',' token */
  get_lvalue(&destination);
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  store_value(destination, mousevalues[1], NOSTRING);   /* Mouse y coordinate */
  basicvars.current++;  /* Skip ',' token */
  get_lvalue(&destination);
  store_value(destination, mousevalues[2], NOSTRING);   /* Mouse button state */
  if (*basicvars.current == ',') {      /* Want timestamp as well */
    basicvars.current++;        /* Skip ',' token */
    get_lvalue(&destination);
    store_value(destination, mousevalues[3], NOSTRING); /* Timestamp */
  }
  check_ateol();
  DEBUGFUNCMSGOUT;
}


/*
** 'exec_mouse' handles the Basic 'MOUSE' statement
*/
void exec_mouse(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;          /* Skip MOUSE token */
  switch (*basicvars.current) {
  case BASTOKEN_ON:  /* MOUSE ON */
    exec_mouse_on();
    break;
  case BASTOKEN_OFF: /* MOUSE OFF */
    exec_mouse_off();
    break;
  case BASTOKEN_TO:  /* MOUSE TO */
    exec_mouse_to();
    break;
  case BASTOKEN_STEP:        /* MOUSE STEP */
    exec_mouse_step();
    break;
  case BASTOKEN_COLOUR:      /* MOUSE COLOUR */
    exec_mouse_colour();
    break;
  case BASTOKEN_RECTANGLE:   /* MOUSE RECTANGLE */
    exec_mouse_rectangle();
    break;
  default:
    exec_mouse_position();
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_move' deals with the Basic 'MOVE [BY]' statement
*/
void exec_move(void) {
  int32 x, y;
  int32 plotcode = DRAW_SOLIDLINE+MOVE_ABSOLUTE;

  DEBUGFUNCMSGIN;
  basicvars.current++;
  if (*basicvars.current == BASTOKEN_BY) {
    basicvars.current++;
    plotcode = DRAW_SOLIDLINE+MOVE_RELATIVE;
  }
  x = eval_integer();           /* Get x coordinate of end point */
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  y = eval_integer();           /* Get y coordinate of end point */
  check_ateol();
  emulate_plot(plotcode, x, y);
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_off' handles the Basic 'OFF' statement. This turns off the
** text cursor
*/
void exec_off(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;
  check_ateol();
  emulate_off();
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_origin' deals with the Basic 'ORIGIN' statement, which
** is used to change the graphics origin
*/
void exec_origin(void) {
  int32 x, y;

  DEBUGFUNCMSGIN;
  basicvars.current++;
  x = eval_integer();           /* Get x coordinate of new origin */
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  y = eval_integer();           /* Get y coordinate of new origin */
  check_ateol();
  emulate_origin(x, y);
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_plot' handles the Basic 'PLOT' statement
*/
void exec_plot(void) {
  int32 code, x, y;

  DEBUGFUNCMSGIN;
  basicvars.current++;
  if (*basicvars.current == BASTOKEN_BY) {
    basicvars.current++;
    code = PLOT_POINT+DRAW_RELATIVE;
    x = eval_integer();           /* Get x coordinate of point */
    if (*basicvars.current != ',') {
      DEBUGFUNCMSGOUT;
      error(ERR_COMISS);
      return;
    }
    basicvars.current++;
    y = eval_integer();           /* Get y coordinate of point */
    check_ateol();
    emulate_plot(code, x, y);
  } else {
    code = eval_integer();        /* Get 'PLOT' code */
    if (*basicvars.current != ',') {
      DEBUGFUNCMSGOUT;
      error(ERR_COMISS);
      return;
    }
    basicvars.current++;
    x = eval_integer();           /* Get x coordinate for 'plot' command */
    if (*basicvars.current != ',') {
      /* Only two parameters, so assume code is 69 and shuffle parameters, as per BBCSDL */
      y = x;
      x = code;
      code = PLOT_POINT+DRAW_ABSOLUTE;
    } else {
      basicvars.current++;
      y = eval_integer();           /* Get y coordinate for 'plot' command */
    }
    check_ateol();
    emulate_plot(code, x, y);
    DEBUGFUNCMSGOUT;
  }
}

/*
** 'exec_point' deals with the Basic 'POINT' statement
*/
void exec_point(void) {
  int32 x, y;
  int32 plotcode = PLOT_POINT+DRAW_ABSOLUTE;

  DEBUGFUNCMSGIN;
  basicvars.current++;          /* Skip POINT token */
  if (*basicvars.current == BASTOKEN_BY) {
    basicvars.current++;
    plotcode = PLOT_POINT+DRAW_RELATIVE;
  }
  if (*basicvars.current == BASTOKEN_TO) {
    basicvars.current++;
    plotcode = -1;
  }
  x = eval_integer();           /* Get x coordinate of point */
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  y = eval_integer();           /* Get y coordinate of point */
  check_ateol();
  if (plotcode == -1) {
    emulate_pointto(x, y);
  } else {
    emulate_plot(plotcode, x, y);
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'print_screen deals with the Basic 'PRINT' statement when output is
** to the screen. This code still needs some improvement
*
** Regarding the behaviour of the precision field being set to 0,
** Acorn's docs are woeful in this area and indicate 0 is not legal, but
** the Acorn 6502 and ARM sources all clearly handle that particular
** value and show that unless FORMAT_F is in play then the precision is
** set to the maximum number of digits supported by that build:
** BASIC I: 9, II-IV: 10, BASIC V 10 or (since 2017) 11, BASIC VI: 17
** Matrix Brandy follows this behaviour for BASIC VI.
*/
static void print_screen(void) {
  stackitem resultype;
  boolean hex, rightjust, newline;
  int32 format, formattype, fieldwidth, numdigits, size;
  int32 eoff;
  char *leftfmt, *rightfmt;
  char *bufptr, *crptr;

  DEBUGFUNCMSGIN;
  hex = FALSE;
  rightjust = TRUE;
  newline = TRUE;
  format = basicvars.staticvars[ATPERCENT].varentry.varinteger;
  fieldwidth = format & BYTEMASK;
  numdigits  = (format>>BYTESHIFT) & BYTEMASK;
  formattype = (format>>2*BYTESHIFT) & 0x03;
  /* Matrix Brandy extension - bits 5 and 6 of format byte set the padding size in E format */
  eoff = (((format>>2*BYTESHIFT) & 0x30) >> 4) + 4;
  if (numdigits > 19 ) numdigits = 19; /* Maximum meaningful length */
  switch (formattype) { /* Determine format of floating point values */
  case FORMAT_E:
    if (numdigits == 0) numdigits = DEFDIGITS;  /* Use default of 17 digits if value is 0 */
    leftfmt = "%.*E"; rightfmt = "%*.*E";
    if (numdigits > 1) numdigits--;
    break;
  case FORMAT_F:
    leftfmt = "%.*F"; rightfmt = "%*.*F";
    break;
  default:      /* Assume anything else will be general format */
    if (numdigits == 0) numdigits = DEFDIGITS;  /* Use default of 17 digits if value is 0 */
    leftfmt = "%.*G"; rightfmt = "%*.*G";
    break;
  }
  while (!ateol[*basicvars.current]) {
    newline = TRUE;
    while (*basicvars.current == '~' || *basicvars.current == ',' || *basicvars.current == ';'
     || *basicvars.current == '\'' || *basicvars.current == TYPE_PRINTFN) {
      if (*basicvars.current == TYPE_PRINTFN) { /* Have to use an 'if' here as LCC generate bad code for a switch */
        newline = TRUE;
        if (*(basicvars.current+1) == BASTOKEN_TAB) {
          basicvars.current+=2;         /* Skip two byte function token */
          fn_tab();
        }
        else if (*(basicvars.current+1) == BASTOKEN_SPC) {
          basicvars.current+=2;         /* Skip two byte function token */
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
          size = (fieldwidth ? basicvars.printcount%fieldwidth : 0);    /* Tab to next multiple of <fieldwidth> chars */
          if (size != 0) {      /* Already at tab position */
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
          newline = FALSE;      /* If ';' is last item on line do not skip to a new line */
          basicvars.current++;
          break;
        case '\'':
          hex = FALSE;
          emulate_newline();
          basicvars.printcount = 0;
          basicvars.current++;
          break;
        default:
          DEBUGFUNCMSGOUT;
          error(ERR_BROKEN, __LINE__, "iostate");
          return;
        }
      }
    }
    if (ateol[*basicvars.current]) break;
    newline = TRUE;
    expression();
    resultype = GET_TOPITEM;
    switch (resultype) {
    case STACK_INT: case STACK_UINT8: case STACK_INT64: case STACK_FLOAT:
      if (rightjust) {  /* Value is printed right justified */
        if (hex) {
          if (matrixflags.hex64)
            size = snprintf(basicvars.stringwork, MAXSTRING, "%*llX", fieldwidth, pop_anynum64());
          else
            size = snprintf(basicvars.stringwork, MAXSTRING, "%*X", fieldwidth, pop_anynum32());
        } else {
          if (resultype == STACK_FLOAT || formattype == FORMAT_E || formattype == FORMAT_F) {
            size = snprintf(basicvars.stringwork, MAXSTRING, rightfmt, fieldwidth, numdigits, pop_anynumfp());
          } else { /* One of the integer types */
            int64 fromstack=pop_anynum64();
            size = snprintf(basicvars.stringwork, MAXSTRING, "%lld", fromstack);
            if (size > numdigits)
              size = snprintf(basicvars.stringwork, MAXSTRING, rightfmt, fieldwidth, numdigits, TOFLOAT(fromstack));
            else
              size = snprintf(basicvars.stringwork, MAXSTRING, "%*lld", fieldwidth, fromstack);
          }
        }
      } 
      else {    /* Left justify the value */
        if (hex)
          if (matrixflags.hex64)
            size = snprintf(basicvars.stringwork, MAXSTRING, "%llX", pop_anynum64());
          else
            size = snprintf(basicvars.stringwork, MAXSTRING, "%X", pop_anynum32());
        else {
          if (resultype == STACK_FLOAT || formattype == FORMAT_E || formattype == FORMAT_F)
            size = snprintf(basicvars.stringwork, MAXSTRING, leftfmt, numdigits, pop_anynumfp());
          else {
            int64 fromstack=pop_anynum64();
            size = snprintf(basicvars.stringwork, MAXSTRING, "%lld", fromstack);
            if (size > numdigits)
              size = snprintf(basicvars.stringwork, MAXSTRING, rightfmt, fieldwidth, numdigits, TOFLOAT(fromstack));
          }
        }
      }
      if (format & COMMADPT) decimaltocomma(basicvars.stringwork, size);
      /* Hack to mangle the exponent format to BBC-style rather than C-style */
      bufptr = strchr(basicvars.stringwork,'E');
      if(!hex && bufptr) {
        bufptr++;
        if (*bufptr == '+') {
          if (rightjust && (size <= fieldwidth)) {
            memmove(basicvars.stringwork+1, basicvars.stringwork, (bufptr-basicvars.stringwork));
            basicvars.stringwork[0]=' ';
          } else {
            memmove(bufptr, bufptr+1, size);
            size--;
          }
        } else {
          if (!rightjust || (size > fieldwidth)) bufptr++;
        }
        if (rightjust && (size <= fieldwidth) && basicvars.stringwork[size-eoff] != 'E') bufptr++;
        while (*bufptr == '0' && *(bufptr+1) != '\0') {
          if (rightjust && (size <= fieldwidth)) {
          memmove(basicvars.stringwork+1, basicvars.stringwork, (bufptr-basicvars.stringwork));
          basicvars.stringwork[0]=' ';
          bufptr++;
          } else {
            memmove(bufptr, bufptr+1, size);
            size--;
          }
        }
        /* Now, let's sort out the padding malarkey if right-justifying*/
        if (rightjust && formattype == FORMAT_E) {
          while (basicvars.stringwork[0] == ' ' && basicvars.stringwork[size-eoff] != 'E' && basicvars.stringwork[size-eoff] != '-') {
            memmove(basicvars.stringwork, basicvars.stringwork+1, size);
            basicvars.stringwork[size-1]=' ';
          }
          while (basicvars.stringwork[size-eoff] != 'E' && basicvars.stringwork[size-eoff] != '-') {
            basicvars.stringwork[size]=' ';
            basicvars.stringwork[size+1]='\0';
            size++;
          }
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
        crptr=strrchr(descriptor.stringaddr, '\r');
        if (crptr) {
          basicvars.printcount+=(descriptor.stringlen - 1 - (crptr-descriptor.stringaddr));
        } else {
          basicvars.printcount+=descriptor.stringlen;
        }
      }
      if (resultype == STACK_STRTEMP) free_string(descriptor);
      break;
    }
    default:
      DEBUGFUNCMSGOUT;
      error(ERR_VARNUMSTR);
      return;
    }
  }
  if (newline) {
    emulate_newline();
    basicvars.printcount = 0;
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'print_file' is called to deal with the Basic 'PRINT#' statement
*/
static void print_file(void) {
  basicstring descriptor;
  int32 handle;
  boolean more;

  DEBUGFUNCMSGIN;
  basicvars.current++;  /* Skip '#' token */
  handle = eval_intfactor();    /* Find handle of file */
  more = !ateol[*basicvars.current];
  while (more) {
    if (*basicvars.current != ',') {
      DEBUGFUNCMSGOUT;
      error(ERR_SYNTAX);
      return;
    }
    basicvars.current++;
    expression();
    switch (GET_TOPITEM) {
    case STACK_INT:
      fileio_printint(handle, pop_int());
      break;
    case STACK_UINT8:
      fileio_printuint8(handle, pop_uint8());
      break;
    case STACK_INT64:
      fileio_printint64(handle, pop_int64());
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
      DEBUGFUNCMSGOUT;
      error(ERR_VARNUMSTR);
      return;
    }
    more = !ateol[*basicvars.current];
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_print' deals with the Basic 'PRINT' statement
*/
void exec_print(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;
  if (*basicvars.current == '#')
    print_file();
  else {
    print_screen();
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_rectangle' is called to deal with the Basic 'RECTANGLE'
** statement
*/
void exec_rectangle(void) {
  int32 x1, y1, width, height;
  boolean filled;

  DEBUGFUNCMSGIN;
  basicvars.current++;          /* Skip RECTANGLE token */
  filled = *basicvars.current == BASTOKEN_FILL;
  if (filled) basicvars.current++;
  x1 = eval_integer();          /* Get x coordinate of a corner */
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  y1 = eval_integer();          /* Get y coordinate of a corner */
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  width = eval_integer();               /* Get width of rectangle */
  if (*basicvars.current == ',') {      /* Height is specified */
    basicvars.current++;
    height = eval_integer();            /* Get height of rectangle */
  }
  else {        /* Height is not specified - Assume height = width */
    height = width;
  }
  if (*basicvars.current == BASTOKEN_TO) {   /* Got 'RECTANGLE ... TO' form of statement */
    int32 x2, y2;
    basicvars.current++;
    x2 = eval_integer();                /* Get destination x coordinate */
    if (*basicvars.current != ',') {
      DEBUGFUNCMSGOUT;
      error(ERR_COMISS);
      return;
    }
    basicvars.current++;
    y2 = eval_integer();                /* Get destination y coordinate */
    check_ateol();
    emulate_moverect(x1, y1, width, height, x2, y2, filled);
  }
  else {        /* Just draw a rectangle */
    check_ateol();
    emulate_drawrect(x1, y1, width, height, filled);
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_sound' deals with the Basic statement 'SOUND'
*/
void exec_sound(void) {
  int32 channel, amplitude, pitch, duration, delay;

  DEBUGFUNCMSGIN;
  basicvars.current++;          /* Skip sound token */
  switch (*basicvars.current) {
  case BASTOKEN_ON:
    basicvars.current++;
    check_ateol();
    mos_sound_on();
    break;
  case BASTOKEN_OFF:
    basicvars.current++;
    check_ateol();
    mos_sound_off();
    break;
  default:
    delay = -1;
    channel = eval_integer();
    if (*basicvars.current != ',') {
      DEBUGFUNCMSGOUT;
      error(ERR_COMISS);
      return;
    }
    basicvars.current++;
    amplitude = eval_integer();
    if (*basicvars.current != ',') {
      DEBUGFUNCMSGOUT;
      error(ERR_COMISS);
      return;
    }
    basicvars.current++;
    pitch = eval_integer();
    if (*basicvars.current != ',') {
      DEBUGFUNCMSGOUT;
      error(ERR_COMISS);
      return;
    }
    basicvars.current++;
    duration = eval_integer();
    if (*basicvars.current == ',') {
      basicvars.current++;
      delay = eval_integer();
    }
    check_ateol();
    mos_sound(channel, amplitude, pitch, duration, delay);
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_stereo' handles the Basic 'STEREO' statement
*/
void exec_stereo(void) {
  int32 channel, position;

  DEBUGFUNCMSGIN;
  basicvars.current++;
  channel = eval_integer();
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  position = eval_integer();
  check_ateol();
  mos_stereo(channel, position);
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_tempo' deals with the Basic 'TEMPO' statement
*/
void exec_tempo(void) {
  int32 tempo;

  DEBUGFUNCMSGIN;
  basicvars.current++;
  tempo = eval_integer();
  check_ateol();
  mos_wrtempo(tempo);
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_tint' is called to handle the 'TINT' command. The documentation
** is not very clear as to what effect this has.
*/
void exec_tint(void) {
  int32 colour, tint;

  DEBUGFUNCMSGIN;
  basicvars.current++;
  colour = eval_integer();
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  tint = eval_integer();
  check_ateol();
  emulate_tint(colour, tint);
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_vdu' handles the Basic 'VDU' statement
*/
void exec_vdu(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;          /* Skip VDU token */
  do {
    int32 value = eval_integer();
    if (*basicvars.current == ';') {    /* Send value as two bytes */
      emulate_vdu(value);
      emulate_vdu(value>>BYTESHIFT);
      basicvars.current++;
    }
    else {
      emulate_vdu(value);
      if (*basicvars.current == ',')
        basicvars.current++;
      else if (*basicvars.current == '|') {     /* Got a '|' - Send nine nulls */
        int32 n;
        for (n=1; n<=9; n++) emulate_vdu(0);
        basicvars.current++;
      }
    }
  } while (!ateol[*basicvars.current]);
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_voice' handles the Basic statement 'VOICE'
*/
void exec_voice(void) {
  int32 channel;

  DEBUGFUNCMSGIN;
  basicstring name;
  stackitem stringtype;
  basicvars.current++;
  channel = eval_integer();
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  expression();
  check_ateol();
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) {
    DEBUGFUNCMSGOUT;
    error(ERR_TYPESTR);
    return;
  }
  name = pop_string();
  mos_voice(channel, tocstring(name.stringaddr, name.stringlen));
  if (stringtype == STACK_STRTEMP) free_string(name);
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_voices' handles the Basic statement 'VOICES'
*/
void exec_voices(void) {
  int32 count;

  DEBUGFUNCMSGIN;
  basicvars.current++;
  count = eval_integer();
  check_ateol();
  mos_voices(count);
  DEBUGFUNCMSGOUT;
}

/*
** 'exec_width' handles the Basic statement 'WIDTH'
*/
void exec_width(void) {
  int32 width;

  DEBUGFUNCMSGIN;
  basicvars.current++;
  width = eval_integer();
  check_ateol();
  basicvars.printwidth = (width>=0 ? width : 0);
  DEBUGFUNCMSGOUT;
}


#ifndef TARGET_RISCOS
/* little routines for opening a connection to the printer.
** Not used on RISC OS, this will only work on Linux/UNIX
** with CUPS installed, and are a no-op on other platforms.
 */
void open_printer(void) {
  DEBUGFUNCMSGIN;
#ifdef TARGET_UNIX
  matrixflags.printer = popen("lpr -o document-format='text/plain'","w");
  if (!matrixflags.printer) error(ERR_PRINTER);
#endif
  DEBUGFUNCMSGOUT;
}

void close_printer(void) {
  DEBUGFUNCMSGIN;
#ifdef TARGET_UNIX
  if (matrixflags.printer) pclose(matrixflags.printer);
  matrixflags.printer = NULL;
#endif
  DEBUGFUNCMSGOUT;
}

/* Only called when we have the handle.
** Send the character to the stream if not the ignored character.
 */
void printout_character(int32 ch) {
  DEBUGFUNCMSGIN;
#ifdef TARGET_UNIX
  if (ch == matrixflags.printer_ignore) return;
  fputc(ch, matrixflags.printer);
#endif
  DEBUGFUNCMSGOUT;
}
#endif /* ! TARGET_RISCOS */
