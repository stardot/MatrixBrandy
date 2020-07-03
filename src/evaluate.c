/*
** This file is part of the Matrix Brandy Basic VI Interpreter.
** Copyright (C) 2000-2014 David Daniels
** Copyright (C) 2018-2019 Michael McConnell and contributors
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
**	This file contains the interpreter's expression evaluation code
**	apart from the built-in Basic functions which are in functions.c
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <setjmp.h>
#include "common.h"
#include "target.h"
#include "basicdefs.h"
#include "tokens.h"
#include "variables.h"
#include "lvalue.h"
#include "strings.h"
#include "stack.h"
#include "errors.h"
#include "evaluate.h"
#include "statement.h"
#include "miscprocs.h"
#include "functions.h"
#include "keyboard.h"

/* #define DEBUG */

/*
** The expression evaluator forms the heart of the interpreter. Doing
** just about anything involves calling the functions in this module.
** The evaluation code uses two different methods for parsing code
** depending on which was the more convenient to use at the time. The
** dyadic operators are evaluated using operator precedence but recursive
** descent is used as well, for example, in function calls.
** Error handling: all errors are dealt with by calls to function 'error'.
** This function does not return (it does a 'siglongjmp' to a well-defined
** position in the interpreter)
*/

#ifdef TARGET_DJGPP
#define DJGPPLIMIT 75000		/* Minimum amount of C stack allowed in DJGPP version of program */
#endif

#define RADCONV 57.2957795130823229	/* Used when converting degrees -> radians and vice versa */
#define TIMEFORMAT "%a,%d %b %Y.%H:%M:%S"  /* Date format used by 'TIME$' */

#define OPSTACKMARK 0			/* 'Operator' used as sentinel at the base of the operator stack */

static float64 floatvalue;		/* Temporary for holding floating point values */
static int64 int64value;		/* Temporary for holding 64-bit integers */
/*
** Notes:
** 1) 'floatvalue' is used to hold floating point values in a number of the
** functions below. Whilst it might be more politically correct to declare
** it as a local variable where needed, this has a big impact on the
** performance of the code on a processor like the ARM. The reason for this
** is that the Acorn C compiler tries to using a floating point register
** for it. As the floating point instructions are emulated, including the
** ones to push and pop floating point values from the stack, this slows
** things down horribly. In fact it represented about a 30% overhead on the
** speed of this code! Floating point values therefore live in static
** variables.
**
** 2) 'if' statements are used in a number of places where perhaps 'switch'
** statements would be more natural. The reason for this is that the code
** that 'gcc' generates for small 'switch' statements (say two or three
** cases) is not very good. 'if' statements generate better code and allow
** the most common case, an integer, to be dealt with first rather than
** wherever the code generator for 'switch' statements puts it.
**
** 3) The Basic stack is manipulated using a combination of functions and
** macros. Anything starting with 'PUSH_', 'push_' or 'pop_' pushes or
** pops stack entries. The names of macros are always in upper case.
** Macros and functions are directly equivalent but macros are used wherever
** possible for speed.
*/

/* Operator priorities */

#define POWPRIO 0x700
#define MULPRIO 0x600
#define ADDPRIO 0x500
#define COMPRIO 0x400
#define ANDPRIO 0x300
#define ORPRIO 0x200
#define MARKPRIO 0

/* Operator identities (values used on operator stack) */

#define OP_NOP	0
#define OP_ADD	1
#define OP_SUB	2
#define OP_MUL	3
#define OP_MATMUL 4
#define OP_DIV	5
#define OP_INTDIV 6
#define OP_MOD	7
#define OP_POW	8
#define OP_LSL	9
#define OP_LSR	10
#define OP_ASR	11
#define OP_EQ	12
#define OP_NE	13
#define OP_GT	14
#define OP_LT	15
#define OP_GE	16
#define OP_LE	17
#define OP_AND	18
#define OP_OR	19
#define OP_EOR	20

#define OPCOUNT (OP_EOR+1)

#define OPERMASK 0xFF
#define PRIOMASK 0xFF00

#define PRIORITY(x) (x & PRIOMASK)

typedef void operator(void);


#if 0          //#ifdef DEBUG - This function is never referenced
/*
** 'show_result' is a debugging function used to display the result of an
** expression
*/
static void show_result(void) {
  if (basicvars.debug_flags.debug) {
    switch(GET_TOPITEM) {
    case STACK_INT:
      fprintf(stderr, "  Integer result, value=%d\n", basicvars.stacktop.intsp->intvalue);
      break;
    case STACK_FLOAT:
      fprintf(stderr, "  Floating point result, value=%g\n", basicvars.stacktop.floatsp->floatvalue);
      break;
    case STACK_STRING: case STACK_STRTEMP: {
      basicstring x;
      int32 n, limit;
      char *cp;
      x = basicvars.stacktop.stringsp->descriptor;
      fprintf(stderr, "  String result, length=%d, address=%p, value='", x.stringlen, x.stringaddr);
      cp = x.stringaddr;
      limit = (x.stringlen > 50 ? 50 : x.stringlen);
      for (n = 0; n < limit; n++) putchar(*cp++);	/* Print contents of string */
      if (limit != x.stringlen)
        fprintf(stderr, "...'\n");
      else {
        fprintf(stderr, "'\n");
      }
      break;
    }
    default:
      fprintf(stderr, "*** Bad entry on stack, type = %d ***\n", GET_TOPITEM);
    }
  }
}
#endif

/*
** 'eval_integer' evaluates a numeric expression where an integer value
** is required, returning the value
*/
int32 eval_integer(void) {
  expression();
  return pop_anynum32();
}

int64 eval_int64(void) {
  expression();
  return pop_anynum64();
}

/*
** 'eval_intfactor' evaluates a numeric factor where an integer is
** required. The function returns the value obtained.
*/
int32 eval_intfactor(void) {
  (*factor_table[*basicvars.current])();
  return pop_anynum32();
}

/*
** 'check_arrays' returns 'true' if the two arrays passed to it
** have the same number of dimensions and the bounds of each
** dimension are the same. It does not check the types of the
** the array elements
*/
boolean check_arrays(basicarray *p1, basicarray *p2) {
  int32 n;
  if (p1->dimcount != p2->dimcount) return FALSE;
  n = 0;
  while (n < p1->dimcount && p1->dimsize[n] == p2->dimsize[n]) n++;
  return n == p1->dimcount;
}

/*
** 'type_table' is used for checking the types of formal and
** actual procedure and function parameters. The first index gives
** the type of the formal parameter according to the variable type
** flags and the second is the type of the actual parameter as given
** by the type of its entry on top of the Basic stack. 'ERR_NONE'
** means that they are compatible. Anything else signifies that
** there is a type error or that the interpreter has gone wrong
*/
static int32 type_table [TYPECHECKMASK+1][STACK_LOCARRAY+1] = {
/* Undefined variable type (0) */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN},
/* Byte-sized integer (1) */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_NONE,    ERR_NONE,
  ERR_NONE,    ERR_NONE,    ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM},
/* Word-sized integer (2) */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_NONE,    ERR_NONE,
  ERR_NONE,    ERR_NONE,    ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM},
/* Floating point (3) */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_NONE,    ERR_NONE,
  ERR_NONE,    ERR_NONE,    ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM},
/* 'string$' type string (4) */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_PARMSTR, ERR_PARMSTR,
  ERR_PARMSTR, ERR_PARMSTR, ERR_NONE,    ERR_NONE,
  ERR_PARMSTR, ERR_PARMSTR, ERR_PARMSTR, ERR_PARMSTR,
  ERR_PARMSTR, ERR_PARMSTR, ERR_PARMSTR, ERR_PARMSTR,
  ERR_PARMSTR, ERR_PARMSTR, ERR_PARMSTR},
/* '$string' type string (5) */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_PARMSTR, ERR_PARMSTR,
  ERR_PARMSTR, ERR_PARMSTR, ERR_NONE,    ERR_NONE,
  ERR_PARMSTR, ERR_PARMSTR, ERR_PARMSTR, ERR_PARMSTR,
  ERR_PARMSTR, ERR_PARMSTR, ERR_PARMSTR, ERR_PARMSTR,
  ERR_PARMSTR, ERR_PARMSTR, ERR_PARMSTR},
/* 64-bit integer (6) */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_NONE,    ERR_NONE,
  ERR_NONE,    ERR_NONE,    ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM},
/* Unsigned 8-bit integer (7) */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_NONE,    ERR_NONE,
  ERR_NONE,    ERR_NONE,    ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM},
/* Undefined array type (8) */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN},
/* Byte-sized integer array (9) */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN},
/* Word-sized integer array (10) */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM,
  ERR_NONE,    ERR_NONE,    ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM},
/* Floating point array (11) */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_NONE,    ERR_NONE,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM},
/* 'string$' array (12) */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_PARMSTR, ERR_PARMSTR,
  ERR_PARMSTR, ERR_PARMSTR, ERR_PARMSTR, ERR_PARMSTR,
  ERR_PARMSTR, ERR_PARMSTR, ERR_PARMSTR, ERR_PARMSTR,
  ERR_PARMSTR, ERR_PARMSTR, ERR_PARMSTR, ERR_PARMSTR,
  ERR_NONE,    ERR_NONE,    ERR_PARMSTR},
/* Undefined array type (13) */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN},
/* 64-bit integer array type (14) */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM,
  ERR_NONE,    ERR_NONE,    ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM},
/* Unsigned 8-bit integer array type (15) */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_NONE,    ERR_NONE,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM},
};

