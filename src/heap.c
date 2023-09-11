/*
** This file is part of the Matrix Brandy Basic VI Interpreter.
** Copyright (C) 2000-2014 David Daniels
** Copyright (C) 2018-2021 Michael McConnell and contributors
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

/* Linux memory allocator based on Richard Russell's allocator
** from BBCSDL as it aims to get memory with low addresses, and within
** the first 1TB on 64-bit hardware.
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

#ifdef TARGET_LINUX
#ifndef __USE_LARGEFILE64
#define __USE_LARGEFILE64
#endif
#include <sys/mman.h>
#endif

#ifdef TARGET_RISCOS
#include "kernel.h"
#include "swis.h"
#endif

#if defined(TARGET_LINUX) && defined(__LP64__)
static void *mymap (size_t size)
{
  FILE *fp;
  char line[256] ;
  void *start, *finish, *base = (void *) 0x400000 ;

  fp = fopen ("/proc/self/maps", "r") ;
  if (fp == NULL)
    return NULL ;

  while (NULL != fgets (line, 256, fp)) {
    sscanf (line, "%p-%p", &start, &finish) ;
    start = (void *)((size_t)start & -0x1000) ; // page align (GCC extension)
    if (start >= (base + size)) {
      fclose(fp);
      return base ;
    }
    if (finish > (void *)0xFFFFF000) {
      fclose(fp);
      return NULL ;
    }
    base = (void *)(((size_t)finish + 0xFFF) & -0x1000) ; // page align
    if (base > ((void *)0xFFFFFFFF - size)) {
      fclose(fp);
      return NULL ;
    }
  }
  fclose(fp);
  return base ;
}
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
*/
boolean init_workspace(size_t heapsize) {
  byte *wp = NULL;
#if defined(TARGET_LINUX) && defined(__LP64__)
  void *base = NULL;
  uint32 heaporig;
#endif

  basicvars.misc_flags.usedmmap = 0;
  if (heapsize==0)
    heapsize = DEFAULTSIZE;
  else if (heapsize<MINSIZE)
    heapsize = MINSIZE;
  else if (heapsize>0xFFFFFC00ull)
    heapsize = 0xFFFFFC00ull;
  else {
    heapsize = ALIGN(heapsize);
  }
#if defined(TARGET_LINUX) && defined(__LP64__)
  heaporig = heapsize;
  basicvars.misc_flags.usedmmap = 1;
#ifdef DEBUG
#  ifdef MATRIX64BIT
  fprintf(stderr, "heap.c:init_workspace: Requested heapsize is %ld (&%lX)\n", heapsize, heapsize);
#  else
  fprintf(stderr, "heap.c:init_workspace: Requested heapsize is %d (&%X)\n", heapsize, heapsize);
#  endif
#endif
  base = mymap(heapsize);
  if (base != NULL) {
#ifdef DEBUG
#  ifdef MATRIX64BIT
    fprintf(stderr, "heap.c:init_workspace: Allocating at %p, size &%lX\n", base, heapsize);
#  else
    fprintf(stderr, "heap.c:init_workspace: Allocating at %p, size &%X\n", base, heapsize);
#  endif
#endif
    wp = mmap64(base, heapsize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) ;
#ifdef DEBUG
    fprintf(stderr, "heap.c:init_workspace: mmap returns %p\n", wp);
#endif
    if ((size_t)wp == -1) {
      heapsize=heaporig;
      wp=malloc(heapsize);
#ifdef DEBUG
      fprintf(stderr, "heap.c:init_workspace: Fallback, malloc returns %p\n", wp);
#endif
      basicvars.misc_flags.usedmmap = 0;
    }
  } else {
    /* Trying to allocate via mmap didn't work, let's try malloc instead */
    wp=malloc(heapsize);
    basicvars.misc_flags.usedmmap = 0;
  }
#else
  wp = malloc(heapsize);
#endif

  if (wp==NIL) heapsize = 0;			/* Could not obtain block of requested size */
  basicvars.worksize = heapsize;
  basicvars.workspace = wp;
  basicvars.slotend = basicvars.end = basicvars.himem = wp+basicvars.worksize;
  basicvars.memory = 0;				/* Use as a byte array to access arbitrary points of memory */
  basicvars.page = wp;
  basicvars.memdump_lastaddr = (size_t)wp;

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
#if defined(TARGET_LINUX) && defined(__LP64__)
    if (basicvars.misc_flags.usedmmap)
      munmap(basicvars.workspace, basicvars.worksize);
    else
#endif
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
** for this. The second parameter is a flag, set to 1 for traditional
** allocmem behaviour of reporting an error, or 0 for old condalloc
** behaviour of returning NIL upon an error to allow the calling function
** to deal with the error.
*/
void *allocmem(size_t size, boolean reporterror) {
  byte *newlimit;
  size = ALIGN(size);
  newlimit = basicvars.stacklimit.bytesp+size;
  if (newlimit>=basicvars.stacktop.bytesp) {	/* Have run out of memory */
    if (reporterror) error(ERR_NOROOM);
    else return NIL;
  }
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
** 'clear_heap' is used to clear the variable and free string lists
** when a 'clear' command is used, a program is edited or 'new' or
** 'old' are issued.
*/
void clear_heap(void) {
  basicvars.vartop = basicvars.lomem;
  basicvars.stacklimit.bytesp = basicvars.lomem+STACKBUFFER;
}

