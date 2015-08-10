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
**	This file contains functions for dealing with lvalues
*/

#include <string.h>
#include "common.h"
#include "target.h"
#include "basicdefs.h"
#include "tokens.h"
#include "evaluate.h"
#include "stack.h"
#include "errors.h"
#include "variables.h"
#include "miscprocs.h"
#include "lvalue.h"

/*
** The functions in this file are concerned with returning lvalues for
** variables, that is, a structure that gives the type of a variable
** and the address at which its value is stored.

** Note the the contents of 'lvalue': 'address' contains the address from
** which to retrieve or at which to store data. 'typeinfo' says what it
** refers to. If the type is set to 'VAR_xxxPTR' then the code that uses
** the address cannot make any assumptions as to whether the address is
** aligned on a word boundary or anything. This is of vital importance on
** the ARM where word addresses have to be aligned. On other processors it
** can be ignored. If the address is not of the 'xxxPTR' variety, the
** value of address will be properly aligned for the type of data and
** processor
*/

static void (*lvalue_table[256])(lvalue *);	/* Forward reference */

/*
** 'bad_token' is called when a bad token is found when trying
** to identify what kind of lvalue is being dealt with. This
** normally indicates a bug in the interpreter
*/
static void bad_token(lvalue *destination) {
  error(ERR_BROKEN, __LINE__, "lvalue");
}

/*
** 'bad_syntax' is called when the wrong sort of token is found
** when trying to identify the type of lvalue being dealt with
*/
static void bad_syntax(lvalue *destination) {
  error(ERR_SYNTAX);
}

/*
** 'fix_address' is called the first time a variable is seen by 'get_lvalue'
** to locate the variable, decide its type and fill in its address.
** In the case of missing variables, the function will create the variable
** and return details of it. Arrays are a bit more complicated: normally
** it is an error to find an array that has not been declared. However,
** there are times (when dealing with 'LOCAL', 'DEF PROC' and 'DEF FN'
** statements) where the function has to create the array if the
** reference is to the whole array, that is, is of the form 'array()'.
** The flag 'basicvars.runflags.make_array' says what it should do
*/
static void fix_address(lvalue *destination) {
  variable *vp;
  byte *base, *tp, *np;
  boolean isarray = 0;
  base = get_srcaddr(basicvars.current);	/* Point 'base' at start of variable name */
  tp = skip_name(base);		/* Find to end of name */
  np = basicvars.current+1+LOFFSIZE;	/* Point at token after the XVAR token */
  vp = find_variable(base, tp-base);
  if (vp==NIL) {	/* Unknown variable or array */
    if (*(tp-1)=='(' || *(tp-1)=='[') {	/* Missing array */
      if (basicvars.runflags.make_array && *np==')')	/* Can create array */
        vp = create_variable(base, tp-base, NIL);
      else {
        error(ERR_ARRAYMISS, tocstring(CAST(base, char *), tp-base));	/* Cannot create array - Flag error */
      }
    }
    else {	/* Missing variable - Create it */
      vp = create_variable(base, tp-base, NIL);
    }
  }
  else {	/* Known variable */
    isarray = (vp->varflags & VAR_ARRAY)!=0;
/* Note that make_array is being used here to check if the array reference */
/* is in a LOCAL, DEF PROC or DEF FN statement as it is legal for there to */
/* be a null pointer to the array descriptor in these contexts */
    if (isarray && !basicvars.runflags.make_array &&
     vp->varentry.vararray==NIL) error(ERR_NODIMS, vp->varname);	/* Array not dimensioned */
  }
/*
** Update the token that gives the variable's type and store a pointer
** to either its value (if a simple variable) or its symbol table entry
** (if an array or followed by an indirection operator)
*/
  if (!isarray && (*np=='?' || *np=='!')) {	/* Variable is followed by an indirection operator */
    switch (vp->varflags) {
    case VAR_INTWORD:		/* Op follows an integer variable */
      *basicvars.current = TOKEN_INTINDVAR;
      set_address(basicvars.current, &vp->varentry.varinteger);
      break;
    case VAR_FLOAT:		/* Op follows a floating point variable */
      *basicvars.current = TOKEN_FLOATINDVAR;
      set_address(basicvars.current, &vp->varentry.varfloat);
      break;
    default:
      error(ERR_VARNUM);	/* Need a numeric variable before the operator */
    }
  }
  else {	/* Simple variable reference or any type of array reference */
    switch (vp->varflags) {
    case VAR_INTWORD:		/* Simple reference to integer variable */
      *basicvars.current = TOKEN_INTVAR;
      set_address(basicvars.current, &vp->varentry.varinteger);
    break;
    case VAR_FLOAT:		/* Simple reference to floating point variable */
      *basicvars.current = TOKEN_FLOATVAR;
      set_address(basicvars.current, &vp->varentry.varfloat);
      break;
    case VAR_STRINGDOL:	/* Simple reference to string variable */
      *basicvars.current = TOKEN_STRINGVAR;
      set_address(basicvars.current, &vp->varentry.varstring);
      break;
    default:			/* Array or array reference with indirection operator */
      if (*np==')')		/* Reference to an entire array */
        *basicvars.current = TOKEN_ARRAYVAR;
      else {	/* Reference to array element */
        *basicvars.current = TOKEN_ARRAYREF;
      }
      set_address(basicvars.current, vp);
    }
  }
  (*lvalue_table[*basicvars.current])(destination);
}

