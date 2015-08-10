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
**	This file defines the functions and so forth associated with
**	memory manangement for strings
*/

#include <string.h>
#include <stdlib.h>
#include "common.h"
#include "target.h"
#include "basicdefs.h"
#include "strings.h"
#include "heap.h"
#include "errors.h"

/* #define DEBUG */

#ifdef DEBUG
#include <stdio.h>
#endif

/*
** The string memory management is based around a series of 'bins' in which
** are kept free strings of different lengths, a bin for each length. There
** are bins for string lengths ranging from four to 64K bytes. The array
** 'binsizes' gives the string lengths for each bin. The emphasis is on
** dealing with short strings (up to 128 bytes) with about two thirds of
** the bins being for short strings. There is no reason why the number of
** bins could not be increased to improve memory usage nor why the maximum
** length string could be set higher. (The only problem here is that the
** interpreter uses a string workspace of the maximum string length and
** increasing the maximum beyond, say, a megabyte, would probably be
** impractical.
** The allocation strategy is as follows:
** 1)  Search the bin for a string of the required size
** 2)  If the bin is empty acquire a block directly from the Basic heap.
** 3)  If that fails then search the free string list and use the first
**	one that fits. The unused potion of the block is returned to
**	either one of the bins or the free string list, depending on its
**	size.
** 4)  If nothing can be found in step 3), try to merge free blocks
**	and start again from step 1).
** 5)  If there is still nothing available give up.
**	(Actually it would be possible to check one more place to see
**	if there is any memory left. The bins for longer string sizes
**	could be checked but if steps 1) to 4) fail to produce anything
**	it seems unlikely that this will.)
**
** In this module, string lengths are referred to by the number of the bin
** that corresponds to that length.
*/

#define SHORTLIMIT 128			/* Largest 'short' string */
#define MEDLIMIT 1024			/* Largest 'medium' string */

#define SHORTGRAIN (sizeof(int))	/* Difference between each 'short' string length */
#define MEDGRAIN 128			/* Difference between each 'medium' string length */

#define SHORTBINS ((SHORTLIMIT/SHORTGRAIN)+1)	/* Number of bins for short strings (+1 as range is 0..128) */
#define MEDSTART SHORTBINS		/* Index of first 'medium' bin entry */
#define MEDBINS ((MEDLIMIT/MEDGRAIN)-1)	/* Number of bins for medium strings (-1 as range is 256..1024) */
#define LONGSTART (SHORTBINS+MEDBINS)	/* Index of first 'long' bin entry */
#define BINCOUNT 46			/* Number of bins */

typedef struct heapblock {
  struct heapblock *blockflink;		/* Next block in list */
  int32 blocksize;			/* Size of heap block (Use only in free list) */
} heapblock;

typedef struct {
  heapblock *freestart;			/* Address of a free string */
  int32 freesize;			/* Size of free string */
} freeblock;

#ifdef DEBUG
  static int32 allocated;		/* Number of bytes allocated */
  static int32 created[BINCOUNT];	/* Number of times string of this size has been created */
  static int32 reused[BINCOUNT];	/* Number of times strings in bins have been reused */
  static int32 allocations[BINCOUNT];	/* Number of times string of this size has been allocated */

#endif

static int32 freestrings;		/* Number of free strings in bins */
static heapblock *binlists[BINCOUNT];	/* Free memory block bins */
static heapblock *freelist;		/* List of free blocks not in bins */

static int32 binsizes[BINCOUNT] = {	/* Bin number -> string size */
/* short strings */
   0,  4,  8, 12, 16, 20, 24, 28,  32,  36,  40,  44,  48,  52,  56,  60, 64,
  68, 72, 76, 80, 84, 88, 92, 96, 100, 104, 108, 112, 116, 120, 124, 128,
/* Now the medium strings */
  256, 384, 512, 640, 768, 896, 1024,
/* Finally the long strings */
  2048, 4096, 8192, 16384, 32768, 65536
};

char emptystring;	/* All requests for zero bytes point here */

static boolean collect(void);	/* Forward reference */