/*
** 'push_oneparm' is called to deal with a PROC or FN parameter. It does this
** in two stages, evaluating the parameter first and after all the other have
** been processed, moving it to the variable being used as the formal parameter
*/
static void push_oneparm(formparm *fp, int32 parmno, char *procname) {
  int32 intparm = 0, typerr;
  uint8 uint8parm = 0;
  int64 int64parm = 0;
  float64 floatparm = 0;
  basicstring stringparm = {0, NULL};
  basicarray *arrayparm = NULL;
  lvalue retparm;
  stackitem parmtype = STACK_UNKNOWN;
  boolean isreturn;

#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function evaluate.c:push_oneparm\n");
#endif
  isreturn = (fp->parameter.typeinfo & VAR_RETURN) != 0;
  if (!isreturn) {	/* Normal parameter */
    expression();
    parmtype = GET_TOPITEM;
    if (parmtype == STACK_INT)        intparm = pop_int();
    else if (parmtype == STACK_UINT8) uint8parm = pop_uint8();
    else if (parmtype == STACK_INT64) int64parm = pop_int64();
    else if (parmtype == STACK_FLOAT) floatparm = pop_float();
    else if (parmtype == STACK_STRING || parmtype == STACK_STRTEMP)
      stringparm = pop_string();
    else if (parmtype >= STACK_INTARRAY && parmtype <= STACK_SATEMP)
      arrayparm = pop_array();
    else {
      error(ERR_BROKEN, __LINE__, "evaluate");
    }
  } else {	/* Return parameter */
    get_lvalue(&retparm);
    switch (retparm.typeinfo) {	/* Now fetch the parameter's value */
    case VAR_INTWORD:		/* Integer parameter */
      intparm = *retparm.address.intaddr;
      parmtype = STACK_INT;
      break;
    case VAR_UINT8:		/* Integer parameter */
      uint8parm = *retparm.address.uint8addr;
      parmtype = STACK_UINT8;
      break;
    case VAR_INTLONG:		/* Integer parameter */
      int64parm = *retparm.address.int64addr;
      parmtype = STACK_INT64;
      break;
    case VAR_FLOAT:		/* Floating point parameter */
      floatparm = *retparm.address.floataddr;
      parmtype = STACK_FLOAT;
      break;
    case VAR_STRINGDOL:		/* Normal string parameter */
      stringparm = *retparm.address.straddr;
      parmtype = STACK_STRING;
      break;
    case VAR_INTBYTEPTR:	/* Indirect byte-sized integer */
      check_write(retparm.address.offset, sizeof(byte));
      intparm = basicvars.offbase[retparm.address.offset];
      parmtype = STACK_INT;
      break;
    case VAR_INTWORDPTR:	/* Indirect word-sized integer */
      intparm = get_integer(retparm.address.offset);
      parmtype = STACK_INT;
      break;
    case VAR_FLOATPTR:		/* Indirect eight-byte floating point value */
      floatparm = get_float(retparm.address.offset);
      parmtype = STACK_FLOAT;
      break;
    case VAR_DOLSTRPTR:		/* Indirect string */
      check_write(retparm.address.offset, sizeof(byte));
      stringparm.stringlen = get_stringlen(retparm.address.offset);
      stringparm.stringaddr = CAST(&basicvars.offbase[retparm.address.offset], char *);
      parmtype = STACK_STRING;
      break;
    case VAR_INTARRAY:		/* Array of integers */
      arrayparm = *retparm.address.arrayaddr;
      parmtype = STACK_INTARRAY;
      break;
    case VAR_UINT8ARRAY:	/* Array of integers */
      arrayparm = *retparm.address.arrayaddr;
      parmtype = STACK_UINT8ARRAY;
      break;
    case VAR_INT64ARRAY:		/* Array of integers */
      arrayparm = *retparm.address.arrayaddr;
      parmtype = STACK_INT64ARRAY;
      break;
    case VAR_FLOATARRAY:		/* Array of floating point values */
      arrayparm = *retparm.address.arrayaddr;
      parmtype = STACK_FLOATARRAY;
      break;
    case VAR_STRARRAY:		/* Array of strings */
      arrayparm = *retparm.address.arrayaddr;
      parmtype = STACK_STRARRAY;
      break;
    default:		/* Bad parameter type */
      error(ERR_BROKEN, __LINE__, "evaluate");
    }
  }

/* Type check the parameter */

  typerr = type_table[fp->parameter.typeinfo & TYPECHECKMASK][parmtype];
  if (typerr != ERR_NONE) {
    if (typerr == ERR_BROKEN) error(ERR_BROKEN, __LINE__, "evaluate");
    error(typerr, parmno);
  }

/* Check for another parameter and process it if one is found */

  if (*basicvars.current == ',') {	/* More parameters to come - Point at start of next one */
    basicvars.current++;		/* SKip comma */
    if (*basicvars.current == ')') {
      error(ERR_SYNTAX);
    }
    if (fp->nextparm == NIL) {
      error(ERR_TOOMANY, procname);
    }
    push_oneparm(fp->nextparm, parmno+1, procname);
  }
  else if (*basicvars.current == ')') {	/* End of parameters */
    if (fp->nextparm != NIL) {	/* Have reached end of parameters but not the end of the parameter list */
      error(ERR_NOTENUFF, procname);
    }
    basicvars.current++;	/* Step past the ')' */
  }
  else {	/* Syntax error - ',' or ')' expected */
    error(ERR_CORPNEXT);
  }
/*
** Now move the parameter to the formal parameter variable, saving the
** variable's original value on the stack. In the case of a 'return'
** parameter, the address of the variable that will receive the returned
** value has to be saved as well.
*/
  if ((fp->parameter.typeinfo & PARMTYPEMASK) == VAR_INTWORD) {	/* Deal with most common case first */
    int32 *p = fp->parameter.address.intaddr;
    if (isreturn)
      save_retint(retparm, fp->parameter, *p);
    else {
      save_int(fp->parameter, *p);
    }
    switch(parmtype) {
      case STACK_INT:   *p = intparm; break;
      case STACK_UINT8: *p = uint8parm; break;
      case STACK_INT64:
        if (int64parm <= 0x7FFFFFFFll && int64parm >= 0xFFFFFFFF80000000ll)
          *p = (int32)int64parm;
        else
          error(ERR_RANGE);
        break;
      case STACK_FLOAT: *p = TOINT(floatparm); break;
      default: error(ERR_BROKEN, __LINE__, "evaluate");
    }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function evaluate.c:push_oneparm via VAR_INTWORD\n");
#endif
    return;
  }
/* Now deal with other parameter types */
  switch (fp->parameter.typeinfo & PARMTYPEMASK) {	/* Go by formal parameter type */
  case VAR_UINT8: {
    uint8 *p = fp->parameter.address.uint8addr;
    if (isreturn)
      save_retuint8(retparm, fp->parameter, *p);
    else {
      save_uint8(fp->parameter, *p);
    }
    switch(parmtype) {
      case STACK_INT:   *p = intparm; break;
      case STACK_UINT8: *p = uint8parm; break;
      case STACK_INT64: *p = int64parm; break;
      case STACK_FLOAT: *p = TOINT(floatparm); break;
      default: error(ERR_BROKEN, __LINE__, "evaluate");
    }
    break;
  }
  case VAR_INTLONG: {
    int64 *p = fp->parameter.address.int64addr;
    if (isreturn)
      save_retint64(retparm, fp->parameter, *p);
    else {
      save_int64(fp->parameter, *p);
    }
    switch(parmtype) {
      case STACK_INT:   *p = intparm; break;
      case STACK_UINT8: *p = uint8parm; break;
      case STACK_INT64: *p = int64parm; break;
      case STACK_FLOAT: *p = TOINT64(floatparm); break;
      default: error(ERR_BROKEN, __LINE__, "evaluate");
    }
    break;
  }
  case VAR_FLOAT: {		/* Floating point parameter */
    float64 *p = fp->parameter.address.floataddr;
    if (isreturn)
      save_retfloat(retparm, fp->parameter, *p);
    else {
      save_float(fp->parameter, *p);
    }
    switch(parmtype) {
      case STACK_INT:   *p = TOFLOAT(intparm); break;
      case STACK_UINT8: *p = TOFLOAT(uint8parm); break;
      case STACK_INT64: *p = TOFLOAT(int64parm); break;
      case STACK_FLOAT: *p = floatparm; break;
      default: error(ERR_BROKEN, __LINE__, "evaluate");
    }
    break;
  }
  case VAR_STRINGDOL: {	/* Normal string parameter */
    basicstring *p = fp->parameter.address.straddr;
    if (isreturn)
      save_retstring(retparm, fp->parameter, *p);
    else {
      save_string(fp->parameter, *p);
    }
    if (parmtype == STACK_STRING) {	/* Argument is a string variable - Have to copy string */
      p->stringlen = stringparm.stringlen;
      p->stringaddr = alloc_string(stringparm.stringlen);
      if (stringparm.stringlen > 0) memmove(p->stringaddr, stringparm.stringaddr, stringparm.stringlen);
    }
    else {	/* Argument is a string expression - Can use it directly */
      *p = stringparm;
    }
    break;
  }
  case VAR_INTBYTEPTR:	/* Indirect byte-sized integer */
    check_write(fp->parameter.address.offset, sizeof(byte));
    if (isreturn)
      save_retint(retparm, fp->parameter, basicvars.offbase[fp->parameter.address.offset]);
    else {
      save_int(fp->parameter, basicvars.offbase[fp->parameter.address.offset]);
    }
    basicvars.offbase[fp->parameter.address.offset] = parmtype == STACK_INT ? intparm : TOINT(floatparm);
    break;
  case VAR_INTWORDPTR:	/* Indirect word-sized integer */
    if (isreturn)
      save_retint(retparm, fp->parameter, get_integer(fp->parameter.address.offset));
    else {
      save_int(fp->parameter, get_integer(fp->parameter.address.offset));
    }
    store_integer(fp->parameter.address.offset, parmtype == STACK_INT ? intparm : TOINT(floatparm));
    break;
  case VAR_FLOATPTR:	/* Indirect eight-byte floating point intparm */
    if (isreturn)
      save_retfloat(retparm, fp->parameter, get_float(fp->parameter.address.offset));
    else {
      save_float(fp->parameter, get_float(fp->parameter.address.offset));
    }
    store_float(fp->parameter.address.offset, parmtype == STACK_INT ? TOFLOAT(intparm) : floatparm);
    break;
  case VAR_DOLSTRPTR: {	/* Indirect string */
    basicstring descriptor;
    byte *sp;
    check_write(fp->parameter.address.offset, stringparm.stringlen+1);
    sp = &basicvars.offbase[fp->parameter.address.offset];	/* This is too long to keep typing... */
/* Fake a descriptor for the original '$<string>' string */
    descriptor.stringlen = get_stringlen(fp->parameter.address.offset)+1;
    descriptor.stringaddr = alloc_string(descriptor.stringlen);
    if (descriptor.stringlen > 0)
     memmove(descriptor.stringaddr, sp, descriptor.stringlen);
    if (isreturn)
      save_retstring(retparm, fp->parameter, descriptor);	/* Save the '$<string>' string */
    else {
      save_string(fp->parameter, descriptor);	/* Save the '$<string>' string */
    }
    if (stringparm.stringlen > 0) memmove(sp, stringparm.stringaddr, stringparm.stringlen);
    sp[stringparm.stringlen] = asc_CR;
    if (parmtype == STACK_STRTEMP) free_string(stringparm);
    break;
  }
  case VAR_INTARRAY: case VAR_UINT8ARRAY: case VAR_FLOATARRAY: case VAR_STRARRAY:
    save_array(fp->parameter);
    *fp->parameter.address.arrayaddr = arrayparm;
    break;
  default:		/* Bad parameter type */
    error(ERR_BROKEN, __LINE__, "evaluate");
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function evaluate.c:push_oneparm\n");
#endif
}

/*
** 'push_singleparm' is called when a procedure or function has a single
** 32-bit integer parameter
*/
static void push_singleparm(formparm *fp, char *procname) {
  int32 intparm = 0;

#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function evaluate.c:push_singleparm\n");
#endif
  expression();
  if (*basicvars.current != ')') {	/* Try to put out a meaningful error message */
    if (*basicvars.current == ',')	/* Assume there is another parameter */
      error(ERR_TOOMANY, procname);
    else {	/* Something else - Assume ')' is missing */
      error(ERR_RPMISS);
    }
  }
  basicvars.current++;	/* Skip the ')' */
  intparm = pop_anynum32();
  save_int(fp->parameter, *(fp->parameter.address.intaddr));
  *(fp->parameter.address.intaddr) = intparm;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function evaluate.c:push_singleparm\n");
#endif
}

/*
** 'push_parameters' evaluates the parameters for a procedure or function
** call and moves them to their respective formal parameters. It returns a
** pointer to the first character after the parameters. On entry 'dp' points
** at the procedure's or functions definition structure, 'lp' at the start
** of the actual parameters and 'base' at the name of the procedure or
** function.
*/
void push_parameters(fnprocdef *dp, char *base) {
  basicvars.current++;	/* Skip the '(' */
  if (dp->simple)
    push_singleparm(dp->parmlist, base);
  else {
    push_oneparm(dp->parmlist, 1, base);
  }
}

/*
** 'do_staticvar' is called to deal with a simple reference to a static
** variable, that is, one that is not followed by an indirection operator
*/
static void do_staticvar(void) {
  PUSH_INT(basicvars.staticvars[*(basicvars.current+1)].varentry.varinteger);
  basicvars.current+=2;
}

/*
** 'do_statindvar' is one of the 'factor' functions. It deals with static
** variables that are followed by an indirection operator, pushing the
** value pointed at by the variable on to the Basic stack
*/
static void do_statindvar(void) {
  size_t address = basicvars.staticvars[*(basicvars.current+1)].varentry.varinteger;
  byte operator;
  basicvars.current+=2;
  operator = *basicvars.current;
  basicvars.current++;
  (*factor_table[*basicvars.current])();
  address += pop_anynum64();
/* Now load the value on to the Basic stack */
  if (operator == '?') {		/* Byte-sized integer */
    check_read(address, sizeof(byte));
    PUSH_INT(basicvars.offbase[address]);
  }
  else {		/* Word-sized integer */
    PUSH_INT(get_integer(address));
  }
}

/*
** 'do_intzero' pushes the integer value 0 on to the Basic stack
*/
static void do_intzero(void) {
  basicvars.current++;
  PUSH_INT(0);
}

/*
** 'do_intone' pushes the integer value 1 on to the Basic stack
*/
static void do_intone(void) {
  basicvars.current++;
  PUSH_INT(1);
}

/*
** 'do_smallconst' pushes a small integer value on to the Basic stack
*/
static void do_smallconst(void) {
  PUSH_INT(*(basicvars.current+1)+1);	/* +1 as values 1..256 are held as 0..255 */
  basicvars.current+=2;	/* Skip 'smallconst' token and value */
}

/*
** 'do_intconst' pushes a 32-bit integer constant on to the Basic stack
*/
static void do_intconst(void) {
  basicvars.current++;		/* Point current at binary version of number */
  PUSH_INT(GET_INTVALUE(basicvars.current));
  basicvars.current+=INTSIZE;
}

/*
** 'do_int64const' pushes a 64-bit integer constant on to the Basic stack
*/
static void do_int64const(void) {
  basicvars.current++;		/* Point current at binary version of number */
  PUSH_INT64(GET_INT64VALUE(basicvars.current));
  basicvars.current+=INT64SIZE;
}

/*
** 'do_floatzero' pushes the floating point value 0.0 on to the
** Basic stack
*/
static void do_floatzero(void) {
  basicvars.current++;
  PUSH_FLOAT(0.0);
}

/*
** 'do_floatone' pushes the floating point value 1.0 on to the
** Basic stack
*/
static void do_floatone(void) {
  basicvars.current++;
  PUSH_FLOAT(1.0);
}

/*
** 'do_floatconst' pushes the floating point value that follows
** the token on to the Basic stack
*/
static void do_floatconst(void) {
  PUSH_FLOAT(get_fpvalue(basicvars.current));
  basicvars.current+=(FLOATSIZE+1);
}

/*
** 'do_intvar' handles a simple reference to a known 32-bit integer
** variable, pushing its value on to the stack. The variable is known
** to be not followed by an indirection operator
*/
static void do_intvar(void) {
  int32 *ip;
  ip = GET_ADDRESS(basicvars.current, int32 *);
  basicvars.current+=LOFFSIZE+1;	/* Skip pointer */
  PUSH_INT(*ip);
}

/*
** 'do_uint8var' deals with simple references to a known unsigned
** 8-bit integer variable.
*/
static void do_uint8var(void) {
  uint8 *ip;
  ip = GET_ADDRESS(basicvars.current, uint8 *);
  basicvars.current+=LOFFSIZE+1;	/* Skip pointer */
  PUSH_UINT8(*ip);
}

/*
** 'do_int64var' deals with simple references to a known 64-bit
** integer variable.
*/
static void do_int64var(void) {
  int64 *ip;
  ip = GET_ADDRESS(basicvars.current, int64 *);
  basicvars.current+=LOFFSIZE+1;	/* Skip pointer */
  PUSH_INT64(*ip);
}

/*
** 'do_floatvar' deals with simple references to a known floating
** point variable
*/
static void do_floatvar(void) {
  float64 *fp;
  fp = GET_ADDRESS(basicvars.current, float64 *);
  basicvars.current+=LOFFSIZE+1;	/* Skip pointer */
  PUSH_FLOAT(*fp);
}

/*
** 'do_stringvar' handles references to a known string variable
*/
static void do_stringvar(void) {
  basicstring *sp;
  sp = GET_ADDRESS(basicvars.current, basicstring *);
  basicvars.current+=LOFFSIZE+1;	/* Skip pointer */
  PUSH_STRING(*sp);
}

/*
** 'do_arrayvar' handles references to entire arrays
*/
static void do_arrayvar(void) {
  variable *vp;
  vp = GET_ADDRESS(basicvars.current, variable *);
  basicvars.current+=LOFFSIZE+2;		/* Skip pointer to array and ')' */
  push_array(vp->varentry.vararray, vp->varflags);
}


/*
** 'do_arrayref' handles array references where an individual element is
** being accessed. It deals with both simple references to them and
** references followed by an indirection operator
*/
static void do_arrayref(void) {
  variable *vp;
  byte operator;
  int32 vartype, maxdims, index = 0, dimcount, element = 0;
  size_t offset = 0;
  basicarray *descriptor;

#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function evaluate.c:do\n");
#endif
  vp = GET_ADDRESS(basicvars.current, variable *);
  basicvars.current+=LOFFSIZE+1;	/* Skip pointer to variable */
  descriptor = vp->varentry.vararray;
  vartype = vp->varflags;
  if (descriptor->dimcount == 1) {	/* Array has only one dimension - Use faster code */
    expression();	      /* Evaluate an array index */
    element = pop_anynum32();
    if (element < 0 || element >= descriptor->dimsize[0]) error(ERR_BADINDEX, element, vp->varname);
  }
  else {	/* Multi-dimensional array */
    maxdims = descriptor->dimcount;
    dimcount = 0;
    element = 0;
    do {	/* Gather the array indexes */
      expression();	      /* Evaluate an array index */
      index = pop_anynum32();
      if (index < 0 || index >= descriptor->dimsize[dimcount]) error(ERR_BADINDEX, index, vp->varname);
      dimcount++;
      element+=index;
      if (*basicvars.current != ',') break;	/* No more array indexes expected */
      basicvars.current++;
      if (dimcount > maxdims) error(ERR_INDEXCO, vp->varname);	/* Too many dimensions */
      if (dimcount != maxdims) element = element*descriptor->dimsize[dimcount];
    } while (TRUE);
    if (dimcount != maxdims) error(ERR_INDEXCO, vp->varname);	/* Not enough dimensions */
  }
  if (*basicvars.current != ')') error(ERR_RPMISS);
  basicvars.current++;		/* Point at character after the ')' */
  if (*basicvars.current != '?' && *basicvars.current != '!') {	/* Ordinary array reference */
    if (vartype == VAR_INTARRAY) {	/* Can push the array element on to the stack then go home */
      PUSH_INT(vp->varentry.vararray->arraystart.intbase[element]);
      return;
    }
    if (vartype == VAR_UINT8ARRAY) {	/* Can push the array element on to the stack then go home */
      PUSH_UINT8(vp->varentry.vararray->arraystart.uint8base[element]);
      return;
    }
    if (vartype == VAR_INT64ARRAY) {	/* Can push the array element on to the stack then go home */
      PUSH_INT64(vp->varentry.vararray->arraystart.int64base[element]);
      return;
    }
    if (vartype == VAR_FLOATARRAY) {
      PUSH_FLOAT(vp->varentry.vararray->arraystart.floatbase[element]);
      return;
    }
    if (vartype == VAR_STRARRAY) {
      PUSH_STRING(vp->varentry.vararray->arraystart.stringbase[element]);
      return;
    }
    error(ERR_BROKEN, __LINE__, "evaluate");	/* Sanity check */
  }
  else {	/* Array reference is followed by an indirection operator */
    switch(vartype) {
      case VAR_INTARRAY:   offset = vp->varentry.vararray->arraystart.intbase[element]; break;
      case VAR_UINT8ARRAY: offset = vp->varentry.vararray->arraystart.uint8base[element]; break;
      case VAR_INT64ARRAY: offset = vp->varentry.vararray->arraystart.int64base[element]; break;
      case VAR_FLOATARRAY: offset = TOINT64(vp->varentry.vararray->arraystart.floatbase[element]); break;
      default: error(ERR_TYPENUM);
    }
    operator = *basicvars.current;
    basicvars.current++;
    (*factor_table[*basicvars.current])();
    offset+=pop_anynum64();
    if (operator == '?') {	/* Byte-sized integer */
      check_read(offset, sizeof(byte));
      push_int(basicvars.offbase[offset]);
    }
    else {		/* Word-sized integer */
      push_int(get_integer(offset));
    }
  }
}

/*
** 'do_indrefvar' handles references to dynamic variables that
** are followed by indirection operators
*/
static void do_indrefvar(void) {
  byte operator;
  size_t offset;
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function evaluate.c:do_indrefvar\n");
#endif
  if (*basicvars.current == BASIC_TOKEN_INTINDVAR) {	/* Fetch variable's value */
    offset = *GET_ADDRESS(basicvars.current, int32 *);
  } else if (*basicvars.current == BASIC_TOKEN_INT64INDVAR) {	/* Fetch variable's value */
    offset = *GET_ADDRESS(basicvars.current, int64 *);
  } else {
    offset = TOINT64(*GET_ADDRESS(basicvars.current, float64 *));
  }
  basicvars.current+=LOFFSIZE+1;		/* Skip pointer to variable */
  operator = *basicvars.current;
  basicvars.current++;
  (*factor_table[*basicvars.current])();
  offset+=pop_anynum64();
  if (operator == '?') {	/* Byte-sized integer */
    check_read(offset, sizeof(byte));
#ifdef USE_SDL
    if (offset >= matrixflags.mode7fb && offset <= (matrixflags.mode7fb + 1023)) {
      /* Mode 7 screen memory */
      offset = (offset - matrixflags.mode7fb) + (size_t)mode7frame;
    }
#endif /* USE_SDL */
    push_int(basicvars.offbase[offset]);
  }
  else {		/* Word-sized integer */
#ifdef USE_SDL
    if (offset >= matrixflags.mode7fb && offset <= (matrixflags.mode7fb + 1023)) {
      /* Mode 7 screen memory */
      offset = (offset - matrixflags.mode7fb) + (size_t)mode7frame;
    }
#endif
    push_int(get_integer(offset));
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function evaluate.c:do_indrefvar\n");
#endif
}

/*
** 'do_xvar' is called to deal with a reference to a variable that
** has not been seen before. It locates the variable, stores its
** address in the tokenised code and changes the variable's type
** token before branching to the routine for that type for variable
*/
static void do_xvar(void) {
  byte *np, *base;
  variable *vp;
  int32 vartype;
  boolean isarray;

#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function evaluate.c:do_xvar\n");
#endif
  base = get_srcaddr(basicvars.current);		/* Point 'base' at the start of the variable's name */
  np = skip_name(base);
  vp = find_variable(base, np-base);
  if (vp == NIL) {	/* Cannot find the variable */
    if (*(np-1) == '(' || *(np-1) == '[')
      error(ERR_ARRAYMISS, tocstring(CAST(base, char *), np-base));	/* Unknown array */
    else {
      error(ERR_VARMISS, tocstring(CAST(base, char *), np-base));	/* Unknown variable */
    }
  }
  vartype = vp->varflags;
  isarray = (vartype & VAR_ARRAY) != 0;
  if (isarray && vp->varentry.vararray == NIL) error(ERR_NODIMS, vp->varname);	/* Array not dimensioned */
  np = basicvars.current+LOFFSIZE+1;
  if (!isarray && (*np == '?' || *np == '!')) {		/* Variable is followed by an indirection operator */
    switch (vartype) {
    case VAR_INTWORD:
      *basicvars.current = BASIC_TOKEN_INTINDVAR;
      set_address(basicvars.current, &vp->varentry.varinteger);
      break;
    case VAR_INTLONG:
      *basicvars.current = BASIC_TOKEN_INT64INDVAR;
      set_address(basicvars.current, &vp->varentry.var64int);
      break;
    case VAR_FLOAT:
      *basicvars.current = BASIC_TOKEN_FLOATINDVAR;
      set_address(basicvars.current, &vp->varentry.varfloat);
      break;
    case VAR_UINT8: error(ERR_UNSUITABLEVAR);
    default: error(ERR_VARNUM);
    }
    do_indrefvar();
  }
  else {	/* Simple reference to variable or reference to an array */
    if (vartype == VAR_INTWORD) {
      *basicvars.current = BASIC_TOKEN_INTVAR;
      set_address(basicvars.current, &vp->varentry.varinteger);
      do_intvar();
    }
    else if (vartype == VAR_UINT8) {
      *basicvars.current = BASIC_TOKEN_UINT8VAR;
      set_address(basicvars.current, &vp->varentry.varu8int);
      do_uint8var();
    }
    else if (vartype == VAR_INTLONG) {
      *basicvars.current = BASIC_TOKEN_INT64VAR;
      set_address(basicvars.current, &vp->varentry.var64int);
      do_int64var();
    }
    else if (vartype == VAR_FLOAT) {
      *basicvars.current = BASIC_TOKEN_FLOATVAR;
      set_address(basicvars.current, &vp->varentry.varfloat);
      do_floatvar();
    }
    else if (vartype == VAR_STRINGDOL) {
      *basicvars.current = BASIC_TOKEN_STRINGVAR;
      set_address(basicvars.current, &vp->varentry.varstring);
      do_stringvar();
    }
    else {	/* Array or array followed by an indirection operator */
      if (*np == ')') {	/* Reference is to entire array */
        *basicvars.current = BASIC_TOKEN_ARRAYVAR;
        set_address(basicvars.current, vp);
        do_arrayvar();
      }
      else {	/* Reference is to an array element */
        *basicvars.current = BASIC_TOKEN_ARRAYREF;
        set_address(basicvars.current, vp);
        do_arrayref();
      }
    }
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function evaluate.c:do_xvar\n");
#endif
}

/*
** 'do_stringcon' pushes a descriptor for a simple string constant on
** to the Basic stack
*/
static void do_stringcon(void) {
  basicstring descriptor;
  descriptor.stringaddr = TOSTRING(get_srcaddr(basicvars.current));
  descriptor.stringlen = GET_SIZE(basicvars.current+1+OFFSIZE);
  basicvars.current+=1+OFFSIZE+SIZESIZE;
  PUSH_STRING(descriptor);
}

/*
** 'do_qstringcom' handles string constants where the string contains
** '""' pairs. The '""' have to be replaced with '"' when the string
** is put on the Basic stack
*/
static void do_qstringcon(void) {
  int32 length, srce, dest;
  byte *string;
  char *cp;
  string = get_srcaddr(basicvars.current);
  length = GET_SIZE(basicvars.current+1+OFFSIZE);
  basicvars.current+=1+OFFSIZE+SIZESIZE;
  cp = alloc_string(length);
  if (length > 0) {
    srce = 0;
    for (dest = 0; dest < length; dest++) {
      cp[dest] = string[srce];
      if (string[srce] == '"') srce++;	/* Skip one '"' of '""' */
      srce++;
    }
  }
  push_strtemp(length, cp);
}

/*
** 'do_brackets' is called when a '(' is founf to handle the
**  expression in the brackets
*/
static void do_brackets(void) {
  basicvars.current++;	/* Skip the '(' */
  expression();
  if (*basicvars.current != ')') error(ERR_RPMISS);
  basicvars.current++;
}

/*
** 'do_unaryplus' handles the unary '+' operator. This is a
** no-op apart from type checking
*/
static void do_unaryplus(void) {
  basicvars.current++;		/* Skip '+' */
  (*factor_table[*basicvars.current])();
  if ((GET_TOPITEM != STACK_INT) && (GET_TOPITEM != STACK_UINT8) && (GET_TOPITEM != STACK_INT64) && (GET_TOPITEM != STACK_FLOAT)) error(ERR_TYPENUM);
}

/*
** 'do_unaryminus' negates the value on top of the stack
*/
static void do_unaryminus(void) {
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function evaluate.c:do_unaryminus\n");
#endif
  basicvars.current++;		/* Skip '-' */
  (*factor_table[*basicvars.current])();
  switch(GET_TOPITEM) {
    case STACK_INT:   NEGATE_INT; break;
    case STACK_UINT8: error(ERR_UNSUITABLEVAR); break;
    case STACK_INT64: NEGATE_INT64; break;
    case STACK_FLOAT: NEGATE_FLOAT; break;
    default: error(ERR_TYPENUM);
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function evaluate.c:do_unaryminus\n");
#endif
}

/*
** 'do_getbyte' handles the byte indirection operator, '?', pushing
** the byte addressed by the numeric value on top of the stack on to
** the stack
*/
static void do_getbyte(void) {
  size_t offset = 0;
  basicvars.current++;		/* Skip '?' */
  (*factor_table[*basicvars.current])();
  offset = (size_t)pop_anynum64();
  check_read(offset, sizeof(byte));
#ifdef USE_SDL
  if (offset >= matrixflags.mode7fb && offset <= (matrixflags.mode7fb + 1023)) {
    /* Mode 7 screen memory */
    offset = (offset - matrixflags.mode7fb) + (size_t)mode7frame;
  }
#endif /* USE_SDL */
  push_int(basicvars.offbase[offset]);
}

/*
** 'do_getword' handles the word indirection operator, '!', pushing the
** word addressed by the numeric value on top of the Basic stack on to
** the stack. The address of the word to be pushed is byte-aligned
*/
static void do_getword(void) {
  size_t offset = 0;
  basicvars.current++;		/* Skip '!' */
  (*factor_table[*basicvars.current])();
  offset = (size_t)pop_anynum64();
#ifdef USE_SDL
  if (offset >= matrixflags.mode7fb && offset <= (matrixflags.mode7fb + 1023)) {
    /* Mode 7 screen memory */
    offset = (offset - matrixflags.mode7fb) + (size_t)mode7frame;
  }
#endif
  push_int(get_integer(offset));
}

/*
** 'do_getstring' handles the unary string indirection operator, '$'.
** It pushes a descriptor for the CR-terminated string addressed by
** the numeric value on top of the stack on to the stack. Note that
** if no 'CR' character is found within 65536 characters of the start
** of the string, a null string is pushed on to the stack.
*/
static void do_getstring(void) {
  size_t offset = 0;
  int32 len;
  basicvars.current++;		/* Skip '$' */
  (*factor_table[*basicvars.current])();
  offset = pop_anynum64();
#ifdef USE_SDL
  if (offset >= matrixflags.mode7fb && offset <= (matrixflags.mode7fb + 1023)) {
    /* Mode 7 screen memory */
    offset = (offset - matrixflags.mode7fb) + (size_t)mode7frame;
  }
#endif /* USE_SDL */
  len = get_stringlen(offset);
  check_read(offset, len);
  push_dolstring(len, CAST(&basicvars.offbase[offset], char *));
}

/*
** 'do_getfloat' handles the unary floating point indirection operator, '|'.
** It pushes the value addressed by the numeric value on top of the stack on
** to the Basic stack
*/
static void do_getfloat(void) {
  size_t offset = 0;
  basicvars.current++;		/* Skip '|' */
  (*factor_table[*basicvars.current])();
  offset = pop_anynum64();
#ifdef USE_SDL
  if (offset >= matrixflags.mode7fb && offset <= (matrixflags.mode7fb + 1023)) {
    /* Mode 7 screen memory */
    offset = (offset - matrixflags.mode7fb) + (size_t)mode7frame;
  }
#endif /* USE_SDL */
  push_float(get_float(offset));
}

/*
** 'do_function' calls a user-defined function.
** Functions are called in the middle of expressions so control has
** to return here at the end of the call. It therefore makes a
** recursive call to 'exec_fnstatements' to deal with the body of the
** function. The function also sets up a new operator stack so that
** there is no problem with the operator stack overflowing on deeply
** nested function calls. It also simplifies managing the stack.
**
** One thing to note is that each function call needs its own environment
** block that 'siglongjmp' can use when returning control to the program
** when handling an error when 'ON ERROR LOCAL' is being used to trap
** errors. The existing environment block has to be saved. What the
** program does is allocate a block on the stack for each environment
** block with a pointer to that block held with the rest of the Basic
** variables in 'basicvars'. The existing pointer is saved by the call
** 'push_fn'. Note that 'push_fn' also saves the operator stack pointer.
**
** The DJGPP version of the program includes a check for the amount of
** C stack left in this function. This is needed as there are no checks
** for stack overflow in this environment (the gcc option '-fstack-check'
** does not seem to work) and it is possible for the stack to overwrite
** the interpreter's data and cause a crash. As this is where problems
** are most likely to show up (in deeply nested function calls in Basic
** programs) there is an explicit check for the amount of stack left. If
** it is less than 75K then an error is flagged (setting the limit lower
** than this seems to lead to crashes still)
*/
static void do_function(void) {
  byte *tp;
  fnprocdef *dp;
  variable *vp;
#ifdef NEWKBD
  if (kbd_escpoll()) error(ERR_ESCAPE);
#else
  if (basicvars.escape) error(ERR_ESCAPE);
#endif
#ifdef TARGET_DJGPP
  if (stackavail()<DJGPPLIMIT) error(ERR_STACKFULL);
#endif
  vp = GET_ADDRESS(basicvars.current, variable *);
  dp = vp->varentry.varfnproc;
  basicvars.current+=LOFFSIZE+1;	/* Skip pointer to function */

/* Now deal with the arguments of the function call */

  if (*basicvars.current == '(') push_parameters(dp, vp->varname);

/* Save everything */

  push_fn(vp->varname, dp->parmcount);
  tp = basicvars.current;

/* Lastly, create a new operator stack and call the function */

  basicvars.opstop = make_opstack();
  basicvars.opstlimit = basicvars.opstop+OPSTACKSIZE;
  basicvars.local_restart = make_restart();
  if (basicvars.traces.enabled) {
    if (basicvars.traces.procs) trace_proc(vp->varname, TRUE);
    if (basicvars.traces.branches) trace_branch(basicvars.current, dp->fnprocaddr);
  }
  if (sigsetjmp(*basicvars.local_restart, 1) == 0)
    exec_fnstatements(dp->fnprocaddr);
  else {
/*
** Restart here after an error in the function or something
** called from it is trapped by ON ERROR LOCAL
*/
    reset_opstack();
    exec_fnstatements(basicvars.error_handler.current);
  }

/* Restore stuff after the call has ended */

  basicvars.current = tp;	/* Note that 'basicvars.current' is preserved over the function call in 'tp' */
}

/*
** 'do_xfunction' is called to handle the first time a reference
** to a function is found
*/
static void do_xfunction(void) {
  byte *base, *tp;
  fnprocdef *dp;
  variable *vp;
  boolean gotparms;
  base = get_srcaddr(basicvars.current);		/* Point 'base' at start of function's name */
  if (*base != BASIC_TOKEN_FN) error(ERR_NOTAFN);	/* Ensure a function is being called */
  tp = skip_name(base);
  gotparms = *(tp-1) == '(';
  if (gotparms) tp--;	/* '(' found but it is not part of name */
  vp = find_fnproc(base, tp-base);
  dp = vp->varentry.varfnproc;
  *basicvars.current = BASIC_TOKEN_FNPROCALL;
  set_address(basicvars.current, vp);
  if (gotparms) {	/* PROC/FN call has some parameters */
    if (dp->parmlist == NIL) error(ERR_TOOMANY, vp->varname);	/* Got a '(' but function has no parameters */
  }
  else {	/* No parameters found */
    if (dp->parmlist != NIL) error(ERR_NOTENUFF, vp->varname);	/* But function should have them */
  }
  do_function();	/* Call the function */
}

/* =============== Operators =============== */

/*
** 'want_number' is called when a numeric stack entry type is needed
** but an entry of another type was found instead
*/
static void want_number(void) {
  stackitem baditem;
  baditem = GET_TOPITEM;
  if (baditem==STACK_STRING || baditem==STACK_STRTEMP)		/* Numeric operand required */
    error(ERR_TYPENUM);
  else if (baditem>STACK_UNKNOWN && baditem <= STACK_SATEMP)	/* Operator is not defined for this operand type */
    error(ERR_BADARITH);
  else {	/* Unrecognisable operand - Stack is probably corrupt */
    fprintf(stderr, "Baditem = %d, sp = %p, safe=%p, opstop=%p\n", baditem,
     basicvars.stacktop.bytesp, basicvars.safestack.bytesp, basicvars.opstop);
    error(ERR_BROKEN, __LINE__, "evaluate");
  }
}

/*
** 'want_string' is called when a string stack entry type is needed
** but an entry of another type was found instead
*/
static void want_string(void) {
  stackitem baditem;
  baditem = GET_TOPITEM;
  if (baditem == STACK_INT || baditem == STACK_UINT8 || baditem == STACK_INT64 || baditem == STACK_FLOAT)		/* String operand required */
    error(ERR_TYPESTR);
  else if (baditem > STACK_UNKNOWN && baditem <= STACK_SATEMP)	/* Operator is not defined for this operand type */
    error(ERR_BADARITH);
  else {	/* Unrecognisable operand - Stack is probably corrupt */
    error(ERR_BROKEN, __LINE__, "evaluate");
  }
}

/*
** 'want_array' is called when an array stack entry type is required
** but an entry of another type is found
*/
static void want_array(void) {
  error(ERR_VARARRAY);
}

/*
** 'eval_badcall' is called when an invalid stack entry type is found
*/
static void eval_badcall(void) {
  error(ERR_BROKEN, __LINE__, "evaluate");
}

/*
** 'make_array' creates a temporary array to hold the results of an array
** operation, allocating memory for it on the Basic stack. It also creates
** the array descriptor and pushes that on to the stack as well. It returns
** a pointer to the start of the array body. All the calling code has to
** do is fill in the values in the array on the stack
*/
static void *make_array(int32 arraytype, basicarray* original) {
  basicarray result;
  void *base = NULL;
  result = *original;

#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function evaluate.c:make_array with arraytpe=0x%X\n", arraytype);
#endif
  switch (arraytype) {
  case VAR_INTWORD:
    base = alloc_stackmem(original->arrsize*sizeof(int32));
    result.arraystart.intbase = base;
    break;
  case VAR_UINT8:
    base = alloc_stackmem(original->arrsize*sizeof(uint8));
    result.arraystart.uint8base = base;
    break;
  case VAR_INTLONG:
    base = alloc_stackmem(original->arrsize*sizeof(int64));
    result.arraystart.int64base = base;
    break;
  case VAR_FLOAT:
    base = alloc_stackmem(original->arrsize*sizeof(float64));
    result.arraystart.floatbase = base;
    break;
  case VAR_STRINGDOL:
    base = alloc_stackmem(original->arrsize*sizeof(basicstring));
    result.arraystart.stringbase = base;
    break;
  default:
    error(ERR_BROKEN, __LINE__, "evaluate");		/* Passed bad array type */
  }
  if (base == NIL) error(ERR_NOROOM);	/* Not enough room on stack to create array */
  push_arraytemp(&result, arraytype);
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exit function evaluate.c:make_array with base=0x%p\n", base);
#endif
  return base;
}

/*
** 'eval_ivplus' deals with addition when the right-hand operand is
** any integer value. All versions of the operator are dealt with
** by this function
*/
static void eval_ivplus(void) {
  stackitem lhitem, rhitem;
  int64 rhint = 0;
  
  rhitem = GET_TOPITEM;
  rhint=pop_anyint();
  lhitem = GET_TOPITEM;
  if (TOPITEMISINT) {
    push_varyint(pop_anyint() + rhint);
  } else if (lhitem == STACK_FLOAT)
    INCR_FLOAT(TOFLOAT(rhint));	/* float+int - Update value on stack in place */
  else if (lhitem == STACK_INTARRAY || lhitem == STACK_UINT8ARRAY || lhitem == STACK_INT64ARRAY || lhitem == STACK_FLOATARRAY) {	/* <array>+<integer value> */
    basicarray *lharray;
    int32 n, count;
    lharray = pop_array();
    count = lharray->arrsize;
    if (lhitem == STACK_INTARRAY) {
      if (rhitem == STACK_INT64) {
        int64 *srce, *base = make_array(VAR_INTLONG, lharray);
        srce = lharray->arraystart.int64base;
        for (n = 0; n < count; n++) base[n] = srce[n]+rhint;
      } else {
        int32 *srce, *base = make_array(VAR_INTWORD, lharray);
        srce = lharray->arraystart.intbase;
        for (n = 0; n < count; n++) base[n] = (int32)(srce[n]+rhint);
      }
    } else if (lhitem == STACK_UINT8ARRAY) {
      if (rhitem == STACK_INT) {
        uint8 *srce, *base = make_array(VAR_UINT8, lharray);
        srce = lharray->arraystart.uint8base;
        for (n = 0; n < count; n++) base[n] = (uint8)(srce[n]+rhint);
      } else if (rhitem == STACK_UINT8) {
        int32 *srce, *base = make_array(VAR_INTWORD, lharray);
        srce = lharray->arraystart.intbase;
        for (n = 0; n < count; n++) base[n] = (int32)(srce[n]+rhint);
      } else { /* STACK_INT64 */
        int64 *srce, *base = make_array(VAR_INTLONG, lharray);
        srce = lharray->arraystart.int64base;
        for (n = 0; n < count; n++) base[n] = srce[n]+rhint;
      }
    } else if (lhitem == STACK_INT64ARRAY) {
      int64 *srce, *base = make_array(VAR_INTLONG, lharray);
      srce = lharray->arraystart.int64base;
      for (n = 0; n < count; n++) base[n] = srce[n]+rhint;
    } else {
      float64 *srce, *base = make_array(VAR_FLOAT, lharray);
      floatvalue = TOFLOAT(rhint);
      srce = lharray->arraystart.floatbase;
      for (n = 0; n < count; n++) base[n] = srce[n]+floatvalue;
    }
  } else if (lhitem == STACK_FATEMP) {	/* <float array>+<integer value> */
    basicarray lharray;
    float64 *base;
    int32 n, count;
    lharray = pop_arraytemp();
    base = lharray.arraystart.floatbase;
    count = lharray.arrsize;
    floatvalue = TOFLOAT(rhint);
    for (n = 0; n < count; n++) base[n]+=floatvalue;
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
}

/*
** 'eval_fvplus' deals with addition when the right-hand operand is
** a floating point value. All versions of the operator are dealt with
** by this function
*/
static void eval_fvplus(void) {
  stackitem lhitem;
  floatvalue = pop_float();	/* Top item on Basic stack is right-hand operand */
  lhitem = GET_TOPITEM;
  if (TOPITEMISINT) {
    floatvalue+=TOFLOAT(pop_anyint());	/* This has to be split otherwise the macro */
    PUSH_FLOAT(floatvalue);		/* expansion of PUSH_FLOAT goes wrong */
  } else if (lhitem == STACK_FLOAT)
    INCR_FLOAT(floatvalue);
  else if (lhitem == STACK_INTARRAY || lhitem == STACK_UINT8ARRAY || lhitem == STACK_INT64ARRAY || lhitem == STACK_FLOATARRAY) {	/* <array>+<float value> */
    basicarray *lharray;
    float64 *base;
    int32 n, count;
    lharray = pop_array();
    count = lharray->arrsize;
    base = make_array(VAR_FLOAT, lharray);
    if (lhitem == STACK_INTARRAY) {
      int32 *srce = lharray->arraystart.intbase;
      for (n = 0; n < count; n++) base[n] = TOFLOAT(srce[n])+floatvalue;
    } else if (lhitem == STACK_UINT8ARRAY) {
      uint8 *srce = lharray->arraystart.uint8base;
      for (n = 0; n < count; n++) base[n] = TOFLOAT(srce[n])+floatvalue;
    } else if (lhitem == STACK_INT64ARRAY) {
      int64 *srce = lharray->arraystart.int64base;
      for (n = 0; n < count; n++) base[n] = TOFLOAT(srce[n])+floatvalue;
    } else {
      float64 *srce = lharray->arraystart.floatbase;
      for (n = 0; n < count; n++) base[n] = srce[n]+floatvalue;
    }
  } else if (lhitem == STACK_FATEMP) {	/* <float array>+<float value> */
    basicarray lharray;
    float64 *base;
    int32 n, count;
    lharray = pop_arraytemp();
    base = lharray.arraystart.floatbase;
    count = lharray.arrsize;
    for (n = 0; n < count; n++) base[n]+=floatvalue;
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
}

/*
** 'eval_svplus' is called when the right-hand operand is a string. The
** only legal case is string concatenation
*/
static void eval_svplus(void) {
  stackitem lhitem, rhitem;
  basicstring lhstring, rhstring;
  int32 newlen;
  char *cp;
  rhitem = GET_TOPITEM;
  rhstring = pop_string();
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_STRING || lhitem == STACK_STRTEMP) {
    if (rhstring.stringlen == 0) return;	/* Do nothing if right-hand string is of zero length */
    lhstring = pop_string();
    newlen = lhstring.stringlen+rhstring.stringlen;
    if (newlen > MAXSTRING) error(ERR_STRINGLEN);
    if (lhitem == STACK_STRTEMP) {	/* Reuse left-hand string as it is a temporary */
      cp = resize_string(lhstring.stringaddr, lhstring.stringlen, newlen);
      lhstring.stringaddr = cp;
      memmove(cp+lhstring.stringlen, rhstring.stringaddr, rhstring.stringlen);
    } else {	/* Any other case - Create a new string temporary */
      cp = alloc_string(newlen);
      memmove(cp, lhstring.stringaddr, lhstring.stringlen);
      memmove(cp+lhstring.stringlen, rhstring.stringaddr, rhstring.stringlen);
    }
    if (rhitem == STACK_STRTEMP) free_string(rhstring);
    push_strtemp(newlen, cp);
  } else if (lhitem == STACK_STRARRAY) {	/* <array>+<string> */
    basicarray *lharray;
    basicstring *base, *srce;
    int32 n, count;
    if (rhstring.stringlen == 0) return;	/* Do nothing if right-hand string is of zero length */
    lharray = pop_array();
    count = lharray->arrsize;
    srce = lharray->arraystart.stringbase;
    base = make_array(VAR_STRINGDOL, lharray);
    for (n = 0; n < count; n++) {		/* Append right hand string to each element of string array */
      newlen = srce[n].stringlen+rhstring.stringlen;
      if (newlen > MAXSTRING) error(ERR_STRINGLEN);
      cp = alloc_string(newlen);
      memmove(cp, srce[n].stringaddr, srce[n].stringlen);
      memmove(cp+srce[n].stringlen, rhstring.stringaddr, rhstring.stringlen);
      base[n].stringaddr = cp;
      base[n].stringlen = newlen;
    }
    if (rhitem == STACK_STRTEMP) free_string(rhstring);
  } else want_string();
}

/*
** 'eval_iaplus' deals with addition when the right-hand operand is
** a 32-bit integer array. All versions of the operator are dealt with
** by this function
*/
static void eval_iaplus(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n, count;
  int32 *rhsrce;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.intbase;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT || lhitem == STACK_UINT8) {
    int32 lhint32 = pop_anyint();
    int32 *base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) base[n] = lhint32+rhsrce[n];
  } else if (lhitem == STACK_INT64) {
    int64 lhint64 = pop_int64();
    int64 *base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) base[n] = lhint64+rhsrce[n];
  } else if (lhitem == STACK_FLOAT) {	/* <float>+<int array> */
    float64 *base;
    floatvalue = pop_float();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) base[n] = floatvalue+TOFLOAT(rhsrce[n]);
  } else if (lhitem == STACK_INTARRAY) {	/* <int array>+<int array> */
    int32 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.intbase;
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) base[n] = lhsrce[n]+rhsrce[n];
  } else if (lhitem == STACK_UINT8ARRAY) {	/* <uint8 array>+<int array> */
    int32 *base;
    uint8 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.uint8base;
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) base[n] = lhsrce[n]+rhsrce[n];
  } else if (lhitem == STACK_INT64ARRAY) {	/* <int64 array>+<int array> */
    int64 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.int64base;
    base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) base[n] = lhsrce[n]+rhsrce[n];
  } else if (lhitem == STACK_FLOATARRAY) {	/* <float array>+<int array> */
    float64 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) base[n] = lhsrce[n]+TOFLOAT(rhsrce[n]);
  } else if (lhitem == STACK_FATEMP) {		/* <float array>+<int array> */
    float64 *lhsrce;
    basicarray lharray = pop_arraytemp();
    if (!check_arrays(&lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray.arraystart.floatbase;
    for (n = 0; n < count; n++) lhsrce[n]+=TOFLOAT(rhsrce[n]);
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
}

/*
** 'eval_iu8aplus' deals with addition when the right-hand operand is
** an unsigned 8-bit integer array. All versions of the operator are
** dealt with by this function
*/
static void eval_iu8aplus(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n, count;
  uint8 *rhsrce;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.uint8base;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT) {
    int32 lhint32 = pop_int();
    int32 *base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) base[n] = lhint32+rhsrce[n];
  } else if (lhitem == STACK_UINT8) {
    int32 lhint32 = pop_uint8();
    uint8 *base = make_array(VAR_UINT8, rharray);
    for (n = 0; n < count; n++) base[n] = lhint32+rhsrce[n];
  } else if (lhitem == STACK_INT64) {
    int64 lhint64 = pop_int64();
    int64 *base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) base[n] = lhint64+rhsrce[n];
  } else if (lhitem == STACK_FLOAT) {	/* <float>+<int array> */
    float64 *base;
    floatvalue = pop_float();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) base[n] = floatvalue+TOFLOAT(rhsrce[n]);
  } else if (lhitem == STACK_INTARRAY) {	/* <int array>+<uint8 array> */
    int32 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.intbase;
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) base[n] = lhsrce[n]+rhsrce[n];
  } else if (lhitem == STACK_UINT8ARRAY) {	/* <uint8 array>+<uint8 array> */
    uint8 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.uint8base;
    base = make_array(VAR_UINT8, rharray);
    for (n = 0; n < count; n++) base[n] = lhsrce[n]+rhsrce[n];
  } else if (lhitem == STACK_INT64ARRAY) {	/* <int64 array>+<uint8 array> */
    int64 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.int64base;
    base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) base[n] = lhsrce[n]+rhsrce[n];
  } else if (lhitem == STACK_FLOATARRAY) {	/* <float array>+<uint8 array> */
    float64 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) base[n] = lhsrce[n]+TOFLOAT(rhsrce[n]);
  } else if (lhitem == STACK_FATEMP) {		/* <float array>+<int array> */
    float64 *lhsrce;
    basicarray lharray = pop_arraytemp();
    if (!check_arrays(&lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray.arraystart.floatbase;
    for (n = 0; n < count; n++) lhsrce[n]+=TOFLOAT(rhsrce[n]);
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
}

/*
** 'eval_iaplus' deals with addition when the right-hand operand is
** a 64-bit integer array. All versions of the operator are dealt with
** by this function
*/
static void eval_i64aplus(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n, count;
  int64 *rhsrce;
  int64 lhint=0;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.int64base;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT || lhitem ==  STACK_UINT8 || lhitem == STACK_INT64) {
    int64 *base;
    lhitem=pop_anyint();
    base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) base[n] = lhint+rhsrce[n];
  } else if (lhitem == STACK_FLOAT) {	/* <float>+<int array> */
    float64 *base;
    floatvalue = pop_float();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) base[n] = floatvalue+TOFLOAT(rhsrce[n]);
  } else if (lhitem == STACK_INTARRAY) {	/* <int array>+<int64 array> */
    int64 *base;
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.intbase;
    base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) base[n] = lhsrce[n]+rhsrce[n];
  } else if (lhitem == STACK_UINT8ARRAY) {	/* <uint8 array>+<int64 array> */
    int64 *base;
    uint8 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.uint8base;
    base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) base[n] = lhsrce[n]+rhsrce[n];
  } else if (lhitem == STACK_INT64ARRAY) {	/* <int64 array>+<int64 array> */
    int64 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.int64base;
    base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) base[n] = lhsrce[n]+rhsrce[n];
  } else if (lhitem == STACK_FLOATARRAY) {	/* <float array>+<int64 array> */
    float64 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) base[n] = lhsrce[n]+TOFLOAT(rhsrce[n]);
  } else if (lhitem == STACK_FATEMP) {		/* <float array>+<int array> */
    float64 *lhsrce;
    basicarray lharray = pop_arraytemp();
    if (!check_arrays(&lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray.arraystart.floatbase;
    for (n = 0; n < count; n++) lhsrce[n]+=TOFLOAT(rhsrce[n]);
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
}

/*
** 'eval_faplus' deals with addition when the right-hand operand is
** a floating point array. All versions of the operator are dealt with
** by this function
*/
static void eval_faplus(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n, count;
  float64 *base, *rhsrce;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.floatbase;
  lhitem = GET_TOPITEM;
  if (TOPITEMISNUM) {	/* <int or float>+<float array> or <uint8>+<float array> */
    floatvalue = pop_anynumfp();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) base[n] = floatvalue+rhsrce[n];
  } else if (lhitem == STACK_INTARRAY) {	/* <int array>+<float array> */
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.intbase;
    for (n = 0; n < count; n++) base[n] = TOFLOAT(lhsrce[n])+rhsrce[n];
  } else if (lhitem == STACK_UINT8ARRAY) {	/* <uint8 array>+<float array> */
    uint8 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.uint8base;
    for (n = 0; n < count; n++) base[n] = TOFLOAT(lhsrce[n])+rhsrce[n];
  } else if (lhitem == STACK_INT64ARRAY) {	/* <int64 array>+<float array> */
    int64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.int64base;
    for (n = 0; n < count; n++) base[n] = TOFLOAT(lhsrce[n])+rhsrce[n];
  } else if (lhitem == STACK_FLOATARRAY) {	/* <float array>+<float array> */
    float64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) base[n] = lhsrce[n]+rhsrce[n];
  } else if (lhitem == STACK_FATEMP) {		/* <float array>+<float array> */
    float64 *lhsrce;
    basicarray lharray = pop_arraytemp();
    if (!check_arrays(&lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray.arraystart.floatbase;
    for (n = 0; n < count; n++) lhsrce[n]+=rhsrce[n];
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
}

/*
** 'eval_saplus' deals with addition when the right-hand operand is
** a string array. All versions of the operator are dealt with
** by this function
*/
static void eval_saplus(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n, count;
  basicstring *base, *rhsrce;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.stringbase;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_STRING || lhitem == STACK_STRTEMP) {
    int32 newlen;
    char *cp;
    basicstring lhstring = pop_string();
    if (lhstring.stringlen == 0) {	/* Do nothing if left-hand string is of zero length */
      push_array(rharray, VAR_STRINGDOL);
      return;
    }
    base = make_array(VAR_STRINGDOL, rharray);
    for (n = 0; n < count; n++) {		/* Prepend left-hand string to each element of string array */
      newlen = rhsrce[n].stringlen + lhstring.stringlen;
      if (newlen > MAXSTRING) error(ERR_STRINGLEN);
      cp = alloc_string(newlen);
      memmove(cp, lhstring.stringaddr, lhstring.stringlen);
      memmove(cp + lhstring.stringlen, rhsrce[n].stringaddr, rhsrce[n].stringlen);
      base[n].stringaddr = cp;
      base[n].stringlen = newlen;
    }
    if (lhitem == STACK_STRTEMP) free_string(lhstring);
  }
  else if (lhitem == STACK_STRARRAY) {	/* <string array>+<string array> */
    char *cp;
    int32 newlen;
    basicstring *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_STRINGDOL, rharray);
    lhsrce = lharray->arraystart.stringbase;
    for (n = 0; n < count; n++) {		/* Prepend left-hand string to each element of string array */
      newlen = lhsrce[n].stringlen + rhsrce[n].stringlen;
      if (newlen > MAXSTRING) error(ERR_STRINGLEN);
      cp = alloc_string(newlen);
      memmove(cp, lhsrce[n].stringaddr, lhsrce[n].stringlen);
      memmove(cp + lhsrce[n].stringlen, rhsrce[n].stringaddr, rhsrce[n].stringlen);
      base[n].stringaddr = cp;
      base[n].stringlen = newlen;
    }
  }
  else if (lhitem == STACK_SATEMP) {	/* <string array>+<string array> */
    char *cp;
    int32 newlen;
    basicstring *lhsrce;
    basicarray lharray = pop_arraytemp();
    if (!check_arrays(&lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray.arraystart.stringbase;
    for (n = 0; n < count; n++) {		/* Concatenate left-hand and right-hand strings of each array element */
      newlen = lhsrce[n].stringlen + rhsrce[n].stringlen;
      if (newlen > MAXSTRING) error(ERR_STRINGLEN);
      cp = resize_string(lhsrce[n].stringaddr, lhsrce[n].stringlen, newlen);
      memmove(cp + lhsrce[n].stringlen, rhsrce[n].stringaddr, rhsrce[n].stringlen);
      lhsrce[n].stringaddr = cp;
      lhsrce[n].stringlen = newlen;
    }
    push_arraytemp(&lharray, VAR_STRINGDOL);
  }
  else {
    want_string();
  }
}

/*
** 'eval_ivminus' deals with subtraction when the right-hand operand is
** any integer value. All versions of the operator are dealt with
** by this function
*/
static void eval_ivminus(void) {
  stackitem lhitem, rhitem;
  int64 rhint = 0;

  rhitem = GET_TOPITEM;
  rhint = pop_anyint();
  lhitem = GET_TOPITEM;
  if (TOPITEMISINT) {	/* Branch according to type of left-hand operand */
    push_varyint(pop_anyint() - rhint);
  } else if (lhitem == STACK_FLOAT)
    DECR_FLOAT(TOFLOAT(rhint));
  else if (lhitem == STACK_INTARRAY || lhitem == STACK_UINT8ARRAY || lhitem == STACK_INT64ARRAY || lhitem == STACK_FLOATARRAY) {	/* <array>-<integer value> */
    basicarray *lharray;
    int32 n, count;
    lharray = pop_array();
    count = lharray->arrsize;
    if (lhitem == STACK_INTARRAY) {
      if (rhitem == STACK_INT64) {
        int64 *srce, *base = make_array(VAR_INTLONG, lharray);
        srce = lharray->arraystart.int64base;
        for (n = 0; n < count; n++) base[n] = srce[n]-rhint;
      } else {
        int32 *srce, *base = make_array(VAR_INTWORD, lharray);
        srce = lharray->arraystart.intbase;
        for (n = 0; n < count; n++) base[n] = (int32)(srce[n]-rhint);
      }
    } else if (lhitem == STACK_UINT8ARRAY) {
      if (rhitem == STACK_INT) {
        uint8 *srce, *base = make_array(VAR_UINT8, lharray);
        srce = lharray->arraystart.uint8base;
        for (n = 0; n < count; n++) base[n] = (uint8)(srce[n]-rhint);
      } else if (rhitem == STACK_UINT8) {
        int32 *srce, *base = make_array(VAR_INTWORD, lharray);
        srce = lharray->arraystart.intbase;
        for (n = 0; n < count; n++) base[n] = (int32)(srce[n]-rhint);
      } else { /* STACK_INT64 */
        int64 *srce, *base = make_array(VAR_INTLONG, lharray);
        srce = lharray->arraystart.int64base;
        for (n = 0; n < count; n++) base[n] = srce[n]-rhint;
      }
    } else if (lhitem == STACK_INT64ARRAY) {
      int64 *srce, *base = make_array(VAR_INTLONG, lharray);
      srce = lharray->arraystart.int64base;
      for (n = 0; n < count; n++) base[n] = srce[n] - rhint;
    } else {
      float64 *srce, *base = make_array(VAR_FLOAT, lharray);
      floatvalue = TOFLOAT(rhint);
      srce = lharray->arraystart.floatbase;
      for (n = 0; n < count; n++) base[n] = srce[n] - floatvalue;
    }
  } else if (lhitem == STACK_FATEMP) {	/* <float array>-<integer value> */
    basicarray lharray;
    float64 *base;
    int32 n, count;
    lharray = pop_arraytemp();
    base = lharray.arraystart.floatbase;
    count = lharray.arrsize;
    floatvalue = TOFLOAT(rhint);
    for (n = 0; n < count; n++) base[n] -= floatvalue;
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
}

/*
** 'eval_fvminus' deals with subtraction when the right-hand operand is
** a floating point value
*/
static void eval_fvminus(void) {
  stackitem lhitem;
  floatvalue = pop_float();
  lhitem = GET_TOPITEM;
  if (TOPITEMISINT) {	/* <int>-<float> */
    floatvalue = TOFLOAT(pop_anyint()) - floatvalue;
    PUSH_FLOAT(floatvalue);
  } else if (lhitem == STACK_FLOAT)
    DECR_FLOAT(floatvalue);
  else if (lhitem == STACK_INTARRAY || lhitem == STACK_INT64ARRAY || lhitem == STACK_FLOATARRAY) {	/* <array>-<float value> */
    basicarray *lharray;
    float64 *base;
    int32 n, count;
    lharray = pop_array();
    count = lharray->arrsize;
    base = make_array(VAR_FLOAT, lharray);
    if (lhitem == STACK_INTARRAY) {
      int32 *srce = lharray->arraystart.intbase;
      for (n = 0; n < count; n++) base[n] = TOFLOAT(srce[n]) - floatvalue;
    } else if (lhitem == STACK_UINT8ARRAY) {
      uint8 *srce = lharray->arraystart.uint8base;
      for (n = 0; n < count; n++) base[n] = TOFLOAT(srce[n]) - floatvalue;
    } else if (lhitem == STACK_INT64ARRAY) {
      int64 *srce = lharray->arraystart.int64base;
      for (n = 0; n < count; n++) base[n] = TOFLOAT(srce[n]) - floatvalue;
    } else {
      float64 *srce = lharray->arraystart.floatbase;
      for (n = 0; n < count; n++) base[n] = srce[n] - floatvalue;
    }
  } else if (lhitem == STACK_FATEMP) {	/* <float array>-<float value> */
    basicarray lharray;
    float64 *base;
    int32 n, count;
    lharray = pop_arraytemp();
    base = lharray.arraystart.floatbase;
    count = lharray.arrsize;
    for (n = 0; n < count; n++) base[n] -= floatvalue;
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
}

/*
** 'eval_iaminus' deals with subtraction when the right-hand operand is
** a 32-bit integer array. All versions of the operator are dealt with
** by this function
*/
static void eval_iaminus(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n, count;
  int32 *rhsrce;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.intbase;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT || lhitem == STACK_UINT8) {			/* <int>-<int array> */
    int32 lhint = pop_anyint();
    int32 *base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) base[n] = lhint - rhsrce[n];
  } else if (lhitem == STACK_INT64) {		/* <int64>-<int array> */
    int64 lhint = pop_int64();
    int64 *base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) base[n] = lhint - rhsrce[n];
  } else if (lhitem == STACK_FLOAT) {		/* <float>-<int array> */
    float64 *base;
    floatvalue = pop_float();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) base[n] = floatvalue - TOFLOAT(rhsrce[n]);
  } else if (lhitem == STACK_INTARRAY) {	/* <int array>-<int array> */
    int32 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.intbase;
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) base[n] = lhsrce[n] - rhsrce[n];
  } else if (lhitem == STACK_UINT8ARRAY) {	/* <uint8 array>-<int array> */
    int32 *base;
    uint8 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.uint8base;
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) base[n] = lhsrce[n] - rhsrce[n];
  } else if (lhitem == STACK_INT64ARRAY) {	/* <int64 array>-<int array> */
    int64 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.int64base;
    base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) base[n] = lhsrce[n] - rhsrce[n];
  } else if (lhitem == STACK_FLOATARRAY) {	/* <float array>-<int array> */
    float64 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) base[n] = lhsrce[n] - TOFLOAT(rhsrce[n]);
  } else if (lhitem == STACK_FATEMP) {		/* <float array>-<int array> */
    float64 *lhsrce;
    basicarray lharray = pop_arraytemp();
    if (!check_arrays(&lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray.arraystart.floatbase;
    for (n = 0; n < count; n++) lhsrce[n] -= TOFLOAT(rhsrce[n]);
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
}

/*
** 'eval_iu8aminus' deals with subtraction when the right-hand operand is
** an unsigned 8-bit integer array. All versions of the operator are
** dealt with by this function
*/
static void eval_iu8aminus(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n, count;
  uint8 *rhsrce;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.uint8base;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT) {			/* <int>-<uint8 array> */
    int32 lhint = pop_int();
    int32 *base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) base[n] = lhint - rhsrce[n];
  } else if (lhitem == STACK_UINT8) {		/* <uint8>-<uint8 array> */
    uint8 lhint = pop_uint8();
    uint8 *base = make_array(VAR_UINT8, rharray);
    for (n = 0; n < count; n++) base[n] = lhint - rhsrce[n];
  } else if (lhitem == STACK_INT64) {		/* <int64>-<uint8 array> */
    int64 lhint = pop_int64();
    int64 *base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) base[n] = lhint - rhsrce[n];
  } else if (lhitem == STACK_FLOAT) {		/* <float>-<uint8 array> */
    float64 *base;
    floatvalue = pop_float();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) base[n] = floatvalue - TOFLOAT(rhsrce[n]);
  } else if (lhitem == STACK_INTARRAY) {	/* <int array>-<uint8 array> */
    int32 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.intbase;
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) base[n] = lhsrce[n] - rhsrce[n];
  } else if (lhitem == STACK_UINT8ARRAY) {	/* <uint8 array>-<uint8 array> */
    uint8 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.uint8base;
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) base[n] = lhsrce[n] - rhsrce[n];
  } else if (lhitem == STACK_INT64ARRAY) {	/* <int64 array>-<uint8 array> */
    int64 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.int64base;
    base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) base[n] = lhsrce[n] - rhsrce[n];
  } else if (lhitem == STACK_FLOATARRAY) {	/* <float array>-<uint8 array> */
    float64 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) base[n] = lhsrce[n] - TOFLOAT(rhsrce[n]);
  } else if (lhitem == STACK_FATEMP) {		/* <float array>-<uint8 array> */
    float64 *lhsrce;
    basicarray lharray = pop_arraytemp();
    if (!check_arrays(&lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray.arraystart.floatbase;
    for (n = 0; n < count; n++) lhsrce[n] -= TOFLOAT(rhsrce[n]);
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
}

/*
** 'eval_ia64minus' deals with subtraction when the right-hand operand is
** a 64-bit integer array. All versions of the operator are dealt with
** by this function
*/
static void eval_i64aminus(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n, count;
  int64 *rhsrce;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.int64base;
  lhitem = GET_TOPITEM;
  if (TOPITEMISINT) {			/* <any int>-<int64 array> */
    int64 lhint = pop_anyint();
    int64 *base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) base[n] = lhint - rhsrce[n];
  } else if (lhitem == STACK_FLOAT) {		/* <float>-<int64 array> */
    float64 *base;
    floatvalue = pop_float();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) base[n] = floatvalue - TOFLOAT(rhsrce[n]);
  } else if (lhitem == STACK_INTARRAY) {	/* <int array>-<int64 array> */
    int64 *base;
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.intbase;
    base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) base[n] = lhsrce[n] - rhsrce[n];
  } else if (lhitem == STACK_UINT8ARRAY) {	/* <uint8 array>-<int64 array> */
    int64 *base;
    uint8 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.uint8base;
    base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) base[n] = lhsrce[n] - rhsrce[n];
  } else if (lhitem == STACK_INT64ARRAY) {	/* <int array>-<int64 array> */
    int64 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.int64base;
    base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) base[n] = lhsrce[n] - rhsrce[n];
  } else if (lhitem == STACK_FLOATARRAY) {	/* <float array>-<int64 array> */
    float64 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) base[n] = lhsrce[n] - TOFLOAT(rhsrce[n]);
  } else if (lhitem == STACK_FATEMP) {		/* <float array>-<int64 array> */
    float64 *lhsrce;
    basicarray lharray = pop_arraytemp();
    if (!check_arrays(&lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray.arraystart.floatbase;
    for (n = 0; n < count; n++) lhsrce[n] -= TOFLOAT(rhsrce[n]);
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
}

/*
** 'eval_faminus' deals with subtraction when the right-hand operand is
** a floating point array. All versions of the operator are dealt with
** by this function
*/
static void eval_faminus(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n, count;
  float64 *base, *rhsrce;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.floatbase;
  lhitem = GET_TOPITEM;
  if (TOPITEMISNUM) {	/* <int or float>-<float array> or <uint8>-<float array> */
    floatvalue = pop_anynumfp();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) base[n] = floatvalue - rhsrce[n];
  } else if (lhitem == STACK_INTARRAY) {			/* <int array>+<float array> */
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.intbase;
    for (n = 0; n < count; n++) base[n] = TOFLOAT(lhsrce[n]) - rhsrce[n];
  } else if (lhitem == STACK_UINT8ARRAY) {			/* <uint8 array>+<float array> */
    uint8 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.uint8base;
    for (n = 0; n < count; n++) base[n] = TOFLOAT(lhsrce[n]) - rhsrce[n];
  } else if (lhitem == STACK_INT64ARRAY) {			/* <int64 array>+<float array> */
    int64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.int64base;
    for (n = 0; n < count; n++) base[n] = TOFLOAT(lhsrce[n]) - rhsrce[n];
  } else if (lhitem == STACK_FLOATARRAY) {			/* <float array>-<float array> */
    float64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) base[n] = lhsrce[n] - rhsrce[n];
  } else if (lhitem == STACK_FATEMP) {				/* <float array>-<float array> */
    float64 *lhsrce;
    basicarray lharray = pop_arraytemp();
    if (!check_arrays(&lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray.arraystart.floatbase;
    for (n = 0; n < count; n++) lhsrce[n] -= rhsrce[n];
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
}

/*
** 'eval_ivmul' handles multiplication where the right-hand operand is
** a 32-bit integer.
** Note that in order to catch an integer overflow, the operands have
** to be converted to floating point before they are multiplied so that
** the result can be checked to see if it is in range still. There
** should be no problem here as long as the number of bits in the
** mantissa of the floating point number exceeds the number of bits
** in an integer
*/
static void eval_ivmul(void) {
  stackitem lhitem, rhitem;
  int64 rhint = 0, lhint = 0, intres=0;
  float64 floatres = 0;
  rhitem = GET_TOPITEM;
  rhint = pop_anyint();

  lhitem = GET_TOPITEM;
  if (TOPITEMISINT) {	/* Now look at left-hand operand */
    lhint = pop_anyint();
    intres=lhint*rhint;
    floatres=(TOFLOAT(lhint)*TOFLOAT(rhint));
    if (fabs(floatres) > MAXINT64VAL)
      push_float(floatres);
    else
      push_varyint(intres);
  } else if (lhitem == STACK_FLOAT)
    push_float(pop_float()*TOFLOAT(rhint));
  else if (lhitem == STACK_INTARRAY || lhitem == STACK_UINT8ARRAY || lhitem == STACK_INT64ARRAY || lhitem == STACK_FLOATARRAY) {	/* <array>*<integer value> */
    basicarray *lharray;
    int32 n, count;
    int64 int64value;
    lharray = pop_array();
    count = lharray->arrsize;
    if (lhitem == STACK_INTARRAY) {			/* <int array>*<intX> */
      int32 *srce;
      if (rhitem == STACK_INT64) {
        int64 *base;
        base = make_array(VAR_INTLONG, lharray);
        srce = lharray->arraystart.intbase;
        for (n = 0; n < count; n++) {
          floatvalue = TOFLOAT(srce[n])*TOFLOAT(rhint);	/* This is going to be slow */
          if (fabs(floatvalue) <= TOFLOAT(MAXINT64VAL))
            base[n] = srce[n]*rhint;			/* Redo in int64-space as at the higher end float loses precision */
          else {					/* Value is out of range for a 64-bit integer */
            error(ERR_RANGE);
          }
        }
      } else { /* STACK_INT and STACK_UINT8 */
        int32 *base;
        base = make_array(VAR_INTWORD, lharray);
        srce = lharray->arraystart.intbase;
        for (n = 0; n < count; n++) {
          int64value = srce[n]*rhint;
          if (llabs(int64value) <= MAXINTVAL)
            base[n] = int64value;
          else {					/* Value is out of range for an integer */
            error(ERR_RANGE);
          }
        }
      }
    } else if (lhitem == STACK_UINT8ARRAY) {			/* <int array>*<intX> */
      uint8 *srce;
      if (rhitem == STACK_INT) {
        int32 *base;
        base = make_array(VAR_INTLONG, lharray);
        srce = lharray->arraystart.uint8base;
        for (n = 0; n < count; n++) {
          floatvalue = TOFLOAT(srce[n])*TOFLOAT(rhint);	/* This is going to be slow */
          if (fabs(floatvalue) <= TOFLOAT(MAXINT64VAL))
            base[n] = srce[n]*rhint;			/* Redo in int64-space as at the higher end float loses precision */
          else {					/* Value is out of range for a 64-bit integer */
            error(ERR_RANGE);
          }
        }
      } else if (rhitem == STACK_INT64) {
        int64 *base;
        base = make_array(VAR_INTLONG, lharray);
        srce = lharray->arraystart.uint8base;
        for (n = 0; n < count; n++) {
          floatvalue = TOFLOAT(srce[n])*TOFLOAT(rhint);	/* This is going to be slow */
          if (fabs(floatvalue) <= TOFLOAT(MAXINT64VAL))
            base[n] = srce[n]*rhint;			/* Redo in int64-space as at the higher end float loses precision */
          else {					/* Value is out of range for a 64-bit integer */
            error(ERR_RANGE);
          }
        }
      } else { /* STACK_UINT8 */
        uint8 *base;
        base = make_array(VAR_UINT8, lharray);
        srce = lharray->arraystart.uint8base;
        for (n = 0; n < count; n++) {
          base[n] = srce[n]*rhint;
        }
      }
    } else if (lhitem == STACK_INT64ARRAY) {		/* <int64 array>*<intX> */
      int64 *srce, *base;
      base = make_array(VAR_INTLONG, lharray);
      srce = lharray->arraystart.int64base;
      for (n = 0; n < count; n++) {
        floatvalue = TOFLOAT(srce[n])*TOFLOAT(rhint);	/* This is going to be slow */
        if (fabs(floatvalue) <= TOFLOAT(MAXINT64VAL))
          base[n] = srce[n]*rhint;			/* Redo in int64-space as at the higher end float loses precision */
        else {		/* Value is out of range for an integer */
          error(ERR_RANGE);
        }
      }
    } else {	/* <float array>*<integer> */
      float64 *srce, *base = make_array(VAR_FLOAT, lharray);
      floatvalue = TOFLOAT(rhint);
      srce = lharray->arraystart.floatbase;
      for (n = 0; n < count; n++) base[n] = srce[n] * floatvalue;
    }
  } else if (lhitem == STACK_FATEMP) {	/* <float array>*<integer value> */
    basicarray lharray;
    float64 *base;
    int32 n, count;
    lharray = pop_arraytemp();
    base = lharray.arraystart.floatbase;
    count = lharray.arrsize;
    floatvalue = TOFLOAT(rhint);
    for (n = 0; n < count; n++) base[n]*=floatvalue;
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
}

/*
** 'eval_fvmul' handles multiplication where the right-hand operand is
** a floating point value
*/
static void eval_fvmul(void) {
  stackitem lhitem;
  floatvalue = pop_float();
  lhitem = GET_TOPITEM;
  if (TOPITEMISNUM)			/* Now branch according to type of left-hand operand */
    push_float(pop_anynumfp()*floatvalue);
  else if (lhitem == STACK_INTARRAY || lhitem == STACK_UINT8ARRAY || lhitem == STACK_INT64ARRAY || lhitem == STACK_FLOATARRAY) {	/* <array>*<float value> */
    basicarray *lharray;
    float64 *base;
    int32 n, count;
    lharray = pop_array();
    count = lharray->arrsize;
    base = make_array(VAR_FLOAT, lharray);
    if (lhitem == STACK_INTARRAY) {
      int32 *srce = lharray->arraystart.intbase;
      for (n = 0; n < count; n++) base[n] = TOFLOAT(srce[n]) * floatvalue;
    } else if (lhitem == STACK_UINT8ARRAY) {
      uint8 *srce = lharray->arraystart.uint8base;
      for (n = 0; n < count; n++) base[n] = TOFLOAT(srce[n]) * floatvalue;
    } else if (lhitem == STACK_INT64ARRAY) {
      int64 *srce = lharray->arraystart.int64base;
      for (n = 0; n < count; n++) base[n] = TOFLOAT(srce[n]) * floatvalue;
    } else {
      float64 *srce = lharray->arraystart.floatbase;
      for (n = 0; n < count; n++) base[n] = srce[n] * floatvalue;
    }
  } else if (lhitem == STACK_FATEMP) {	/* <float array>*<float value> */
    basicarray lharray;
    float64 *base;
    int32 n, count;
    lharray = pop_arraytemp();
    base = lharray.arraystart.floatbase;
    count = lharray.arrsize;
    for (n = 0; n < count; n++) base[n]*=floatvalue;
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
}

/*
** 'eval_iamul' handles multiplication where the right-hand operand is
** a 32-bit integer array
*/
static void eval_iamul(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n, count;
  int32 *rhsrce;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.intbase;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT || lhitem == STACK_UINT8) {	/* <int32/uint8 value>*<integer array> */
    int32 *base;
    int64 lhint64 = pop_anyint();
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      int64value = lhint64*rhsrce[n];
      if (llabs(int64value) <= MAXINTVAL)
        base[n] = (int32)int64value;
      else {		/* Result is out of range for an integer */
        error(ERR_RANGE);
      }
    }
  } else if (lhitem == STACK_INT64) {			/* <int64 value>*<integer array> */
    int64 *base;
    int64 lhint64;
    lhint64=pop_int64();
    base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) {
      floatvalue = TOFLOAT(lhint64)*TOFLOAT(rhsrce[n]);
      if (fabs(floatvalue) <= MAXINT64VAL)
        base[n] = lhint64*rhsrce[n];
      else {		/* Result is out of range for an integer */
        error(ERR_RANGE);
      }
    }
  } else if (lhitem == STACK_FLOAT) {			/* <float>*<int array> */
    float64 *base;
    floatvalue = pop_float();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) base[n] = floatvalue * TOFLOAT(rhsrce[n]);
  } else if (lhitem == STACK_INTARRAY) {		/* <int array>*<int array> */
    int32 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.intbase;
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      int64value = (int64)lhsrce[n] * (int64)rhsrce[n];
      if (llabs(int64value) <= MAXINTVAL)
        base[n] = (int32)int64value;
      else {		/* Result is out of range for an integer */
        error(ERR_RANGE);
      }
    }
  } else if (lhitem == STACK_UINT8ARRAY) {		/* <uint8 array>*<int array> */
    int32 *base;
    uint8 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.uint8base;
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      int64value = (int64)lhsrce[n] * (int64)rhsrce[n];
      if (llabs(int64value) <= MAXINTVAL)
        base[n] = (int32)int64value;
      else {		/* Result is out of range for an integer */
        error(ERR_RANGE);
      }
    }
  } else if (lhitem == STACK_INT64ARRAY) {		/* <int64 array>*<int array> */
    int64 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.int64base;
    base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) {
      int64value = lhsrce[n] * (int64)rhsrce[n];
      if (llabs(int64value) <= MAXINT64VAL)
        base[n] = int64value;
      else {		/* Result is out of range for an integer */
        error(ERR_RANGE);
      }
    }
  } else if (lhitem == STACK_FLOATARRAY) {	/* <float array>*<int array> */
    float64 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) base[n] = lhsrce[n] * TOFLOAT(rhsrce[n]);
  } else if (lhitem == STACK_FATEMP) {		/* <float array>*<int array> */
    float64 *lhsrce;
    basicarray lharray = pop_arraytemp();
    if (!check_arrays(&lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray.arraystart.floatbase;
    for (n = 0; n < count; n++) lhsrce[n] *= TOFLOAT(rhsrce[n]);
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
}

/*
** 'eval_iu8amul' handles multiplication where the right-hand operand is
** an unsigned 8-bit integer array
*/
static void eval_iu8amul(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n, count;
  uint8 *rhsrce;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.uint8base;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT || lhitem == STACK_UINT8) {	/* <int32/uint8 value>*<integer array> */
    int32 *base;
    int64 lhint64 = pop_anyint();
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      int64value = lhint64*rhsrce[n];
      if (llabs(int64value) <= MAXINTVAL)
        base[n] = (int32)int64value;
      else {		/* Result is out of range for an integer */
        error(ERR_RANGE);
      }
    }
  } else if (lhitem == STACK_INT64) {			/* <int64 value>*<integer array> */
    int64 *base;
    int64 lhint64;
    lhint64=pop_int64();
    base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) {
      floatvalue = TOFLOAT(lhint64)*TOFLOAT(rhsrce[n]);
      if (fabs(floatvalue) <= MAXINT64VAL)
        base[n] = lhint64*rhsrce[n];
      else {		/* Result is out of range for an integer */
        error(ERR_RANGE);
      }
    }
  } else if (lhitem == STACK_FLOAT) {			/* <float>*<int array> */
    float64 *base;
    floatvalue = pop_float();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) base[n] = floatvalue * TOFLOAT(rhsrce[n]);
  } else if (lhitem == STACK_INTARRAY) {		/* <int array>*<int array> */
    int32 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.intbase;
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      int64value = (int64)lhsrce[n] * (int64)rhsrce[n];
      if (llabs(int64value) <= MAXINTVAL)
        base[n] = (int32)int64value;
      else {		/* Result is out of range for an integer */
        error(ERR_RANGE);
      }
    }
  } else if (lhitem == STACK_UINT8ARRAY) {		/* <uint8 array>*<int array> */
    int32 *base;
    uint8 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.uint8base;
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      int64value = (int64)lhsrce[n] * (int64)rhsrce[n];
      if (llabs(int64value) <= MAXINTVAL)
        base[n] = (int32)int64value;
      else {		/* Result is out of range for an integer */
        error(ERR_RANGE);
      }
    }
  } else if (lhitem == STACK_INT64ARRAY) {		/* <int64 array>*<int array> */
    int64 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.int64base;
    base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) {
      int64value = lhsrce[n] * (int64)rhsrce[n];
      if (llabs(int64value) <= MAXINT64VAL)
        base[n] = int64value;
      else {		/* Result is out of range for an integer */
        error(ERR_RANGE);
      }
    }
  } else if (lhitem == STACK_FLOATARRAY) {	/* <float array>*<int array> */
    float64 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) base[n] = lhsrce[n] * TOFLOAT(rhsrce[n]);
  } else if (lhitem == STACK_FATEMP) {		/* <float array>*<int array> */
    float64 *lhsrce;
    basicarray lharray = pop_arraytemp();
    if (!check_arrays(&lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray.arraystart.floatbase;
    for (n = 0; n < count; n++) lhsrce[n] *= TOFLOAT(rhsrce[n]);
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
}

/*
** 'eval_i64amul' handles multiplication where the right-hand operand is
** a 64-bit integer array
*/
static void eval_i64amul(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n, count;
  int64 *rhsrce;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.int64base;
  lhitem = GET_TOPITEM;
  if (TOPITEMISINT) {	/* <int32/uint8 value>*<int64 array> */
    int64 *base;
    int64 lhint64 = pop_anyint();
    base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) {
      int64value = lhint64*rhsrce[n];
      if (llabs(int64value) <= MAXINT64VAL)
        base[n] = int64value;
      else {		/* Result is out of range for an integer */
        error(ERR_RANGE);
      }
    }
  } else if (lhitem == STACK_FLOAT) {	/* <float>*<int64 array> */
    float64 *base;
    floatvalue = pop_float();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) base[n] = floatvalue * TOFLOAT(rhsrce[n]);
  } else if (lhitem == STACK_INTARRAY) {	/* <int array>*<int64 array> */
    int64 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.int64base;
    base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) {
      int64value = (int64)lhsrce[n] * rhsrce[n];
      if (llabs(int64value) <= MAXINT64VAL)
        base[n] = int64value;
      else {		/* Result is out of range for an integer */
        error(ERR_RANGE);
      }
    }
  } else if (lhitem == STACK_UINT8ARRAY) {	/* <uint8 array>*<int64 array> */
    int64 *base;
    uint8 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.uint8base;
    base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) {
      int64value = (int64)lhsrce[n] * rhsrce[n];
      if (llabs(int64value) <= MAXINT64VAL)
        base[n] = int64value;
      else {		/* Result is out of range for an integer */
        error(ERR_RANGE);
      }
    }
  } else if (lhitem == STACK_INT64ARRAY) {	/* <int64 array>*<int64 array> */
    int64 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.int64base;
    base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) {
      int64value = lhsrce[n] * (int64)rhsrce[n];
      if (llabs(int64value) <= MAXINT64VAL)
        base[n] = int64value;
      else {		/* Result is out of range for an integer */
        error(ERR_RANGE);
      }
    }
  } else if (lhitem == STACK_FLOATARRAY) {	/* <float array>*<int64 array> */
    float64 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) base[n] = lhsrce[n] * TOFLOAT(rhsrce[n]);
  } else if (lhitem == STACK_FATEMP) {		/* <float array>*<int array> */
    float64 *lhsrce;
    basicarray lharray = pop_arraytemp();
    if (!check_arrays(&lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray.arraystart.floatbase;
    for (n = 0; n < count; n++) lhsrce[n] *= TOFLOAT(rhsrce[n]);
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
}

/*
** 'eval_famul' handles multiplication where the right-hand operand is
** a floating point array
*/
static void eval_famul(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n, count;
  float64 *base, *rhsrce;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.floatbase;
  lhitem = GET_TOPITEM;
  if (TOPITEMISNUM) {	/* <int or float>*<float array> */
    floatvalue = pop_anynumfp();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) base[n] = floatvalue * rhsrce[n];
  } else if (lhitem == STACK_INTARRAY) {	/* <int array>*<float array> */
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.intbase;
    for (n = 0; n < count; n++) base[n] = TOFLOAT(lhsrce[n]) * rhsrce[n];
  } else if (lhitem == STACK_UINT8ARRAY) {	/* <uint8 array>*<float array> */
    uint8 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.uint8base;
    for (n = 0; n < count; n++) base[n] = TOFLOAT(lhsrce[n]) * rhsrce[n];
  } else if (lhitem == STACK_FLOATARRAY) {	/* <float array>*<float array> */
    float64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) base[n] = lhsrce[n] * rhsrce[n];
  } else if (lhitem == STACK_FATEMP) {		/* <float array>*<float array> */
    float64 *lhsrce;
    basicarray lharray = pop_arraytemp();
    if (!check_arrays(&lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray.arraystart.floatbase;
    for (n = 0; n < count; n++) lhsrce[n] *= rhsrce[n];
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
}

#define ROW 0
#define COLUMN 1
/*
** 'check_arraytype' ensures that two arrays are compatible for
** matrix multiplication and calculates the dimensions of the
** result array. It fills in the details of the result array
** in the descriptor 'result'
*/
static void check_arraytype(basicarray *result, basicarray *lharray, basicarray *rharray) {
  int32 lhrows, lhcols, rhrows, rhcols;
  if (lharray->dimcount > 2 || rharray->dimcount > 2) error(ERR_MATARRAY);
  lhrows = lharray->dimsize[ROW];		/* First dimemsion is the number of rows */
  lhcols = lharray->dimsize[COLUMN];		/* Second dimension is the number of columns */
  rhrows = rharray->dimsize[ROW];
  rhcols = rharray->dimsize[COLUMN];
  if (lharray->dimcount == 1) {	/* First array is a row vector */
    if (lhrows != rhrows) error(ERR_MATARRAY);
    result->dimcount = 1;		/* Result is a row vector */
    if (rharray->dimcount == 1)	/* Second array is a column vector - Result is a 1 by 1 array */
      result->dimsize[ROW] = result->arrsize = 1;
    else {	/* Second array is a matrix - Result is a N by 1 array */
      result->dimsize[ROW] = result->arrsize = rhcols;
    }
  }
  else if (rharray->dimcount == 1) {	/* Second array is a column vector (1st must be a matrix) */
    if (rhrows != lhcols) error(ERR_MATARRAY);
    result->dimcount = 1;		/* Result is a column vector the same size as the second array */
    result->dimsize[ROW] = result->arrsize = rhrows;
  }
  else {		/* Multiplying two two dimensional matrixes */
    if (lhcols != rhrows) error(ERR_MATARRAY);
    result->dimcount = 2;
    result->arrsize = lhrows * rhcols;
    result->dimsize[ROW] = lhrows;
    result->dimsize[COLUMN] = rhcols;
  }
}

/*
** 'eval_immul' is called to handle matrix multiplication when
** the right-hand array is a 32-bit integer array
*/
static void eval_immul(void) {
  int32 *base, *lhbase, *rhbase, resindex, row, col, sum, lhrowsize, rhrowsize;
  basicarray *lharray, *rharray, result;
  stackitem lhitem;
  rharray = pop_array();
  lhitem = GET_TOPITEM;		/* Get type of left-hand item */
  if (lhitem != STACK_INTARRAY) error(ERR_INTARRAY);	/* Want an integer array */
  lharray = pop_array();
  check_arraytype(&result, lharray, rharray);
  base = make_array(VAR_INTWORD, &result);
/* Find number of array elements to skip going row to row */
  lhrowsize = rhrowsize = 0;
  if (lharray->dimcount != 1) lhrowsize = lharray->dimsize[COLUMN];	/* Want no. of columns (elements in row) */
  if (rharray->dimcount != 1) rhrowsize = rharray->dimsize[COLUMN];
  lhbase = lharray->arraystart.intbase;
  rhbase = rharray->arraystart.intbase;
  if (lharray->dimcount == 1) {	/* Result is a row vector */
    for (resindex = 0; resindex < result.dimsize[ROW]; resindex++) {
      sum = 0;
      for (col = 0; col < lharray->dimsize[ROW]; col++) {
        sum+=lhbase[col] * rhbase[col * rhrowsize + resindex];
      }
      base[resindex] = sum;
    }
  }
  else if (rharray->dimcount == 1) {	/* Result is a column vector */
    for (resindex = 0; resindex < result.dimsize[ROW]; resindex++) {
      int32 lhcol = 0;
      sum = 0;
      for (col = 0; col < rharray->dimsize[ROW]; col++) {
        sum+=lhbase[lhcol] * rhbase[col];
        lhcol++;
      }
      base[resindex] = sum;
    }
  }
  else {	/* Multiplying two two dimensional matrices */
    resindex = 0;
    for (row = 0; row < result.dimsize[ROW]; row++) {
      for (col = 0; col < result.dimsize[COLUMN]; col++) {
        int lhcol;
        sum = 0;
        for (lhcol = 0; lhcol < lharray->dimsize[COLUMN]; lhcol++) {
          sum+=lhbase[lhrowsize * row + lhcol] * rhbase[rhrowsize * lhcol + col];
        }
        base[resindex] = sum;
        resindex++;
      }
    }
  }
}

/*
** 'eval_fmmul' is called to handle matrix multiplication when
** the right-hand array is a floating point array
*/
static void eval_fmmul(void) {
  int32 resindex, row, col, lhrowsize, rhrowsize;
  float64 *base, *lhbase, *rhbase;
  static float64 sum;
  basicarray *lharray, *rharray, result;
  stackitem lhitem;
  rharray = pop_array();
  lhitem = GET_TOPITEM;		/* Get type of left-hand item */
  if (lhitem != STACK_FLOATARRAY) error(ERR_FPARRAY);	/* Want a floating point array */
  lharray = pop_array();
  check_arraytype(&result, lharray, rharray);
  base = make_array(VAR_FLOAT, &result);
/* Find number of array elements to skip going row to row */
  lhrowsize = rhrowsize = 0;
  if (lharray->dimcount != 1) lhrowsize = lharray->dimsize[COLUMN];	/* Want no. of elements in row */
  if (rharray->dimcount != 1) rhrowsize = rharray->dimsize[COLUMN];
  lhbase = lharray->arraystart.floatbase;
  rhbase = rharray->arraystart.floatbase;
  if (lharray->dimcount == 1) {	/* Result is a row vector */
    for (resindex = 0; resindex < result.dimsize[ROW]; resindex++) {
      sum = 0;
      for (col = 0; col < lharray->dimsize[ROW]; col++) {
        sum+=lhbase[col] * rhbase[col * rhrowsize + resindex];
      }
      base[resindex] = sum;
    }
  }
  else if (rharray->dimcount == 1) {	/* Result is a column vector */
    for (resindex = 0; resindex < result.dimsize[ROW]; resindex++) {
      int32 lhcol = 0;
      sum = 0;
      for (col = 0; col < rharray->dimsize[ROW]; col++) {
        sum += lhbase[lhcol] * rhbase[col];
        lhcol++;
      }
      base[resindex] = sum;
    }
  }
  else {	/* Multiplying two two dimensional matrices */
    resindex = 0;
    for (row = 0; row < result.dimsize[ROW]; row++) {	/* Row in the result array */
      for (col = 0; col < result.dimsize[COLUMN]; col++) {	/* Column in the result array */
        int lhcol;
        sum = 0;
        for (lhcol = 0; lhcol < lharray->dimsize[COLUMN]; lhcol++) {
          sum+=lhbase[lhrowsize * row + lhcol] * rhbase[rhrowsize * lhcol + col];
        }
        base[resindex] = sum;
        resindex++;
      }
    }
  }
}

/*
** 'eval_ivdiv' handles floating point division where the right-hand operand
** is a 32-bit integer value
*/
static void eval_ivdiv(void) {
  stackitem lhitem;
  int64 rhint = pop_anyint();
  if (rhint == 0) error(ERR_DIVZERO);
  lhitem = GET_TOPITEM;
  if (TOPITEMISNUM)
    push_float(pop_anynumfp()/TOFLOAT(rhint));
  else if (lhitem == STACK_INTARRAY || lhitem == STACK_UINT8ARRAY || lhitem == STACK_INT64ARRAY || lhitem == STACK_FLOATARRAY) {	/* <array>/<integer value> */
    basicarray *lharray;
    int32 n, count;
    float64 *base;
    lharray = pop_array();
    count = lharray->arrsize;
    base = make_array(VAR_FLOAT, lharray);
    floatvalue = TOFLOAT(rhint);
    if (lhitem == STACK_INTARRAY) {
      int32 *srce = lharray->arraystart.intbase;
      for (n = 0; n < count; n++) base[n] = TOFLOAT(srce[n]) / floatvalue;
    } else if (lhitem == STACK_UINT8ARRAY) {
      uint8 *srce = lharray->arraystart.uint8base;
      for (n = 0; n < count; n++) base[n] = TOFLOAT(srce[n]) / floatvalue;
    } else if (lhitem == STACK_INT64ARRAY) {
      int64 *srce = lharray->arraystart.int64base;
      for (n = 0; n < count; n++) base[n] = TOFLOAT(srce[n]) / floatvalue;
    } else {
      float64 *srce = lharray->arraystart.floatbase;
      for (n = 0; n < count; n++) base[n] = srce[n] / floatvalue;
    }
  } else if (lhitem == STACK_FATEMP) {	/* <float array>/<integer value> */
    basicarray lharray;
    float64 *base;
    int32 n, count;
    lharray = pop_arraytemp();
    base = lharray.arraystart.floatbase;
    count = lharray.arrsize;
    floatvalue = TOFLOAT(rhint);
    for (n = 0; n < count; n++) base[n]/=floatvalue;
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
}

/*
** 'eval_fvdiv' handles division where the right-hand operand is a
** floating point value
*/
static void eval_fvdiv(void) {
  stackitem lhitem;
  floatvalue = pop_float();
  if (floatvalue == 0.0) error(ERR_DIVZERO);
  lhitem = GET_TOPITEM;
  if (TOPITEMISNUM)
    push_float(pop_anynumfp()/floatvalue);
  else if (lhitem == STACK_INTARRAY || lhitem == STACK_FLOATARRAY) {	/* <array>/<float value> */
    basicarray *lharray;
    int32 n, count;
    float64 *base;
    lharray = pop_array();
    count = lharray->arrsize;
    base = make_array(VAR_FLOAT, lharray);
    if (lhitem == STACK_INTARRAY) {
      int32 *srce = lharray->arraystart.intbase;
      for (n = 0; n < count; n++) base[n] = TOFLOAT(srce[n])/floatvalue;
    } else if (lhitem == STACK_UINT8ARRAY) {
      uint8 *srce = lharray->arraystart.uint8base;
      for (n = 0; n < count; n++) base[n] = TOFLOAT(srce[n])/floatvalue;
    } else if (lhitem == STACK_INT64ARRAY) {
      int64 *srce = lharray->arraystart.int64base;
      for (n = 0; n < count; n++) base[n] = TOFLOAT(srce[n])/floatvalue;
    } else {
      float64 *srce = lharray->arraystart.floatbase;
      for (n = 0; n < count; n++) base[n] = srce[n]/floatvalue;
    }
  } else if (lhitem == STACK_FATEMP) {	/* <float array>/<float value> */
    basicarray lharray;
    float64 *base;
    int32 n, count;
    lharray = pop_arraytemp();
    base = lharray.arraystart.floatbase;
    count = lharray.arrsize;
    for (n = 0; n < count; n++) base[n]/=floatvalue;
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
}

/*
** 'eval_iadiv' handles floating point division where the right-hand operand
** is a 32-bit integer array
*/
static void eval_iadiv(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n, count;
  int32 *rhsrce;
  float64 *base;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.intbase;
  lhitem = GET_TOPITEM;
  if (TOPITEMISINT) {						/* <any int value>/<integer array> */
    float64 *base;
    floatvalue = TOFLOAT(pop_anyint());
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = floatvalue / TOFLOAT(rhsrce[n]);
    }
  } else if (lhitem == STACK_FLOAT) {				/* <float value>/<integer array> */
    float64 *base;
    floatvalue = pop_float();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = floatvalue / TOFLOAT(rhsrce[n]);
    }
  } else if (lhitem == STACK_INTARRAY) {			/* <int array>/<int array> */
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.intbase;
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = TOFLOAT(lhsrce[n]) / TOFLOAT(rhsrce[n]);
    }
  } else if (lhitem == STACK_UINT8ARRAY) {			/* <uint8 array>/<int array> */
    uint8 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.uint8base;
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = TOFLOAT(lhsrce[n]) / TOFLOAT(rhsrce[n]);
    }
  } else if (lhitem == STACK_INT64ARRAY) {			/* <int64 array>/<int array> */
    int64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.int64base;
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = TOFLOAT(lhsrce[n]) / TOFLOAT(rhsrce[n]);
    }
  } else if (lhitem == STACK_FLOATARRAY) {			/* <float array>/<int array> */
    float64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = lhsrce[n] / TOFLOAT(rhsrce[n]);
    }
  } else if (lhitem == STACK_FATEMP) {				/* <float array>/<int array> */
    float64 *lhsrce;
    basicarray lharray = pop_arraytemp();
    if (!check_arrays(&lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray.arraystart.floatbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      lhsrce[n] /= TOFLOAT(rhsrce[n]);
    }
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
}

/*
** 'eval_iu8adiv' handles floating point division where the right-hand operand
** is an unsigned 8-bit integer array
*/
static void eval_iu8adiv(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n, count;
  uint8 *rhsrce;
  float64 *base;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.uint8base;
  lhitem = GET_TOPITEM;
  if (TOPITEMISINT) {						/* <any int value>/<integer array> */
    float64 *base;
    floatvalue = TOFLOAT(pop_anyint());
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = floatvalue / TOFLOAT(rhsrce[n]);
    }
  } else if (lhitem == STACK_FLOAT) {				/* <float value>/<integer array> */
    float64 *base;
    floatvalue = pop_float();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = floatvalue / TOFLOAT(rhsrce[n]);
    }
  } else if (lhitem == STACK_INTARRAY) {			/* <int array>/<int array> */
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.intbase;
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = TOFLOAT(lhsrce[n]) / TOFLOAT(rhsrce[n]);
    }
  } else if (lhitem == STACK_UINT8ARRAY) {			/* <uint8 array>/<int array> */
    uint8 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.uint8base;
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = TOFLOAT(lhsrce[n]) / TOFLOAT(rhsrce[n]);
    }
  } else if (lhitem == STACK_INT64ARRAY) {			/* <int64 array>/<int array> */
    int64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.int64base;
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = TOFLOAT(lhsrce[n]) / TOFLOAT(rhsrce[n]);
    }
  } else if (lhitem == STACK_FLOATARRAY) {			/* <float array>/<int array> */
    float64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = lhsrce[n] / TOFLOAT(rhsrce[n]);
    }
  } else if (lhitem == STACK_FATEMP) {				/* <float array>/<int array> */
    float64 *lhsrce;
    basicarray lharray = pop_arraytemp();
    if (!check_arrays(&lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray.arraystart.floatbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      lhsrce[n] /= TOFLOAT(rhsrce[n]);
    }
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
}

/*
** 'eval_i64adiv' handles floating point division where the right-hand operand
** is a 64-bit integer array
*/
static void eval_i64adiv(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n, count;
  int64 *rhsrce;
  float64 *base;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.int64base;
  lhitem = GET_TOPITEM;
  if (TOPITEMISINT) {						/* <any int value>/<int64 array> */
    float64 *base;
    floatvalue = TOFLOAT(pop_anyint());
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = floatvalue / TOFLOAT(rhsrce[n]);
    }
  } else if (lhitem == STACK_FLOAT) {				/* <float value>/<int64 array> */
    float64 *base;
    floatvalue = pop_float();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = floatvalue / TOFLOAT(rhsrce[n]);
    }
  } else if (lhitem == STACK_INTARRAY) {			/* <int array>/<int64 array> */
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.intbase;
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = TOFLOAT(lhsrce[n]) / TOFLOAT(rhsrce[n]);
    }
  } else if (lhitem == STACK_UINT8ARRAY) {			/* <uint8 array>/<int64 array> */
    uint8 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.uint8base;
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = TOFLOAT(lhsrce[n]) / TOFLOAT(rhsrce[n]);
    }
  } else if (lhitem == STACK_INT64ARRAY) {			/* <int64 array>/<int64 array> */
    int64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.int64base;
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = TOFLOAT(lhsrce[n]) / TOFLOAT(rhsrce[n]);
    }
  } else if (lhitem == STACK_FLOATARRAY) {			/* <float array>/<int64 array> */
    float64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = lhsrce[n] / TOFLOAT(rhsrce[n]);
    }
  } else if (lhitem == STACK_FATEMP) {				/* <float array>/<int64 array> */
    float64 *lhsrce;
    basicarray lharray = pop_arraytemp();
    if (!check_arrays(&lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray.arraystart.floatbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      lhsrce[n] /= TOFLOAT(rhsrce[n]);
    }
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
}

/*
** 'eval_fadiv' handles floating point division where the right-hand operand
** is a floating point array
*/
static void eval_fadiv(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n, count;
  float64 *base, *rhsrce;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.floatbase;
  lhitem = GET_TOPITEM;
  if (TOPITEMISINT) {						/* <int32/float value>/<float array> */
    floatvalue = TOFLOAT(pop_anyint());
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base[n] = floatvalue / rhsrce[n];
    }
  } else if (lhitem == STACK_FLOAT) {				/* <float value>/<float array> */
    floatvalue = pop_float();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base[n] = floatvalue / rhsrce[n];
    }
  } else if (lhitem == STACK_INTARRAY) {			/* <int array>/<float array> */
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.intbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base[n] = TOFLOAT(lhsrce[n]) / rhsrce[n];
    }
  } else if (lhitem == STACK_UINT8ARRAY) {			/* <int array>/<float array> */
    uint8 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.uint8base;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base[n] = TOFLOAT(lhsrce[n]) / rhsrce[n];
    }
  } else if (lhitem == STACK_INT64ARRAY) {			/* <int64 array>/<float array> */
    int64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.int64base;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base[n] = TOFLOAT(lhsrce[n]) / rhsrce[n];
    }
  } else if (lhitem == STACK_FLOATARRAY) {			/* <float array>/<float array> */
    float64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base[n] = lhsrce[n] / rhsrce[n];
    }
  } else if (lhitem == STACK_FATEMP) {				/* <float array>/<float array> */
    float64 *lhsrce;
    basicarray lharray = pop_arraytemp();
    if (!check_arrays(&lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray.arraystart.floatbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      lhsrce[n] /= rhsrce[n];
    }
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
}

/*
** 'eval_ivintdiv' handles the integer division operator when the
** right-hand operand is any integer or floating point value
*/
static void eval_ivintdiv(void) {
  stackitem lhitem;
  int64 rhint = 0;
  
  switch(GET_TOPITEM) {
    case STACK_INT: case STACK_UINT8: case STACK_INT64: rhint = pop_anyint(); break;
    case STACK_FLOAT: rhint = TOINT64(pop_float()); break;
    default: error(ERR_BROKEN, __LINE__, "evaluate");
  }
  if (rhint == 0) error(ERR_DIVZERO);
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)	/* Branch according to type of left-hand operand */
    INTDIV_INT(rhint);
  else if (lhitem == STACK_UINT8)	/* Branch according to type of left-hand operand */
    INTDIV_UINT8(rhint);
  else if (lhitem == STACK_INT64)	/* Branch according to type of left-hand operand */
    INTDIV_INT64(rhint);
  else if (lhitem == STACK_FLOAT)
    push_int64(TOINT64(pop_float())/rhint);
  else if (lhitem == STACK_INTARRAY || lhitem == STACK_UINT8ARRAY || lhitem == STACK_INT64ARRAY || lhitem == STACK_FLOATARRAY) {	/* <array> DIV <integer value> */
    basicarray *lharray;
    int32 n, count;
    lharray = pop_array();
    count = lharray->arrsize;
    if (lhitem == STACK_INTARRAY) {		/* <integer array> DIV <integer value> */
      int32 *srce, *base = make_array(VAR_INTWORD, lharray);
      srce = lharray->arraystart.intbase;
      for (n = 0; n < count; n++) base[n] = srce[n] / rhint;
    } else if (lhitem == STACK_UINT8ARRAY) {	/* <integer array> DIV <integer value> */
      uint8 *srce, *base = make_array(VAR_UINT8, lharray);
      srce = lharray->arraystart.uint8base;
      for (n = 0; n < count; n++) base[n] = srce[n] / rhint;
    } else if (lhitem == STACK_INT64ARRAY) {	/* <integer array> DIV <integer value> */
      int64 *srce, *base = make_array(VAR_INTLONG, lharray);
      srce = lharray->arraystart.int64base;
      for (n = 0; n < count; n++) base[n] = srce[n] / rhint;
    } else {	/* <float array> DIV <integer value> */
      float64 *srce;
      int64 *base = make_array(VAR_INTLONG, lharray);
      srce = lharray->arraystart.floatbase;
      for (n = 0; n < count; n++) base[n] = TOINT64(srce[n]) / rhint;
    }
  } else want_number();
}

