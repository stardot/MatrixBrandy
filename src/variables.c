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
**	The main purpose of this module is to handle variables. It also
**	contains the functions for searching for procedures and functions
**	in the running program and any libraries that have been loaded.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "common.h"
#include "target.h"
#include "basicdefs.h"
#include "variables.h"
#include "evaluate.h"
#include "tokens.h"
#include "stack.h"
#include "heap.h"
#include "errors.h"
#include "miscprocs.h"
#include "screen.h"
#include "lvalue.h"

#define FIELDWIDTH 20		/* Width of field used to print each variable's value */
#define PRINTWIDTH 80		/* Default maximum number of characters printed per line */
#define MAXSUBSTR 45		/* Maximum characters printed from string */

#define VARMASK (VARLISTS-1)	/* Mask for selecting hash list */

/* #define DEBUG */

char *nullstring = "";		/* Null string used when defining string variables */


/*
** 'hash' returns a hash value for the variable name passed to it
*/
static int32 hash(char *p) {
  int32 hashtotal = 0;
  while (*p) {
    hashtotal = hashtotal*5^*p;
    p++;
  }
  return hashtotal;
}

/*
** 'clear_varlists' is called to dispose of the variable lists and
** details of any libraries loaded via 'LIBRARY'. The procedure and
** function lists and private symbol tables built for libraries
** loaded using an 'INSTALL' command are cleared too. The memory
** occupied by the variables is reclaimed elsewhere
*/
void clear_varlists(void) {
  int n;
  library *lp;
  for (n=0; n<VARLISTS; n++) basicvars.varlists[n] = NIL;
  basicvars.runflags.has_variables = FALSE;
  basicvars.lastsearch = basicvars.start;
  basicvars.liblist = NIL;
/* Now clear the PROC/FN lists and symbol tables for installed libraries */
  lp = basicvars.installist;
  while (lp!=NIL) {
    lp->libfplist = NIL;
    for (n=0; n<VARLISTS; n++) lp->varlists[n] = NIL;
    lp = lp->libflink;
  }
}