/*
** 'find_bin' returns the bin number used to hold strings of length
** 'size'. There is no error checking here and so functions that call
** this one must ensure that the string length is in range
*/
static int32 find_bin(int size) {
  if (size<=SHORTLIMIT)		/* Size<=128 bytes (including zero) */
    return (size+SHORTGRAIN-1)/SHORTGRAIN;
  else if (size<=MEDLIMIT)
    return (size+MEDGRAIN-1)/MEDGRAIN+MEDSTART-2;	/* -2 as there are no 0 and 128 byte medium strings */
  else {	/* Search through larger size string bins */
    int n = LONGSTART;
    do {
      if (binsizes[n]>=size) return n;	/* Found wanted string bin size */
      n+=1;
    } while (n<BINCOUNT);
    error(ERR_BROKEN, __LINE__, "strings");		/* Sanity check - String size is too long */
  }
  return 0;	/* Should never be executed */
}

/*
** 'alloc_string' is called to allocate memory for a string. The
** function returns a pointer to the memory allocated. Note that
** requests for zero bytes are allowed, and the address returned
** will point to a valid memory location ('emptystring').
*/
void *alloc_string(int32 size) {
  int32 bin, unused;
  heapblock *p, *last;
  boolean reclaimed;
  if (size==0) return &emptystring;
  basicvars.runflags.has_variables = TRUE;
  bin = find_bin(size);
  reclaimed = FALSE;
  do {
    if (binlists[bin]!=NIL) {	/* Found something usable in a bin */
      p = binlists[bin];
      binlists[bin] = p->blockflink;
      freestrings-=1;
#ifdef DEBUG
      reused[bin]+=1;
      allocations[bin]+=1;
      if (basicvars.debug_flags.strings) fprintf(stderr, "Allocate string at %p, length %d bytes\n", p, binsizes[size]);
#endif
      return p;
    }

/* There was nothing in the bin. Try grabbing more memory from the heap */

    size = binsizes[bin];  	/* Get string size for bin 'bin' */
    p = condalloc(size);
    if (p!=NIL) {		/* Allocated block from heap successfully */
#ifdef DEBUG
      allocated+=size;
      created[bin]+=1;
      allocations[bin]+=1;
      if (basicvars.debug_flags.strings) fprintf(stderr, "Allocate string at %p, length %d bytes\n", p, size);
#endif
      return p;
    }

/* The heap is exhausted. Try the free block list */

    p = freelist;
    last = NIL;
    while (p!=NIL && p->blocksize<size) {	/* Look for first block big enough */
      last = p;
      p = p->blockflink;
    }
    if (p!=NIL) {	/* Found some memory that can be used */
      unused = p->blocksize-size;	/* Find out how much of block will be left */
      if (unused<=SHORTLIMIT) {		/* Remove entire block from the free list */
        if (last==NIL)	/* Block was first in list */
          freelist = p->blockflink;
        else {
          last->blockflink = p->blockflink;
        }
        freestrings-=1;
        if (unused>0) {	/* If anything is left from block, put it in a bin */
          basicstring descriptor;
          descriptor.stringaddr = CAST(p, char *)+size;
          descriptor.stringlen = unused;
          free_string(descriptor);
        }
      }
      else {	/* Use part of block. Return unused portion to free list */
        heapblock *up;
        up = CAST(CAST(p, char *)+size, heapblock *);
        up->blockflink = p->blockflink;
        up->blocksize = unused;
        if (last==NIL)
          freelist = up;
        else {
          last->blockflink = up;
        }
      }
#ifdef DEBUG
      allocations[bin]+=1;
      if (basicvars.debug_flags.strings) fprintf(stderr, "Allocate string at %p, length %d bytes\n", p, size);
#endif
      return p;
    }

/*
** The free list was empty too. Attempt to reclaim some memory. Note
** that 'reclaimed' will be set to 'TRUE' if we have already passed
** this way once on this call, indicating that we have already tried
** to reclaim some string memory but that there is still not enough
** available to meet the current request
*/

    if (reclaimed || !collect()) error(ERR_NOROOM);	/* Fail if 'collect' does not achieve anything */
    reclaimed = TRUE;
  } while (TRUE);
  return NIL;		/* Will never be executed */
}