/*
** 'eval_iaintdiv' handles the integer division operator when the
** right-hand operand is a 32-bit integer array
*/
static void eval_iaintdiv(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n, count;
  int32 *base, *rhsrce;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.intbase;
  lhitem = GET_TOPITEM;
  if (TOPITEMISNUM) {	/* <value> DIV <int32 array> */
    int64 lhint64, *base64;
    lhint64 = lhitem == STACK_FLOAT ? TOINT64(pop_float()) : pop_anyint();
    base64 = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base64[n] = lhint64 / rhsrce[n];
    }
  } else if (lhitem == STACK_INTARRAY) {			/* <int32 array> DIV <int32 array> */
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.intbase;
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = lhsrce[n] / rhsrce[n];
    }
  } else if (lhitem == STACK_UINT8ARRAY) {			/* <uint8 array> DIV <int32 array> */
    uint8 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.uint8base;
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = lhsrce[n] / rhsrce[n];
    }
  } else if (lhitem == STACK_INT64ARRAY) {			/* <int64 array> DIV <int32 array> */
    int64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.int64base;
    base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = lhsrce[n] / rhsrce[n];
    }
  } else if (lhitem == STACK_FLOATARRAY) {			/* <float array> DIV <int32 array> */
    float64 *lhsrce;
    int64 *base64;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base64 = make_array(VAR_INTLONG, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base64[n] = TOINT64(lhsrce[n]) / rhsrce[n];
    }
  } else want_number();
}

