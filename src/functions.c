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
**	This file contains all of the built-in Basic functions
*/
/*
** Changed by Crispian Daniels on August 12th 2002.
**	Changed 'fn_rnd' to use a pseudo-random generator equivalent
**	to the BASIC II implementation.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#include "common.h"
#include "target.h"
#include "basicdefs.h"
#include "tokens.h"
#include "variables.h"
#include "strings.h"
#include "convert.h"
#include "stack.h"
#include "errors.h"
#include "evaluate.h"
#include "keyboard.h"
#include "screen.h"
#include "emulate.h"
#include "miscprocs.h"
#include "fileio.h"
#include "functions.h"


/* #define DEBUG */

/*
 * This file contains all of the built-in Basic functions. Most of
 * them are dispatched via exec_function() as they have two byte
 * tokens but some of them, particularly tokens that can be used
 * as either functions or statements such as 'MODE', are called
 * directly from the factor code in evaluate.c. The ones
 * invoked via exec_function() are marked as 'static'. If they
 * are not static they are called from evaluate.c. The value of
 * basicvars.current depends on where the function was called
 * from. If from exec_function() then it points at the byte
 * after the function's token. (This is a two byte value where
 * the second byte is a function number and basicvars.current
 * points at the byte after the function number.) If called from
 * factor() then it points at the function token still. This
 * will always be a one byte token.
 */

#ifndef PI
#define PI 3.14159265358979323846
#endif

#define RADCONV 57.2957795130823229	/* Used when converting degrees -> radians and vice versa */
#define TIMEFORMAT "%a,%d %b %Y.%H:%M:%S"  /* Date format used by 'TIME$' */

#define STRFORMAT 0xA0A			/* Default format used by function STR$ */

static int32 lastrandom;		/* 32-bit pseudo-random number generator value */
static int32 randomoverflow;		/* 1-bit overflow from pseudo-random number generator */
static float64 floatvalue;		/* Temporary for holding floating point values */

/*
** 'bad_token' is called to report a bad token value. This could mean
** two things: either the program has been corrupted or there is a bug
** in the interpreter. At this stage, the latter is more often the
** case.
*/
static void bad_token(void)  {
#ifdef DEBUG
  printf("Bad token value %x at %p\n", *basicvars.current, basicvars.current);
#endif
  error(ERR_BROKEN, __LINE__, "expressions");
}

/*
** 'eval_integer' evaluates a numeric expression where an integer value
** is required, returning the value
*/
int32 eval_integer(void) {
  stackitem numtype;
  expression();
  numtype = GET_TOPITEM;
  if (numtype == STACK_INT) return pop_int();
  if (numtype == STACK_FLOAT) return TOINT(pop_float());
  error(ERR_TYPENUM);
  return 0;	/* Keep Acorn's compiler happy */
}

/*
** 'eval_intfactor' evaluates a numeric factor where an integer is
** required. The function returns the value obtained.
*/
int32 eval_intfactor(void) {
  stackitem numtype;
  (*factor_table[*basicvars.current])();
  numtype = GET_TOPITEM;
  if (numtype == STACK_INT) return pop_int();
  if (numtype == STACK_FLOAT) return TOINT(pop_float());
  error(ERR_TYPENUM);
  return 0;	/* Keep Acorn's compiler happy */
}

/*
** 'fn_himem' pushes the value of HIMEM on to the Basic stack
*/
static void fn_himem(void) {
  push_int(basicvars.himem-basicvars.offbase);
}

/*
** 'fn_ext' pushes the size of the open file referenced by the handle
** given by its argument on to the Basic stack
*/
static void fn_ext(void) {
  if (*basicvars.current != '#') error(ERR_HASHMISS);
  basicvars.current++;
  push_int(fileio_getext(eval_intfactor()));
}

/*
** 'fn_filepath' pushes a copy of the current program and library
** load path on to the Basic stack
*/
static void fn_filepath(void) {
  int32 length;
  char *cp;
  if (basicvars.loadpath == NIL)
    length = 0;
  else {
    length = strlen(basicvars.loadpath);
  }
  cp = alloc_string(length);
  if (length>0) memcpy(cp, basicvars.loadpath, length);
  push_strtemp(length, cp);
}

/*
** 'fn_left' handles the 'LEFT$(' function
*/
static void fn_left(void) {
  stackitem stringtype;
  basicstring descriptor;
  int32 length;
  char *cp;
  expression();		/* Fetch the string */
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) error(ERR_TYPESTR);
  if (*basicvars.current == ',') {	/* Function call is of the form LEFT(<string>,<value>) */
    basicvars.current++;
    length = eval_integer();
    if (*basicvars.current != ')') error(ERR_RPMISS);	/* ')' missing */
    basicvars.current++;
    if (length<0)
      return;	/* Do nothing if required length is negative, that is, return whole string */
    else if (length == 0) {		/* Don't want anything from the string */
      descriptor = pop_string();
      if (stringtype == STACK_STRTEMP) free_string(descriptor);
      cp = alloc_string(0);		/* Allocate a null string */
      push_strtemp(0, cp);
    }
    else {
      descriptor = pop_string();
      if (length>=descriptor.stringlen)	/* Substring length exceeds that of original string */
        push_string(descriptor);	/* So put the old string back on the stack */
      else {
        cp = alloc_string(length);
        memcpy(cp, descriptor.stringaddr, length);
        push_strtemp(length, cp);
        if (stringtype == STACK_STRTEMP) free_string(descriptor);
      }
    }
  }
  else {	/* Return original string with the last character sawn off */
    if (*basicvars.current != ')') error(ERR_RPMISS);	/* ')' missing */
    basicvars.current++;	/* Skip past the ')' */
    descriptor = pop_string();
    length = descriptor.stringlen-1;
    if (length<=0) {
      if (stringtype == STACK_STRTEMP) free_string(descriptor);
      cp = alloc_string(0);		/* Allocate a null string */
      push_strtemp(0, cp);
    }
    else {	/* Create a new string of the required length */
      cp = alloc_string(length);
      memmove(cp, descriptor.stringaddr, length);
      push_strtemp(length, cp);
      if (stringtype == STACK_STRTEMP) free_string(descriptor);
    }
  }
}

/*
** 'fn_lomem' pushes the address of the start of the Basic heap on
** to the Basic stack
*/
static void fn_lomem(void) {
  push_int(basicvars.lomem-basicvars.offbase);
}

