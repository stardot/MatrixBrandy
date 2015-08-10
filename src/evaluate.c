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

/* #define DEBUG */

/*
** The expression evaluator forms the heart of the interpreter. Doing
** just about anything involves calling the functions in this module.
** The evaluation code uses two different methods for parsing code
** depending on which was the more convenient to use at the time. The
** dyadic operators are evaluated using operator precedence but recursive
** descent is used as well, for example, in function calls.
** Error handling: all errors are dealt with by calls to function 'error'.
** This function does not return (it does a 'longjmp' to a well-defined
** position in the interpreter)
*/

#ifdef TARGET_DJGPP
#define DJGPPLIMIT 75000		/* Minimum amount of C stack allowed in DJGPP version of program */
#endif

#define RADCONV 57.2957795130823229	/* Used when converting degrees -> radians and vice versa */
#define TIMEFORMAT "%a,%d %b %Y.%H:%M:%S"  /* Date format used by 'TIME$' */

#define OPSTACKMARK 0			/* 'Operator' used as sentinel at the base of the operator stack */

static float64 floatvalue;		/* Temporary for holding floating point values */

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
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN},
/* Byte-sized integer */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_NONE,    ERR_NONE,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM},
/* Word-sized integer */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_NONE,    ERR_NONE,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM},
/* Floating point */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_NONE,    ERR_NONE,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM},
/* 'string$' type string */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_PARMSTR, ERR_PARMSTR,
  ERR_NONE,    ERR_NONE,    ERR_PARMSTR, ERR_PARMSTR,
  ERR_PARMSTR, ERR_PARMSTR, ERR_PARMSTR, ERR_PARMSTR},
/* '$string' type string */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_PARMSTR, ERR_PARMSTR,
  ERR_NONE,    ERR_NONE,    ERR_PARMSTR, ERR_PARMSTR,
  ERR_PARMSTR, ERR_PARMSTR, ERR_PARMSTR, ERR_PARMSTR},
/* Undefined variable type (6) */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN},
/* Undefined variable type (7) */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN},
/* Undefined array type (8) */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN},
/* Byte-sized integer array (9) */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN},
/* Word-sized integer array */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_NONE,    ERR_NONE,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM},
/* Floating point array */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_PARMNUM, ERR_PARMNUM,
  ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM, ERR_PARMNUM,
  ERR_NONE,    ERR_NONE,    ERR_PARMNUM, ERR_PARMNUM},
/* 'string$' array */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_PARMSTR, ERR_PARMSTR,
  ERR_PARMSTR, ERR_PARMSTR, ERR_PARMSTR, ERR_PARMSTR,
  ERR_PARMSTR, ERR_PARMSTR, ERR_NONE,    ERR_NONE},
/* Undefined array type (0x0d) */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN},
/* Undefined array type (0x0e) */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN},
/* Undefined array type (0x0f) */
 {ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,
  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN,  ERR_BROKEN}
};