/*
** 'do_staticvar' returns an lvalue structure for a static
** variable. On entry, basicvars.current points at the 'static
** variable' token. basicvars.current is updated to point at the
** token after the variable
*/
static void do_staticvar(lvalue *destination) {
  destination->typeinfo = VAR_INTWORD;
  destination->address.intaddr = &basicvars.staticvars[*(basicvars.current+1)].varentry.varinteger;
  basicvars.current+=2;
}

/*
** 'do_intvar' fills in the lvalue structure for a simple reference to
** an integer variable
*/
static void do_intvar(lvalue *destination) {
  destination->typeinfo = VAR_INTWORD;
  destination->address.intaddr = GET_ADDRESS(basicvars.current, int32 *);
  basicvars.current+=LOFFSIZE+1;	/* Point at byte after variable */
}

/*
** 'do_floatvar' fills in the lvalue structure for a simple reference
** to a floating point variable
*/
static void do_floatvar(lvalue *destination) {
  destination->typeinfo = VAR_FLOAT;
  destination->address.floataddr = GET_ADDRESS(basicvars.current, float64 *);
  basicvars.current+=LOFFSIZE+1;	/* Point at byte after variable */
}

/*
** 'do_stringvar' fills in the lvalue structure for a simple reference
** to a string variable
*/
static void do_stringvar(lvalue *destination) {
  destination->typeinfo = VAR_STRINGDOL;
  destination->address.straddr = GET_ADDRESS(basicvars.current, basicstring *);
  basicvars.current+=LOFFSIZE+1;		/* Point at byte after variable */
}

/*
** 'do_arrayvar' fills in the lvalue structure for a reference to an
** entire array
*/
static void do_arrayvar(lvalue *destination) {
  variable *vp;
  vp = GET_ADDRESS(basicvars.current, variable *);
  basicvars.current+=LOFFSIZE+2;		/* Skip pointer to array and ')' */
  destination->typeinfo = vp->varflags;
  destination->address.arrayaddr = &vp->varentry.vararray;
}