/*
** 'fn_mid' handles the 'MID$(' function, which returns the middle
** part of a string
*/
static void fn_mid(void) {
  stackitem stringtype;
  basicstring descriptor;
  int32 start, length;
  char *cp;
  expression();		/* Fetch the string */
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) error(ERR_TYPESTR);
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  start = eval_integer();
  if (*basicvars.current == ',') {	/* Call of the form 'MID$(<string>,<expr>,<expr>) */
    basicvars.current++;
    length = eval_integer();
    if (length<0) length = MAXSTRING;	/* -ve length = use remainder of string */
  }
  else {	/* Length not given - Use remainder of string */
    length = MAXSTRING;
  }
  if (*basicvars.current != ')') error(ERR_RPMISS);	/* ')' missing */
  basicvars.current++;
  descriptor = pop_string();
  if (length == 0 || start<0 || start>descriptor.stringlen) {	/* Don't want anything from the string */
    if (stringtype == STACK_STRTEMP) free_string(descriptor);
    cp = alloc_string(0);		/* Allocate a null string */
    push_strtemp(0, cp);
  }
  else {	/* Want only some of the original string */
    if (start>0) start-=1;	/* Turn start position into an offset from zero */
    if (start == 0 && length>=descriptor.stringlen)	/* Substring is entire string */
      push_string(descriptor);	/* So put the old string back on the stack */
    else {
      if (start+length>descriptor.stringlen) length = descriptor.stringlen-start;
      cp = alloc_string(length);
      memcpy(cp, descriptor.stringaddr+start, length);
      push_strtemp(length, cp);
      if (stringtype == STACK_STRTEMP) free_string(descriptor);
    }
  }
}

/*
** 'fn_page' pushes the address of the start of the basic program on to the
** Basic stack
*/
static void fn_page(void) {
  push_int(basicvars.page-basicvars.offbase);
}

/*
** 'fn_ptr' returns the current offset within the file of the file
** pointer for the file associated with file handle 'handle'
*/
static void fn_ptr(void) {
  if (*basicvars.current != '#') error(ERR_HASHMISS);
  basicvars.current++;
  push_int(fileio_getptr(eval_intfactor()));
}

/*
** 'fn_right' evaluates the function 'RIGHT$('.
*/
static void fn_right(void) {
  stackitem stringtype;
  basicstring descriptor;
  int32 length;
  char *cp;
  expression();		/* Fetch the string */
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) error(ERR_TYPESTR);
  if (*basicvars.current == ',') {	/* Function call is of the form RIGHT$(<string>,<value>) */
    basicvars.current++;
    length = eval_integer();
    if (*basicvars.current != ')') error(ERR_RPMISS);	/* ')' missing */
    basicvars.current++;
    if (length<=0) {	/* Do not want anything from string */
      descriptor = pop_string();
      if (stringtype == STACK_STRTEMP) free_string(descriptor);
      cp = alloc_string(0);		/* Allocate a null string */
      push_strtemp(0, cp);
    }
    else {
      descriptor = pop_string();
      if (length>=descriptor.stringlen)	/* Substring length exceeds that of original string */
        push_string(descriptor);	/* So put the old string back on the stack */
      else {
        cp = alloc_string(length);
        memcpy(cp, descriptor.stringaddr+descriptor.stringlen-length, length);
        push_strtemp(length, cp);
        if (stringtype == STACK_STRTEMP) free_string(descriptor);
      }
    }
  }
  else {	/* Return only the last character */
    if (*basicvars.current != ')') error(ERR_RPMISS);	/* ')' missing */
    basicvars.current++;	/* Skip past the ')' */
    descriptor = pop_string();
    if (descriptor.stringlen == 0)
      push_string(descriptor);	/* String length is zero - Just put null string back on stack */
    else {	/* Create a new single character string */
      cp = alloc_string(1);
      *cp = *(descriptor.stringaddr+descriptor.stringlen-1);
      push_strtemp(1, cp);
      if (stringtype == STACK_STRTEMP) free_string(descriptor);
    }
  }
}

/*
** 'fn_time' returns the value of the centisecond timer. How accurate
** this is depends on the underlying OS
*/
static void fn_time(void) {
  push_int(emulate_time());
}

/*
** 'timedol' returns the date and time as string in the standard
** RISC OS format. There is no need to emulate this as standard C
** functions can be used to return the value
*/
static void fn_timedol(void) {
  size_t length;
  time_t thetime;
  char *cp;
  thetime = time(NIL);
  length = strftime(basicvars.stringwork, MAXSTRING, TIMEFORMAT, localtime(&thetime));
  cp = alloc_string(length);
  memcpy(cp, basicvars.stringwork, length);
  push_strtemp(length, cp);
}