/*
** 'free_string' returns the block at 'hp' to one of the string heap bins.
** The free blocks are arranged in ascending order of address
*/
void free_string(basicstring descriptor) {
  heapblock *hp, *hp2;
  int32 size, bin;
  size = descriptor.stringlen;
#ifdef DEBUG
  if (basicvars.debug_flags.strings) fprintf(stderr, "Free string at %p, length %d bytes\n",
   descriptor.stringaddr, size);
#endif
  if (size==0) return;	/* Null string - Nothing to return */
  hp = CAST(descriptor.stringaddr, heapblock *);
  bin = find_bin(size);
  hp2 = binlists[bin];
  if (hp2==NIL || hp<hp2) {	/* New first element in list */
    hp->blockflink = hp2;
    binlists[bin] = hp;
  }
  else {	/* Add block somewhere in the middle of the list */
    heapblock *last;
    do {
      last = hp2;
      hp2 = hp2->blockflink;
    } while (hp2!=NIL && hp>hp2);
    hp->blockflink = last->blockflink;
    last->blockflink = hp;
  }
  freestrings+=1;	/* Bump up number of free strings */
}

/*
** 'discard_strings' is called to dispose of all of the strings in a
** string array. It is used when getting rid of local string arrays.
** 'base' is a pointer to the start of the array and 'size' is its
** size in bytes
*/
void discard_strings(byte *base, int32 size) {
  basicstring *p;
  int32 n;
  p = CAST(base, basicstring *);
  n = size/sizeof(basicstring);
  while (n>0) {
    free_string(*p);
    p++;
    n--;
  }
}

/*
** 'resize_string' is used to check if there is enough room following
** the string passed to it to increase its length to 'newlen' characters.
** If there is not, a new chunk of memory is allocated and the string
** copied to that. The function returns a pointer to the new chunk of
** memory for the string or the old string depending on what happens.
** If a new block is allocated, this functions disposes of the old one.
** Note that 'newlen' can be less that 'oldlen', which means that the
** string is being truncated. Depending on the difference, either a new
** block will be allocated for the string or the old string will be
** returned with the extra bit 'cut off'. The spare block will be
** added to the relevant bin
*/
char *resize_string(char *cp, int32 oldlen, int32 newlen) {
  int32 oldbin, newbin, sizediff;
  char *newcp;
  basicstring descriptor;
  oldbin = find_bin(oldlen);
  newbin = find_bin(newlen);
  if (newbin==oldbin) return cp;	/* Can use same string */
  if (newlen>oldlen) {		/* New string is longer than old one */
    newcp = alloc_string(newlen);	/* Grab new block and copy old string to it */
    if (oldlen!=0) {
      memmove(newcp, cp, oldlen);
      descriptor.stringlen = oldlen;	/* Have to fake a descriptor for 'free_string' */
      descriptor.stringaddr = cp;
      free_string(descriptor);
    }
    return newcp;
  }
  else {	/* New string length is shorter than old */
    if (newlen==0) {	/* New string is the null string */
      descriptor.stringlen = oldlen;	/* Have to fake a descriptor for 'free_string' */
      descriptor.stringaddr = cp;
      free_string(descriptor);
      return &emptystring;
    }
/*
** At this point the new string length is shorter than the original and the
** string has to be allocated from a bin for a shorter string size. There
** are two possibilities here: allocate a new string and copy the old one
** to it or chop the end off the old string. The only time a string can be
** truncated is when there is a bin of the size of the bit to be released.
*/
    sizediff = binsizes[oldbin]-binsizes[newbin];
    if (binsizes[find_bin(sizediff)]==sizediff) {	/* Bit to be chopped off will go in a bin */
      descriptor.stringlen = sizediff;	/* Have to fake a descriptor for 'free_string' */
      descriptor.stringaddr = cp+binsizes[newbin];
      free_string(descriptor);
      return cp;
    }
    else {	/* Have to copy string */
      newcp = alloc_string(newlen);
      memmove(newcp, cp, newlen);
      descriptor.stringlen = oldlen;	/* Have to fake a descriptor for 'free_string' */
      descriptor.stringaddr = cp;
      free_string(descriptor);
      return newcp;
    }
  }
}