/*
** 'do_elementvar' fills in the lvalue structure for a reference to
** an element of an array. This can be followed by an indirection
** operator
*/
static void do_elementvar(lvalue *destination) {
  variable *vp;
  int32 vartype, offset = 0, element = 0;
  basicarray *descriptor;
  vp = GET_ADDRESS(basicvars.current, variable *);
  basicvars.current+=LOFFSIZE+1;		/* Skip the pointer to the array's address */
  vartype = vp->varflags;
  descriptor = vp->varentry.vararray;
  if (descriptor->dimcount==1) {	/* Shortcut for single dimension arrays */
    expression();	/* Evaluate the array index */
    if (GET_TOPITEM==STACK_INT)
      element = pop_int();
    else if (GET_TOPITEM==STACK_FLOAT)
      element = TOINT(pop_float());
    else {
      error(ERR_TYPENUM);
    }
    if (element<0 || element>=descriptor->dimsize[0]) error(ERR_BADINDEX, element, vp->varname);
  }
  else {
    int32 index = 0, maxdims = descriptor->dimcount, dimcount = 0;
    element = 0;
    do {	/* Gather the array indexes */
      expression();	/* Evaluate an array index */
      if (GET_TOPITEM==STACK_INT)
        index = pop_int();
      else if (GET_TOPITEM==STACK_FLOAT)
        index = TOINT(pop_float());
      else {
        error(ERR_TYPENUM);
      }
      if (index<0 || index>=descriptor->dimsize[dimcount]) error(ERR_BADINDEX, index, vp->varname);
      element+=index;
      dimcount++;
      if (*basicvars.current!=',') break;	/* Escape from loop if no further indexes are expected */
      basicvars.current++;
      if (dimcount>maxdims) error(ERR_INDEXCO, vp->varname);	/* Too many dimensions */
      if (dimcount!=maxdims) element = element*descriptor->dimsize[dimcount];
    } while (TRUE);
    if (dimcount!=maxdims) error(ERR_INDEXCO, vp->varname);	/* Not enough dimensions */
  }
  if (*basicvars.current!=')') error(ERR_RPMISS);
  basicvars.current++;	/* Step past the ')' */
  destination->typeinfo = vartype = vartype-VAR_ARRAY;	/* Clear the 'array' bit */
  if (*basicvars.current!='?' && *basicvars.current!='!') {
/* There is nothing after the array ref - Finish off and return home */
/* Calculate the address of the required element */
    if (vartype==VAR_INTWORD)	/* Integer array */
      destination->address.intaddr = descriptor->arraystart.intbase+element;
    else if (vartype==VAR_FLOAT)	/* Floating point array */
      destination->address.floataddr = descriptor->arraystart.floatbase+element;
    else {	/* String array */
      destination->address.straddr = descriptor->arraystart.stringbase+element;
    }
    return;
  }
/*
** The array reference is followed by an indirection operator.
** Fetch the value of the array element. This will provide the value
** for the left-hand side of the operator
*/
  if (vartype==VAR_INTWORD)	/* Integer array */
    offset = descriptor->arraystart.intbase[element];
  else if (vartype==VAR_FLOAT)	/* Floating point array */
    offset = TOINT(descriptor->arraystart.floatbase[element]);
  else {	/* Must use a numeric array with an indirection operator */
    error(ERR_VARNUM);
  }
/* Now deal with the indirection operator */
  if (*basicvars.current=='?')		/* Result of operator is a single byte integer */
    destination->typeinfo = VAR_INTBYTEPTR;
  else {				/* Result of operator is a four byte integer */
    destination->typeinfo = VAR_INTWORDPTR;
  }
  basicvars.current++;	/* Skip the operator */
  factor();		/* Evaluate the RH operand */
  if (GET_TOPITEM==STACK_INT)
    destination->address.offset = offset+pop_int();
  else if (GET_TOPITEM==STACK_FLOAT)
    destination->address.offset = offset+TOINT(pop_float());
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'do_intindvar' fills in the lvalue structure for the case
** of an iteger variable followed by an indirection operator
*/
static void do_intindvar(lvalue *destination) {
  int32 *ip;
  ip = GET_ADDRESS(basicvars.current, int32 *);
  basicvars.current+=LOFFSIZE+1;
  if (*basicvars.current=='?')		/* Decide on the type of the result from the operator */
    destination->typeinfo = VAR_INTBYTEPTR;
  else {	/* Four byte integer */
    destination->typeinfo = VAR_INTWORDPTR;
  }
  basicvars.current++;	/* Skip the operator */
  factor();		/* Evaluate the RH operand */
  if (GET_TOPITEM==STACK_INT)
    destination->address.offset = *ip+pop_int();
  else if (GET_TOPITEM==STACK_FLOAT)
    destination->address.offset = *ip+TOINT(pop_float());
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'do_floatindvar' fills in the lvalue structure for the case
** of a floating point variable followed by an indirection operator
*/
static void do_floatindvar(lvalue *destination) {
  float64 *fp;
  fp = GET_ADDRESS(basicvars.current, float64 *);
  basicvars.current+=LOFFSIZE+1;
  if (*basicvars.current=='?')		/* Decide on the type of the result from the operator */
    destination->typeinfo = VAR_INTBYTEPTR;
  else {	/* Four byte integer */
    destination->typeinfo = VAR_INTWORDPTR;
  }
  basicvars.current++;	/* Skip the operator */
  factor();		/* Evaluate the RH operand */
  if (GET_TOPITEM==STACK_INT)
    destination->address.offset = TOINT(*fp)+pop_int();
  else if (GET_TOPITEM==STACK_FLOAT)
    destination->address.offset = TOINT(*fp)+TOINT(pop_float());
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'do_statindvar' fills in the lvalue structure for the case
** of a static integer variable followed by an indirection operator
*/
static void do_statindvar(lvalue *destination) {
  byte index;
  index = *(basicvars.current+1);	/* Get static variable's index */
  basicvars.current+=2;			/* Skip the variable */
  if (*basicvars.current=='?')		/* Decide on the type of the result from the operator */
    destination->typeinfo = VAR_INTBYTEPTR;
  else {	/* Four byte integer */
    destination->typeinfo = VAR_INTWORDPTR;
  }
  basicvars.current++;	/* Skip the operator */
  factor();		/* Evaluate the RH operand */
  if (GET_TOPITEM==STACK_INT)
    destination->address.offset = basicvars.staticvars[index].varentry.varinteger+pop_int();
  else if (GET_TOPITEM==STACK_FLOAT)
    destination->address.offset = basicvars.staticvars[index].varentry.varinteger+TOINT(pop_float());
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'do_unaryind' fills in the lvalue structure for an unary
** indirection operator, i.e. for something of the form
** ?(abc%+10)
*/
static void do_unaryind(lvalue *destination) {
  byte operator;
  operator = *basicvars.current;
  basicvars.current++;
  if (operator=='?')	/* Byte unary indirection operator */
    destination->typeinfo = VAR_INTBYTEPTR;
  else if (operator=='!')	/* Word unary indirection operator */
    destination->typeinfo = VAR_INTWORDPTR;
  else if (operator=='|')	/* Floating point unary indirection operator */
    destination->typeinfo = VAR_FLOATPTR;
  else {	/* String unary indirection operator */
    destination->typeinfo = VAR_DOLSTRPTR;
  }
  factor();
  if (GET_TOPITEM==STACK_INT)
    destination->address.offset = pop_int();
  else if (GET_TOPITEM==STACK_FLOAT)
    destination->address.offset = TOINT(pop_float());
  else {
    error(ERR_TYPENUM);
  }
}

/*
** lvalue_table is a table of functions called to create an lvalue
** structure for the different types of variable and so forth. It
** is indexed by the token type that gives the type of the variable
*/
static void (*lvalue_table[256])(lvalue *) = {
  bad_syntax, fix_address, do_staticvar, do_intvar,		/* 00..03 */
  do_floatvar, do_stringvar, do_arrayvar, do_elementvar,	/* 04..07 */
  do_elementvar, do_intindvar, do_floatindvar, do_statindvar,	/* 08..0B */
  bad_token, bad_token, bad_token, bad_token,			/* 0C..0F */
  bad_token, bad_token, bad_token, bad_token,			/* 10..13 */
  bad_token, bad_token, bad_token, bad_token,			/* 14..17 */
  bad_token, bad_token, bad_token, bad_token,			/* 18..1B */
  bad_token, bad_token, bad_token, bad_token,			/* 1C..1F */
  bad_token, do_unaryind, bad_token, bad_token,			/* 20..23 */
  do_unaryind, bad_token, bad_token, bad_syntax,		/* 24..27 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* 28..2B */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* 2C..2F */
  bad_token, bad_token, bad_token, bad_token,			/* 30..33 */
  bad_token, bad_token, bad_token, bad_token,			/* 34..37 */
  bad_token, bad_token, bad_syntax, bad_syntax,			/* 38..3B */
  bad_syntax, bad_syntax, bad_syntax, do_unaryind,		/* 3C..3F */
  bad_token, bad_token, bad_token, bad_token,			/* 40..43 */
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
  do_unaryind, bad_syntax, bad_syntax, bad_token,		/* 7C..7F */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* 80..83 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* 84..87 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* 88..8B */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* 8C..8F */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* 90..93 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* 94..97 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* 98..9B */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* 9C..9F */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* A0..A3 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* A4..A7 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* A8..AB */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* AC..AF */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* B0..B3 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* B4..B7 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* B8..BB */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* BC..BF */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* C0..C3 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* C4..C7 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* C8..CB */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* CC..CF */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* D0..D3 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* D4..D7 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* D8..DB */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* DC..DF */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* E0..E3 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* E4..E7 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* E8..EB */
  bad_syntax, bad_syntax, bad_token, bad_token,			/* EC..EF */
  bad_token, bad_token, bad_token, bad_token,			/* F0..F3 */
  bad_token, bad_token, bad_token, bad_token,			/* F4..F7 */
  bad_token, bad_token, bad_token, bad_token,			/* F8..FB */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax		/* FC..FF */
};

/*
** 'get_lvalue' is a crucial function within the interpreter. It parses
** a variable and returns the address of its value and its type in
** an 'lvalue' structure.  It takes into account any indirection
** operators and array indexes.
** On entry, basicvars.current points at the variable type token before
** the variable's name. The function leaves basicvars.current pointing
** at the byte after the variable's name.
*/
void get_lvalue(lvalue *destination) {
  (*lvalue_table[*basicvars.current])(destination);
}

