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
**	This file defines functions associated with the editing of Basic
**	programs as well as loading them from and saving them to disk
*/

#ifndef __editor_h
#define __editor_h

#include "common.h"

#define LOAD_LIBRARY TRUE
#define INSTALL_LIBRARY FALSE

extern void mark_end(byte *);
extern void edit_line(void);
extern void delete_range(int32, int32);
extern void clear_program(void);
extern void clear_tables(void);
extern void read_basic(char *);
extern void write_basic(char *);
extern void read_library(char *, boolean);
extern void write_text(char *);
extern boolean validate_program(void);
extern boolean recover_program(void);
extern void renumber_program(byte *, int32, int32);

#endif