/*
** 'get_stringlen' returns the length of a '$<addr>' type string. If no
** 'CR' character is found before the maximum allowed string length, the
** length is returned as zero
*/
int32 get_stringlen(int32 start) {
  int32 n;
  n =start;
  while (n-start<=MAXSTRING && basicvars.offbase[n]!=CR) n++;
  if (basicvars.offbase[n]==CR) return n-start;
  return 0;
}

/*
** 'clear_strings' is called when a program is loaded, edited or
** run to reset the string memory management stuff when the Basic
** heap is cleared
*/
void clear_strings(void) {
  int32 n;
  for (n=0; n<BINCOUNT; n++) binlists[n] = NIL;
  freestrings = 0;
  freelist = NIL;
#ifdef DEBUG
  allocated = 0;
  for (n=0; n<BINCOUNT; n++) allocations[n] = created[n] = reused[n] = 0;
#endif
}

int compare(const void *first, const void *second) {
  return CAST(CAST(first, freeblock *)->freestart, char *)-CAST(CAST(second, freeblock *)->freestart, char *);
}

/*
** 'collect' is called to try and free some memory in the free string
** lists. It returns 'true' if it mananged to find some otherwise it
** returns 'false'.
*/
static boolean collect(void) {
  int n, here, next, size;
  heapblock *p;
  freeblock *base;
  boolean merged;
#ifdef DEBUG
  int32 largest, count;
  fprintf(stderr, "Trying to merge %d free strings\n", freestrings);
#endif
  if (freestrings==0) return FALSE;	/* Give up if there is no free memory */
/*
** Start by creating an unsorted table of free blocks of memory held in
** the bins and on the free list. Sort the table into ascending order of
** address and then merge adjacent blocks
*/
  base = malloc(freestrings*sizeof(freeblock));
  if (base==NIL) return FALSE;		/* Indicate call failed */
  next = 0;
  p = freelist;	/* Copy details of strings on free list to table of free blocks */
  while (p!=NIL) {
    base[next].freestart = p;
    base[next].freesize = p->blocksize;
    next++;
    p = p->blockflink;
  }
  for (n=1; n<BINCOUNT; n++) {	/* Create unsorted table of free blocks */
    p = binlists[n];
    binlists[n] = NIL;
    size = binsizes[n];
    while (p!=NIL) {
      base[next].freestart = p;
      base[next].freesize = size;
      next+=1;
      p = p->blockflink;
    }
  }
  qsort(base, freestrings, sizeof(freeblock), compare);	/* Sort free blocks into address order */
  merged = FALSE;
  here = 0;
  next = 1;
#ifdef DEBUG
  largest = count = 0;
#endif
  do {	/* Go through table and merge adjacent free blocks */
    size = base[here].freesize;
    while (next<freestrings &&
     CAST(CAST(base[here].freestart, char *)+size, heapblock *)==base[next].freestart) {
      base[here].freesize = size = size+base[next].freesize;
      base[next].freestart = NIL;
      merged = TRUE;	/* Have managed to merge a couple of blocks */
      next++;
#ifdef DEBUG
      if (size>largest) largest = size;
      count++;
#endif
    }
    here = next;
    next++;
  } while (here<freestrings-1);
#ifdef DEBUG
  fprintf(stderr, "%d blocks were merged. Largest block size is %d bytes\n", count, largest);
#endif
/*
** Start by checking if the last block in the table can be returned to the
** Basic heap and dispose of it if it can. After that go through the free
** block table and put strings into bins if they are of the right size.
** Anything left will be added to the free string list.
** Note that the code goes through the table in reverse order so that the
** strings at the lowest addresses will be the first ones to be taken from
** the bins when requests for string memory are made
*/
  n = freestrings-1;
  while (n>=0 && base[n].freestart==NIL) n--;	/* Find final block in table */
  if (n>=0 && returnable(base[n].freestart, base[n].freesize)) {	/* Return block to Basic heap if possible */
    freemem(base[n].freestart, base[n].freesize);
#ifdef DEBUG
    allocated-=base[n].freesize;
    fprintf(stderr, "Returned %d bytes at %p to Basic heap\n", base[n].freesize, base[n].freestart);
#endif
    n--;
  }
  freestrings = 0;
  freelist = NIL;
  while (n>=0) {	/* Add blocks either to a bin or the free string list depending on size */
    if (base[n].freestart!=NIL) {	/* Want this entry */
      if (base[n].freesize<=MAXSTRING)
        size = find_bin(base[n].freesize);
      else {
        size = 0;
      }
      if (size>0 && binsizes[size]==base[n].freesize) {	/* Block size matches that of a bin */
        base[n].freestart->blockflink = binlists[size];
        binlists[size] = base[n].freestart;
      }
      else {
        base[n].freestart->blocksize = base[n].freesize;
        base[n].freestart->blockflink = freelist;
        freelist = base[n].freestart;
      }
      freestrings++;
    }
    n--;
  }
  free(base);
  return merged;
}

