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
**	Miscellaneous functions
*/

#ifndef __miscprocs_h
#define __miscprocs_h

#include "common.h"
#include "basicdefs.h"

extern boolean read_line(char [], int32);
extern boolean amend_line(char [], int32);
extern boolean isidstart(char);
extern boolean isidchar(char);
extern boolean isident(byte);
extern void check_read(int32, int32);
extern void check_write(int32, int32);
extern int32 get_integer(int32);
extern float64 get_float(int32);
extern void store_integer(int32, int32);
extern void store_float(int32, float64);
extern byte *alignaddr(byte *);
extern char *skip_blanks(char *);
extern byte *skip(byte *);
extern char *tocstring(char *, int32);
extern byte *find_line(int32);
extern byte *find_linestart(byte *);
extern library *find_library(byte *);
extern void show_byte(int32, int32);
extern void show_word(int32, int32);
extern void save_current(void);
extern void restore_current(void);
extern boolean secure_tmpnam(char []);

#define ISIDSTART(ch) (isalpha(ch) || ch=='_' || ch=='`')
#define ISIDCHAR(ch) (isalnum(ch) || ch=='_' || ch=='`')

#endif
