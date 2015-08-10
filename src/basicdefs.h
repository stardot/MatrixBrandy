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
**	This file defines the main constants and structures used
**	throughout the interpreter
*/

#ifndef __basicdefs_h
#define __basicdefs_h

#include <setjmp.h>
#include "common.h"
#include "target.h"

#define OPSTACKSIZE 20			/* Size of operator stack */

#define STDVARS 27			/* Number of built-in variables (@% to Z%) */
#define VARLISTS 64			/* Number of lists of variables (must be power of two) */

#define DEFWIDTH 0			/* Default value for 'WIDTH' */

#define ATPERCENT 0			/* Index of '@%' in builtin variables array */
#define STDFORMAT 0x90A			/* Default number format */
#define DEFDIGITS 10			/* Default no. of digits to print if format says 0 */
#define FORMAT_G 0			/* General (and default) format */
#define FORMAT_E 1			/* Exponent format */
#define FORMAT_F 2			/* Floating point format */
#define STRUSE 0x1000000		/* STR$ uses @% to format string */
#define COMMADPT 0x80000000		/* Use ',' instead of '.' as the decimal point */

#define FNAMESIZE 256			/* Maximum length of file names */
#define INPUTLEN 1024			/* Size of terminal input buffer */

#define PRESERVED 8			/* Number of bytes at start of program saved for OLD */
#define MAXCURCOUNT 10			/* Maximum no. of entries on 'saved current' stack */

/* 'LISTO' options */

#define LIST_SPACE 1			/* Print space after line number */
#define LIST_INDENT 2			/* Indent structures */
#define LIST_SPLIT 4			/* Split lines at ':' */
#define LIST_NOLINE 8			/* Do not print line numbers */
#define LIST_LOWER 0x10			/* List keywords in lower case */
#define LIST_PAGE 0x20			/* List 20 lines at a time */
#define LIST_EXPAND 0x40		/* Add extra spaces when expanding line */

/* 'LISTO' options for debugging the interpreter */

#define DEBUG_DEBUG 0x100		/* Show debugging output */
#define DEBUG_TOKENS 0x200		/* Show tokenised lines on input plus addresses on listings */
#define DEBUG_VARIABLES 0x400		/* List addresses of variables when created + on LVAR */
#define DEBUG_STRINGS 0x800		/* Show allocation/release of memory for strings */
#define DEBUG_STATS 0x1000		/* Show string heap statistics */
#define DEBUG_STACK 0x2000		/* Show structures pushed and popped from stack */
#define DEBUG_ALLSTACK 0x4000		/* Show in detail items pushed and popped from stack */

/* Variable type flags */

#define VAR_INTBYTE 1				/* One-byte integer */
#define VAR_INTWORD 2				/* Four-byte integer */
#define VAR_FLOAT 3				/* Eight byte floating point */
#define VAR_STRINGDOL 4				/* String ('string$' type) */
#define VAR_DOLSTRING 5				/* String ('$string' type) */
#define VAR_ARRAY 0x08				/* Array */
#define VAR_INTARRAY (VAR_INTWORD+VAR_ARRAY)	/* Integer array */
#define VAR_FLOATARRAY (VAR_FLOAT+VAR_ARRAY)	/* Floating point array */
#define VAR_STRARRAY (VAR_STRINGDOL+VAR_ARRAY)	/* String array */
#define VAR_POINTER 0x10			/* Pointer */
#define VAR_INTBYTEPTR (VAR_INTBYTE+VAR_POINTER)	/* Pointer to 1 byte integer */
#define VAR_INTWORDPTR (VAR_INTWORD+VAR_POINTER)	/* Pointer to 4 byte integer */
#define VAR_FLOATPTR (VAR_FLOAT+VAR_POINTER)		/* Pointer to floating point */
#define VAR_DOLSTRPTR (VAR_DOLSTRING+VAR_POINTER)	/* Pointer to string */
#define VAR_PROC 0x20				/* Entry is for a procedure */
#define VAR_FUNCTION 0x40			/* Entry is for a function */
#define VAR_MARKER 0x80				/* Entry marks location of a proc/fn */