/*
** 'list_varlist' lists the variables and arrays (plus their values)
** whose names start with the letter 'which'
*/
static void list_varlist(char which, library *lp) {
  variable *vp;
  char temp[200];
  int done = 0, columns = 0, next, len = 0, n, width;
  width = (basicvars.printwidth==0 ? PRINTWIDTH : basicvars.printwidth);
  for (n=0; n<VARLISTS; n++) {
    if (lp==NIL) 	/* list entries in program's symbol table */
      vp = basicvars.varlists[n];
    else {
      vp = lp->varlists[n];
    }
    while (vp!=NIL) {
      if (*vp->varname == which || ((*CAST(vp->varname, byte*) == TOKEN_PROC
       || *CAST(vp->varname, byte *) == TOKEN_FN) && *(vp->varname+1) == which)) {	/* Found a match */
        done++;
        switch (vp->varflags) {
        case VAR_INTWORD:
          if (basicvars.debug_flags.variables)
            len = sprintf(temp, "%p  %s = %d", vp, vp->varname, vp->varentry.varinteger);
          else {
            len = sprintf(temp, "%s = %d", vp->varname, vp->varentry.varinteger);
          }
          break;
        case VAR_FLOAT:
          if (basicvars.debug_flags.variables)
            len = sprintf(temp, "%p  %s = %g", vp, vp->varname, vp->varentry.varfloat);
          else {
            len = sprintf(temp, "%s = %g", vp->varname, vp->varentry.varfloat);
          }
          break;
        case VAR_STRINGDOL: {
          int count;
          if (basicvars.debug_flags.variables)
            len = sprintf(temp, "%p  %s = \"", vp, vp->varname);
          else {
            len = sprintf(temp, "%s = \"", vp->varname);
          }
          if (vp->varentry.varstring.stringlen<=MAXSUBSTR)
            count = vp->varentry.varstring.stringlen;
          else {
            count = MAXSUBSTR;
          }
          memmove(temp+len, vp->varentry.varstring.stringaddr, count);
          if (vp->varentry.varstring.stringlen<=MAXSUBSTR)
            strcpy(temp+len+count, "\"");
          else {
            strcpy(temp+len+count, "...\"");
          }
          len = strlen(temp);
          break;
        }
        case VAR_INTARRAY: case VAR_FLOATARRAY: case VAR_STRARRAY: {
          int i;
          char temp2[20];
          basicarray *ap;
          if (basicvars.debug_flags.variables)
            len = sprintf(temp, "%p  %s", vp, vp->varname);
          else {
            len = sprintf(temp, "%s", vp->varname);
          }
          if (vp->varentry.vararray==NIL) {	/* Array bounds are undefined */
            temp[len] = ')';
            temp[len+1] = NUL;
          }
          else {
            ap = vp->varentry.vararray;
            for (i=0; i<ap->dimcount; i++) {
              if (i+1==ap->dimcount)	/* Doing last dimension */
                sprintf(temp2, "%d)", ap->dimsize[i]-1);
              else {
                sprintf(temp2, "%d,", ap->dimsize[i]-1);
              }
              strcat(temp, temp2);
            }
          }
          len = strlen(temp);
          break;
        }
        case VAR_PROC: case VAR_FUNCTION: {
          formparm *fp;
          char *p;
          if (vp->varflags==VAR_PROC)
            p = "PROC";
          else {
            p = "FN";
          }
          if (basicvars.debug_flags.variables)
            len = sprintf(temp, "%p  %s%s", vp, p, vp->varname+1);
          else {
            len = sprintf(temp, "%s%s", p, vp->varname+1);
          }
          fp = vp->varentry.varfnproc->parmlist;
          if (fp!=NIL) {
            strcat(temp, "(");
            do {
              if ((fp->parameter.typeinfo & VAR_RETURN)!=0) strcat(temp, "RETURN ");
              switch(fp->parameter.typeinfo & PARMTYPEMASK) {
              case VAR_INTWORD: case VAR_INTBYTEPTR: case VAR_INTWORDPTR:
                strcat(temp, "integer");
                break;
              case VAR_FLOAT: case VAR_FLOATPTR:
                strcat(temp, "real");
                break;
              case VAR_STRINGDOL: case VAR_DOLSTRPTR:
                strcat(temp, "string");
                break;
              case VAR_INTARRAY:
                strcat(temp, "integer()");
                break;
              case VAR_FLOATARRAY:
                strcat(temp, "real()");
                break;
              case VAR_STRARRAY:
                strcat(temp, "string()");
                break;
              default:
                error(ERR_BROKEN);
              }
              fp = fp->nextparm;
              if (fp==NIL)
                strcat(temp, ")");
              else {
                strcat(temp, ",");
              }
            } while (fp!=NIL);
          }
          len = strlen(temp);
          break;
        }
        case VAR_MARKER: {
          char *p;
          if (*CAST(vp->varname, byte *)==TOKEN_PROC)
            p = "PROC";
          else {
            p = "FN";
          }
          if (basicvars.debug_flags.variables)
            len = sprintf(temp, "%p  [line %d] %s%s", vp, get_lineno(find_linestart(vp->varentry.varmarker)), p, vp->varname+1);
          else {
            len = sprintf(temp, "[line %d] %s%s", get_lineno(find_linestart(vp->varentry.varmarker)), p, vp->varname+1);
          }
          break;
        }
        default:	/* Bad type of variable flag */
          error(ERR_BROKEN);
        }
        next = (columns+FIELDWIDTH-1)/FIELDWIDTH*FIELDWIDTH;
        if (next>=width) {	/* Not enough room on this line */
          emulate_printf("\r\n%s", temp);
          columns = len;
        }
        else {
          while (columns<next) {
            emulate_vdu(' ');
            columns++;
          }
          emulate_printf("%s", temp);
          columns+=len;
        }
      }
      vp = vp->varflink;
    }
  }
  if (done!=0) emulate_printf("\r\n\n");
}

/*
** 'list_entries' lists all of the entries in either the Basic program's
** symbol table (lp==NIL) or the symbol table of library 'lp'
*/
static void list_entries(library *lp) {
  char n;
  for (n='A'; n<='Z'; n++) {
    list_varlist(n, lp);
    list_varlist(tolower(n), lp);
  }
  list_varlist('_', lp);
  list_varlist('`', lp);
}

