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
**	This file defines the functions that handle all the Basic
**	statement types apart from assignments and I/O statements
*/

#ifndef __mainstate_h
#define __mainstate_h

extern void exec_assembler(void);
extern void exec_asmend(void);
extern void exec_oscmd(void);
extern void exec_call(void);
extern void exec_case(void);
extern void exec_xcase(void);
extern void exec_chain(void);
extern void exec_clear(void);
extern void exec_data(void);
extern void exec_def(void);
extern void exec_dim(void);
extern void exec_elsewhen(void);
extern void exec_xelse(void);
extern void exec_xlhelse(void);
extern void exec_end(void);
extern void exec_endifcase(void);
extern void exec_endproc(void);
extern void exec_fnreturn(void);
extern void exec_endwhile(void);
extern void exec_error(void);
extern void exec_for(void);
extern void exec_gosub(void);
extern void exec_goto(void);
extern void exec_blockif(void);
extern void exec_singlif(void);
extern void exec_xif(void);
extern void exec_let(void);
extern void exec_library(void);
extern void exec_local(void);
extern void exec_next(void);
extern void exec_on(void);
extern void exec_oscli(void);
extern void exec_overlay(void);
extern void exec_proc(void);
extern void exec_xproc(void);
extern void exec_quit(void);
extern void exec_read(void);
extern void exec_repeat(void);
extern void exec_report(void);
extern void exec_restore(void);
extern void exec_return(void);
extern void exec_run(void);
extern void exec_stop(void);
extern void exec_swap(void);
extern void exec_sys(void);
extern void exec_trace(void);
extern void exec_until(void);
extern void exec_wait(void);
extern void exec_xwhen(void);
extern void exec_while(void);

#endif