#define VAR_FIXED 0x100			/* Marks variable as a constant */
#define VAR_RETURN 0x200		/* Marks variable as a 'return' variable */

#define TYPEMASK 0x07			/* Mask to extract type of item */
#define TYPECHECKMASK 0x0F		/* Mask for type checking PROC/FN parameters */
#define PARMTYPEMASK 0x1F		/* Mask to extract PROC/FN parameter type */

/* Values used to mark type of value in file create with 'PRINT#' */

#define PRINT_INT 0x40			/* Marks value in file as binary integer */
#define PRINT_FLOAT5 0x80		/* Marks value in file as five byte binary floating point */
#define PRINT_FLOAT 0x88		/* Marks value in file as binary floating point */
#define PRINT_SHORTSTR 0x00		/* Marks value in file as a short string */
#define PRINT_LONGSTR 0x01		/* Marks value in file as a long string */

/* Values used to represent 'true' and 'false' in Basic programs */

#define BASFALSE 0
#define BASTRUE (-1)

/* Indexes of static variables (@%, A%..Z%) in basicvars.staticvars */

#define AT_PERCENT 0
#define A_PERCENT 1
#define X_PERCENT 24
#define Y_PERCENT 25

/* 'basicstring' defines the layout of a string descriptor */

typedef struct {
  int32 stringlen;			/* Length of string */
  char *stringaddr;			/* Pointer to string */
} basicstring;

/* 'basicarray' gives the layout of an array descriptor */

typedef struct {
  int32 dimcount;			/* Number of array dimensions */
  int32 arrsize;			/* Total number of elements in array */
  union {
    int32 *intbase;			/* Pointer to start of integer elements */
    float64 *floatbase;			/* Pointer to start of floating point elements */
    basicstring *stringbase;		/* Pointer to start of string elements */
    void *arraybase;			/* Pointer to start of array */
  } arraystart;				/* Pointer to start of array */
  int32 dimsize[MAXDIMS];		/* Sizes of the array dimemsions */
} basicarray;

typedef union {
  char *charaddr;			/* Pointer to a character */
  int32 *intaddr;			/* Pointer to Basic integer value */
  float64 *floataddr;			/* Pointer to Basic floating point value */
  basicstring *straddr;			/* Pointer to Basic string descriptor */
  basicarray **arrayaddr;		/* Pointer to pointer to Basic array descriptor */
  int32 offset;				/* Byte offset in workspace for indirection operators */
} pointers;

/* 'lvalue' refers to an item when a writable address is needed rather than its value */

typedef struct {
  int32 typeinfo;			/* Type of item referenced */
  pointers address;			/* Address of item */
} lvalue;

typedef struct {
  byte *whenexpr;			/* Pointer to an expression after a 'WHEN' */
  byte *whenaddr;			/* Pointer to the code for that 'WHEN' */
} whenvalue;

typedef struct {
  int32 whencount;			/* Number of 'WHEN' cases in table */
  byte *defaultaddr;			/* Address of 'OTHERWISE' code */
  whenvalue whentable[1];		/* First entry in table of WHEN cases */
} casetable;

/* 'formparm' is used to refer to a formal parameter of a function or a procedure */

typedef struct formparm {
  struct formparm *nextparm;		/* Next parameter */
  lvalue parameter;			/* Parameter's details */
} formparm;

/* 'fnprocdef' gives details of a procedure's or function's formal parameters */

typedef struct {
  byte *fnprocaddr;			/* Address of start of PROC/FN */
  int32 parmcount;			/* Number of parameters */
  boolean simple;			/* PROC/FN has only one integer parameter */
  formparm *parmlist;			/* Pointer to first parameter */
} fnprocdef;