/*
** 'list_variables' either lists the variables, procedures and functions
** that start with the letter given in 'which' or, if this is a blank, it
** lists everything
*/
void list_variables(char which) {
  char n;
  char temp[40];
  int columns, len, next, width;
  width = (basicvars.printwidth==0 ? PRINTWIDTH : basicvars.printwidth);
  if (which==' ') {	/* List everything */
    emulate_printf("Static integer variables:\r\n");
    columns = 0;
    for (n='A'; n<='Z'; n++) {
      len = sprintf(temp, "%c%% = %d", n, basicvars.staticvars[n-'A'+1].varentry.varinteger);
      next = (columns+FIELDWIDTH-1)/FIELDWIDTH*FIELDWIDTH;
      if (next>=width) {	/* Not enough room on this line */
        emulate_printf("\r\n%s", temp);
        columns = len;
      }
      else {
        while (columns<next) {
          emulate_vdu(' ');
          columns++;
        }
        emulate_printf("%s", temp);
        columns+=len;
      }
    }
    emulate_printf("\r\n\nDynamic variables, procedures and functions:\r\n");
    list_entries(NIL);		/* List entries in main symbol table */
  }
  else {	/* List only variables whose names begin with 'which' */
    if (which>='A' && which<='Z') {
      emulate_printf("Static integer variable '%c%%' = %d\r\n", which, basicvars.staticvars[which-'A'+1].varentry.varinteger);
    }
    emulate_printf("Dynamic variables, procedures and functions:\r\n");
    list_varlist(which, NIL);
  }
}

/*
** 'detail_library' displays the name of a library and the names
** and values of any variables defined as local to it
*/
void detail_library(library *lp) {
  int n;
  emulate_printf("%s\r\n", lp->libname);
  for (n=0; n<VARLISTS && lp->varlists[n]==NIL; n++);	/* Are there any entries in the library's symbol table? */
  if (n==VARLISTS)
    emulate_printf("Library has no local variables\r\n", lp->libname);
  else {	/* Library has symbols - List them */
    emulate_printf("Variables local to library:\r\n");
    list_entries(lp);
  }
}

/*
** 'list_libraries' lists the libraries that have been loaded
*/
void list_libraries(char ch) {
  library *lp;
  if (basicvars.liblist!=NIL) {
    emulate_printf("\nLibraries (in search order):\r\n");
    for (lp = basicvars.liblist; lp!=NIL; lp = lp->libflink) detail_library(lp);
  }
  if (basicvars.installist!=NIL) {
    emulate_printf("\nInstalled libraries (in search order):\r\n");
    for (lp = basicvars.installist; lp!=NIL; lp = lp->libflink) detail_library(lp);
  }
}

/*
** 'define_array' is called to collect the dimensions of an array
** and to create the array. 'vp' points at the symbol table entry
** of the array. 'islocal' is set to TRUE if the array is a local
** array, that is, it is defined in a procedure or function.
*/
void define_array(variable *vp, boolean islocal) {
  int32 bounds[MAXDIMS];
  int32 n, dimcount, highindex, elemsize = 0, size;
  basicarray *ap;
  dimcount = 0;		/* Number of dimemsions */
  size = 1;		/* Number of elements */
  switch (vp->varflags) {	/* Figure out array element size */
  case VAR_INTARRAY:
    elemsize = sizeof(int32);
    break;
  case VAR_FLOATARRAY:
    elemsize = sizeof(float64);
    break;
  case VAR_STRARRAY:
    elemsize = sizeof(basicstring);
    break;
  default:
    error(ERR_BROKEN, __LINE__, "variables");	/* Bad variable type flags found */
  }
  do {	/* Find size of each dimension */
    highindex = eval_integer();
    if (*basicvars.current!=',' && *basicvars.current!=')' && *basicvars.current!=']') error(ERR_CORPNEXT);
    if (highindex<0) error(ERR_NEGDIM, vp->varname);
    highindex++;	/* Add 1 to get size of dimension */
    if (dimcount>MAXDIMS) error(ERR_DIMCOUNT, vp->varname);	/* Array has too many dimemsions */
    bounds[dimcount] = highindex;
    size = size*highindex;
    dimcount++;
    if (*basicvars.current!=',') break;
    basicvars.current++;
  } while (TRUE);
  if (*basicvars.current!=')' && *basicvars.current!=']') error(ERR_RPMISS);
  if (dimcount==0) error(ERR_SYNTAX);	/* No array dimemsions supplied */
  basicvars.current++;	/* Skip the ')' */
/* Now create the array and initialise it */
  if (islocal) {	/* Acquire memory from stack for a local array */
    ap = alloc_stackmem(sizeof(basicarray));	/* Grab memory for array descriptor */
    if (ap==NIL) error(ERR_BADDIM, vp->varname);
    if (vp->varflags==VAR_STRARRAY)	/* Grab memory for array and mark it as string array */
      ap->arraystart.arraybase = alloc_stackstrmem(size*elemsize);
    else {	/* Grab memory for numeric array */
      ap->arraystart.arraybase = alloc_stackmem(size*elemsize);
    }
  }
  else {	/* Acquire memory from heap for a normal array */
    ap = condalloc(sizeof(basicarray));		/* Grab memory for array descriptor */
    if (ap==NIL) error(ERR_BADDIM, vp->varname);	/* There is not enough memory available for the descriptor */
    ap->arraystart.arraybase = condalloc(size*elemsize);	/* Grab memory for array proper */
  }
  if (ap->arraystart.arraybase==NIL) error(ERR_BADDIM, vp->varname);	/* There is not enough memory */
  ap->dimcount = dimcount;
  ap->arrsize = size;
  for (n=0; n<dimcount; n++) ap->dimsize[n] = bounds[n];
  vp->varentry.vararray = ap;
/* Now zeroise all the array elememts */
  if (vp->varflags==VAR_INTARRAY)
    for (n=0; n<size; n++) ap->arraystart.intbase[n] = 0;
  else if (vp->varflags==VAR_FLOATARRAY)
    for (n=0; n<size; n++) ap->arraystart.floatbase[n] = 0.0;
  else {	/* This leaves string arrays */
    basicstring temp;
    temp.stringlen = 0;
    temp.stringaddr = nullstring;
    for (n=0; n<size; n++) ap->arraystart.stringbase[n] = temp;
  }
}

