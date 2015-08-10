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
**	Types and constants used throughout the interpreter
*/

#ifndef __common_h
#define __common_h

#define FALSE 0
#define TRUE 1
#define NIL 0

#define MAXINTVAL 2147483647
#define SMALLCONST 256
#define MAXFLOATVAL 1.7976931348623157E+308
#define TINYFLOATVAL 2.2250738585072014E-308
#define MAXEXPONENT 308

#define MAXSTATELEN 1024	/* Maximum length of a tokenised line of Basic */
#define MINSTATELEN 7		/* Minimum legal length of a tokenised line */
#define MAXLINENO 65279		/* Highest line number allowed */
#define ENDLINENO 0xFF00	/* Line number value used to mark end of program */
#define INTSIZE 4		/* Size of an integer in bytes */
#define SMALLSIZE 1		/* Size of a small integer */
#define FLOATSIZE 8		/* Size of a floating point value */
#define STRINGSIZE 8		/* Size of a string descriptor block */
#define LOFFSIZE 4		/* Size of a long offset embedded in the code */
#define OFFSIZE 2		/* Size of a short offset embedded in the code */
#define LINESIZE 2		/* Size of a line number */
#define LENGTHSIZE 2		/* Size of the line length */
#define SIZESIZE 2		/* Size of string size embedded in the code */
#define MAXDIMS 10		/* Maximum number of array dimensions allowed */
#define MAXNAMELEN 256		/* Size of buffers used to hold variable names */

#define CR 0xD
#define LF 0xA
#define TAB '\t'
#define NUL '\0'
#define ESC 0x1B
#define VBAR 0x7C

#define BYTEMASK 0xFF
#define BYTESHIFT 8

typedef unsigned char byte;
typedef unsigned char boolean;

/* These macros hide type casts */

#define CAST(x,y) ((y)(x))
#define TOINT(x) ((int32)(x))
#define TOFLOAT(x) ((float64)(x))
#define TOSTRING(x) ((char *)(x))
#define TOINTADDR(x) ((int32 *)(x))

#endif
