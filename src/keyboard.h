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
**	This file defines the keyboard handling routines
*/

#ifndef __keyboard_h
#define __keyboard_h

#include "common.h"

typedef enum {READ_OK, READ_ESC, READ_EOF} readstate;

extern void purge_keys(void);
extern int32 emulate_get(void);
extern int32 read_key(void);
extern int32 emulate_inkey(int32);
extern readstate emulate_readline(char [], int32);
extern void set_fn_string(int key, char *string, int length);
extern boolean init_keyboard(void);
extern void end_keyboard(void);

#endif