/*
** 'create_variable' is called to create a new variable or array.
** It returns a pointer to the variable list entry created.
** 'lp' says which symbol table the variable is to be added to. If
** it is NIL then the entry is added to the symbol table for the
** program in memory. If lp is is not NIL then it points at the
** 'library' structure of the library whose symbol table is to be
** used
*/
variable *create_variable(byte *varname, int namelen, library *lp) {
  variable *vp;
  char *np;
  int32 hashvalue;
  np = allocmem(namelen+1);
  vp = allocmem(sizeof(variable));
  memcpy(np, varname, namelen);		/* Make copy of name */
  if (np[namelen-1]=='[') np[namelen-1] = '(';
  np[namelen] = NUL;			/* And add a null at the end */
  hashvalue = hash(np);
  vp->varname = np;
  vp->varhash = hashvalue;
  vp->varowner = lp;
  if (lp==NIL) {	/* Add variable to program's symbol table */
    vp->varflink = basicvars.varlists[hashvalue & VARMASK];
    basicvars.varlists[hashvalue & VARMASK] = vp;
  }
  else {	/* Add variable to library's symbol table */
    vp->varflink = lp->varlists[hashvalue & VARMASK];
    lp->varlists[hashvalue & VARMASK] = vp;
  }
  basicvars.runflags.has_variables = TRUE;	/* Say program now has some variables */
  switch (np[namelen-1]) {	/* Figure out type of variable from last character of name */
  case '(':	/* Defining an array */
    switch (np[namelen-2]) {
    case '%':
      vp->varflags = VAR_INTWORD|VAR_ARRAY;
      break;
    case '$':
      vp->varflags = VAR_STRINGDOL|VAR_ARRAY;
      break;
    default:
      vp->varflags = VAR_FLOAT|VAR_ARRAY;
    }
    vp->varentry.vararray = NIL;
    break;
  case '%':
    vp->varflags = VAR_INTWORD;
    vp->varentry.varinteger = 0;
    break;
  case '$':
    vp->varflags = VAR_STRINGDOL;
    vp->varentry.varstring.stringlen = 0;
    vp->varentry.varstring.stringaddr = nullstring;
    break;
  default:
    vp->varflags = VAR_FLOAT;
    vp->varentry.varfloat = 0.0;
  }
#ifdef DEBUG
  if (basicvars.debug_flags.variables) fprintf(stderr, "Created variable '%s' at %p\n", vp->varname, vp);
#endif
  return vp;
}

