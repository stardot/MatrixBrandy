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
**	This file declares the file I/O routines for the interpreter
*/

#ifndef __fileio_h
#define __fileio_h

extern boolean isapath(char *);

extern void init_fileio(void);
extern int32 fileio_openin(char *, int32);
extern int32 fileio_openout(char *, int32);
extern int32 fileio_openup(char *, int32);
extern void fileio_close(int32);
extern int32 fileio_bget(int32);
extern int32 fileio_getdol(int32, char *);
extern void fileio_getnumber(int32, boolean *, int32 *, float64 *);
extern int32 fileio_getstring(int32, char *);
extern void fileio_bput(int32, int32);
extern void fileio_bputstr(int32, char *, int32);
extern void fileio_printint(int32, int32);
extern void fileio_printfloat(int32, float64);
extern void fileio_printstring(int32, char *, int32);
extern int32 fileio_eof(int32);
extern int32 fileio_getptr(int32);
extern void fileio_setptr(int32, int32);
extern int32 fileio_getext(int32);
extern void fileio_setext(int32, int32);
extern void fileio_shutdown(void);

#endif