/*
** 'eval_iu8aintdiv' handles the integer division operator when the
** right-hand operand is an unsigned 8-bit integer array
*/
static void eval_iu8aintdiv(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n, count;
  int32 *base;
  uint8 *rhsrce;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.uint8base;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT) {					/* <int32> DIV <uint8 array> */
    int32 lhint = pop_int();
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = lhint / rhsrce[n];
    }
  } else  if (lhitem == STACK_UINT8) {				/* <uint8> DIV <uint8 array> */
    uint8 *baseu8, lhint = pop_uint8();
    baseu8 = make_array(VAR_UINT8, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      baseu8[n] = lhint / rhsrce[n];
    }
  } else if (lhitem == STACK_INT64 || lhitem == STACK_FLOAT) {	/* <int64/float> DIV <uint8 array> */
    int64 lhint64, *base64;
    lhint64 = lhitem == STACK_INT64 ? pop_int64() : TOINT64(pop_float());
    base64 = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base64[n] = lhint64 / rhsrce[n];
    }
  } else if (lhitem == STACK_INTARRAY) {			/* <int32 array> DIV <uint8 array> */
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.intbase;
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = lhsrce[n] / rhsrce[n];
    }
  } else if (lhitem == STACK_UINT8ARRAY) {			/* <uint8 array> DIV <uint8 array> */
    uint8 *lhsrce, *baseu8;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.uint8base;
    baseu8 = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      baseu8[n] = lhsrce[n] / rhsrce[n];
    }
  } else if (lhitem == STACK_INT64ARRAY) {			/* <int64 array> DIV <uint8 array> */
    int64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.int64base;
    base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = lhsrce[n] / rhsrce[n];
    }
  } else if (lhitem == STACK_FLOATARRAY) {			/* <float array> DIV <uint8 array> */
    float64 *lhsrce;
    int64 *base64;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base64 = make_array(VAR_INTLONG, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base64[n] = TOINT64(lhsrce[n]) / rhsrce[n];
    }
  } else want_number();
}

