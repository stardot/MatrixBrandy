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
**	This file contains various functions that are used to manipulate the
**	Basic stack
*/

#include <string.h>
#include "common.h"
#include "target.h"
#include "basicdefs.h"
#include "stack.h"
#include "miscprocs.h"
#include "strings.h"
#include "tokens.h"
#include "errors.h"

#ifdef DEBUG
#include <stdio.h>
#endif

/*
** Stack overflow
** --------------
** The interpreter tries to be clever about checking for stack overflow.
** Whilst the functions that add control blocks to the stack, for example,
** for 'WHILE' statements, explicitly check for stack overflow, functions
** that add numeric and string values do not. The only time the code needs
** to check for overflow in these cases is when adding an extra value to
** the stack because it has found an operator of a higher priority than
** the last one it saw. There will be one entry on the Basic stack for each
** entry on the operator stack, so as the operator stack is of a fixed
** size, it is only necessary to check that the Basic stack will hold that
** many entries. The stack limit is also set a little way above the Basic
** heap so that the stack can be extended beyond that point (by about half
** a dozen entries) without causing any damage.
*/

/*
** 'entrysize' gives the size of each type of entry possible on the
** basic stack
*/
static int32 entrysize [] = {
  0, 0, ALIGNSIZE(stack_int), ALIGNSIZE(stack_float),
  ALIGNSIZE(stack_string), ALIGNSIZE(stack_string), ALIGNSIZE(stack_array),
  ALIGNSIZE(stack_arraytemp), ALIGNSIZE(stack_array), ALIGNSIZE(stack_arraytemp),
  ALIGNSIZE(stack_array), ALIGNSIZE(stack_arraytemp), ALIGNSIZE(stack_locarray),
  ALIGNSIZE(stack_locarray), ALIGNSIZE(stack_gosub), ALIGNSIZE(stack_proc),
  ALIGNSIZE(stack_fn), ALIGNSIZE(stack_local), ALIGNSIZE(stack_retparm),
  ALIGNSIZE(stack_while), ALIGNSIZE(stack_repeat), ALIGNSIZE(stack_for),
  ALIGNSIZE(stack_for), ALIGNSIZE(stack_error), ALIGNSIZE(stack_data),
  ALIGNSIZE(stack_opstack), ALIGNSIZE(stack_restart)
};

/*
** This table says which types of entries can be simply discarded
** from the Basic stack
*/
static boolean disposible [] = {
  FALSE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE,
  TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, FALSE,
  FALSE, FALSE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE
};

#ifdef DEBUG

static char *stackentries [] = {	/* Stack entry type names */
  "<unknown>", "lvalue", "integer", "floating point", "string",
  "temporary string", "integer array", "temp integer array",
  "floating poing array", "temp floating point array", "string array",
  "temp string array", "local array", "local string array", "gosub",
  "PROC", "FN", "local variable", "return parameter", "WHILE", "REPEAT",
  "integer FOR", "floating point FOR", "ON ERROR", "DATA", "operator stack",
  "longjmp block"
};

static char entry [50];

static char *entryname(stackitem what) {
  if (what <= STACK_RESTART)
    return stackentries[what];
  else {
    sprintf(entry, "** Bad type %d **", what);
    return entry;
  }
}

void dump(byte *sp) {
  int m, *ip;
  m = 4;
  fprintf(stderr, "sp = %8p  ", sp);
  for (ip = (int *)(sp-32); ip < (int *)(sp+288); ip++) {
    if (m == 4) fprintf(stderr, "\n%8p  ", ip), m = 0;
    fprintf(stderr, "%08x ", *ip);
    m++;
  }
  fprintf(stderr, "\n");
}

#endif

/*
** 'check_stack' is called to check that there is enough room on the
** Basic stack to add another 'count' numeric or string items to it.
** If gives up on the spot if this would cause stack overflow
*/
void check_stack(int32 count) {
  if (basicvars.stacktop.bytesp-count*LARGEST_ENTRY<basicvars.stacklimit.bytesp) error(ERR_STACKFULL);
}

/*
** 'safestack' returns TRUE if it is safe to move the Basic stack.
** At the moment this is only allowed if the stack is empty, that is,
** the only thing on it is the operator stack and the program is not
** in a procedure or function
*/
boolean safestack(void) {
  return basicvars.procstack==NIL && basicvars.stacktop.intsp->itemtype==STACK_OPSTACK;
}

/*
** 'make_opstack' is called to create a new operator stack. It also
** checks that there is enough room on the Basic stack to hold
** 'OPSTACKSIZE' numeric or string entries. It returns a pointer to
** the base of the stack
*/
int32 *make_opstack(void) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_opstack);
  if (basicvars.stacktop.bytesp-OPSTACKSIZE*LARGEST_ENTRY<basicvars.stacklimit.bytesp) error(ERR_STACKFULL);
  basicvars.stacktop.opstacksp->itemtype = STACK_OPSTACK;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Create operator stack at %p\n", basicvars.stacktop.bytesp);
#endif
  return &basicvars.stacktop.opstacksp->opstack[0];
}

/*
** 'make_restart' creates an entry on the Basic stack for the
** environment block used by 'longjmp' when handling errors when
** an 'ON ERROR LOCAL' has been executed. It returns a pointer to
** the block for the longjmp's 'jmp_buf' structure
*/
jmp_buf *make_restart(void) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_restart);
  if (basicvars.stacktop.bytesp<basicvars.stacklimit.bytesp) error(ERR_STACKFULL);
  basicvars.stacktop.restartsp->itemtype = STACK_RESTART;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Create restart block at %p\n", basicvars.stacktop.bytesp);