/*
** 'push_oneparm' is called to deal with a PROC or FN parameter. It does this
** in two stages, evaluating the parameter first and after all the other have
** been processed, moving it to the variable being used as the formal parameter
*/
static void push_oneparm(formparm *fp, int32 parmno, char *procname) {
  int32 intparm = 0, typerr;
  float64 floatparm = 0;
  basicstring stringparm = {0, NULL};
  basicarray *arrayparm = NULL;
  lvalue retparm;
  stackitem parmtype = STACK_UNKNOWN;
  boolean isreturn;
  isreturn = (fp->parameter.typeinfo & VAR_RETURN) != 0;
  if (!isreturn) {	/* Normal parameter */
    expression();
    parmtype = GET_TOPITEM;
    if (parmtype == STACK_INT)
      intparm = pop_int();
    else if (parmtype == STACK_FLOAT)
      floatparm = pop_float();
    else if (parmtype == STACK_STRING || parmtype == STACK_STRTEMP)
      stringparm = pop_string();
    else if (parmtype >= STACK_INTARRAY && parmtype <= STACK_SATEMP)
      arrayparm = pop_array();
    else {
      error(ERR_BROKEN, __LINE__, "evaluate");
    }
  }
  else {	/* Return parameter */
    get_lvalue(&retparm);
    switch (retparm.typeinfo) {	/* Now fetch the parameter's value */
    case VAR_INTWORD:		/* Integer parameter */
      intparm = *retparm.address.intaddr;
      parmtype = STACK_INT;
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
    if (*basicvars.current == ')') error(ERR_SYNTAX);
    if (fp->nextparm == NIL) error(ERR_TOOMANY, procname);
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
    *p = (parmtype == STACK_INT ? intparm : TOINT(floatparm));
    return;
  }
/* Now deal with other parameter types */
  switch (fp->parameter.typeinfo & PARMTYPEMASK) {	/* Go by formal parameter type */
  case VAR_FLOAT: {		/* Floating point parameter */
    float64 *p = fp->parameter.address.floataddr;
    if (isreturn)
      save_retfloat(retparm, fp->parameter, *p);
    else {
      save_float(fp->parameter, *p);
    }
    *p = (parmtype == STACK_INT ? TOFLOAT(intparm) : floatparm);
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
    sp[stringparm.stringlen] = CR;
    if (parmtype == STACK_STRTEMP) free_string(stringparm);
    break;
  }
  case VAR_INTARRAY: case VAR_FLOATARRAY: case VAR_STRARRAY:
    save_array(fp->parameter);
    *fp->parameter.address.arrayaddr = arrayparm;
    break;
  default:		/* Bad parameter type */
    error(ERR_BROKEN, __LINE__, "evaluate");
  }
}

/*
** 'push_singleparm' is called when a procedure or function has a single
** integer parameter
*/
static void push_singleparm(formparm *fp, char *procname) {
  stackitem parmtype;
  int32 intparm;
  expression();
  if (*basicvars.current != ')') {	/* Try to put out a meaningful error message */
    if (*basicvars.current == ',')	/* Assume there is another parameter */
      error(ERR_TOOMANY, procname);
    else {	/* Something else - Assume ')' is missing */
      error(ERR_RPMISS);
    }
  }
  basicvars.current++;	/* Skip the ')' */
  parmtype = GET_TOPITEM;
  if (parmtype != STACK_INT && parmtype != STACK_FLOAT) error(ERR_PARMNUM, 1);
  intparm = parmtype == STACK_INT ? pop_int() : TOINT(pop_float());	/* Value has to be popped first */
  save_int(fp->parameter, *(fp->parameter.address.intaddr));
  *(fp->parameter.address.intaddr) = intparm;
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
  int32 address;
  byte operator;
  address = basicvars.staticvars[*(basicvars.current+1)].varentry.varinteger;
  basicvars.current+=2;
  operator = *basicvars.current;
  basicvars.current++;
  (*factor_table[*basicvars.current])();
  if (GET_TOPITEM == STACK_INT)	/* Calculate the address to be referenced */
    address+=pop_int();
  else if (GET_TOPITEM == STACK_FLOAT)
    address+=TOINT(pop_float());
  else {
    error(ERR_TYPENUM);
  }
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
** 'do_intconst' pushes a four byte integer constant on to the Basic stack
*/
static void do_intconst(void) {
  basicvars.current++;		/* Point current at binary version of number */
  PUSH_INT(GET_INTVALUE(basicvars.current));
  basicvars.current+=INTSIZE;
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
** 'do_intvar' handles a simple reference to a known integer variable,
** pushing its value on to the stack. The variable is known to be not
** followed by an indirection operator
*/
static void do_intvar(void) {
  int32 *ip;
  ip = GET_ADDRESS(basicvars.current, int32 *);
  basicvars.current+=LOFFSIZE+1;	/* Skip pointer */
  PUSH_INT(*ip);
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
  int32 vartype, maxdims, index = 0, dimcount, element = 0, offset = 0;
  basicarray *descriptor;
  vp = GET_ADDRESS(basicvars.current, variable *);
  basicvars.current+=LOFFSIZE+1;	/* Skip pointer to variable */
  descriptor = vp->varentry.vararray;
  vartype = vp->varflags;
  if (descriptor->dimcount == 1) {	/* Array has only one dimension - Use faster code */
    expression();	      /* Evaluate an array index */
    if (GET_TOPITEM == STACK_INT)
      element = pop_int();
    else if (GET_TOPITEM == STACK_FLOAT)
      element = TOINT(pop_float());
    else {
      error(ERR_TYPENUM);
    }
    if (element < 0 || element >= descriptor->dimsize[0]) error(ERR_BADINDEX, element, vp->varname);
  }
  else {	/* Multi-dimensional array */
    maxdims = descriptor->dimcount;
    dimcount = 0;
    element = 0;
    do {	/* Gather the array indexes */
      expression();	      /* Evaluate an array index */
      if (GET_TOPITEM == STACK_INT)
        index = pop_int();
      else if (GET_TOPITEM == STACK_FLOAT)
        index = TOINT(pop_float());
      else {
        error(ERR_TYPENUM);
      }
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
    if (vartype == VAR_INTARRAY) 	/* Fetch the element value */
      offset = vp->varentry.vararray->arraystart.intbase[element];
    else if (vartype == VAR_FLOATARRAY)
      offset = TOINT(vp->varentry.vararray->arraystart.floatbase[element]);
    else {
      error(ERR_TYPENUM);
    }
    operator = *basicvars.current;
    basicvars.current++;
    (*factor_table[*basicvars.current])();
    if (GET_TOPITEM == STACK_INT)	/* Calculate the offset to be referenced */
      offset+=pop_int();
    else if (GET_TOPITEM == STACK_FLOAT)
      offset+=TOINT(pop_float());
    else {
      error(ERR_TYPENUM);
    }
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
  int32 offset;
  if (*basicvars.current == TOKEN_INTINDVAR)	/* Fetch variable's value */
    offset = *GET_ADDRESS(basicvars.current, int32 *);
  else {
    offset = TOINT(*GET_ADDRESS(basicvars.current, float64 *));
  }
  basicvars.current+=LOFFSIZE+1;		/* Skip pointer to variable */
  operator = *basicvars.current;
  basicvars.current++;
  (*factor_table[*basicvars.current])();
  if (GET_TOPITEM == STACK_INT)	/* Calculate the offset to be referenced */
    offset+=pop_int();
  else if (GET_TOPITEM == STACK_FLOAT)
    offset+=TOINT(pop_float());
  else {
    error(ERR_TYPENUM);
  }
  if (operator == '?') {	/* Byte-sized integer */
    check_read(offset, sizeof(byte));
    push_int(basicvars.offbase[offset]);
  }
  else {		/* Word-sized integer */
    push_int(get_integer(offset));
  }
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
      *basicvars.current = TOKEN_INTINDVAR;
      set_address(basicvars.current, &vp->varentry.varinteger);
      break;
    case VAR_FLOAT:
      *basicvars.current = TOKEN_FLOATINDVAR;
      set_address(basicvars.current, &vp->varentry.varfloat);
      break;
    default:
      error(ERR_VARNUM);
    }
    do_indrefvar();
  }
  else {	/* Simple reference to variable or reference to an array */
    if (vartype == VAR_INTWORD) {
      *basicvars.current = TOKEN_INTVAR;
      set_address(basicvars.current, &vp->varentry.varinteger);
      do_intvar();
    }
    else if (vartype == VAR_FLOAT) {
      *basicvars.current = TOKEN_FLOATVAR;
      set_address(basicvars.current, &vp->varentry.varfloat);
      do_floatvar();
    }
    else if (vartype == VAR_STRINGDOL) {
      *basicvars.current = TOKEN_STRINGVAR;
      set_address(basicvars.current, &vp->varentry.varstring);
      do_stringvar();
    }
    else {	/* Array or array followed by an indirection operator */
      if (*np == ')') {	/* Reference is to entire array */
        *basicvars.current = TOKEN_ARRAYVAR;
        set_address(basicvars.current, vp);
        do_arrayvar();
      }
      else {	/* Reference is to an array element */
        *basicvars.current = TOKEN_ARRAYREF;
        set_address(basicvars.current, vp);
        do_arrayref();
      }
    }
  }
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
  if (!GET_TOPITEM == STACK_INT && !GET_TOPITEM == STACK_FLOAT) error(ERR_TYPENUM);
}

/*
** 'do_unaryminus' negates the value on top of the stack
*/
static void do_unaryminus(void) {
  basicvars.current++;		/* Skip '-' */
  (*factor_table[*basicvars.current])();
  if (GET_TOPITEM == STACK_INT)
    NEGATE_INT;
  else if (GET_TOPITEM == STACK_FLOAT)
    NEGATE_FLOAT;
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'do_getbyte' handles the byte indirection operator, '?', pushing
** the byte addressed by the numeric value on top of the stack on to
** the stack
*/
static void do_getbyte(void) {
  int32 offset = 0;
  basicvars.current++;		/* Skip '?' */
  (*factor_table[*basicvars.current])();
  if (GET_TOPITEM == STACK_INT)
    offset = pop_int();
  else if (GET_TOPITEM == STACK_FLOAT)
    offset = TOINT(pop_float());
  else {
    error(ERR_TYPENUM);
  }
  check_read(offset, sizeof(byte));
  push_int(basicvars.offbase[offset]);
}

/*
** 'do_getword' handles the word indirection operator, '!', pushing the
** word addressed by the numeric value on top of the Basic stack on to
** the stack. The address of the word to be pushed is byte-aligned
*/
static void do_getword(void) {
  int32 offset = 0;
  basicvars.current++;		/* Skip '!' */
  (*factor_table[*basicvars.current])();
  if (GET_TOPITEM == STACK_INT)
    offset = pop_int();
  else if (GET_TOPITEM == STACK_FLOAT)
    offset = TOINT(pop_float());
  else {
    error(ERR_TYPENUM);
  }
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
  int32 offset = 0, len;
  basicvars.current++;		/* Skip '$' */
  (*factor_table[*basicvars.current])();
  if (GET_TOPITEM == STACK_INT)
    offset = pop_int();
  else if (GET_TOPITEM == STACK_FLOAT)
    offset = TOINT(pop_float());
  else {
    error(ERR_TYPENUM);
  }
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
  int32 offset = 0;
  basicvars.current++;		/* Skip '|' */
  (*factor_table[*basicvars.current])();
  if (GET_TOPITEM == STACK_INT)
    offset = pop_int();
  else if (GET_TOPITEM == STACK_FLOAT)
    offset = TOINT(pop_float());
  else {
    error(ERR_TYPENUM);
  }
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
** block that 'longjmp' can use when returning control to the program
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
  if (basicvars.escape) error(ERR_ESCAPE);
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
  if (setjmp(*basicvars.local_restart) == 0)
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
  if (*base != TOKEN_FN) error(ERR_NOTAFN);	/* Ensure a function is being called */
  tp = skip_name(base);
  gotparms = *(tp-1) == '(';
  if (gotparms) tp--;	/* '(' found but it is not part of name */
  vp = find_fnproc(base, tp-base);
  dp = vp->varentry.varfnproc;
  *basicvars.current = TOKEN_FNPROCALL;
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
  if (baditem == STACK_INT || baditem == STACK_FLOAT)		/* String operand required */
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
  switch (arraytype) {
  case VAR_INTWORD:
    base = alloc_stackmem(original->arrsize*sizeof(int32));
    result.arraystart.intbase = base;
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
  return base;
}

/*
** 'eval_ivplus' deals with addition when the right-hand operand is
** an integer value. All versions of the operator are dealt with
** by this function
*/
static void eval_ivplus(void) {
  stackitem lhitem;
  int32 rhint = pop_int();	/* Top item on Basic stack is right-hand operand */
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)
    INCR_INT(rhint);		/* int+int - Update value on stack in place */
  else if (lhitem == STACK_FLOAT)
    INCR_FLOAT(TOFLOAT(rhint));	/* float+int - Update value on stack in place */
  else if (lhitem == STACK_INTARRAY || lhitem == STACK_FLOATARRAY) {	/* <array>+<integer value> */
    basicarray *lharray;
    int32 n, count;
    lharray = pop_array();
    count = lharray->arrsize;
    if (lhitem == STACK_INTARRAY) {
      int32 *srce, *base = make_array(VAR_INTWORD, lharray);
      srce = lharray->arraystart.intbase;
      for (n = 0; n < count; n++) base[n] = srce[n]+rhint;
    }
    else {
      float64 *srce, *base = make_array(VAR_FLOAT, lharray);
      floatvalue = TOFLOAT(rhint);
      srce = lharray->arraystart.floatbase;
      for (n = 0; n < count; n++) base[n] = srce[n]+floatvalue;
    }
  }
  else if (lhitem == STACK_FATEMP) {	/* <float array>+<integer value> */
    basicarray lharray;
    float64 *base;
    int32 n, count;
    lharray = pop_arraytemp();
    base = lharray.arraystart.floatbase;
    count = lharray.arrsize;
    floatvalue = TOFLOAT(rhint);
    for (n = 0; n < count; n++) base[n]+=floatvalue;
    push_arraytemp(&lharray, VAR_FLOAT);
  }
  else {
    want_number();
  }
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
  if (lhitem == STACK_INT) {	/* Branch according to type of left-hand operand */
    floatvalue+=TOFLOAT(pop_int());	/* This has to be split otherwise the macro */
    PUSH_FLOAT(floatvalue);		/* expansion of PUSH_FLOAT goes wrong */
  }
  else if (lhitem == STACK_FLOAT)
    INCR_FLOAT(floatvalue);
  else if (lhitem == STACK_INTARRAY || lhitem == STACK_FLOATARRAY) {	/* <array>+<float value> */
    basicarray *lharray;
    float64 *base;
    int32 n, count;
    lharray = pop_array();
    count = lharray->arrsize;
    base = make_array(VAR_FLOAT, lharray);
    if (lhitem == STACK_INTARRAY) {
      int32 *srce = lharray->arraystart.intbase;
      for (n = 0; n < count; n++) base[n] = TOFLOAT(srce[n])+floatvalue;
    }
    else {
      float64 *srce = lharray->arraystart.floatbase;
      for (n = 0; n < count; n++) base[n] = srce[n]+floatvalue;
    }
  }
  else if (lhitem == STACK_FATEMP) {	/* <float array>+<float value> */
    basicarray lharray;
    float64 *base;
    int32 n, count;
    lharray = pop_arraytemp();
    base = lharray.arraystart.floatbase;
    count = lharray.arrsize;
    for (n = 0; n < count; n++) base[n]+=floatvalue;
    push_arraytemp(&lharray, VAR_FLOAT);
  }
  else {
    want_number();
  }
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
    }
    else {	/* Any other case - Create a new string temporary */
      cp = alloc_string(newlen);
      memmove(cp, lhstring.stringaddr, lhstring.stringlen);
      memmove(cp+lhstring.stringlen, rhstring.stringaddr, rhstring.stringlen);
    }
    if (rhitem == STACK_STRTEMP) free_string(rhstring);
    push_strtemp(newlen, cp);
  }
  else if (lhitem == STACK_STRARRAY) {	/* <array>+<string> */
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
  }
  else {
    want_string();
  }
}

/*
** 'eval_iaplus' deals with addition when the right-hand operand is
** an integer array. All versions of the operator are dealt with
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
  if (lhitem == STACK_INT) {
    int32 lhint = pop_int();
    int32 *base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) base[n] = lhint+rhsrce[n];
  }
  else if (lhitem == STACK_FLOAT) {	/* <float>+<int array> */
    float64 *base;
    floatvalue = pop_float();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) base[n] = floatvalue+TOFLOAT(rhsrce[n]);
  }
  else if (lhitem == STACK_INTARRAY) {	/* <int array>+<int array> */
    int32 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.intbase;
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) base[n] = lhsrce[n]+rhsrce[n];
  }
  else if (lhitem == STACK_FLOATARRAY) {	/* <float array>+<int array> */
    float64 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) base[n] = lhsrce[n]+TOFLOAT(rhsrce[n]);
  }
  else if (lhitem == STACK_FATEMP) {		/* <float array>+<int array> */
    float64 *lhsrce;
    basicarray lharray = pop_arraytemp();
    if (!check_arrays(&lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray.arraystart.floatbase;
    for (n = 0; n < count; n++) lhsrce[n]+=TOFLOAT(rhsrce[n]);
    push_arraytemp(&lharray, VAR_FLOAT);
  }
  else {
    want_number();
  }
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
  if (lhitem == STACK_INT || lhitem == STACK_FLOAT) {	/* <integer>+<float array> or <float>+<float array> */
    floatvalue = lhitem == STACK_INT ? TOFLOAT(pop_int()) : pop_float();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) base[n] = floatvalue+rhsrce[n];
  }
  else if (lhitem == STACK_INTARRAY) {	/* <int array>+<float array> */
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.intbase;
    for (n = 0; n < count; n++) base[n] = TOFLOAT(lhsrce[n])+rhsrce[n];
  }
  else if (lhitem == STACK_FLOATARRAY) {	/* <float array>+<float array> */
    float64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) base[n] = lhsrce[n]+rhsrce[n];
  }
  else if (lhitem == STACK_FATEMP) {		/* <float array>+<float array> */
    float64 *lhsrce;
    basicarray lharray = pop_arraytemp();
    if (!check_arrays(&lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray.arraystart.floatbase;
    for (n = 0; n < count; n++) lhsrce[n]+=rhsrce[n];
    push_arraytemp(&lharray, VAR_FLOAT);
  }
  else {
    want_number();
  }
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
** an integer value. All versions of the operator are dealt with
** by this function
*/
static void eval_ivminus(void) {
  stackitem lhitem;
  int32 rhint = pop_int();
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)	/* Branch according to type of left-hand operand */
    DECR_INT(rhint);
  else if (lhitem == STACK_FLOAT)
    DECR_FLOAT(TOFLOAT(rhint));
  else if (lhitem == STACK_INTARRAY || lhitem == STACK_FLOATARRAY) {	/* <array>-<integer value> */
    basicarray *lharray;
    int32 n, count;
    lharray = pop_array();
    count = lharray->arrsize;
    if (lhitem == STACK_INTARRAY) {
      int32 *srce, *base = make_array(VAR_INTWORD, lharray);
      srce = lharray->arraystart.intbase;
      for (n = 0; n < count; n++) base[n] = srce[n] - rhint;
    }
    else {
      float64 *srce, *base = make_array(VAR_FLOAT, lharray);
      floatvalue = TOFLOAT(rhint);
      srce = lharray->arraystart.floatbase;
      for (n = 0; n < count; n++) base[n] = srce[n] - floatvalue;
    }
  }
  else if (lhitem == STACK_FATEMP) {	/* <float array>-<integer value> */
    basicarray lharray;
    float64 *base;
    int32 n, count;
    lharray = pop_arraytemp();
    base = lharray.arraystart.floatbase;
    count = lharray.arrsize;
    floatvalue = TOFLOAT(rhint);
    for (n = 0; n < count; n++) base[n] -= floatvalue;
    push_arraytemp(&lharray, VAR_FLOAT);
  }
  else {
    want_number();
  }
}

/*
** 'eval_fvminus' deals with subtraction when the right-hand operand is
** a floating point value
*/
static void eval_fvminus(void) {
  stackitem lhitem;
  floatvalue = pop_float();
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT) {	/* <int>-<float> */
    floatvalue = TOFLOAT(pop_int()) - floatvalue;
    PUSH_FLOAT(floatvalue);
  }
  else if (lhitem == STACK_FLOAT)
    DECR_FLOAT(floatvalue);
  else if (lhitem == STACK_INTARRAY || lhitem == STACK_FLOATARRAY) {	/* <array>-<float value> */
    basicarray *lharray;
    float64 *base;
    int32 n, count;
    lharray = pop_array();
    count = lharray->arrsize;
    base = make_array(VAR_FLOAT, lharray);
    if (lhitem == STACK_INTARRAY) {
      int32 *srce = lharray->arraystart.intbase;
      for (n = 0; n < count; n++) base[n] = TOFLOAT(srce[n]) - floatvalue;
    }
    else {
      float64 *srce = lharray->arraystart.floatbase;
      for (n = 0; n < count; n++) base[n] = srce[n] - floatvalue;
    }
  }
  else if (lhitem == STACK_FATEMP) {	/* <float array>-<float value> */
    basicarray lharray;
    float64 *base;
    int32 n, count;
    lharray = pop_arraytemp();
    base = lharray.arraystart.floatbase;
    count = lharray.arrsize;
    for (n = 0; n < count; n++) base[n] -= floatvalue;
    push_arraytemp(&lharray, VAR_FLOAT);
  }
  else {
    want_number();
  }
}