/* 'variable' is the main structure used to define a variable */

typedef struct variable {
  struct variable *varflink;		/* Next variable in chain */
  int32 varflags;			/* Type flags */
  char *varname;			/* Pointer to variable's name */
  int32 varhash;			/* Hash value for symbol's name */
  struct library *varowner;		/* Library in which var was defined or NIL */
  union {
    int32 varinteger;			/* Value if an integer */
    float64 varfloat;			/* Value if floating point */
    basicstring varstring;		/* Descriptor if a string */
    basicarray *vararray;		/* Pointer to array's dope vector */
    fnprocdef *varfnproc;		/* Pointer to proc/fn definition */
    byte *varmarker;			/* Pointer to proc/fn def marked earlier */
  } varentry;
} variable;

/* 'fnprocinfo' is the structure saved on the Basic stack when */
/* a procedure or function is called */

typedef struct fnprocinfo {
  struct fnprocinfo *lastcall;		/* Pointer to last PROC/FN return block */
  byte *retaddr;			/* Return address in program */
  int32 parmcount;			/* Number of parameters on stack for call */
  char *fnprocname;			/* Pointer to FN's or PROC's name */
} fnprocinfo;

/* 'gosubinfo' contains the information saved on the Basic stack */
/* when a subroutine is called */

typedef struct gosubinfo {
  struct gosubinfo *lastcall;		/* Pointer to last GOSUB return block */
  byte *retaddr;			/* Return address is program */
} gosubinfo;

typedef struct {
  boolean islocal;			/* TRUE if this is a 'ON ERROR LOCAL' error block */
  byte *current;			/* Value of 'current' when restarting after an error */
  byte *stacktop;			/* Value of the Basic stack pointer when restarting */
} errorblock;

/* 'libfnproc' entries are set up for each procedure or function in a library */

typedef struct libfnproc {
  struct libfnproc *fpflink;		/* Pointer to next PROC/FN entry */
  byte *fpline;				/* Pointer to start of line containing DEF PROC/FN */
  int32 fphash;				/* Hash value of PROC/FN's name */
  byte *fpname;				/* Pointer to PROC/FN's name in source line */
  byte *fpmarker;			/* Pointer to XFNPROCALL token in executable line */
} libfnproc;

/* 'library' entries describe libraries loaded */

typedef struct library {
  struct library *libflink;		/* Next library in list */
  char *libname;			/* Library name */
  byte *libstart;			/* Pointer to start of library in memory */
  int32 libsize;			/* Size of library */
  libfnproc *libfplist;			/* Pointer to list of procedures and functions in library */
  variable *varlists[VARLISTS];		/* Pointers to lists of variables, procedures and functions in library */
} library;

/* Following are the types describing items found on the Basic stack */

typedef enum {
  STACK_UNKNOWN, STACK_LVALUE, STACK_INT, STACK_FLOAT, STACK_STRING,	/* 04 */
  STACK_STRTEMP, STACK_INTARRAY, STACK_IATEMP, STACK_FLOATARRAY,	/* 08 */
  STACK_FATEMP, STACK_STRARRAY, STACK_SATEMP, STACK_LOCARRAY,		/* 0C */
  STACK_LOCSTRING, STACK_GOSUB, STACK_PROC, STACK_FN, STACK_LOCAL,	/* 11 */
  STACK_RETPARM, STACK_WHILE, STACK_REPEAT, STACK_INTFOR,		/* 15 */
  STACK_FLOATFOR, STACK_ERROR, STACK_DATA, STACK_OPSTACK,		/* 19 */
  STACK_RESTART, STACK_HIGHEST						/* 1B */
} stackitem;

typedef struct {		/* Operator stack */
  stackitem itemtype;		/* Type of item */
  int32 opstack[OPSTACKSIZE];	/* Operator stack */
} stack_opstack;