#endif
  return &basicvars.stacktop.restartsp->restart;
}

/*
** 'get_topitem' returns the type of the top item on the Basic stack
*/
stackitem get_topitem(void) {
  return basicvars.stacktop.intsp->itemtype;
}

/*
** 'get_stacktop' returns the current value of the Basic stack pointer
*/
byte *get_stacktop(void) {
  return basicvars.stacktop.bytesp;
}

/*
** 'get_safestack' returns the value that the stack pointer is set
** to after an error to restore the stack to a known condition
*/
byte *get_safestack(void) {
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Get safestack = %p\n", basicvars.safestack.bytesp);
#endif
  return basicvars.safestack.bytesp;
}


/*
** 'push_int' pushes an integer value on to the Basic stack
*/
void push_int(int32 x) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_int);
  basicvars.stacktop.intsp->itemtype = STACK_INT;
  basicvars.stacktop.intsp->intvalue = x;
#ifdef DEBUG
  if (basicvars.debug_flags.allstack) fprintf(stderr, "Push integer value on to stack at %p, value %d\n", basicvars.stacktop.intsp, x);
#endif
}

/*
** 'push_float' pushes a floating point value on to the Basic stack
*/
void push_float(float64 x) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_float);
  basicvars.stacktop.floatsp->itemtype = STACK_FLOAT;
  basicvars.stacktop.floatsp->floatvalue = x;
#ifdef DEBUG
  if (basicvars.debug_flags.allstack) fprintf(stderr, "Push floating point value on to stack at %p, value %g\n", basicvars.stacktop.floatsp, x);
#endif
}

/*
** 'push_string' copies a string descriptor on to the Basic stack
*/
void push_string(basicstring x) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_string);
  basicvars.stacktop.stringsp->itemtype = STACK_STRING;
  basicvars.stacktop.stringsp->descriptor = x;
#ifdef DEBUG
  if (basicvars.debug_flags.allstack) fprintf(stderr, "Push string value on to stack at %p, address %p, length %d\n",
   basicvars.stacktop.stringsp, x.stringaddr, x.stringlen);
#endif
}

/*
** 'push_strtemp' creates a string descriptor on the Basic stack for an
** 'intermediate value' string, that is, a string created as a result of a
** string operation such as 'STRING$'.
*/
void push_strtemp(int32 stringlen, char *stringaddr) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_string);
  basicvars.stacktop.stringsp->itemtype = STACK_STRTEMP;
  basicvars.stacktop.stringsp->descriptor.stringlen = stringlen;
  basicvars.stacktop.stringsp->descriptor.stringaddr = stringaddr;
#ifdef DEBUG
  if (basicvars.debug_flags.allstack) fprintf(stderr, "Push string temp on to stack at %p, address %p, length %d\n",
   basicvars.stacktop.stringsp, stringaddr, stringlen);
#endif
}

/*
** 'push_dolstring' is called to push a reference to a '$<string>'
** type of string on to the Basic stack. It creates a descriptor
** for the string and copies that on to the stack
*/
void push_dolstring(int32 strlength, char *strtext) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_string);
  basicvars.stacktop.stringsp->itemtype = STACK_STRING;
  basicvars.stacktop.stringsp->descriptor.stringlen = strlength;
  basicvars.stacktop.stringsp->descriptor.stringaddr = strtext;
#ifdef DEBUG
  if (basicvars.debug_flags.allstack) fprintf(stderr, "Push $<string> string on to stack at %p, address %p, length %d\n",
   basicvars.stacktop.stringsp, strtext, strlength);
#endif
}

static stackitem arraytype [] = {	/* Variable type -> array type */
  STACK_UNKNOWN, STACK_UNKNOWN, STACK_INTARRAY, STACK_FLOATARRAY,
  STACK_STRARRAY, STACK_UNKNOWN, STACK_UNKNOWN, STACK_UNKNOWN
};

static stackitem arraytemptype [] = {	/* Variable type -> temporary array type */
  STACK_UNKNOWN, STACK_UNKNOWN, STACK_IATEMP, STACK_FATEMP,
  STACK_SATEMP, STACK_UNKNOWN, STACK_UNKNOWN, STACK_UNKNOWN
};

/*
** 'push_array' pushes a pointer to an array descriptor on to the Basic stack
*/
void push_array(basicarray *descriptor, int32 type) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_array);
  basicvars.stacktop.arraysp->itemtype = arraytype[type & TYPEMASK];
  basicvars.stacktop.arraysp->descriptor = descriptor;
#ifdef DEBUG
  if (basicvars.debug_flags.allstack) fprintf(stderr, "Push array descriptor block at %p\n", basicvars.stacktop.arraysp);
#endif
}

/*
** 'push_arraytemp' pushes a descriptor for a temporary array on
** to the Basic stack. As this is a temporary array, the entire
** descriptor is copied on to the stack rather than just a pointer
** to it
*/
void push_arraytemp(basicarray *descriptor, int32 type) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_arraytemp);
  basicvars.stacktop.arraytempsp->itemtype = arraytemptype[type & TYPEMASK];
  basicvars.stacktop.arraytempsp->descriptor = *descriptor;
#ifdef DEBUG
  if (basicvars.debug_flags.allstack) fprintf(stderr, "Push temp array descriptor block at %p\n", basicvars.stacktop.arraytempsp);
#endif
}