/*
** 'eval_iaminus' deals with subtraction when the right-hand operand is
** an integer array. All versions of the operator are dealt with
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
  if (lhitem == STACK_INT) {
    int32 lhint = pop_int();
    int32 *base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) base[n] = lhint - rhsrce[n];
  }
  else if (lhitem == STACK_FLOAT) {	/* <float>-<int array> */
    float64 *base;
    floatvalue = pop_float();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) base[n] = floatvalue - TOFLOAT(rhsrce[n]);
  }
  else if (lhitem == STACK_INTARRAY) {	/* <int array>-<int array> */
    int32 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.intbase;
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) base[n] = lhsrce[n] - rhsrce[n];
  }
  else if (lhitem == STACK_FLOATARRAY) {	/* <float array>-<int array> */
    float64 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) base[n] = lhsrce[n] - TOFLOAT(rhsrce[n]);
  }
  else if (lhitem == STACK_FATEMP) {		/* <float array>-<int array> */
    float64 *lhsrce;
    basicarray lharray = pop_arraytemp();
    if (!check_arrays(&lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray.arraystart.floatbase;
    for (n = 0; n < count; n++) lhsrce[n] -= TOFLOAT(rhsrce[n]);
    push_arraytemp(&lharray, VAR_FLOAT);
  }
  else {
    want_number();
  }
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
  if (lhitem == STACK_INT || lhitem == STACK_FLOAT) {	/* <integer>-<float array> or <float>-<float array> */
    floatvalue = lhitem == STACK_INT ? TOFLOAT(pop_int()) : pop_float();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) base[n] = floatvalue - rhsrce[n];
  }
  else if (lhitem == STACK_INTARRAY) {	/* <int array>+<float array> */
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.intbase;
    for (n = 0; n < count; n++) base[n] = TOFLOAT(lhsrce[n]) - rhsrce[n];
  }
  else if (lhitem == STACK_FLOATARRAY) {	/* <float array>-<float array> */
    float64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) base[n] = lhsrce[n] - rhsrce[n];
  }
  else if (lhitem == STACK_FATEMP) {		/* <float array>-<float array> */
    float64 *lhsrce;
    basicarray lharray = pop_arraytemp();
    if (!check_arrays(&lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray.arraystart.floatbase;
    for (n = 0; n < count; n++) lhsrce[n] -= rhsrce[n];
    push_arraytemp(&lharray, VAR_FLOAT);
  }
  else {
    want_number();
  }
}

/*
** 'eval_ivmul' handles multiplication where the right-hand operand is
** an integer
** Note that in order to catch an integer overflow, the operands have
** to be converted to floating point before they are multiplied so that
** the result can be checked to see if it is in range still. There
** should be no problem here as long as the number of bits in the
** mantissa of the floating point number exceeds the number of bits
** in an integer
*/
static void eval_ivmul(void) {
  stackitem lhitem;
  int32 rhint = pop_int();
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT) {	/* Now look at left-hand operand */
    int32 lhint = pop_int();
    if (CAST(lhint|rhint, uint32)<0x8000u) {	/* Result can be represented in thirty two bits */
      PUSH_INT(lhint*rhint);
    }
    else {	/* Result may overflow thirty two bits - Use floating point multiply */
      floatvalue = TOFLOAT(lhint)*TOFLOAT(rhint);
      if (fabs(floatvalue) <= TOFLOAT(MAXINTVAL)) {	/* If in range, convert back to integer */
        PUSH_INT(TOINT(floatvalue));
      }
      else {
        error(ERR_RANGE);	/* I'd prefer to just convert the value to floating point */
        PUSH_FLOAT(floatvalue);	/* This PUSH_FLOAT will never be executed but is what is needed for this */
      }
    }
  }
  else if (lhitem == STACK_FLOAT)
    push_float(pop_float()*TOFLOAT(rhint));
  else if (lhitem == STACK_INTARRAY || lhitem == STACK_FLOATARRAY) {	/* <array>*<integer value> */
    basicarray *lharray;
    int32 n, count;
    lharray = pop_array();
    count = lharray->arrsize;
    if (lhitem == STACK_INTARRAY) {	/* <integer array>*<integer> */
      int32 *srce, *base;
      base = make_array(VAR_INTWORD, lharray);
      srce = lharray->arraystart.intbase;
      for (n = 0; n < count; n++) {
        floatvalue = TOFLOAT(srce[n])*TOFLOAT(rhint);	/* This is going to be slow */
        if (fabs(floatvalue) <= TOFLOAT(MAXINTVAL))
          base[n] = TOINT(floatvalue);
        else {		/* Value is out of range for an integer */
          error(ERR_RANGE);
        }
      }
    }
    else {	/* <float array>*<integer> */
      float64 *srce, *base = make_array(VAR_FLOAT, lharray);
      floatvalue = TOFLOAT(rhint);
      srce = lharray->arraystart.floatbase;
      for (n = 0; n < count; n++) base[n] = srce[n] * floatvalue;
    }
  }
  else if (lhitem == STACK_FATEMP) {	/* <float array>*<integer value> */
    basicarray lharray;
    float64 *base;
    int32 n, count;
    lharray = pop_arraytemp();
    base = lharray.arraystart.floatbase;
    count = lharray.arrsize;
    floatvalue = TOFLOAT(rhint);
    for (n = 0; n < count; n++) base[n]*=floatvalue;
    push_arraytemp(&lharray, VAR_FLOAT);
  }
  else {
    want_number();
  }
}