/*
** 'find_variable' looks for the variable whose name starts at 'name',
** returning a pointer to its symbol table entry or NIL if it cannot
** be found.
** There are two places where the function can check. If the reference
** to the variable is in a library, it checks to see if it has been
** declared in the library's symbol table. If the reference is not
** in a library or the variable cannot be found in the library's symbol
** table, the code searches the main symbol table
*/
variable *find_variable(byte *np, int namelen) {
  variable *vp;
  library *lp;
  char name[MAXNAMELEN];
  int32 hashvalue;
  memcpy(name, np, namelen);
  if (name[namelen-1]=='[') name[namelen-1] = '(';
  name[namelen] = NUL;		/* Ensure name is null-terminated */
  hashvalue = hash(name);
  lp = find_library(np);	/* Was the variable reference in a library? */
  if (lp!=NIL) {		/* Yes - Search library's symbol table first */
    vp = lp->varlists[hashvalue & VARMASK];
    while (vp!=NIL && (hashvalue!=vp->varhash || strcmp(name, vp->varname)!=0)) vp = vp->varflink;
    if (vp!=NIL) return vp;	/* Found symbol - Return pointer to symbol table entry */
  }
  vp = basicvars.varlists[hashvalue & VARMASK];
  while (vp!=NIL && (hashvalue!=vp->varhash || strcmp(name, vp->varname)!=0)) vp = vp->varflink;
  return vp;
}

/*
** 'scan_parmlist' builds the parameter list for the procedure or
** function 'vp'.
** The code is called the first time the procedure or function is
** used. A symbol table entry will have been set up that notes its
** location; this function fills in the rest of the details.
*/
static void scan_parmlist(variable *vp) {
  int32 count;
  fnprocdef *dp;
  formparm *formlist, *formlast, *fp;
  byte what;
  boolean isreturn;
  count = 0;
  formlist = formlast = NIL;
  save_current();
  basicvars.current = vp->varentry.varmarker;	/* Point at the XFNPROCALL token */
  basicvars.runflags.make_array = TRUE;	/* Can create arrays in PROC/FN parm list */
  what = *vp->varname;		/* Note whether this is a PROC or FN */
#ifdef DEBUG
  if (basicvars.debug_flags.variables) fprintf(stderr, "Fill in details for PROC/FN '%s%s' at %p, vp=%p\n",
   (what==TOKEN_PROC ? "PROC" : "FN"), vp->varname+1, basicvars.current, vp);
#endif
  basicvars.current+=1+LOFFSIZE;	/* Find parameters (if any) */
  if (*basicvars.current=='(') {	/* Procedure or function has a parameter list */
    do {
      basicvars.current++;	/* Skip '(' or ',' and point at a parameter */
      isreturn = *basicvars.current==TOKEN_RETURN;
      if (isreturn) basicvars.current++;
      fp = allocmem(sizeof(formparm));	/* Create new parameter list entry */
      get_lvalue(&(fp->parameter));
      if (isreturn) fp->parameter.typeinfo+=VAR_RETURN;
      fp->nextparm = NIL;
      if (formlist==NIL)
        formlist = fp;
      else {
        formlast->nextparm = fp;
      }
      formlast = fp;
      count++;
      if (*basicvars.current!=',') break;	/* There is nothing more to do */
    } while(TRUE);
    if (*basicvars.current!=')') error(ERR_CORPNEXT);
    basicvars.current++;	/* Move past ')' */
  }
  if (*basicvars.current==':') basicvars.current++;	/* Body of procedure starts on same line as 'DEF PROC/FN' */
  while (*basicvars.current==NUL) {	/* Body of procedure starts on next line */
    basicvars.current++;	/* Move to start of next line */
    if (AT_PROGEND(basicvars.current)) error(ERR_SYNTAX);	/* There is no procedure body */
    basicvars.current = FIND_EXEC(basicvars.current);	/* Find the first executable token */
  }
  dp = allocmem(sizeof(fnprocdef));
  dp->fnprocaddr = basicvars.current;
  dp->parmcount = count;
  dp->simple = count==1 && formlist->parameter.typeinfo==VAR_INTWORD;
  dp->parmlist = formlist;
  vp->varentry.varfnproc = dp;
  if (what==TOKEN_PROC)
    vp->varflags = VAR_PROC;
  else {
    vp->varflags = VAR_FUNCTION;
  }
  basicvars.runflags.make_array = FALSE;
  restore_current();	/* Restore current to its rightful value */
}