/*
** 'eval_i64aintdiv' handles the integer division operator when the
** right-hand operand is a 64-bit integer array
*/
static void eval_i64aintdiv(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n, count;
  int64 *base, *rhsrce;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.int64base;
  lhitem = GET_TOPITEM;
  if (TOPITEMISNUM) {			/* <value> DIV <int64 array> */
    int64 lhint64;
    lhint64 = lhitem == STACK_FLOAT ? TOINT64(pop_float()) : pop_anyint();
    base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = lhint64 / rhsrce[n];
    }
  } else if (lhitem == STACK_INTARRAY) {			/* <int32 array> DIV <int64 array> */
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.intbase;
    base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = lhsrce[n] / rhsrce[n];
    }
  } else if (lhitem == STACK_UINT8ARRAY) {			/* <uint8 array> DIV <int64 array> */
    uint8 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.uint8base;
    base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = lhsrce[n] / rhsrce[n];
    }
  } else if (lhitem == STACK_INT64ARRAY) {			/* <int64 array> DIV <int64 array> */
    int64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.int64base;
    base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = lhsrce[n] / rhsrce[n];
    }
  } else if (lhitem == STACK_FLOATARRAY) {			/* <float array> DIV <int64 array> */
    float64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_INTLONG, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = TOINT64(lhsrce[n]) / rhsrce[n];
    }
  } else {
    want_number();
  }
}

