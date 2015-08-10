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
**
**	This file contains functions that handle assignments to
**	all types of variable and also the pseudo variables
*/

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "common.h"
#include "target.h"
#include "basicdefs.h"
#include "tokens.h"
#include "heap.h"
#include "stack.h"
#include "strings.h"
#include "variables.h"
#include "errors.h"
#include "miscprocs.h"
#include "editor.h"
#include "evaluate.h"
#include "lvalue.h"
#include "statement.h"
#include "assign.h"
#include "fileio.h"
#include "emulate.h"

#ifdef DEBUG
#include <stdio.h>
#endif


/*
** 'assignment_invalid' is called when an attempt is made to assign to
** a variable with an invalid type in 'vartype'
*/
static void assignment_invalid(pointers address) {
  error(ERR_BROKEN, __LINE__, "assign");	/* Bad variable type found */
}

/*
** 'assign_intword' deals with assignments to normal integer variables
*/
static void assign_intword(pointers address) {
  stackitem exprtype;
  if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
  exprtype = GET_TOPITEM;
  if (exprtype==STACK_INT)
    *address.intaddr = pop_int();
  else if (exprtype==STACK_FLOAT)
    *address.intaddr = TOINT(pop_float());
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'assign_float' deals with assignments to normal floating point variables
*/
static void assign_float(pointers address) {
  stackitem exprtype;
  if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
  exprtype = GET_TOPITEM;
  if (exprtype==STACK_INT)
    *address.floataddr = TOFLOAT(pop_int());
  else if (exprtype==STACK_FLOAT)
    *address.floataddr = pop_float();
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'assign_stringdol' deals with assignments to normal string variables
*/
static void assign_stringdol(pointers address) {
  stackitem exprtype;
  basicstring result, *lhstring;
  char *cp;
  if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
  exprtype = GET_TOPITEM;
  if (exprtype!=STACK_STRING && exprtype!=STACK_STRTEMP) error(ERR_TYPESTR);
  result = pop_string();
  lhstring = address.straddr;
  if (exprtype==STACK_STRTEMP) {	/* Can use string built by expression */
    free_string(*lhstring);
    *lhstring = result;
  }
  else if (lhstring->stringaddr!=result.stringaddr) {	/* Not got something like 'a$=a$' */
    cp = alloc_string(result.stringlen);	/* Have to make copy of string */
    memmove(cp, result.stringaddr, result.stringlen);
    free_string(*lhstring);
    lhstring->stringlen = result.stringlen;
    lhstring->stringaddr = cp;
  }
}

/*
** 'assign_intbyteptr' deals with assignments to byte-sized indirect
** integer variables
*/
static void assign_intbyteptr(pointers address) {
  stackitem exprtype;
  if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
  check_write(address.offset, sizeof(byte));
  exprtype = GET_TOPITEM;
  if (exprtype==STACK_INT)
    basicvars.offbase[address.offset] = pop_int();
  else if (exprtype==STACK_FLOAT)
    basicvars.offbase[address.offset] = TOINT(pop_float());
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'assign_intwordptr' deals with assignments to word-sized indirect
** integer variables
*/
static void assign_intwordptr(pointers address) {
  stackitem exprtype;
  if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
  exprtype = GET_TOPITEM;
  if (exprtype==STACK_INT)
    store_integer(address.offset, pop_int());
  else if (exprtype==STACK_FLOAT)
    store_integer(address.offset, TOINT(pop_float()));
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'assign_floatptr' assigns a value to an indirect floating point
** variable
*/
static void assign_floatptr(pointers address) {
  stackitem exprtype;
  if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
  exprtype = GET_TOPITEM;
  if (exprtype==STACK_INT)
    store_float(address.offset, TOFLOAT(pop_int()));
  else if (exprtype==STACK_FLOAT) {
    store_float(address.offset, pop_float());
  }
  else {
    error(ERR_TYPENUM);
  }
}

static void assign_dolstrptr(pointers address) {
  stackitem exprtype;
  basicstring result;
  if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
  exprtype = GET_TOPITEM;
  if (exprtype!=STACK_STRING && exprtype!=STACK_STRTEMP) error(ERR_TYPESTR);
  result = pop_string();
  check_write(address.offset, result.stringlen);
  memmove(&basicvars.offbase[address.offset], result.stringaddr, result.stringlen);
  basicvars.offbase[address.offset+result.stringlen] = CR;
  if (exprtype==STACK_STRTEMP) free_string(result);
}

/*
** 'assign_intarray' handles assignments to integer arrays.
** There is a minor issue here in that there is no pointer to the start
** of the array name available so it cannot be included in error messages
*/
static void assign_intarray(pointers address) {
  basicarray *ap, *ap2;
  stackitem exprtype;
  int32 n, value;
  int32 *p;
  exprtype = GET_TOPITEM;
  ap = *address.arrayaddr;
  if (ap==NIL) error(ERR_NODIMS, "(");	/* Undefined array */
  if (exprtype==STACK_INT || exprtype==STACK_FLOAT) {	/* array()=<value> [,<value>] */
    if (*basicvars.current==',') {	/* array()=<value>,<value>,... */
      p = ap->arraystart.intbase;
      n = 0;
      do {
        if (n>=ap->arrsize) error(ERR_BADINDEX, n, "(");	/* Trying to assign too many elements */
        p[n] = exprtype==STACK_INT ? pop_int() : TOINT(pop_float());
        n++;
        if (*basicvars.current!=',') break;
        basicvars.current++;
        expression();
        exprtype = GET_TOPITEM;
        if (exprtype!=STACK_INT && exprtype!=STACK_FLOAT) error(ERR_TYPENUM);
      } while (TRUE);
      if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
    }
    else if (!ateol[*basicvars.current])
      error(ERR_SYNTAX);
    else {	/* array()=<value> */
      value = exprtype==STACK_INT ? pop_int() : TOINT(pop_float());
      p = ap->arraystart.intbase;
      for (n=0; n<ap->arrsize; n++) p[n] = value;
    }
  }
  else if (exprtype==STACK_INTARRAY) {	/* array1()=array2() */
    if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
    ap2 = pop_array();
    if (ap2==NIL) error(ERR_NODIMS, "(");	/* Undefined array */
    if (!check_arrays(ap, ap2)) error(ERR_TYPEARRAY);
    if (ap!=ap2) memmove(ap->arraystart.intbase, ap2->arraystart.intbase, ap->arrsize*sizeof(int32));
  }
  else if (exprtype==STACK_IATEMP) {	/* array1()=array2()<op><value> */
    basicarray temp = pop_arraytemp();
    if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
    if (!check_arrays(ap, &temp)) error(ERR_TYPEARRAY);
    memmove(ap->arraystart.intbase, temp.arraystart.intbase, ap->arrsize*sizeof(int32));
    free_stackmem();
  }
  else if (exprtype==STACK_FLOATARRAY) {	/* array1()=array2() */
    float64 *fp;
    if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
    ap2 = pop_array();
    if (ap2==NIL) error(ERR_NODIMS, "(");	/* Undefined array */
    if (!check_arrays(ap, ap2)) error(ERR_TYPEARRAY);
    p = ap->arraystart.intbase;
    fp = ap2->arraystart.floatbase;
    for (n=0; n<ap->arrsize; n++) p[n] = TOINT(fp[n]);
  }
  else if (exprtype==STACK_FATEMP) {	/* array1()=array2()<op><value> */
    basicarray temp = pop_arraytemp();
    float64 *fp;
    if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
    if (!check_arrays(ap, &temp)) error(ERR_TYPEARRAY);
    p = ap->arraystart.intbase;
    fp = temp.arraystart.floatbase;
    for (n=0; n<ap->arrsize; n++) p[n] = TOINT(fp[n]);
    free_stackmem();
  }
  else {
    error(ERR_INTARRAY);
  }
}

/*
** 'assign_floatarray' deals with assignments to floating point arrays
*/
static void assign_floatarray(pointers address) {
  basicarray *ap, *ap2;
  stackitem exprtype;
  int32 n;
  float64 *p;
  static float64 fpvalue;
  exprtype = GET_TOPITEM;
  ap = *address.arrayaddr;
  if (ap==NIL) error(ERR_NODIMS, "(");	/* Undefined array */
  if (exprtype==STACK_INT || exprtype==STACK_FLOAT) {	/* array()=<value> */
    if (*basicvars.current==',') {
      p = ap->arraystart.floatbase;
      n = 0;
      do {
        if (n>=ap->arrsize) error(ERR_BADINDEX, n, "(");	/* Trying to assign too many elements */
        p[n] = exprtype==STACK_INT ? TOFLOAT(pop_int()) : pop_float();
        n++;
        if (*basicvars.current!=',') break;
        basicvars.current++;
        expression();
        exprtype = GET_TOPITEM;
        if (exprtype!=STACK_INT && exprtype!=STACK_FLOAT) error(ERR_TYPENUM);
      } while (TRUE);
      if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
    }
    else if (!ateol[*basicvars.current])
      error(ERR_SYNTAX);
    else {
      fpvalue = exprtype==STACK_INT ? TOFLOAT(pop_int()) : pop_float();
      p = ap->arraystart.floatbase;
      for (n=0; n<ap->arrsize; n++) p[n] = fpvalue;
    }
  }
  else if (exprtype==STACK_FLOATARRAY) {	/* array1()=array2() */
    if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
    ap2 = pop_array();
    if (ap2==NIL) error(ERR_NODIMS, "(");	/* Undefined array */
    if (!check_arrays(ap, ap2)) error(ERR_TYPEARRAY);
    if (ap!=ap2) memmove(ap->arraystart.floatbase, ap2->arraystart.floatbase, ap->arrsize*sizeof(float64));
  }
  else if (exprtype==STACK_FATEMP) {	/* array1()=array2()<op><value> */
    basicarray temp = pop_arraytemp();
    if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
    if (!check_arrays(ap, &temp)) error(ERR_TYPEARRAY);
    memmove(ap->arraystart.floatbase, temp.arraystart.floatbase, ap->arrsize*sizeof(float64));
    free_stackmem();
  }
  else if (exprtype==STACK_INTARRAY) {	/* array1()=array2() */
    int32 *ip;
    if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
    ap2 = pop_array();
    if (ap2==NIL) error(ERR_NODIMS, "(");	/* Undefined array */
    if (!check_arrays(ap, ap2)) error(ERR_TYPEARRAY);
    p = ap->arraystart.floatbase;
    ip = ap2->arraystart.intbase;
    for (n=0; n<ap->arrsize; n++) p[n] = TOFLOAT(ip[n]);
  }
  else if (exprtype==STACK_IATEMP) {	/* array1()=array2()<op><value> */
    basicarray temp = pop_arraytemp();
    int32 *ip;
    if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
    if (!check_arrays(ap, &temp)) error(ERR_TYPEARRAY);
    p = ap->arraystart.floatbase;
    ip = temp.arraystart.intbase;
    for (n=0; n<ap->arrsize; n++) p[n] = TOFLOAT(ip[n]);
    free_stackmem();
  }
  else {
    error(ERR_FPARRAY);
  }
}

/*
** 'assign_strarray' handles assignments to string arrays.
** One complication here is that if the string is a normal string (that
** is, it is not a string built as the result of an expression) it has to
** be copied to the string workspace so that cases such as 'a$()=a$(0)'
** will be dealt with correctly.
*/
static void assign_strarray(pointers address) {
  stackitem exprtype;
  int32 n, stringlen;
  basicarray *ap, *ap2;
  basicstring *p, *p2, stringvalue;
  char *stringaddr;
  exprtype = GET_TOPITEM;
  ap = *address.arrayaddr;
  if (ap==NIL) error(ERR_NODIMS, "(");	/* Undefined array */
  if (exprtype==STACK_STRING || exprtype==STACK_STRTEMP) {	/* array$()=<string> */
    if (*basicvars.current==',') {	/* array$()=<value>,<value>,... */
      p = ap->arraystart.stringbase;
      n = 0;
      do {
        if (n>=ap->arrsize) error(ERR_BADINDEX, n, "(");	/* Trying to assign too many elements */
        stringvalue = pop_string();
        if (stringvalue.stringlen==0) {		/* Treat the null string as a special case */
          free_string(*p);
          p->stringlen = 0;
          p->stringaddr = nullstring;	/* 'nullstring' is the null string found in 'variables.c' */
        }
        else {
          stringlen = stringvalue.stringlen;
          if (exprtype==STACK_STRING) {	/* Reference to normal string e.g. 'abc$' */
            memmove(basicvars.stringwork, stringvalue.stringaddr, stringlen);	/* Have to use a copy of the string */
            free_string(*p);
            p->stringlen = stringlen;
            p->stringaddr = alloc_string(stringlen);
            memmove(p->stringaddr, basicvars.stringwork, stringlen);
          }
          else {	/* Source is a string temp - Can use this directly */
            free_string(*p);
            p->stringlen = stringlen;
            p->stringaddr = alloc_string(stringlen);
            memmove(p->stringaddr, stringvalue.stringaddr, stringlen);
            free_string(stringvalue);
          }
        }
        p++;
        n++;
        if (*basicvars.current!=',') break;
        basicvars.current++;
        expression();
        exprtype = GET_TOPITEM;
        if (exprtype!=STACK_STRING && exprtype!=STACK_STRTEMP) error(ERR_TYPESTR);
      } while (TRUE);
      if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
    }
    else if (!ateol[*basicvars.current])
      error(ERR_SYNTAX);
    else {	/* array$()=<value> */
      stringvalue = pop_string();
      p = ap->arraystart.stringbase;
      stringlen = stringvalue.stringlen;
      if (stringlen==0) {	/* Treat 'array$()=""' as a special case */
        for (n=0; n<ap->arrsize; n++) {	/* Set all elements of the array to "" */
          free_string(p[n]);
          p[n].stringlen = 0;
          p[n].stringaddr = nullstring;
        }
      }
      else {	/* Normal case - 'array$()=<non-null string>' */
        if (exprtype==STACK_STRING) {
          memmove(basicvars.stringwork, stringvalue.stringaddr, stringlen);
          stringaddr = basicvars.stringwork;
        }
        else {
          stringaddr = stringvalue.stringaddr;	/* String is a temp string anyway */
        }
        for (n=0; n<ap->arrsize; n++) {	/* Set all elements of the array to <stringvalue> */
          free_string(*p);
          p->stringlen = stringlen;
          p->stringaddr = alloc_string(stringlen);
          memmove(p->stringaddr, stringaddr, stringlen);
          p++;
        }
        if (exprtype==STACK_STRTEMP) free_string(stringvalue); /* Finally dispose of string if a temporary */
      }
    }
  }
  else if (exprtype==STACK_STRARRAY) {	/* array$()=array$() */
    if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
    ap2 = pop_array();
    if (ap!=ap2) {	/* 'a$()=a$()' could cause this code to go wrong */
      if (ap2==NIL) error(ERR_NODIMS, "(");	/* Undefined array */
      if (!check_arrays(ap, ap2)) error(ERR_TYPEARRAY);
      p = ap->arraystart.stringbase;
      p2 = ap2->arraystart.stringbase;
      for (n=0; n<ap->arrsize; n++) {	/* Duplicate entire array */
        free_string(*p);
        p->stringlen = p2->stringlen;
        p->stringaddr = alloc_string(p2->stringlen);
        memmove(p->stringaddr, p2->stringaddr, p2->stringlen);
        p++;
        p2++;
      }
    }
  }
  else if (exprtype==STACK_SATEMP) {	/* array1$()=array2$()<op><value> */
    basicarray temp = pop_arraytemp();
    int n, count;
    if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
    if (!check_arrays(ap, &temp)) error(ERR_TYPEARRAY);
    count = ap->arrsize;
    p = ap->arraystart.stringbase;
    for (n=0; n<count; n++) free_string(p[n]);	/* Discard old destination array strings */
    memmove(p, temp.arraystart.stringbase, count*sizeof(basicstring));		/* Copy temp array to dest array */
    free_stackmem();	/* Discard temp string array (but not the strings just copied!) */
  }
  else {
    error(ERR_STRARRAY);
  }
}

/*
** 'assiplus_intword' handles the '+=' assignment operator for integer
** variables
*/
static void assiplus_intword(pointers address) {
  stackitem exprtype;
  exprtype = GET_TOPITEM;
  if (exprtype==STACK_INT)
    *address.intaddr+=pop_int();
  else if (exprtype==STACK_FLOAT)
    *address.intaddr+=TOINT(pop_float());
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'assiplus_float' handles the '+=' assignment operator for floating point
** variables
*/
static void assiplus_float(pointers address) {
  stackitem exprtype;
  exprtype = GET_TOPITEM;
  if (exprtype==STACK_INT)
    *address.floataddr+=TOFLOAT(pop_int());
  else if (exprtype==STACK_FLOAT)
    *address.floataddr+=pop_float();
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'assiplus_stringdol' handles the '+=' assignment operator for string
** variables
*/
static void assiplus_stringdol(pointers address) {
  stackitem exprtype;
  basicstring result, *lhstring;
  int32 extralen, newlen;
  char *cp;
  exprtype = GET_TOPITEM;
  if (exprtype!=STACK_STRING && exprtype!=STACK_STRTEMP) error(ERR_TYPESTR);
  result = pop_string();
  extralen = result.stringlen;
  if (extralen!=0) {	/* Length of string to append is not zero */
    lhstring = address.straddr;
    newlen = lhstring->stringlen+extralen;
    if (newlen>MAXSTRING) error(ERR_STRINGLEN);
    cp = resize_string(lhstring->stringaddr, lhstring->stringlen, newlen);
    memmove(cp+lhstring->stringlen, result.stringaddr, extralen);
    lhstring->stringlen = newlen;
    lhstring->stringaddr = cp;
  }
  if (exprtype==STACK_STRTEMP) free_string(result);
}

/*
** 'addass_intbyteptr' handles the '+=' assignment operator for single
** byte integer indirect variables
*/
static void assiplus_intbyteptr(pointers address) {
  stackitem exprtype;
  check_write(address.offset, sizeof(byte));
  exprtype = GET_TOPITEM;
  if (exprtype==STACK_INT)
    basicvars.offbase[address.offset]+=pop_int();
  else if (exprtype==STACK_FLOAT)
    basicvars.offbase[address.offset]+=TOINT(pop_float());
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'assiplus_intwordptr' handles the '+=' assignment operator for word
** indirect integer variables
*/
static void assiplus_intwordptr(pointers address) {
  stackitem exprtype = GET_TOPITEM;
  if (exprtype==STACK_INT)
    store_integer(address.offset, get_integer(address.offset)+pop_int());
  else if (exprtype==STACK_FLOAT)
    store_integer(address.offset, get_integer(address.offset)+TOINT(pop_float()));
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'assiplus_floatptr' handles the '+=' assignment operator for indirect
** floating point variables
*/
static void assiplus_floatptr(pointers address) {
  stackitem exprtype= GET_TOPITEM;
  if (exprtype==STACK_INT)
    store_float(address.offset, get_float(address.offset)+TOFLOAT(pop_int()));
  else if (exprtype==STACK_FLOAT)
    store_float(address.offset, get_float(address.offset)+pop_float());
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'assiplus_dolstrptr' handles the '+=' assignment operator for indirect
** string variables
*/
static void assiplus_dolstrptr(pointers address) {
  stackitem exprtype;
  basicstring result;
  int32 stringlen, endoff;
  exprtype = GET_TOPITEM;
  if (exprtype!=STACK_STRING && exprtype!=STACK_STRTEMP) error(ERR_TYPESTR);
  result = pop_string();
  endoff = address.offset;	/* Figure out where to append the string */
  stringlen = 0;
  while (stringlen<=MAXSTRING && basicvars.offbase[endoff]!=CR) {	/* Find the CR at the end of the dest string */
    endoff++;
    stringlen++;
  }
  if (stringlen>MAXSTRING) endoff = address.offset;	/* CR at end not found - Assume dest is zero length */
  check_write(endoff, result.stringlen);
  memmove(&basicvars.offbase[endoff], result.stringaddr, result.stringlen);
  basicvars.offbase[endoff+result.stringlen] = CR;
  if (exprtype==STACK_STRTEMP) free_string(result);
}

/*
** 'assiplus_intarray' handles the '+=' assignment operator for integer
** arrays
*/
static void assiplus_intarray(pointers address) {
  stackitem exprtype;
  basicarray *ap, *ap2;
  int32 *p, *p2;
  int32 n, value;
  exprtype = GET_TOPITEM;
  ap = *address.arrayaddr;
  if (ap==NIL) error(ERR_NODIMS, "(");	/* Undefined array */
  if (exprtype==STACK_INT || exprtype==STACK_FLOAT) {	/* array()+=<value> */
    value = exprtype==STACK_INT ? pop_int() : TOINT(pop_float());
    p = ap->arraystart.intbase;
    for (n=0; n<ap->arrsize; n++) p[n]+=value;
  }
  else if (exprtype==STACK_INTARRAY) {	/* array1()+=array2() */
    ap2 = pop_array();
    if (ap2==NIL) error(ERR_NODIMS, "(");	/* Undefined array */
    if (!check_arrays(ap, ap2)) error(ERR_TYPEARRAY);
    p = ap->arraystart.intbase;
    p2 = ap2->arraystart.intbase;
    for (n=0; n<ap->arrsize; n++) p[n]+=p2[n];
  }
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'assiplus_floatarray' handles the '+=' assignment operator for
** floating point arrays
*/
static void assiplus_floatarray(pointers address) {
  stackitem exprtype;
  basicarray *ap, *ap2;
  float64 *p, *p2;
  int32 n;
  static float64 fpvalue;
  exprtype = GET_TOPITEM;
  ap = *address.arrayaddr;
  if (ap==NIL) error(ERR_NODIMS, "(");	/* Undefined array */
  if (exprtype==STACK_INT || exprtype==STACK_FLOAT) {	/* array()+=<value> */
    fpvalue = exprtype==STACK_INT ? TOFLOAT(pop_int()) : pop_float();
    p = ap->arraystart.floatbase;
    for (n=0; n<ap->arrsize; n++) p[n]+=fpvalue;
  }
  else if (exprtype==STACK_FLOATARRAY) {	/* array1()+=array2() */
    ap2 = pop_array();
    if (ap2==NIL) error(ERR_NODIMS, "(");	/* Undefined array */
    if (!check_arrays(ap, ap2)) error(ERR_TYPEARRAY);
    p = ap->arraystart.floatbase;
    p2 = ap2->arraystart.floatbase;
    for (n=0; n<ap->arrsize; n++) p[n]+=p2[n];
  }
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'assiplus_strarray' handles the '+=' assignment operator for
** string arrays
*/
static void assiplus_strarray(pointers address) {
  stackitem exprtype;
  basicarray *ap, *ap2;
  basicstring *p, *p2;
  int32 n, stringlen;
  char *stringaddr, *cp;
  basicstring stringvalue;
  exprtype = GET_TOPITEM;
  ap = *address.arrayaddr;
  if (ap==NIL) error(ERR_NODIMS, "(");	/* Undefined array */
  if (exprtype==STACK_STRING || exprtype==STACK_STRTEMP) {	/* array$()+=<string> */
    stringvalue = pop_string();
    stringlen = stringvalue.stringlen;
    if (stringlen>0) {	/* Not trying to append a null string */
      p = ap->arraystart.stringbase;
      if (exprtype==STACK_STRING) {	/* Must work with a copy of the string here */
        memmove(basicvars.stringwork, stringvalue.stringaddr, stringlen);
        stringaddr = basicvars.stringwork;
      }
      else {	/* String is already a temporary string - Can use it directly */
        stringaddr = stringvalue.stringaddr;
      }
      for (n=0; n<ap->arrsize; n++) {	/* Append <stringvalue> to all elements of the array */
        if (p->stringlen+stringlen>MAXSTRING) error(ERR_STRINGLEN);
        cp = resize_string(p->stringaddr, p->stringlen, p->stringlen+stringlen);
        memmove(cp+p->stringlen, stringaddr, stringlen);
        p->stringlen+=stringlen;
        p->stringaddr = cp;
        p++;
      }
      if (exprtype==STACK_STRTEMP) free_string(stringvalue);	/* Dispose of string if a temporary */
    }
  }
  else if (exprtype==STACK_STRARRAY) {	/* array$()+=array$() */
    ap2 = pop_array();
    if (ap2==NIL) error(ERR_NODIMS, "(");	/* Undefined array */
    if (!check_arrays(ap, ap2)) error(ERR_TYPEARRAY);
    p = ap->arraystart.stringbase;
    p2 = ap2->arraystart.stringbase;
    for (n=0; n<ap->arrsize; n++) {
      stringlen = p2->stringlen;
      if (stringlen>0) {
        if (p->stringlen+stringlen>MAXSTRING) error(ERR_STRINGLEN);
        memmove(basicvars.stringwork, p2->stringaddr, stringlen);
        cp = resize_string(p->stringaddr, p->stringlen, p->stringlen+stringlen);
        memmove(cp+p->stringlen, basicvars.stringwork, stringlen);
        p->stringlen+=stringlen;
        p->stringaddr = cp;
      }
      p++;
      p2++;
    }
  }
  else {
    error(ERR_TYPESTR);
  }
}

/*
** 'assiminus_intword' handles the '-=' assignment operator for integer
** variables
*/
static void assiminus_intword(pointers address) {
  stackitem exprtype;
  exprtype = GET_TOPITEM;
  if (exprtype==STACK_INT)
    *address.intaddr-=pop_int();
  else if (exprtype==STACK_FLOAT)
    *address.intaddr-=TOINT(pop_float());
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'assiminus_float' handles the '-=' assignment operator for floating point
** variables
*/
static void assiminus_float(pointers address) {
  stackitem exprtype;
  exprtype = GET_TOPITEM;
  if (exprtype==STACK_INT)
    *address.floataddr-=TOFLOAT(pop_int());
  else if (exprtype==STACK_FLOAT)
    *address.floataddr-=pop_float();
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'addass_intbyteptr' handles the '-=' assignment operator for single
** byte integer indirect variables
*/
static void assiminus_intbyteptr(pointers address) {
  stackitem exprtype;
  check_write(address.offset, sizeof(byte));
  exprtype = GET_TOPITEM;
  if (exprtype==STACK_INT)
    basicvars.offbase[address.offset]-=pop_int();
  else if (exprtype==STACK_FLOAT)
    basicvars.offbase[address.offset]-=TOINT(pop_float());
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'assiminus_intwordptr' handles the '-=' assignment operator for word
** indirect integer variables
*/
static void assiminus_intwordptr(pointers address) {
  stackitem exprtype = GET_TOPITEM;
  if (exprtype==STACK_INT)
    store_integer(address.offset, get_integer(address.offset)-pop_int());
  else if (exprtype==STACK_FLOAT)
    store_integer(address.offset, get_integer(address.offset)-TOINT(pop_float()));
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'assiminus_floatptr' handles the '-=' assignment operator for indirect
** floating point variables
*/
static void assiminus_floatptr(pointers address) {
  stackitem exprtype = GET_TOPITEM;
  if (exprtype==STACK_INT)
    store_float(address.offset, get_float(address.offset)-TOFLOAT(pop_int()));
  else if (exprtype==STACK_FLOAT)
    store_float(address.offset, get_float(address.offset)-pop_float());
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'assiminus_intarray' handles the '-=' assignment operator for integer
** arrays
*/
static void assiminus_intarray(pointers address) {
  stackitem exprtype;
  basicarray *ap, *ap2;
  int32 *p, *p2, n, value;
  exprtype = GET_TOPITEM;
  ap = *address.arrayaddr;
  if (ap==NIL) error(ERR_NODIMS, "(");	/* Undefined array */
  if (exprtype==STACK_INT || exprtype==STACK_FLOAT) {	/* array()-=<value> */
    value = exprtype==STACK_INT ? pop_int() : TOINT(pop_float());
    p = ap->arraystart.intbase;
    for (n=0; n<ap->arrsize; n++) p[n]-=value;
  }
  else if (exprtype==STACK_INTARRAY) {	/* array1()-=array2() */
    ap2 = pop_array();
    if (ap2==NIL) error(ERR_NODIMS, "(");	/* Undefined array */
    if (!check_arrays(ap, ap2)) error(ERR_TYPEARRAY);
    p = ap->arraystart.intbase;
    p2 =ap2->arraystart.intbase;
    for (n=0; n<ap->arrsize; n++) p[n]-=p2[n];
  }
  else {
    error(ERR_TYPENUM);
  }
}

/*
** 'assiminus_floatarray' handles the '-=' assignment operator for
** floating point arrays
*/
static void assiminus_floatarray(pointers address) {
  stackitem exprtype;
  basicarray *ap, *ap2;
  float64 *p, *p2;
  int32 n;
  static float64 fpvalue;
  exprtype = GET_TOPITEM;
  ap = *address.arrayaddr;
  if (ap==NIL) error(ERR_NODIMS, "(");	/* Undefined array */
  if (exprtype==STACK_INT || exprtype==STACK_FLOAT) {	/* array()-=<value> */
    fpvalue = exprtype==STACK_INT ? TOFLOAT(pop_int()) : pop_float();
    p = ap->arraystart.floatbase;
    for (n=0; n<ap->arrsize; n++) p[n]-=fpvalue;
  }
  else if (exprtype==STACK_FLOATARRAY) {	/* array1()-=array2() */
    ap2 = pop_array();
    if (ap2==NIL) error(ERR_NODIMS, "(");	/* Undefined array */
    if (!check_arrays(ap, ap2)) error(ERR_TYPEARRAY);
    p = ap->arraystart.floatbase;
    p2 = ap2->arraystart.floatbase;
    for (n=0; n<ap->arrsize; n++) p[n]-=p2[n];
  }
  else {
    error(ERR_TYPENUM);
  }
}

static void assiminus_badtype(pointers address) {
  error(ERR_BADARITH);	/* Cannot use '-=' on string operands */
}

static void (*assign_table[])(pointers) = {
  assignment_invalid, assignment_invalid, assign_intword, assign_float,
  assign_stringdol, assignment_invalid, assignment_invalid, assignment_invalid,
  assignment_invalid, assignment_invalid, assign_intarray, assign_floatarray,
  assign_strarray, assignment_invalid, assignment_invalid, assignment_invalid,
  assignment_invalid, assign_intbyteptr, assign_intwordptr, assign_floatptr,
  assignment_invalid, assign_dolstrptr, assignment_invalid, assignment_invalid
};

static void (*assiplus_table[])(pointers) = {
  assignment_invalid, assignment_invalid, assiplus_intword, assiplus_float,
  assiplus_stringdol, assignment_invalid, assignment_invalid, assignment_invalid,
  assignment_invalid, assignment_invalid, assiplus_intarray, assiplus_floatarray,
  assiplus_strarray, assignment_invalid, assignment_invalid, assignment_invalid,
  assignment_invalid, assiplus_intbyteptr, assiplus_intwordptr, assiplus_floatptr,
  assignment_invalid, assiplus_dolstrptr, assignment_invalid, assignment_invalid
};

static void (*assiminus_table[])(pointers) = {
  assignment_invalid, assignment_invalid, assiminus_intword, assiminus_float,
  assiminus_badtype, assignment_invalid, assignment_invalid, assignment_invalid,
  assignment_invalid, assignment_invalid, assiminus_intarray, assiminus_floatarray,
  assiminus_badtype, assignment_invalid, assignment_invalid, assignment_invalid,
  assignment_invalid, assiminus_intbyteptr, assiminus_intwordptr, assiminus_floatptr,
  assignment_invalid, assiminus_badtype, assignment_invalid, assignment_invalid
};

/*
** The main purpose of 'exec_assignment' is to deal with the more complex
** assignments. However all assignments are handled by this function the
** first time they are seen, that is, when the token type of the variable
** on the left hand side is 'TOKEN_XVAR'. The call to 'get_lvalue' will
** change the token type so that on future calls simple cases, for example,
** assignments to integer variables, will be dealt with by specific functions
** rather than this general one. Any of the more complex types, for example,
** variables with indirection operators, will contine to be dealt with by
** this code
*/
void exec_assignment(void) {
  byte assignop;
  lvalue destination;
#ifdef DEBUG
  if (basicvars.debug_flags.allstack) fprintf(stderr, "Start assignment- Basic stack pointer = %p\n", basicvars.stacktop.bytesp);
#endif
  get_lvalue(&destination);
  assignop = *basicvars.current;
  if (assignop=='=') {
    basicvars.current++;
    expression();
    (*assign_table[destination.typeinfo])(destination.address);
  }
  else if (assignop==TOKEN_PLUSAB) {
    basicvars.current++;
    expression();
    if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
    (*assiplus_table[destination.typeinfo])(destination.address);
  }
  else if (assignop==TOKEN_MINUSAB) {
    basicvars.current++;
    expression();
    if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
    (*assiminus_table[destination.typeinfo])(destination.address);
  }
  else {
    error(ERR_EQMISS);
  }
#ifdef DEBUG
  if (basicvars.debug_flags.allstack) fprintf(stderr, "End assignment- Basic stack pointer = %p\n", basicvars.stacktop.bytesp);
#endif
}

/*
** 'decode_format' decodes an '@%' format when it is supplied as a
** character string, returning the new format. If there are any
** errors the original format is returned
*/
static int32 decode_format(basicstring format) {
  const int32
    FORMATMASK = 0xff0000,	/* Mask for format specifier */
    DECPTMASK = 0xff00,		/* Mask for number of digits after decimal point */
    WIDTHMASK = 0xff,		/* Mask for field width */
    GFORMAT = 0,
    EFORMAT = 0x10000,
    FFORMAT = 0x20000,
    DECPTSHIFT = 8;
  int32 original, newformat;
  char *fp, *ep;
  original = newformat = basicvars.staticvars[ATPERCENT].varentry.varinteger;
  fp = format.stringaddr;
  ep = fp+format.stringlen;
  if (fp==ep) return newformat & ~STRUSE;	/* Null string turns off 'use with STR$' flag */
  if (*fp=='+') {	/* Turn on 'use with STR$' flag */
    newformat = newformat | STRUSE;
    fp++;
    if (fp==ep) return newformat;
  }
  else {	/* Clear the 'use with STR$' flag */
    newformat = newformat & ~STRUSE;
  }
  if (tolower(*fp)>='e' && tolower(*fp)<='g') {	/* Change number format */
    switch (tolower(*fp)) {
    case 'e':
      newformat = (newformat & ~FORMATMASK) | EFORMAT;
      break;
    case 'f':
      newformat = (newformat & ~FORMATMASK) | FFORMAT;
      break;
    default:	/* This leaves 'g' format */
      newformat = (newformat & ~FORMATMASK) | GFORMAT;
    }
    fp++;
    if (fp==ep) return newformat;
  }
  if (isdigit(*fp)) {	/* Field width */
    newformat = (newformat & ~WIDTHMASK) | (CAST(strtol(fp, &fp, 10), int32) & WIDTHMASK);
    if (fp==ep) return newformat;
  }
  if (*fp==',' || *fp=='.') {	/* Number of digits after decimal point */
    if (*fp==',')	/* Set "use ',' as decimal point" flag */
      newformat = newformat | COMMADPT;
    else {	/* Use '.' as decimal point */
      newformat = newformat & ~COMMADPT;
    }
    fp++;
    if (fp==ep) return newformat;
    if (!isdigit(*fp)) return original;
    newformat = (newformat & ~DECPTMASK) | ((CAST(strtol(fp, &fp, 10), int32)<<DECPTSHIFT) & DECPTMASK);
  }
  if (fp!=ep) return original;
  return newformat;
}

/*
** 'assign_staticvar' handles simple assignments to the static integer
** variables
*/
void assign_staticvar(void) {
  byte assignop;
  int32 value = 0;
  int32 varindex;
  stackitem exprtype;
#ifdef DEBUG
  if (basicvars.debug_flags.allstack) fprintf(stderr, "Static integer assignment start - Basic stack pointer = %p\n", basicvars.stacktop.bytesp);
#endif
  basicvars.current++;		/* Skip to the variable's index */
  varindex = *basicvars.current;
  basicvars.current++;		/* Skip index */
  assignop = *basicvars.current;
  basicvars.current++;
  if (assignop!='=' && assignop!=TOKEN_PLUSAB && assignop!=TOKEN_MINUSAB) error(ERR_EQMISS);
  expression();
  if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
  exprtype = GET_TOPITEM;
  if (varindex==ATPERCENT && assignop=='=') {	/* @%= is a special case */
    if (exprtype==STACK_INT)
      basicvars.staticvars[ATPERCENT].varentry.varinteger = pop_int();
    else if (exprtype==STACK_FLOAT)
      basicvars.staticvars[ATPERCENT].varentry.varinteger = TOINT(pop_float());
    else {
      basicstring format;
      format = pop_string();
      basicvars.staticvars[ATPERCENT].varentry.varinteger = decode_format(format);
      if (exprtype==STACK_STRTEMP) free_string(format);
    }
  }
  else {	/* Other static variables */
    if (exprtype==STACK_INT)
      value = pop_int();
    else if (exprtype==STACK_FLOAT)
      value = TOINT(pop_float());
    else {
      error(ERR_TYPENUM);
    }
    if (assignop=='=')
      basicvars.staticvars[varindex].varentry.varinteger = value;
    else if (assignop==TOKEN_PLUSAB)
      basicvars.staticvars[varindex].varentry.varinteger+=value;
    else {
      basicvars.staticvars[varindex].varentry.varinteger-=value;
    }
  }
#ifdef DEBUG
  if (basicvars.debug_flags.allstack) fprintf(stderr, "End assignment- Basic stack pointer = %p\n", basicvars.stacktop.bytesp);
#endif
}

/*
** 'assign_intvar' handles assignments to integer variables.
** There is no need for this function to check the assignment operator
** used as this would have been checked the first time the assignment
** was seen (when it was dealt with by 'exec_assignment'). The same
** goes for the end of statement check.
*/
void assign_intvar(void) {
  byte assignop;
  int32 value = 0;
  int32 *ip;
  stackitem exprtype;
#ifdef DEBUG
  if (basicvars.debug_flags.allstack) fprintf(stderr, "Integer assignment start - Basic stack pointer = %p\n", basicvars.stacktop.bytesp);
#endif
  ip = GET_ADDRESS(basicvars.current, int32 *);
  basicvars.current+=1+LOFFSIZE;	/* Skip the pointer to the variable */
  assignop = *basicvars.current;
  basicvars.current++;
  expression();
  exprtype = GET_TOPITEM;
  if (exprtype==STACK_INT)
    value = pop_int();
  else if (exprtype==STACK_FLOAT)
    value = TOINT(pop_float());
  else {
    error(ERR_TYPENUM);
  }
  if (assignop=='=')
    *ip = value;
  else if (assignop==TOKEN_PLUSAB)
    *ip+=value;
  else {
    *ip-=value;
  }
#ifdef DEBUG
  if (basicvars.debug_flags.allstack) fprintf(stderr, "Integer assignment end - Basic stack pointer = %p\n", basicvars.stacktop.bytesp);
#endif
}

/*
** 'assign_floatvar' handles assignments to floating point variables
** See 'assign_intval' for general comments
*/
void assign_floatvar(void) {
  byte assignop;
  static float64 value;
  float64 *fp;
  stackitem exprtype;
#ifdef DEBUG
  if (basicvars.debug_flags.allstack) fprintf(stderr, "Float assignment start - Basic stack pointer = %p\n", basicvars.stacktop.bytesp);
#endif
  fp = GET_ADDRESS(basicvars.current, float64 *);
  basicvars.current+=1+LOFFSIZE;		/* Skip the pointer to the variable */
  assignop = *basicvars.current;
  basicvars.current++;
  expression();
  exprtype = GET_TOPITEM;
  if (exprtype==STACK_INT)
    value = TOFLOAT(pop_int());
  else if (exprtype==STACK_FLOAT)
    value = pop_float();
  else {
    error(ERR_TYPENUM);
  }
  if (assignop=='=')
    *fp = value;
  else if (assignop==TOKEN_PLUSAB)
    *fp+=value;
  else {
    *fp-=value;
  }
#ifdef DEBUG
  if (basicvars.debug_flags.allstack) fprintf(stderr, "Float assignment end - Basic stack pointer = %p\n", basicvars.stacktop.bytesp);
#endif
}

/*
** 'assign_stringvar' handles assignments to string variables
** See 'assign_intval' for general comments
*/
void assign_stringvar(void) {
  byte assignop;
  pointers address;
#ifdef DEBUG
  if (basicvars.debug_flags.allstack) fprintf(stderr, "String assignment start - Basic stack pointer = %p\n", basicvars.stacktop.bytesp);
#endif
  address.straddr = GET_ADDRESS(basicvars.current, basicstring *);
  basicvars.current+=1+LOFFSIZE;		/* Skip the pointer to the variable */
  assignop = *basicvars.current;
  basicvars.current++;
  if (assignop=='=') {
    expression();
    assign_stringdol(address);
  }
  else if (assignop==TOKEN_PLUSAB) {
    expression();
    if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
    assiplus_stringdol(address);
  }
  else if (assignop==TOKEN_MINUSAB)
    assiminus_badtype(address);
  else {
    error(ERR_EQMISS);
  }
#ifdef DEBUG
  if (basicvars.debug_flags.allstack) fprintf(stderr, "String assignment end - Basic stack pointer = %p\n", basicvars.stacktop.bytesp);
#endif
}

/*
** 'assign_himem' is called to change the value of 'HIMEM'. It
** only allows HIMEM to be changed if there is nothing on the
** Basic stack, that is, outside any functions or procedures,
** when LOCAL ERROR has not been used and so forth.
*/
static void assign_himem(void) {
  int32 offset;
  byte *address;
  basicvars.current++;		/* Skip HIMEM */
  if (*basicvars.current!='=') error(ERR_EQMISS);
  basicvars.current++;
  offset = ALIGN(eval_integer());
  if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
  address = basicvars.offbase+offset;
  if (address<(basicvars.vartop+1024) || address>=basicvars.end)
    error(WARN_BADHIMEM);	/* Flag error (execution continues after this one) */
  else if (!safestack())
    error(ERR_HIMEMFIXED);	/* Cannot alter HIMEM here */
  else {
/*
** Reset HIMEM. The Basic stack is created afresh at the new value
** of HIMEM.
*/
    basicvars.himem = address;
    init_stack();
    init_expressions();
  }
}

/*
** 'assign_ext' handles the Basic pseudo-variable 'EXT', which sets the
** size of a file
*/
static void assign_ext(void) {
  int32 handle, newsize;
  basicvars.current++;
  if (*basicvars.current!='#') error(ERR_HASHMISS);
  basicvars.current++;		/* Skip '#' token */
  handle = eval_intfactor();
  if (*basicvars.current!='=') error(ERR_EQMISS);
  basicvars.current++;
  newsize = eval_integer();
  if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
  fileio_setext(handle, newsize);
}

/*
** 'assign_filepath' changes the value of the pseudo-variable 'FILEPATH$'.
** There is no check to ensure that the directory list is valid
*/
static void assign_filepath(void) {
  stackitem stringtype;
  basicstring string;
  basicvars.current++;
  if (*basicvars.current!='=') error(ERR_EQMISS);
  basicvars.current++;
  expression();
  if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
  stringtype = GET_TOPITEM;
  if (stringtype!=STACK_STRING && stringtype!=STACK_STRTEMP) error(ERR_TYPESTR);
  string = pop_string();
  if (basicvars.loadpath!=NIL) free(basicvars.loadpath);	/* Discard current path */
  if (string.stringlen==0)	/* String length is zero - No path given */
    basicvars.loadpath = NIL;
  else {	/* Set up new load path */
    basicvars.loadpath = malloc(string.stringlen+1);		/* +1 for NUL at end */
    if (basicvars.loadpath==NIL) error(ERR_NOROOM);	/* Not enough memory left */
    memcpy(basicvars.loadpath, string.stringaddr, string.stringlen);
    basicvars.loadpath[string.stringlen] = NUL;
  }
  if (stringtype==STACK_STRTEMP) free_string(string);
}

/*
** 'assign_left' deals with the 'LEFT$(' pseudo variable, which replaces the
** left-hand end of a string with the string on the right hand side of the
** assignment
*/
static void assign_left(void) {
  int32 count;
  lvalue destination;
  stackitem stringtype;
  basicstring lhstring, rhstring;
  basicvars.current++;		/* Skip LEFT$( token */
  get_lvalue(&destination);	/* Fetch the destination address */
  if (destination.typeinfo!=VAR_STRINGDOL && destination.typeinfo!=VAR_DOLSTRPTR) error(ERR_TYPESTR);
  if (*basicvars.current==',') {	/* Number of characters to be replaced is given */
    basicvars.current++;
    count = eval_integer();
    if (count<0)		/* If count is negative, treat it as if it was missing */
      count = MAXSTRING;
    else if (count==0) {	/* If count is zero, BBC Basic still replaces the first char */
      count = 1;
    }
  }
  else {
    count = MAXSTRING;
  }
  if (*basicvars.current!=')') error(ERR_RPMISS);
  basicvars.current++;		/* Skip ')' */
  if (*basicvars.current!='=') error(ERR_EQMISS);
  basicvars.current++;
  expression();	/* Evaluate the RH side of the assignment */
  if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
  stringtype = GET_TOPITEM;
  if (stringtype!=STACK_STRING && stringtype!=STACK_STRTEMP) error(ERR_TYPESTR);
  rhstring = pop_string();
  if (count>rhstring.stringlen) count = rhstring.stringlen;
  if (destination.typeinfo==VAR_STRINGDOL)	/* Left-hand string is a string variable */
    lhstring = *destination.address.straddr;
  else {	/* Left-hand string is a '$<addr>' string, so fake a descriptor for it */
    lhstring.stringaddr = CAST(&basicvars.offbase[destination.address.offset], char *);
    lhstring.stringlen = get_stringlen(destination.address.offset);
  }
  if (count>lhstring.stringlen) count = lhstring.stringlen;
  if (count>0) memmove(lhstring.stringaddr, rhstring.stringaddr, count);
  if (stringtype==STACK_STRTEMP) free_string(rhstring);
}

/*
** 'assign_lomem' deals with the Basic pseudo variable 'LOMEM'.
** Changing the value of LOMEM results in all of the variables defined
** so far being discarded. Note that the value of 'stacklimit' is also
** changed by this. As stacklimit is always set to the address of the
** top of the heap plus a bit for safety, this means that Basic heap
** always has to live below the Basic stack
*/
static void assign_lomem(void) {
  byte *address;
  basicvars.current++;		/* Skip LOMEM token */
  if (*basicvars.current!='=') error(ERR_EQMISS);
  basicvars.current++;
  address = basicvars.offbase+ALIGN(eval_integer());
  if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
  if (address<basicvars.top || address>=basicvars.himem)
    error(WARN_BADLOMEM);	/* Flag error (execution continues after this one) */
  else if (basicvars.procstack!=NIL)	/* Cannot alter LOMEM in a procedure */
    error(ERR_NOTINPROC);
  else {
    basicvars.lomem = basicvars.vartop = address;
    basicvars.stacklimit.bytesp = address+STACKBUFFER;
    clear_varlists();	/* Discard all variables and clear any references to */
    clear_strings();	/* them in the program */
    clear_heap();
    clear_varptrs();
  }
}

/*
** 'assign_mid' deals with the pseudo variable 'MID$('
*/
static void assign_mid(void) {
  int32 start, count;
  lvalue destination;
  stackitem stringtype;
  basicstring lhstring, rhstring;
  basicvars.current++;		/* Skip MID$( token */
  get_lvalue(&destination);		/* Fetch the destination address */
  if (destination.typeinfo!=VAR_STRINGDOL && destination.typeinfo!=VAR_DOLSTRPTR) error(ERR_TYPESTR);
  if (*basicvars.current!=',') error(ERR_COMISS);
  basicvars.current++;
  start = eval_integer();
  if (start<1) start = 1;
  if (*basicvars.current==',') {	/* Number of characters to be replaced is given */
    basicvars.current++;
    count = eval_integer();
    if (count<0)		/* If count is negative, treat it as if it was missing */
      count = MAXSTRING;
    else if (count==0) {	/* If count is zero, BBC Basic still replaces one char */
      count = 1;
    }
  }
  else {
    count = MAXSTRING;
  }
  if (*basicvars.current!=')') error(ERR_RPMISS);
  basicvars.current++;		/* Skip ')' */
  if (*basicvars.current!='=') error(ERR_EQMISS);
  basicvars.current++;
  expression();	/* Evaluate the RH side of the assignment */
  if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
  stringtype = GET_TOPITEM;
  if (stringtype!=STACK_STRING && stringtype!=STACK_STRTEMP) error(ERR_TYPESTR);
  rhstring = pop_string();
  if (destination.typeinfo==VAR_STRINGDOL)	/* Left-hand string is a string variable */
    lhstring = *destination.address.straddr;
  else {	/* Left-hand string is a '$<addr>' string, so fake a descriptor for it */
    lhstring.stringaddr = CAST(&basicvars.offbase[destination.address.offset], char *);
    lhstring.stringlen = get_stringlen(destination.address.offset);
  }
  if (start<=lhstring.stringlen) {	/* Only do anything if start position lies inside string */
    start-=1;		/* Change start position to an offset */
    if (count>rhstring.stringlen) count = rhstring.stringlen;
    if (start+count>lhstring.stringlen) count = lhstring.stringlen-start;
    if (count>0) memmove(lhstring.stringaddr+start, rhstring.stringaddr, count);
  }
  if (stringtype==STACK_STRTEMP) free_string(rhstring);
}

/*
** 'assign_page' is used to set the value of the pseudo variable 'PAGE'.
** The interpreter automatically issues the command 'new' after changing
** page so that the interpreter is in a well-defined state afterwards. This
** is not the way Acorn's interpreter works and means that it is not
** possible to keep several programs in memory and to switch between them
** adjusting 'PAGE'
*/
static void assign_page(void) {
  int32 offset;
  basicvars.current++;		/* Skip PAGE token */
  if (*basicvars.current!='=') error(ERR_EQMISS);
  basicvars.current++;
  offset = eval_integer();
  if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
  if (offset<0 || offset>=basicvars.worksize) {
    error(WARN_BADPAGE);	/* Flag error (execution continues after this one) */
    return;
  }
  basicvars.page = basicvars.offbase + offset;
  clear_program();	/* Issue 'NEW' to ensure everything is the way it should be */
}

/*
** 'assign_ptr' deals with the Basic 'PTR#x=' statement
*/
static void assign_ptr(void) {
  int32 handle, newplace;
  basicvars.current++;
  if (*basicvars.current!='#') error(ERR_HASHMISS);
  basicvars.current++;
  handle = eval_intfactor();
  if (*basicvars.current!='=') error(ERR_EQMISS);
  basicvars.current++;
  newplace = eval_integer();
  if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
  fileio_setptr(handle, newplace);
}

/*
** 'assign_right' deals with the 'RIGHT$(' pseudo variable
*/
static void assign_right(void) {
  int32 count;
  lvalue destination;
  stackitem stringtype;
  basicstring lhstring, rhstring;
  basicvars.current++;	/* Skip 'RIGHT$(' token */
  get_lvalue(&destination);		/* Fetch the destination address */
  if (destination.typeinfo!=VAR_STRINGDOL && destination.typeinfo!=VAR_DOLSTRPTR) error(ERR_TYPESTR);
  if (*basicvars.current==',') {	/* Number of characters to be replaced is given */
    basicvars.current++;
    count = eval_integer();
    if (count<0) count = 0;		/* If count is negative or zero, nothing is changed */
  }
  else {
    count = MAXSTRING;
  }
  if (*basicvars.current!=')') error(ERR_RPMISS);
  basicvars.current++;		/* Skip ')' token */
  if (*basicvars.current!='=') error(ERR_EQMISS);
  basicvars.current++;
  expression();	/* Evaluate the RH side of the assignment */
  if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
  stringtype = GET_TOPITEM;
  if (stringtype!=STACK_STRING && stringtype!=STACK_STRTEMP) error(ERR_TYPESTR);
  rhstring = pop_string();
  if (count>0) {	/* Only do anything if count is greater than zero */
    if (destination.typeinfo==VAR_STRINGDOL)	/* Left-hand string is a string variable */
      lhstring = *destination.address.straddr;
    else {	/* Left-hand string is a '$<addr>' string, so fake a descriptor for it */
      lhstring.stringaddr = CAST(&basicvars.offbase[destination.address.offset], char *);
      lhstring.stringlen = get_stringlen(destination.address.offset);
    }
    if (count>rhstring.stringlen) count = rhstring.stringlen;
    if (count<=lhstring.stringlen) memmove(lhstring.stringaddr+lhstring.stringlen-count, rhstring.stringaddr, count);
  }
  if (stringtype==STACK_STRTEMP) free_string(rhstring);
}

/*
** 'assign_time' deals with assignments to the pseudo variable 'TIME'
*/
static void assign_time(void) {
  int32 time;
  basicvars.current++;		/* Skip TIME token */
  if (*basicvars.current!='=') error(ERR_EQMISS);
  basicvars.current++;
  time = eval_integer();
  check_ateol();
  emulate_setime(time);
}

/*
** 'assign_timedol' deals with assignments to the pseudo variable 'TIME$'
*/
static void assign_timedol(void) {
  basicstring time;
  stackitem stringtype;
  basicvars.current++;		/* Skip TIME$ token */
  if (*basicvars.current!='=') error(ERR_EQMISS);
  basicvars.current++;
  expression();
  check_ateol();
  stringtype = GET_TOPITEM;
  if (stringtype!=STACK_STRING && stringtype!=STACK_STRTEMP) error(ERR_TYPESTR);
  time = pop_string();
  emulate_setimedol(tocstring(time.stringaddr, time.stringlen));
  if (stringtype==STACK_STRTEMP) free_string(time);
}

static void (*pseudovars[])(void) = {
  NIL, assign_himem, assign_ext, assign_filepath, assign_left, assign_lomem,
  assign_mid, assign_page, assign_ptr, assign_right, assign_time,
  assign_timedol
};

/*
** 'assign_pseudovar' controls the dispatch of the functions dealing
** with assignments to the pseudo variables
*/
void assign_pseudovar(void) {
  byte token;
  basicvars.current++;
  token = *basicvars.current;
  if (token>=TOKEN_HIMEM && token<=TOKEN_TIMEDOL)
    (*pseudovars[token])();	/* Dispatch an assignment to a pseudo variable */
  else if (token<=TOKEN_VPOS)	/* Function call on left hand side of assignment */
    error(ERR_SYNTAX);
  else {
    error(ERR_BROKEN, __LINE__, "assign");
  }
}

/*
** 'exec_let' interprets the Basic 'LET' statement
*/
void exec_let(void) {
  lvalue destination;
  basicvars.current++;		/* Skip LET token */
  get_lvalue(&destination);		/* Get left hand side of assignment */
  if (*basicvars.current=='=') {
    basicvars.current++;
    expression();
    (*assign_table[destination.typeinfo])(destination.address);
  }
  else {	/* No '=' found */
    error(ERR_EQMISS);
  }
}