/*
** 'add_libvars' is called when a 'LIBRARY LOCAL' statement is
** found to add the variables listed on it to the library's
** symbol table.
*/
static void add_libvars(byte *tp, library *lp) {
  byte *ep, *base;
  int namelen;
  variable *vp = NULL;
  save_current();
  basicvars.current = tp;	/* Point current at this line for error messages */
  tp+=2;	/* Skip 'LIBRARY' and 'lOCAL' tokens */
  while (*tp==TOKEN_XVAR) {
    base = get_srcaddr(tp);
    ep = skip_name(base);	/* Find byte after name */
    namelen = ep-base;
    vp = find_variable(base, namelen);	/* Has variable already been created for this library? */
    if (vp==NIL || vp->varowner!=lp) vp = create_variable(base, namelen, lp);
    tp+=LOFFSIZE+1;
    if ((vp->varflags & VAR_ARRAY)!=0) {	/* Array */
      if (*tp!=')' && *tp!=']') error(ERR_RPMISS);
      tp++;
    }
    if (*tp!=',') break;
    tp++;
  }
  if (*tp!=NUL && *tp!=':') error(ERR_SYNTAX);
  restore_current();	/* Restore current to its proper value */
#ifdef DEBUG
  if (basicvars.debug_flags.variables) fprintf(stderr, "Created private variable '%s' in library '%s' at %p\n",
   vp->varname, lp->libname, vp);
#endif
}

/*
** 'add_libarray' is called to add an array to a library's
** symbol table
*/
static void add_libarray(byte *tp, library *lp) {
  byte *ep, *base;
  int namelen;
  variable *vp;
  save_current();
  basicvars.current = tp;
  do {
    basicvars.current++;		/*Skip DIM token or ',' */
    if (*basicvars.current!=TOKEN_XVAR) error(ERR_SYNTAX);	/* Array name wanted */
    base = get_srcaddr(basicvars.current);
    ep = skip_name(base);	/* Find byte after name */
    namelen = ep-base;
    if (*(ep-1)!='(' && *(ep-1)!='[') error(ERR_VARARRAY);	/* Not an array */
    vp = find_variable(base, namelen);	/* Has array already been created for this library? */
    if (vp==NIL)	/* Library does not exist */
      vp = create_variable(base, namelen, lp);
    else {	/* Name found in symbol table */
      if (vp->varowner!=lp)	/* But it is not in this library */
        vp = create_variable(base, namelen, lp);
      else {	/* Array exists - Has its dimensions been defined? */
        if (vp->varentry.vararray!=NIL) error(ERR_DUPLDIM, vp->varname);	      }
    }
    basicvars.current+=LOFFSIZE+1;
    define_array(vp, FALSE);
  } while (*basicvars.current==',');
  if (*basicvars.current!=NUL && *basicvars.current!=':') error(ERR_SYNTAX);
  restore_current();	/* Restore current to its proper value */
#ifdef DEBUG
  if (basicvars.debug_flags.variables) fprintf(stderr, "Created private variable '%s' in library '%s' at %p\n",
   vp->varname, lp->libname, vp);
#endif
}

/*
** 'add_procfn' creates an entry for a procedure or function
** in a library and returns a pointer to its entry. 'tp' points
** at the 'DEF' token when the function is called
*/
static libfnproc *add_procfn(byte *bp, byte *tp) {
  byte *ep, *base;
  libfnproc *fpp;
  int namelen;
  char pfname[MAXNAMELEN];
  base = get_srcaddr(tp+1);	/* Find address of PROC/FN name */
  ep = skip_name(base);	/* Find byte after name */
  if (*(ep-1)=='(') ep--;	/* '(' here is not part of the name but the start of the parameter list */
  namelen = ep-base;
  memmove(pfname, base, namelen);
  pfname[namelen] = NUL;
  fpp = allocmem(sizeof(libfnproc));
  fpp->fpline = bp;
  fpp->fpname = base;
  fpp->fpmarker = tp+1;	/* Need pointer to the XFNPROCALL token for scan_parmlist() */
  fpp->fphash = hash(pfname);
  fpp->fpflink = NIL;
  return fpp;
}

