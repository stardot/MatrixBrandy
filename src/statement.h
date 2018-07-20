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
**	Functions in the main interpreter module
*/

#ifndef __statement_h
#define __statement_h

#include "common.h"
#include "basicdefs.h"

#define STRINGOK FALSE
#define NOSTRING TRUE

extern byte ateol[];

extern void init_interpreter(void);
extern void exec_thisline(void);
extern void exec_statements(byte *);
extern void exec_fnstatements(byte *);
extern void run_program(byte *);
extern void trace_line(int32);
extern void trace_proc(char *, boolean);
extern void trace_branch(byte *, byte *);
extern boolean isateol(byte *);
extern void check_ateol(void);
extern void bad_token(void);
extern void bad_syntax(void);
extern void store_value(lvalue, int32, boolean);
extern void end_run(void);

#endif