/*
** 'push_proc' pushes the return address and so forth for a procedure call
*/
void push_proc(char *name, int32 count) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_proc);
  if (basicvars.stacktop.bytesp<basicvars.stacklimit.bytesp) error(ERR_STACKFULL);
  basicvars.stacktop.procsp->itemtype = STACK_PROC;
  basicvars.stacktop.procsp->fnprocblock.lastcall = basicvars.procstack;
  basicvars.stacktop.procsp->fnprocblock.retaddr = basicvars.current;
  basicvars.stacktop.procsp->fnprocblock.parmcount = count;
  basicvars.stacktop.procsp->fnprocblock.fnprocname = name;
  basicvars.procstack = &basicvars.stacktop.procsp->fnprocblock;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Saving PROC return block at %p\n", basicvars.stacktop.procsp);
#endif
}

/*
** 'push_fn' pushes the return address and so forth for a function call
*/
void push_fn(char *name, int32 count) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_fn);
  if (basicvars.stacktop.bytesp<basicvars.stacklimit.bytesp) error(ERR_STACKFULL);
  basicvars.stacktop.fnsp->itemtype = STACK_FN;
  basicvars.stacktop.fnsp->lastopstop = basicvars.opstop;
  basicvars.stacktop.fnsp->lastopstlimit = basicvars.opstlimit;
  basicvars.stacktop.fnsp->lastrestart = basicvars.local_restart;
  basicvars.stacktop.fnsp->fnprocblock.lastcall = basicvars.procstack;
  basicvars.stacktop.fnsp->fnprocblock.retaddr = basicvars.current;
  basicvars.stacktop.fnsp->fnprocblock.parmcount = count;
  basicvars.stacktop.fnsp->fnprocblock.fnprocname = name;
  basicvars.procstack = &basicvars.stacktop.fnsp->fnprocblock;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Saving FN return block at %p\n", basicvars.stacktop.fnsp);
#endif
}

/*
** 'push_gosub' pushes a 'GOSUB' return block on to the Basic stack
*/
void push_gosub(void) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_gosub);
  if (basicvars.stacktop.bytesp<basicvars.stacklimit.bytesp) error(ERR_STACKFULL);
  basicvars.stacktop.gosubsp->itemtype = STACK_GOSUB;
  basicvars.stacktop.gosubsp->gosublock.lastcall = basicvars.gosubstack;
  basicvars.stacktop.gosubsp->gosublock.retaddr = basicvars.current;
  basicvars.gosubstack = &basicvars.stacktop.gosubsp->gosublock;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Saving GOSUB return block at %p\n", basicvars.stacktop.gosubsp);
#endif
}

/*
** 'alloc_stackmem' is called to allocate a block of memory on the
** Basic stack. It is used to acquire memory for local arrays. The
** space is automatically reclaimed when the procedure or function
** call ends. It returns a pointer to the area of memory allocated
** or NIL if there is not enough room for the block.
** **NOTE** It is up to the calling function to trap the error if
** this function returns NIL.
*/
void *alloc_stackmem(int32 size) {
  byte *p, *base;
  size = ALIGN(size);
  base = basicvars.stacktop.bytesp-size;
  p = base-ALIGNSIZE(stack_locarray);
  if (p<basicvars.stacklimit.bytesp) return NIL;	/* Bail out if there is no room */
  basicvars.stacktop.bytesp = p;	/* Reset the stack pointer */
  basicvars.stacktop.locarraysp->itemtype = STACK_LOCARRAY;
  basicvars.stacktop.locarraysp->arraysize = size;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "ALlocate memory on stack at %p, size=%d\n", p, size);
#endif
  return base;
}

/*
** 'alloc_stackstrmem' is called to allocate a block of memory on
** the Basic stack for a string array. It returns a pointer to the
** array or NIL if there was no memory available.
** **NOTE** It is up to the calling function to trap the error if
** this function returns NIL.
*/
void *alloc_stackstrmem(int32 size) {
  void *p = alloc_stackmem(size);
  if (p==NIL) return NIL;
  basicvars.stacktop.locarraysp->itemtype = STACK_LOCSTRING;
  return p;
}

/*
** 'free_stackmem' reclaims the stack space used for temporary array
*/
void free_stackmem(void) {
    basicvars.stacktop.bytesp+=ALIGNSIZE(stack_locarray)+basicvars.stacktop.locarraysp->arraysize;
}

/*
** 'free_stackstrmem' reclaims the stack space used for temporary string array
*/
void free_stackstrmem(void) {
    discard_strings(basicvars.stacktop.bytesp+ALIGNSIZE(stack_locarray), basicvars.stacktop.locarraysp->arraysize);
    basicvars.stacktop.bytesp+=ALIGNSIZE(stack_locarray)+basicvars.stacktop.locarraysp->arraysize;
}

/*
** 'push_while' creates a control block on the Basic stack for a 'WHILE' loop
*/
void push_while(byte *expr) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_while);
  if (basicvars.stacktop.bytesp<basicvars.stacklimit.bytesp) error(ERR_STACKFULL);
  basicvars.stacktop.whilesp->itemtype = STACK_WHILE;
  basicvars.stacktop.whilesp->whilexpr = expr;
  basicvars.stacktop.whilesp->whileaddr = basicvars.current;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Create 'WHILE' block at %p\n", basicvars.stacktop.whilesp);
#endif
}

/*
** 'push_repeat' creates a control block on the Basic stack for a 'REPEAT' loop
*/
void push_repeat(void) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_repeat);
  if (basicvars.stacktop.bytesp<basicvars.stacklimit.bytesp) error(ERR_STACKFULL);
  basicvars.stacktop.repeatsp->itemtype = STACK_REPEAT;
  basicvars.stacktop.repeatsp->repeataddr = basicvars.current;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Create 'REPEAT' block at %p\n", basicvars.stacktop.repeatsp);
#endif
}

