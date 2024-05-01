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
**
**      This file contains the interpreter's expression evaluation code
**      apart from the built-in Basic functions which are in functions.c
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

#ifdef TARGET_RISCOS
extern float80 powl(long double x, long double y);
extern float80 fabsl(long double x);
#endif

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
#define DJGPPLIMIT 75000                /* Minimum amount of C stack allowed in DJGPP version of program */
#endif

#define TIMEFORMAT "%a,%d %b %Y.%H:%M:%S"  /* Date format used by 'TIME$' */

#define OPSTACKMARK 0                   /* 'Operator' used as sentinel at the base of the operator stack */

static float80 floatvalue;              /* Temporary for holding floating point values */
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

#define POWPRIO  0x700
#define MULPRIO  0x600
#define ADDPRIO  0x500
#define COMPRIO  0x400
#define ANDPRIO  0x300
#define ORPRIO   0x200
#define MARKPRIO 0

/* Operator identities (values used on operator stack) */

#define OP_NOP    0
#define OP_ADD    1
#define OP_SUB    2
#define OP_MUL    3
#define OP_MATMUL 4
#define OP_DIV    5
#define OP_INTDIV 6
#define OP_MOD    7
#define OP_POW    8
#define OP_LSL    9
#define OP_LSR   10
#define OP_ASR   11
#define OP_EQ    12
#define OP_NE    13
#define OP_GT    14
#define OP_LT    15
#define OP_GE    16
#define OP_LE    17
#define OP_AND   18
#define OP_OR    19
#define OP_EOR   20

#define OPCOUNT (OP_EOR+1)

#define OPERMASK 0xFF
#define PRIOMASK 0xFF00

#define PRIORITY(x) (x & PRIOMASK)

typedef void operator(void);

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
** 'eval_integer' evaluates a numeric expression where an integer value
** is required, returning the value
*/
int32 eval_integer(void) {
  DEBUGFUNCMSGIN;
  expression();
  DEBUGFUNCMSGOUT;
  return pop_anynum32();
}

int64 eval_int64(void) {
  DEBUGFUNCMSGIN;
  expression();
  DEBUGFUNCMSGOUT;
  return pop_anynum64();
}

static int32 i32mulwithtest(int32 lh, int32 rh) {
  DEBUGFUNCMSGIN;
  if (llabs((int64)lh * (int64)rh) > MAXINTVAL) {
    DEBUGFUNCMSGOUT;
    error(ERR_RANGE);
  }
  DEBUGFUNCMSGOUT;
  return lh*rh;
}

/* Use cast to float instead of TOFLOAT() as we don't care about loss of precision here */
static int64 i64mulwithtest(int64 lh, int64 rh) {
  DEBUGFUNCMSGIN;
  if (fabsl((float64)(lh) * (float64)(rh)) > (float64)(MAXINT64VAL)) {
    DEBUGFUNCMSGOUT;
    error(ERR_RANGE);
  }
  DEBUGFUNCMSGOUT;
  return lh*rh;
}

static int32 i32divwithtest(int32 lh, int32 rh) {
  DEBUGFUNCMSGIN;
  if(rh == 0) {
    DEBUGFUNCMSGOUT;
    error(ERR_DIVZERO);
  }
  DEBUGFUNCMSGOUT;
  return(lh/rh);
}

static int64 i64divwithtest(int64 lh, int64 rh) {
  DEBUGFUNCMSGIN;
  if(rh == 0) {
    DEBUGFUNCMSGOUT;
    error(ERR_DIVZERO);
  }
  DEBUGFUNCMSGOUT;
  return(lh/rh);
}

static int32 i32modwithtest(int32 lh, int32 rh) {
  DEBUGFUNCMSGIN;
  if(rh == 0) {
    DEBUGFUNCMSGOUT;
    error(ERR_DIVZERO);
  }
  DEBUGFUNCMSGOUT;
  return(lh%rh);
}

static int64 i64modwithtest(int64 lh, int64 rh) {
  DEBUGFUNCMSGIN;
  if(rh == 0) {
    DEBUGFUNCMSGOUT;
    error(ERR_DIVZERO);
  }
  DEBUGFUNCMSGOUT;
  return(lh%rh);
}

/*
** 'fmulwithtest' multiplies two float64 values, and checks that the
** number is not an invalid response (NaN, Inf, Subnormal). Also,
** zero is OK, annoyingly isnormal() doesn't consider 0 to be normal,
** and isfinite() doesn't report on FP_SUBNORMAL according to the
** man pages.
*/

static float64 fmulwithtest(float64 lh, float64 rh) {
  float64 res=lh*rh;
  DEBUGFUNCMSGIN;
  if ((res != 0.0) && !isnormal(res)) {
    DEBUGFUNCMSGOUT;
    error(ERR_RANGE);
  }
  DEBUGFUNCMSGOUT;
  return(res);
}

/* Similarly for 'fdivwithtest' */
static float64 fdivwithtest(float64 lh, float64 rh) {
  float64 res;
  DEBUGFUNCMSGIN;
  if (rh == 0.0) {
    DEBUGFUNCMSGOUT;
    error(ERR_DIVZERO);
  }
  res=lh/rh;
  if ((res != 0.0) && !isnormal(res)) {
    DEBUGFUNCMSGOUT;
    error(ERR_RANGE);
  }
  DEBUGFUNCMSGOUT;
  return(res);
}

/*
** 'eval_intfactor' evaluates a numeric factor where an integer is
** required. The function returns the value obtained.
*/
int32 eval_intfactor(void) {
  DEBUGFUNCMSGIN;
  (*factor_table[*basicvars.current])();
  DEBUGFUNCMSGOUT;
  return pop_anynum32();
}