/*
** 'fn_abs' returns the absolute value of the function's argument. The
** values are updated in place on the Basic stack
*/
static void fn_abs(void) {
  stackitem numtype;
  (*factor_table[*basicvars.current])();
  numtype = GET_TOPITEM;
  if (numtype == STACK_INT)
    ABS_INT;
  else if (numtype == STACK_FLOAT)
    ABS_FLOAT;
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'fn_acs' evalutes the arc cosine of its argument
*/
static void fn_acs(void) {
  (*factor_table[*basicvars.current])();
  if (GET_TOPITEM == STACK_INT)
    push_float(acos(TOFLOAT(pop_int())));
  else if (GET_TOPITEM == STACK_FLOAT)
    push_float(acos(pop_float()));
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'fn_adval' deals with the 'ADVAL' function. This is a BBC Micro-specific
** function that returns the current value of that machine's built-in A/D
** convertor. As per RISC OS, using the function for this purpose generates
** an error. 'ADVAL' can also be used to return the space left or the number
** of entries currently used in the various buffers within RISC OS for the
** serial port, parallel port and so on. A dummy value is returned for
** these
*/
static void fn_adval(void) {
  push_int(emulate_adval(eval_intfactor()));
}

/*
** fn_argc pushes the number of command line arguments on to the
** Basic stack
*/
static void fn_argc(void) {
  push_int(basicvars.argcount);
}

/*
** fn_argvdol' pushes a copy of a command line parameter on to
** the Basic stack
*/
static void fn_argvdol(void) {
  int32 number, length;
  cmdarg *ap;
  char *cp;
  number = eval_intfactor();	/* Fetch number of argument to push on to stack */
  if (number<0 || number>basicvars.argcount) error(ERR_RANGE);
  ap = basicvars.arglist;
  while (number > 0) {
    number--;
    ap = ap->nextarg;
  }    
  length = strlen(ap->argvalue);
  cp = alloc_string(length);
  if (length>0) memcpy(cp, ap->argvalue, length);
  push_strtemp(length, cp);
}

/*
** 'fn_asc' returns the character code for the first character of the string
** given as its argument or -1 if the string is the null string
*/
static void fn_asc(void) {
  basicstring descriptor;
  stackitem topitem;
  (*factor_table[*basicvars.current])();
  topitem = GET_TOPITEM;
  if (topitem == STACK_STRING || topitem == STACK_STRTEMP) {
    descriptor = pop_string();
    if (descriptor.stringlen == 0)	/* Null string returns -1 with ASC */
      push_int(-1);
    else {
      push_int(*descriptor.stringaddr & BYTEMASK);
      if (topitem == STACK_STRTEMP) free_string(descriptor);
    }
  }
  else {
    error(ERR_TYPESTR);	/* String wanted */
  }
}

/*
** 'fn_asn' evalutes the arc sine of its argument
*/
static void fn_asn(void) {
  (*factor_table[*basicvars.current])();
  if (GET_TOPITEM == STACK_INT)
    push_float(asin(TOFLOAT(pop_int())));
  else if (GET_TOPITEM == STACK_FLOAT)
    push_float(asin(pop_float()));
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'fn_atn' evalutes the arc tangent of its argument
*/
static void fn_atn(void) {
  (*factor_table[*basicvars.current])();
  if (GET_TOPITEM == STACK_INT)
    push_float(atan(TOFLOAT(pop_int())));
  else if (GET_TOPITEM == STACK_FLOAT)
    push_float(atan(pop_float()));
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'fn_beat' is one of the functions associated with RISC OS' sound system.
** Both 'BEATS' and 'BEAT' seem to return the same value.
*/
void fn_beat(void) {
  if (*basicvars.current == TOKEN_BEATS) basicvars.current++;	/* BEATS and BEAT arrive here via different routes */
  push_int(emulate_beatfn());
}

/*
** 'bget' returns the next byte from the file identified by the
** handle specified as its argument
*/
static void fn_bget(void) {
  if (*basicvars.current != '#') error(ERR_HASHMISS);
  basicvars.current++;
  push_int(fileio_bget(eval_intfactor()));
}

/*
** 'fn_chr' converts the value given as its argument to a single
** character string
*/
static void fn_chr(void) {
  char *cp;
  (*factor_table[*basicvars.current])();
  if (GET_TOPITEM == STACK_INT) {
    cp = alloc_string(1);
    *cp = pop_int();
    push_strtemp(1, cp);
  }
  else if (GET_TOPITEM == STACK_FLOAT) {
    cp = alloc_string(1);	/* obtain memory for a single character string */
    *cp = TOINT(pop_float());	/* Cast rounds towards zero */
    push_strtemp(1, cp);
  }
  else {
    error(ERR_TYPENUM);
  }
}

/*
** fn_colour - Return the colour number of the colour
** which most closely matches the colour with red, green
** and blue components passed to it. The colour is matched
** against the colours available in the current screen
** mode
*/
void fn_colour(void) {
  int32 red, green, blue;
  basicvars.current++;
  if (*basicvars.current != '(') error(ERR_SYNTAX);	/* COLOUR must be followed by a '(' */
  basicvars.current++;
  red = eval_integer();
  if (*basicvars.current != ',') error(ERR_SYNTAX);
  basicvars.current++;
  green = eval_integer();
  if (*basicvars.current != ',') error(ERR_SYNTAX);
  basicvars.current++;
  blue = eval_integer();
  if (*basicvars.current != ')') error(ERR_RPMISS);
  basicvars.current++;
  push_int(emulate_colourfn(red, green, blue));
}

/*
** 'fn_cos' evaluates the cosine of its argument
*/
static void fn_cos(void) {
  (*factor_table[*basicvars.current])();
  if (GET_TOPITEM == STACK_INT)
    push_float(cos(TOFLOAT(pop_int())));
  else if (GET_TOPITEM == STACK_FLOAT)
    push_float(cos(pop_float()));
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'fn_count' returns the number of characters printed on the current
** line by 'PRINT'
*/
static void fn_count(void) {
  push_int(basicvars.printcount);
}

/*
** 'get_arrayname' parses an array name and returns a pointer to its
** symbol table entry. On entry, 'basicvars.current' points at the
** array's variable token. It is left pointing at the byte after the
** pointer to the array's symbol table entry
*/
static variable *get_arrayname(void) {
  variable *vp = NULL;
  if (*basicvars.current == TOKEN_ARRAYVAR)	/* Known reference */
    vp = GET_ADDRESS(basicvars.current, variable *);
  else if (*basicvars.current == TOKEN_XVAR) {	/* Reference not seen before */
    byte *base, *ep;
    base = get_srcaddr(basicvars.current);	/* Find address of array's name */
    ep = skip_name(base);
    vp = find_variable(base, ep-base);
    if (vp == NIL) error(ERR_ARRAYMISS, tocstring(CAST(base, char *), ep-base));
    if ((vp->varflags & VAR_ARRAY) == 0) error(ERR_VARARRAY);	/* Not an array */
    if (*(basicvars.current+LOFFSIZE+1) != ')') error(ERR_RPMISS);	/* Array name must be suppled as 'array()' */
    *basicvars.current = TOKEN_ARRAYVAR;
    set_address(basicvars.current, vp);
  }
  else {	/* Not an array name */
    error(ERR_VARARRAY);	/* Name not found */
  }
  if (vp->varentry.vararray == NIL) error(ERR_NODIMS, vp->varname);	/* Array has not been dimensioned */
  basicvars.current+=LOFFSIZE+2;	/* Skip pointer to array and ')' */
  return vp;
}

/*
** 'fn_dim' handles the 'DIM' function. This returns either the number
** of dimensions the specified array has or the upper bound of the
** dimension given by the second parameter
*/
void fn_dim(void) {
  variable *vp;
  int32 dimension;
  basicvars.current++;
  if (*basicvars.current != '(') error(ERR_SYNTAX);	/* DIM must be followed by a '(' */
  basicvars.current++;
  vp = get_arrayname();
  switch (*basicvars.current) {
  case ',':	/* Got 'array(),<x>) - Return upper bound of dimension <x> */
    basicvars.current++;
    dimension = eval_integer();		/* Get dimension number */
    if (*basicvars.current != ')') error(ERR_RPMISS);
    basicvars.current++; 	/* Skip the trailing ')' */
    if (dimension<1 || dimension>vp->varentry.vararray->dimcount) error(ERR_DIMRANGE);
    push_int(vp->varentry.vararray->dimsize[dimension-1]-1);
    break;
  case ')':	/* Got 'array())' - Return the number of dimensions */
    push_int(vp->varentry.vararray->dimcount);
    basicvars.current++;
    break;
  default:
    error(ERR_SYNTAX);
  }
}

/*
** The function 'DEG' converts an angrle expressed in radians to degrees
*/
static void fn_deg(void) {
  (*factor_table[*basicvars.current])();
  if (GET_TOPITEM == STACK_INT)
    push_float(TOFLOAT(pop_int())*RADCONV);
  else if (GET_TOPITEM == STACK_FLOAT)
    push_float(pop_float()*RADCONV);
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'fn_end' deals with the 'END' function, which pushes the address of
** the top of the Basic program and variables on to the Basic stack
*/
void fn_end(void) {
  basicvars.current++;
  push_int(basicvars.vartop-basicvars.offbase);
}

/*
** 'fn_eof' deals with the 'EOF' function, which returns 'TRUE' if the
** 'at end of file' flag is set for the file specified
*/
static void fn_eof(void) {
  int32 handle;
  if (*basicvars.current != '#') error(ERR_HASHMISS);
  basicvars.current++;
  handle = eval_intfactor();
  push_int(fileio_eof(handle) ? BASTRUE : BASFALSE);
}

/*
** 'fn_erl' pushes the line number of the line at which the last error
** occured
*/
static void fn_erl(void) {
  push_int(basicvars.error_line);
}

/*
** 'fn_err' pushes the error number of the last error on to the Basic
** stack
*/
static void fn_err(void) {
  push_int(basicvars.error_number);
}

/*
** 'fn_eval' deals with the function 'eval'
** The argument of the function is tokenized and stored in
** 'evalexpr'. The current value of 'basicvars.current' is
** saved locally, but this is not the proper place if an
** error occurs in the expression being evaluated as the
** current will not be pointing into the Basic program. I
** think the value should be saved on the Basic stack.
*/
static void fn_eval(void) {
  stackitem stringtype;
  basicstring descriptor;
  byte evalexpr[MAXSTATELEN];
  (*factor_table[*basicvars.current])();
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) error(ERR_TYPESTR);
  descriptor = pop_string();
  memmove(basicvars.stringwork, descriptor.stringaddr, descriptor.stringlen);
  basicvars.stringwork[descriptor.stringlen] = NUL;	/* Now have a null-terminated version of string */
  if (stringtype == STACK_STRTEMP) free_string(descriptor);
  tokenize(basicvars.stringwork, evalexpr, NOLINE);	/* 'tokenise' leaves its results in 'thisline' */
  save_current();		/* Save pointer to current position in expression */
  basicvars.current = FIND_EXEC(evalexpr);
  expression();
  if (*basicvars.current != NUL) error(ERR_SYNTAX);
  restore_current();
}

/*
** 'fn_exp' evaluates the exponentinal function of its argument
*/
static void fn_exp(void) {
  stackitem topitem;
  (*factor_table[*basicvars.current])();
  topitem = GET_TOPITEM;
  if (topitem == STACK_INT)
    push_float(exp(TOFLOAT(pop_int())));
  else if (topitem == STACK_FLOAT)
    push_float(exp(pop_float()));
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'fn_false' pushes the value which represents 'FALSE' on to the Basic stack
*/
void fn_false(void) {
  basicvars.current++;
  PUSH_INT(BASFALSE);
}

/*
** 'fn_get' implements the 'get' function which reads a character from the
** keyboard and saves it on the Basic stack as a number
*/
static void fn_get(void) {
  push_int(emulate_get());
}

/*
** 'fn_getdol' implements the 'get$' function which either reads a character
** from the keyboard or a string from a file
*/
static void fn_getdol(void) {
  char *cp;
  int32 handle, count;
  if (*basicvars.current == '#') {	/* Have encountered the 'GET$#' version */
    basicvars.current++;
    handle = eval_intfactor();
    count = fileio_getdol(handle, basicvars.stringwork);
    cp = alloc_string(count);
    memcpy(cp, basicvars.stringwork, count);
    push_strtemp(count, cp);
  }
  else {	/* Normal 'GET$' - Return character read as a string */
    cp = alloc_string(1);
    *cp = emulate_get();
    push_strtemp(1, cp);
  }
}

/*
** 'fn_inkey' deals with the 'inkey' function. Under RISC OS this is just a call to
** OS_Byte 129 under a different name.
*/
static void fn_inkey(void) {
  push_int(emulate_inkey(eval_intfactor()));
}

/*
** 'fn_inkeydol' carries out the same functions as 'fn_inkey' except that the
** result is returned as a string. Where the result would be -1, a null
** string is saved on the Basic stack instead
*/
static void fn_inkeydol(void) {
  int32 result;
  char *cp;
  result = emulate_inkey(eval_intfactor());
  if (result == -1) {
    cp = alloc_string(0);
    push_strtemp(0, cp);
  }
  else {
    cp = alloc_string(1);
    *cp = result;
    push_strtemp(1, cp);
  }
}

/*
** 'fn_instr' deals with the 'INSTR' function.
** Note: in the case where the search string is the null string, the value
** returned by BBC Basic is not what the Acorn documentation says it should be.
** The manuals say that the function should return either one or the starting
** position of the search if it was specified. It only does this if the starting
** position is one or two. If greater than two, zero is returned. 'fn_instr'
** mimics this behaviour
*/
static void fn_instr(void) {
  basicstring needle, haystack;
  stackitem needtype, haytype;
  char *hp, *p;
  int32 start, count;
  char first;
  expression();
  if (*basicvars.current != ',') error(ERR_COMISS);	/* ',' missing */
  basicvars.current++;
  haytype = GET_TOPITEM;
  if (haytype != STACK_STRING && haytype != STACK_STRTEMP) error(ERR_TYPESTR);
  haystack = pop_string();
  expression();
  needtype = GET_TOPITEM;
  if (needtype != STACK_STRING && needtype != STACK_STRTEMP) error(ERR_TYPESTR);
  needle = pop_string();
  if (*basicvars.current == ',') {	/* Starting position given */
    basicvars.current++;
    start = eval_integer();
    if (start<1) start = 1;
  }
  else {	/* Set starting position to one */
    start = 1;
  }
  if (*basicvars.current != ')') error(ERR_RPMISS);
  basicvars.current++;
/*
** After finding the string to be searched (haystack) and what to look
** for (needle) and the starting position (start), deal with the special
** cases first. First, check if the search string is longer than the
** original string or would extend beyond the end of that string then
** deal with a zero-length target string. If anything is left after
** this, there is nothing else for it but to search for the target
** string
*/
  if (needle.stringlen>haystack.stringlen-start+1)
    push_int(0);	/* Always returns zero if search string is longer than main string */
  else if (needle.stringlen == 0) {	/* Search string length is zero */
    if (haystack.stringlen == 0)	/* Both string are the null string */
      push_int(1);
    else if (start<3)
      push_int(start);
    else {
      push_int(0);
    }
  }
  else {	/* Will have to search string */
    hp = haystack.stringaddr+start-1;	/* Start searching from this address */
    first = *needle.stringaddr;
    count = haystack.stringaddr+haystack.stringlen-hp;	/* Count of chars in original string to check */
    if (needle.stringlen == 1) {		/* Looking for a single character */
      p = memchr(hp, first, count);
      if (p == NIL)	/* Did not find the character */
        push_int(0);
      else {	/* Found character - Place its offset (from 1) on stack */
        push_int(p-haystack.stringaddr+1);
      }
    }
    else {	/* Looking for more than one character */
      do {
        p = memchr(hp, first, count);	/* Look for first char in string */
        if (p == NIL)
          count = 0;	/* Character not found */
        else {	/* Found an occurence of the first search char in the original string */
          count-=(p-hp);
          if (count<needle.stringlen)	/* Chars left to search is less that search string length */
            count = 0;
          else {
            if (memcmp(p, needle.stringaddr, needle.stringlen) == 0) break;
            hp = p+1;
            count--;
          }
        }
      } while (count>0);
      if (count == 0)	/* Search string not found */
        push_int(0);
      else {		/* Push offset (from 1) at which string was found on to stack */
        push_int(p-haystack.stringaddr+1);
      }
    }
  }
  if (haytype == STACK_STRTEMP) free_string(haystack);
  if (needtype == STACK_STRTEMP) free_string(needle);
}

/*
** 'fn_int' implements the 'INT' function. It pushes the integer part
** of its argument on to the Basic stack
*/
static void fn_int(void) {
  (*factor_table[*basicvars.current])();
  if (GET_TOPITEM == STACK_FLOAT)
    push_int(TOINT(floor(pop_float())));
  else if (GET_TOPITEM != STACK_INT) {
    error(ERR_TYPENUM);
  }
}

/*
** 'fn_len' pushes the length of its string argument on to the Basic stack
*/
static void fn_len(void) {
  basicstring descriptor;
  stackitem stringtype;
  (*factor_table[*basicvars.current])();
  stringtype = GET_TOPITEM;
  if (stringtype == STACK_STRING || stringtype == STACK_STRTEMP) {
    descriptor = pop_string();
    PUSH_INT(descriptor.stringlen);
    if (stringtype == STACK_STRTEMP) free_string(descriptor);
  }
  else {
    error(ERR_TYPESTR);
  }
}

/*
** 'fn_listofn' pushes the current 'LISTO' value on to the stack
*/
static void fn_listofn(void) {
  push_int(basicvars.list_flags.space | basicvars.list_flags.indent<<1
  	| basicvars.list_flags.split<<2 | basicvars.list_flags.noline<<3
  	| basicvars.list_flags.lower<<4 | basicvars.list_flags.showpage<<5);
}

/*
** 'fn_ln' evaluates the natural log of its argument
*/
static void fn_ln(void) {
  (*factor_table[*basicvars.current])();
  switch (GET_TOPITEM) {
  case STACK_INT: {
    int32 value = pop_int();
    if (value<=0) error(ERR_LOGRANGE);
    push_float(log(TOFLOAT(value)));
    break;
  }
  case STACK_FLOAT:
    floatvalue = pop_float();
    if (floatvalue<=0.0) error(ERR_LOGRANGE);
    push_float(log(floatvalue));
    break;
  default:
    error(ERR_TYPENUM);
  }
}

/*
** 'fn_log' computes the base 10 log of its argument
*/
static void fn_log(void) {
  (*factor_table[*basicvars.current])();
  switch (GET_TOPITEM) {
  case STACK_INT: {
    int32 value = pop_int();
    if (value<=0) error(ERR_LOGRANGE);
    push_float(log10(TOFLOAT(value)));
    break;
  }
  case STACK_FLOAT:
    floatvalue = pop_float();
    if (floatvalue<=0.0) error(ERR_LOGRANGE);
    push_float(log10(floatvalue));
    break;
  default:
    error(ERR_TYPENUM);
  }
}

/*
** 'fn_mod' deals with 'mod' when it is used as a function. It
** returns the modulus (square root of the sum of the squares)
** of an array
*/
void fn_mod(void) {
  static float64 fpsum;
  int32 n, elements;
  variable *vp;
  basicvars.current++;		/* Skip MOD token */
  if(*basicvars.current == '(') {	/* One level of parentheses is allowed */
    basicvars.current++;
    vp = get_arrayname();
    if (*basicvars.current != ')') error(ERR_RPMISS);
    basicvars.current++;
  }
  else {
    vp = get_arrayname();
  }
  elements = vp->varentry.vararray->arrsize;
  switch (vp->varflags) {
  case VAR_INTARRAY: {	/* Calculate the modulus of an integer array */
    int32 *p = vp->varentry.vararray->arraystart.intbase;
    fpsum = 0;
    for (n=0; n<elements; n++) fpsum+=TOFLOAT(p[n])*TOFLOAT(p[n]);
    push_float(sqrt(fpsum));
    break;
  }
  case VAR_FLOATARRAY: {	/* Calculate the modulus of a floating point array */
    float64 *p = vp->varentry.vararray->arraystart.floatbase;
    fpsum = 0;
    for (n=0; n<elements; n++) fpsum+=p[n]*p[n];
    push_float(sqrt(fpsum));
    break;
  }
  case VAR_STRARRAY:
    error(ERR_NUMARRAY);	/* Numeric array wanted */
    break;
  default:	/* Bad 'varflags' value found */
    error(ERR_BROKEN, __LINE__, "expressions");
  }
}

/*
** 'fn_mode' pushes the current screen mode number on to the Basic
** stack. Under operating systems other than RISC OS this might have
** no meaning
*/
void fn_mode(void) {
  basicvars.current++;
  push_int(emulate_modefn());
}

/*
** 'fn_not' implements the 'not' function, pushing the bitwise
** 'not' of its argument on to the stack
*/
void fn_not(void) {
  basicvars.current++;		/* Skip NOT token */
  (*factor_table[*basicvars.current])();
  if (GET_TOPITEM == STACK_INT)
    NOT_INT;
  else if (GET_TOPITEM == STACK_FLOAT) {
    push_int(~TOINT(pop_float()));
  }
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'fn_openin deals with the function OPENIN which opens a file for input
*/
static void fn_openin(void) {
  stackitem stringtype;
  basicstring descriptor;
  (*factor_table[*basicvars.current])();
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) error(ERR_TYPESTR);
  descriptor = pop_string();
  push_int(fileio_openin(descriptor.stringaddr, descriptor.stringlen));
  if (stringtype == STACK_STRTEMP) free_string(descriptor);
}

/*
** 'fn_openout' deals the function 'OPENOUT', which opens a file for
** output
*/
static void fn_openout(void) {
  stackitem stringtype;
  basicstring descriptor;
  (*factor_table[*basicvars.current])();
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) error(ERR_TYPESTR);
  descriptor = pop_string();
  push_int(fileio_openout(descriptor.stringaddr, descriptor.stringlen));
  if (stringtype == STACK_STRTEMP) free_string(descriptor);
}

/*
** 'fn_openup' deals the function 'OPENUP', which opens a file for
** both input and output
*/
static void fn_openup(void) {
  stackitem stringtype;
  basicstring descriptor;
  (*factor_table[*basicvars.current])();
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) error(ERR_TYPESTR);
  descriptor = pop_string();
  push_int(fileio_openup(descriptor.stringaddr, descriptor.stringlen));
  if (stringtype == STACK_STRTEMP) free_string(descriptor);
}

/*
** 'fn_pi' pushes the constant value pi on to the Basic stack
*/
static void fn_pi(void) {
  push_float(PI);
}

/*
** 'fn_point' emulates the Basic function 'POINT'
*/
static void fn_pointfn(void) {
  int32 x, y;
  x = eval_integer();
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  y = eval_integer();
  if (*basicvars.current != ')') error(ERR_RPMISS);
  basicvars.current++;
  push_int(emulate_pointfn(x, y));
}

/*
** 'fn_pos' emulates the Basic function 'POS'
*/
static void fn_pos(void) {
  push_int(emulate_pos());
}

/*
** 'fn_quit' saves 'true' or' false' on the stack depending
**on the value of the 'quit interpreter at end of run' flag
*/
void fn_quit(void) {
  basicvars.current++;
  push_int(basicvars.runflags.quitatend);
}

/*
** 'rad' converts the value on top of the Basic stack from degrees
** to radians
*/
static void fn_rad(void) {
  (*factor_table[*basicvars.current])();
  if (GET_TOPITEM == STACK_INT)
    push_float(TOFLOAT(pop_int())/RADCONV);
  else if (GET_TOPITEM == STACK_FLOAT)
    push_float(pop_float()/RADCONV);
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'fn_reportdol' handles the 'REPORT$' function, which puts a
** copy of the last error message on the Basic stack
*/
static void fn_reportdol(void) {
  char *p;
  int32 length;
  length = strlen(get_lasterror());
  p = alloc_string(length);
  memmove(p, get_lasterror(), length);
  push_strtemp(length, p);
}

/*
** 'fn_retcode' pushes the return code from the last command
** issued via OSCLI or '*' on to the Basic stack
*/
static void fn_retcode(void) {
  push_int(basicvars.retcode);
}

/*
** 'nextrandom' updates the pseudo-random number generator
**
** Based on the BASIC II pseudo-random number generator
*/
static void nextrandom(void) {
  int n;
  for (n=0; n<32; n++) {
    int newbit = ((lastrandom>>19) ^ randomoverflow) & 1;
    randomoverflow = lastrandom>>31;
    lastrandom = (lastrandom<<1) | newbit;
  }
}

/*
** 'randomfraction' returns the pseudo-random number as float fraction
*/
static float64 randomfraction(void) {
  uint32 reversed = ((lastrandom>>24)&0xFF)|((lastrandom>>8)&0xFF00)|((lastrandom<<8)&0xFF0000)|((lastrandom<<24)&0xFF000000);

  return TOFLOAT(reversed) / 4294967296.0;
}

/*
** 'fn_rnd' evaluates the function 'RND'.
*/
static void fn_rnd(void) {
  int32 value;
  if (*basicvars.current == '(') {		/* Have got 'RND()' */
    basicvars.current++;
    value = eval_integer();
    if (*basicvars.current != ')') error(ERR_RPMISS);
    basicvars.current++;
    if (value<0) {	/* Negative value = reseed random number generator */
      lastrandom = value;
      randomoverflow = 0;
      push_int(value);
    }
    else if (value == 0) {	/* Return last result */
      push_float(randomfraction());
    }
    else if (value == 1) {	/* Return value in range 0 to 0.9999999999 */
      nextrandom();
      push_float(randomfraction());
    }
    else {
      nextrandom();
      push_int(TOINT(randomfraction()*TOFLOAT(value)));
    }
  }
  else {	/* Return number in the range 0x80000000..0x7fffffff */
    nextrandom();
    push_int(lastrandom);
  }
}

/*
** 'fn_sgn' pushes +1, 0 or -1 on to the Basic stack depending on
** whether the value there is positive, zero or negative
*/
static void fn_sgn(void) {
  (*factor_table[*basicvars.current])();
  if (GET_TOPITEM == STACK_INT) {
    int32 value = pop_int();
    if (value>0) {
      PUSH_INT(1);
    }
    else if (value == 0) {
      PUSH_INT(0);
    }
    else {
      PUSH_INT(-1);
    }
  }
  else if (GET_TOPITEM == STACK_FLOAT) {
    floatvalue = pop_float();
    if (floatvalue>0.0) {
      PUSH_INT(1);
    }
    else if (floatvalue == 0.0) {
      PUSH_INT(0);
    }
    else {
      PUSH_INT(-1);
    }
  }
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'fn_sin' evaluates the sine of its argument
*/
static void fn_sin(void) {
  (*factor_table[*basicvars.current])();
  if (GET_TOPITEM == STACK_INT)
    push_float(sin(TOFLOAT(pop_int())));
  else if (GET_TOPITEM == STACK_FLOAT)
    push_float(sin(pop_float()));
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'fn_sqr' evaluates the square root of its argument
*/
static void fn_sqr(void) {
  (*factor_table[*basicvars.current])();
  if (GET_TOPITEM == STACK_INT) {
    int32 value = pop_int();
    if (value<0) error(ERR_NEGROOT);
    push_float(sqrt(TOFLOAT(value)));
  }
  else if (GET_TOPITEM == STACK_FLOAT) {
    floatvalue = pop_float();
    if (floatvalue<0.0) error(ERR_NEGROOT);
    push_float(sqrt(floatvalue));
  }
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'fn_str' converts its numeric argument to a character string. The
** number is converted to its hex representation if 'STR$' is followed
** with a '~'
*/
static void fn_str(void) {
  boolean ishex;
  int32 length = 0;
  char *cp;
  ishex = *basicvars.current == '~';
  if (ishex) basicvars.current++;
  (*factor_table[*basicvars.current])();
  if (GET_TOPITEM == STACK_INT) {
    if (ishex)
      length = sprintf(basicvars.stringwork, "%X", pop_int());
    else {
      length = sprintf(basicvars.stringwork, "%d", pop_int());
    }
  }
  else if (GET_TOPITEM == STACK_FLOAT) {
    if (ishex)
      length = sprintf(basicvars.stringwork, "%X", TOINT(pop_float()));
    else {
      int32 format, numdigits;
      char *fmt;
      format = basicvars.staticvars[ATPERCENT].varentry.varinteger;
      if ((format & STRUSE) == 0) format = STRFORMAT;	/* Use predefined format, not @% */
      switch ((format>>2*BYTESHIFT) & BYTEMASK) {	/* Determine format of floating point values */
      case FORMAT_E:
       fmt = "%.*E";
        break;
      case FORMAT_F:
        fmt = "%.*F";
        break;
      default:	/* Assume anything else will be general format */
        fmt = "%.*G";
      }
      numdigits = (format>>BYTESHIFT) & BYTEMASK;
      if (numdigits == 0) numdigits = DEFDIGITS;
      length = sprintf(basicvars.stringwork, fmt, numdigits, pop_float());
    }
  }
  else {
    error(ERR_TYPENUM);
  }
  cp = alloc_string(length);
  memcpy(cp, basicvars.stringwork, length);
  push_strtemp(length, cp);
}

/*
** 'fn_string' implements the 'STRING$' function
*/
static void fn_string(void) {
  int32 count, newlen;
  basicstring descriptor;
  char* base, *cp;
  stackitem stringtype;
  count = eval_integer();
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  expression();
  if (*basicvars.current != ')') error(ERR_RPMISS);
  basicvars.current++;
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) error(ERR_TYPESTR);
  if (count == 1) return;	/* Leave things as they are if repeat count is 1 */
  descriptor = pop_string();
  if (count<=0)
    newlen = 0;
  else  {
    newlen = count*descriptor.stringlen;
    if (newlen>MAXSTRING) error(ERR_STRINGLEN);	/* New string is too long */
  }
  base = cp = alloc_string(newlen);
  while (count>0) {
    memmove(cp, descriptor.stringaddr, descriptor.stringlen);
    cp+=descriptor.stringlen;
    count--;
  }
  if (stringtype == STACK_STRTEMP) free_string(descriptor);
  push_strtemp(newlen, base);
}

/*
** 'fn_sum' implements the Basic functions 'SUM' and 'SUM LEN'. 'SUM'
** either calculates the sum of all the elements if a numeric array or
** concatenates them to form one large string if a string array.
** 'SUM LEN' calculates the total length of all the strings in a
** string array
*/
static void fn_sum(void) {
  int32 n, elements;
  variable *vp;
  boolean sumlen;
  sumlen = *basicvars.current == TYPE_FUNCTION && *(basicvars.current+1) == TOKEN_LEN;
  if (sumlen) basicvars.current+=2;	/* Skip the 'LEN' token */
  if(*basicvars.current == '(') {	/* One level of parentheses is allowed */
    basicvars.current++;
    vp = get_arrayname();
    if (*basicvars.current != ')') error(ERR_RPMISS);
    basicvars.current++;
  }
  else {
    vp = get_arrayname();
  }
  elements = vp->varentry.vararray->arrsize;
  if (sumlen) {		/* Got 'SUM LEN' */
    int32 length;
    basicstring *p;
    if (vp->varflags != VAR_STRARRAY) error(ERR_TYPESTR);	/* Array is not a string array */
    p = vp->varentry.vararray->arraystart.stringbase;
    length = 0;
    for (n=0; n<elements; n++) length+=p[n].stringlen;	/* Find length of all strings in array */
    push_int(length);
  }
  else {	/* Got 'SUM' */
    switch (vp->varflags) {
    case VAR_INTARRAY: {	/* Calculate sum of elements in an integer array */
      int32 intsum, *p;
      p = vp->varentry.vararray->arraystart.intbase;
      intsum = 0;
      for (n=0; n<elements; n++) intsum+=p[n];
      push_int(intsum);
      break;
    }
    case VAR_FLOATARRAY: {	/* Calculate sum of elements in a floating point array */
      float64 fpsum, *p;
      fpsum = 0;
      p = vp->varentry.vararray->arraystart.floatbase;
      for (n=0; n<elements; n++) fpsum+=p[n];
      push_float(fpsum);
      break;
    }
    case VAR_STRARRAY: {	/* Concatenate all strings in a string array */
      int32 length, strlen;
      char *cp, *cp2;
      basicstring *p;
      p = vp->varentry.vararray->arraystart.stringbase;
      length = 0;
      for (n=0; n<elements; n++) length+=p[n].stringlen;	/* Find length of result string */
      if (length>MAXSTRING) error(ERR_STRINGLEN);		/* String is too long */
      cp = cp2 = alloc_string(length);	/* Grab enough memory to hold the result string */
      if (length>0) {
        for (n=0; n<elements; n++) {	/* Concatenate strings */
          strlen = p[n].stringlen;
          if (strlen>0) {	/* Ignore zero-length strings */
            memmove(cp2, p[n].stringaddr, strlen);
            cp2+=strlen;
          }
        }
      }
      push_strtemp(length, cp);
      break;
    }
    default:	/* Bad 'varflags' value found */
      error(ERR_BROKEN, __LINE__, "expressions");
    }
  }
}

/*
** 'fn_tan' calculates the tangent of its argument
*/
static void fn_tan(void) {
  (*factor_table[*basicvars.current])();
  if (GET_TOPITEM == STACK_INT)
    push_float(tan(TOFLOAT(pop_int())));
  else if (GET_TOPITEM == STACK_FLOAT)
    push_float(tan(pop_float()));
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'fn_tempofn' pushes the value returned by the Basic function
** 'TEMPO' on to the stack
*/
static void fn_tempofn(void) {
  push_int(emulate_tempofn());
}

/*
** 'fn_tint' deals with 'TINT' when used as a function, pushing
** the 'tint' value of point (x,y) on the screen on to the stack
*/
void fn_tint(void) {
  int32 x, y;
  if (*basicvars.current != '(') error(ERR_LPMISS);
  basicvars.current++;
  x = eval_integer();
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  y = eval_integer();
  if (*basicvars.current != ')') error(ERR_RPMISS);
  push_int(emulate_tintfn(x, y));
}

/*
** 'fn_top' pushes the address of the end of the Basic program
** itself on to the Basic stack.
** Note that 'TOP' is encoded as the token for 'TO' followed by
** the letter 'P'. There is no token for 'TOP'. This is the way
** the RISC OS Basic interpreter works.
*/
void fn_top(void) {
  byte *p;
  basicvars.current++;		/* Skip the 'TO' token */
  if (*basicvars.current != TOKEN_XVAR) error(ERR_SYNTAX);	/* 'TO' is not followed by a variable name */
  p = get_srcaddr(basicvars.current);		/* Find the address of the variable */
  if (*p != 'P') error(ERR_SYNTAX);		/* But it does not start with the letter 'P' */
  basicvars.current+=LOFFSIZE + 1;
  push_int(basicvars.top-basicvars.offbase);
}

/*
** 'trace' returns the handle of the file to which trace output
** is written
*/
void fn_trace(void) {
  basicvars.current++;
  push_int(basicvars.tracehandle);
}

/*
** 'fn_true' pushes the value that Basic uses to represent 'TRUE' on to
** the stack
*/
void fn_true(void) {
  basicvars.current++;
  push_int(BASTRUE);
}

/*
** 'fn_usr' is called to deal with the Basic function 'USR'. This
** allows machine code routines to be called from a Basic program.
** It is probably safer to say that this function is unsupported
*/
static void fn_usr(void) {
  push_int(emulate_usr(eval_intfactor()));
}

/*
** 'fn_val' converts a number held as a character string to binary. It
** interprets the string as a number as far as the first character that
** is not a valid digit, decimal point or 'E' (exponent mark). The number
** can be preceded with a sign. Both floating point and integer values
** are dealt with as well as binary and hexadecimal values. The result
** is left on the Basic stack
*/
static void fn_val(void) {
  stackitem stringtype;
  basicstring descriptor;
  char *cp;
  boolean isint;
  int32 intvalue;
  static float64 fpvalue;
  (*factor_table[*basicvars.current])();
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) error(ERR_TYPESTR);
  descriptor = pop_string();
  if (descriptor.stringlen == 0)
    push_int(0);	/* Nothing to do */
  else {
    memmove(basicvars.stringwork, descriptor.stringaddr, descriptor.stringlen);
    basicvars.stringwork[descriptor.stringlen] = NUL;
    if (stringtype == STACK_STRTEMP) free_string(descriptor);
    cp = tonumber(basicvars.stringwork, &isint, &intvalue, &fpvalue);
    if (cp == NIL) {	/* Error found when converting number */
      error(intvalue);	/* 'intvalue' is used to return the precise error */
    }
    if (isint)
      push_int(intvalue);
    else {
      push_float(fpvalue);
    }
  }
}

/*
** fn_vdu - Handle VDU when it is used as a function. It pushes
** the value of the VDU variable after the function name
*/
void fn_vdu(void) {
  int variable;
  basicvars.current++;
  variable = eval_intfactor();	/* Number of VDU variable */
  push_int(emulate_vdufn(variable));
}

/*
** 'fn_verify' handles the Basic function 'VERIFY'
*/
static void fn_verify(void) {
  stackitem stringtype, veritype;
  basicstring string, verify;
  int32 start, n;
  byte present[256];
  expression();
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) error(ERR_TYPESTR);
  string = pop_string();
  if (*basicvars.current != ',') error(ERR_COMISS);
  basicvars.current++;
  expression();
  veritype = GET_TOPITEM;
  if (veritype != STACK_STRING && veritype != STACK_STRTEMP) error(ERR_TYPESTR);
  verify = pop_string();
  if (*basicvars.current == ',') {	/* Start position supplied */
    basicvars.current++;
    start = eval_integer();
    if (start<1) start = 1;
  }
  else {	/* Set starting position to one */
    start = 1;
  }
  if (*basicvars.current != ')') error(ERR_RPMISS);
  basicvars.current++;
/*
** Start by dealing with the special cases. These are:
** 1) Start position is greater than the string length.
** 2) String is a null string (special case of 1).
** 3) Verify string is a null string.
** In cases 1) and 2) the value returned by the function is zero.
** In case 2) the start position is returned.
*/
  if (start>string.stringlen || verify.stringlen == 0) {
    if (veritype == STACK_STRTEMP) free_string(verify);
    if (stringtype == STACK_STRTEMP) free_string(string);
    if (verify.stringlen == 0)
      push_int(start);
    else {
      push_int(0);
    }
  }
/* Build a table of the characters present in the verify string */
  memset(present, FALSE, sizeof(present));
  for (n=0; n<verify.stringlen; n++) present[CAST(verify.stringaddr[n], byte)] = TRUE;
  start--;	/* Convert start index to offset in string */
/* Now ensure that all characters in string are in the verify string */
  while (start<string.stringlen && present[CAST(string.stringaddr[start], byte)]) start++;
  if (start == string.stringlen)	/* All characters are present and correct */
    push_int(0);
  else {	/* Character found that is not in the verify string */
    push_int(start+1);	/* Push its index on to the stack */
  }
  if (veritype == STACK_STRTEMP) free_string(verify);
  if (stringtype == STACK_STRTEMP) free_string(string);
}

/*
** 'fn_vpos' pushes the row number in which the text cursor is to be found
** on to the Basic stack
*/
static void fn_vpos(void) {
  push_int(emulate_vpos());
}

/*
** 'fn_width' pushes the current value of 'WIDTH' on to the Basic stack
*/
void fn_width(void) {
  basicvars.current++;	/* Skip WIDTH token */
  push_int(basicvars.printwidth);
}

/*
** 'fn_xlatefol' either converts the string argument to lower case
** or translates it using the user-supplied translate table. The
** translated string is pushed back on to the Basic stack
*/
static void fn_xlatedol(void) {
  stackitem stringtype, transtype;
  basicstring string, transtring = {0, NULL};
  basicarray *transarray = NULL;
  char *cp;
  int32 n;
  byte ch;
  expression();
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) error(ERR_TYPESTR);
  string = pop_string();
  if (*basicvars.current == ',') {
/* Got user-supplied translate table */
    basicvars.current++;
    expression();
    if (*basicvars.current != ')') error(ERR_RPMISS);
    basicvars.current++;	/* Skip the ')' */
    transtype = GET_TOPITEM;
    if (transtype == STACK_STRING || transtype == STACK_STRTEMP)
      transtring = pop_string();
    else if (transtype == STACK_STRARRAY) {
      transarray = pop_array();
      if (transarray->dimcount != 1) error(ERR_NOTONEDIM);	/* Must be a 1-D array */
    }
    else {
      error(ERR_TYPESTR);
    }
/* If the string or table length is zero then there is nothing to do */
    if (string.stringlen == 0 || (transtype != STACK_STRARRAY && transtring.stringlen == 0)) {
      push_string(string);	/* Put the old string back on the stack */
      return;
    }
    if (stringtype == STACK_STRING) {	/* Have to make a copy of the string to modify */
      cp = alloc_string(string.stringlen);
      memmove(cp, string.stringaddr, string.stringlen);
    }
    else {
      cp = string.stringaddr;
    }
/*
** Translate the string according to the user-supplied translate
** table. The table can be either a string or a string array.
** Only the characters that lie in the range covered by the
** translate table are altered, for example, if the translate
** table string is 100 characters long, only characters in the
** original string with an ASCII code in the range 0 to 99 are
** changed.
*/
    if (transtype == STACK_STRARRAY) {		/* Translate table is an array */
      int32 highcode = transarray->dimsize[0];
      basicstring *arraybase = transarray->arraystart.stringbase;
      for (n=0; n<string.stringlen; n++) {
        ch = CAST(cp[n], byte);		/* Must work with unsigned characters */
        if (ch<highcode && arraybase[ch].stringlen>0) cp[n] = arraybase[ch].stringaddr[0];
      }
    }
    else {
      for (n=0; n<string.stringlen; n++) {
        ch = CAST(cp[n], byte);		/* Must work with unsigned characters */
        if (ch<transtring.stringlen) cp[n] = transtring.stringaddr[ch];
      }
       if (transtype == STACK_STRTEMP) free_string(transtring);
    }
    push_strtemp(string.stringlen, cp);
  }
  else if (*basicvars.current != ')')
    error(ERR_RPMISS);	/* Must have a ')' next */
  else {
/* Translate string to lower case */
    basicvars.current++;	/* Skip the ')' */
    if (string.stringlen == 0) {	/* String length is zero */
      push_string(string);	/* So put the old string back on the stack */
      return;
    }
    if (stringtype == STACK_STRING) {	/* Have to make a copy of the string to modify */
      cp = alloc_string(string.stringlen);
      memmove(cp, string.stringaddr, string.stringlen);
    }
    else {
      cp = string.stringaddr;
    }
/*
** Translate string to lower case. To avoid any signed/unsigned
** char problems, only characters with an ASCII code in the
** range 0 to 0x7F are changed
*/
    for (n=0; n<string.stringlen; n++) {
      if (CAST(cp[n], byte)<0x80) cp[n] = tolower(cp[n]);
    }
    push_strtemp(string.stringlen, cp);
  }
}


/*
** The function table maps the function token to the function that deals
** with it
*/
static void (*function_table[])(void) = {
  bad_token, fn_himem, fn_ext, fn_filepath,		/* 00..03 */
  fn_left, fn_lomem, fn_mid, fn_page,			/* 04..07 */
  fn_ptr, fn_right, fn_time, fn_timedol,		/* 08..0B */
  bad_token, bad_token, bad_token, bad_token,		/* 0C..0F */
  fn_abs, fn_acs, fn_adval, fn_argc,			/* 10..13 */
  fn_argvdol, fn_asc, fn_asn, fn_atn, 			/* 14..17 */
  fn_beat, fn_bget, fn_chr, fn_cos,			/* 18..1B */
  fn_count, fn_deg, fn_eof, fn_erl,			/* 1C..1F */
  fn_err, fn_eval, fn_exp, fn_get,			/* 20..23 */
  fn_getdol, fn_inkey, fn_inkeydol, fn_instr,		/* 24..27 */
  fn_int, fn_len, fn_listofn, fn_ln,			/* 28..2B */
  fn_log, fn_openin, fn_openout, fn_openup, 		/* 2C..2F */
  fn_pi, fn_pointfn, fn_pos, fn_rad,			/* 30..33 */
  fn_reportdol, fn_retcode, fn_rnd, fn_sgn, 		/* 34..37 */
  fn_sin, fn_sqr, fn_str, fn_string,  			/* 38..3B */
  fn_sum, fn_tan, fn_tempofn, fn_usr, 			/* 3C..3F */
  fn_val, fn_verify, fn_vpos, fn_xlatedol		/* 40..43 */
};

/*
** 'exec_function' dispatches one of the built-in function routines
*/
void exec_function(void) {
  byte token = *(basicvars.current+1);
  basicvars.current+=2;
  if (token>TOKEN_XLATEDOL) bad_token();	/* Function token is out of range */
  (*function_table[token])();
}

/*
** 'init_functions' is called before running a program
*/
void init_functions(void) {
  lastrandom = 0x00575241;
  randomoverflow = 0;
}