/*
** 'eval_fvmul' handles multiplication where the right-hand operand is
** a floating point value
*/
static void eval_fvmul(void) {
  stackitem lhitem;
  floatvalue = pop_float();
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)	/* Now branch according to type of left-hand operand */
    push_float(TOFLOAT(pop_int())*floatvalue);
  else if (lhitem == STACK_FLOAT)
    push_float(pop_float()*floatvalue);
  else if (lhitem == STACK_INTARRAY || lhitem == STACK_FLOATARRAY) {	/* <array>*<float value> */
    basicarray *lharray;
    float64 *base;
    int32 n, count;
    lharray = pop_array();
    count = lharray->arrsize;
    base = make_array(VAR_FLOAT, lharray);
    if (lhitem == STACK_INTARRAY) {
      int32 *srce = lharray->arraystart.intbase;
      for (n = 0; n < count; n++) base[n] = TOFLOAT(srce[n]) * floatvalue;
    }
    else {
      float64 *srce = lharray->arraystart.floatbase;
      for (n = 0; n < count; n++) base[n] = srce[n] * floatvalue;
    }
  }
  else if (lhitem == STACK_FATEMP) {	/* <float array>*<float value> */
    basicarray lharray;
    float64 *base;
    int32 n, count;
    lharray = pop_arraytemp();
    base = lharray.arraystart.floatbase;
    count = lharray.arrsize;
    for (n = 0; n < count; n++) base[n]*=floatvalue;
    push_arraytemp(&lharray, VAR_FLOAT);
  }
  else {
    want_number();
  }
}