/*
** 'eval_faintdiv' handles the integer division operator when the
** right-hand operand is a floating point array
*/
static void eval_faintdiv(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 *base, n, count;
  int64 *base64;
  float64 *rhsrce;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.floatbase;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT || lhitem == STACK_UINT8) {		/* <int32/uint8> DIV <float array> */
    int32 lhint;
    lhint = lhitem == STACK_INT ? pop_int() : pop_uint8();
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base[n] = lhint / TOINT(rhsrce[n]);
    }
  } else if (lhitem == STACK_INT64 || lhitem == STACK_FLOAT) {	/* <int64/float> DIV <float array> */
    int64 lhint;
    lhint = lhitem == STACK_INT64 ? pop_int64() : TOINT64(pop_float());
    base64 = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base64[n] = lhint / TOINT64(rhsrce[n]);
    }
  } else if (lhitem == STACK_INTARRAY) {			/* <int array> DIV <float array> */
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_INTWORD, rharray);
    lhsrce = lharray->arraystart.intbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base[n] = lhsrce[n] / TOINT(rhsrce[n]);
    }
  } else if (lhitem == STACK_UINT8ARRAY) {			/* <uint8 array> DIV <float array> */
    uint8 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_INTWORD, rharray);
    lhsrce = lharray->arraystart.uint8base;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base[n] = lhsrce[n] / TOINT(rhsrce[n]);
    }
  } else if (lhitem == STACK_INT64ARRAY) {			/* <int64 array> DIV <float array> */
    int64 *lhsrce, *base64;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base64 = make_array(VAR_INTLONG, rharray);
    lhsrce = lharray->arraystart.int64base;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base64[n] = lhsrce[n] / TOINT(rhsrce[n]);
    }
  } else if (lhitem == STACK_FLOATARRAY) {			/* <float array> DIV <float array> */
    float64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base64 = make_array(VAR_INTLONG, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base64[n] = TOINT(lhsrce[n]) / TOINT64(rhsrce[n]);
    }
  } else want_number();
}

/*
** 'eval_ivmod' carries out the integer remainder operator when the right-hand
** operand is any integer or floating point value
*/
static void eval_ivmod(void) {
  stackitem lhitem;
  int64 rhint = 0;
  rhint = pop_anynum64();
  if (rhint == 0) error(ERR_DIVZERO);
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)	/* Branch according to type of left-hand operand */
    INTMOD_INT(rhint);
  else if (lhitem == STACK_UINT8)	/* Branch according to type of left-hand operand */
    INTMOD_UINT8(rhint);
  else if (lhitem == STACK_INT64)	/* Branch according to type of left-hand operand */
    INTMOD_INT64(rhint);
  else if (lhitem == STACK_FLOAT)
    push_int64(TOINT64(pop_float()) % rhint);
  else if (lhitem == STACK_INTARRAY || lhitem == STACK_UINT8ARRAY || lhitem == STACK_INT64ARRAY || lhitem == STACK_FLOATARRAY) {	/* <array> MOD <integer value> */
    basicarray *lharray;
    int32 n, count;
    lharray = pop_array();
    count = lharray->arrsize;
    if (lhitem == STACK_INTARRAY) {				/* <int32 array> MOD <integer value> */
      int32 *srce, *base = make_array(VAR_INTWORD, lharray);
      srce = lharray->arraystart.intbase;
      for (n = 0; n < count; n++) base[n] = srce[n] % rhint;
    } else if (lhitem == STACK_UINT8ARRAY) {			/* <uint8 array> MOD <integer value> */
      uint8 *srce, *base = make_array(VAR_UINT8, lharray);
      srce = lharray->arraystart.uint8base;
      for (n = 0; n < count; n++) base[n] = srce[n] % rhint;
    } else if (lhitem == STACK_INT64ARRAY) {			/* <int64 array> MOD <integer value> */
      int64 *srce, *base = make_array(VAR_INTLONG, lharray);
      srce = lharray->arraystart.int64base;
      for (n = 0; n < count; n++) base[n] = srce[n] % rhint;
    } else {							/* <float array> MOD <integer value> */
      float64 *srce;
      int64 *base = make_array(VAR_INTLONG, lharray);
      srce = lharray->arraystart.floatbase;
      for (n = 0; n < count; n++) base[n] = TOINT64(srce[n]) % rhint;
    }
  } else want_number();
}

/*
** 'eval_iamod' carries out the integer remainder operator when the right-hand
** operand is a 32-bit integer array
*/
static void eval_iamod(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n, count;
  uint8 *baseu8;
  int32 *base, *rhsrce;
  int64 *base64;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.intbase;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT || lhitem == STACK_UINT8) {		/* <value> MOD <integer array> */
    int32 lhint = lhitem == STACK_INT ? pop_int() : pop_uint8();
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = lhint % rhsrce[n];
    }
  } else if (lhitem == STACK_INT64 || lhitem == STACK_FLOAT) {	/* <value> MOD <integer array> */
    int64 lhint = lhitem == STACK_INT64 ? pop_int64() : TOINT64(pop_float());
    base64 = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base64[n] = lhint % rhsrce[n];
    }
  } else if (lhitem == STACK_INTARRAY) {			/* <int32 array> MOD <integer array> */
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.intbase;
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = lhsrce[n] % rhsrce[n];
    }
  } else if (lhitem == STACK_UINT8ARRAY) {			/* <uint8 array> MOD <integer array> */
    uint8 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.uint8base;
    baseu8 = make_array(VAR_UINT8, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      baseu8[n] = lhsrce[n] % rhsrce[n];
    }
  } else if (lhitem == STACK_INT64ARRAY) {			/* <int64 array> MOD <integer array> */
    int64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.int64base;
    base64 = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base64[n] = lhsrce[n] % rhsrce[n];
    }
  } else if (lhitem == STACK_FLOATARRAY) {			/* <float array> MOD <integer array> */
    float64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base64 = make_array(VAR_INTLONG, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base64[n] = TOINT64(lhsrce[n]) % rhsrce[n];
    }
  } else want_number();
}