/*
** 'push_intfor' creates a control block on the Basic stack for a 'FOR'
** loop with an integer control variable
*/
void push_intfor(lvalue forvar, byte *foraddr, int32 limit, int32 step, boolean simple) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_for);
  if (basicvars.stacktop.bytesp<basicvars.stacklimit.bytesp) error(ERR_STACKFULL);
  basicvars.stacktop.forsp->itemtype = STACK_INTFOR;
  basicvars.stacktop.forsp->simplefor = simple;
  basicvars.stacktop.forsp->forvar = forvar;
  basicvars.stacktop.forsp->foraddr = foraddr;
  basicvars.stacktop.forsp->fortype.intfor.intlimit = limit;
  basicvars.stacktop.forsp->fortype.intfor.intstep = step;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Create integer 'FOR' block at %p\n", basicvars.stacktop.forsp);
#endif
}

/*
** 'push_floatfor' creates a control block on the Basic stack for a 'FOR'
** loop with a floating point control variable
*/
void push_floatfor(lvalue forvar, byte *foraddr, float64 limit, float64 step, boolean simple) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_for);
  if (basicvars.stacktop.bytesp<basicvars.stacklimit.bytesp) error(ERR_STACKFULL);
  basicvars.stacktop.forsp->itemtype = STACK_FLOATFOR;
  basicvars.stacktop.forsp->simplefor = simple;
  basicvars.stacktop.forsp->forvar = forvar;
  basicvars.stacktop.forsp->foraddr = foraddr;
  basicvars.stacktop.forsp->fortype.floatfor.floatlimit = limit;
  basicvars.stacktop.forsp->fortype.floatfor.floatstep = step;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Create floating point 'FOR' block at %p\n", basicvars.stacktop.forsp);
#endif
}

/*
** 'push_data' is called to save the current value of the 'DATA'
** pointer on the Basic stack
*/
void push_data(byte *address) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_data);
  if (basicvars.stacktop.bytesp<basicvars.stacklimit.bytesp) error(ERR_STACKFULL);
  basicvars.stacktop.datasp->itemtype = STACK_DATA;
  basicvars.stacktop.datasp->address = address;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Create saved 'DATA' block at %p\n", basicvars.stacktop.datasp);
#endif
}

/*
** 'push_error' creates a control block on the stack for a Basic
** error handler
*/
void push_error(errorblock handler) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_error);
  if (basicvars.stacktop.bytesp<basicvars.stacklimit.bytesp) error(ERR_STACKFULL);
  basicvars.stacktop.errorsp->itemtype = STACK_ERROR;
  basicvars.stacktop.errorsp->handler = handler;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Create saved 'ON ERROR' block at %p\n", basicvars.stacktop.errorsp);
#endif
}

/*
** 'save_int' saves an integer value on the stack. It is used when
** dealing with local variables
*/
void save_int(lvalue details, int32 value) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_local);
  if (basicvars.stacktop.bytesp<basicvars.stacklimit.bytesp) error(ERR_STACKFULL);
  basicvars.stacktop.localsp->itemtype = STACK_LOCAL;
  basicvars.stacktop.localsp->savedetails = details;
  basicvars.stacktop.localsp->value.savedint = value;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "LOCAL variable - saving integer from %p at %p\n",
   details.address.intaddr, basicvars.stacktop.localsp);
#endif
}

/*
** 'save_float' saves a floating point value on the stack. It is used when
** dealing with local variables
*/
void save_float(lvalue details, float64 floatvalue) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_local);
  if (basicvars.stacktop.bytesp<basicvars.stacklimit.bytesp) error(ERR_STACKFULL);
  basicvars.stacktop.localsp->itemtype = STACK_LOCAL;
  basicvars.stacktop.localsp->savedetails = details;
  basicvars.stacktop.localsp->value.savedfloat = floatvalue;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "LOCAL variable - saving floating point value from %p at %p\n",
   details.address.floataddr, basicvars.stacktop.localsp);
#endif
}

/*
** 'save_string' saves a string descriptor on the stack. It is used when
** dealing with local variables.
** Note that the string descriptor is passed separately as the address
** given in 'details' as the home of the string descriptor is in fact the
** address of the string itself in the case of '$<string>' type strings.
** In this case the descriptor represents the place at which the string
** has been saved
*/
void save_string(lvalue details, basicstring thestring) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_local);
  if (basicvars.stacktop.bytesp<basicvars.stacklimit.bytesp) error(ERR_STACKFULL);
  basicvars.stacktop.localsp->itemtype = STACK_LOCAL;
  basicvars.stacktop.localsp->savedetails = details;
  basicvars.stacktop.localsp->value.savedstring = thestring;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "LOCAL variable - saving string from %p at %p\n",
   details.address.straddr, basicvars.stacktop.localsp);
#endif
}

/*
** save_array is called to save an array descriptor on the stack when
* creating a local array
*/
void save_array(lvalue details) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_local);
  if (basicvars.stacktop.bytesp<basicvars.stacklimit.bytesp) error(ERR_STACKFULL);
  basicvars.stacktop.localsp->itemtype = STACK_LOCAL;
  basicvars.stacktop.localsp->savedetails = details;
  basicvars.stacktop.localsp->value.savedarray = *details.address.arrayaddr;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "LOCAL variable - saving array dimensions from %p at %p\n",
   details.address.arrayaddr, basicvars.stacktop.localsp);
#endif
}