/*
** 'eval_iamul' handles multiplication where the right-hand operand is
** an integer array
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
  if (lhitem == STACK_INT) {	/* <integer value>*<integer array> */
    static float64 lhfloat;
    int32 *base;
    lhfloat = TOFLOAT(pop_int());
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      floatvalue = lhfloat*TOFLOAT(rhsrce[n]);
      if (fabs(floatvalue) <= TOFLOAT(MAXINTVAL))
        base[n] = TOINT(floatvalue);
      else {		/* Result is out of range for an integer */
        error(ERR_RANGE);
      }
    }
  }
  else if (lhitem == STACK_FLOAT) {	/* <float>*<int array> */
    float64 *base;
    floatvalue = pop_float();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) base[n] = floatvalue * TOFLOAT(rhsrce[n]);
  }
  else if (lhitem == STACK_INTARRAY) {	/* <int array>*<int array> */
    int32 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.intbase;
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      floatvalue = TOFLOAT(lhsrce[n])*TOFLOAT(rhsrce[n]);
      if (fabs(floatvalue) <= TOFLOAT(MAXINTVAL))
        base[n] = TOINT(floatvalue);
      else {		/* Result is out of range for an integer */
        error(ERR_RANGE);
      }
    }
  }
  else if (lhitem == STACK_FLOATARRAY) {	/* <float array>*<int array> */
    float64 *base, *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) base[n] = lhsrce[n] * TOFLOAT(rhsrce[n]);
  }
  else if (lhitem == STACK_FATEMP) {		/* <float array>*<int array> */
    float64 *lhsrce;
    basicarray lharray = pop_arraytemp();
    if (!check_arrays(&lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray.arraystart.floatbase;
    for (n = 0; n < count; n++) lhsrce[n] *= TOFLOAT(rhsrce[n]);
    push_arraytemp(&lharray, VAR_FLOAT);
  }
  else {
    want_number();
  }
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
  if (lhitem == STACK_INT || lhitem == STACK_FLOAT) {	/* <integer>*<float array> or <float>*<float array> */
    floatvalue = lhitem == STACK_INT ? TOFLOAT(pop_int()) : pop_float();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) base[n] = floatvalue * rhsrce[n];
  }
  else if (lhitem == STACK_INTARRAY) {	/* <int array>*<float array> */
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.intbase;
    for (n = 0; n < count; n++) base[n] = TOFLOAT(lhsrce[n]) * rhsrce[n];
  }
  else if (lhitem == STACK_FLOATARRAY) {	/* <float array>*<float array> */
    float64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) base[n] = lhsrce[n] * rhsrce[n];
  }
  else if (lhitem == STACK_FATEMP) {		/* <float array>*<float array> */
    float64 *lhsrce;
    basicarray lharray = pop_arraytemp();
    if (!check_arrays(&lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray.arraystart.floatbase;
    for (n = 0; n < count; n++) lhsrce[n] *= rhsrce[n];
    push_arraytemp(&lharray, VAR_FLOAT);
  }
  else {
    want_number();
  }
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
** the right-hand array is an integer array
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
** is an integer value
*/
static void eval_ivdiv(void) {
  stackitem lhitem;
  int32 rhint = pop_int();
  if (rhint == 0) error(ERR_DIVZERO);
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)
    push_float(TOFLOAT(pop_int())/TOFLOAT(rhint));
  else if (lhitem == STACK_FLOAT)
    push_float(pop_float()/TOFLOAT(rhint));
  else if (lhitem == STACK_INTARRAY || lhitem == STACK_FLOATARRAY) {	/* <array>/<integer value> */
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
    }
    else {
      float64 *srce = lharray->arraystart.floatbase;
      for (n = 0; n < count; n++) base[n] = srce[n] / floatvalue;
    }
  }
  else if (lhitem == STACK_FATEMP) {	/* <float array>/<integer value> */
    basicarray lharray;
    float64 *base;
    int32 n, count;
    lharray = pop_arraytemp();
    base = lharray.arraystart.floatbase;
    count = lharray.arrsize;
    floatvalue = TOFLOAT(rhint);
    for (n = 0; n < count; n++) base[n]/=floatvalue;
    push_arraytemp(&lharray, VAR_FLOAT);
  }
  else {
    want_number();
  }
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
  if (lhitem == STACK_INT)
    push_float(TOFLOAT(pop_int())/floatvalue);
  else if (lhitem == STACK_FLOAT)
    push_float(pop_float()/floatvalue);
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
    }
    else {
      float64 *srce = lharray->arraystart.floatbase;
      for (n = 0; n < count; n++) base[n] = srce[n]/floatvalue;
    }
  }
  else if (lhitem == STACK_FATEMP) {	/* <float array>/<float value> */
    basicarray lharray;
    float64 *base;
    int32 n, count;
    lharray = pop_arraytemp();
    base = lharray.arraystart.floatbase;
    count = lharray.arrsize;
    for (n = 0; n < count; n++) base[n]/=floatvalue;
    push_arraytemp(&lharray, VAR_FLOAT);
  }
  else {
    want_number();
  }
}

/*
** 'eval_iadiv' handles floating point division where the right-hand operand
** is an integer array
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
  if (lhitem == STACK_INT) {	/* <integer value>/<integer array> */
    float64 *base;
    floatvalue = TOFLOAT(pop_int());
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = floatvalue / TOFLOAT(rhsrce[n]);
    }
  }
  else if (lhitem == STACK_FLOAT) {	/* <float value>/<integer array> */
    floatvalue = pop_float();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = floatvalue/TOFLOAT(rhsrce[n]);
    }
  }
  else if (lhitem == STACK_INTARRAY) {	/* <int array>/<int array> */
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.intbase;
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = TOFLOAT(lhsrce[n]) / TOFLOAT(rhsrce[n]);
    }
  }
  else if (lhitem == STACK_FLOATARRAY) {	/* <float array>/<int array> */
    float64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = lhsrce[n] / TOFLOAT(rhsrce[n]);
    }
  }
  else if (lhitem == STACK_FATEMP) {		/* <float array>/<int array> */
    float64 *lhsrce;
    basicarray lharray = pop_arraytemp();
    if (!check_arrays(&lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray.arraystart.floatbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      lhsrce[n] /= TOFLOAT(rhsrce[n]);
    }
    push_arraytemp(&lharray, VAR_FLOAT);
  }
  else {
    want_number();
  }
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
  if (lhitem == STACK_INT || lhitem == STACK_FLOAT) {	/* <value>/<float array> */
    floatvalue = lhitem == STACK_INT ? TOFLOAT(pop_int()) : pop_float();
    base = make_array(VAR_FLOAT, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base[n] = floatvalue / rhsrce[n];
    }
  }
  else if (lhitem == STACK_INTARRAY) {	/* <int array>/<float array> */
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.intbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base[n] = TOFLOAT(lhsrce[n]) / rhsrce[n];
    }
  }
  else if (lhitem == STACK_FLOATARRAY) {	/* <float array>/<float array> */
    float64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_FLOAT, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base[n] = lhsrce[n] / rhsrce[n];
    }
  }
  else if (lhitem == STACK_FATEMP) {	/* <float array>/<float array> */
    float64 *lhsrce;
    basicarray lharray = pop_arraytemp();
    if (!check_arrays(&lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray.arraystart.floatbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      lhsrce[n] /= rhsrce[n];
    }
    push_arraytemp(&lharray, VAR_FLOAT);
  }
  else {
    want_number();
  }
}

/*
** 'eval_ivintdiv' handles the integer division operator when the
** right-hand operand is an integer value
*/
static void eval_ivintdiv(void) {
  stackitem lhitem;
  int32 rhint = pop_int();
  if (rhint == 0) error(ERR_DIVZERO);
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)	/* Branch according to type of left-hand operand */
    INTDIV_INT(rhint);
  else if (lhitem == STACK_FLOAT)
    push_int(TOINT(pop_float())/rhint);
  else if (lhitem == STACK_INTARRAY || lhitem == STACK_FLOATARRAY) {	/* <array> DIV <integer value> */
    basicarray *lharray;
    int32 n, count;
    lharray = pop_array();
    count = lharray->arrsize;
    if (lhitem == STACK_INTARRAY) {	/* <integer array> DIV <integer value> */
      int32 *srce, *base = make_array(VAR_INTWORD, lharray);
      srce = lharray->arraystart.intbase;
      for (n = 0; n < count; n++) base[n] = srce[n] / rhint;
    }
    else {	/* <float array> DIV <integer value> */
      float64 *srce;
      int32 *base = make_array(VAR_INTWORD, lharray);
      srce = lharray->arraystart.floatbase;
      for (n = 0; n < count; n++) base[n] = TOINT(srce[n]) / rhint;
    }
  }
  else {
    want_number();
  }
}

/*
** 'eval_fvintdiv' handles the integer division operator when the
** right-hand operand is a floating point value
*/
static void eval_fvintdiv(void) {
  stackitem lhitem;
  int32 rhint = TOINT(pop_float());
  if (rhint == 0) error(ERR_DIVZERO);
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)	/* Branch according to type of left-hand operand */
    INTDIV_INT(rhint);
  else if (lhitem == STACK_FLOAT)
    push_int(TOINT(pop_float())/rhint);
  else if (lhitem == STACK_INTARRAY || lhitem == STACK_FLOATARRAY) {	/* <array> DIV <float value> */
    basicarray *lharray;
    int32 *base, n, count;
    lharray = pop_array();
    count = lharray->arrsize;
    base = make_array(VAR_INTWORD, lharray);
    if (lhitem == STACK_INTARRAY) {	/* <integer array> DIV <float value> */
      int32 *srce = lharray->arraystart.intbase;
      for (n = 0; n < count; n++) base[n] = srce[n] / rhint;
    }
    else {	/* <float array> DIV <float value> */
      float64 *srce = lharray->arraystart.floatbase;
      for (n = 0; n < count; n++) base[n] = TOINT(srce[n]) / rhint;
    }
  }
  else {
    want_number();
  }
}