/*
** 'eval_iu8amod' carries out the integer remainder operator when the right-hand
** operand is an unsigned 8-bit integer array
*/
static void eval_iu8amod(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n, count;
  uint8 *baseu8, *rhsrce;
  int32 *base32;
  int64 *base64;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.uint8base;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT || lhitem == STACK_UINT8) {		/* <value> MOD <int64 array> */
    int32 lhint = lhitem == STACK_INT ? pop_int() : pop_uint8();
    base32 = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base32[n] = lhint % rhsrce[n];
    }
  } else if (lhitem == STACK_INT64 || lhitem == STACK_FLOAT) {	/* <value> MOD <int64 array> */
    int64 lhint;
    lhint = lhitem == STACK_INT64 ? pop_int64() : TOINT64(pop_float());
    base64 = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base64[n] = lhint % rhsrce[n];
    }
  } else if (lhitem == STACK_INTARRAY) {			/* <int32 array> MOD <int64 array> */
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.intbase;
    base32 = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base32[n] = lhsrce[n] % rhsrce[n];
    }
  } else if (lhitem == STACK_UINT8ARRAY) {			/* <uint8 array> MOD <int64 array> */
    uint8 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.uint8base;
    baseu8 = make_array(VAR_UINT8, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      baseu8[n] = lhsrce[n] % rhsrce[n];
    }
  } else if (lhitem == STACK_INT64ARRAY) {			/* <int64 array> MOD <int64 array> */
    int64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.int64base;
    base64 = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base64[n] = lhsrce[n] % rhsrce[n];
    }
  } else if (lhitem == STACK_FLOATARRAY) {			/* <float array> MOD <int64 array> */
    float64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base64 = make_array(VAR_INTLONG, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base64[n] = TOINT64(lhsrce[n]) % rhsrce[n];
    }
  } else want_number();
}

/*
** 'eval_i64amod' carries out the integer remainder operator when the right-hand
** operand is a 64-bit integer array
*/
static void eval_i64amod(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n, count;
  uint8 *baseu8;
  int32 *base32;
  int64 *base64, *rhsrce;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.int64base;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT || lhitem == STACK_UINT8) {		/* <value> MOD <int64 array> */
    int32 lhint = lhitem == STACK_INT ? pop_int() : pop_uint8();
    base32 = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base32[n] = lhint % rhsrce[n];
    }
  } else if (lhitem == STACK_INT64 || lhitem == STACK_FLOAT) {	/* <value> MOD <int64 array> */
    int64 lhint;
    lhint = lhitem == STACK_INT64 ? pop_int64() : TOINT64(pop_float());
    base64 = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base64[n] = lhint % rhsrce[n];
    }
  } else if (lhitem == STACK_INTARRAY) {			/* <int32 array> MOD <int64 array> */
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.intbase;
    base32 = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base32[n] = lhsrce[n] % rhsrce[n];
    }
  } else if (lhitem == STACK_UINT8ARRAY) {			/* <uint8 array> MOD <int64 array> */
    uint8 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.uint8base;
    baseu8 = make_array(VAR_UINT8, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      baseu8[n] = lhsrce[n] % rhsrce[n];
    }
  } else if (lhitem == STACK_INT64ARRAY) {			/* <int64 array> MOD <int64 array> */
    int64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.int64base;
    base64 = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base64[n] = lhsrce[n] % rhsrce[n];
    }
  } else if (lhitem == STACK_FLOATARRAY) {			/* <float array> MOD <int64 array> */
    float64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base64 = make_array(VAR_INTLONG, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base64[n] = TOINT64(lhsrce[n]) % rhsrce[n];
    }
  } else want_number();
}

/*
** 'eval_famod' carries out the integer remainder operator when the right-hand
** operand is a floating point array
*/
static void eval_famod(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 *base32, n, count;
  int64 *base64;
  uint8 *baseu8;
  float64 *rhsrce;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.floatbase;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT || lhitem == STACK_UINT8) {	/* <value> MOD <float array> */
    int32 lhint;
    lhint = lhitem == STACK_INT ? pop_int() : pop_uint8();
    base32 = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base32[n] = lhint % TOINT(rhsrce[n]);
    }
  } else if (lhitem == STACK_INT64 || lhitem == STACK_FLOAT) {	/* <value> MOD <float array> */
    int64 lhint;
    lhint = lhitem == STACK_INT64 ? pop_int64() : TOINT64(pop_float());
    base64 = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base64[n] = lhint % TOINT64(rhsrce[n]);
    }
  } else if (lhitem == STACK_INTARRAY) {	/* <int array> MOD <float array> */
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base32 = make_array(VAR_INTWORD, rharray);
    lhsrce = lharray->arraystart.intbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base32[n] = lhsrce[n] % TOINT(rhsrce[n]);
    }
  } else if (lhitem == STACK_UINT8ARRAY) {	/* <int array> MOD <float array> */
    uint8 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    baseu8 = make_array(VAR_UINT8, rharray);
    lhsrce = lharray->arraystart.uint8base;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      baseu8[n] = lhsrce[n] % TOINT(rhsrce[n]);
    }
  } else if (lhitem == STACK_INT64ARRAY) {	/* <int64 array> MOD <float array> */
    int64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base64 = make_array(VAR_INTLONG, rharray);
    lhsrce = lharray->arraystart.int64base;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base64[n] = lhsrce[n] % TOINT64(rhsrce[n]);
    }
  } else if (lhitem == STACK_FLOATARRAY) {	/* <float array> MOD <float array> */
    float64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base64 = make_array(VAR_INTLONG, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base64[n] = TOINT64(lhsrce[n]) % TOINT64(rhsrce[n]);
    }
  } else want_number();
}

/* Replicate the error conditions found in RISC OS BASIC VI
 * (FPA build on RISC OS 3.71).
 * It is slightly more permissive than BASIC versions I to V.
 */
static float64 mpow(float64 lh, float64 rh) {
  float64 result=pow(lh,rh);
  if (isnan(result) || isint(result)) error(ERR_ARITHMETIC);
  return result;
}

/*
** 'eval_vpow' deals with the 'raise' operator when the right-hand operand is
** a 32-bit or 64-bit integer, or a floating point value
*/
static void eval_vpow(void) {
  floatvalue = pop_anynumfp();
  push_float(mpow(pop_anynumfp(), floatvalue));
}

/*
** 'eval_vlsl' deals with the logical left shift operator when the right-hand
** operand is a 32-bit or 64-bit integer value, or a floating point value.
*/
static void eval_vlsl(void) {
  stackitem lhitem;
  int32 lhint = 0, rhint, val32;
  int64 lhint64 = 0;

#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function evaluate.c:eval_vlsl\n");
#endif

  rhint = pop_anynum32() % 256;

  while (rhint < 0) rhint += 256;
  lhitem = GET_TOPITEM;	/* Branch according to type of left-hand operand */
  if (lhitem == STACK_INT || lhitem == STACK_UINT8) {
    lhint = lhitem == STACK_INT ? pop_int() : pop_uint8();
    val32 = lhint << rhint;
    if (rhint < 32) {
      push_int(val32);
    } else push_int(0);
  } else if (lhitem == STACK_INT64 || lhitem == STACK_FLOAT) {
    lhint64 = lhitem == STACK_INT64 ? pop_int64() : TOINT64(pop_float());
    if (matrixflags.bitshift64) {
      if (rhint < 64) {
        push_int64(lhint64 << rhint);
      } else push_int(0);
    } else {
      if (rhint < 32) {
        push_int(((int32)lhint64) << rhint);
      } else push_int(0);
    }
  } else want_number();
#ifdef DEBUG
    if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function evaluate.c:eval_vlsl at end of function\n");
#endif
}

/* The code in the next two functions assumes GCC behaviour, that it performs
 * logical shifts to the right on unsigned values, and arithmetic shifts
 * on signed values.
 */

