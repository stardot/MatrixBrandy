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
**	manangement. It also contains the code to deal with memory allocation
**	for strings
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "target.h"
#include "heap.h"
#include "basicdefs.h"
#include "errors.h"
#include "miscprocs.h"

#ifdef TARGET_RISCOS
#include "kernel.h"
#include "swis.h"
#endif

/*
** 'init_heap' is called when the interpreter starts to initialise the
** heap
*/
boolean init_heap(void) {
  basicvars.stringwork = malloc(MAXSTRING);
  return basicvars.stringwork!=NIL;
}

/*
** 'init_workspace' is called to obtain the memory used to hold the Basic
** program. 'heapsize' gives the size of block. If zero, the size of the
** area used is the implementation-defined default. If returns 'true' if
** if the heap space could be allocated or 'false' if it failed
**
** The base address for the byte offsets used by indirection operators
** (basicvars.offbase) is set up here. Normally the offset is from the
** start of the Basic workspace but under RISC OS it has to be from the
** start of memory otherwise the SYS statement does not work.
*/
boolean init_workspace(int32 heapsize) {
  byte *wp;
  if (heapsize==0)
    heapsize = DEFAULTSIZE;
  else if (heapsize<MINSIZE)
    heapsize = MINSIZE;
  else {
    heapsize = ALIGN(heapsize);
  }
  wp = malloc(heapsize);
  if (wp==NIL) heapsize = 0;	/* Could not obtain block of requested size */
  basicvars.worksize = heapsize;
  basicvars.page = basicvars.workspace = wp;
  basicvars.slotend = basicvars.end = basicvars.himem = wp+basicvars.worksize;
#ifdef TARGET_RISCOS
  basicvars.offbase = 0;
#else
  basicvars.offbase = wp;
#endif
/* Under RISC OS, find out the address of the end of wimp slot */
#ifdef TARGET_RISCOS
  {
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  oserror = _kernel_swi(OS_GetEnv, &regs, &regs);
/* OS_GetEnv returns the end of the wimp slot in R1 */
  if (oserror==NIL) basicvars.slotend = CAST(regs.r[1], byte *);
  }
#endif
  return wp!=NIL;
}

/*
** 'release_workspace' is called to return the Basic workspace to the operating
** system. It is used either when the program finishes or when the size of
** the workspace is being altered
*/
void release_workspace(void) {
  if (basicvars.workspace!=NIL) {
    free(basicvars.workspace);
    basicvars.workspace = NIL;
    basicvars.worksize = 0;
  }
}

/*
** 'release_heap' is called at the end of the interpreter's run to
** return all memory to the OS
*/
void release_heap(void) {
  library *lp, *lp2;
  lp = basicvars.installist;	/* Free memory acquired for installed libraries */
  while (lp!=NIL) {
    lp2 = lp->libflink;
    free(lp->libname);
    free(lp);
    lp = lp2;
  }
  release_workspace();
  free(basicvars.stringwork);
  if (basicvars.loadpath!=NIL) free(basicvars.loadpath);
}

/*
** 'allocmem' is called to allocate space for variables, arrays, strings
** and so forth. The memory between 'lomem' and 'stacklimit' is available
** for this
*/
void *allocmem(int32 size) {
  byte *newlimit;
  size = ALIGN(size);
  newlimit = basicvars.stacklimit.bytesp+size;
  if (newlimit>=basicvars.stacktop.bytesp) error(ERR_NOROOM);	/* Have run out of memory */
  basicvars.stacklimit.bytesp = newlimit;
  newlimit = basicvars.vartop;
  basicvars.vartop+=size;
  return newlimit;
}

/*
** 'condalloc' allocates memory in the same way as 'allocmem' except that it
** returns 'NIL' if the requested memory is not available to allow the calling
** function to deal with the error
*/
void *condalloc(int32 size) {
  byte *newlimit;
  size = ALIGN(size);
  newlimit = basicvars.stacklimit.bytesp+size;
  if (newlimit>=basicvars.stacktop.bytesp) return NIL;	/* Have run out of memory */
  basicvars.stacklimit.bytesp = newlimit;
  newlimit = basicvars.vartop;
  basicvars.vartop+=size;
  return newlimit;
}

/*
** 'freemem' is called to return memory to the heap.
** Note that this version of the code can only reclaim the memory if it
** was the last item allocated. It does not deal with returning memory
** to the middle of the heap. 'returnable' should be called to ensure
** that the memory can be returned
*/
void freemem(void *where, int32 size) {
  basicvars.vartop-=size;
  basicvars.stacklimit.bytesp-=size;
}

/*
** 'returnable' is called to check if the block at 'where' is the
** last item allocated on the heap and can therefore be returned
** to it
*/
boolean returnable(void *where, int32 size) {
  size = ALIGN(size);
  return CAST(where, byte *)+size==basicvars.vartop;
}

/*
** 'mark_basicheap' is called to save the pointer to the top of
** the Basic heap
*/
void mark_basicheap(void) {
  basicvars.lastvartop = basicvars.vartop;
}

void release_basicheap(void) {
  basicvars.vartop = basicvars.lastvartop;
  basicvars.stacklimit.bytesp = basicvars.vartop+STACKBUFFER;
}

/*
** 'clear_heap' is used to clear the variable and free string lists
** when a 'clear' command is used, a program is edited or 'new' or
** 'old' are issued.
*/
void clear_heap(void) {
  basicvars.vartop = basicvars.lomem;
  basicvars.stacklimit.bytesp = basicvars.lomem+STACKBUFFER;
}