typedef struct {		/* longjmp environment block for ON ERROR LOCAL */
  stackitem itemtype;		/* Type of item */
  jmp_buf restart;		/* Environment block */
} stack_restart;

typedef struct {		/* Integer value */
  stackitem itemtype;		/* Type of item pushed on to stack */
  int32 intvalue;		/* Value of integer */
} stack_int;

typedef struct {		/* FLoating point value */
  stackitem itemtype;		/* Type of item pushed on to stack */
  float64 floatvalue;		/* Floating point integer */
} stack_float;

typedef struct {		/* String descriptor */
  stackitem itemtype;		/* Type of item pushed on to stack */
  basicstring descriptor;	/* String descriptor */
} stack_string;

typedef struct {		/* Array descriptor */
  stackitem itemtype;		/* Type of item pushed on to stack */
  basicarray *descriptor;	/* Pointer to array descriptor */
} stack_array;

typedef struct {		/* Temporary array descriptor */
  stackitem itemtype;		/* Type of item pushed on to stack */
  basicarray descriptor;	/* Temporary array's descriptor */
} stack_arraytemp;

typedef struct {		/* Local array definition */
  stackitem itemtype;		/* Type of item pushed on to stack */
  int32 arraysize;		/* Size of the array in bytes */
} stack_locarray;

typedef struct {		/* PROC return block */
  stackitem itemtype;
  fnprocinfo fnprocblock;	/* PROC return information */
} stack_proc;

typedef struct {		/* Function return block */
  stackitem itemtype;
  fnprocinfo fnprocblock;	/* Function return information */
  int32 *lastopstop;		/* Saved value of Basic operator stack pointer */
  int32 *lastopstlimit;		/* Saved pointer to end of operator stack */
  jmp_buf *lastrestart;		/* Last function statement restart block for longjmp */
} stack_fn;

typedef struct {		/* GOSUB return block */
  stackitem itemtype;
  gosubinfo gosublock;		/* GOSUB return information */
} stack_gosub;

typedef struct {		/* Saved local variable */
  stackitem itemtype;
  lvalue savedetails;		/* Details of item saved */
  union {
    int32 savedint;		/* Saved integer value */
    float64 savedfloat;		/* Saved floating point value */
    basicstring savedstring;	/* Saved string descriptor */
    basicarray *savedarray;	/* Saved pointer to array descriptor */
  } value;
} stack_local;

typedef struct {		/* Saved RETURN-type local variable */
  stackitem itemtype;
  lvalue savedetails;		/* Details of item saved */
  lvalue retdetails;		/* Details of where to save returned value */
  union {
    int32 savedint;		/* Saved integer value */
    float64 savedfloat;		/* Saved floating point value */
    basicstring savedstring;	/* Saved string descriptor */
    basicarray *savedarray;	/* Saved pointer to array descriptor */
  } value;
} stack_retparm;

typedef struct {		/* WHILE loop control block */
  stackitem itemtype;
  byte *whilexpr;		/* Pointer to expression in WHILE statement */
  byte *whileaddr;		/* Value of 'current' for first statement */
} stack_while;

typedef struct {		/* REPEAT loop control block */
  stackitem itemtype;
  byte *repeataddr;		/* Value of 'current' for first statement in loop */
} stack_repeat;

typedef struct {		/* FOR loop control block */
  stackitem itemtype;
  boolean simplefor;		/* TRUE if an integer variable and incr is +1 */
  lvalue forvar;		/* Details of the 'FOR' loop control variable */
  byte *foraddr;		/* Pointer to first statement in 'FOR' loop */
  union {
    struct {int32 intlimit, intstep;} intfor;
    struct {float64 floatlimit, floatstep;} floatfor;
  } fortype;
} stack_for;

typedef struct {		/* 'LOCAL DATA' control block */
  stackitem itemtype;
  byte *address;		/* Saved 'DATA' pointer */
} stack_data;