/*
** 'eval_iaintdiv' handles the integer division operator when the
** right-hand operand is an integer array
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
  if (lhitem == STACK_INT || lhitem == STACK_FLOAT) {	/* <value> DIV <integer array> */
    int32 lhint;
    lhint = lhitem == STACK_INT ? pop_int() : TOINT(pop_float());
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = lhint / rhsrce[n];
    }
  }
  else if (lhitem == STACK_INTARRAY) {	/* <integer array> DIV <integer array> */
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.intbase;
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = lhsrce[n] / rhsrce[n];
    }
  }
  else if (lhitem == STACK_FLOATARRAY) {	/* <float array> DIV <integer array> */
    float64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_INTWORD, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = TOINT(lhsrce[n]) / rhsrce[n];
    }
  }
  else {
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
  float64 *rhsrce;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.floatbase;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT || lhitem == STACK_FLOAT) {	/* <value> DIV <float array> */
    int32 lhint;
    lhint = lhitem == STACK_INT ? pop_int() : TOINT(pop_float());
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base[n] = lhint / TOINT(rhsrce[n]);
    }
  }
  else if (lhitem == STACK_INTARRAY) {	/* <int array> DIV <float array> */
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_INTWORD, rharray);
    lhsrce = lharray->arraystart.intbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base[n] = lhsrce[n] / TOINT(rhsrce[n]);
    }
  }
  else if (lhitem == STACK_FLOATARRAY) {	/* <float array> DIV <float array> */
    float64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_INTWORD, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base[n] = TOINT(lhsrce[n]) / TOINT(rhsrce[n]);
    }
  }
  else {
    want_number();
  }
}

/*
** 'eval_ivmod' carries out the integer remainder operator when the right-hand
** operand is an integer value
*/
static void eval_ivmod(void) {
  stackitem lhitem;
  int32 rhint = pop_int();
  if (rhint == 0) error(ERR_DIVZERO);
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)	/* Branch according to type of left-hand operand */
    INTMOD_INT(rhint);
  else if (lhitem == STACK_FLOAT)
    push_int(TOINT(pop_float()) % rhint);
  else if (lhitem == STACK_INTARRAY || lhitem == STACK_FLOATARRAY) {	/* <array> MOD <integer value> */
    basicarray *lharray;
    int32 n, count;
    lharray = pop_array();
    count = lharray->arrsize;
    if (lhitem == STACK_INTARRAY) {	/* <integer array> MOD <integer value> */
      int32 *srce, *base = make_array(VAR_INTWORD, lharray);
      srce = lharray->arraystart.intbase;
      for (n = 0; n < count; n++) base[n] = srce[n] % rhint;
    }
    else {	/* <float array> MOD <integer value> */
      float64 *srce;
      int32 *base = make_array(VAR_INTWORD, lharray);
      srce = lharray->arraystart.floatbase;
      for (n = 0; n < count; n++) base[n] = TOINT(srce[n]) % rhint;
    }
  }
  else {
    want_number();
  }
}

/*
** 'eval_fvmod' carries out the integer remainder operator when the right-hand
** operand is a floating point value
*/
static void eval_fvmod(void) {
  stackitem lhitem;
  int32 rhint = TOINT(pop_float());
  if (rhint == 0) error(ERR_DIVZERO);
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)	/* Branch according to type of left-hand operand */
    INTMOD_INT(rhint);
  else if (lhitem == STACK_FLOAT)
    push_int(TOINT(pop_float()) % rhint);
  else if (lhitem == STACK_INTARRAY || lhitem == STACK_FLOATARRAY) {	/* <array> MOD <float value> */
    basicarray *lharray;
    int32 *base;
    int32 n, count;
    lharray = pop_array();
    count = lharray->arrsize;
    base = make_array(VAR_INTWORD, lharray);
    if (lhitem == STACK_INTARRAY) {	/* <integer array> MOD <float value> */
      int32 *srce = lharray->arraystart.intbase;
      for (n = 0; n < count; n++) base[n] = srce[n] % rhint;
    }
    else {	/* <float array> DIV <float value> */
      float64 *srce = lharray->arraystart.floatbase;
      for (n = 0; n < count; n++) base[n] = TOINT(srce[n]) % rhint;
    }
  }
  else {
    want_number();
  }
}

/*
** 'eval_iamod' carries out the integer remainder operator when the right-hand
** operand is an integer array
*/
static void eval_iamod(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 n, count;
  int32 *base, *rhsrce;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.intbase;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT || lhitem == STACK_FLOAT) {	/* <value> MOD <integer array> */
    int32 lhint;
    lhint = lhitem == STACK_INT ? pop_int() : TOINT(pop_float());
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = lhint % rhsrce[n];
    }
  }
  else if (lhitem == STACK_INTARRAY) {	/* <integer array> MOD <integer array> */
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    lhsrce = lharray->arraystart.intbase;
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = lhsrce[n] % rhsrce[n];
    }
  }
  else if (lhitem == STACK_FLOATARRAY) {	/* <float array> MOD <integer array> */
    float64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_INTWORD, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0) error(ERR_DIVZERO);
      base[n] = TOINT(lhsrce[n]) % rhsrce[n];
    }
  }
  else {
    want_number();
  }
}

/*
** 'eval_famod' carries out the integer remainder operator when the right-hand
** operand is a floating point array
*/
static void eval_famod(void) {
  stackitem lhitem;
  basicarray *rharray;
  int32 *base, n, count;
  float64 *rhsrce;
  rharray = pop_array();
  count = rharray->arrsize;
  rhsrce = rharray->arraystart.floatbase;
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT || lhitem == STACK_FLOAT) {	/* <value> MOD <float array> */
    int32 lhint;
    lhint = lhitem == STACK_INT ? pop_int() : TOINT(pop_float());
    base = make_array(VAR_INTWORD, rharray);
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base[n] = lhint % TOINT(rhsrce[n]);
    }
  }
  else if (lhitem == STACK_INTARRAY) {	/* <int array> MOD <float array> */
    int32 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_INTWORD, rharray);
    lhsrce = lharray->arraystart.intbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base[n] = lhsrce[n] % TOINT(rhsrce[n]);
    }
  }
  else if (lhitem == STACK_FLOATARRAY) {	/* <float array> MOD <float array> */
    float64 *lhsrce;
    basicarray *lharray = pop_array();
    if (!check_arrays(lharray, rharray)) error(ERR_TYPEARRAY);
    base = make_array(VAR_INTWORD, rharray);
    lhsrce = lharray->arraystart.floatbase;
    for (n = 0; n < count; n++) {
      if (rhsrce[n] == 0.0) error(ERR_DIVZERO);
      base[n] = TOINT(lhsrce[n]) % TOINT(rhsrce[n]);
    }
  }
  else {
    want_number();
  }
}

/*
** 'eval_ivpow' deals with the 'raise' operator when the right-hand operand is
** an integer
*/
static void eval_ivpow(void) {
  stackitem lhitem;
  floatvalue = TOFLOAT(pop_int());
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)
     push_float(pow(TOFLOAT(pop_int()), floatvalue));
  else if (lhitem == STACK_FLOAT)
    push_float(pow(pop_float(), floatvalue));
  else {
    want_number();
  }
}

/*
** 'eval_fvpow' deals with the 'raise' operator when the right-hand operand is
** a floating point value
*/
static void eval_fvpow(void) {
  stackitem lhitem;
  floatvalue = pop_float();
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)	/* Branch according to type of left-hand operand */
    push_float(pow(TOFLOAT(pop_int()), floatvalue));
  else if (lhitem == STACK_FLOAT)
    push_float(pow(pop_float(), floatvalue));
  else {
    want_number();
  }
}

/*
** 'eval_ivlsl' deals with the logical left shift operator when the right-hand
** operand is an integer value
*/
static void eval_ivlsl(void) {
  stackitem lhitem;
  int32 rhint = pop_int();
  lhitem = GET_TOPITEM;	/* Branch according to type of left-hand operand */
  if (lhitem == STACK_INT)
    LSL_INT(rhint);
  else if (lhitem == STACK_FLOAT)
    push_int(TOINT(pop_float()) << rhint);
  else {
    want_number();
  }
}

/*
** 'eval_fvlsl' deals with the logical left shift operator when the right-hand
** operand is a floating point value
*/
static void eval_fvlsl(void) {
  stackitem lhitem;
  int32 rhint = TOINT(pop_float());
  lhitem = GET_TOPITEM;	/* Branch according to type of left-hand operand */
  if (lhitem == STACK_INT)
    LSL_INT(rhint);
  else if (lhitem == STACK_FLOAT)
    push_int(TOINT(pop_float()) << rhint);
  else {
    want_number();
  }
}