/*
** 'scan_library' is called to build a list of procedures and
** functions in a library to speed up library searches. It also
** looks for 'LIBRARY LOCAL' statements and adds any variables
** listed on to the library's sumbol table. 'lp' points at the
** library list entry of interest.
** The function is called the first time a procedure or function
** is called that is not in the Basic program. As each library
** is searched for the first time, so this function is invoked.
** Variables that will be private to the library are created at
** this time.
*/
static void scan_library(library *lp) {
  byte *tp, *bp;
  libfnproc *fpp, *fpplast;
  boolean foundproc;
  bp = lp->libstart;
  fpplast = NIL;
  foundproc = FALSE;
  while (!AT_PROGEND(bp)) {
    tp = FIND_EXEC(bp);
    if (*tp==TOKEN_DEF && *(tp+1)==TOKEN_XFNPROCALL) {	/* Found DEF PROC or DEF FN */
      foundproc = TRUE;
      fpp = add_procfn(bp, tp);
      if (fpplast==NIL)	/* First PROC or FN found in library */
        lp->libfplist = fpp;
      else {
        fpplast->fpflink = fpp;
      }
      fpplast = fpp;
    }
    else if (!foundproc && *tp==TOKEN_LIBRARY && *(tp+1)==TOKEN_LOCAL)	/* LIBRARY LOCAL */
      add_libvars(tp, lp);
    else if (!foundproc && *tp==TOKEN_DIM) {
      add_libarray(tp, lp);
    }
    bp+=get_linelen(bp);
  }
}

/*
** 'search_library' scans a library for procedure or function 'name'. If
** it finds it, it creates a symbol table entry for the item and returns
** a pointer to that entry. If the procedure or function cannot be found
** in this library the function returns NIL.
*/
static variable *search_library(library *lp, char *name) {
  int32 hashvalue;
  int namelen;
  libfnproc *fpp;
  variable *vp;
  if (lp->libfplist==NIL) scan_library(lp);	/* Create list of PROCs and FNs in library */
  hashvalue = hash(name);
  namelen = strlen(name);
  fpp = lp->libfplist;
  if (fpp==NIL) return NIL;		/* Return if library does not contain anything */
  do {
    if (fpp->fphash==hashvalue && memcmp(fpp->fpname, name, namelen)==0) break;	/* Found it */
    fpp = fpp->fpflink;
  } while (fpp!=NIL);
  if (fpp==NIL) return NIL;		/* Entry not found in library */
  vp = allocmem(sizeof(variable));	/* Entry found. Create symbol table entry for it */
  vp->varname = allocmem(namelen+1);	/* +1 for NUL at end of name */
  strcpy(vp->varname, name);
  vp->varhash = hashvalue;
  vp->varentry.varmarker = fpp->fpmarker;	/* Needed in 'scan_parmlist' */
  vp->varflink = basicvars.varlists[hashvalue & VARMASK];
  basicvars.varlists[hashvalue & VARMASK] = vp;
  basicvars.runflags.has_variables = TRUE;	/* Say program has some variables */
  scan_parmlist(vp);			/* Deal with parameter list */
#ifdef DEBUG
  if (basicvars.debug_flags.variables) fprintf(stderr, "Created PROC/FN '%s%s' in library '%s' at %p\n",
   (*CAST(name, byte *)==TOKEN_PROC ? "PROC" : "FN"), name+1, lp->libname, vp);
#endif
  return vp;
}

/*
** 'mark_procfn' adds an entry for a procedure or function to the
** symbol table. This call marks the position of the definition by means
** of a pointer to the 'XFNPROCALL' token in the executable part of the
** tokenised line. 'pp' points at this token on entry to the function.
*/
static variable *mark_procfn(byte *pp) {
  byte *base, *ep;
  variable *vp;
  int32 hashvalue;
  int namelen;
  char *cp;
  base = get_srcaddr(pp);	/* Point at start of name (includes 'PROC' or 'FN' token) */
  ep = skip_name(base);
  if (*(ep-1)=='(') ep--;
  namelen = ep-base;
  cp = allocmem(namelen+1);
  vp = allocmem(sizeof(variable));
  memcpy(cp, base, namelen);	/* Make copy of name */
  *(cp+namelen) = NUL;		/* And add a null at the end */
  vp->varname = cp;
  vp->varhash = hashvalue = hash(cp);
  vp->varflags = VAR_MARKER;
  vp->varentry.varmarker = pp;
  vp->varflink = basicvars.varlists[hashvalue & VARMASK];
  basicvars.varlists[hashvalue & VARMASK] = vp;
  basicvars.runflags.has_variables = TRUE;	/* Say program now has some variables */
#ifdef DEBUG
  if (basicvars.debug_flags.variables) fprintf(stderr, "Created PROC/FN '%s%s' at %p\n",
   (*base==TOKEN_PROC ? "PROC" : "FN"), vp->varname+1, vp);
#endif
  return vp;
}