/*
** 'save_retint' is called to set up the control block on the stack for a
** 'RETURN' type PROC/FN parameter where the parameter is an integer.
** 'retaddr' details the place were the return value is to be saved,
** 'details' and 'value' refer to the variable that will be used for
** the value in the procedure
*/
void save_retint(lvalue retdetails, lvalue details, int32 value) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_retparm);
  if (basicvars.stacktop.bytesp<basicvars.stacklimit.bytesp) error(ERR_STACKFULL);
  basicvars.stacktop.retparmsp->itemtype = STACK_RETPARM;
  basicvars.stacktop.retparmsp->retdetails = retdetails;
  basicvars.stacktop.retparmsp->savedetails = details;
  basicvars.stacktop.retparmsp->value.savedint = value;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Saving integer variable from %p at %p\n",
   details.address.intaddr, basicvars.stacktop.retparmsp);
#endif
}

/*
** 'save_retfloat' sets up the control block on the stack for a floating point
**'RETURN' type PROC/FN parameter
*/
void save_retfloat(lvalue retdetails, lvalue details, float64 floatvalue) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_retparm);
  if (basicvars.stacktop.bytesp<basicvars.stacklimit.bytesp) error(ERR_STACKFULL);
  basicvars.stacktop.retparmsp->itemtype = STACK_RETPARM;
  basicvars.stacktop.retparmsp->retdetails = retdetails;
  basicvars.stacktop.retparmsp->savedetails = details;
  basicvars.stacktop.retparmsp->value.savedfloat = floatvalue;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Saving floating point variable from %p at %p\n",
   details.address.floataddr, basicvars.stacktop.retparmsp);
#endif
}

/*
** 'save_retstring' sets up the control block on the Basic stack for a
** string 'RETURN' type PROC/FN parameter.
** Note that the string descriptor is passed separately as the address
** given in 'details' as the home of the string descriptor is in fact the
** address of the string itself in the case of '$<string>' type strings.
** In this case the descriptor represents the place at which the string
** has been saved
*/
void save_retstring(lvalue retdetails, lvalue details, basicstring thestring) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_retparm);
  if (basicvars.stacktop.bytesp<basicvars.stacklimit.bytesp) error(ERR_STACKFULL);
  basicvars.stacktop.retparmsp->itemtype = STACK_RETPARM;
  basicvars.stacktop.retparmsp->retdetails = retdetails;
  basicvars.stacktop.retparmsp->savedetails = details;
  basicvars.stacktop.retparmsp->value.savedstring = thestring;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Saving string variable from %p at %p\n",
   details.address.straddr, basicvars.stacktop.retparmsp);
#endif
}

void save_retarray(lvalue retdetails, lvalue details) {
  basicvars.stacktop.bytesp-=ALIGNSIZE(stack_retparm);
  if (basicvars.stacktop.bytesp<basicvars.stacklimit.bytesp) error(ERR_STACKFULL);
  basicvars.stacktop.retparmsp->itemtype = STACK_RETPARM;
  basicvars.stacktop.retparmsp->retdetails = retdetails;
  basicvars.stacktop.retparmsp->savedetails = details;
  basicvars.stacktop.retparmsp->value.savedarray = *details.address.arrayaddr;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Saving array dimensions from %p at %p\n",
   details.address.arrayaddr, basicvars.stacktop.retparmsp);
#endif
}

/*
** 'restore_retparm' is called when a 'return parameter' block is found on the
** stack. It saves the value currently in the parameter at the address stored as
** the return parameter address and then returns the local variable to its
** correct value
*/
void restore_retparm(int32 parmcount) {
  stack_retparm *p;
  int32 vartype = 0, intvalue = 0;
  float64 floatvalue = 0.0;
  basicstring stringvalue = {0, NULL};
  p = basicvars.stacktop.retparmsp;	/* Not needed, but the code is unreadable otherwise */
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Restoring RETURN variable at %p from %p, return dest=%p\n",
   p->savedetails.address.intaddr, p, p->retdetails.address.intaddr);
#endif
  basicvars.stacktop.retparmsp++;
  switch (p->savedetails.typeinfo & PARMTYPEMASK) {	/* Fetch value from local variable and restore local var */
  case VAR_INTWORD:	/* Integer variable */
    intvalue = *p->savedetails.address.intaddr;		/* Fetch current value of local variable */
    *p->savedetails.address.intaddr = p->value.savedint;	/* Restore local variable to its old value */
    vartype = VAR_INTWORD;
    break;
  case VAR_FLOAT:	/* Floating point variable */
    floatvalue = *p->savedetails.address.floataddr;
    *p->savedetails.address.floataddr = p->value.savedfloat;
    vartype = VAR_FLOAT;
    break;
  case VAR_STRINGDOL:	/* String variable */
    stringvalue = *p->savedetails.address.straddr;
    *p->savedetails.address.straddr = p->value.savedstring;
    vartype = VAR_STRINGDOL;
    break;
  case VAR_INTBYTEPTR:	/* Indirect byte integer variable */
    intvalue = basicvars.offbase[p->savedetails.address.offset];
    basicvars.offbase[p->savedetails.address.offset] = p->value.savedint;
    vartype = VAR_INTWORD;
    break;
  case VAR_INTWORDPTR:	/* Indirect word integer variable */
    intvalue = get_integer(p->savedetails.address.offset);
    store_integer(p->savedetails.address.offset, p->value.savedint);
    vartype = VAR_INTWORD;
    break;
  case VAR_FLOATPTR:		/* Indirect floating point variable */
    floatvalue = get_float(p->savedetails.address.offset);
    store_float(p->savedetails.address.offset, p->value.savedfloat);
    vartype = VAR_FLOAT;
    break;
  case VAR_DOLSTRPTR:		/* Indirect string variable */
    intvalue = stringvalue.stringlen = get_stringlen(p->savedetails.address.offset);
    stringvalue.stringaddr = alloc_string(intvalue);
    if (intvalue>0) memmove(stringvalue.stringaddr, &basicvars.offbase[p->savedetails.address.offset], intvalue);
    memmove(&basicvars.offbase[p->savedetails.address.offset], p->value.savedstring.stringaddr, p->value.savedstring.stringlen);
    free_string(p->value.savedstring);		/* Discard saved copy of original '$ string' */
    vartype = VAR_DOLSTRPTR;
    break;
  case VAR_INTARRAY: case VAR_FLOATARRAY: case VAR_STRARRAY:	/* Array - Do nothing */
    break;
  default:
    error(ERR_BROKEN, __LINE__, "stack");
  }

