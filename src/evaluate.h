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
**	This file contains definitions for the interpreter's expression
**	evaluation code
*/

#ifndef __expressions_h
#define __expressions_h

#include "common.h"
#include "basicdefs.h"

extern void (*factor_table[256])(void);

extern boolean check_arrays(basicarray *, basicarray *);
extern int32 get_integer(int32);
extern float64 get_float(int32);
extern void expression(void);
extern void factor(void);
extern int32 eval_integer(void);
extern int32 eval_intfactor(void);
extern void push_parameters(fnprocdef *, char *);
extern void init_expressions(void);
extern void reset_opstack(void);

#endif