/*
** 'scan_fnproc' scans though the Basic program for the the procedure or
** function 'name'. It creates symbol table entries for any procedures
** of functions it finds (leaving them as 'marker' entries so that their
** positions are known.) It returns a pointer to the symbol table definition
** of the procedure or function
*/
static variable *scan_fnproc(char *name) {
  byte *tp, *bp;
  int32 namehash;
  variable *vp;
  library *lp;
  namehash = hash(name);
  bp = basicvars.lastsearch;	/* Start new search where last one ended */
  vp = NIL;
  while (!AT_PROGEND(bp)) {
    tp = FIND_EXEC(bp);
    bp+=get_linelen(bp);	/* This is updated here so that 'lastsearch' is set correctly below */
    if (*tp==TOKEN_DEF && *(tp+1)==TOKEN_XFNPROCALL) {	/* Found 'DEF PROC' or 'DEF FN' */
      vp = mark_procfn(tp+1); /* Must be a previously unseen entry */
      if (vp->varhash==namehash && strcmp(name, vp->varname)==0) break;	/* Found it */
      vp = NIL;	/* Reset 'vp' as this proc/fn is not the one needed */
    }
  }
  basicvars.lastsearch = bp;
  if (vp==NIL && basicvars.liblist!=NIL) {	/* Check the library list for the PROC/FN */
    lp = basicvars.liblist;
    do {
      vp = search_library(lp, name);
      if (vp!=NIL) break;
      lp = lp->libflink;
    } while (lp!=NIL);
  }
  if (vp==NIL && basicvars.installist!=NIL) {	/* Check the installed library list */
    lp = basicvars.installist;
    do {
      vp = search_library(lp, name);
      if (vp!=NIL) break;
      lp = lp->libflink;
    } while (lp!=NIL);
  }
  if (vp==NIL) {	/* Procedure/function not found */
    if (*CAST(name, byte *)==TOKEN_PROC)	/* First byte of name is a 'PROC' or 'FN' token */
      error(ERR_PROCMISS, name+1);
    else {
      error(ERR_FNMISS, name+1);
    }
  }
  return vp;
}

/*
** 'find_fnproc' is called to find a procedure or function in the
** variable lists, returning a pointer to the required entry. The
** function will search the program for the function if there is
** no entry. To speed up searches for functions and procedures when
** they are needed, the search code notes the locations of any
** functions or procedures it finds but does not create a full entry
** for them. That task is carried out the first time the procedure
** or function is called
*/
variable *find_fnproc(byte *np, int namelen) {
  variable *vp;
  int32 hashvalue;
  memcpy(basicvars.stringwork, np, namelen);	/* Copy name from after 'FN' or 'PROC' token */
  *(basicvars.stringwork+namelen) = NUL;	/* Ensure name is properly terminated */
  hashvalue = hash(basicvars.stringwork);
  vp = basicvars.varlists[hashvalue & VARMASK];
  if (vp!=NIL) {	/* List is not empty - Scan it for function or proc */
    while (vp!=NIL && (hashvalue!=vp->varhash || strcmp(basicvars.stringwork, vp->varname)!=0)) vp = vp->varflink;
    if (vp!=NIL && vp->varflags!=VAR_MARKER) return vp;	/* Found it */
  }
  if (vp==NIL) vp = scan_fnproc(basicvars.stringwork);	/* Not a known proc - Scan program and libraries for it */
  if (vp->varflags==VAR_MARKER) scan_parmlist(vp);	/* Fill in its details */
  return vp;
}

/*
** 'init_staticvars' is called when the interpreter is first started
** to set the static variables A% to Z% to their initial values
*/
void init_staticvars(void) {
  int n;
  for (n=0; n<STDVARS; n++) {
    basicvars.staticvars[n].varflags = VAR_INTWORD;
    basicvars.staticvars[n].varentry.varinteger = 0;
  }
  basicvars.staticvars[ATPERCENT].varentry.varinteger = STDFORMAT;
}
