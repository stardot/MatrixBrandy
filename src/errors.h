/*
** This file is part of the Matrix Brandy Basic VI Interpreter.
** Copyright (C) 2000-2014 David Daniels
** Copyright (C) 2014 Jonathan Harston
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
**	Error messages and error handling
**
** 05-Mar-2014 JGH: Added MOS/OSCLI errors (merged from his Banana Brandy fork)
*/

#ifndef __errors_h
#define __errors_h

/* Basic error numbers */

typedef enum {
    ERR_NONE,           /* No error */
    ERR_UNSUPPORTED,    /* Unsupported feature */
    ERR_UNSUPSTATE,     /* Unsupported statement type */
    ERR_NOGRAPHICS,     /* No graphics available */
    ERR_NOVDUCMDS,      /* VDU commands cannot be used here */
    ERR_SYNTAX,         /* General purpose error */
    ERR_SILLY,          /* A silly error */
    ERR_BADPROG,        /* Corrupted program */
    ERR_ESCAPE,         /* Escape key pressed */
    ERR_STOP,           /* STOP statement */
    ERR_STATELEN,       /* Statement > 1024 chars long */
    ERR_LINENO,         /* Line number out of range */
    ERR_LINEMISS,       /* Line number not found */
    ERR_VARMISS,        /* Unknown variable */
    ERR_ARRAYMISS,      /* Unknown array */
    ERR_FNMISS,         /* Unknown function */
    ERR_PROCMISS,       /* Unknown procedure */
    ERR_TOOMANY,        /* Too many parameters in FN or PROC call */
    ERR_NOTENUFF,       /* Not enough parameters in FN or PROC call */
    ERR_BADDIM,         /* Not enough room to create an array */
    ERR_BADBYTEDIM,     /* Not enough room to create a byte array */
    ERR_NEGDIM,         /* Array dimension is negative */
    ERR_NEGBYTEDIM,     /* Array dimension is negative */
    ERR_DIMCOUNT,       /* Array has too many dimensions */
    ERR_DUPLDIM,        /* Array already defined */
    ERR_BADINDEX,       /* Array index is out of range */
    ERR_INDEXCO,        /* Wrong number of array dimension */
    ERR_DIMRANGE,       /* Array dimension number is out of range */
    ERR_NODIMS,         /* Array dimensions not defined */
    ERR_ADDRESS,        /* Addressing exception */
    WARN_BADTOKEN,      /* Bad token value entered */
    WARN_BADHEX,        /* Bad hexadecimal constant */
    WARN_BADBIN,        /* Bad binary constant */
    WARN_EXPOFLO,       /* Exponent is too large */
    ERR_NAMEMISS,       /* Variable name expected */
    ERR_EQMISS,         /* Mistake (usually '=' missing) */
    ERR_COMISS,         /* ',' missing */
    ERR_LPMISS,         /* '(' missing */
    ERR_RPMISS,         /* ')' missing */
    WARN_QUOTEMISS,     /* '"' missing (warning) */
    ERR_QUOTEMISS,      /* '"' missing */
    ERR_HASHMISS,       /* '#' missing */
    ERR_ENDIF,          /* ENDIF missing */
    ERR_ENDWHILE,       /* ENDWHILE missing */
    ERR_ENDCASE,        /* ENDCASE missing */
    ERR_OFMISS,         /* OF missing */
    ERR_TOMISS,         /* 'TO' missing */
    ERR_CORPNEXT,       /* ',' or ')' expected */
    ERR_NOTWHILE,       /* Not in a 'WHILE' loop */
    ERR_NOTREPEAT,      /* Not in a 'REPEAT' loop */
    ERR_NOTFOR,         /* Not in a 'FOR' loop */
    ERR_DIVZERO,        /* Divide by zero */
    ERR_NEGROOT,        /* Square root of a negative number */
    ERR_LOGRANGE,       /* Log of zero or a negative number */
    ERR_RANGE,          /* General number out of range error */
    ERR_ONRANGE,        /* 'ON' index is out of range */
    ERR_ARITHMETIC,     /* Floating point exception */
    ERR_STRINGLEN,      /* String is too long */
    ERR_BADOPER,        /* Unrecognisable operand */
    ERR_TYPENUM,        /* Type mismatch: number wanted */
    ERR_TYPESTR,        /* Type mismatch: string wanted */
    ERR_PARMNUM,        /* Parameter type mismatch: number wanted */
    ERR_PARMSTR,        /* Parameter type mismatch: string wanted */
    ERR_VARNUM,         /* Type mismatch: numeric variable wanted */
    ERR_VARNUMSTR,      /* Integer or string value wanted */
    ERR_VARARRAY,       /* Type mismatch: array wanted */
    ERR_OFFHEAPARRAY,   /* Type mismatch: off-heap array wanted */
    ERR_INTARRAY,       /* Type mismatch: integer array wanted */
    ERR_FPARRAY,        /* Type mismatch: floating point array wanted */
    ERR_STRARRAY,       /* Type mismatch: string array wanted */
    ERR_NUMARRAY,       /* Type mismatch: numeric array wanted */
    ERR_NOTONEDIM,      /* Array must have only one dimension */
    ERR_TYPEARRAY,      /* Type mismatch: arrays must be the same size */
    ERR_MATARRAY,       /* Type mismatch: cannot perform matrix multiplication on these arrays */
    ERR_NOSWAP,         /* Type mismatch: cannot swap variables of different types */
    ERR_UNSUITABLEVAR,  /* Type mismatch: unsuitable variable type for operation */
    ERR_BADARITH,       /* Cannot perform arithmetic operations on these types of operand */
    ERR_BADEXPR,        /* Syntax error in expression */
    ERR_RETURN,         /* RETURN encountered outside a subroutine */
    ERR_NOTAPROC,       /* Cannot use a function as a procedure */
    ERR_NOTAFN,         /* Cannot use a procedure as a function */
    ERR_ENDPROC,        /* ENDPROC encountered outside a PROC */
    ERR_FNRETURN,       /* Function return encountered outside a FN */
    ERR_LOCAL,          /* LOCAL found outside a PROC or FN */
    ERR_DATA,           /* Out of data */
    ERR_NOROOM,         /* Out of memory */
    ERR_WHENCOUNT,      /* Too many WHEN clauses in CASE statement */
    ERR_SYSCOUNT,       /* Too many parameters found in a SYS statement */
    ERR_STACKFULL,      /* Arithmetic stack overflow */
    ERR_OPSTACK,        /* Operator stack overflow */
    WARN_BADHIMEM,      /* Attempted to set HIMEM to a bad value */
    WARN_BADLOMEM,      /* Attempted to set LOMEM to a bad value */
    WARN_BADPAGE,       /* Attempted to set PAGE to a bad value */
    ERR_LOMEMFIXED,     /* Cannot change LOMEM in a function or procedure */
    ERR_HIMEMFIXED,     /* Cannot change HIMEM here */
    ERR_BADTRACE,       /* Bad TRACE option */
    ERR_ERRNOTOP,       /* Error block not on top of stack */
    ERR_DATANOTOP,      /* DATA pointer not on top of stack */
    ERR_BADMODESC,      /* Bad mode descriptor */
    ERR_BADMODE,        /* Screen mode unavailable */
    WARN_LIBLOADED,     /* Library already loaded */
    ERR_NOLIB,          /* Cannot find library */
    ERR_LIBSIZE,        /* Not enough memory available to load library */
    ERR_NOLIBLOC,       /* LIBRARY LOCAL not at start of library */
    ERR_FILENAME,       /* File name missing */
    ERR_NOTFOUND,       /* Cannot find file */
    ERR_OPENWRITE,      /* Cannot open file for write */
    ERR_OPENIN,         /* File is open for reading, not writing */
    ERR_CANTREAD,       /* Unable to read from file */
    ERR_CANTWRITE,      /* Unable to write to file */
    ERR_HITEOF,         /* Have hit end of file */
    ERR_READFAIL,       /* Cannot read file */
    ERR_NOTCREATED,     /* Cannot create file */
    ERR_WRITEFAIL,      /* Could not finish writing to file */
    ERR_FILEIO,         /* Some other I/O error */
    ERR_CMDFAIL,        /* OS command failed */
    ERR_UNKNOWN,        /* Unexpected signal received */
    ERR_BADHANDLE,      /* Handle is invalid or file associated with it is closed */
    ERR_SETPTRFAIL,     /* File pointer cannot be changed */
    ERR_GETPTRFAIL,     /* File pointer cannot be read */
    ERR_GETEXTFAIL,     /* File size cannot be found */
    ERR_MAXHANDLE,      /* Maximum number of files are already open */
    ERR_INVALIDFNAME,   /* Invalid file name */
    ERR_NOMEMORY,       /* Not enough memory available to run interpreter */
    ERR_BROKEN,         /* Basic program is corrupt or interpreter logic error */
    ERR_COMMAND,        /* Basic command found in program */
    ERR_RENUMBER,       /* RENUMBER failed */
    WARN_LINENO,        /* Line number too large (warning) */
    WARN_LINEMISS,      /* Line number missing (warning) */
    WARN_RPMISS,        /* ')' missing (warning) */
    WARN_RPAREN,        /* Too many ')' (warning) */
    WARN_PARNEST,       /* '()' nested incorrectly */
    ERR_EDITFAIL,       /* Edit session failed */
    ERR_OSCLIFAIL,      /* OSCLI failed */
    ERR_NOGZIP,         /* gzip support not available */
    WARN_FUNNYFLOAT,    /* Unknown floating point format */
    ERR_SWINAMENOTKNOWN,/* SWI name not known */
    ERR_SWINUMNOTKNOWN, /* SWI &xxx not known */
    ERR_DIRNOTFOUND,    /* Directory not found */
    ERR_BADBITWISE,     /* 6, Bitwise operations cannot be performed on these operands */
    ERR_ADDREXCEPT,     /* Address exception - use for segfault handler */
    ERR_PRINTER,        /* Unable to connect to printer */
    ERR_BADVARPROCNAME, /* Bad variable or procedure/function name */
    ERR_BADPROCFNNAME,  /* Bad procedure or function name found at line X */
// From JGH's Banana Brandy fork
    ERR_BADCOMMAND,	/* 254, Bad command */
    ERR_BADSTRING,	/* 253, Bad string */
    ERR_BADNUMBER,	/* 252, Bad number */
    ERR_BADKEY,		/* 251, Bad key */
    ERR_KEYINUSE,	/* 250, Key in use */
    ERR_MOSVERSION,	/* 247, MOS x,yz */
    ERR_BADSYNTAX,	/* 220, Bad syntax */
// Network errors
    ERR_NET_CONNREFUSED,/* 165, Connection refused */
    ERR_NET_NOTFOUND,	/* 213, Host not found */
    ERR_NET_MAXSOCKETS,	/* 192, Maximum number of sockets already open */
    ERR_NET_NOTSUPP,	/* 157, Network operation not supported */
    ERR_NO_RPI_GPIO,	/* 510, Raspberry Pi GPIO not available */
// Dynamic Linker errors
    ERR_DL_NODL,	/* 0, dlopen() and friends not available */
    ERR_DL_NOSYM,	/* 0, Symbol not found */
// Misc errors
    ERR_BAD_OSFILE,	/* 1026, Bad OSFile call */
    ERR_FILELOCKED,	/* 67779, This item is locked */
    ERR_DIRNOTEMPTY,	/* 104884, Directory not empty */
    ERR_NODIR,		/* 104885, Unable to create directory */
    HIGHERROR		/* Leave last, dummy error */
} errnum;

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
extern void show_options(int32);
extern void announce(void);

#endif