/*
** 'eval_ivlsr' handles the logical right shift operator when the right-hand
** operand is an integer value.
** It should be noted that this code assumes that using unsigned operands
** will result in the C compiler generating a logical shift. This is not
** guaranteed: any C compiler is free to generate an arithmetic shift if
** it so desires.
*/
static void eval_ivlsr(void) {
  stackitem lhitem;
  uint32 lhuint, rhuint;
  rhuint = pop_int();
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT) {	/* Branch according to type of left-hand operand */
    lhuint = pop_int();
    push_int(lhuint >> rhuint);
  }
  else if (lhitem == STACK_FLOAT) {
    lhuint = TOINT(pop_float());
    push_int(lhuint >> rhuint);
  }
  else {
    want_number();
  }
}

/*
** 'eval_fvlsr' handles the logical right shift operator when the right-hand
** operand is a floating point value.
*/
static void eval_fvlsr(void) {
  stackitem lhitem;
  uint32 lhuint, rhuint;
  rhuint = TOINT(pop_float());
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT) {	/* Branch according to type of left-hand operand */
    lhuint = pop_int();
    push_int(lhuint >> rhuint);
  }
  else if (lhitem == STACK_FLOAT) {
    lhuint = TOINT(pop_float());
    push_int(lhuint >> rhuint);
  }
  else {
    want_number();
  }
}

/*
** 'eval_ivasr' deals with arithmetic right shifts when the right-hand operand
** is an integer value
*/
static void eval_ivasr(void) {
  stackitem lhitem;
  int32 rhint = pop_int();
  lhitem = GET_TOPITEM;	/* Branch according to type of left-hand operand */
  if (lhitem == STACK_INT)
    ASR_INT(rhint);
  else if (lhitem == STACK_FLOAT)
    push_int(TOINT(pop_float()) >> rhint);
  else {
    want_number();
  }
}

/*
** 'eval_fvasr' deals with arithmetic right shifts when the right-hand operand
** is a floating point value
*/
static void eval_fvasr(void) {
  stackitem lhitem;
  int32 rhint = TOINT(pop_float());
  lhitem = GET_TOPITEM;	/* Branch according to type of left-hand operand */
  if (lhitem == STACK_INT)
    ASR_INT(rhint);
  else if (lhitem == STACK_FLOAT)
    push_int(TOINT(pop_float()) >> rhint);
  else {
    want_number();
  }
}

/*
** 'eval_iveq' deals with the 'equals' operator when the right-hand operand
** is an integer value. It pushes either 'TRUE' or 'FALSE' on to the Basic
** stack depending on whether the two operands are equal or not.
*/
static void eval_iveq(void) {
  stackitem lhitem;
  int32 result, rhint = pop_int();
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)
    CPEQ_INT(rhint);
  else if (lhitem == STACK_FLOAT) {
    result = pop_float() == TOFLOAT(rhint) ? BASTRUE : BASFALSE;	/* Due to the macros used... */
    PUSH_INT(result);			/* ...these two lines cannot be combined */
  }
  else {
    want_number();
  }
}

/*
** 'eval_fveq' deals with the 'equals' operator when the right-hand operand
** is a floating point value. It pushes either 'TRUE' or 'FALSE' on to the
** Basic stack depending on whether the two operands are equal or not.
*/
static void eval_fveq(void) {
  stackitem lhitem;
  int32 result;
  floatvalue = pop_float();
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT) {	/* Now branch according to type of left-hand operand */
    result = TOFLOAT(pop_int()) == floatvalue ? BASTRUE : BASFALSE;
    PUSH_INT(result);
  }
  else if (lhitem == STACK_FLOAT) {
    result = pop_float() == floatvalue ? BASTRUE : BASFALSE;
    PUSH_INT(result);
  }
  else {
    want_number();
  }
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
** operand is an integer value
*/
static void eval_ivne(void) {
  stackitem lhitem;
  int32 rhint = pop_int();
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)
    CPNE_INT(rhint);
  else if (lhitem == STACK_FLOAT)
    push_int(pop_float() != TOFLOAT(rhint) ? BASTRUE : BASFALSE);
  else {
    want_number();
  }
}

/*
** 'eval_fvne' deals with the 'not equals' operator when the right-hand
** operand is a floating point value
*/
static void eval_fvne(void) {
  stackitem lhitem;
  floatvalue = pop_float();
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)	/* Now branch according to type of left-hand operand */
    push_int(TOFLOAT(pop_int()) != floatvalue ? BASTRUE : BASFALSE);
  else if (lhitem == STACK_FLOAT)
    push_int(pop_float() != floatvalue ? BASTRUE : BASFALSE);
  else {
    want_number();
  }
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
** operand is an integer value
*/
static void eval_ivgt(void) {
  stackitem lhitem;
  int32 rhint = pop_int();
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)
    CPGT_INT(rhint);
  else if (lhitem == STACK_FLOAT)
    push_int(pop_float()>TOFLOAT(rhint) ? BASTRUE : BASFALSE);
  else {
    want_number();
  }
}

/*
** 'eval_fvgt' deals with the 'greater than' operator when the right-hand
** operand is a floating point value
*/
static void eval_fvgt(void) {
  stackitem lhitem;
  floatvalue = pop_float();
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)	/* Now branch according to type of left-hand operand */
    push_int(TOFLOAT(pop_int()) > floatvalue ? BASTRUE : BASFALSE);
  else if (lhitem == STACK_FLOAT)
    push_int(pop_float() > floatvalue ? BASTRUE : BASFALSE);
  else {
    want_number();
  }
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
** operand is an integer
*/
static void eval_ivlt(void) {
  stackitem lhitem;
  int32 rhint = pop_int();
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)
    CPLT_INT(rhint);
  else if (lhitem == STACK_FLOAT)
    push_int(pop_float() < TOFLOAT(rhint) ? BASTRUE : BASFALSE);
  else {
    want_number();
  }
}

/*
** 'eval_fvlt' handles the 'less than' operator when the right-hand
** operand is a floating point value
*/
static void eval_fvlt(void) {
  stackitem lhitem;
  floatvalue = pop_float();
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)	/* Now branch according to type of left-hand operand */
    push_int(TOFLOAT(pop_int()) < floatvalue ? BASTRUE : BASFALSE);
  else if (lhitem == STACK_FLOAT)
    push_int(pop_float() < floatvalue ? BASTRUE : BASFALSE);
  else {
    want_number();
  }
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
** right-hand operand is an integer value
*/
static void eval_ivge(void) {
  stackitem lhitem;
  int32 rhint = pop_int();
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)
    CPGE_INT(rhint);
  else if (lhitem == STACK_FLOAT)
    push_int(pop_float() >= TOFLOAT(rhint) ? BASTRUE : BASFALSE);
  else {
    want_number();
  }
}

/*
** 'eval_fvge' handles the 'greater than or equal to' operator when the
** right-hand operand is a floating point value
*/
static void eval_fvge(void) {
  stackitem lhitem;
  floatvalue = pop_float();
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)	/* Now branch according to type of left-hand operand */
    push_int(TOFLOAT(pop_int()) >= floatvalue ? BASTRUE : BASFALSE);
  else if (lhitem == STACK_FLOAT)
    push_int(pop_float() >= floatvalue ? BASTRUE : BASFALSE);
  else {
    want_number();
  }
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
** right-hand operand is an integer value
*/
static void eval_ivle(void) {
  stackitem lhitem;
  int32 rhint = pop_int();
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)
    CPLE_INT(rhint);
  else if (lhitem == STACK_FLOAT)
    push_int(pop_float() <= TOFLOAT(rhint) ? BASTRUE : BASFALSE);
  else {
    want_number();
  }
}

/*
** 'eval_fvle' deals with the 'less than or equal to' operator when the
** right-hand operand is a floating point value
*/
static void eval_fvle(void) {
  stackitem lhitem;
  floatvalue = pop_float();
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)	/* Now branch according to type of left-hand operand */
    push_int(TOFLOAT(pop_int()) <= floatvalue ? BASTRUE : BASFALSE);
  else if (lhitem == STACK_FLOAT)
    push_int(pop_float() <= floatvalue ? BASTRUE : BASFALSE);
  else {
    want_number();
  }
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
** operand is an integer value
*/
static void eval_ivand(void) {
  stackitem lhitem;
  int32 rhint = pop_int();
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)	/* Branch according to type of left-hand operand */
    AND_INT(rhint);
  else if (lhitem == STACK_FLOAT)
    push_int(TOINT(pop_float()) & rhint);
  else {
    want_number();
  }
}

