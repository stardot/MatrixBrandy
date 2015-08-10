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
**	This file contains definitions for functions that deal
**	with the built-in Basic functions
*/

#ifndef __functions_h
#define __functions_h

extern void exec_function(void);
extern void init_functions(void);

/*
** The following functions are invoked from the factor function
** table as they have one byte tokens. Most of them are tokens
** that can be used as statements as well as functions, for
** example, MODE
*/
extern void fn_width(void);
extern void fn_vdu(void);
extern void fn_true(void);
extern void fn_trace(void);
extern void fn_top(void);
extern void fn_tint(void);
extern void fn_quit(void);
extern void fn_not(void);
extern void fn_mode(void);
extern void fn_mod(void);
extern void fn_false(void);
extern void fn_end(void);
extern void fn_dim(void);
extern void fn_colour(void);
extern void fn_beat(void);

#endif