/*
** 'eval_vlsr' handles the logical right shift operator (>>>) when the right-hand
** operand is a 32-bit or 64-bit integer value, or a floating point value.
** It should be noted that this code assumes that using unsigned operands
** will result in the C compiler generating a logical shift. This is not
** guaranteed: any C compiler is free to generate an arithmetic shift if
** it so desires.
*/
static void eval_vlsr(void) {
  uint32 lhuint=0, rhuint=0;
  uint64 lhuint64 = 0, res64 = 0;

#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function evaluate.c:eval_vlsr\n");
#endif

  rhuint = pop_anynum32() % 256;

  if (!matrixflags.bitshift64) {
    lhuint=pop_anynum32();
    if (rhuint < 32) {
      push_int(lhuint >> rhuint);
    } else push_int(0);
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function evaluate.c:eval_vlsr at end of 32-bit handler\n");
#endif
    return; /* end of !bitshift64 */
  } else {
    lhuint64 = pop_anynum64();
  }
  if ((rhuint >= 64) || ((!matrixflags.bitshift64) && (rhuint >= 32))) {
    push_int(0);
  } else {
    res64 = (lhuint64 >> rhuint);
    if (matrixflags.bitshift64) {
      push_int64(res64);
    } else {
      push_int(TOINT(res64));
    }
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function evaluate.c:eval_vlsr at end of function\n");
#endif
}

/*
** 'eval_vasr' deals with arithmetic right shifts (>>)  when the right-hand
** operand is a 32-bit or 64-bit integer value, or a floating point value.
*/
static void eval_vasr(void) {
  int32 rhint = 0;
  int64 lhint64 = 0, res64 = 0;

#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function evaluate.c:eval_vasr\n");
#endif

  rhint = pop_anynum32() % 256;

  while (rhint < 0) rhint += 256;
  lhint64 = pop_anynum64();
  if ((rhint >= 64) || ((!matrixflags.bitshift64) && (rhint >= 32))) {
    push_int(0);
  } else {
    res64 = (lhint64 >> rhint);
    if (matrixflags.bitshift64) {
      push_int64(res64);
    } else {
      push_int(TOINT(res64));
    }
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function evaluate.c:eval_vasr at end of function\n");
#endif
}

/*
** 'eval_iveq' deals with the 'equals' operator when the right-hand operand
** is any integer value. It pushes either 'TRUE' or 'FALSE' on to the Basic
** stack depending on whether the two operands are equal or not.
*/
static void eval_iveq(void) {
  int64 rhint = pop_anyint();

  switch(GET_TOPITEM) {						/* Get the left-hand item */
    case STACK_INT:   CPEQ_INT(rhint); break;
    case STACK_UINT8: CPEQ_UINT8(rhint); break;
    case STACK_INT64: CPEQ_INT64(rhint); break;
    case STACK_FLOAT: push_int(pop_float() == TOFLOAT(rhint) ? BASTRUE : BASFALSE); break;
    default: want_number();
  }
}

/*
** 'eval_fveq' deals with the 'equals' operator when the right-hand operand
** is a floating point value. It pushes either 'TRUE' or 'FALSE' on to the
** Basic stack depending on whether the two operands are equal or not.
*/
static void eval_fveq(void) {
  floatvalue = pop_float();
  push_int(pop_anynumfp() == floatvalue ? BASTRUE : BASFALSE);
}

/*
** 'eval_sveq' deals with the 'equals' operator when the right-hand operand
** is a string. It pushes either 'TRUE' or 'FALSE' on to the Basic stack
** depending on whether the two operands are equal or not.
*/
static void eval_sveq(void) {
  stackitem lhitem, rhitem;
  int32 result;
  basicstring lhstring, rhstring;
  rhitem = GET_TOPITEM;
  rhstring = pop_string();
  lhitem = GET_TOPITEM;
  if (lhitem != STACK_STRING && lhitem != STACK_STRTEMP) want_string();
  lhstring = pop_string();
  if (lhstring.stringlen != rhstring.stringlen)
    result = BASFALSE;
  else {
    result = memcmp(lhstring.stringaddr, rhstring.stringaddr, lhstring.stringlen) == 0 ? BASTRUE : BASFALSE;
  }
  PUSH_INT(result);
  if (lhitem == STACK_STRTEMP) free_string(lhstring);
  if (rhitem == STACK_STRTEMP) free_string(rhstring);
}

/*
** 'eval_ivne' deals with the 'not equals' operator when the right-hand
** operand is any integer value
*/
static void eval_ivne(void) {
  int64 rhint = pop_anyint();

  switch(GET_TOPITEM) {						/* Get the left-hand item */
    case STACK_INT:   CPNE_INT(rhint); break;
    case STACK_UINT8: CPNE_UINT8(rhint); break;
    case STACK_INT64: CPNE_INT64(rhint); break;
    case STACK_FLOAT: push_int(pop_float() != TOFLOAT(rhint) ? BASTRUE : BASFALSE); break;
    default: want_number();
  }
}

/*
** 'eval_fvne' deals with the 'not equals' operator when the right-hand
** operand is a floating point value
*/
static void eval_fvne(void) {
  floatvalue = pop_float();
  push_int(pop_anynumfp() != floatvalue ? BASTRUE : BASFALSE);
}

/*
** 'eval_svne' deals with the 'not equals' operator when the right-hand
** operand is a string
*/
static void eval_svne(void) {
  stackitem lhitem, rhitem;
  basicstring lhstring, rhstring;
  int32 result;
  rhitem = GET_TOPITEM;
  rhstring = pop_string();
  lhitem = GET_TOPITEM;
  if (lhitem != STACK_STRING && lhitem != STACK_STRTEMP) want_string();
  lhstring = pop_string();
  if (lhstring.stringlen != rhstring.stringlen)
    result = BASTRUE;
  else {
    result = memcmp(lhstring.stringaddr, rhstring.stringaddr, lhstring.stringlen) != 0 ? BASTRUE : BASFALSE;
  }
  PUSH_INT(result);
  if (lhitem == STACK_STRTEMP) free_string(lhstring);
  if (rhitem == STACK_STRTEMP) free_string(rhstring);
}

/*
** 'eval_ivgt' deals with the 'greater than' operator when the right-hand
** operand is any integer value
*/
static void eval_ivgt(void) {
  int64 rhint = pop_anyint();

  switch(GET_TOPITEM) {						/* Get the left-hand item */
    case STACK_INT:   CPGT_INT(rhint); break;
    case STACK_UINT8: CPGT_UINT8(rhint); break;
    case STACK_INT64: CPGT_INT64(rhint); break;
    case STACK_FLOAT: push_int(pop_float() > TOFLOAT(rhint) ? BASTRUE : BASFALSE); break;
    default: want_number();
  }
}

/*
** 'eval_fvgt' deals with the 'greater than' operator when the right-hand
** operand is a floating point value
*/
static void eval_fvgt(void) {
  floatvalue = pop_float();
  push_int(pop_anynumfp() > floatvalue ? BASTRUE : BASFALSE);
}

/*
** 'eval_svgt' deals with the 'greater than' operator when the right-hand
** operand is a string
*/
static void eval_svgt(void) {
  stackitem lhitem, rhitem;
  basicstring lhstring, rhstring;
  int32 result, complen;
  rhitem = GET_TOPITEM;
  rhstring = pop_string();
  lhitem = GET_TOPITEM;
  if (lhitem != STACK_STRING && lhitem != STACK_STRTEMP) want_string();
  lhstring = pop_string();
  if (lhstring.stringlen < rhstring.stringlen)	/* Compare shorter of two strings */
    complen = lhstring.stringlen;
  else {
    complen = rhstring.stringlen;
  }
  result = memcmp(lhstring.stringaddr, rhstring.stringaddr, complen);
  if (result > 0 || (result == 0 && lhstring.stringlen > rhstring.stringlen))
    result = BASTRUE;
  else {
    result = BASFALSE;
  }
  PUSH_INT(result);
  if (lhitem == STACK_STRTEMP) free_string(lhstring);
  if (rhitem == STACK_STRTEMP) free_string(rhstring);
}

/*
** 'eval_ivlt' handles the 'less than' operator when the right-hand
** operand is any integer
*/
static void eval_ivlt(void) {
  int64 rhint = pop_anyint();

  switch(GET_TOPITEM) {						/* Get the left-hand item */
    case STACK_INT:   CPLT_INT(rhint); break;
    case STACK_UINT8: CPLT_UINT8(rhint); break;
    case STACK_INT64: CPLT_INT64(rhint); break;
    case STACK_FLOAT: push_int(pop_float() < TOFLOAT(rhint) ? BASTRUE : BASFALSE); break;
    default: want_number();
  }
}

/*
** 'eval_fvlt' handles the 'less than' operator when the right-hand
** operand is a floating point value
*/
static void eval_fvlt(void) {
  floatvalue = pop_float();
  push_int(pop_anynumfp() < floatvalue ? BASTRUE : BASFALSE);
}

/*
** 'eval_ivlt' handles the 'less than' operator when the right-hand
** operand is a string
*/
static void eval_svlt(void) {
  stackitem lhitem, rhitem;
  basicstring lhstring, rhstring;
  int32 result, complen;
  rhitem = GET_TOPITEM;
  rhstring = pop_string();
  lhitem = GET_TOPITEM;
  if (lhitem != STACK_STRING && lhitem != STACK_STRTEMP) want_string();
  lhstring = pop_string();
  if (lhstring.stringlen < rhstring.stringlen)	/* Compare shorter of two strings */
    complen = lhstring.stringlen;
  else {
    complen = rhstring.stringlen;
  }
  result = memcmp(lhstring.stringaddr, rhstring.stringaddr, complen);
  if (result < 0 || (result == 0 && lhstring.stringlen < rhstring.stringlen))
    result = BASTRUE;
  else {
    result = BASFALSE;
  }
  PUSH_INT(result);
  if (lhitem == STACK_STRTEMP) free_string(lhstring);
  if (rhitem == STACK_STRTEMP) free_string(rhstring);
}

/*
** 'eval_ivge' handles the 'greater than or equal to' operator when the
** right-hand operand is any integer value
*/
static void eval_ivge(void) {
  int64 rhint = pop_anyint();

  switch(GET_TOPITEM) {						/* Get the left-hand item */
    case STACK_INT:   CPGE_INT(rhint); break;
    case STACK_UINT8: CPGE_UINT8(rhint); break;
    case STACK_INT64: CPGE_INT64(rhint); break;
    case STACK_FLOAT: push_int(pop_float() >= TOFLOAT(rhint) ? BASTRUE : BASFALSE); break;
    default: want_number();
  }
}

/*
** 'eval_fvge' handles the 'greater than or equal to' operator when the
** right-hand operand is a floating point value
*/
static void eval_fvge(void) {
  floatvalue = pop_float();
  push_int(pop_anynumfp() >= floatvalue ? BASTRUE : BASFALSE);
}

/*
** 'eval_svge' handles the 'greater than or equal to' operator when the
** right-hand operand is a string
*/
static void eval_svge(void) {
  stackitem lhitem, rhitem;
  basicstring lhstring, rhstring;
  int32 result, complen;
  rhitem = GET_TOPITEM;
  rhstring = pop_string();
  lhitem = GET_TOPITEM;
  if (lhitem != STACK_STRING && lhitem != STACK_STRTEMP) want_string();
  lhstring = pop_string();
  if (lhstring.stringlen < rhstring.stringlen)	/* Compare shorter of two strings */
    complen = lhstring.stringlen;
  else {
    complen = rhstring.stringlen;
  }
  result = memcmp(lhstring.stringaddr, rhstring.stringaddr, complen);
  if (result > 0 || (result == 0 && lhstring.stringlen >= rhstring.stringlen))
    result = BASTRUE;
  else {
    result = BASFALSE;
  }
  push_int(result);
  if (lhitem == STACK_STRTEMP) free_string(lhstring);
  if (rhitem == STACK_STRTEMP) free_string(rhstring);
}

/*
** 'eval_ivle' deals with the 'less than or equal to' operator when the
** right-hand operand is a 32-bit integer value
*/
static void eval_ivle(void) {
  int64 rhint = pop_anyint();

  switch(GET_TOPITEM) {						/* Get the left-hand item */
    case STACK_INT:   CPLE_INT(rhint); break;
    case STACK_UINT8: CPLE_UINT8(rhint); break;
    case STACK_INT64: CPLE_INT64(rhint); break;
    case STACK_FLOAT: push_int(pop_float() <= TOFLOAT(rhint) ? BASTRUE : BASFALSE); break;
    default: want_number();
  }
}

/*
** 'eval_fvle' deals with the 'less than or equal to' operator when the
** right-hand operand is a floating point value
*/
static void eval_fvle(void) {
  floatvalue = pop_float();
  push_int(pop_anynumfp() <= floatvalue ? BASTRUE : BASFALSE);
}

/*
** 'eval_svle' deals with the 'less than or equal to' operator when the
** right-hand operand is a string
*/
static void eval_svle(void) {
  stackitem lhitem, rhitem;
  basicstring lhstring, rhstring;
  int32 result, complen;
  rhitem = GET_TOPITEM;
  rhstring = pop_string();
  lhitem = GET_TOPITEM;
  if (lhitem != STACK_STRING && lhitem != STACK_STRTEMP) want_string();
  lhstring = pop_string();
  if (lhstring.stringlen < rhstring.stringlen)	/* Compare shorter of two strings */
    complen = lhstring.stringlen;
  else {
    complen = rhstring.stringlen;
  }
  result = memcmp(lhstring.stringaddr, rhstring.stringaddr, complen);
  if (result < 0 || (result == 0 && lhstring.stringlen <= rhstring.stringlen))
    result = BASTRUE;
  else {
    result = BASFALSE;
  }
  push_int(result);
  if (lhitem == STACK_STRTEMP) free_string(lhstring);
  if (rhitem == STACK_STRTEMP) free_string(rhstring);
}

/*
** 'eval_ivand' deals with the logical 'and' operator when the right-hand
** operand is any integer or floating point value
*/
static void eval_ivand(void) {
  int64 rhint=pop_anynum64();
  switch(GET_TOPITEM) {						/* Get the left-hand item */
    case STACK_INT:   AND_INT(rhint); break;
    case STACK_UINT8: AND_UINT8(rhint); break;
    case STACK_INT64: AND_INT64(rhint); break;
    case STACK_FLOAT: push_int64(TOINT64(pop_float()) & rhint); break;
    default: want_number();
  }
}

/*
** 'eval_ivor' deals with the logical 'or' operator when the right-hand
** operand is any integer or floating point value
*/
static void eval_ivor(void) {
  int64 rhint=pop_anynum64();
  switch(GET_TOPITEM) {						/* Get the left-hand item */
    case STACK_INT:   OR_INT(rhint); break;
    case STACK_UINT8: OR_UINT8(rhint); break;
    case STACK_INT64: OR_INT64(rhint); break;
    case STACK_FLOAT: push_int64(TOINT64(pop_float()) | rhint); break;
    default: want_number();
  }
}


/*
** 'eval_iveor' deals with the exclusive or operator when right-hand
** operand is any integer or floating point value
*/
static void eval_iveor(void) {
  int64 rhint=pop_anynum64();
  switch(GET_TOPITEM) {						/* Get the left-hand item */
    case STACK_INT:   EOR_INT(rhint); break;
    case STACK_UINT8: EOR_UINT8(rhint); break;
    case STACK_INT64: EOR_INT64(rhint); break;
    case STACK_FLOAT: push_int64(TOINT64(pop_float()) ^ rhint); break;
    default: want_number();
  }
}
/*
** 'factor_table' is a table of functions indexed by token type used
** to deal with factors in an expression.
** Note that there are a number of entries in here for functions
** as the keywords can be used as both statement types and functions
*/
void (*factor_table[256])(void) = {
  bad_syntax, do_xvar, do_staticvar, do_uint8var,		/* 00..03 */
  do_intvar, do_int64var, do_floatvar, do_stringvar,		/* 04..07 */
  do_arrayvar, do_arrayref, do_arrayref, do_indrefvar,		/* 08..0B */
  do_indrefvar, do_indrefvar, do_statindvar, do_xfunction,	/* 0C..0F */
  do_function, do_intzero, do_intone, do_smallconst,		/* 10..13 */
  do_intconst, do_floatzero, do_floatone, do_floatconst,	/* 14..17 */
  do_stringcon, do_qstringcon, do_int64const, bad_token,	/* 18..1B */
  bad_token, bad_token, bad_token, bad_token,			/* 1C..1F */
  bad_token, do_getword, bad_syntax, bad_syntax,		/* 20..23 */
  do_getstring, bad_syntax, bad_syntax, bad_syntax,		/* 24..27 */
  do_brackets, bad_syntax, bad_syntax, do_unaryplus,		/* 28..2B */
  bad_syntax, do_unaryminus, bad_syntax, bad_syntax,		/* 2C..2F */
  bad_token, bad_token, bad_token, bad_token,			/* 30..33 */
  bad_token, bad_token, bad_token, bad_token,			/* 34..37 */
  bad_token, bad_token, bad_syntax, bad_syntax,			/* 38..3B */
  bad_syntax, bad_syntax, bad_syntax, do_getbyte,		/* 3C..3F */
  bad_syntax, bad_token, bad_token, bad_token,			/* 40..43 */
  bad_token, bad_token, bad_token, bad_token,			/* 44..47 */
  bad_token, bad_token, bad_token, bad_token,			/* 48..4B */
  bad_token, bad_token, bad_token, bad_token,			/* 4C..4F */
  bad_token, bad_token, bad_token, bad_token,			/* 50..53 */
  bad_token, bad_token, bad_token, bad_token,			/* 54..57 */
  bad_token, bad_token, bad_token, bad_syntax,			/* 58..5B */
  bad_syntax, bad_syntax, bad_syntax, bad_token,		/* 5C..5F */
  bad_token, bad_token, bad_token, bad_token,			/* 60..63 */
  bad_token, bad_token, bad_token, bad_token,			/* 64..67 */
  bad_token, bad_token, bad_token, bad_token,			/* 68..6B */
  bad_token, bad_token, bad_token, bad_token,			/* 6C..6F */
  bad_token, bad_token, bad_token, bad_token,			/* 70..73 */
  bad_token, bad_token, bad_token, bad_token,			/* 74..77 */
  bad_token, bad_token, bad_token, bad_syntax,			/* 78..7B */
  do_getfloat, bad_syntax, bad_syntax, bad_token,		/* 7C..7F */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* 80..83 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* 84..87 */
  bad_syntax, fn_mod, bad_syntax, bad_syntax,			/* 88..8B */
  bad_syntax, fn_beats, bad_syntax, bad_syntax,			/* 8C..8F */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* 90..93 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* 94..97 */
  fn_colour, bad_syntax, bad_syntax, fn_dim,			/* 98..9B */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* 9C..9F */
  bad_syntax, bad_syntax, bad_syntax, fn_end,			/* A0..A3 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* A4..A7 */
  bad_syntax, bad_token,  fn_false, bad_syntax,			/* A8..AB */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* AC..AF */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* B0..B3 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* B4..B7 */
  bad_syntax, bad_syntax, fn_mode, bad_syntax,			/* B8..BB */
  bad_syntax, bad_syntax, bad_syntax, fn_not,			/* BC..BF */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* C0..C3 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* C4..C7 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* C8..CB */
  bad_syntax, bad_syntax, fn_quit, bad_syntax,			/* CC..CF */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* D0..D3 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* D4..D7 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* D8..DB */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* DC..DF */
  fn_tint, fn_top, fn_trace, fn_true,				/* E0..E3 */
  bad_syntax, fn_vdu, bad_syntax, bad_syntax,			/* E4..E7 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* E8..EB */
  bad_syntax, fn_width, bad_token, bad_token,			/* EC..EF */
  bad_token, bad_token, bad_token, bad_token,			/* F0..F3 */
  bad_token, bad_token, bad_token, bad_token,			/* F4..F7 */
  bad_token, bad_token, bad_token, bad_token,			/* F8..FB */
  bad_syntax, bad_token, bad_syntax, exec_function		/* FC..FF */
};

/*
** Operator table
** This gives the priority of each dyadic operator, indexed by the
** operator's token value. A value of zero means that the token is not
** an operator (and that the end of the expression has been reached)
*/
static int32 optable [256] = {	/* Character -> priority/operator */
  0, 0, 0, 0, 0, 0, 0, 0,				/* 00..07 */
  0 ,0, 0, 0, 0, 0, 0, 0,				/* 08..0F */
  0, 0, 0, 0, 0, 0, 0, 0,				/* 10..17 */
  0 ,0, 0, 0, 0, 0, 0, 0,				/* 18..1F */
  0, 0, 0, 0, 0, 0, 0, 0,				/* 20..27 */
  0 ,0, MULPRIO+OP_MUL, ADDPRIO+OP_ADD,			/* 28..2B */
  0, ADDPRIO+OP_SUB, MULPRIO+OP_MATMUL, MULPRIO+OP_DIV,	/* 2C..2F */
  0, 0, 0, 0, 0, 0, 0, 0,				/* 30..37 */
  0 ,0, 0, 0,						/* 38..3B */
  COMPRIO+OP_LT, COMPRIO+OP_EQ, COMPRIO+OP_GT, 0,	/* 3C..3F */
  0, 0, 0, 0, 0, 0, 0, 0,				/* 40..47 */
  0 ,0, 0, 0, 0, 0, 0, 0,				/* 48..4F */
  0, 0, 0, 0, 0, 0, 0, 0,				/* 50..57 */
  0 ,0, 0, 0, 0, 0, POWPRIO+OP_POW, 0,			/* 58..5F */
  0, 0, 0, 0, 0, 0, 0, 0,				/* 60..67 */
  0 ,0, 0, 0, 0, 0, 0, 0,				/* 68..6F */
  0, 0, 0, 0, 0, 0, 0, 0,				/* 70..77 */
  0 ,0, 0, 0, 0, 0, 0, 0,				/* 78..7F */
  ANDPRIO+OP_AND, COMPRIO+OP_ASR, MULPRIO+OP_INTDIV, ORPRIO+OP_EOR,	/* 80..83 */
  COMPRIO+OP_GE,  COMPRIO+OP_LE,  COMPRIO+OP_LSL, COMPRIO+OP_LSR,	/* 84..87 */
  0 ,MULPRIO+OP_MOD, COMPRIO+OP_NE, ORPRIO+OP_OR,			/* 88..8B */
  0, 0, 0, 0,						/* 8C..8F */
  0, 0, 0, 0, 0, 0, 0, 0,				/* 90..97 */
  0 ,0, 0, 0, 0, 0, 0, 0,				/* 98..9F */
  0, 0, 0, 0, 0, 0, 0, 0,				/* A0..A7 */
  0 ,0, 0, 0, 0, 0, 0, 0,				/* A8..AF */
  0, 0, 0, 0, 0, 0, 0, 0,				/* B0..B7 */
  0 ,0, 0, 0, 0, 0, 0, 0,				/* B8..BF */
  0, 0, 0, 0, 0, 0, 0, 0,				/* C0..C7 */
  0 ,0, 0, 0, 0, 0, 0, 0,				/* C8..CF */
  0, 0, 0, 0, 0, 0, 0, 0,				/* D0..D7 */
  0 ,0, 0, 0, 0, 0, 0, 0,				/* D8..DF */
  0, 0, 0, 0, 0, 0, 0, 0,				/* E0..E7 */
  0 ,0, 0, 0, 0, 0, 0, 0,				/* E8..EF */
  0, 0, 0, 0, 0, 0, 0, 0,				/* F0..F7 */
  0 ,0, 0, 0, 0, 0, 0, 0				/* F8..FF */
};

/*
** The opfunctions table gives which function to call for which operator.
** It is indexed by the operator and the type of the right hand operand
** on the stack
*/
static void (*opfunctions [21][18])(void) = {
/* Dummy */
 {eval_badcall,  eval_badcall,  eval_badcall,   eval_badcall,  eval_badcall,
  eval_badcall,  eval_badcall,  eval_badcall,   eval_badcall,
  eval_badcall,  eval_badcall,  eval_badcall,   eval_badcall,
  eval_badcall,  eval_badcall,  eval_badcall,   eval_badcall,
  eval_badcall},
/* Addition */
 {eval_badcall,  eval_badcall,  eval_ivplus,    eval_ivplus,   eval_ivplus,
  eval_fvplus,   eval_svplus,   eval_svplus,    eval_iaplus,
  eval_iaplus,   eval_iu8aplus, eval_iu8aplus,  eval_i64aplus,
  eval_i64aplus, eval_faplus,   eval_faplus,    eval_saplus,
  eval_saplus},
/* Subtraction */
 {eval_badcall,  eval_badcall,  eval_ivminus,   eval_ivminus,  eval_ivminus,
  eval_fvminus,  want_number,   want_number,    eval_iaminus,
  eval_iaminus,  eval_iu8aminus,eval_iu8aminus, eval_i64aminus,
  eval_i64aminus,eval_faminus,  eval_faminus,   want_number,
  want_number},
/* Multiplication */
 {eval_badcall,  eval_badcall,  eval_ivmul,     eval_ivmul,    eval_ivmul,
  eval_fvmul,    want_number,   want_number,    eval_iamul,
  eval_iamul,    eval_iu8amul,  eval_iu8amul,   eval_i64amul,
  eval_i64amul,  eval_famul,    eval_famul,     want_number,
  want_number},
/* Matrix multiplication */
 {want_array,    eval_badcall,  want_array,     want_array,    want_array,
  want_array,    want_array,    want_array,     eval_immul,
  want_array,    want_array,    want_array,     want_array,
  want_array,    eval_fmmul,    want_array,     want_array,
  want_array},
/* Division */
 {eval_badcall,  eval_badcall,  eval_ivdiv,     eval_ivdiv,    eval_ivdiv,
  eval_fvdiv,    want_number,   want_number,    eval_iadiv,
  eval_iadiv,    eval_iu8adiv,  eval_iu8adiv,   eval_i64adiv,
  eval_i64adiv,  eval_fadiv,    eval_fadiv,     want_number,
  want_number},
/* Integer division */
 {eval_badcall,  eval_badcall,  eval_ivintdiv,  eval_ivintdiv, eval_ivintdiv,
  eval_ivintdiv, want_number,   want_number,    eval_iaintdiv,
  eval_iaintdiv, eval_iu8aintdiv,eval_iu8aintdiv,eval_i64aintdiv,
  eval_i64aintdiv,eval_faintdiv, eval_faintdiv, want_number,
  want_number},
/* Integer remainder (MOD)*/
 {eval_badcall,  eval_badcall,  eval_ivmod,     eval_ivmod,    eval_ivmod,
  eval_ivmod,    want_number,   want_number,    eval_iamod,
  eval_iamod,    eval_iu8amod,  eval_iu8amod,   eval_i64amod,
  eval_i64amod,  eval_famod,    eval_famod,     want_number,
  want_number},
/* Raise */
 {eval_badcall,  eval_badcall,  eval_vpow,      eval_vpow,     eval_vpow,
  eval_vpow,     want_number,   want_number,    want_number,
  want_number,   want_number,   want_number,    want_number,
  want_number,   want_number,   want_number,    want_number,
  want_number},
/* Logical left shift */
 {eval_badcall,  eval_badcall,  eval_vlsl,      eval_vlsl,     eval_vlsl,
  eval_vlsl,     want_number,   want_number,    want_number,
  want_number,   want_number ,  want_number,    want_number,
  want_number,   want_number,   want_number,    want_number,
  want_number},
/* Logical right shift */
 {eval_badcall,  eval_badcall,  eval_vlsr,      eval_vlsr,     eval_vlsr,
  eval_vlsr,     want_number,   want_number,    want_number,
  want_number,   want_number,   want_number,    want_number,
  want_number,   want_number,   want_number,    want_number,
  want_number},
/* Arithmetic right shift */
 {eval_badcall,  eval_badcall,  eval_vasr,      eval_vasr,     eval_vasr,
  eval_vasr,     want_number,   want_number,    want_number,
  want_number,   want_number,   want_number,    want_number,
  want_number,   want_number,   want_number,    want_number,
  want_number},
/* Equals */
 {eval_badcall,  eval_badcall,  eval_iveq,      eval_iveq,     eval_iveq,
  eval_fveq,     eval_sveq,     eval_sveq,      want_number,
  want_number,   want_number,   want_number,    want_number,
  want_number,   want_number,   want_number,    want_number,
  want_number},
/* Not equals */
 {eval_badcall,  eval_badcall,  eval_ivne,      eval_ivne,     eval_ivne,
  eval_fvne,     eval_svne,     eval_svne,      want_number,
  want_number,   want_number,   want_number,    want_number,
  want_number,   want_number,   want_number,    want_number,
  want_number},
/* Greater than */
 {eval_badcall,  eval_badcall,  eval_ivgt,      eval_ivgt,     eval_ivgt,
  eval_fvgt,     eval_svgt,     eval_svgt,      want_number,
  want_number,   want_number,   want_number,    want_number,
  want_number,   want_number,   want_number,    want_number,
  want_number},
/* Less than */
 {eval_badcall,  eval_badcall,  eval_ivlt,      eval_ivlt,     eval_ivlt,
  eval_fvlt,     eval_svlt,     eval_svlt,      want_number,
  want_number,   want_number,   want_number,    want_number,
  want_number,   want_number,   want_number,    want_number,
  want_number},
/* Greater than or equal to */
 {eval_badcall,  eval_badcall,  eval_ivge,      eval_ivge,     eval_ivge,
  eval_fvge,     eval_svge,     eval_svge,      want_number,
  want_number,   want_number,   want_number,    want_number,
  want_number,   want_number,   want_number,    want_number,
  want_number},
/* Less than or equal to */
 {eval_badcall,  eval_badcall,  eval_ivle,      eval_ivle,     eval_ivle,
  eval_fvle,     eval_svle,     eval_svle,      want_number,
  want_number,   want_number,   want_number,    want_number,
  want_number,   want_number,   want_number,    want_number,
  want_number},
/* Logical and */
 {eval_badcall,  eval_badcall,  eval_ivand,     eval_ivand,    eval_ivand,
  eval_ivand,    want_number,   want_number,    want_number,
  want_number,   want_number,   want_number,    want_number,
  want_number,   want_number,   want_number,    want_number,
  want_number},
/* Logical or */
 {eval_badcall,  eval_badcall,  eval_ivor,      eval_ivor,     eval_ivor,
  eval_ivor,     want_number,   want_number,    want_number,
  want_number,   want_number,   want_number,    want_number,
  want_number,   want_number,   want_number,    want_number,
  want_number},
/* Logical exclusive or */
 {eval_badcall,  eval_badcall,  eval_iveor,     eval_iveor,    eval_iveor,
  eval_iveor,    want_number,   want_number,    want_number,
  want_number,   want_number,   want_number,    want_number,
  want_number,   want_number,   want_number,    want_number,
  want_number},
};

/*
** 'expression' is the main function called when evaluating an expression
** and also the heart of the expression code. It contains the program's
** inner loop. The function is called with basicvars.current pointing at
** the expression, which can have blanks before it. basicvars.current is
** left pointing at the first non-blank token after the expression. The
** value is left on the Basic stack. The code is optimised to deal with
** simple expressions of the form '<value>' or '<value> <op> <value>'.
**
** Note that there is a complication here involving relational operators.
** You cannot have two or more operators in a row, for example, 'x>1=-1'
** is not allowed. This example would be treated as 'x>1' with the '=-1'
** part as a separate statement. In fact the rules are more complex than
** this: you cannot have two or more relational operators if they are
** adjacent or separated by higher priority operators.
*/
void expression(void) {
  int32 thisop, lastop;

#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function evaluate.c:expression\n");
#endif
  (*factor_table[*basicvars.current])();	/* Get first factor in the expression */
  lastop = optable[*basicvars.current];
  if (lastop == 0) {
#ifdef DEBUG
    if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function evaluate.c:expression via lastop=0\n");
#endif
    return;	/* Quick way out if there is nothing to do */
  }
  basicvars.current++;		/* Skip operator (always one character) */
  (*factor_table[*basicvars.current])();	/* Get second operand */
  thisop = optable[*basicvars.current];
  if (thisop == 0) {
/* Have got a simple '<value> <op> <value>' type of expression */
    (*opfunctions[lastop & OPERMASK][GET_TOPITEM])();
#ifdef DEBUG
    if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function evaluate.c:expression via thisop=0\n");
#endif
    return;
  }
/* Expression is more complex so we have to invoke the heavy machinery */
  if (basicvars.opstop == basicvars.opstlimit) error(ERR_OPSTACK);
  basicvars.opstop++;
  *basicvars.opstop = OPSTACKMARK;
  do {
    if (PRIORITY(thisop) > PRIORITY(lastop)) {	/* Priority of this operator > priority of last */
      if (basicvars.opstop == basicvars.opstlimit) error(ERR_OPSTACK);
    }
    else {	/* Priority of this operator <= last op's priority - exec last operator */
      if (PRIORITY(thisop) == COMPRIO) {		/* Ghastly hack for ghastly Basic relational operator syntax */
        while (PRIORITY(lastop) >= PRIORITY(thisop) && PRIORITY(lastop) != COMPRIO) {
          (*opfunctions[lastop & OPERMASK][GET_TOPITEM])();
          lastop = *basicvars.opstop;
          basicvars.opstop--;
        }
        if (PRIORITY(lastop) == COMPRIO) break;
      }
      else {	/* Normal case without check for relational operator */
        do {
          (*opfunctions[lastop & OPERMASK][GET_TOPITEM])();
          lastop = *basicvars.opstop;
          basicvars.opstop--;
        } while (PRIORITY(lastop) >= PRIORITY(thisop));
      }
    }
    basicvars.opstop++;
    *basicvars.opstop = lastop;
    lastop = thisop;
    basicvars.current++;	/* Skip operator (always one character) */
    (*factor_table[*basicvars.current])();	/* Get next operand */
    thisop = optable[*basicvars.current];
  } while (thisop != 0);
  while (lastop != OPSTACKMARK) {	/* Now clear the operator stack */
    (*opfunctions[lastop & OPERMASK][GET_TOPITEM])();
    lastop = *basicvars.opstop;
    basicvars.opstop--;
  }
#ifdef DEBUG
    if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function evaluate.c:expression at end of function\n");
#endif
}

/*
** 'factor' is similar to expression. It is used in cases where the language
** specifies a 'factor' instead of a complete expression. In most cases
** it will be the built-in functions that invoke this code but some
** statement types such as 'BPUT' use it too.
*/
void factor(void) {
  *basicvars.opstop = OPSTACKMARK;
  (*factor_table[*basicvars.current])();
  if (*basicvars.opstop != OPSTACKMARK) error(ERR_BADEXPR);
}

/*
** 'init_expressions' is called to reset the expression evaluation code
** before running a program
*/
void init_expressions(void) {
  basicvars.opstop = make_opstack();
  basicvars.opstlimit = basicvars.opstop+OPSTACKSIZE;
  *basicvars.opstop = OPSTACKMARK;
  init_functions();
}

/*
** 'reset_opstack' is called to reset the operator stack pointer to its
** initial value
*/
void reset_opstack(void) {
  basicvars.opstop = basicvars.opstlimit-OPSTACKSIZE;
  *basicvars.opstop = OPSTACKMARK;
}