/* Now restore the next parameter */

  parmcount--;
  if (parmcount>0) {	/* There are still some parameters to do */
    if (basicvars.stacktop.intsp->itemtype==STACK_LOCAL)
      restore(parmcount);
    else {	/* Must be a return parameter */
      restore_retparm(parmcount);
    }
  }

/* Now we can store the returned value in original variable */

  switch (p->retdetails.typeinfo) {
  case VAR_INTWORD:
    *p->retdetails.address.intaddr = vartype==VAR_INTWORD ? intvalue : TOINT(floatvalue);
    break;
  case VAR_FLOAT:
    *p->retdetails.address.floataddr = vartype==VAR_INTWORD ? TOFLOAT(intvalue) : floatvalue;
    break;
  case VAR_STRINGDOL:
    free_string(*p->retdetails.address.straddr);
    *p->retdetails.address.straddr = stringvalue;
    break;
  case VAR_INTBYTEPTR:
    basicvars.offbase[p->retdetails.address.offset] = vartype==VAR_INTWORD ? intvalue : TOINT(floatvalue);
    break;
  case VAR_INTWORDPTR:
    store_integer(p->retdetails.address.offset, vartype==VAR_INTWORD ? intvalue : TOINT(floatvalue));
    break;
  case VAR_FLOATPTR:
    store_float(p->retdetails.address.offset, vartype==VAR_INTWORD ? TOFLOAT(intvalue) : floatvalue);
    break;
  case VAR_DOLSTRPTR:
    if (stringvalue.stringlen>0) memmove(&basicvars.offbase[p->retdetails.address.offset], stringvalue.stringaddr, stringvalue.stringlen);
    if (vartype==VAR_STRINGDOL) {	/* Local var was a normal string variable */
      basicvars.offbase[p->retdetails.address.offset+stringvalue.stringlen] = CR;	/* So add a 'CR' at the end of the string */
    }
    free_string(stringvalue);
    break;
  case VAR_INTARRAY: case VAR_FLOATARRAY: case VAR_STRARRAY:	/* 'RETURN' dest is array - Do nothing */
    break;
  default:
    error(ERR_BROKEN, __LINE__, "stack");
  }
}

/*
** 'restore' is called to restore a variable to its saved value.
*/
void restore(int32 parmcount) {
  stack_local *p;
  stackitem localitem;
  do {
    p = basicvars.stacktop.localsp;
    basicvars.stacktop.bytesp+=ALIGNSIZE(stack_local);
#ifdef DEBUG
    if (basicvars.debug_flags.stack) fprintf(stderr, "Restoring variable at %p from %p\n", p->savedetails.address.intaddr, p);
#endif
    if (p->savedetails.typeinfo==VAR_INTWORD)	/* Deal with most common case first */
      *p->savedetails.address.intaddr = p->value.savedint;
    else {
      switch (p->savedetails.typeinfo & PARMTYPEMASK) {
      case VAR_FLOAT:
        *p->savedetails.address.floataddr = p->value.savedfloat;
        break;
      case VAR_STRINGDOL:
        free_string(*p->savedetails.address.straddr);
        *p->savedetails.address.straddr = p->value.savedstring;
        break;
      case VAR_INTBYTEPTR:
        basicvars.offbase[p->savedetails.address.offset] = p->value.savedint;
        break;
      case VAR_INTWORDPTR:
        store_integer(p->savedetails.address.offset, p->value.savedint);
        break;
      case VAR_FLOATPTR:
        store_float(p->savedetails.address.offset, p->value.savedfloat);
        break;
      case VAR_DOLSTRPTR:
        memmove(&basicvars.offbase[p->savedetails.address.offset], p->value.savedstring.stringaddr, p->value.savedstring.stringlen);
        free_string(p->value.savedstring);
        break;
      case VAR_INTARRAY: case VAR_FLOATARRAY: case VAR_STRARRAY:
        *p->savedetails.address.arrayaddr = p->value.savedarray;
        break;
      default:
        error(ERR_BROKEN, __LINE__, "stack");
      }
    }

/* Now restore the next parameter */

    parmcount--;
    localitem = GET_TOPITEM;
  } while (parmcount>0 && localitem==STACK_LOCAL);
  if (parmcount>0 && localitem==STACK_RETPARM) restore_retparm(parmcount);
}

/*
** 'restore_parameters' is called when returning from a procedure or
** function to restore its parameters to their original values. The
** number of parameters to be dealt with is given by 'parmcount'.
*/
void restore_parameters(int32 parmcount) {
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Restoring PROC/FN parameters\n");
#endif
  if (basicvars.stacktop.intsp->itemtype==STACK_LOCAL)
    restore(parmcount);
  else {
    restore_retparm(parmcount);
  }
}

/*
** 'pop_int' pops an integer from the Basic stack
*/
int32 pop_int(void) {
  stack_int *p = basicvars.stacktop.intsp;
#ifdef DEBUG
  if (basicvars.debug_flags.allstack) fprintf(stderr, "Pop integer from stack at %p, value %d\n",
   p, p->intvalue);
#endif
  basicvars.stacktop.bytesp+=ALIGNSIZE(stack_int);
  return p->intvalue;
}

