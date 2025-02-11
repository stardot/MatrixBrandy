/*
** This file is part of the Matrix Brandy Basic VI Interpreter.
** Copyright (C) 2000-2014 David Daniels
** Copyright (C) 2018-2024 Michael McConnell and contributors
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
**      This file defines the functions and so forth associated with memory
**      manangement
*/

#ifndef __heap_h
#define __heap_h

#include "common.h"

#define STACKBUFFER 256         /* Minimum space allowed between Basic's stack and variables */

extern boolean init_heap(void);
extern void release_heap(void);
extern boolean init_workspace(size_t);
extern void release_workspace(void);
extern void *allocmem(size_t, boolean);
extern void freemem(void *, int32);
extern void clear_heap(void);

/*
** 'returnable' is called to check if the block at 'where' is the
** last item allocated on the heap and can therefore be returned
** to it.
*/
#define returnable(x,y) (CAST(x, byte *)+ALIGN(y)==basicvars.vartop)

#endif