typedef struct {		/* 'LOCAL ERROR' control block */
  stackitem itemtype;
  errorblock handler;		/* Preserved error handler detail block */
} stack_error;

typedef union {		/* This type represents everything that goes on the stack */
  stack_int *intsp;
  stack_float *floatsp;
  stack_string *stringsp;
  stack_array *arraysp;
  stack_arraytemp *arraytempsp;
  stack_locarray *locarraysp;
  stack_proc *procsp;
  stack_fn *fnsp;
  stack_gosub *gosubsp;
  stack_local *localsp;
  stack_retparm *retparmsp;
  stack_while *whilesp;
  stack_repeat *repeatsp;
  stack_for *forsp;
  stack_data *datasp;
  stack_error *errorsp;
  stack_opstack *opstacksp;
  stack_restart *restartsp;
  byte *bytesp;
} stack_pointer;

typedef struct cmdarg {		/* Command line argument structure */
  char *argvalue;
  struct cmdarg *nextarg;
} cmdarg;

/*
** 'workspace' gives the layout of the main interpreter data area. Everything concerned
** with running the Basic program is located here (with the exception of the string
** memory management stuff). The idea of doing it this way was that the interpreter
** could deal with several Basic programs at a time
*/
typedef struct {
  byte *workspace;			/* Address of start of Basic's memory */
  int32 worksize;			/* Size of Basic's memory */
  byte *offbase;			/* Indirection op addresses are offsets from here */
  int32 *opstop;			/* Basic operator stack pointer */
  int32 *opstlimit;			/* Pointer to end of operator stack */
  byte *page;				/* The program starts here */
  byte *start;				/* Pointer to the first line of the program */
  byte *top;				/* Address of top of source code */
  byte *lomem;				/* Address of start of variables and data */
  byte *vartop;				/* Address of top of variables and data */
  stack_pointer stacklimit;		/* Point beyond which stack dares not tread */
  stack_pointer stacktop;		/* Basic stack pointer (full, descending stack) */
  stack_pointer safestack;		/* Value Basic stack pointer is set to after an error */
  byte *himem;				/* Address of top of basic stack */
  byte *end;				/* Address of top of address space */
  byte *slotend;			/* Address of end of wimp slot under RISC OS */
  byte *current;			/* Current pointer into Basic program */
  byte *lastvartop;			/* Used to note the address of the top of the Basic heap */
  char *stringwork;			/* Pointer to string workspace */
  jmp_buf restart;			/* For trapping errors */
  int32 error_line;			/* Line number of last error */
  int32 error_number;			/* Number of last error */
  errorblock error_handler;		/* 'ON ERROR' error handler details */
  jmp_buf error_restart;		/* For restarting the interpreter when an 'ON ERROR' is executed */
  jmp_buf *local_restart;		/* For restarting the interpreter when an 'ON ERROR LOCAL' is executed */
  fnprocinfo *procstack;		/* Pointer to return block of current PROC/FN */
  gosubinfo *gosubstack;		/* Pointer to return block of current GOSUB subroutine */
  byte *datacur;			/* Pointer to position in DATA statement */
  library *liblist;			/* Pointer to list of libraries loaded via LIBRARY */
  library *installist;			/* Pointer to list of libraries loaded via INSTALL */
  char *loadpath;			/* List of directories to search for program and libraries */
  struct {
    unsigned int running:1;		/* TRUE if program is running */
    unsigned int loadngo:1;		/* TRUE if program should be loaded and run immediately */
    unsigned int quitatend:1;		/* TRUE if interpreter should be exited at end of program */
    unsigned int start_graphics:1;	/* TRUE if interpreter starts with screen in graphics mode */
    unsigned int outofdata:1;		/* TRUE if program has run out of DATA statements */
    unsigned int has_offsets:1;		/* TRUE if program contains embedded offsets */
    unsigned int has_variables:1;	/* TRUE if any variables have been created */
    unsigned int make_array:1;		/* TRUE if missing arrays should be created */
    unsigned int closefiles:1;		/* TRUE if any open files are closed at the end of the run */
    unsigned int inredir:1;		/* TRUE if input is being taken from s file */
    unsigned int outredir:1;		/* TRUE if output is being redirected */
    unsigned int flag_cosmetic:1;	/* TRUE if all unsupported features flagged as errors */
    unsigned int ignore_starcmd:1;	/* TRUE if built-in '*' commands are ignored */
  } runflags;				/* Various runtime flags */
  struct {
    unsigned int enabled:1;		/* TRUE if any trace options are enabled */
    unsigned int lines:1;		/* TRUE if line numbers are being traced */
    unsigned int procs:1;		/* TRUE if PROC and FN calls/returns are being traced */
    unsigned int pause:1;		/* TRUE if program execution pauses at each new line */
    unsigned int branches:1;		/* TRUE if tracing branches in the code */
    unsigned int backtrace:1;		/* TRUE if a stack backtrace is wanted after an error */
  } traces;				/* Trace options */
  int tracehandle;			/* Handle of file for output from TRACE */
  struct {
    unsigned int space:1;		/* Insert a space before the listed line */
    unsigned int indent:1;		/* Indent statements according to structure */
    unsigned int split:1;		/* Split lines at ':' when listing them */
    unsigned int noline:1;		/* Do not print a line number */
    unsigned int lower:1;		/* List keywords in lower case */
    unsigned int showpage:1;		/* Show 20 lines then pause before continuing */
    unsigned int expand:1;		/* Add extra spaces to lines when listing them */
  } list_flags, listo_copy, edit_flags;	/* LISTO options */
  struct {
    unsigned int debug:1;		/* Show interpreter debugging information */
    unsigned int tokens:1;		/* Show interpreter token debugging information */
    unsigned int variables:1;		/* Show interpreter debugging info for variables */
    unsigned int strings:1;		/* Show allocation/release of memory for strings */
    unsigned int stats:1;		/* Show string heap statistics */
    unsigned int stack:1;		/* Show important stack push/pop info */
    unsigned int allstack:1;		/* Show detailed stack push/pop info */
  } debug_flags;			/* Interpreter debugging options */
  struct {
    unsigned int badprogram:1;		/* TRUE if program is invalid */
    unsigned int trapexcp:1;		/* TRUE if exceptions are trapped by interpreter */
    unsigned int validsaved:1;		/* TRUE if 'savedstart' contains something valid */
    unsigned int validedit:1;		/* TRUE if 'edit_flags' contains something valid */
  } misc_flags;
  byte savedstart[PRESERVED];		/* Save area for start of program when 'NEW' issued */
  int32 curcount;			/* Number of entries on savedcur[] stack*/
  byte *savedcur[MAXCURCOUNT];		/* Stack of saved values of 'current' for EVAL and READ */
  boolean escape;			/* TRUE if the 'escape' key has been pressed */
  int32 retcode;			/* Return code from last OSCLI command */
  int32 argcount;			/* Number of Basic program command line arguments */
  int32 printcount;			/* Chars printed this line (used by PRINT) */
  int32 printwidth;			/* Width of line (used by PRINT) */
  byte *lastsearch;			/* Place last proc/fn search reached */
  int32 linecount;			/* Used when reading a Basic program or library into memory */
  variable staticvars[STDVARS];		/* Static integer variables @%-Z% */
  variable *varlists[VARLISTS];		/* Pointers to lists of variables, procedures and functions */
  char program[FNAMESIZE];		/* Name of program loaded */
  char filename[FNAMESIZE];		/* Name of last file read */
  cmdarg *arglist;			/* Pointer to list of Basic program command line arguments */
} workspace;

extern workspace basicvars;		/* Interpreter variables for the Basic program */

extern void exit_interpreter(int);

#endif