#ifdef DEBUG

/*
** 'show_stringstats' prints statistics on string bin usage
*/
void show_stringstats(void) {
  int32 n, free;
  heapblock *p;
  fprintf(stderr, "String statistics:\n");
  for (n=1; n<BINCOUNT; n++) {
    p = binlists[n];
    free = 0;
    while (p!=NIL) {
      free++;
      p = p->blockflink;
    }
    fprintf(stderr, "Size = %5d  requests = %d  created = %d  reused = %d  free = %d\n",
     binsizes[n], allocations[n], created[n], reused[n], free);
  }
  collect();
}

/*
** 'check_alloc' is called to check for memory leaks. It counts the
** number of bytes held in the free lists and currently allocated and
** ensures that the total of these is equal to the number of bytes
** allocated from the Basic heap. If it is not, either memory is being
** lost somewhere or being released more than once
*/
void check_alloc(void) {
  int32 n, m, used, usedcount, free, freecount, elements;
  heapblock *p;
  variable *vp;
  basicstring *sp;
  if (allocated==0) return;	/* No strings were allocated */
  used = usedcount = free = freecount = 0;
  for (n=1; n<BINCOUNT; n++) {	/* Find number of bytes in free lists */
    p = binlists[n];
    m = 0;
    while (p!=NIL) {
      m++;
      p = p->blockflink;
    }
    free+=m*binsizes[n];
    freecount+=m;
/*    if (m!=0) fprintf(stderr, "Block size %5d: %d entries\n", binsizes[n], m); */
  }
  for (n=0; n<VARLISTS; n++) {		/* Find number of bytes in use */
    vp = basicvars.varlists[n];
    while (vp!=NIL) {
      if (vp->varflags==VAR_STRINGDOL) {
        used+=binsizes[find_bin(vp->varentry.varstring.stringlen)];
        usedcount++;
      }
      else if (vp->varflags==VAR_STRARRAY && vp->varentry.vararray!=NIL) {
        sp = vp->varentry.vararray->arraystart.stringbase;
        elements = vp->varentry.vararray->arrsize;
        for (m=1; m<=elements; m++) {
          used+=binsizes[find_bin(sp->stringlen)];
          sp++;
        }
        usedcount+=elements;
      }
      vp = vp->varflink;
    }
  }
  n = allocated-used-free;
  fprintf(stderr, "Bytes allocated = %d,  in use = %d,  free = %d",
   allocated, used, free);
  if (n==0)
    fprintf(stderr, " - Okay\n");
  else if (n<0)
    fprintf(stderr, " - Too many releases (%d bytes)\n", n);
  else {
    fprintf(stderr, " *** Memory leak (%d bytes) ***\n", n);
  }
  fprintf(stderr, "Strings in use = %d,  free = %d\n", usedcount, freecount);
}

#endif