/*
** 'eval_fvand' deals with the logical 'and' operator when the right-hand
** operand is a floatin point value
*/
static void eval_fvand(void) {
  stackitem lhitem;
  int32 rhint = TOINT(pop_float());
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)
    AND_INT(rhint);
  else if (lhitem == STACK_FLOAT)
    push_int(TOINT(pop_float()) & rhint);
  else {
    want_number();
  }
}

/*
** 'eval_ivand' deals with the logical 'or' operator when the right-hand
** operand is an integer value
*/
static void eval_ivor(void) {
  stackitem lhitem;
  int32 rhint = pop_int();
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)	/* Branch according to type of left-hand operand */
    OR_INT(rhint);
  else if (lhitem == STACK_FLOAT)
    push_int(TOINT(pop_float()) | rhint);
  else {
    want_number();
  }
}

/*
** 'eval_fvand' deals with the logical 'or' operator when the right-hand
** operand is a floating point value
*/
static void eval_fvor(void) {
  stackitem lhitem;
  int32 rhint = TOINT(pop_float());
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)
    OR_INT(rhint);
  else if (lhitem == STACK_FLOAT)
    push_int(TOINT(pop_float()) | rhint);
  else {
    want_number();
  }
}

/*
** 'eval_iveor' deals with the exclusive or operator when right-hand
** operand is an integer value
*/
static void eval_iveor(void) {
  stackitem lhitem;
  int32 rhint = pop_int();
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)	/* Branch according to type of left-hand operand */
    EOR_INT(rhint);
  else if (lhitem == STACK_FLOAT)
    push_int(TOINT(pop_float()) ^ rhint);
  else {
    want_number();
  }
}

/*
** 'eval_fveor' deals with the exclusive or operator when right-hand
** operand is a floating point value
*/
static void eval_fveor(void) {
  stackitem lhitem;
  int32 rhint = TOINT(pop_float());
  lhitem = GET_TOPITEM;
  if (lhitem == STACK_INT)
    EOR_INT(rhint);
  else if (lhitem == STACK_FLOAT)
    push_int(TOINT(pop_float()) ^ rhint);
  else {
    want_number();
  }
}

/*
** 'factor_table' is a table of functions indexed by token type used
** to deal with factors in an expression.
** Note that there are a number of entries in here for functions
** as the keywords can be used as both statement types and functions
*/
void (*factor_table[256])(void) = {
  bad_syntax, do_xvar, do_staticvar, do_intvar,			/* 00..03 */
  do_floatvar, do_stringvar, do_arrayvar, do_arrayref,		/* 04..07 */
  do_arrayref, do_indrefvar, do_indrefvar, do_statindvar,	/* 08..0B */
  do_xfunction, do_function, bad_token, bad_token,		/* 0C..0F */
  do_intzero, do_intone, do_smallconst, do_intconst, 		/* 10..13 */
  do_floatzero, do_floatone, do_floatconst, do_stringcon,	/* 14..17 */
  do_qstringcon, bad_token, bad_token, bad_token,		/* 18..1B */
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
  bad_syntax, fn_beat, bad_syntax, bad_syntax,			/* 8C..8F */
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
static void (*opfunctions [21][12])(void) = {
/* Dummy */
 {eval_badcall, eval_badcall, eval_badcall, eval_badcall,
  eval_badcall, eval_badcall, eval_badcall, eval_badcall,
  eval_badcall, eval_badcall, eval_badcall, eval_badcall},
/* Addition */
 {eval_badcall, eval_badcall, eval_ivplus,  eval_fvplus,
  eval_svplus,  eval_svplus,  eval_iaplus,  eval_iaplus,
  eval_faplus,  eval_faplus,  eval_saplus,  eval_saplus},
/* Subtraction */
 {eval_badcall, eval_badcall, eval_ivminus, eval_fvminus,
  want_number,  want_number,  eval_iaminus, eval_iaminus,
  eval_faminus, eval_faminus, want_number,  want_number},
/* Multiplication */
 {eval_badcall, eval_badcall, eval_ivmul,   eval_fvmul,
  want_number,  want_number,  eval_iamul,   eval_iamul,
  eval_famul,   eval_famul,   want_number,  want_number},
/* Matrix multiplication */
 {want_array,   eval_badcall, want_array,   want_array,
  want_array,   want_array,   eval_immul,   want_array,
  eval_fmmul,   want_array,   want_array,   want_array},
/* Division */
 {eval_badcall, eval_badcall, eval_ivdiv,   eval_fvdiv,
  want_number,  want_number,  eval_iadiv,   eval_iadiv,
  eval_fadiv,   eval_fadiv,   want_number,  want_number},
/* Integer division */
 {eval_badcall, eval_badcall, eval_ivintdiv, eval_fvintdiv,
  want_number,  want_number,  eval_iaintdiv, eval_iaintdiv,
  eval_faintdiv, eval_faintdiv, want_number, want_number},
/* Integer remainder */
 {eval_badcall, eval_badcall, eval_ivmod,   eval_fvmod,
  want_number,  want_number,  eval_iamod,   eval_iamod,
  eval_famod,   eval_famod,   want_number,  want_number},
/* Raise */
 {eval_badcall, eval_badcall, eval_ivpow,   eval_fvpow,
  want_number,  want_number,  want_number,  want_number,
  want_number,  want_number,  want_number,  want_number},
/* Logical left shift */
 {eval_badcall, eval_badcall, eval_ivlsl,   eval_fvlsl,
  want_number,  want_number,  want_number,  want_number,
  want_number,  want_number,  want_number,  want_number},
/* Logical right shift */
 {eval_badcall, eval_badcall, eval_ivlsr,   eval_fvlsr,
  want_number,  want_number,  want_number,  want_number,
  want_number,  want_number,  want_number,  want_number},
/* Arithmetic right shift */
 {eval_badcall, eval_badcall, eval_ivasr,   eval_fvasr,
  want_number,  want_number,  want_number,  want_number,
  want_number,  want_number,  want_number,  want_number},
/* Equals */
 {eval_badcall, eval_badcall, eval_iveq,    eval_fveq,
  eval_sveq,    eval_sveq,    want_number,  want_number,
  want_number,  want_number,  want_number,  want_number},
/* Not equals */
 {eval_badcall, eval_badcall, eval_ivne,    eval_fvne,
  eval_svne,    eval_svne,    want_number,  want_number,
  want_number,  want_number,  want_number,  want_number},
/* Greater than */
 {eval_badcall, eval_badcall, eval_ivgt,    eval_fvgt,
  eval_svgt,    eval_svgt,    want_number,  want_number,
  want_number,  want_number,  want_number,  want_number},
/* Less than */
 {eval_badcall, eval_badcall, eval_ivlt,    eval_fvlt,
  eval_svlt,    eval_svlt,    want_number,  want_number,
  want_number,  want_number,  want_number,  want_number},
/* Greater than or equal to */
 {eval_badcall, eval_badcall, eval_ivge,    eval_fvge,
  eval_svge,    eval_svge,    want_number,  want_number,
  want_number,  want_number,  want_number,  want_number},
/* Less than or equal to */
 {eval_badcall, eval_badcall, eval_ivle,    eval_fvle,
  eval_svle,    eval_svle,    want_number,  want_number,
  want_number,  want_number,  want_number,  want_number},
/* Logical and */
 {eval_badcall, eval_badcall, eval_ivand,   eval_fvand,
  want_number,  want_number,  want_number,  want_number,
  want_number,  want_number,  want_number,  want_number},
/* Logical or */
 {eval_badcall, eval_badcall, eval_ivor,    eval_fvor,
  want_number,  want_number,  want_number,  want_number,
  want_number,  want_number,  want_number,  want_number},
/* Logical exclusive or */
 {eval_badcall, eval_badcall, eval_iveor,   eval_fveor,
  want_number,  want_number,  want_number,  want_number,
  want_number,  want_number,  want_number,  want_number},
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
  (*factor_table[*basicvars.current])();	/* Get first factor in the expression */
  lastop = optable[*basicvars.current];
  if (lastop == 0) return;	/* Quick way out if there is nothing to do */
  basicvars.current++;		/* Skip operator (always one character) */
  (*factor_table[*basicvars.current])();	/* Get second operand */
  thisop = optable[*basicvars.current];
  if (thisop == 0) {
/* Have got a simple '<value> <op> <value>' type of expression */
    (*opfunctions[lastop & OPERMASK][GET_TOPITEM])();
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