/*
** 'pop_float' pops a floating point value from the Basic stack
*/
float64 pop_float(void) {
  stack_float *p = basicvars.stacktop.floatsp;
#ifdef DEBUG
  if (basicvars.debug_flags.allstack) fprintf(stderr, "Pop floating point value from stack at %p, value %g\n",
   p, p->floatvalue);
#endif
  basicvars.stacktop.bytesp+=ALIGNSIZE(stack_float);
  return p->floatvalue;
}

/*
** 'pop_string' pops a string descriptor from the Basic stack
*/
basicstring pop_string(void) {
  stack_string *p = basicvars.stacktop.stringsp;
#ifdef DEBUG
  if (basicvars.debug_flags.allstack) fprintf(stderr, "Pop string from stack at %p, address %p, length %d\n",
   p, p->descriptor.stringaddr, p->descriptor.stringlen);
#endif
  basicvars.stacktop.bytesp+=ALIGNSIZE(stack_string);
  return p->descriptor;
}

/*
** 'pop_array' returns a pointer to an array descriptor that has
** been saved on the stack
*/
basicarray *pop_array(void) {
  stack_array *p = basicvars.stacktop.arraysp;
#ifdef DEBUG
  if (basicvars.debug_flags.allstack) fprintf(stderr, "Pop array block at %p\n", p);
#endif
  basicvars.stacktop.bytesp+=ALIGNSIZE(stack_array);
  return p->descriptor;
}

/*
** 'pop_arraytemp' removes a temporary array descriptor from thr
** Basic stack and returns it to the calling program
*/
basicarray pop_arraytemp(void) {
  stack_arraytemp *p = basicvars.stacktop.arraytempsp;
#ifdef DEBUG
  if (basicvars.debug_flags.allstack) fprintf(stderr, "Pop temporary array block at %p\n", p);
#endif
  basicvars.stacktop.bytesp+=ALIGNSIZE(stack_arraytemp);
  return p->descriptor;
}

/*
** 'pop_proc' removes a procedure return control block from the Basic
** stack, updating the procedure/function call chain as well
*/
fnprocinfo pop_proc(void) {
  stack_proc *p = basicvars.stacktop.procsp;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Discard 'PROC' block at %p\n", p);
#endif
  basicvars.procstack = p->fnprocblock.lastcall;
  basicvars.stacktop.bytesp+=ALIGNSIZE(stack_proc);
  return p->fnprocblock;
}

/*
** 'pop_fn' removes a function return control block from the Basic stack,
** updating the procedure/function call chain as well
*/
fnprocinfo pop_fn(void) {
  stack_fn *p = basicvars.stacktop.fnsp;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Discard 'FN' block at %p, restart = %p\n", p, p->lastrestart);
#endif
  basicvars.opstop = p->lastopstop;
  basicvars.opstlimit = p->lastopstlimit;
  basicvars.local_restart = p->lastrestart;
  basicvars.procstack = p->fnprocblock.lastcall;
  basicvars.stacktop.bytesp+=ALIGNSIZE(stack_fn);
  return p->fnprocblock;
}

/*
** 'pop_gosub' removes a GOSUB return control block from the Basic stack.
** It updates the GOSUB call chain as well
*/
gosubinfo pop_gosub(void) {
  stack_gosub *p = basicvars.stacktop.gosubsp;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Discard 'GOSUB' block at %p\n", p);
#endif
  basicvars.gosubstack = p->gosublock.lastcall;
  basicvars.stacktop.bytesp+=ALIGNSIZE(stack_gosub);
  return p->gosublock;
}

/*
** 'discard' removes an item from the Basic stack, carrying out any
** work needed to undo the effects of that item
*/
static void discard(stackitem item) {
  basicstring temp;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Drop '%s' entry at %p\n",
   entryname(item), basicvars.stacktop.bytesp);
#endif
  switch(item) {
  case STACK_STRTEMP:
    temp = pop_string();
    free_string(temp);
    break;
  case STACK_LOCAL: 	/* Restore local variable to its old value */
    restore(1);
    break;
  case STACK_RETPARM:	/* Deal with a 'return' parameter and restore local parameter */
    restore_retparm(1);
    break;
  case STACK_GOSUB:	/* Clear 'GOSUB' block from stack */
    (void) pop_gosub();
    break;
  case STACK_PROC:	/* Clear 'PROC' block */
    (void) pop_proc();
    break;
  case STACK_FN:	/* Clear 'FN' block */
    (void) pop_fn();
    break;
  case STACK_ERROR:	/* Restore old Basic error handler */
    basicvars.error_handler = pop_error();
    break;
  case STACK_DATA:	/* Restore old Basic data pointer */
    basicvars.datacur = pop_data();
    break;
  case STACK_LOCARRAY:	/* Local numeric array */
    basicvars.stacktop.bytesp+=entrysize[STACK_LOCARRAY]+basicvars.stacktop.locarraysp->arraysize;
    break;
  case STACK_LOCSTRING:	/* Local string array */
    discard_strings(basicvars.stacktop.bytesp+entrysize[STACK_LOCARRAY], basicvars.stacktop.locarraysp->arraysize);
    basicvars.stacktop.bytesp+=entrysize[STACK_LOCARRAY]+basicvars.stacktop.locarraysp->arraysize;
    break;
  default:
    if (item==STACK_UNKNOWN || item>=STACK_HIGHEST) error(ERR_BROKEN, __LINE__, "stack");
    basicvars.stacktop.bytesp+=entrysize[item];
  }
}

