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
**	Error messages and error handling
*/

#ifndef __errors_h
#define __errors_h

/* Basic error numbers */

#define ERR_NONE	0	/* No error */

#define ERR_UNSUPPORTED	1	/* Unsupported feature */
#define ERR_UNSUPSTATE	2	/* Unsupported statement type */
#define ERR_NOGRAPHICS	3	/* No graphics available */
#define ERR_NOVDUCMDS	4	/* VDU commands cannot be used here */
#define ERR_SYNTAX	5	/* General purpose error */
#define ERR_SILLY	6	/* A silly error */
#define ERR_BADPROG	7	/* Corrupted program */
#define ERR_ESCAPE	8	/* Escape key pressed */
#define ERR_STOP	9	/* STOP statement */
#define ERR_STATELEN	10	/* Statement > 1024 chars long */
#define ERR_LINENO	11	/* Line number out of range */
#define ERR_LINEMISS	12	/* Line number not found */
#define ERR_VARMISS	13	/* Unknown variable */
#define ERR_ARRAYMISS	14	/* Unknown array */
#define ERR_FNMISS	15	/* Unknown function */
#define ERR_PROCMISS	16	/* Unknown procedure */
#define ERR_TOOMANY	17	/* Too many parameters in FN or PROC call */
#define ERR_NOTENUFF	18	/* Not enough parameters in FN or PROC call */
#define ERR_FNTOOMANY	19	/* Too many parameters in call to built-in function */
#define ERR_FNNOTENUFF	20	/* Not enough parameters in call to built-in function */
#define ERR_BADRET	21	/* Parameter is not a valid 'return' parameter */
#define ERR_CRASH	22	/* Run into procedure or function */
#define ERR_BADDIM	23	/* Not enough room to create an array */
#define ERR_BADBYTEDIM	24	/* Not enough room to create a byte array */
#define ERR_NEGDIM	25	/* Array dimension is negative */
#define ERR_DIMCOUNT	26	/* Array has too many dimensions */
#define ERR_DUPLDIM	27	/* Array already defined */
#define ERR_BADINDEX	28	/* Array index is out of range */
#define ERR_INDEXCO	29	/* Wrong number of array dimension */
#define ERR_DIMRANGE	30	/* Array dimension number is out of range */
#define ERR_NODIMS	31	/* Array dimensions not defined */
#define ERR_ADDRESS	32	/* Addressing exception */
#define WARN_BADTOKEN	33	/* Bad token value entered */
#define WARN_BADHEX	34	/* Bad hexadecimal constant */
#define WARN_BADBIN	35	/* Bad binary constant */
#define WARN_EXPOFLO	36	/* Exponent is too large */
#define ERR_NAMEMISS	37	/* Variable name expected */
#define ERR_EQMISS	38	/* '=' missing */
#define ERR_COMISS	39	/* ',' missing */
#define ERR_LPMISS	40	/* '(' missing */
#define ERR_RPMISS	41	/* ')' missing */
#define WARN_QUOTEMISS	42	/* '"' missing (warning) */
#define ERR_QUOTEMISS	43	/* '"' missing */
#define ERR_HASHMISS	44	/* '#' missing */
#define ERR_ENDIF	45	/* ENDIF missing */
#define ERR_ENDWHILE	46	/* ENDWHILE missing */
#define ERR_ENDCASE	47	/* ENDCASE missing */
#define ERR_OFMISS	48	/* OF missing */
#define ERR_TOMISS	49	/* 'TO' missing */
#define ERR_CORPNEXT	50	/* ',' or ')' expected */
#define ERR_NOTWHILE	51	/* Not in a 'WHILE' loop */
#define ERR_NOTREPEAT	52	/* Not in a 'REPEAT' loop */
#define ERR_NOTFOR	53	/* Not in a 'FOR' loop */
#define ERR_WRONGFOR	54	/* Cannot match 'FOR' loop */
#define ERR_DIVZERO	55	/* Divide by zero */
#define ERR_NEGROOT	56	/* Square root of a negative number */
#define ERR_LOGRANGE	57	/* Log of zero or a negative number */
#define ERR_RANGE	58	/* General number out of range error */
#define ERR_ONRANGE	59	/* 'ON' index is out of range */
#define ERR_ARITHMETIC	60	/* Floating point exception */
#define ERR_STRINGLEN	61	/* String is too long */
#define ERR_BADOPER	62	/* Unrecognisable operand */
#define ERR_TYPENUM	63	/* Type mismatch: number wanted */
#define ERR_TYPESTR	64	/* Type mismatch: string wanted */
#define ERR_PARMNUM	65	/* Parameter type mismatch: number wanted */
#define ERR_PARMSTR	66	/* Parameter type mismatch: string wanted */
#define ERR_VARNUM	67	/* Type mismatch: numeric variable wanted */
#define ERR_VARSTR	68	/* Type mismatch: string variable wanted */
#define ERR_VARNUMSTR	69	/* Integer or string value wanted */
#define ERR_VARARRAY	70	/* Type mismatch: array wanted */
#define ERR_INTARRAY	71	/* Type mismatch: integer array wanted */
#define ERR_FPARRAY	72	/* Type mismatch: floating point array wanted */
#define ERR_STRARRAY	73	/* Type mismatch: string array wanted */
#define ERR_NUMARRAY	74	/* Type mismatch: numeric array wanted */
#define ERR_NOTONEDIM	75	/* Array must have only one dimension */
#define ERR_TYPEARRAY	76	/* Type mismatch: arrays must be the same size */
#define ERR_MATARRAY	77	/* Type mismatch: cannot perform matrix multiplication on these arrays */
#define ERR_NOSWAP	78	/* Type mismatch: cannot swap variables of different types */
#define ERR_BADCOMP	79	/* Cannot compare these types of operand */
#define ERR_BADARITH	80	/* Cannot perform arithmetic operations on these types of operand */
#define ERR_BADEXPR	81	/* Syntax error in expression */
#define ERR_RETURN	82	/* RETURN encountered outside a subroutine */
#define ERR_NOTAPROC	83	/* Cannot use a function as a procedure */
#define ERR_NOTAFN	84	/* Cannot use a procedure as a function */
#define ERR_ENDPROC	85	/* ENDPROC encountered outside a PROC */
#define ERR_FNRETURN	86	/* Function return encountered outside a FN */
#define ERR_LOCAL	87	/* LOCAL found outside a PROC or FN */
#define ERR_DATA	88	/* Out of data */
#define ERR_NOROOM	89	/* Out of memory */
#define ERR_WHENCOUNT	90	/* Too many WHEN clauses in CASE statement */
#define ERR_SYSCOUNT	91	/* Too many parameters found in a SYS statement */
#define ERR_STACKFULL	92	/* Arithmetic stack overflow */
#define ERR_OPSTACK	93	/* Operator stack overflow */
#define WARN_BADHIMEM	94	/* Attempted to set HIMEM to a bad value */
#define WARN_BADLOMEM	95	/* Attempted to set LOMEM to a bad value */
#define WARN_BADPAGE	96	/* Attempted to set PAGE to a bad value */
#define ERR_NOTINPROC	97	/* Cannot change HIMEM in a function or procedure */
#define ERR_HIMEMFIXED	98	/* Cannot change HIMEM here */
#define ERR_BADTRACE	99	/* Bad TRACE option */
#define ERR_ERRNOTOP	100	/* Error block not on top of stack */
#define ERR_DATANOTOP	101	/* DATA pointer not on top of stack */
#define ERR_BADPLACE	102	/* SPC() or TAB() found outside INPUT or PRINT */
#define ERR_BADMODESC	103	/* Bad mode descriptor */
#define ERR_BADMODE	104	/* Screen mode unavailable */
#define WARN_LIBLOADED	105	/* Library already loaded */
#define ERR_NOLIB	106	/* Cannot find library */
#define ERR_LIBSIZE	107	/* Not enough memory available to load library */
#define ERR_NOLIBLOC	108	/* LIBRARY LOCAL not at start of library */
#define ERR_FILENAME	109	/* File name missing */
#define ERR_NOTFOUND	110	/* Cannot find file */
#define ERR_OPENWRITE	111	/* Cannot open file for write */
#define ERR_OPENUPDATE	112	/* Cannot open file for update */
#define ERR_OPENIN	113	/* File is open for reading, not writing */
#define ERR_CANTREAD	114	/* Unable to read from file */
#define ERR_CANTWRITE	115	/* Unable to write to file */
#define ERR_HITEOF	116	/* Have hit end of file */
#define ERR_READFAIL	117	/* Cannot read file */
#define ERR_NOTCREATED	118	/* Cannot create file */
#define ERR_WRITEFAIL	119	/* Could not finish writing to file */
#define ERR_EMPTYFILE	120	/* Basic program file is empty */
#define ERR_FILEIO	121	/* Some other I/O error */
#define ERR_UNKNOWN	122	/* Unexpected signal received */
#define ERR_CMDFAIL	123	/* OS command failed */
#define ERR_BADHANDLE	124	/* Handle is invalid or file associated with it is closed */
#define ERR_SETPTRFAIL	125	/* File pointer cannot be changed */
#define ERR_GETPTRFAIL	126	/* File pointer cannot be read */
#define ERR_GETEXTFAIL	127	/* File size cannot be found */
#define ERR_MAXHANDLE	128	/* Maximum number of files are already open */
#define ERR_NOMEMORY	129	/* Not enough memory available to run interpreter */
#define ERR_BROKEN	130	/* Basic program is corrupt or interpreter logic error */
#define ERR_COMMAND	131	/* Basic command found in program */
#define ERR_RENUMBER	132	/* RENUMBER failed */
#define WARN_LINENO	133	/* Line number too large (warning) */
#define WARN_LINEMISS	134	/* Line number missing (warning) */
#define WARN_RENUMBERED	135	/* Program renumbered */
#define WARN_RPMISS	136	/* ')' missing (warning) */
#define WARN_RPAREN	137	/* Too many ')' (warning) */
#define WARN_PARNEST	138	/* '()' nested incorrectly */
#define WARN_NEWSIZE	139	/* Size of workspace changed */
#define WARN_ONEFILE	140	/* One file closed */
#define WARN_MANYFILES	141	/* Many files closed */
#define ERR_EDITFAIL	142	/* Edit session failed */
#define ERR_OSCLIFAIL	143	/* OSCLI failed */
#define ERR_NOGZIP	144	/* gzip support not available */
#define WARN_FUNNYFLOAT	145	/* Unknown floating point format */
#define ERR_EMUCMDFAIL	146	/* Emulated RISC OS command failed */
#define ERR_SDL_TIMER   147     /* SDL Timer Error */

#define HIGHERROR	147

/* Other interpreter errors */

#define CMD_NOFILE	1	/* No file name supplied after option */
#define CMD_NOSIZE	2	/* No workspace size supplied after option */
#define CMD_FILESUPP	3	/* File name already supplied */
#define CMD_NOMEMORY	4	/* Not enough memory to run the interpreter */
#define CMD_INITFAIL	5	/* Interpreter initialisation failed */

extern void init_errors(void);
extern void watch_signals(void);
extern void restore_handlers(void);
extern void cmderror(int32, ...);
extern void error(int32, ...);
extern char *get_lasterror(void);
extern void show_error(int32, char *);
extern void set_error(void);
extern void set_local_error(void);
extern void clear_error(void);
extern void show_help(void);
extern void show_options(void);
extern void announce(void);

#endif
