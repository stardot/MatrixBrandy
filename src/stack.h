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
**	This file contains the definitions of functions concerned with
**	manipulating the Basic stack
*/

#ifndef __stack_h
#define __stack_h

#include "common.h"
#include "basicdefs.h"

extern void *alloc_stackmem(int32);
extern void *alloc_stackstrmem(int32);
extern void free_stackmem(void);
extern void free_stackstrmem(void);
extern void push_lvalue(int32, pointers);
extern void push_int(int32);
extern void push_float(float64);
extern void push_string(basicstring);
extern void push_strtemp(int32, char *);
extern void push_dolstring(int32, char *);
extern void push_array(basicarray *, int32);
extern void push_arraytemp(basicarray *, int32);
extern void push_pointer(void *);
extern void push_proc(char *, int32);
extern void push_fn(char *, int32);
extern void push_gosub(void);
extern void push_while(byte *);
extern void push_repeat(void);
extern void push_intfor(lvalue, byte *, int32, int32, boolean);
extern void push_floatfor(lvalue, byte *, float64, float64, boolean);
extern void push_data(byte *);
extern void push_error(errorblock);
extern int32 *make_opstack(void);
extern jmp_buf *make_restart(void);
extern stackitem get_topitem(void);
extern byte *get_stacktop(void);
extern byte *get_safestack(void);
extern boolean safestack(void);
extern lvalue pop_lvalue(void);
extern int32 pop_int(void);
extern float64 pop_float(void);
extern basicstring pop_string(void);
extern basicarray *pop_array(void);
extern basicarray pop_arraytemp(void);
extern fnprocinfo pop_proc(void);
extern fnprocinfo pop_fn(void);
extern gosubinfo pop_gosub(void);
extern stack_while *get_while(void);
extern stack_repeat *get_repeat(void);
extern stack_for *get_for(void);
extern void pop_while(void);
extern void pop_repeat(void);
extern void pop_for(void);
extern byte *pop_data(void);
extern errorblock pop_error(void);
extern void save_int(lvalue, int32);
extern void save_float(lvalue, float64);
extern void save_string(lvalue, basicstring);
extern void save_array(lvalue);
extern void save_retint(lvalue, lvalue, int32);
extern void save_retfloat(lvalue, lvalue, float64);
extern void save_retstring(lvalue, lvalue, basicstring);
extern void save_retarray(lvalue, lvalue);
extern void intdiv_int(int32);
extern void intmod_int(int32);
extern void restore(int32);
extern void restore_parameters(int32);
extern void check_stack(int32);
extern void empty_stack(stackitem);
extern void reset_stack(byte *);
extern void init_stack(void);
extern void clear_stack(void);
extern void *alloc_local(int32);

/* LARGEST_ENTRY is the size of the largest string or numeric entry on the stack */

#define LARGEST_ENTRY sizeof(basicstring)
#define ALIGNSIZE(type) (ALIGN(sizeof(type)))

/* The following macros are used to speed up operations on the Basic stack */

#define GET_TOPITEM (basicvars.stacktop.intsp->itemtype)

#define PUSH_INT(x) basicvars.stacktop.bytesp-=ALIGN(sizeof(stack_int)); \
		basicvars.stacktop.intsp->itemtype = STACK_INT; \
		basicvars.stacktop.intsp->intvalue = (x);
#define PUSH_FLOAT(x) basicvars.stacktop.bytesp-=ALIGN(sizeof(stack_float)); \
		basicvars.stacktop.floatsp->itemtype = STACK_FLOAT; \
		basicvars.stacktop.floatsp->floatvalue = (x);
#define PUSH_STRING(x) basicvars.stacktop.bytesp-=ALIGN(sizeof(stack_string)); \
		basicvars.stacktop.stringsp->itemtype = STACK_STRING; \
		basicvars.stacktop.stringsp->descriptor = (x);
#define INCR_INT(x) basicvars.stacktop.intsp->intvalue+=(x)
#define INCR_FLOAT(x) basicvars.stacktop.floatsp->floatvalue+=(x)
#define DECR_INT(x) basicvars.stacktop.intsp->intvalue-=(x)
#define DECR_FLOAT(x) basicvars.stacktop.floatsp->floatvalue-=(x)
#define INTDIV_INT(x) basicvars.stacktop.intsp->intvalue/=(x)
#define DIV_FLOAT(x) basicvars.stacktop.floatsp->floatvalue/=(x)
#define INTMOD_INT(x) basicvars.stacktop.intsp->intvalue%=(x)
#define LSL_INT(x) basicvars.stacktop.intsp->intvalue<<=(x)
#define ASR_INT(x) basicvars.stacktop.intsp->intvalue>>=(x)
#define AND_INT(x) basicvars.stacktop.intsp->intvalue&=(x)
#define OR_INT(x) basicvars.stacktop.intsp->intvalue|=(x)
#define EOR_INT(x) basicvars.stacktop.intsp->intvalue^=(x)
#define NEGATE_INT basicvars.stacktop.intsp->intvalue = -basicvars.stacktop.intsp->intvalue
#define NEGATE_FLOAT basicvars.stacktop.floatsp->floatvalue = -basicvars.stacktop.floatsp->floatvalue
#define NOT_INT basicvars.stacktop.intsp->intvalue = ~basicvars.stacktop.intsp->intvalue
#define ABS_INT basicvars.stacktop.intsp->intvalue = abs(basicvars.stacktop.intsp->intvalue)
#define ABS_FLOAT basicvars.stacktop.floatsp->floatvalue = fabs(basicvars.stacktop.floatsp->floatvalue)
#define CPEQ_INT(x) basicvars.stacktop.intsp->intvalue = \
		(basicvars.stacktop.intsp->intvalue==(x) ? BASTRUE : BASFALSE)
#define CPNE_INT(x) basicvars.stacktop.intsp->intvalue = \
		(basicvars.stacktop.intsp->intvalue!=(x) ? BASTRUE : BASFALSE)
#define CPGT_INT(x) basicvars.stacktop.intsp->intvalue = \
		(basicvars.stacktop.intsp->intvalue>(x) ? BASTRUE : BASFALSE)
#define CPLT_INT(x) basicvars.stacktop.intsp->intvalue = \
		(basicvars.stacktop.intsp->intvalue<(x) ? BASTRUE : BASFALSE)
#define CPGE_INT(x) basicvars.stacktop.intsp->intvalue = \
		(basicvars.stacktop.intsp->intvalue>=(x) ? BASTRUE : BASFALSE)
#define CPLE_INT(x) basicvars.stacktop.intsp->intvalue = \
		(basicvars.stacktop.intsp->intvalue<=(x) ? BASTRUE : BASFALSE)

/*
** If the debug version of the code is being used, replace some
** of the macros with calls to their equivalent functions as these
** produce debugging output if the DEBUG macro is defined
*/
#ifdef DEBUG
#undef PUSH_INT
#undef PUSH_FLOAT
#undef PUSH_STRING
#define PUSH_INT(x) push_int(x)
#define PUSH_FLOAT(x) push_float(x)
#define PUSH_STRING(x) push_string(x)
#endif

#endif