/*
** 'get_while' returns a pointer to the first 'WHILE' block it finds on
** the Basic stack or 'NIL' if it cannot find one.
*/
stack_while *get_while(void) {
  stackitem item;
  item = basicvars.stacktop.whilesp->itemtype;
  while (disposible[item]) {
    discard(item);
    item = basicvars.stacktop.whilesp->itemtype;
    if (item==STACK_WHILE) return basicvars.stacktop.whilesp;
  }
  return NIL;
}

/*
** 'pop_while' discards a 'WHILE' block from the top of the Basic stack
*/
void pop_while(void) {
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Discard 'WHILE' block at %p\n", basicvars.stacktop.whilesp);
#endif
  basicvars.stacktop.bytesp+=ALIGNSIZE(stack_while);
}

/*
** 'get_repeat' returns a pointer to the first 'REPEAT' block it finds
** on the Basic stack or 'NIL' if it cannot find one. Note that some
** types of entry on the stack can be silently discarded after undoing
** any effects they had, for example, error handler addresses stored in
** 'ERROR'-type entries are restored to the saved values.
*/
stack_repeat *get_repeat(void) {
  stackitem item;
  item = basicvars.stacktop.repeatsp->itemtype;
  while (disposible[item]) {
    discard(item);
    item = basicvars.stacktop.repeatsp->itemtype;
    if (item==STACK_REPEAT) return basicvars.stacktop.repeatsp;
  }
  return NIL;
}

/*
** 'pop_repeat' discards a 'REPEAT' block from the top of the Basic stack
*/
void pop_repeat(void) {
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Discard 'REPEAT' block at %p\n", basicvars.stacktop.repeatsp);
#endif
  basicvars.stacktop.bytesp+=ALIGNSIZE(stack_repeat);
}

/*
** 'get_for' returns a pointer to the first 'FOR' block it can find on
** Basic stack, discarding entries from the stack as it goes. It returns
** 'NIL' if it cannot find one or comes across a stack entry type that
** it cannot just throw away
*/
stack_for *get_for(void) {
  stackitem item;
  item = basicvars.stacktop.forsp->itemtype;
  while (disposible[item]) {
    discard(item);
    item = basicvars.stacktop.forsp->itemtype;
    if (item==STACK_INTFOR || item==STACK_FLOATFOR) return basicvars.stacktop.forsp;
  }
  return NIL;
}

/*
** 'pop_for' discards a 'FOR' block from the top of the Basic stack
*/
void pop_for(void) {
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Discard 'FOR' block at %p\n", basicvars.stacktop.forsp);
#endif
  basicvars.stacktop.bytesp+=ALIGNSIZE(stack_for);
}

/*
** 'pop_data' removes a stored 'DATA' pointer value from the stack
** and returns the value of the pointer
*/
byte *pop_data(void) {
  stack_data *p = basicvars.stacktop.datasp;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Discard 'DATA' block at %p\n", p);
#endif
  basicvars.stacktop.bytesp+=ALIGNSIZE(stack_data);
  return p->address;
}

/*
** 'pop_error' removes an 'ON ERROR' control block from the stack
** and returns the error block it contained
*/
errorblock pop_error(void) {
  stack_error *p = basicvars.stacktop.errorsp;
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Discard 'ERROR' block at %p\n", p);
#endif
  basicvars.stacktop.bytesp+=ALIGNSIZE(stack_error);
  return p->handler;
}

/*
** 'empty_stack' is called to clear entries from the Basic stack until one of
** the desired type is found. This is used when returning from a procedure,
** function or subroutine. It is assumed that the calling function has already
** checked that the top item on the stack is not a 'return' block of the
** required sort. (This should be the most common case)
*/
void empty_stack(stackitem required) {
  do
    discard(GET_TOPITEM);
  while (GET_TOPITEM!=required);
}

/*
** 'reset_stack' is called to restore the Basic stack pointer to a known,
** safe value after an error has occured. Entries on the stack are
** discarded and their effects undone if necessary as far as 'newstacktop'
*/
void reset_stack(byte *newstacktop) {
  while (basicvars.stacktop.bytesp < newstacktop) {
#ifdef debug
    if (basicvar.debug_flags.stack) fprintf(stderr, "Reset stack - Discard entry at %p\n",
     basicvars.stacktop.bytesp);
#endif
    discard(GET_TOPITEM);
  }
  if (basicvars.stacktop.bytesp != basicvars.safestack.bytesp
   && basicvars.stacktop.bytesp != newstacktop) {
    basicvars.stacktop.bytesp = basicvars.safestack.bytesp;
    error(ERR_BROKEN, __LINE__, "stack");
  }
}

/*
** 'init_stack' is called to completely initialise the Basic stack
** when the interpreter starts running or when the 'new' command is
** used
*/
void init_stack(void) {
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Initialise stack %p\n", basicvars.himem);
#endif
  basicvars.stacktop.bytesp = basicvars.himem;
  basicvars.stacktop.intsp--;
  basicvars.stacktop.intsp->itemtype = STACK_UNKNOWN;
  basicvars.stacktop.intsp->intvalue = 0x504f5453;
  basicvars.safestack.bytesp = basicvars.stacktop.bytesp;
}

/*
** 'clear_stack' is called to discard everything on the stack. This
** includes the operator stack (which exists as a stack within a
** stack), so beware!
*/
void clear_stack(void) {
#ifdef DEBUG
  if (basicvars.debug_flags.stack) fprintf(stderr, "Clear stack to %p\n", basicvars.safestack.bytesp);
#endif 
  basicvars.stacktop.bytesp = basicvars.safestack.bytesp;
  basicvars.procstack = NIL;
  basicvars.gosubstack = NIL;
}
