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
**	This file defines the functions and so forth associated with memory
**	manangement
*/

#ifndef __heap_h
#define __heap_h

#include "common.h"

#define STACKBUFFER 256		/* Minimum space allowed between Basic's stack and variables */

extern boolean init_heap(void);
extern void release_heap(void);
extern boolean init_workspace(int32);
extern void release_workspace(void);
extern void *allocmem(int32);
extern void *condalloc(int32);
extern boolean returnable(void *, int32);
extern void freemem(void *, int32);
extern void clear_heap(void);
extern void mark_basicheap(void);
extern void release_basicheap(void);

#endif