/*
** 'check_arrays' returns silently if the two arrays passed to it
** have the same number of dimensions and the bounds of each
** dimension are the same, or an error of they do not match.
** It does not check the types of the the array elements.
*/
void check_arrays(basicarray *p1, basicarray *p2) {
  int32 n;
  DEBUGFUNCMSGIN;
  if (p1->dimcount != p2->dimcount) {
    DEBUGFUNCMSGOUT;
    error(ERR_TYPEARRAY);
  }
  n = 0;
  while (n < p1->dimcount && p1->dimsize[n] == p2->dimsize[n])
    n++;
  DEBUGFUNCMSGOUT;
  if (!(n == p1->dimcount)) error(ERR_TYPEARRAY);
}

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

  DEBUGFUNCMSGIN;
  isreturn = (fp->parameter.typeinfo & VAR_RETURN) != 0;
  if (!isreturn) {      /* Normal parameter */
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
      DEBUGFUNCMSGOUT;
      error(ERR_BROKEN, __LINE__, "evaluate");
    }
  } else {      /* Return parameter */
    get_lvalue(&retparm);
    switch (retparm.typeinfo) { /* Now fetch the parameter's value */
    case VAR_INTWORD:           /* Integer parameter */
      intparm = *retparm.address.intaddr;
      parmtype = STACK_INT;
      break;
    case VAR_UINT8:             /* Integer parameter */
      uint8parm = *retparm.address.uint8addr;
      parmtype = STACK_UINT8;
      break;
    case VAR_INTLONG:           /* Integer parameter */
      int64parm = *retparm.address.int64addr;
      parmtype = STACK_INT64;
      break;
    case VAR_FLOAT:             /* Floating point parameter */
      floatparm = *retparm.address.floataddr;
      parmtype = STACK_FLOAT;
      break;
    case VAR_STRINGDOL:         /* Normal string parameter */
      stringparm = *retparm.address.straddr;
      parmtype = STACK_STRING;
      break;
    case VAR_INTBYTEPTR:        /* Indirect byte-sized integer */
      intparm = basicvars.memory[retparm.address.offset];
      parmtype = STACK_INT;
      break;
    case VAR_INTWORDPTR:        /* Indirect word-sized integer */
      intparm = get_integer(retparm.address.offset);
      parmtype = STACK_INT;
      break;
    case VAR_FLOATPTR:          /* Indirect eight-byte floating point value */
      floatparm = get_float(retparm.address.offset);
      parmtype = STACK_FLOAT;
      break;
    case VAR_DOLSTRPTR:         /* Indirect string */
      stringparm.stringlen = get_stringlen(retparm.address.offset);
      stringparm.stringaddr = CAST(&basicvars.memory[retparm.address.offset], char *);
      parmtype = STACK_STRING;
      break;
    case VAR_INTARRAY:          /* Array of integers */
      arrayparm = *retparm.address.arrayaddr;
      parmtype = STACK_INTARRAY;
      break;
    case VAR_UINT8ARRAY:        /* Array of integers */
      arrayparm = *retparm.address.arrayaddr;
      parmtype = STACK_UINT8ARRAY;
      break;
    case VAR_INT64ARRAY:                /* Array of integers */
      arrayparm = *retparm.address.arrayaddr;
      parmtype = STACK_INT64ARRAY;
      break;
    case VAR_FLOATARRAY:                /* Array of floating point values */
      arrayparm = *retparm.address.arrayaddr;
      parmtype = STACK_FLOATARRAY;
      break;
    case VAR_STRARRAY:          /* Array of strings */
      arrayparm = *retparm.address.arrayaddr;
      parmtype = STACK_STRARRAY;
      break;
    default:            /* Bad parameter type */
      DEBUGFUNCMSGOUT;
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

  if (*basicvars.current == ',') {      /* More parameters to come - Point at start of next one */
    basicvars.current++;                /* SKip comma */
    if (*basicvars.current == ')') {
      DEBUGFUNCMSGOUT;
      error(ERR_SYNTAX);
    }
    if (fp->nextparm == NIL) {
      DEBUGFUNCMSGOUT;
      error(ERR_TOOMANY, procname);
    }
    push_oneparm(fp->nextparm, parmno+1, procname);
  }
  else if (*basicvars.current == ')') { /* End of parameters */
    if (fp->nextparm != NIL) {  /* Have reached end of parameters but not the end of the parameter list */
      DEBUGFUNCMSGOUT;
      error(ERR_NOTENUFF, procname);
    }
    basicvars.current++;        /* Step past the ')' */
  }
  else {        /* Syntax error - ',' or ')' expected */
    DEBUGFUNCMSGOUT;
    error(ERR_CORPNEXT);
  }
/*
** Now move the parameter to the formal parameter variable, saving the
** variable's original value on the stack. In the case of a 'return'
** parameter, the address of the variable that will receive the returned
** value has to be saved as well.
*/
  if ((fp->parameter.typeinfo & PARMTYPEMASK) == VAR_INTWORD) { /* Deal with most common case first */
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
        if ((int64parm >= 0 && int64parm <= 0x7FFFFFFFll) ||
            (int64parm < 0 && int64parm >= 0xFFFFFFFF80000000ll)) {
          *p = (int32)int64parm;
        } else {
          DEBUGFUNCMSGOUT;
          error(ERR_RANGE);
        }
        break;
      case STACK_FLOAT: *p = TOINT(floatparm); break;
      default:
        DEBUGFUNCMSGOUT;
        error(ERR_BROKEN, __LINE__, "evaluate");
    }
    DEBUGFUNCMSG("Exiting via VAR_INTWORD");
    return;
  }
/* Now deal with other parameter types */
  switch (fp->parameter.typeinfo & PARMTYPEMASK) {      /* Go by formal parameter type */
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
      default:
        DEBUGFUNCMSGOUT;
        error(ERR_BROKEN, __LINE__, "evaluate");
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
      default:
        DEBUGFUNCMSGOUT;
        error(ERR_BROKEN, __LINE__, "evaluate");
    }
    break;
  }
  case VAR_FLOAT: {             /* Floating point parameter */
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
      default:
        DEBUGFUNCMSGOUT;
        error(ERR_BROKEN, __LINE__, "evaluate");
    }
    break;
  }
  case VAR_STRINGDOL: { /* Normal string parameter */
    basicstring *p = fp->parameter.address.straddr;
    if (isreturn)
      save_retstring(retparm, fp->parameter, *p);
    else {
      save_string(fp->parameter, *p);
    }
    if (parmtype == STACK_STRING) {     /* Argument is a string variable - Have to copy string */
      p->stringlen = stringparm.stringlen;
      p->stringaddr = alloc_string(stringparm.stringlen);
      if (stringparm.stringlen > 0) memmove(p->stringaddr, stringparm.stringaddr, stringparm.stringlen);
    }
    else {      /* Argument is a string expression - Can use it directly */
      *p = stringparm;
    }
    break;
  }
  case VAR_INTBYTEPTR:  /* Indirect byte-sized integer */
    if (isreturn)
      save_retint(retparm, fp->parameter, basicvars.memory[fp->parameter.address.offset]);
    else {
      save_int(fp->parameter, basicvars.memory[fp->parameter.address.offset]);
    }
    basicvars.memory[fp->parameter.address.offset] = parmtype == STACK_INT ? intparm : TOINT(floatparm);
    break;
  case VAR_INTWORDPTR:  /* Indirect word-sized integer */
    if (isreturn)
      save_retint(retparm, fp->parameter, get_integer(fp->parameter.address.offset));
    else {
      save_int(fp->parameter, get_integer(fp->parameter.address.offset));
    }
    store_integer(fp->parameter.address.offset, parmtype == STACK_INT ? intparm : TOINT(floatparm));
    break;
  case VAR_FLOATPTR:    /* Indirect eight-byte floating point intparm */
    if (isreturn)
      save_retfloat(retparm, fp->parameter, get_float(fp->parameter.address.offset));
    else {
      save_float(fp->parameter, get_float(fp->parameter.address.offset));
    }
    store_float(fp->parameter.address.offset, parmtype == STACK_INT ? TOFLOAT(intparm) : floatparm);
    break;
  case VAR_DOLSTRPTR: { /* Indirect string */
    basicstring descriptor;
    byte *sp;
    sp = &basicvars.memory[fp->parameter.address.offset];       /* This is too long to keep typing... */
/* Fake a descriptor for the original '$<string>' string */
    descriptor.stringlen = get_stringlen(fp->parameter.address.offset)+1;
    descriptor.stringaddr = alloc_string(descriptor.stringlen);
    if (descriptor.stringlen > 0)
     memmove(descriptor.stringaddr, sp, descriptor.stringlen);
    if (isreturn)
      save_retstring(retparm, fp->parameter, descriptor);       /* Save the '$<string>' string */
    else {
      save_string(fp->parameter, descriptor);   /* Save the '$<string>' string */
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
  default:              /* Bad parameter type */
    DEBUGFUNCMSGOUT;
    error(ERR_BROKEN, __LINE__, "evaluate");
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'push_singleparm' is called when a procedure or function has a single
** 32-bit integer parameter
*/
static void push_singleparm(formparm *fp, char *procname) {
  int32 intparm = 0;

  DEBUGFUNCMSGIN;
  expression();
  if (*basicvars.current != ')') {      /* Try to put out a meaningful error message */
    if (*basicvars.current == ',') {     /* Assume there is another parameter */
      DEBUGFUNCMSGOUT;
      error(ERR_TOOMANY, procname);
    } else {      /* Something else - Assume ')' is missing */
      DEBUGFUNCMSGOUT;
      error(ERR_RPMISS);
    }
  }
  basicvars.current++;  /* Skip the ')' */
  intparm = pop_anynum32();
  save_int(fp->parameter, *(fp->parameter.address.intaddr));
  *(fp->parameter.address.intaddr) = intparm;
  DEBUGFUNCMSGOUT;
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
  DEBUGFUNCMSGIN;
  basicvars.current++;  /* Skip the '(' */
  if (dp->simple)
    push_singleparm(dp->parmlist, base);
  else {
    push_oneparm(dp->parmlist, 1, base);
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'do_staticvar' is called to deal with a simple reference to a static
** variable, that is, one that is not followed by an indirection operator
*/
static void do_staticvar(void) {
  DEBUGFUNCMSGIN;
  push_int(basicvars.staticvars[*(basicvars.current+1)].varentry.varinteger);
  basicvars.current+=2;
  DEBUGFUNCMSGOUT;
}

/*
** 'do_statindvar' is one of the 'factor' functions. It deals with static
** variables that are followed by an indirection operator, pushing the
** value pointed at by the variable on to the Basic stack
*/
static void do_statindvar(void) {
  size_t address = basicvars.staticvars[*(basicvars.current+1)].varentry.varinteger;
  byte operator;

  DEBUGFUNCMSGIN;
  basicvars.current+=2;
  operator = *basicvars.current;
  basicvars.current++;
  (*factor_table[*basicvars.current])();
  address += pop_anynum64();
/* Now load the value on to the Basic stack */
  if (operator == '?') {                /* Byte-sized integer */
    push_int(basicvars.memory[address]);
  }
  else {                /* Word-sized integer */
    push_int(get_integer(address));
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'do_intzero' pushes the integer value 0 on to the Basic stack
*/
static void do_intzero(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;
  push_int(0);
  DEBUGFUNCMSGOUT;
}

/*
** 'do_intone' pushes the integer value 1 on to the Basic stack
*/
static void do_intone(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;
  push_int(1);
  DEBUGFUNCMSGOUT;
}

/*
** 'do_smallconst' pushes a small integer value on to the Basic stack
*/
static void do_smallconst(void) {
  DEBUGFUNCMSGIN;
  push_int(*(basicvars.current+1)+1);   /* +1 as values 1..256 are held as 0..255 */
  basicvars.current+=2; /* Skip 'smallconst' token and value */
  DEBUGFUNCMSGOUT;
}

/*
** 'do_intconst' pushes a 32-bit integer constant on to the Basic stack
*/
static void do_intconst(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;          /* Point current at binary version of number */
  push_int(GET_INTVALUE(basicvars.current));
  basicvars.current+=INTSIZE;
  DEBUGFUNCMSGOUT;
}

/*
** 'do_int64const' pushes a 64-bit integer constant on to the Basic stack
*/
static void do_int64const(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;          /* Point current at binary version of number */
  push_int64(GET_INT64VALUE(basicvars.current));
  basicvars.current+=INT64SIZE;
  DEBUGFUNCMSGOUT;
}

/*
** 'do_floatzero' pushes the floating point value 0.0 on to the
** Basic stack
*/
static void do_floatzero(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;
  push_float(0.0);
  DEBUGFUNCMSGOUT;
}

/*
** 'do_floatone' pushes the floating point value 1.0 on to the
** Basic stack
*/
static void do_floatone(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;
  push_float(1.0);
  DEBUGFUNCMSGOUT;
}

/*
** 'do_floatconst' pushes the floating point value that follows
** the token on to the Basic stack
*/
static void do_floatconst(void) {
  DEBUGFUNCMSGIN;
  push_float(get_fpvalue(basicvars.current));
  basicvars.current+=(FLOATSIZE+1);
  DEBUGFUNCMSGOUT;
}

/*
** 'do_intvar' handles a simple reference to a known 32-bit integer
** variable, pushing its value on to the stack. The variable is known
** to be not followed by an indirection operator
*/
static void do_intvar(void) {
  int32 *ip;

  DEBUGFUNCMSGIN;
  ip = GET_ADDRESS(basicvars.current, int32 *);
  basicvars.current+=LOFFSIZE+1;        /* Skip pointer */
  push_int(*ip);
  DEBUGFUNCMSGOUT;
}

/*
** 'do_uint8var' deals with simple references to a known unsigned
** 8-bit integer variable.
*/
static void do_uint8var(void) {
  uint8 *ip;

  DEBUGFUNCMSGIN;
  ip = GET_ADDRESS(basicvars.current, uint8 *);
  basicvars.current+=LOFFSIZE+1;        /* Skip pointer */
  push_uint8(*ip);
  DEBUGFUNCMSGOUT;
}

/*
** 'do_int64var' deals with simple references to a known 64-bit
** integer variable.
*/
static void do_int64var(void) {
  int64 *ip;

  DEBUGFUNCMSGIN;
  ip = GET_ADDRESS(basicvars.current, int64 *);
  basicvars.current+=LOFFSIZE+1;        /* Skip pointer */
  push_int64(*ip);
  DEBUGFUNCMSGOUT;
}

/*
** 'do_floatvar' deals with simple references to a known floating
** point variable
*/
static void do_floatvar(void) {
  float64 *fp;

  DEBUGFUNCMSGIN;
  fp = GET_ADDRESS(basicvars.current, float64 *);
  basicvars.current+=LOFFSIZE+1;        /* Skip pointer */
  push_float(*fp);
  DEBUGFUNCMSGOUT;
}

/*
** 'do_stringvar' handles references to a known string variable
*/
static void do_stringvar(void) {
  basicstring *sp;

  DEBUGFUNCMSGIN;
  sp = GET_ADDRESS(basicvars.current, basicstring *);
  basicvars.current+=LOFFSIZE+1;        /* Skip pointer */
  push_string(*sp);
  DEBUGFUNCMSGOUT;
}

/*
** 'do_arrayvar' handles references to entire arrays
*/
static void do_arrayvar(void) {
  variable *vp;

  DEBUGFUNCMSGIN;
  vp = GET_ADDRESS(basicvars.current, variable *);
  basicvars.current+=LOFFSIZE+2;                /* Skip pointer to array and ')' */
  push_array(vp->varentry.vararray, vp->varflags);
  DEBUGFUNCMSGOUT;
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

  DEBUGFUNCMSGIN;
  vp = GET_ADDRESS(basicvars.current, variable *);
  basicvars.current+=LOFFSIZE+1;        /* Skip pointer to variable */
  descriptor = vp->varentry.vararray;
  vartype = vp->varflags;
  if (descriptor->dimcount == 1) {      /* Array has only one dimension - Use faster code */
    expression();             /* Evaluate an array index */
    element = pop_anynum32();
    if (element < 0 || element >= descriptor->dimsize[0]) {
      DEBUGFUNCMSGOUT;
      error(ERR_BADINDEX, element, vp->varname);
    }
  }
  else {        /* Multi-dimensional array */
    maxdims = descriptor->dimcount;
    dimcount = 0;
    element = 0;
    do {        /* Gather the array indexes */
      expression();           /* Evaluate an array index */
      index = pop_anynum32();
      if (index < 0 || index >= descriptor->dimsize[dimcount]) {
        DEBUGFUNCMSGOUT;
        error(ERR_BADINDEX, index, vp->varname);
      }
      dimcount++;
      element+=index;
      if (*basicvars.current != ',') break;     /* No more array indexes expected */
      basicvars.current++;
      if (dimcount > maxdims) {  /* Too many dimensions */
        DEBUGFUNCMSGOUT;
        error(ERR_INDEXCO, vp->varname);
      }
      if (dimcount != maxdims) element = element*descriptor->dimsize[dimcount];
    } while (TRUE);
    if (dimcount != maxdims) {   /* Not enough dimensions */
      DEBUGFUNCMSGOUT;
      error(ERR_INDEXCO, vp->varname);
    }
  }
  if (*basicvars.current != ')') {
    DEBUGFUNCMSGOUT;
    error(ERR_RPMISS);
  }
  basicvars.current++;          /* Point at character after the ')' */
  if (*basicvars.current != '?' && *basicvars.current != '!') { /* Ordinary array reference */
    if (vartype == VAR_INTARRAY) {      /* Can push the array element on to the stack then go home */
      push_int(vp->varentry.vararray->arraystart.intbase[element]);
      DEBUGFUNCMSGOUT;
      return;
    }
    if (vartype == VAR_UINT8ARRAY) {    /* Can push the array element on to the stack then go home */
      push_uint8(vp->varentry.vararray->arraystart.uint8base[element]);
      DEBUGFUNCMSGOUT;
      return;
    }
    if (vartype == VAR_INT64ARRAY) {    /* Can push the array element on to the stack then go home */
      push_int64(vp->varentry.vararray->arraystart.int64base[element]);
      DEBUGFUNCMSGOUT;
      return;
    }
    if (vartype == VAR_FLOATARRAY) {
      push_float(vp->varentry.vararray->arraystart.floatbase[element]);
      DEBUGFUNCMSGOUT;
      return;
    }
    if (vartype == VAR_STRARRAY) {
      push_string(vp->varentry.vararray->arraystart.stringbase[element]);
      DEBUGFUNCMSGOUT;
      return;
    }
    DEBUGFUNCMSGOUT;
    error(ERR_BROKEN, __LINE__, "evaluate");    /* Sanity check */
  }
  else {        /* Array reference is followed by an indirection operator */
    switch(vartype) {
      case VAR_INTARRAY:   offset = vp->varentry.vararray->arraystart.intbase[element]; break;
      case VAR_UINT8ARRAY: offset = vp->varentry.vararray->arraystart.uint8base[element]; break;
      case VAR_INT64ARRAY: offset = vp->varentry.vararray->arraystart.int64base[element]; break;
      case VAR_FLOATARRAY: offset = TOINT64(vp->varentry.vararray->arraystart.floatbase[element]); break;
      default:
        DEBUGFUNCMSGOUT;
        error(ERR_TYPENUM);
    }
    operator = *basicvars.current;
    basicvars.current++;
    (*factor_table[*basicvars.current])();
    offset+=pop_anynum64();
    if (operator == '?') {      /* Byte-sized integer */
      push_int(basicvars.memory[offset]);
    }
    else {              /* Word-sized integer */
      push_int(get_integer(offset));
    }
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'do_indrefvar' handles references to dynamic variables that
** are followed by indirection operators
*/
static void do_indrefvar(void) {
  byte operator;
  size_t offset;

  DEBUGFUNCMSGIN;
  if (*basicvars.current == BASTOKEN_INTINDVAR) {    /* Fetch variable's value */
    offset = *GET_ADDRESS(basicvars.current, int32 *);
  } else if (*basicvars.current == BASTOKEN_INT64INDVAR) {   /* Fetch variable's value */
    offset = *GET_ADDRESS(basicvars.current, int64 *);
  } else {
    offset = TOINT64(*GET_ADDRESS(basicvars.current, float64 *));
  }
  basicvars.current+=LOFFSIZE+1;                /* Skip pointer to variable */
  operator = *basicvars.current;
  basicvars.current++;
  (*factor_table[*basicvars.current])();
  offset+=pop_anynum64();
#ifdef USE_SDL
  offset = m7offset(offset);
#endif /* USE_SDL */
  if (operator == '?') {        /* Byte-sized integer */
    push_int(basicvars.memory[offset]);
  }
  else {                /* Word-sized integer */
    push_int(get_integer(offset));
  }
  DEBUGFUNCMSGOUT;
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

  DEBUGFUNCMSGIN;
  base = get_srcaddr(basicvars.current);                /* Point 'base' at the start of the variable's name */
  np = skip_name(base);
  vp = find_variable(base, np-base);
  if (vp == NIL) {      /* Cannot find the variable */
    if (*(np-1) == '(' || *(np-1) == '[') {
      DEBUGFUNCMSGOUT;
      error(ERR_ARRAYMISS, tocstring(CAST(base, char *), np-base));     /* Unknown array */
    } else {
      DEBUGFUNCMSGOUT;
      error(ERR_VARMISS, tocstring(CAST(base, char *), np-base));       /* Unknown variable */
    }
  }
  vartype = vp->varflags;
  isarray = (vartype & VAR_ARRAY) != 0;
  if (isarray && vp->varentry.vararray == NIL) {  /* Array not dimensioned */
    DEBUGFUNCMSGOUT;
    error(ERR_NODIMS, vp->varname);
  }
  np = basicvars.current+LOFFSIZE+1;
  if (!isarray && (*np == '?' || *np == '!')) {         /* Variable is followed by an indirection operator */
    switch (vartype) {
    case VAR_INTWORD:
      *basicvars.current = BASTOKEN_INTINDVAR;
      set_address(basicvars.current, &vp->varentry.varinteger);
      break;
    case VAR_INTLONG:
      *basicvars.current = BASTOKEN_INT64INDVAR;
      set_address(basicvars.current, &vp->varentry.var64int);
      break;
    case VAR_FLOAT:
      *basicvars.current = BASTOKEN_FLOATINDVAR;
      set_address(basicvars.current, &vp->varentry.varfloat);
      break;
    case VAR_UINT8: 
      DEBUGFUNCMSGOUT;
      error(ERR_UNSUITABLEVAR);
    default:
      DEBUGFUNCMSGOUT;
      error(ERR_VARNUM);
    }
    do_indrefvar();
  }
  else {        /* Simple reference to variable or reference to an array */
    if (vartype == VAR_INTWORD) {
      *basicvars.current = BASTOKEN_INTVAR;
      set_address(basicvars.current, &vp->varentry.varinteger);
      do_intvar();
    }
    else if (vartype == VAR_UINT8) {
      *basicvars.current = BASTOKEN_UINT8VAR;
      set_address(basicvars.current, &vp->varentry.varu8int);
      do_uint8var();
    }
    else if (vartype == VAR_INTLONG) {
      *basicvars.current = BASTOKEN_INT64VAR;
      set_address(basicvars.current, &vp->varentry.var64int);
      do_int64var();
    }
    else if (vartype == VAR_FLOAT) {
      *basicvars.current = BASTOKEN_FLOATVAR;
      set_address(basicvars.current, &vp->varentry.varfloat);
      do_floatvar();
    }
    else if (vartype == VAR_STRINGDOL) {
      *basicvars.current = BASTOKEN_STRINGVAR;
      set_address(basicvars.current, &vp->varentry.varstring);
      do_stringvar();
    }
    else {      /* Array or array followed by an indirection operator */
      if (*np == ')') { /* Reference is to entire array */
        *basicvars.current = BASTOKEN_ARRAYVAR;
        set_address(basicvars.current, vp);
        do_arrayvar();
      }
      else {    /* Reference is to an array element */
        *basicvars.current = BASTOKEN_ARRAYREF;
        set_address(basicvars.current, vp);
        do_arrayref();
      }
    }
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'do_stringcon' pushes a descriptor for a simple string constant on
** to the Basic stack
*/
static void do_stringcon(void) {
  basicstring descriptor;

  DEBUGFUNCMSGIN;
  descriptor.stringaddr = TOSTRING(get_srcaddr(basicvars.current));
  descriptor.stringlen = GET_SIZE(basicvars.current+1+OFFSIZE);
  basicvars.current+=1+OFFSIZE+SIZESIZE;
  push_string(descriptor);
  DEBUGFUNCMSGOUT;
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

  DEBUGFUNCMSGIN;
  string = get_srcaddr(basicvars.current);
  length = GET_SIZE(basicvars.current+1+OFFSIZE);
  basicvars.current+=1+OFFSIZE+SIZESIZE;
  cp = alloc_string(length);
  if (length > 0) {
    srce = 0;
    for (dest = 0; dest < length; dest++) {
      cp[dest] = string[srce];
      if (string[srce] == '"') srce++;  /* Skip one '"' of '""' */
      srce++;
    }
  }
  push_strtemp(length, cp);
  DEBUGFUNCMSGOUT;
}

/*
** 'do_brackets' is called when a '(' is founf to handle the
**  expression in the brackets
*/
static void do_brackets(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;  /* Skip the '(' */
  expression();
  if (*basicvars.current != ')') {
    DEBUGFUNCMSGOUT;
    error(ERR_RPMISS);
  }
  basicvars.current++;
  DEBUGFUNCMSGOUT;
}

/*
** 'do_getbyte' handles the byte indirection operator, '?', pushing
** the byte addressed by the numeric value on top of the stack on to
** the stack
*/
static void do_getbyte(void) {
  DEBUGFUNCMSGIN;
  size_t offset = 0;
  basicvars.current++;          /* Skip '?' */
  (*factor_table[*basicvars.current])();
  offset = m7offset((size_t)pop_anynum64());
  push_int(basicvars.memory[offset]);
  DEBUGFUNCMSGOUT;
}

/*
** 'do_getword' handles the word indirection operator, '!', pushing the
** word addressed by the numeric value on top of the Basic stack on to
** the stack. The address of the word to be pushed is byte-aligned
*/
static void do_getword(void) {
  DEBUGFUNCMSGIN;
  size_t offset = 0;
  basicvars.current++;          /* Skip '!' */
  (*factor_table[*basicvars.current])();
  offset = m7offset((size_t)pop_anynum64());
  push_int(get_integer(offset));
  DEBUGFUNCMSGOUT;
}

/*
** 'do_getlong' handles the 64-bit int indirection operator, ']', pushing the
** 64-bit int addressed by the numeric value on top of the Basic stack on to
** the stack. The address of the 64-bit int to be pushed is byte-aligned
*/
static void do_getlong(void) {
  DEBUGFUNCMSGIN;
  size_t offset = 0;
  basicvars.current++;          /* Skip ']' */
  (*factor_table[*basicvars.current])();
  offset = m7offset((size_t)pop_anynum64());
  push_int64(get_int64(offset));
  DEBUGFUNCMSGOUT;
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

  DEBUGFUNCMSGIN;
  basicvars.current++;          /* Skip '$' */
  (*factor_table[*basicvars.current])();
  offset = m7offset((size_t)pop_anynum64());
  len = get_stringlen(offset);
  push_dolstring(len, CAST(&basicvars.memory[offset], char *));
  DEBUGFUNCMSGOUT;
}

/*
** 'do_getfloat' handles the unary floating point indirection operator, '|'.
** It pushes the value addressed by the numeric value on top of the stack on
** to the Basic stack
*/
static void do_getfloat(void) {
  size_t offset = 0;

  DEBUGFUNCMSGIN;
  basicvars.current++;          /* Skip '|' */
  (*factor_table[*basicvars.current])();
  offset = m7offset((size_t)pop_anynum64());
  push_float(get_float(offset));
  DEBUGFUNCMSGOUT;
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
  byte *tp = NULL;
  fnprocdef *dp = NULL;
  variable *vp = NULL;

  DEBUGFUNCMSGIN;
#ifdef TARGET_DJGPP
  if (stackavail()<DJGPPLIMIT) error(ERR_STACKFULL);
#endif
  basicvars.recdepth++;
  if (basicvars.recdepth > basicvars.maxrecdepth) {
    DEBUGFUNCMSGOUT;
    error(ERR_STACKFULL);
  }
  vp = GET_ADDRESS(basicvars.current, variable *);
  dp = vp->varentry.varfnproc;
  basicvars.current+=LOFFSIZE+1;        /* Skip pointer to function */

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
  if (sigsetjmp(*basicvars.local_restart, 1) == 0) {
    exec_fnstatements(dp->fnprocaddr);
    basicvars.recdepth--;
  } else {
/*
** Restart here after an error in the function or something
** called from it is trapped by ON ERROR LOCAL
*/
    reset_opstack();
    exec_fnstatements(basicvars.error_handler.current);
  }

/* Restore stuff after the call has ended */
  basicvars.recdepth--;
  basicvars.current = tp;       /* Note that 'basicvars.current' is preserved over the function call in 'tp' */
  DEBUGFUNCMSGOUT;
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

  DEBUGFUNCMSGIN;
  base = get_srcaddr(basicvars.current);                /* Point 'base' at start of function's name */
  if (*base != BASTOKEN_FN) {
    DEBUGFUNCMSGOUT;
    error(ERR_NOTAFN);       /* Ensure a function is being called */
  }
  tp = skip_name(base);
  gotparms = *(tp-1) == '(';
  if (gotparms) tp--;   /* '(' found but it is not part of name */
  vp = find_fnproc(base, tp-base);
  dp = vp->varentry.varfnproc;
  *basicvars.current = BASTOKEN_FNPROCALL;
  set_address(basicvars.current, vp);
  if (gotparms) {       /* PROC/FN call has some parameters */
    if (dp->parmlist == NIL) {   /* Got a '(' but function has no parameters */
      DEBUGFUNCMSGOUT;
      error(ERR_TOOMANY, vp->varname);
    }
  }
  else {        /* No parameters found */
    if (dp->parmlist != NIL) {        /* But function should have them */
      DEBUGFUNCMSGOUT;
      error(ERR_NOTENUFF, vp->varname+1);
    }
  }
  do_function();        /* Call the function */
  DEBUGFUNCMSGOUT;
}

/* =============== Operators =============== */

/*
** 'want_number' is called when a numeric stack entry type is needed
** but an entry of another type was found instead
*/
static void want_number(void) {
  stackitem baditem;

  DEBUGFUNCMSGIN;
  baditem = GET_TOPITEM;
  if (baditem==STACK_STRING || baditem==STACK_STRTEMP) {         /* Numeric operand required */
    DEBUGFUNCMSGOUT;
    error(ERR_TYPENUM);
  } else if (baditem>STACK_UNKNOWN && baditem <= STACK_SATEMP) {   /* Operator is not defined for this operand type */
    DEBUGFUNCMSGOUT;
    error(ERR_BADARITH);
  } else {        /* Unrecognisable operand - Stack is probably corrupt */
    fprintf(stderr, "Baditem = %d, sp = %p, safe=%p, opstop=%p\n", baditem,
      basicvars.stacktop.bytesp, basicvars.safestack.bytesp, basicvars.opstop);
    DEBUGFUNCMSGOUT;
    error(ERR_BROKEN, __LINE__, "evaluate");
  }
  /* This should never be reached */
  DEBUGFUNCMSGOUT;
}

/*
** 'want_string' is called when a string stack entry type is needed
** but an entry of another type was found instead
*/
static void want_string(void) {
  stackitem baditem;

  DEBUGFUNCMSGIN;
  baditem = GET_TOPITEM;
  if (baditem == STACK_INT || baditem == STACK_UINT8 || baditem == STACK_INT64 || baditem == STACK_FLOAT) {              /* String operand required */
    DEBUGFUNCMSGOUT;
    error(ERR_TYPESTR);
  } else if (baditem > STACK_UNKNOWN && baditem <= STACK_SATEMP) {  /* Operator is not defined for this operand type */
    DEBUGFUNCMSGOUT;
    error(ERR_BADARITH);
  } else {        /* Unrecognisable operand - Stack is probably corrupt */
    DEBUGFUNCMSGOUT;
    error(ERR_BROKEN, __LINE__, "evaluate");
  }
  /* This should never be reached */
  DEBUGFUNCMSGOUT;
}

/*
** 'want_array' is called when an array stack entry type is required
** but an entry of another type is found
*/
static void want_array(void) {
  DEBUGFUNCMSGIN;
  DEBUGFUNCMSGOUT;
  error(ERR_VARARRAY);
}

/*
** 'eval_badcall' is called when an invalid stack entry type is found
*/
static void eval_badcall(void) {
  DEBUGFUNCMSGIN;
  DEBUGFUNCMSGOUT;
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

  DEBUGFUNCMSGIN;
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
    DEBUGFUNCMSGOUT;
    error(ERR_BROKEN, __LINE__, "evaluate");            /* Passed bad array type */
  }
  if (base == NIL) {   /* Not enough room on stack to create array */
    DEBUGFUNCMSGOUT;
    error(ERR_NOROOM);
  }
  push_arraytemp(&result, arraytype);
  DEBUGFUNCMSGOUT;
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
  
  DEBUGFUNCMSGIN;
  rhitem = GET_TOPITEM;
  rhint=pop_anyint();
  lhitem = GET_TOPITEM;
  if (TOPITEMISINT) {
    push_varyint(pop_anyint() + rhint);
  } else if (lhitem == STACK_FLOAT)
    INCR_FLOAT(TOFLOAT(rhint)); /* float+int - Update value on stack in place */
  else if (TOPITEMISNUMARRAY) {        /* <array>+<integer value> */
    basicarray *lharray = pop_array();
    int32 n;
    if (lhitem == STACK_INTARRAY) {
      if (rhitem == STACK_INT64) {
        int64 *srce = lharray->arraystart.int64base;
        int64 *base = make_array(VAR_INTLONG, lharray);
        for (n = 0; n < lharray->arrsize; n++) base[n] = srce[n]+rhint;
      } else {
        int32 *srce = lharray->arraystart.intbase;
        int32 *base = make_array(VAR_INTWORD, lharray);
        for (n = 0; n < lharray->arrsize; n++) base[n] = (int32)(srce[n]+rhint);
      }
    } else if (lhitem == STACK_UINT8ARRAY) {
      if (rhitem == STACK_INT) {
        uint8 *srce = lharray->arraystart.uint8base;
        uint8 *base = make_array(VAR_UINT8, lharray);
        for (n = 0; n < lharray->arrsize; n++) base[n] = (uint8)(srce[n]+rhint);
      } else if (rhitem == STACK_UINT8) {
        int32 *srce = lharray->arraystart.intbase;
        int32 *base = make_array(VAR_INTWORD, lharray);
        for (n = 0; n < lharray->arrsize; n++) base[n] = (int32)(srce[n]+rhint);
      } else { /* STACK_INT64 */
        int64 *srce = lharray->arraystart.int64base;
        int64 *base = make_array(VAR_INTLONG, lharray);
        for (n = 0; n < lharray->arrsize; n++) base[n] = srce[n]+rhint;
      }
    } else if (lhitem == STACK_INT64ARRAY) {
      int64 *srce = lharray->arraystart.int64base;
      int64 *base = make_array(VAR_INTLONG, lharray);
      for (n = 0; n < lharray->arrsize; n++) base[n] = srce[n]+rhint;
    } else {
      float64 *srce = lharray->arraystart.floatbase;
      float64 *base = make_array(VAR_FLOAT, lharray);
      floatvalue = TOFLOAT(rhint);
      for (n = 0; n < lharray->arrsize; n++) base[n] = srce[n]+floatvalue;
    }
  } else if (lhitem == STACK_FATEMP) {  /* <float array>+<integer value> */
    basicarray lharray = pop_arraytemp();
    float64 *base = lharray.arraystart.floatbase;
    int32 n;
    floatvalue = TOFLOAT(rhint);
    for (n = 0; n < lharray.arrsize; n++) base[n]+=floatvalue;
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_fvplus' deals with addition when the right-hand operand is
** a floating point value. All versions of the operator are dealt with
** by this function
*/
static void eval_fvplus(void) {
  stackitem lhitem;

  DEBUGFUNCMSGIN;
  floatvalue = pop_float();       /* Top item on Basic stack is right-hand operand */
  lhitem = GET_TOPITEM;
  if (TOPITEMISINT) {
    floatvalue+=TOFLOAT(pop_anyint());  /* This has to be split otherwise the macro */
    push_float(floatvalue);             /* expansion of PUSH_FLOAT goes wrong */
  } else if (lhitem == STACK_FLOAT)
    INCR_FLOAT(floatvalue);
  else if (TOPITEMISNUMARRAY) {        /* <array>+<float value> */
    basicarray *lharray = pop_array();
    float64 *base = make_array(VAR_FLOAT, lharray);
    int32 n;
    if (lhitem == STACK_INTARRAY) {
      int32 *srce = lharray->arraystart.intbase;
      for (n = 0; n < lharray->arrsize; n++) base[n] = TOFLOAT(srce[n])+floatvalue;
    } else if (lhitem == STACK_UINT8ARRAY) {
      uint8 *srce = lharray->arraystart.uint8base;
      for (n = 0; n < lharray->arrsize; n++) base[n] = TOFLOAT(srce[n])+floatvalue;
    } else if (lhitem == STACK_INT64ARRAY) {
      int64 *srce = lharray->arraystart.int64base;
      for (n = 0; n < lharray->arrsize; n++) base[n] = TOFLOAT(srce[n])+floatvalue;
    } else {
      float64 *srce = lharray->arraystart.floatbase;
      for (n = 0; n < lharray->arrsize; n++) base[n] = srce[n]+floatvalue;
    }
  } else if (lhitem == STACK_FATEMP) {  /* <float array>+<float value> */
    basicarray lharray = pop_arraytemp();
    float64 *base = lharray.arraystart.floatbase;
    int32 n;
    for (n = 0; n < lharray.arrsize; n++) base[n]+=floatvalue;
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
  DEBUGFUNCMSGOUT;
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

  DEBUGFUNCMSGIN;
  rhitem = GET_TOPITEM;
  rhstring = pop_string();
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_STRING || lhitem == STACK_STRTEMP) {
    if (rhstring.stringlen == 0) return;        /* Do nothing if right-hand string is of zero length */
    lhstring = pop_string();
    newlen = lhstring.stringlen+rhstring.stringlen;
    if (newlen > MAXSTRING) {
      DEBUGFUNCMSGOUT;
      error(ERR_STRINGLEN);
    }
    if (lhitem == STACK_STRTEMP) {      /* Reuse left-hand string as it is a temporary */
      cp = resize_string(lhstring.stringaddr, lhstring.stringlen, newlen);
      lhstring.stringaddr = cp;
      memmove(cp+lhstring.stringlen, rhstring.stringaddr, rhstring.stringlen);
    } else {    /* Any other case - Create a new string temporary */
      cp = alloc_string(newlen);
      memmove(cp, lhstring.stringaddr, lhstring.stringlen);
      memmove(cp+lhstring.stringlen, rhstring.stringaddr, rhstring.stringlen);
    }
    if (rhitem == STACK_STRTEMP) free_string(rhstring);
    push_strtemp(newlen, cp);
  } else if (lhitem == STACK_STRARRAY) {        /* <array>+<string> */
    basicarray *lharray;
    basicstring *base, *srce;
    int32 n;
    if (rhstring.stringlen == 0) return;        /* Do nothing if right-hand string is of zero length */
    lharray = pop_array();
    srce = lharray->arraystart.stringbase;
    base = make_array(VAR_STRINGDOL, lharray);
    for (n = 0; n < lharray->arrsize; n++) {               /* Append right hand string to each element of string array */
      newlen = srce[n].stringlen+rhstring.stringlen;
      if (newlen > MAXSTRING) {
        DEBUGFUNCMSGOUT;
        error(ERR_STRINGLEN);
      }
      cp = alloc_string(newlen);
      memmove(cp, srce[n].stringaddr, srce[n].stringlen);
      memmove(cp+srce[n].stringlen, rhstring.stringaddr, rhstring.stringlen);
      base[n].stringaddr = cp;
      base[n].stringlen = newlen;
    }
    if (rhitem == STACK_STRTEMP) free_string(rhstring);
  } else want_string();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_iaplus' deals with addition when the right-hand operand is
** a 32-bit integer array. All versions of the operator are dealt with
** by this function
*/
static void eval_iaplus(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n;
  int32 *rhsrce;

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  rhsrce = rharray->arraystart.intbase;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT || lhitem == STACK_UINT8) {
    int32 lhint32 = pop_anyint();
    int32 *base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < rharray->arrsize; n++) base[n] = lhint32+rhsrce[n];
  } else if (lhitem == STACK_INT64) {
    int64 lhint64 = pop_int64();
    int64 *base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < rharray->arrsize; n++) base[n] = lhint64+rhsrce[n];
  } else if (lhitem == STACK_FLOAT) {   /* <float>+<int array> */
    float64 *base = make_array(VAR_FLOAT, rharray);
    floatvalue = pop_float();
    for (n = 0; n < rharray->arrsize; n++) base[n] = floatvalue+TOFLOAT(rhsrce[n]);
  } else if (TOPITEMISNUMARRAY) {
    basicarray *lharray = pop_array();
    check_arrays(lharray, rharray);
    if (lhitem == STACK_INTARRAY) {        /* <int array>+<int array> */
      int32 *base = make_array(VAR_INTWORD, rharray);
      int32 *lhsrce = lharray->arraystart.intbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n]+rhsrce[n];
    } else if (lhitem == STACK_UINT8ARRAY) {      /* <uint8 array>+<int array> */
      int32 *base = make_array(VAR_INTWORD, rharray);
      uint8 *lhsrce = lharray->arraystart.uint8base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n]+rhsrce[n];
    } else if (lhitem == STACK_INT64ARRAY) {      /* <int64 array>+<int array> */
      int64 *base = make_array(VAR_INTLONG, rharray);
      int64 *lhsrce = lharray->arraystart.int64base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n]+rhsrce[n];
    } else if (lhitem == STACK_FLOATARRAY) {      /* <float array>+<int array> */
      float64 *base = make_array(VAR_FLOAT, rharray);
      float64 *lhsrce = lharray->arraystart.floatbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n]+TOFLOAT(rhsrce[n]);
    } 
  } else if (lhitem == STACK_FATEMP) {          /* <float array>+<int array> */
    basicarray lharray = pop_arraytemp();
    float64 *lhsrce = lharray.arraystart.floatbase;
    check_arrays(&lharray, rharray);
    for (n = 0; n < rharray->arrsize; n++) lhsrce[n]+=TOFLOAT(rhsrce[n]);
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_iu8aplus' deals with addition when the right-hand operand is
** an unsigned 8-bit integer array. All versions of the operator are
** dealt with by this function
*/
static void eval_iu8aplus(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n;
  uint8 *rhsrce;

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  rhsrce = rharray->arraystart.uint8base;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT) {
    int32 lhint32 = pop_int();
    int32 *base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < rharray->arrsize; n++) base[n] = lhint32+rhsrce[n];
  } else if (lhitem == STACK_UINT8) {
    int32 lhint32 = pop_uint8();
    uint8 *base = make_array(VAR_UINT8, rharray);
    for (n = 0; n < rharray->arrsize; n++) base[n] = lhint32+rhsrce[n];
  } else if (lhitem == STACK_INT64) {
    int64 lhint64 = pop_int64();
    int64 *base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < rharray->arrsize; n++) base[n] = lhint64+rhsrce[n];
  } else if (lhitem == STACK_FLOAT) {   /* <float>+<int array> */
    float64 *base = make_array(VAR_FLOAT, rharray);
    floatvalue = pop_float();
    for (n = 0; n < rharray->arrsize; n++) base[n] = floatvalue+TOFLOAT(rhsrce[n]);
  } else if (TOPITEMISNUMARRAY) {
    basicarray *lharray = pop_array();
    check_arrays(lharray, rharray);
    if (lhitem == STACK_INTARRAY) {        /* <int array>+<uint8 array> */
      int32 *base = make_array(VAR_INTWORD, rharray);
      int32 *lhsrce = lharray->arraystart.intbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n]+rhsrce[n];
    } else if (lhitem == STACK_UINT8ARRAY) {      /* <uint8 array>+<uint8 array> */
      uint8 *base = make_array(VAR_UINT8, rharray);
      uint8 *lhsrce = lharray->arraystart.uint8base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n]+rhsrce[n];
    } else if (lhitem == STACK_INT64ARRAY) {      /* <int64 array>+<uint8 array> */
      int64 *base = make_array(VAR_INTLONG, rharray);
      int64 *lhsrce = lharray->arraystart.int64base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n]+rhsrce[n];
    } else if (lhitem == STACK_FLOATARRAY) {      /* <float array>+<uint8 array> */
      float64 *base = make_array(VAR_FLOAT, rharray);
      float64 *lhsrce = lharray->arraystart.floatbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n]+TOFLOAT(rhsrce[n]);
    }
  } else if (lhitem == STACK_FATEMP) {          /* <float array>+<int array> */
    basicarray lharray = pop_arraytemp();
    float64 *lhsrce = lharray.arraystart.floatbase;
    check_arrays(&lharray, rharray);
    for (n = 0; n < rharray->arrsize; n++) lhsrce[n]+=TOFLOAT(rhsrce[n]);
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_iaplus' deals with addition when the right-hand operand is
** a 64-bit integer array. All versions of the operator are dealt with
** by this function
*/
static void eval_i64aplus(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n;
  int64 *rhsrce;
  int64 lhint=0;

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  rhsrce = rharray->arraystart.int64base;
  lhitem = GET_TOPITEM;
  if (TOPITEMISINT) {
    int64 *base = make_array(VAR_INTLONG, rharray);
    lhitem=pop_anyint();
    for (n = 0; n < rharray->arrsize; n++) base[n] = lhint+rhsrce[n];
  } else if (lhitem == STACK_FLOAT) {   /* <float>+<int array> */
    float64 *base = make_array(VAR_FLOAT, rharray);
    floatvalue = pop_float();
    for (n = 0; n < rharray->arrsize; n++) base[n] = floatvalue+TOFLOAT(rhsrce[n]);
  } else if (TOPITEMISNUMARRAY) {
    basicarray *lharray = pop_array();
    check_arrays(lharray, rharray);
    if (lhitem == STACK_INTARRAY) {        /* <int array>+<int64 array> */
      int64 *base = make_array(VAR_INTLONG, rharray);
      int32 *lhsrce = lharray->arraystart.intbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n]+rhsrce[n];
    } else if (lhitem == STACK_UINT8ARRAY) {      /* <uint8 array>+<int64 array> */
      int64 *base = make_array(VAR_INTLONG, rharray);;
      uint8 *lhsrce = lharray->arraystart.uint8base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n]+rhsrce[n];
    } else if (lhitem == STACK_INT64ARRAY) {      /* <int64 array>+<int64 array> */
      int64 *base = make_array(VAR_INTLONG, rharray);
      int64 *lhsrce = lharray->arraystart.int64base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n]+rhsrce[n];
    } else if (lhitem == STACK_FLOATARRAY) {      /* <float array>+<int64 array> */
      float64 *base = make_array(VAR_FLOAT, rharray);
      float64 *lhsrce = lharray->arraystart.floatbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n]+TOFLOAT(rhsrce[n]);
    }
  } else if (lhitem == STACK_FATEMP) {          /* <float array>+<int array> */
    basicarray lharray = pop_arraytemp();
    float64 *lhsrce = lharray.arraystart.floatbase;
    check_arrays(&lharray, rharray);
    for (n = 0; n < rharray->arrsize; n++) lhsrce[n]+=TOFLOAT(rhsrce[n]);
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_faplus' deals with addition when the right-hand operand is
** a floating point array. All versions of the operator are dealt with
** by this function
*/
static void eval_faplus(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n;
  float64 *base, *rhsrce;

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  rhsrce = rharray->arraystart.floatbase;
  lhitem = GET_TOPITEM;
  if (TOPITEMISNUM) {   /* <int or float>+<float array> or <uint8>+<float array> */
    floatvalue = pop_anynumfp();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < rharray->arrsize; n++) base[n] = floatvalue+rhsrce[n];
  } else if (TOPITEMISNUMARRAY) {
    basicarray *lharray = pop_array();
    check_arrays(lharray, rharray);
    base = make_array(VAR_FLOAT, rharray);
    if (lhitem == STACK_INTARRAY) {        /* <int array>+<float array> */
      int32 *lhsrce = lharray->arraystart.intbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = TOFLOAT(lhsrce[n])+rhsrce[n];
    } else if (lhitem == STACK_UINT8ARRAY) {      /* <uint8 array>+<float array> */
      uint8 *lhsrce = lharray->arraystart.uint8base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = TOFLOAT(lhsrce[n])+rhsrce[n];
    } else if (lhitem == STACK_INT64ARRAY) {      /* <int64 array>+<float array> */
      int64 *lhsrce = lharray->arraystart.int64base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = TOFLOAT(lhsrce[n])+rhsrce[n];
    } else if (lhitem == STACK_FLOATARRAY) {      /* <float array>+<float array> */
      float64 *lhsrce = lharray->arraystart.floatbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n]+rhsrce[n];
    }
  } else if (lhitem == STACK_FATEMP) {          /* <float array>+<float array> */
    basicarray lharray = pop_arraytemp();
    float64 *lhsrce = lharray.arraystart.floatbase;
    check_arrays(&lharray, rharray);
    for (n = 0; n < rharray->arrsize; n++) lhsrce[n]+=rhsrce[n];
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_saplus' deals with addition when the right-hand operand is
** a string array. All versions of the operator are dealt with
** by this function
*/
static void eval_saplus(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n;
  basicstring *base, *rhsrce;

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  rhsrce = rharray->arraystart.stringbase;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_STRING || lhitem == STACK_STRTEMP) {
    int32 newlen;
    char *cp;
    basicstring lhstring = pop_string();
    if (lhstring.stringlen == 0) {      /* Do nothing if left-hand string is of zero length */
      push_array(rharray, VAR_STRINGDOL);
      return;
    }
    base = make_array(VAR_STRINGDOL, rharray);
    for (n = 0; n < rharray->arrsize; n++) {               /* Prepend left-hand string to each element of string array */
      newlen = rhsrce[n].stringlen + lhstring.stringlen;
      if (newlen > MAXSTRING) {
        DEBUGFUNCMSGOUT;
        error(ERR_STRINGLEN);
      }
      cp = alloc_string(newlen);
      memmove(cp, lhstring.stringaddr, lhstring.stringlen);
      memmove(cp + lhstring.stringlen, rhsrce[n].stringaddr, rhsrce[n].stringlen);
      base[n].stringaddr = cp;
      base[n].stringlen = newlen;
    }
    if (lhitem == STACK_STRTEMP) free_string(lhstring);
  } else if (lhitem == STACK_STRARRAY) {  /* <string array>+<string array> */
    char *cp;
    int32 newlen;
    basicarray *lharray = pop_array();
    basicstring *lhsrce = lharray->arraystart.stringbase;
    check_arrays(lharray, rharray);
    base = make_array(VAR_STRINGDOL, rharray);
    for (n = 0; n < rharray->arrsize; n++) {               /* Prepend left-hand string to each element of string array */
      newlen = lhsrce[n].stringlen + rhsrce[n].stringlen;
      if (newlen > MAXSTRING) {
        DEBUGFUNCMSGOUT;
        error(ERR_STRINGLEN);
      }
      cp = alloc_string(newlen);
      memmove(cp, lhsrce[n].stringaddr, lhsrce[n].stringlen);
      memmove(cp + lhsrce[n].stringlen, rhsrce[n].stringaddr, rhsrce[n].stringlen);
      base[n].stringaddr = cp;
      base[n].stringlen = newlen;
    }
  } else if (lhitem == STACK_SATEMP) {    /* <string array>+<string array> */
    char *cp;
    int32 newlen;
    basicarray lharray = pop_arraytemp();
    basicstring *lhsrce = lharray.arraystart.stringbase;
    check_arrays(&lharray, rharray);
    for (n = 0; n < rharray->arrsize; n++) {               /* Concatenate left-hand and right-hand strings of each array element */
      newlen = lhsrce[n].stringlen + rhsrce[n].stringlen;
      if (newlen > MAXSTRING) {
        DEBUGFUNCMSGOUT;
        error(ERR_STRINGLEN);
      }
      cp = resize_string(lhsrce[n].stringaddr, lhsrce[n].stringlen, newlen);
      memmove(cp + lhsrce[n].stringlen, rhsrce[n].stringaddr, rhsrce[n].stringlen);
      lhsrce[n].stringaddr = cp;
      lhsrce[n].stringlen = newlen;
    }
    push_arraytemp(&lharray, VAR_STRINGDOL);
  } else want_string();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_ivminus' deals with subtraction when the right-hand operand is
** any integer value. All versions of the operator are dealt with
** by this function
*/
static void eval_ivminus(void) {
  stackitem lhitem, rhitem;
  int64 rhint = 0;

  DEBUGFUNCMSGIN;
  rhitem = GET_TOPITEM;
  rhint = pop_anyint();
  lhitem = GET_TOPITEM;
  if (TOPITEMISINT) {   /* Branch according to type of left-hand operand */
    if (matrixflags.legacyintmaths && is8or32int(rhitem) && is8or32int(lhitem)) {
      push_int(pop_int() - rhint);
    } else {
      push_varyint(pop_anyint() - rhint);
    }
  } else if (lhitem == STACK_FLOAT) {
    /* Use float80 space - doesn't help on ARM but doesn't hurt it */
    float80 fltmp = (float80)pop_float() - (float80)rhint;
    if (fltmp == (int64)fltmp) {
      push_varyint((int64)fltmp);
    } else {
      push_float((float64)fltmp);
    }
    //DECR_FLOAT(TOFLOAT(rhint));
  } else if (TOPITEMISNUMARRAY) {      /* <array>-<integer value> */
    basicarray *lharray = pop_array();
    int32 n;
    if (lhitem == STACK_INTARRAY) {
      if (rhitem == STACK_INT64) {
        int64 *srce = lharray->arraystart.int64base;
        int64 *base = make_array(VAR_INTLONG, lharray);
        for (n = 0; n < lharray->arrsize; n++) base[n] = srce[n]-rhint;
      } else {
        int32 *srce = lharray->arraystart.intbase;
        int32 *base = make_array(VAR_INTWORD, lharray);
        for (n = 0; n < lharray->arrsize; n++) base[n] = (int32)(srce[n]-rhint);
      }
    } else if (lhitem == STACK_UINT8ARRAY) {
      if (rhitem == STACK_INT) {
        uint8 *srce = lharray->arraystart.uint8base;
        uint8 *base = make_array(VAR_UINT8, lharray);
        for (n = 0; n < lharray->arrsize; n++) base[n] = (uint8)(srce[n]-rhint);
      } else if (rhitem == STACK_UINT8) {
        int32 *srce = lharray->arraystart.intbase;
        int32 *base = make_array(VAR_INTWORD, lharray);
        for (n = 0; n < lharray->arrsize; n++) base[n] = (int32)(srce[n]-rhint);
      } else { /* STACK_INT64 */
        int64 *srce = lharray->arraystart.int64base;
        int64 *base = make_array(VAR_INTLONG, lharray);
        for (n = 0; n < lharray->arrsize; n++) base[n] = srce[n]-rhint;
      }
    } else if (lhitem == STACK_INT64ARRAY) {
      int64 *srce = lharray->arraystart.int64base;
      int64 *base = make_array(VAR_INTLONG, lharray);
      for (n = 0; n < lharray->arrsize; n++) base[n] = srce[n] - rhint;
    } else {
      float64 *srce = lharray->arraystart.floatbase;
      float64 *base = make_array(VAR_FLOAT, lharray);
      floatvalue = TOFLOAT(rhint);
      for (n = 0; n < lharray->arrsize; n++) {
        base[n] = (float64)((float80)srce[n] - (float80)floatvalue);
      }
    }
  } else if (lhitem == STACK_FATEMP) {  /* <float array>-<integer value> */
    basicarray lharray = pop_arraytemp();
    float64 *base = lharray.arraystart.floatbase;
    int32 n;
    floatvalue = TOFLOAT(rhint);
    for (n = 0; n < lharray.arrsize; n++) base[n] -= floatvalue;
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_fvminus' deals with subtraction when the right-hand operand is
** a floating point value
*/
static void eval_fvminus(void) {
  stackitem lhitem;
  floatvalue = pop_float();

  DEBUGFUNCMSGIN;
  lhitem = GET_TOPITEM;
  if (TOPITEMISINT) {   /* <int>-<float> */
    floatvalue = TOFLOAT(pop_anyint()) - floatvalue;
    push_float(floatvalue);
  } else if (lhitem == STACK_FLOAT) {
    /* Use float80 space - doesn't help on ARM but doesn't hurt it */
    float80 fltmp = (float80)pop_float() - (float80)floatvalue;
    if (fltmp == (int64)fltmp) {
      push_varyint((int64)fltmp);
    } else {
      push_float((float64)fltmp);
    }
    // DECR_FLOAT(floatvalue);
  } else if (TOPITEMISNUMARRAY) {    /* <array>-<float value> */
    basicarray *lharray = pop_array();
    float64 *base = make_array(VAR_FLOAT, lharray);
    int32 n;
    if (lhitem == STACK_INTARRAY) {
      int32 *srce = lharray->arraystart.intbase;
      for (n = 0; n < lharray->arrsize; n++) base[n] = TOFLOAT(srce[n]) - floatvalue;
    } else if (lhitem == STACK_UINT8ARRAY) {
      uint8 *srce = lharray->arraystart.uint8base;
      for (n = 0; n < lharray->arrsize; n++) base[n] = TOFLOAT(srce[n]) - floatvalue;
    } else if (lhitem == STACK_INT64ARRAY) {
      int64 *srce = lharray->arraystart.int64base;
      for (n = 0; n < lharray->arrsize; n++) base[n] = TOFLOAT(srce[n]) - floatvalue;
    } else {
      float64 *srce = lharray->arraystart.floatbase;
      for (n = 0; n < lharray->arrsize; n++) base[n] = srce[n] - floatvalue;
    }
  } else if (lhitem == STACK_FATEMP) {  /* <float array>-<float value> */
    basicarray lharray = pop_arraytemp();
    float64 *base = lharray.arraystart.floatbase;
    int32 n;
    for (n = 0; n < lharray.arrsize; n++) base[n] -= floatvalue;
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_iaminus' deals with subtraction when the right-hand operand is
** a 32-bit integer array. All versions of the operator are dealt with
** by this function
*/
static void eval_iaminus(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n;
  int32 *rhsrce;

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  rhsrce = rharray->arraystart.intbase;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT || lhitem == STACK_UINT8) {                   /* <int>-<int array> */
    int32 lhint = pop_anyint();
    int32 *base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < rharray->arrsize; n++) base[n] = lhint - rhsrce[n];
  } else if (lhitem == STACK_INT64) {           /* <int64>-<int array> */
    int64 lhint = pop_int64();
    int64 *base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < rharray->arrsize; n++) base[n] = lhint - rhsrce[n];
  } else if (lhitem == STACK_FLOAT) {           /* <float>-<int array> */
    float64 *base = make_array(VAR_FLOAT, rharray);
    floatvalue = pop_float();
    for (n = 0; n < rharray->arrsize; n++) base[n] = floatvalue - TOFLOAT(rhsrce[n]);
  } else if (TOPITEMISNUMARRAY) {
    basicarray *lharray = pop_array();
    check_arrays(lharray, rharray);
    if (lhitem == STACK_INTARRAY) {        /* <int array>-<int array> */
      int32 *base = make_array(VAR_INTWORD, rharray);
      int32 *lhsrce = lharray->arraystart.intbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n] - rhsrce[n];
    } else if (lhitem == STACK_UINT8ARRAY) {      /* <uint8 array>-<int array> */
      int32 *base = make_array(VAR_INTWORD, rharray);
      uint8 *lhsrce = lharray->arraystart.uint8base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n] - rhsrce[n];
    } else if (lhitem == STACK_INT64ARRAY) {      /* <int64 array>-<int array> */
      int64 *base = make_array(VAR_INTLONG, rharray);
      int64 *lhsrce = lharray->arraystart.int64base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n] - rhsrce[n];
    } else if (lhitem == STACK_FLOATARRAY) {      /* <float array>-<int array> */
      float64 *base = make_array(VAR_FLOAT, rharray);
      float64 *lhsrce = lharray->arraystart.floatbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n] - TOFLOAT(rhsrce[n]);
    }
  } else if (lhitem == STACK_FATEMP) {          /* <float array>-<int array> */
    basicarray lharray = pop_arraytemp();
    float64 *lhsrce = lharray.arraystart.floatbase;
    check_arrays(&lharray, rharray);
    for (n = 0; n < rharray->arrsize; n++) lhsrce[n] -= TOFLOAT(rhsrce[n]);
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_iu8aminus' deals with subtraction when the right-hand operand is
** an unsigned 8-bit integer array. All versions of the operator are
** dealt with by this function
*/
static void eval_iu8aminus(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n;
  uint8 *rhsrce;

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  rhsrce = rharray->arraystart.uint8base;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT) {                    /* <int>-<uint8 array> */
    int32 lhint = pop_int();
    int32 *base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < rharray->arrsize; n++) base[n] = lhint - rhsrce[n];
  } else if (lhitem == STACK_UINT8) {           /* <uint8>-<uint8 array> */
    uint8 lhint = pop_uint8();
    uint8 *base = make_array(VAR_UINT8, rharray);
    for (n = 0; n < rharray->arrsize; n++) base[n] = lhint - rhsrce[n];
  } else if (lhitem == STACK_INT64) {           /* <int64>-<uint8 array> */
    int64 lhint = pop_int64();
    int64 *base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < rharray->arrsize; n++) base[n] = lhint - rhsrce[n];
  } else if (lhitem == STACK_FLOAT) {           /* <float>-<uint8 array> */
    float64 *base = make_array(VAR_FLOAT, rharray);
    floatvalue = pop_float();
    for (n = 0; n < rharray->arrsize; n++) base[n] = floatvalue - TOFLOAT(rhsrce[n]);
  } else if (TOPITEMISNUMARRAY) {
    basicarray *lharray = pop_array();
    check_arrays(lharray, rharray);
    if (lhitem == STACK_INTARRAY) {        /* <int array>-<uint8 array> */
      int32 *base = make_array(VAR_INTWORD, rharray);
      int32 *lhsrce = lharray->arraystart.intbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n] - rhsrce[n];
    } else if (lhitem == STACK_UINT8ARRAY) {      /* <uint8 array>-<uint8 array> */
      uint8 *base = make_array(VAR_INTWORD, rharray);
      uint8 *lhsrce = lharray->arraystart.uint8base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n] - rhsrce[n];
    } else if (lhitem == STACK_INT64ARRAY) {      /* <int64 array>-<uint8 array> */
      int64 *base = make_array(VAR_INTLONG, rharray);
      int64 *lhsrce = lharray->arraystart.int64base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n] - rhsrce[n];
    } else if (lhitem == STACK_FLOATARRAY) {      /* <float array>-<uint8 array> */
      float64 *base = make_array(VAR_FLOAT, rharray);
      float64 *lhsrce = lharray->arraystart.floatbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n] - TOFLOAT(rhsrce[n]);
    }
  } else if (lhitem == STACK_FATEMP) {          /* <float array>-<uint8 array> */
    basicarray lharray = pop_arraytemp();
    float64 *lhsrce = lharray.arraystart.floatbase;
    check_arrays(&lharray, rharray);
    for (n = 0; n < rharray->arrsize; n++) lhsrce[n] -= TOFLOAT(rhsrce[n]);
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_ia64minus' deals with subtraction when the right-hand operand is
** a 64-bit integer array. All versions of the operator are dealt with
** by this function
*/
static void eval_i64aminus(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n;
  int64 *rhsrce;

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  rhsrce = rharray->arraystart.int64base;
  lhitem = GET_TOPITEM;
  if (TOPITEMISINT) {                   /* <any int>-<int64 array> */
    int64 lhint = pop_anyint();
    int64 *base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < rharray->arrsize; n++) base[n] = lhint - rhsrce[n];
  } else if (lhitem == STACK_FLOAT) {           /* <float>-<int64 array> */
    float64 *base = make_array(VAR_FLOAT, rharray);
    floatvalue = pop_float();
    for (n = 0; n < rharray->arrsize; n++) base[n] = floatvalue - TOFLOAT(rhsrce[n]);
  } else if (TOPITEMISNUMARRAY) {
    basicarray *lharray = pop_array();
    check_arrays(lharray, rharray);
    if (lhitem == STACK_INTARRAY) {        /* <int array>-<int64 array> */
      int64 *base = make_array(VAR_INTLONG, rharray);
      int32 *lhsrce = lharray->arraystart.intbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n] - rhsrce[n];
    } else if (lhitem == STACK_UINT8ARRAY) {      /* <uint8 array>-<int64 array> */
      int64 *base = make_array(VAR_INTLONG, rharray);
      uint8 *lhsrce = lharray->arraystart.uint8base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n] - rhsrce[n];
    } else if (lhitem == STACK_INT64ARRAY) {      /* <int array>-<int64 array> */
      int64 *base = make_array(VAR_INTLONG, rharray);
      int64 *lhsrce = lharray->arraystart.int64base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n] - rhsrce[n];
    } else if (lhitem == STACK_FLOATARRAY) {      /* <float array>-<int64 array> */
      float64 *base = make_array(VAR_FLOAT, rharray);
      float64 *lhsrce = lharray->arraystart.floatbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n] - TOFLOAT(rhsrce[n]);
    }
  } else if (lhitem == STACK_FATEMP) {          /* <float array>-<int64 array> */
    basicarray lharray = pop_arraytemp();
    float64 *lhsrce = lharray.arraystart.floatbase;
    check_arrays(&lharray, rharray);
    for (n = 0; n < rharray->arrsize; n++) lhsrce[n] -= TOFLOAT(rhsrce[n]);
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_faminus' deals with subtraction when the right-hand operand is
** a floating point array. All versions of the operator are dealt with
** by this function
*/
static void eval_faminus(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n;
  float64 *base, *rhsrce;

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  rhsrce = rharray->arraystart.floatbase;
  lhitem = GET_TOPITEM;
  if (TOPITEMISNUM) {   /* <int or float>-<float array> or <uint8>-<float array> */
    floatvalue = pop_anynumfp();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < rharray->arrsize; n++) base[n] = floatvalue - rhsrce[n];
  } else if (TOPITEMISNUMARRAY) {
    basicarray *lharray = pop_array();
    check_arrays(lharray, rharray);
    base = make_array(VAR_FLOAT, rharray);
    if (lhitem == STACK_INTARRAY) {                        /* <int array>+<float array> */
      int32 *lhsrce = lharray->arraystart.intbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = TOFLOAT(lhsrce[n]) - rhsrce[n];
    } else if (lhitem == STACK_UINT8ARRAY) {                      /* <uint8 array>+<float array> */
      uint8 *lhsrce = lharray->arraystart.uint8base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = TOFLOAT(lhsrce[n]) - rhsrce[n];
    } else if (lhitem == STACK_INT64ARRAY) {                      /* <int64 array>+<float array> */
      int64 *lhsrce = lharray->arraystart.int64base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = TOFLOAT(lhsrce[n]) - rhsrce[n];
    } else if (lhitem == STACK_FLOATARRAY) {                      /* <float array>-<float array> */
      float64 *lhsrce = lharray->arraystart.floatbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = lhsrce[n] - rhsrce[n];
    }
  } else if (lhitem == STACK_FATEMP) {                          /* <float array>-<float array> */
    basicarray lharray = pop_arraytemp();
    float64 *lhsrce = lharray.arraystart.floatbase;
    check_arrays(&lharray, rharray);
    for (n = 0; n < rharray->arrsize; n++) lhsrce[n] -= rhsrce[n];
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
  DEBUGFUNCMSGOUT;
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
  int64 rhint, lhint = 0, intres=0;
  float64 floatres = 0;

  DEBUGFUNCMSGIN;
  rhitem = GET_TOPITEM;
  rhint = pop_anyint();

  lhitem = GET_TOPITEM;
  if (TOPITEMISINT) {   /* Now look at left-hand operand */
    lhint = pop_anyint();
    intres=lhint*rhint;
    floatres=(TOFLOAT(lhint)*TOFLOAT(rhint));
    if (fabs(floatres) > (float80)MAXINT64VAL)
      push_float(floatres);
    else
      push_varyint(intres);
  } else if (lhitem == STACK_FLOAT)
    push_float(fmulwithtest(pop_float(), TOFLOAT(rhint)));
  else if (TOPITEMISNUMARRAY) {        /* <array>*<integer value> */
    basicarray *lharray;
    int32 n;
    lharray = pop_array();
    if (lhitem == STACK_INTARRAY) {                     /* <int array>*<intX> */
      int32 *srce;
      if (rhitem == STACK_INT64) {
        int64 *base = make_array(VAR_INTLONG, lharray);
        srce = lharray->arraystart.intbase;
        for (n = 0; n < lharray->arrsize; n++) base[n]=i64mulwithtest(srce[n], rhint);
      } else { /* STACK_INT and STACK_UINT8 */
        int32 *base = make_array(VAR_INTWORD, lharray);
        srce = lharray->arraystart.intbase;
        for (n = 0; n < lharray->arrsize; n++) base[n] = i32mulwithtest(srce[n], rhint);
      }
    } else if (lhitem == STACK_UINT8ARRAY) {                    /* <int array>*<intX> */
      uint8 *srce;
      if (rhitem == STACK_INT) {
        int32 *base = make_array(VAR_INTWORD, lharray);
        srce = lharray->arraystart.uint8base;
        for (n = 0; n < lharray->arrsize; n++) base[n] = i32mulwithtest(srce[n], rhint);
      } else if (rhitem == STACK_INT64) {
        int64 *base = make_array(VAR_INTLONG, lharray);
        srce = lharray->arraystart.uint8base;
        for (n = 0; n < lharray->arrsize; n++) base[n] = i64mulwithtest(srce[n], rhint);
      } else { /* STACK_UINT8 */
        uint8 *base = make_array(VAR_UINT8, lharray);
        srce = lharray->arraystart.uint8base;
        for (n = 0; n < lharray->arrsize; n++) base[n] = srce[n]*rhint;
      }
    } else if (lhitem == STACK_INT64ARRAY) {            /* <int64 array>*<intX> */
      int64 *srce = lharray->arraystart.int64base;
      int64 *base = make_array(VAR_INTLONG, lharray);
      for (n = 0; n < lharray->arrsize; n++) base[n] = i64mulwithtest(srce[n], rhint);
    } else {    /* <float array>*<integer> */
      float64 *srce = lharray->arraystart.floatbase;
      float64 *base = make_array(VAR_FLOAT, lharray);
      floatvalue = TOFLOAT(rhint);
      for (n = 0; n < lharray->arrsize; n++) base[n] = fmulwithtest(srce[n], floatvalue);
    }
  } else if (lhitem == STACK_FATEMP) {  /* <float array>*<integer value> */
    basicarray lharray = pop_arraytemp();
    float64 *base = lharray.arraystart.floatbase;
    int32 n;
    floatvalue = TOFLOAT(rhint);
    for (n = 0; n < lharray.arrsize; n++) base[n]=fmulwithtest(base[n], floatvalue);
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_fvmul' handles multiplication where the right-hand operand is
** a floating point value
*/
static void eval_fvmul(void) {
  stackitem lhitem;

  DEBUGFUNCMSGIN;
  floatvalue = pop_float();
  lhitem = GET_TOPITEM;
  if (TOPITEMISNUM)      /* Now branch according to type of left-hand operand */
    push_float(fmulwithtest(pop_anynumfp(), floatvalue));
  else if (TOPITEMISNUMARRAY) {        /* <array>*<float value> */
    basicarray *lharray = pop_array();
    float64 *base = make_array(VAR_FLOAT, lharray);
    int32 n;
    if (lhitem == STACK_INTARRAY) {
      int32 *srce = lharray->arraystart.intbase;
      for (n = 0; n < lharray->arrsize; n++) base[n] = fmulwithtest(TOFLOAT(srce[n]), floatvalue);
    } else if (lhitem == STACK_UINT8ARRAY) {
      uint8 *srce = lharray->arraystart.uint8base;
      for (n = 0; n < lharray->arrsize; n++) base[n] = fmulwithtest(TOFLOAT(srce[n]), floatvalue);
    } else if (lhitem == STACK_INT64ARRAY) {
      int64 *srce = lharray->arraystart.int64base;
      for (n = 0; n < lharray->arrsize; n++) base[n] = fmulwithtest(TOFLOAT(srce[n]), floatvalue);
    } else {
      float64 *srce = lharray->arraystart.floatbase;
      for (n = 0; n < lharray->arrsize; n++) base[n] = fmulwithtest(srce[n], floatvalue);
    }
  } else if (lhitem == STACK_FATEMP) {  /* <float array>*<float value> */
    basicarray lharray = pop_arraytemp();
    float64 *base = lharray.arraystart.floatbase;
    int32 n;
    for (n = 0; n < lharray.arrsize; n++) base[n]*=floatvalue;
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_iamul' handles multiplication where the right-hand operand is
** a 32-bit integer array
*/
static void eval_iamul(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n;
  int32 *rhsrce;

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  rhsrce = rharray->arraystart.intbase;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT || lhitem == STACK_UINT8) {   /* <int32/uint8 value>*<integer array> */
    int32 *base = make_array(VAR_INTWORD, rharray);
    int32 lhint = pop_anyint();
    for (n = 0; n < rharray->arrsize; n++) base[n] = i32mulwithtest(lhint, rhsrce[n]);
  } else if (lhitem == STACK_INT64) {                   /* <int64 value>*<integer array> */
    int64 *base = make_array(VAR_INTLONG, rharray);
    int64 lhint64=pop_int64();
    for (n = 0; n < rharray->arrsize; n++) base[n] = i64mulwithtest(lhint64, rhsrce[n]);
  } else if (lhitem == STACK_FLOAT) {                   /* <float>*<int array> */
    float64 *base = make_array(VAR_FLOAT, rharray);
    floatvalue = pop_float();
    for (n = 0; n < rharray->arrsize; n++) base[n] = fmulwithtest(floatvalue, TOFLOAT(rhsrce[n]));
  } else if (TOPITEMISNUMARRAY) {
    basicarray *lharray = pop_array();
    check_arrays(lharray, rharray);
    if (lhitem == STACK_INTARRAY) {                /* <int array>*<int array> */
      int32 *base = make_array(VAR_INTWORD, rharray);
      int32 *lhsrce = lharray->arraystart.intbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = i32mulwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_UINT8ARRAY) {              /* <uint8 array>*<int array> */
      int32 *base = make_array(VAR_INTWORD, rharray);
      uint8 *lhsrce = lharray->arraystart.uint8base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = i32mulwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_INT64ARRAY) {              /* <int64 array>*<int array> */
      int64 *base = make_array(VAR_INTLONG, rharray);
      int64 *lhsrce = lharray->arraystart.int64base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = i64mulwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_FLOATARRAY) {      /* <float array>*<int array> */
      float64 *base = make_array(VAR_FLOAT, rharray);
      float64 *lhsrce = lharray->arraystart.floatbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = fmulwithtest(lhsrce[n], TOFLOAT(rhsrce[n]));
    }
  } else if (lhitem == STACK_FATEMP) {          /* <float array>*<int array> */
    basicarray lharray = pop_arraytemp();
    float64 *lhsrce = lharray.arraystart.floatbase;
    check_arrays(&lharray, rharray);
    for (n = 0; n < rharray->arrsize; n++) lhsrce[n] = fmulwithtest(lhsrce[n], TOFLOAT(rhsrce[n]));
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_iu8amul' handles multiplication where the right-hand operand is
** an unsigned 8-bit integer array
*/
static void eval_iu8amul(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n;
  uint8 *rhsrce;

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  rhsrce = rharray->arraystart.uint8base;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT || lhitem == STACK_UINT8) {   /* <int32/uint8 value>*<uint8 array> */
    int32 *base = make_array(VAR_INTWORD, rharray);
    int32 lhint = pop_anyint();
    for (n = 0; n < rharray->arrsize; n++) base[n] = i32mulwithtest(lhint, rhsrce[n]);
  } else if (lhitem == STACK_INT64) {                   /* <int64 value>*<uint8 array> */
    int64 *base = make_array(VAR_INTLONG, rharray);
    int64 lhint64 = pop_int64();
    for (n = 0; n < rharray->arrsize; n++) base[n] = i64mulwithtest(lhint64, rhsrce[n]);
  } else if (lhitem == STACK_FLOAT) {                   /* <float>*<uint8 array> */
    float64 *base = make_array(VAR_FLOAT, rharray);
    floatvalue = pop_float();
    for (n = 0; n < rharray->arrsize; n++) base[n] = fmulwithtest(floatvalue, TOFLOAT(rhsrce[n]));
  } else if (TOPITEMISNUMARRAY) {
    basicarray *lharray = pop_array();
    check_arrays(lharray, rharray);
    if (lhitem == STACK_INTARRAY) {                /* <int array>*<uint8 array> */
      int32 *base = make_array(VAR_INTWORD, rharray);
      int32 *lhsrce = lharray->arraystart.intbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = i32mulwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_UINT8ARRAY) {              /* <uint8 array>*<uint8 array> */
      int32 *base = make_array(VAR_INTWORD, rharray);
      uint8 *lhsrce = lharray->arraystart.uint8base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = i32mulwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_INT64ARRAY) {              /* <int64 array>*<uint8 array> */
      int64 *base = make_array(VAR_INTLONG, rharray);
      int64 *lhsrce = lharray->arraystart.int64base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = i64mulwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_FLOATARRAY) {      /* <float array>*<uint8 array> */
      float64 *base = make_array(VAR_FLOAT, rharray);
      float64 *lhsrce = lharray->arraystart.floatbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = fmulwithtest(lhsrce[n], TOFLOAT(rhsrce[n]));
    }
  } else if (lhitem == STACK_FATEMP) {          /* <float array>*<uint8 array> */
    basicarray lharray = pop_arraytemp();
    float64 *lhsrce = lharray.arraystart.floatbase;
    check_arrays(&lharray, rharray);
    for (n = 0; n < rharray->arrsize; n++) lhsrce[n] = fmulwithtest(lhsrce[n], TOFLOAT(rhsrce[n]));
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_i64amul' handles multiplication where the right-hand operand is
** a 64-bit integer array
*/
static void eval_i64amul(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n;
  int64 *rhsrce;

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  rhsrce = rharray->arraystart.int64base;
  lhitem = GET_TOPITEM;
  if (TOPITEMISINT) {   /* <int32/uint8 value>*<int64 array> */
    int64 lhint64 = pop_anyint();
    int64 *base = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < rharray->arrsize; n++) base[n] = i64mulwithtest(lhint64, rhsrce[n]);
  } else if (lhitem == STACK_FLOAT) {   /* <float>*<int64 array> */
    floatvalue = pop_float();
    float64 *base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < rharray->arrsize; n++) base[n] = fmulwithtest(floatvalue, TOFLOAT(rhsrce[n]));
  } else if (TOPITEMISNUMARRAY) {
    basicarray *lharray = pop_array();
    check_arrays(lharray, rharray);
    if (lhitem == STACK_INTARRAY) {        /* <int array>*<int64 array> */
      int64 *lhsrce = lharray->arraystart.int64base;
      int64 *base = make_array(VAR_INTLONG, rharray);
      for (n = 0; n < rharray->arrsize; n++) base[n] = i64mulwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_UINT8ARRAY) {      /* <uint8 array>*<int64 array> */
      uint8 *lhsrce = lharray->arraystart.uint8base;
      int64 *base = make_array(VAR_INTLONG, rharray);
      for (n = 0; n < rharray->arrsize; n++) base[n] = i64mulwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_INT64ARRAY) {      /* <int64 array>*<int64 array> */
      int64 *lhsrce = lharray->arraystart.int64base;
      int64 *base = make_array(VAR_INTLONG, rharray);
      for (n = 0; n < rharray->arrsize; n++) base[n] = i64mulwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_FLOATARRAY) {      /* <float array>*<int64 array> */
      float64 *lhsrce = lharray->arraystart.floatbase;
      float64 *base = make_array(VAR_FLOAT, rharray);
      lhsrce = lharray->arraystart.floatbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = fmulwithtest(lhsrce[n], TOFLOAT(rhsrce[n]));
    }
  } else if (lhitem == STACK_FATEMP) {          /* <float array>*<int array> */
    basicarray lharray = pop_arraytemp();
    float64 *lhsrce = lharray.arraystart.floatbase;
    check_arrays(&lharray, rharray);
    for (n = 0; n < rharray->arrsize; n++) lhsrce[n] = fmulwithtest(lhsrce[n], TOFLOAT(rhsrce[n]));
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_famul' handles multiplication where the right-hand operand is
** a floating point array
*/
static void eval_famul(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n;
  float64 *base, *rhsrce;

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  rhsrce = rharray->arraystart.floatbase;
  lhitem = GET_TOPITEM;
  if (TOPITEMISNUM) {   /* <int or float>*<float array> */
    floatvalue = pop_anynumfp();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < rharray->arrsize; n++) base[n] = fmulwithtest(floatvalue, rhsrce[n]);
  } else if (TOPITEMISNUMARRAY) {
    basicarray *lharray = pop_array();
    check_arrays(lharray, rharray);
    base = make_array(VAR_FLOAT, rharray);
    if (lhitem == STACK_INTARRAY) {        /* <int array>*<float array> */
      int32 *lhsrce = lharray->arraystart.intbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = fmulwithtest(TOFLOAT(lhsrce[n]), rhsrce[n]);
    } else if (lhitem == STACK_UINT8ARRAY) {      /* <uint8 array>*<float array> */
      uint8 *lhsrce = lharray->arraystart.uint8base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = fmulwithtest(TOFLOAT(lhsrce[n]), rhsrce[n]);
    } else if (lhitem == STACK_FLOATARRAY) {      /* <float array>*<float array> */
      float64 *lhsrce = lharray->arraystart.floatbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = fmulwithtest(lhsrce[n], rhsrce[n]);
    }
  } else if (lhitem == STACK_FATEMP) {          /* <float array>*<float array> */
    float64 *lhsrce;
    basicarray lharray = pop_arraytemp();
    check_arrays(&lharray, rharray);
    lhsrce = lharray.arraystart.floatbase;
    for (n = 0; n < rharray->arrsize; n++) lhsrce[n] = fmulwithtest(lhsrce[n], rhsrce[n]);
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
  DEBUGFUNCMSGOUT;
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

  DEBUGFUNCMSGIN;
  if (lharray->dimcount > 2 || rharray->dimcount > 2) {
    DEBUGFUNCMSGOUT;
    error(ERR_MATARRAY);
  }
  lhrows = lharray->dimsize[ROW];               /* First dimemsion is the number of rows */
  lhcols = lharray->dimsize[COLUMN];            /* Second dimension is the number of columns */
  rhrows = rharray->dimsize[ROW];
  rhcols = rharray->dimsize[COLUMN];
  if (lharray->dimcount == 1) { /* First array is a row vector */
    if (lhrows != rhrows) {
      DEBUGFUNCMSGOUT;
      error(ERR_MATARRAY);
    }
    result->dimcount = 1;               /* Result is a row vector */
    if (rharray->dimcount == 1) { /* Second array is a column vector - Result is a 1 by 1 array */
      result->dimsize[ROW] = result->arrsize = 1;
    } else {      /* Second array is a matrix - Result is a N by 1 array */
      result->dimsize[ROW] = result->arrsize = rhcols;
    }
  }
  else if (rharray->dimcount == 1) {    /* Second array is a column vector (1st must be a matrix) */
    if (rhrows != lhcols) {
      DEBUGFUNCMSGOUT;
      error(ERR_MATARRAY);
    }
    result->dimcount = 1;               /* Result is a column vector the same size as the second array */
    result->dimsize[ROW] = result->arrsize = lhrows;
  }
  else {                /* Multiplying two two dimensional matrixes */
    if (lhcols != rhrows) {
      DEBUGFUNCMSGOUT;
      error(ERR_MATARRAY);
    }
    result->dimcount = 2;
    result->arrsize = lhrows * rhcols;
    result->dimsize[ROW] = lhrows;
    result->dimsize[COLUMN] = rhcols;
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_immul' is called to handle matrix multiplication when
** the right-hand array is a 32-bit integer array
*/
static void eval_immul(void) {
  int32 *base, *lhbase, *rhbase, resindex, row, col, sum, lhrowsize, rhrowsize;
  basicarray *lharray, *rharray, result;
  stackitem lhitem;

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  lhitem = GET_TOPITEM;         /* Get type of left-hand item */
  if (lhitem != STACK_INTARRAY) {    /* Want an integer array */
    DEBUGFUNCMSGOUT;
    error(ERR_INTARRAY);
  }
  lharray = pop_array();
  check_arraytype(&result, lharray, rharray);
  base = make_array(VAR_INTWORD, &result);
/* Find number of array elements to skip going row to row */
  lhrowsize = rhrowsize = 0;
  if (lharray->dimcount != 1) lhrowsize = lharray->dimsize[COLUMN];     /* Want no. of columns (elements in row) */
  if (rharray->dimcount != 1) rhrowsize = rharray->dimsize[COLUMN];
  lhbase = lharray->arraystart.intbase;
  rhbase = rharray->arraystart.intbase;
  if (lharray->dimcount == 1) { /* Result is a row vector */
    for (resindex = 0; resindex < result.dimsize[ROW]; resindex++) {
      sum = 0;
      for (col = 0; col < lharray->dimsize[ROW]; col++) {
        sum+=lhbase[col] * rhbase[col * rhrowsize + resindex];
      }
      base[resindex] = sum;
    }
  }
  else if (lharray->dimcount == 2 && rharray->dimcount == 1) {  /* Result is a column vector */
    for (resindex = 0; resindex < result.dimsize[ROW]; resindex++) {
      sum = 0;
      for (col = 0; col < rharray->dimsize[ROW]; col++) {
        sum+=lhbase[lhrowsize * resindex + col] * rhbase[col];
      }
      base[resindex] = sum;
    }
  }
  else {        /* Multiplying two two dimensional matrices */
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
  DEBUGFUNCMSGOUT;
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

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  lhitem = GET_TOPITEM;         /* Get type of left-hand item */
  if (lhitem != STACK_FLOATARRAY) {   /* Want a floating point array */
    DEBUGFUNCMSGOUT;
    error(ERR_FPARRAY);
  }
  lharray = pop_array();
  check_arraytype(&result, lharray, rharray);
  base = make_array(VAR_FLOAT, &result);
/* Find number of array elements to skip going row to row */
  lhrowsize = rhrowsize = 0;
  if (lharray->dimcount != 1) lhrowsize = lharray->dimsize[COLUMN];     /* Want no. of elements in row */
  if (rharray->dimcount != 1) rhrowsize = rharray->dimsize[COLUMN];
  lhbase = lharray->arraystart.floatbase;
  rhbase = rharray->arraystart.floatbase;
  if (lharray->dimcount == 1) { /* Result is a row vector */
    for (resindex = 0; resindex < result.dimsize[ROW]; resindex++) {
      sum = 0;
      for (col = 0; col < lharray->dimsize[ROW]; col++) {
        sum+= fmulwithtest(lhbase[col], rhbase[col * rhrowsize + resindex]);
      }
      base[resindex] = sum;
    }
  }
  else if (lharray->dimcount == 2 && rharray->dimcount == 1) {  /*  Result is a column vector */
    for (resindex = 0; resindex < result.dimsize[ROW]; resindex++) {
      sum = 0;
      for (col = 0; col < rharray->dimsize[ROW]; col++) {
        sum += fmulwithtest(lhbase[lhrowsize * resindex + col], rhbase[col]);
      }
      base[resindex] = sum;
    }
  }
  else {        /* Multiplying two two dimensional matrices */
    resindex = 0;
    for (row = 0; row < result.dimsize[ROW]; row++) {   /* Row in the result array */
      for (col = 0; col < result.dimsize[COLUMN]; col++) {      /* Column in the result array */
        int lhcol;
        sum = 0;
        for (lhcol = 0; lhcol < lharray->dimsize[COLUMN]; lhcol++) {
          sum+=fmulwithtest(lhbase[lhrowsize * row + lhcol], rhbase[rhrowsize * lhcol + col]);
        }
        base[resindex] = sum;
        resindex++;
      }
    }
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_ivdiv' handles floating point division where the right-hand operand
** is a 32-bit integer value
*/
static void eval_ivdiv(void) {
  stackitem lhitem;
  int64 rhint = pop_anyint();

  DEBUGFUNCMSGIN;
  lhitem = GET_TOPITEM;
  if (TOPITEMISNUM)
    push_float(fdivwithtest(pop_anynumfp(),TOFLOAT(rhint)));
  else if (TOPITEMISNUMARRAY) {        /* <array>/<integer value> */
    basicarray *lharray = pop_array();
    float64 *base = make_array(VAR_FLOAT, lharray);
    int32 n;
    floatvalue = TOFLOAT(rhint);
    if (lhitem == STACK_INTARRAY) {
      int32 *srce = lharray->arraystart.intbase;
      for (n = 0; n < lharray->arrsize; n++) base[n] = fdivwithtest(TOFLOAT(srce[n]), floatvalue);
    } else if (lhitem == STACK_UINT8ARRAY) {
      uint8 *srce = lharray->arraystart.uint8base;
      for (n = 0; n < lharray->arrsize; n++) base[n] = fdivwithtest(TOFLOAT(srce[n]), floatvalue);
    } else if (lhitem == STACK_INT64ARRAY) {
      int64 *srce = lharray->arraystart.int64base;
      for (n = 0; n < lharray->arrsize; n++) base[n] = fdivwithtest(TOFLOAT(srce[n]), floatvalue);
    } else {
      float64 *srce = lharray->arraystart.floatbase;
      for (n = 0; n < lharray->arrsize; n++) base[n] = fdivwithtest(srce[n], floatvalue);
    }
  } else if (lhitem == STACK_FATEMP) {  /* <float array>/<integer value> */
    basicarray lharray = pop_arraytemp();
    float64 *base = lharray.arraystart.floatbase;
    int32 n;
    floatvalue = TOFLOAT(rhint);
    for (n = 0; n < lharray.arrsize; n++) base[n] = fdivwithtest(base[n], floatvalue);
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_fvdiv' handles division where the right-hand operand is a
** floating point value
*/
static void eval_fvdiv(void) {
  stackitem lhitem;

  DEBUGFUNCMSGIN;
  floatvalue = pop_float();
  lhitem = GET_TOPITEM;
  if (TOPITEMISNUM)
    push_float(fdivwithtest(pop_anynumfp(), floatvalue));
  else if (lhitem == STACK_INTARRAY || lhitem == STACK_FLOATARRAY) {    /* <array>/<float value> */
    basicarray *lharray = pop_array();
    float64 *base = make_array(VAR_FLOAT, lharray);
    int32 n;
    if (lhitem == STACK_INTARRAY) {
      int32 *srce = lharray->arraystart.intbase;
      for (n = 0; n < lharray->arrsize; n++) base[n] = fdivwithtest(TOFLOAT(srce[n]), floatvalue);
    } else if (lhitem == STACK_UINT8ARRAY) {
      uint8 *srce = lharray->arraystart.uint8base;
      for (n = 0; n < lharray->arrsize; n++) base[n] = fdivwithtest(TOFLOAT(srce[n]), floatvalue);
    } else if (lhitem == STACK_INT64ARRAY) {
      int64 *srce = lharray->arraystart.int64base;
      for (n = 0; n < lharray->arrsize; n++) base[n] = fdivwithtest(TOFLOAT(srce[n]), floatvalue);
    } else {
      float64 *srce = lharray->arraystart.floatbase;
      for (n = 0; n < lharray->arrsize; n++) base[n] = fdivwithtest(srce[n], floatvalue);
    }
  } else if (lhitem == STACK_FATEMP) {                    /* <float array>/<float value> */
    basicarray lharray = pop_arraytemp();
    float64 *base = lharray.arraystart.floatbase;
    int32 n;
    for (n = 0; n < lharray.arrsize; n++) base[n] = fdivwithtest(base[n], floatvalue);
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_iadiv' handles floating point division where the right-hand operand
** is a 32-bit integer array
*/
static void eval_iadiv(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n;
  int32 *rhsrce;
  float64 *base;

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  rhsrce = rharray->arraystart.intbase;
  lhitem = GET_TOPITEM;
  if (TOPITEMISNUM) {                                     /* <any number>/<integer array> */
    float64 *base = make_array(VAR_FLOAT, rharray);
    floatvalue = pop_anynumfp();
    for (n = 0; n < rharray->arrsize; n++) base[n]=fdivwithtest(floatvalue, TOFLOAT(rhsrce[n]));
  }
  else
  if (TOPITEMISNUMARRAY) {
    basicarray *lharray = pop_array();
    check_arrays(lharray, rharray);
    base = make_array(VAR_FLOAT, rharray);
    if (lhitem == STACK_INTARRAY) {                       /* <int array>/<int array> */
      int32 *lhsrce = lharray->arraystart.intbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = fdivwithtest(TOFLOAT(lhsrce[n]), TOFLOAT(rhsrce[n]));
    } else if (lhitem == STACK_UINT8ARRAY) {              /* <uint8 array>/<int array> */
      uint8 *lhsrce = lharray->arraystart.uint8base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = fdivwithtest(TOFLOAT(lhsrce[n]), TOFLOAT(rhsrce[n]));
    } else if (lhitem == STACK_INT64ARRAY) {              /* <int64 array>/<int array> */
      int64 *lhsrce = lharray->arraystart.int64base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = fdivwithtest(TOFLOAT(lhsrce[n]), TOFLOAT(rhsrce[n]));
    } else if (lhitem == STACK_FLOATARRAY) {              /* <float array>/<int array> */
      float64 *lhsrce = lharray->arraystart.floatbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = fdivwithtest(lhsrce[n], TOFLOAT(rhsrce[n]));
    }
  } else if (lhitem == STACK_FATEMP) {                    /* <float array>/<int array> */
    basicarray lharray = pop_arraytemp();
    float64 *lhsrce = lharray.arraystart.floatbase;
    check_arrays(&lharray, rharray);
    for (n = 0; n < rharray->arrsize; n++) lhsrce[n] = fdivwithtest(lhsrce[n], TOFLOAT(rhsrce[n]));
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_iu8adiv' handles floating point division where the right-hand operand
** is an unsigned 8-bit integer array
*/
static void eval_iu8adiv(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n;
  uint8 *rhsrce;
  float64 *base;

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  rhsrce = rharray->arraystart.uint8base;
  lhitem = GET_TOPITEM;
  if (TOPITEMISNUM) {                                     /* <any number>/<integer array> */
    float64 *base = make_array(VAR_FLOAT, rharray);
    floatvalue = pop_anynumfp();
    for (n = 0; n < rharray->arrsize; n++) base[n] = fdivwithtest(floatvalue, TOFLOAT(rhsrce[n]));
  } else if (TOPITEMISNUMARRAY) {
    basicarray *lharray = pop_array();
    check_arrays(lharray, rharray);
    base = make_array(VAR_FLOAT, rharray);
    if (lhitem == STACK_INTARRAY) {                       /* <int array>/<int array> */
      int32 *lhsrce = lharray->arraystart.intbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = fdivwithtest(TOFLOAT(lhsrce[n]), TOFLOAT(rhsrce[n]));
    } else if (lhitem == STACK_UINT8ARRAY) {              /* <uint8 array>/<int array> */
      uint8 *lhsrce = lharray->arraystart.uint8base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = fdivwithtest(TOFLOAT(lhsrce[n]), TOFLOAT(rhsrce[n]));
    } else if (lhitem == STACK_INT64ARRAY) {              /* <int64 array>/<int array> */
      int64 *lhsrce = lharray->arraystart.int64base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = fdivwithtest(TOFLOAT(lhsrce[n]), TOFLOAT(rhsrce[n]));
    } else if (lhitem == STACK_FLOATARRAY) {              /* <float array>/<int array> */
      float64 *lhsrce = lharray->arraystart.floatbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = fdivwithtest(lhsrce[n], TOFLOAT(rhsrce[n]));
    }
  } else if (lhitem == STACK_FATEMP) {                    /* <float array>/<int array> */
    basicarray lharray = pop_arraytemp();
    float64 *lhsrce = lharray.arraystart.floatbase;
    check_arrays(&lharray, rharray);
    for (n = 0; n < rharray->arrsize; n++) lhsrce[n] = fdivwithtest(lhsrce[n], TOFLOAT(rhsrce[n]));
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_i64adiv' handles floating point division where the right-hand operand
** is a 64-bit integer array
*/
static void eval_i64adiv(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n;
  int64 *rhsrce;
  float64 *base;

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  rhsrce = rharray->arraystart.int64base;
  lhitem = GET_TOPITEM;
  if (TOPITEMISNUM) {                                           /* <any number>/<int64 array> */
    float64 *base = make_array(VAR_FLOAT, rharray);
    floatvalue = pop_anynumfp();
    for (n = 0; n < rharray->arrsize; n++) base[n] = fdivwithtest(floatvalue, TOFLOAT(rhsrce[n]));
  } else if (TOPITEMISNUMARRAY) {
    basicarray *lharray = pop_array();
    check_arrays(lharray, rharray);
    base = make_array(VAR_FLOAT, rharray);
    if (lhitem == STACK_INTARRAY) {                        /* <int array>/<int64 array> */
      int32 *lhsrce = lharray->arraystart.intbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = fdivwithtest(TOFLOAT(lhsrce[n]), TOFLOAT(rhsrce[n]));
    } else if (lhitem == STACK_UINT8ARRAY) {                      /* <uint8 array>/<int64 array> */
      uint8 *lhsrce = lharray->arraystart.uint8base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = fdivwithtest(TOFLOAT(lhsrce[n]), TOFLOAT(rhsrce[n]));
    } else if (lhitem == STACK_INT64ARRAY) {                      /* <int64 array>/<int64 array> */
      int64 *lhsrce = lharray->arraystart.int64base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = fdivwithtest(TOFLOAT(lhsrce[n]), TOFLOAT(rhsrce[n]));
    } else if (lhitem == STACK_FLOATARRAY) {                      /* <float array>/<int64 array> */
      float64 *lhsrce = lharray->arraystart.floatbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = fdivwithtest(lhsrce[n], TOFLOAT(rhsrce[n]));
    }
  } else if (lhitem == STACK_FATEMP) {                          /* <float array>/<int64 array> */
    basicarray lharray = pop_arraytemp();
    float64 *lhsrce = lharray.arraystart.floatbase;
    check_arrays(&lharray, rharray);
    for (n = 0; n < rharray->arrsize; n++) lhsrce[n] = fdivwithtest(lhsrce[n], TOFLOAT(rhsrce[n]));
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
  DEBUGFUNCMSGOUT;
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

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.floatbase;
  lhitem = GET_TOPITEM;
  if (TOPITEMISNUM) {                                           /* <int32/float value>/<float array> */
    floatvalue = pop_anynumfp();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) base[n] = fdivwithtest(floatvalue, rhsrce[n]);
  } else if (TOPITEMISNUMARRAY) {
    basicarray *lharray = pop_array();
    check_arrays(lharray, rharray);
    base = make_array(VAR_FLOAT, rharray);
    if (lhitem == STACK_INTARRAY) {                        /* <int array>/<float array> */
      int32 *lhsrce = lharray->arraystart.intbase;
      for (n = 0; n < count; n++) base[n] = fdivwithtest(TOFLOAT(lhsrce[n]), rhsrce[n]);
    } else if (lhitem == STACK_UINT8ARRAY) {                      /* <int array>/<float array> */
      uint8 *lhsrce = lharray->arraystart.uint8base;
      for (n = 0; n < count; n++) base[n] = fdivwithtest(TOFLOAT(lhsrce[n]), rhsrce[n]);
    } else if (lhitem == STACK_INT64ARRAY) {                      /* <int64 array>/<float array> */
      int64 *lhsrce = lharray->arraystart.int64base;
      for (n = 0; n < count; n++) base[n] = fdivwithtest(TOFLOAT(lhsrce[n]), rhsrce[n]);
    } else if (lhitem == STACK_FLOATARRAY) {                      /* <float array>/<float array> */
      float64 *lhsrce = lharray->arraystart.floatbase;
      for (n = 0; n < count; n++) base[n] = fdivwithtest(lhsrce[n], rhsrce[n]);
    }
  } else if (lhitem == STACK_FATEMP) {                          /* <float array>/<float array> */
    basicarray lharray = pop_arraytemp();
    float64 *lhsrce = lharray.arraystart.floatbase;
    check_arrays(&lharray, rharray);
    for (n = 0; n < count; n++) lhsrce[n] = fdivwithtest(lhsrce[n], rhsrce[n]);
    push_arraytemp(&lharray, VAR_FLOAT);
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_ivintdiv' handles the integer division operator when the
** right-hand operand is any integer or floating point value
*/
static void eval_ivintdiv(void) {
  stackitem lhitem;
  int64 rhint = 0;

  DEBUGFUNCMSGIN;
  rhint = pop_anynum64();
  if (rhint == 0) {
    DEBUGFUNCMSGOUT;
    error(ERR_DIVZERO);
  }
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)              /* Branch according to type of left-hand operand */
    INTDIV_INT(rhint);
  else if (lhitem == STACK_UINT8)       /* Branch according to type of left-hand operand */
    INTDIV_UINT8(rhint);
  else if (lhitem == STACK_INT64)       /* Branch according to type of left-hand operand */
    INTDIV_INT64(rhint);
  else if (lhitem == STACK_FLOAT)
    push_int64(TOINT64(pop_float())/rhint);
  else if (TOPITEMISNUMARRAY) {        /* <array> DIV <integer value> */
    basicarray *lharray = pop_array();
    int32 n;
    if (lhitem == STACK_INTARRAY) {             /* <integer array> DIV <integer value> */
      int32 *srce = lharray->arraystart.intbase;
      int32 *base = make_array(VAR_INTWORD, lharray);
      for (n = 0; n < lharray->arrsize; n++) base[n] = srce[n] / rhint;
    } else if (lhitem == STACK_UINT8ARRAY) {    /* <integer array> DIV <integer value> */
      uint8 *srce = lharray->arraystart.uint8base;
      uint8 *base = make_array(VAR_UINT8, lharray);
      for (n = 0; n < lharray->arrsize; n++) base[n] = srce[n] / rhint;
    } else if (lhitem == STACK_INT64ARRAY) {    /* <integer array> DIV <integer value> */
      int64 *srce = lharray->arraystart.int64base;
      int64 *base = make_array(VAR_INTLONG, lharray);
      for (n = 0; n < lharray->arrsize; n++) base[n] = srce[n] / rhint;
    } else {    /* <float array> DIV <integer value> */
      float64 *srce = lharray->arraystart.floatbase;
      int64 *base = make_array(VAR_INTLONG, lharray);
      for (n = 0; n < lharray->arrsize; n++) base[n] = TOINT64(srce[n]) / rhint;
    }
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_iaintdiv' handles the integer division operator when the
** right-hand operand is a 32-bit integer array
*/
static void eval_iaintdiv(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n;
  int32 *base, *rhsrce;

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  rhsrce = rharray->arraystart.intbase;
  lhitem = GET_TOPITEM;
  if (TOPITEMISNUM) {   /* <value> DIV <int32 array> */
    int64 lhint64, *base64;
    lhint64 = lhitem == STACK_FLOAT ? TOINT64(pop_float()) : pop_anyint();
    base64 = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < rharray->arrsize; n++) base64[n] = i64divwithtest(lhint64, rhsrce[n]);
  } else if (TOPITEMISNUMARRAY) {
    basicarray *lharray = pop_array();
    check_arrays(lharray, rharray);
    if (lhitem == STACK_INTARRAY) {                        /* <int32 array> DIV <int32 array> */
      int32 *lhsrce = lharray->arraystart.intbase;
      base = make_array(VAR_INTWORD, rharray);
      for (n = 0; n < rharray->arrsize; n++) base[n] = i32divwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_UINT8ARRAY) {                      /* <uint8 array> DIV <int32 array> */
      uint8 *lhsrce = lharray->arraystart.uint8base;
      base = make_array(VAR_INTWORD, rharray);
      for (n = 0; n < rharray->arrsize; n++) base[n] = i32divwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_INT64ARRAY) {                      /* <int64 array> DIV <int32 array> */
      int64 *lhsrce = lharray->arraystart.int64base;
      int64 *base64 = make_array(VAR_INTLONG, rharray);
      for (n = 0; n < rharray->arrsize; n++) base64[n] = i64divwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_FLOATARRAY) {                      /* <float array> DIV <int32 array> */
      float64 *lhsrce = lharray->arraystart.floatbase;
      int64 *base64 = make_array(VAR_INTLONG, rharray);
      for (n = 0; n < rharray->arrsize; n++) base64[n] = i64divwithtest(TOINT64(lhsrce[n]), rhsrce[n]);
    }
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_iu8aintdiv' handles the integer division operator when the
** right-hand operand is an unsigned 8-bit integer array
*/
static void eval_iu8aintdiv(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n;
  int64 *base64;
  int32 *base32;
  uint8 *base8;
  uint8 *rhsrce;

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  rhsrce = rharray->arraystart.uint8base;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT) {                                    /* <int32> DIV <uint8 array> */
    int32 lhint = pop_int();
    base32 = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < rharray->arrsize; n++) base32[n] = i32divwithtest(lhint, rhsrce[n]);
  } else  if (lhitem == STACK_UINT8) {                          /* <uint8> DIV <uint8 array> */
    uint8 lhint = pop_uint8();
    base8 = make_array(VAR_UINT8, rharray);
    for (n = 0; n < rharray->arrsize; n++) base8[n] = i32divwithtest(lhint, rhsrce[n]);
  } else if (lhitem == STACK_INT64 || lhitem == STACK_FLOAT) {  /* <int64/float> DIV <uint8 array> */
    int64 lhint64;
    lhint64 = lhitem == STACK_INT64 ? pop_int64() : TOINT64(pop_float());
    base64 = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < rharray->arrsize; n++) base64[n] = i64divwithtest(lhint64, rhsrce[n]);
  } else if (TOPITEMISNUMARRAY) {
      basicarray *lharray = pop_array();
      check_arrays(lharray, rharray);
    if (lhitem == STACK_INTARRAY) {                        /* <int32 array> DIV <uint8 array> */
      int32 *lhsrce = lharray->arraystart.intbase;
      base32 = make_array(VAR_INTWORD, rharray);
      for (n = 0; n < rharray->arrsize; n++) base32[n] = i32divwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_UINT8ARRAY) {                      /* <uint8 array> DIV <uint8 array> */
      uint8 *lhsrce = lharray->arraystart.uint8base;
      base8 = make_array(VAR_UINT8, rharray);
      for (n = 0; n < rharray->arrsize; n++) base8[n] = i32divwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_INT64ARRAY) {                      /* <int64 array> DIV <uint8 array> */
      int64 *lhsrce = lharray->arraystart.int64base;
      base64 = make_array(VAR_INTLONG, rharray);
      for (n = 0; n < rharray->arrsize; n++) base64[n] = i64divwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_FLOATARRAY) {                      /* <float array> DIV <uint8 array> */
      float64 *lhsrce = lharray->arraystart.floatbase;
      int64 *base64 = make_array(VAR_INTLONG, rharray);
      for (n = 0; n < rharray->arrsize; n++) base64[n] = i64divwithtest(TOINT64(lhsrce[n]), rhsrce[n]);
    }
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_i64aintdiv' handles the integer division operator when the
** right-hand operand is a 64-bit integer array
*/
static void eval_i64aintdiv(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n;
  int64 *base64, *rhsrce;
  int32 *base32;
  uint8 *base8;

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  rhsrce = rharray->arraystart.int64base;
  lhitem = GET_TOPITEM;
  if ((lhitem == STACK_INT64) || (lhitem == STACK_FLOAT)) {                     /* <value> DIV <int64 array> */
    int64 lhint64 = pop_anynum64();
    base64 = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < rharray->arrsize; n++) base64[n] = i64divwithtest(lhint64, rhsrce[n]);
  } else if (lhitem == STACK_INT) {                     /* <value> DIV <int64 array> */
    int32 lhint = pop_anyint();
    base32 = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < rharray->arrsize; n++) base32[n] = i64divwithtest(lhint, rhsrce[n]);
  } else if (lhitem == STACK_UINT8) {                   /* <value> DIV <int64 array> */
    int32 lhint = pop_anyint();
    base8 = make_array(VAR_UINT8, rharray);
    for (n = 0; n < rharray->arrsize; n++) base8[n] = i64divwithtest(lhint, rhsrce[n]);
  } else if (TOPITEMISNUMARRAY) {
    basicarray *lharray = pop_array();
    check_arrays(lharray, rharray);
    if (lhitem == STACK_INTARRAY) {                        /* <int32 array> DIV <int64 array> */
      int32 *lhsrce = lharray->arraystart.intbase;
      base32 = make_array(VAR_INTWORD, rharray);
      for (n = 0; n < rharray->arrsize; n++) base32[n] = i64divwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_UINT8ARRAY) {                      /* <uint8 array> DIV <int64 array> */
      uint8 *lhsrce = lharray->arraystart.uint8base;
      base8 = make_array(VAR_UINT8, rharray);
      for (n = 0; n < rharray->arrsize; n++) base8[n] = i64divwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_INT64ARRAY) {                      /* <int64 array> DIV <int64 array> */
      int64 *lhsrce = lharray->arraystart.int64base;
      base64 = make_array(VAR_INTLONG, rharray);
      for (n = 0; n < rharray->arrsize; n++) base64[n] = i64divwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_FLOATARRAY) {                      /* <float array> DIV <int64 array> */
      float64 *lhsrce = lharray->arraystart.floatbase;
      base64 = make_array(VAR_INTLONG, rharray);
      for (n = 0; n < rharray->arrsize; n++) base64[n] = i64divwithtest(lhsrce[n], rhsrce[n]);
    }
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_faintdiv' handles the integer division operator when the
** right-hand operand is a floating point array
*/
static void eval_faintdiv(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 *base32, n;
  int64 *base64;
  uint8 *base8;
  float64 *rhsrce;

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  rhsrce = rharray->arraystart.floatbase;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT) {            /* <int32> DIV <float array> */
    int32 lhint = pop_int();
    base32 = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < rharray->arrsize; n++) base32[n] = i64divwithtest(lhint, TOINT64(rhsrce[n]));
  } else if (lhitem == STACK_UINT8) {           /* <uint8> DIV <float array> */
    int32 lhint = pop_uint8();
    base8 = make_array(VAR_UINT8, rharray);
    for (n = 0; n < rharray->arrsize; n++) base8[n] = i64divwithtest(lhint, TOINT64(rhsrce[n]));
  } else if (lhitem == STACK_INT64 || lhitem == STACK_FLOAT) {  /* <int64/float> DIV <float array> */
    int64 lhint = lhitem == STACK_INT64 ? pop_int64() : TOINT64(pop_float());
    base64 = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < rharray->arrsize; n++) base64[n] = i64divwithtest(lhint, TOINT64(rhsrce[n]));
  } else if (TOPITEMISNUMARRAY) {
    basicarray *lharray = pop_array();
    check_arrays(lharray, rharray);
    if (lhitem == STACK_INTARRAY) {                        /* <int array> DIV <float array> */
      int32 *lhsrce = lharray->arraystart.intbase;
      base32 = make_array(VAR_INTWORD, rharray);
      for (n = 0; n < rharray->arrsize; n++) base32[n] = i64divwithtest(lhsrce[n], TOINT64(rhsrce[n]));
    } else if (lhitem == STACK_UINT8ARRAY) {                      /* <uint8 array> DIV <float array> */
      uint8 *lhsrce = lharray->arraystart.uint8base;
      base8 = make_array(VAR_UINT8, rharray);
      for (n = 0; n < rharray->arrsize; n++) base8[n] = i64divwithtest(lhsrce[n], TOINT64(rhsrce[n]));
    } else if (lhitem == STACK_INT64ARRAY) {                      /* <int64 array> DIV <float array> */
      int64 *lhsrce = lharray->arraystart.int64base;
      base64 = make_array(VAR_INTLONG, rharray);
      for (n = 0; n < rharray->arrsize; n++) base64[n] = i64divwithtest(lhsrce[n], TOINT64(rhsrce[n]));
    } else if (lhitem == STACK_FLOATARRAY) {                      /* <float array> DIV <float array> */
      float64 *lhsrce = lharray->arraystart.floatbase;
      base64 = make_array(VAR_INTLONG, rharray);
      for (n = 0; n < rharray->arrsize; n++) base64[n] = i64divwithtest(TOINT64(lhsrce[n]), TOINT64(rhsrce[n]));
    }
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_ivmod' carries out the integer remainder operator when the right-hand
** operand is any integer or floating point value
*/
static void eval_ivmod(void) {
  stackitem lhitem;
  int64 rhint = 0;

  DEBUGFUNCMSGIN;
  rhint = pop_anynum64();
  if (rhint == 0) {
    DEBUGFUNCMSGOUT;
    error(ERR_DIVZERO);
  }
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)                  /* Branch according to type of left-hand operand */
    INTMOD_INT(rhint);
  else if (lhitem == STACK_UINT8)           /* Branch according to type of left-hand operand */
    INTMOD_UINT8(rhint);
  else if (lhitem == STACK_INT64)           /* Branch according to type of left-hand operand */
    INTMOD_INT64(rhint);
  else if (lhitem == STACK_FLOAT)
    push_int64(TOINT64(pop_float()) % rhint);
  else if (TOPITEMISNUMARRAY) {        /* <array> MOD <integer value> */
    basicarray *lharray = pop_array();
    int32 n;
    if (lhitem == STACK_INTARRAY) {                             /* <int32 array> MOD <integer value> */
      int32 *srce = lharray->arraystart.intbase;
      int32 *base = make_array(VAR_INTWORD, lharray);
      for (n = 0; n < lharray->arrsize; n++) base[n] = srce[n] % rhint;
    } else if (lhitem == STACK_UINT8ARRAY) {                    /* <uint8 array> MOD <integer value> */
      uint8 *srce = lharray->arraystart.uint8base;
      uint8 *base = make_array(VAR_UINT8, lharray);
      for (n = 0; n < lharray->arrsize; n++) base[n] = srce[n] % rhint;
    } else if (lhitem == STACK_INT64ARRAY) {                    /* <int64 array> MOD <integer value> */
      int64 *srce = lharray->arraystart.int64base;
      int64 *base = make_array(VAR_INTLONG, lharray);
      for (n = 0; n < lharray->arrsize; n++) base[n] = srce[n] % rhint;
    } else {                                                    /* <float array> MOD <integer value> */
      float64 *srce = lharray->arraystart.floatbase;
      int64 *base = make_array(VAR_INTLONG, lharray);
      for (n = 0; n < lharray->arrsize; n++) base[n] = TOINT64(srce[n]) % rhint;
    }
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_iamod' carries out the integer remainder operator when the right-hand
** operand is a 32-bit integer array.
*/
static void eval_iamod(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n;
  int32 *base, *rhsrce;

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  rhsrce = rharray->arraystart.intbase;
  lhitem = GET_TOPITEM;
  if (TOPITEMISNUM) {     /* <value> MOD <integer array> */
    int32 lhint = pop_anynum32();
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < rharray->arrsize; n++) base[n] = i32modwithtest(lhint, rhsrce[n]);
  } else if (TOPITEMISNUMARRAY) {
    basicarray *lharray = pop_array();
    check_arrays(lharray, rharray);
    base = make_array(VAR_INTWORD, rharray);
    if (lhitem == STACK_INTARRAY) {                       /* <int32 array> MOD <integer array> */
      int32 *lhsrce = lharray->arraystart.intbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = i32modwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_UINT8ARRAY) {              /* <uint8 array> MOD <integer array> */
      uint8 *lhsrce = lharray->arraystart.uint8base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = i32modwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_INT64ARRAY) {              /* <int64 array> MOD <integer array> */
      int64 *lhsrce = lharray->arraystart.int64base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = i64modwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_FLOATARRAY) {              /* <float array> MOD <integer array> */
      float64 *lhsrce = lharray->arraystart.floatbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = i64modwithtest(TOINT64(lhsrce[n]), rhsrce[n]);
    }
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_iu8amod' carries out the integer remainder operator when the right-hand
** operand is an unsigned 8-bit integer array.
** This always returns a 32-bit integer array, since MOD results may be negative.
*/
static void eval_iu8amod(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n;
  uint8 *rhsrce;
  int32 *base;

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  rhsrce = rharray->arraystart.uint8base;
  lhitem = GET_TOPITEM;
  if (TOPITEMISNUM) {            /* <value> MOD <uint8 array> */
    int64 lhint = pop_anynum64();
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < rharray->arrsize; n++) base[n] = i64modwithtest(lhint, rhsrce[n]);
  } else if (TOPITEMISNUMARRAY) {
    basicarray *lharray = pop_array();
    check_arrays(lharray, rharray);
    base = make_array(VAR_INTWORD, rharray);
    if (lhitem == STACK_INTARRAY) {                        /* <int32 array> MOD <uint8 array> */
      int32 *lhsrce = lharray->arraystart.intbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = i64modwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_UINT8ARRAY) {                      /* <uint8 array> MOD <uint8 array> */
      uint8 *lhsrce = lharray->arraystart.uint8base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = i64modwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_INT64ARRAY) {                      /* <int64 array> MOD <uint8 array> */
      int64 *lhsrce = lharray->arraystart.int64base;
      for (n = 0; n < rharray->arrsize; n++) base[n] = i64modwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_FLOATARRAY) {                      /* <float array> MOD <uint8 array> */
      float64 *lhsrce = lharray->arraystart.floatbase;
      for (n = 0; n < rharray->arrsize; n++) base[n] = i64modwithtest(TOINT64(lhsrce[n]), rhsrce[n]);
    }
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_i64amod' carries out the integer remainder operator when the right-hand
** operand is a 64-bit integer array
*/
static void eval_i64amod(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n;
  uint8 *base8;
  int32 *base32;
  int64 *base64, *rhsrce;

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  rhsrce = rharray->arraystart.int64base;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT) {            /* <value> MOD <int64 array> */
    int32 lhint = pop_int();
    base32 = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < rharray->arrsize; n++) base32[n] = i64modwithtest(lhint, rhsrce[n]);
  } else if (lhitem == STACK_UINT8) {           /* <value> MOD <int64 array> */
    int32 lhint = pop_uint8();
    base8 = make_array(VAR_UINT8, rharray);
    for (n = 0; n < rharray->arrsize; n++) base8[n] = i64modwithtest(lhint, rhsrce[n]);
  } else if (lhitem == STACK_INT64 || lhitem == STACK_FLOAT) {  /* <value> MOD <int64 array> */
    int64 lhint = pop_anynum64();
    base64 = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < rharray->arrsize; n++) base64[n] = i64modwithtest(lhint, rhsrce[n]);
  } else if (TOPITEMISNUMARRAY) {
    basicarray *lharray = pop_array();
    check_arrays(lharray, rharray);
    if (lhitem == STACK_INTARRAY) {                        /* <int32 array> MOD <int64 array> */
      int32 *lhsrce;
      lhsrce = lharray->arraystart.intbase;
      base32 = make_array(VAR_INTWORD, rharray);
      for (n = 0; n < rharray->arrsize; n++) base32[n] = i64modwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_UINT8ARRAY) {                      /* <uint8 array> MOD <int64 array> */
      uint8 *lhsrce;
      lhsrce = lharray->arraystart.uint8base;
      base8 = make_array(VAR_UINT8, rharray);
      for (n = 0; n < rharray->arrsize; n++) base8[n] = i64modwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_INT64ARRAY) {                      /* <int64 array> MOD <int64 array> */
      int64 *lhsrce;
      lhsrce = lharray->arraystart.int64base;
      base64 = make_array(VAR_INTLONG, rharray);
      for (n = 0; n < rharray->arrsize; n++) base64[n] = i64modwithtest(lhsrce[n], rhsrce[n]);
    } else if (lhitem == STACK_FLOATARRAY) {                      /* <float array> MOD <int64 array> */
      float64 *lhsrce;
      base64 = make_array(VAR_INTLONG, rharray);
      lhsrce = lharray->arraystart.floatbase;
      for (n = 0; n < rharray->arrsize; n++) base64[n] = i64modwithtest(TOINT64(lhsrce[n]), rhsrce[n]);
    }
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_famod' carries out the integer remainder operator when the right-hand
** operand is a floating point array
*/
static void eval_famod(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 *base32, n;
  int64 *base64;
  uint8 *base8;
  float64 *rhsrce;

  DEBUGFUNCMSGIN;
  rharray = pop_array();
  rhsrce = rharray->arraystart.floatbase;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT) {    /* <value> MOD <float array> */
    int32 lhint = pop_int();
    base32 = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < rharray->arrsize; n++) base32[n] = i64modwithtest(lhint, TOINT64(rhsrce[n]));
  } else if (lhitem == STACK_UINT8) {   /* <value> MOD <float array> */
    int32 lhint = pop_uint8();
    base8 = make_array(VAR_UINT8, rharray);
    for (n = 0; n < rharray->arrsize; n++) base8[n] = i64modwithtest(lhint, TOINT64(rhsrce[n]));
  } else if (lhitem == STACK_INT64 || lhitem == STACK_FLOAT) {  /* <value> MOD <float array> */
    int64 lhint = pop_anynum64();
    base64 = make_array(VAR_INTLONG, rharray);
    for (n = 0; n < rharray->arrsize; n++) base64[n] = i64modwithtest(lhint, TOINT64(rhsrce[n]));
  } else if (TOPITEMISNUMARRAY) {
    basicarray *lharray = pop_array();
    check_arrays(lharray, rharray);
    if (lhitem == STACK_INTARRAY) {        /* <int array> MOD <float array> */
      int32 *lhsrce = lharray->arraystart.intbase;
      base32 = make_array(VAR_INTWORD, rharray);
      for (n = 0; n < rharray->arrsize; n++) base32[n] = i64modwithtest(lhsrce[n], TOINT64(rhsrce[n]));
    } else if (lhitem == STACK_UINT8ARRAY) {      /* <int array> MOD <float array> */
      uint8 *lhsrce = lharray->arraystart.uint8base;
      base8 = make_array(VAR_UINT8, rharray);
      for (n = 0; n < rharray->arrsize; n++) base8[n] = i64modwithtest(lhsrce[n], TOINT64(rhsrce[n]));
    } else if (lhitem == STACK_INT64ARRAY) {      /* <int64 array> MOD <float array> */
      int64 *lhsrce = lharray->arraystart.int64base;
      base64 = make_array(VAR_INTLONG, rharray);
      for (n = 0; n < rharray->arrsize; n++) base64[n] = i64modwithtest(lhsrce[n], TOINT64(rhsrce[n]));
    } else if (lhitem == STACK_FLOATARRAY) {      /* <float array> MOD <float array> */
      float64 *lhsrce = lharray->arraystart.floatbase;
      base64 = make_array(VAR_INTLONG, rharray);
      for (n = 0; n < rharray->arrsize; n++) base64[n] = i64modwithtest(TOINT64(lhsrce[n]), TOINT64(rhsrce[n]));
    }
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/* Replicate the error conditions found in RISC OS BASIC VI
 * (FPA build on RISC OS 3.71).
 * It is slightly more permissive than BASIC versions I to V.
 */
static float80 mpow(float64 lh, float64 rh) {
  float80 result;

  DEBUGFUNCMSGIN;
  set_fpu();
  result=powl((float80)lh,(float80)rh);
  if (isnan(result) || isinf(result)) {
    DEBUGFUNCMSGOUT;
    error(ERR_ARITHMETIC);
  }
  DEBUGFUNCMSGOUT;
  return result;
}

/* 64-bit integer power function */
static int64 ipow(int64 base, int64 exp) {
  int64 result = 1;

  DEBUGFUNCMSGIN;
  if (exp < 0) return 0; /* Should never be tripped, but remove risk of a near-infinite loop */
  for (;;) {
    if (exp & 1) result *=base;
    exp >>=1;
    if (!exp) break;
    base *= base;
  }
  DEBUGFUNCMSGOUT;
  return result;
}

/*
** 'eval_vpow' deals with the 'raise' operator when the right-hand operand is
** a 32-bit or 64-bit integer, or a floating point value
*/
static void eval_vpow(void) {
  int lhint, rhint;
  int64 resint;
  float80 lh, rh, result;
  float64 res64;

  DEBUGFUNCMSGIN;
  rhint = TOPITEMISINT;
  rh = pop_anynumfp();
  if (rh<0) rhint=FALSE; /* Don't use integer routine if exponent is negative */
  lhint = TOPITEMISINT;
  lh = pop_anynumfp();
  result = mpow(lh, rh);
  resint = ipow((int64)lh, (int64)rh);
  if ((result <= MAXINT64FLT) && (result >= MININT64FLT) && lhint && rhint && (sgni(resint) == sgnf(result))) {
    push_int64(resint);
  } else {
    if (result == (int64)result) {
      /* Integer result by happenstance, return as a 64-bit int so as not to lose precision */
      push_int64((int64)result);
    } else {
      res64=result;
      if (isnan(res64) || isinf(res64)) {
        DEBUGFUNCMSGOUT;
        error(ERR_ARITHMETIC);
      }
      push_float(res64);
    }
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_vlsl' deals with the logical left shift operator when the right-hand
** operand is a numeric value.
*/
static void eval_vlsl(void) {
  int32 rhint;
  int64 lhint64 = 0;

  DEBUGFUNCMSGIN;
  rhint = pop_anynum32() % 256;
  while (rhint < 0) rhint += 256;

  if (TOPITEMISNUM) {
    lhint64 = pop_anynum64();
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
  DEBUGFUNCMSGOUT;
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

  DEBUGFUNCMSGIN;
  rhuint = pop_anynum32() % 256;

  if (TOPITEMISNUM) {
    if (!matrixflags.bitshift64) {
      lhuint=pop_anynum32();
      if (rhuint < 32) {
        push_int(lhuint >> rhuint);
      } else push_int(0);
      DEBUGFUNCMSGOUT;
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
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_vasr' deals with arithmetic right shifts (>>)  when the right-hand
** operand is a 32-bit or 64-bit integer value, or a floating point value.
*/
static void eval_vasr(void) {
  int32 rhint = 0;
  int64 lhint64 = 0, res64 = 0;

  DEBUGFUNCMSGIN;
  rhint = pop_anynum32() % 256;
  while (rhint < 0) rhint += 256;

  if (TOPITEMISNUM) {
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
  } else want_number();
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_iveq' deals with the 'equals' operator when the right-hand operand
** is any integer value. It pushes either 'TRUE' or 'FALSE' on to the Basic
** stack depending on whether the two operands are equal or not.
*/
static void eval_iveq(void) {
  int64 rhint = pop_anyint();

  DEBUGFUNCMSGIN;
  push_int(pop_anynumfp() == rhint ? BASTRUE : BASFALSE);
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_fveq' deals with the 'equals' operator when the right-hand operand
** is a floating point value. It pushes either 'TRUE' or 'FALSE' on to the
** Basic stack depending on whether the two operands are equal or not.
*/
static void eval_fveq(void) {
  floatvalue = pop_float();

  DEBUGFUNCMSGIN;
  push_int(pop_anynumfp() == floatvalue ? BASTRUE : BASFALSE);
  DEBUGFUNCMSGOUT;
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

  DEBUGFUNCMSGIN;
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
  push_int(result);
  if (lhitem == STACK_STRTEMP) free_string(lhstring);
  if (rhitem == STACK_STRTEMP) free_string(rhstring);
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_ivne' deals with the 'not equals' operator when the right-hand
** operand is any integer value
*/
static void eval_ivne(void) {
  int64 rhint = pop_anyint();

  DEBUGFUNCMSGIN;
  push_int(pop_anynumfp() != rhint ? BASTRUE : BASFALSE);
  DEBUGFUNCMSGOUT;
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

  DEBUGFUNCMSGIN;
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
  push_int(result);
  if (lhitem == STACK_STRTEMP) free_string(lhstring);
  if (rhitem == STACK_STRTEMP) free_string(rhstring);
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_ivgt' deals with the 'greater than' operator when the right-hand
** operand is any integer value
*/
static void eval_ivgt(void) {
  int64 rhint = pop_anyint();

  DEBUGFUNCMSGIN;
  push_int(pop_anynumfp() > rhint ? BASTRUE : BASFALSE);
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_fvgt' deals with the 'greater than' operator when the right-hand
** operand is a floating point value
*/
static void eval_fvgt(void) {
  floatvalue = pop_float();

  DEBUGFUNCMSGIN;
  push_int(pop_anynumfp() > floatvalue ? BASTRUE : BASFALSE);
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_svgt' deals with the 'greater than' operator when the right-hand
** operand is a string
*/
static void eval_svgt(void) {
  stackitem lhitem, rhitem;
  basicstring lhstring, rhstring;
  int32 result, complen;

  DEBUGFUNCMSGIN;
  rhitem = GET_TOPITEM;
  rhstring = pop_string();
  lhitem = GET_TOPITEM;
  if (lhitem != STACK_STRING && lhitem != STACK_STRTEMP) want_string();
  lhstring = pop_string();
  if (lhstring.stringlen < rhstring.stringlen)  /* Compare shorter of two strings */
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
  push_int(result);
  if (lhitem == STACK_STRTEMP) free_string(lhstring);
  if (rhitem == STACK_STRTEMP) free_string(rhstring);
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_ivlt' handles the 'less than' operator when the right-hand
** operand is any integer
*/
static void eval_ivlt(void) {
  int64 rhint = pop_anyint();

  DEBUGFUNCMSGIN;
  push_int(pop_anynumfp() < rhint ? BASTRUE : BASFALSE);
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_fvlt' handles the 'less than' operator when the right-hand
** operand is a floating point value
*/
static void eval_fvlt(void) {
  floatvalue = pop_float();

  DEBUGFUNCMSGIN;
  push_int(pop_anynumfp() < floatvalue ? BASTRUE : BASFALSE);
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_ivlt' handles the 'less than' operator when the right-hand
** operand is a string
*/
static void eval_svlt(void) {
  stackitem lhitem, rhitem;
  basicstring lhstring, rhstring;
  int32 result, complen;

  DEBUGFUNCMSGIN;
  rhitem = GET_TOPITEM;
  rhstring = pop_string();
  lhitem = GET_TOPITEM;
  if (lhitem != STACK_STRING && lhitem != STACK_STRTEMP) want_string();
  lhstring = pop_string();
  if (lhstring.stringlen < rhstring.stringlen)  /* Compare shorter of two strings */
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
  push_int(result);
  if (lhitem == STACK_STRTEMP) free_string(lhstring);
  if (rhitem == STACK_STRTEMP) free_string(rhstring);
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_ivge' handles the 'greater than or equal to' operator when the
** right-hand operand is any integer value
*/
static void eval_ivge(void) {
  int64 rhint = pop_anyint();

  DEBUGFUNCMSGIN;
  push_int(pop_anynumfp() >= rhint ? BASTRUE : BASFALSE);
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_fvge' handles the 'greater than or equal to' operator when the
** right-hand operand is a floating point value
*/
static void eval_fvge(void) {
  floatvalue = pop_float();

  DEBUGFUNCMSGIN;
  push_int(pop_anynumfp() >= floatvalue ? BASTRUE : BASFALSE);
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_svge' handles the 'greater than or equal to' operator when the
** right-hand operand is a string
*/
static void eval_svge(void) {
  stackitem lhitem, rhitem;
  basicstring lhstring, rhstring;
  int32 result, complen;

  DEBUGFUNCMSGIN;
  rhitem = GET_TOPITEM;
  rhstring = pop_string();
  lhitem = GET_TOPITEM;
  if (lhitem != STACK_STRING && lhitem != STACK_STRTEMP) want_string();
  lhstring = pop_string();
  if (lhstring.stringlen < rhstring.stringlen)  /* Compare shorter of two strings */
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
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_ivle' deals with the 'less than or equal to' operator when the
** right-hand operand is a 32-bit integer value
*/
static void eval_ivle(void) {
  int64 rhint = pop_anyint();

  DEBUGFUNCMSGIN;
  push_int(pop_anynumfp() <= rhint ? BASTRUE : BASFALSE);
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_fvle' deals with the 'less than or equal to' operator when the
** right-hand operand is a floating point value
*/
static void eval_fvle(void) {
  floatvalue = pop_float();

  DEBUGFUNCMSGIN;
  push_int(pop_anynumfp() <= floatvalue ? BASTRUE : BASFALSE);
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_svle' deals with the 'less than or equal to' operator when the
** right-hand operand is a string
*/
static void eval_svle(void) {
  stackitem lhitem, rhitem;
  basicstring lhstring, rhstring;
  int32 result, complen;

  DEBUGFUNCMSGIN;
  rhitem = GET_TOPITEM;
  rhstring = pop_string();
  lhitem = GET_TOPITEM;
  if (lhitem != STACK_STRING && lhitem != STACK_STRTEMP) want_string();
  lhstring = pop_string();
  if (lhstring.stringlen < rhstring.stringlen)  /* Compare shorter of two strings */
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
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_ivand' deals with the logical 'and' operator when the right-hand
** operand is any integer or floating point value
*/
static void eval_ivand(void) {
  int64 rhint=pop_anynum64();
  int64 lhint=pop_anynum64();

  DEBUGFUNCMSGIN;
  push_varyint(lhint & rhint);
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_ivor' deals with the logical 'or' operator when the right-hand
** operand is any integer or floating point value
*/
static void eval_ivor(void) {
  int64 rhint=pop_anynum64();
  int64 lhint=pop_anynum64();

  DEBUGFUNCMSGIN;
  push_varyint(lhint | rhint);
  DEBUGFUNCMSGOUT;
}

/*
** 'eval_iveor' deals with the exclusive or operator when right-hand
** operand is any integer or floating point value
*/
static void eval_iveor(void) {
  int64 rhint=pop_anynum64();
  int64 lhint=pop_anynum64();

  DEBUGFUNCMSGIN;
  push_varyint(lhint ^ rhint);
  DEBUGFUNCMSGOUT;
}

/*
** 'do_unaryplus' handles the unary '+' operator. This is a
** no-op apart from type checking
*/
static void do_unaryplus(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;          /* Skip '+' */
  (*factor_table[*basicvars.current])();
  switch(GET_TOPITEM) {
    case STACK_INT:
    case STACK_UINT8:
    case STACK_INT64:
    case STACK_FLOAT:
    case STACK_INTARRAY:
    case STACK_UINT8ARRAY:
    case STACK_INT64ARRAY:
    case STACK_FLOATARRAY:
      break;
    default:
      DEBUGFUNCMSGOUT;
      error(ERR_TYPENUM);
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'do_unaryminus' negates the value on top of the stack
*/
static void do_unaryminus(void) {
  basicarray *tmparray;
  int32 topitem;

  DEBUGFUNCMSGIN;
  basicvars.current++;          /* Skip '-' */
  (*factor_table[*basicvars.current])();
  topitem = GET_TOPITEM;
  switch(topitem) {
    case STACK_INT:   NEGATE_INT; break;
    case STACK_UINT8: push_int(pop_anyint() * -1); break;
    case STACK_INT64: NEGATE_INT64; break;
    case STACK_FLOAT: NEGATE_FLOAT; break;
    case STACK_INTARRAY:
    case STACK_INT64ARRAY:
    case STACK_UINT8ARRAY:
    case STACK_FLOATARRAY:
      tmparray=pop_array();
      push_int(0);
      switch(topitem) {
        case STACK_INTARRAY:
          push_array(tmparray, VAR_INTWORD);
          eval_iaminus();
          break;
        case STACK_INT64ARRAY:
          push_array(tmparray, VAR_INTLONG);
          eval_i64aminus();
          break;
        case STACK_UINT8ARRAY:
          push_array(tmparray, VAR_UINT8);
          eval_iu8aminus();
          break;
        case STACK_FLOATARRAY:
          push_array(tmparray, VAR_FLOAT);
          eval_faminus();
          break;
      }
      break;
    default:
      DEBUGFUNCMSGOUT;
      error(ERR_TYPENUM);
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'factor_table' is a table of functions indexed by token type used
** to deal with factors in an expression.
** Note that there are a number of entries in here for functions
** as the keywords can be used as both statement types and functions
*/
void (*factor_table[256])(void) = {
  bad_syntax,   do_xvar,       do_staticvar,  do_uint8var,  /* 00..03 */
  do_intvar,    do_int64var,   do_floatvar,   do_stringvar, /* 04..07 */
  do_arrayvar,  do_arrayref,   do_arrayref,   do_indrefvar, /* 08..0B */
  do_indrefvar, do_indrefvar,  do_statindvar, do_xfunction, /* 0C..0F */
  do_function,  do_intzero,    do_intone,     do_smallconst,/* 10..13 */
  do_intconst,  do_floatzero,  do_floatone,   do_floatconst,/* 14..17 */
  do_stringcon, do_qstringcon, do_int64const, bad_token,    /* 18..1B */
  bad_token,    bad_token,     bad_token,     bad_token,    /* 1C..1F */
  bad_token,    do_getword,    bad_syntax,    bad_syntax,   /* 20..23 */
  do_getstring, bad_syntax,    bad_syntax,    bad_syntax,   /* 24..27 */
  do_brackets,  bad_syntax,    bad_syntax,    do_unaryplus, /* 28..2B */
  bad_syntax,   do_unaryminus, bad_syntax,    bad_syntax,   /* 2C..2F */
  bad_token,    bad_token,     bad_token,     bad_token,    /* 30..33 */
  bad_token,    bad_token,     bad_token,     bad_token,    /* 34..37 */
  bad_token,    bad_token,     bad_syntax,    bad_syntax,   /* 38..3B */
  bad_syntax,   bad_syntax,    bad_syntax,    do_getbyte,   /* 3C..3F */
  bad_syntax,   bad_token,     bad_token,     bad_token,    /* 40..43 */
  bad_token,    bad_token,     bad_token,     bad_token,    /* 44..47 */
  bad_token,    bad_token,     bad_token,     bad_token,    /* 48..4B */
  bad_token,    bad_token,     bad_token,     bad_token,    /* 4C..4F */
  bad_token,    bad_token,     bad_token,     bad_token,    /* 50..53 */
  bad_token,    bad_token,     bad_token,     bad_token,    /* 54..57 */
  bad_token,    bad_token,     bad_token,     bad_syntax,   /* 58..5B */
  bad_syntax,   do_getlong,    bad_syntax,    bad_token,    /* 5C..5F */
  bad_token,    bad_token,     bad_token,     bad_token,    /* 60..63 */
  bad_token,    bad_token,     bad_token,     bad_token,    /* 64..67 */
  bad_token,    bad_token,     bad_token,     bad_token,    /* 68..6B */
  bad_token,    bad_token,     bad_token,     bad_token,    /* 6C..6F */
  bad_token,    bad_token,     bad_token,     bad_token,    /* 70..73 */
  bad_token,    bad_token,     bad_token,     bad_token,    /* 74..77 */
  bad_token,    bad_token,     bad_token,     bad_syntax,   /* 78..7B */
  do_getfloat,  bad_syntax,    bad_syntax,    bad_token,    /* 7C..7F */
  bad_syntax,   bad_syntax,    bad_syntax,    bad_syntax,   /* 80..83 */
  bad_syntax,   bad_syntax,    bad_syntax,    bad_syntax,   /* 84..87 */
  bad_syntax,   fn_mod,        bad_syntax,    bad_syntax,   /* 88..8B */
  bad_syntax,   fn_beats,      bad_syntax,    bad_syntax,   /* 8C..8F */
  bad_syntax,   bad_syntax,    bad_syntax,    bad_syntax,   /* 90..93 */
  bad_syntax,   bad_syntax,    bad_syntax,    bad_syntax,   /* 94..97 */
  fn_colour,    bad_syntax,    bad_syntax,    fn_dim,       /* 98..9B */
  bad_syntax,   bad_syntax,    bad_syntax,    bad_syntax,   /* 9C..9F */
  bad_syntax,   bad_syntax,    bad_syntax,    fn_end,       /* A0..A3 */
  bad_syntax,   bad_syntax,    bad_syntax,    bad_syntax,   /* A4..A7 */
  bad_syntax,   bad_token,     fn_false,      bad_syntax,   /* A8..AB */
  bad_syntax,   bad_syntax,    bad_syntax,    bad_syntax,   /* AC..AF */
  bad_syntax,   bad_syntax,    bad_syntax,    bad_syntax,   /* B0..B3 */
  bad_syntax,   bad_syntax,    bad_syntax,    bad_syntax,   /* B4..B7 */
  bad_syntax,   bad_syntax,    fn_mode,       bad_syntax,   /* B8..BB */
  bad_syntax,   bad_syntax,    bad_syntax,    fn_not,       /* BC..BF */
  bad_syntax,   bad_syntax,    bad_syntax,    bad_syntax,   /* C0..C3 */
  bad_syntax,   bad_syntax,    bad_syntax,    bad_syntax,   /* C4..C7 */
  bad_syntax,   bad_syntax,    bad_syntax,    bad_syntax,   /* C8..CB */
  bad_syntax,   bad_syntax,    fn_quit,       bad_syntax,   /* CC..CF */
  bad_syntax,   bad_syntax,    bad_syntax,    bad_syntax,   /* D0..D3 */
  bad_syntax,   bad_syntax,    bad_syntax,    bad_syntax,   /* D4..D7 */
  bad_syntax,   bad_syntax,    bad_syntax,    bad_syntax,   /* D8..DB */
  bad_syntax,   bad_syntax,    bad_syntax,    bad_syntax,   /* DC..DF */
  fn_tint,      fn_top,        fn_trace,      fn_true,      /* E0..E3 */
  bad_syntax,   fn_vdu,        bad_syntax,    bad_syntax,   /* E4..E7 */
  bad_syntax,   bad_syntax,    bad_syntax,    bad_syntax,   /* E8..EB */
  bad_syntax,   fn_width,      bad_token,     bad_token,    /* EC..EF */
  bad_token,    bad_token,     bad_token,     bad_token,    /* F0..F3 */
  bad_token,    bad_token,     bad_token,     bad_token,    /* F4..F7 */
  bad_token,    bad_token,     bad_token,     bad_token,    /* F8..FB */
  bad_syntax,   bad_token,     bad_syntax,    exec_function /* FC..FF */
};

/*
** Operator table
** This gives the priority of each dyadic operator, indexed by the
** operator's token value. A value of zero means that the token is not
** an operator (and that the end of the expression has been reached)
*/
static int32 optable [256] = {  /* Character -> priority/operator */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,       /* 00..0F */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,       /* 10..1F */
  0, 0, 0, 0, 0, 0, 0, 0,                               /* 20..27 */
  0, 0, MULPRIO+OP_MUL, ADDPRIO+OP_ADD,                 /* 28..2B */
  0, ADDPRIO+OP_SUB, MULPRIO+OP_MATMUL, MULPRIO+OP_DIV, /* 2C..2F */
  0, 0, 0, 0, 0, 0, 0, 0,                               /* 30..37 */
  0, 0, 0, 0,                                           /* 38..3B */
  COMPRIO+OP_LT, COMPRIO+OP_EQ, COMPRIO+OP_GT, 0,       /* 3C..3F */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,       /* 40..4F */
  0, 0, 0, 0, 0, 0, 0, 0,                               /* 50..57 */
  0, 0, 0, 0, 0, 0, POWPRIO+OP_POW, 0,                  /* 58..5F */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,       /* 60..6F */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,       /* 70..7F */
  ANDPRIO+OP_AND, COMPRIO+OP_ASR, MULPRIO+OP_INTDIV, ORPRIO+OP_EOR, /* 80..83 */
  COMPRIO+OP_GE,  COMPRIO+OP_LE,  COMPRIO+OP_LSL,    COMPRIO+OP_LSR,/* 84..87 */
  0,              MULPRIO+OP_MOD, COMPRIO+OP_NE,     ORPRIO+OP_OR,  /* 88..8B */
  0, 0, 0, 0,                                           /* 8C..8F */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,       /* 90..9F */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,       /* A0..AF */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,       /* B0..BF */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,       /* C0..CF */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,       /* D0..DF */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,       /* E0..EF */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0        /* F0..FF */
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

  DEBUGFUNCMSGIN;
  if (*basicvars.current == ' ') {
  DEBUGFUNCMSG("Bumping current");
    basicvars.current++;
  }
  if (*basicvars.current == '\\') {
    /* Try to handle a BBCSDL long line */
    DEBUGFUNCMSG("Trying to handle a BBCSDL long line");
    while (!isateol(basicvars.current)) basicvars.current++;
    next_line();
    basicvars.current++;
  }
#ifdef DEBUG
  if (basicvars.debug_flags.debug) fprintf(stderr, "    expression: About to factor table jump, *basicvars.current=0x%X, current=0x%llX at line %d\n", *basicvars.current, (int64)basicvars.current, 2 + __LINE__);
#endif
  (*factor_table[*basicvars.current])();        /* Get first factor in the expression */
#ifdef DEBUG
  if (basicvars.debug_flags.debug) fprintf(stderr, "expression: returned from factor_table jump, current=0x%llX\n", (int64)basicvars.current);
#endif
  lastop = optable[*basicvars.current];
  if (lastop == 0) {
    DEBUGFUNCMSGOUT;
    return;     /* Quick way out if there is nothing to do */
  }
  basicvars.current++;          /* Skip operator (always one character) */
#ifdef DEBUG
  if (basicvars.debug_flags.debug) fprintf(stderr, "    expression: About to factor table jump, *basicvars.current=0x%X, current=0x%llX at line %d\n", *basicvars.current, (int64)basicvars.current, 2 + __LINE__);
#endif
  (*factor_table[*basicvars.current])();        /* Get second operand */
#ifdef DEBUG
  if (basicvars.debug_flags.debug) fprintf(stderr, "expression: returned from factor_table jump, current=0x%llX\n", (int64)basicvars.current);
#endif
  thisop = optable[*basicvars.current];
  if (thisop == 0) {
/* Have got a simple '<value> <op> <value>' type of expression */
    (*opfunctions[lastop & OPERMASK][GET_TOPITEM])();
#ifdef DEBUG
    if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function evaluate.c:expression via thisop=0, current=0x%llX\n", (int64)basicvars.current);
#endif
    return;
  }
/* Expression is more complex so we have to invoke the heavy machinery */
  if (basicvars.opstop == basicvars.opstlimit) {
    DEBUGFUNCMSGOUT;
    error(ERR_OPSTACK);
  }
  basicvars.opstop++;
  *basicvars.opstop = OPSTACKMARK;
  do {
    if (PRIORITY(thisop) > PRIORITY(lastop)) {  /* Priority of this operator > priority of last */
      if (basicvars.opstop == basicvars.opstlimit) {
        DEBUGFUNCMSGOUT;
        error(ERR_OPSTACK);
      }
    }
    else {      /* Priority of this operator <= last op's priority - exec last operator */
      if (PRIORITY(thisop) == COMPRIO) {                /* Ghastly hack for ghastly Basic relational operator syntax */
        while (PRIORITY(lastop) >= PRIORITY(thisop) && PRIORITY(lastop) != COMPRIO) {
          (*opfunctions[lastop & OPERMASK][GET_TOPITEM])();
          lastop = *basicvars.opstop;
          basicvars.opstop--;
        }
        if (PRIORITY(lastop) == COMPRIO) break;
      }
      else {    /* Normal case without check for relational operator */
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
    basicvars.current++;        /* Skip operator (always one character) */
    (*factor_table[*basicvars.current])();      /* Get next operand */
    thisop = optable[*basicvars.current];
  } while (thisop != 0);
  while (lastop != OPSTACKMARK) {       /* Now clear the operator stack */
    (*opfunctions[lastop & OPERMASK][GET_TOPITEM])();
    lastop = *basicvars.opstop;
    basicvars.opstop--;
  }
#ifdef DEBUG
    if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function evaluate.c:expression at end of function, current=0x%llX\n", (int64)basicvars.current);
#endif
}

/*
** 'factor' is similar to expression. It is used in cases where the language
** specifies a 'factor' instead of a complete expression. In most cases
** it will be the built-in functions that invoke this code but some
** statement types such as 'BPUT' use it too.
*/
void factor(void) {
  DEBUGFUNCMSGIN;
  *basicvars.opstop = OPSTACKMARK;
  (*factor_table[*basicvars.current])();
  if (*basicvars.opstop != OPSTACKMARK) {
    DEBUGFUNCMSGOUT;
    error(ERR_BADEXPR);
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'init_expressions' is called to reset the expression evaluation code
** before running a program
*/
void init_expressions(void) {
  DEBUGFUNCMSGIN;
  basicvars.opstop = make_opstack();
  basicvars.opstlimit = basicvars.opstop+OPSTACKSIZE;
  *basicvars.opstop = OPSTACKMARK;
  init_functions();
  DEBUGFUNCMSGOUT;
}

/*
** 'reset_opstack' is called to reset the operator stack pointer to its
** initial value
*/
void reset_opstack(void) {
  DEBUGFUNCMSGIN;
  basicvars.opstop = basicvars.opstlimit-OPSTACKSIZE;
  *basicvars.opstop = OPSTACKMARK;
  DEBUGFUNCMSGOUT;
}
