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
**      This module contains the tokenisation routines and functions for
**      manipulating pointers and offsets found in the tokenised form of the
**      Basic program.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include "common.h"
#include "target.h"
#include "basicdefs.h"
#include "tokens.h"
#include "miscprocs.h"
#include "convert.h"
#include "errors.h"

/*
** The format of a tokenised line is as follows:
**
**      <line number>
**      <line length>
**      <offset of first executable token>
**      <copy of source>
**      <NUL>
**      <executable tokens>
**      <NUL>
**
** The line number, line length and offset are all two bytes long.
** The line length is that of the whole line, from the first byte of
** line number to the NUL at the end of the line.
** The offset gives the byte offset from the first byte of the line
** number to the first executable token.
** The original source of the line is held in a slightly compressed
** form. Keywords are replaced by tokens and some extra tokens added
** as markers to note the position of variable names.
** The executable tokens are what the interpreter executes. The line
** is compressed as much as possible. Variables are represented by
** pointers. Initially these give the offset of the variable name in
** the source part of the line from the current location in the
** executable part of it but they are replaced with pointers to the
** variable's symbol entry when the program is running. Numbers are
** converted to their binary form. Offsets are added after tokens
** such as 'ELSE' where a branch occurs that give the offset of branch
** destination. These are filled in when the program runs. A similar
** idea is used to deal with line number references in, for example,
** GOTO statements. Initially the GOTO token is followed by the number
** of the destination line but this is changed to the address of that
** line when the program is run.
** When a program is editted or is run afresh, many or all of the
** offsets and pointers have to be restored to their original state.
** This is where the marker tokens in the source part of the line
** are used. The code scans through the executable tokens and when
** it comes across an entry that has to be changed it looks for the
** corresponding marker token and updates the executable token
** accordingly. References to line numbers in, for example, GOTO
** statements, are handled slightly differently in that the code uses
** the pointer to fetch the number of the line from the start of the
** referenced line and replaces the pointer with that. When a program
** is run afresh or the 'CLEAR' statement is encountered, all pointers
** to variables are reset as the symbol table is destroyed. Branch
** offsets and line number references are left alone. If the program
** is editted then everything is reset.
*/
#define INDENTSIZE 2
#define MAXKWLEN 10
#define NOKEYWORD 255

/*
** 'thisline' contains the tokenised version of the line
** read from the keyboard. The +8 is to allow the end marker
** to be added safely when the line is executed
*/
byte thisline[MAXSTATELEN + 8];

/*
** 'tokenbase' points at the start of the buffer in which
** the tokenised version of the line is stored
*/
static byte *tokenbase;

typedef struct {
  char *name;                   /* Name of token */
  int  length;                  /* Length of token's name */
  int  minlength;               /* Minimum no. of chars for keyword to recognised */
  byte lhtype;                  /* Type of token if at start of statement */
  byte lhvalue;                 /* Token's value if at start of statement */
  byte type;                    /* Type of token if elsewhere in statement */
  byte value;                   /* Token's value if elsewhere in statement */
  boolean alone;                /* TRUE if token is not a token if followed by letter */
  boolean linefollow;           /* TRUE if the token can be followed by a line number */
} token;

/*
** The token table is split into two parts, the first containing all the
** normal Basic keywords, function names and so forth and the second one
** the Basic commands. These appear separately as they can be entered in
** mixed case whereas the rest have to be in upper case
*/
static token tokens [] = {
  {"ABS",       3, 2, TYPE_FUNCTION,    TOKEN_ABS,      TYPE_FUNCTION, TOKEN_ABS,       FALSE,  FALSE}, /* 0 */
  {"ACS",       3, 2, TYPE_FUNCTION,    TOKEN_ACS,      TYPE_FUNCTION, TOKEN_ACS,       FALSE,  FALSE},
  {"ADVAL",     5, 2, TYPE_FUNCTION,    TOKEN_ADVAL,    TYPE_FUNCTION, TOKEN_ADVAL,     FALSE,  FALSE},
  {"AND",       3, 1, TYPE_ONEBYTE,     TOKEN_AND,      TYPE_ONEBYTE, TOKEN_AND,        FALSE,  FALSE},
  {"ARGC",      4, 4, TYPE_FUNCTION,    TOKEN_ARGC,     TYPE_FUNCTION, TOKEN_ARGC,      FALSE,  FALSE},
  {"ARGV$",     5, 5, TYPE_FUNCTION,    TOKEN_ARGVDOL,  TYPE_FUNCTION, TOKEN_ARGVDOL,   FALSE,  FALSE},
  {"ASC",       3, 2, TYPE_FUNCTION,    TOKEN_ASC,      TYPE_FUNCTION, TOKEN_ASC,       FALSE,  FALSE},
  {"ASN",       3, 3, TYPE_FUNCTION,    TOKEN_ASN,      TYPE_FUNCTION, TOKEN_ASN,       FALSE,  FALSE},
  {"ATN",       3, 2, TYPE_FUNCTION,    TOKEN_ATN,      TYPE_FUNCTION, TOKEN_ATN,       FALSE,  FALSE},
  {"BEATS",     5, 2, TYPE_ONEBYTE,     TOKEN_BEATS,    TYPE_ONEBYTE, TOKEN_BEATS,      FALSE,  FALSE}, /* 9 */
  {"BEAT",      4, 4, TYPE_FUNCTION,    TOKEN_BEAT,     TYPE_FUNCTION, TOKEN_BEAT,      FALSE,  FALSE},
  {"BGET",      4, 1, TYPE_FUNCTION,    TOKEN_BGET,     TYPE_FUNCTION, TOKEN_BGET,      TRUE,   FALSE},
  {"BPUT",      4, 2, TYPE_ONEBYTE,     TOKEN_BPUT,     TYPE_ONEBYTE, TOKEN_BPUT,       TRUE,   FALSE},
  {"CALL",      4, 2, TYPE_ONEBYTE,     TOKEN_CALL,     TYPE_ONEBYTE, TOKEN_CALL,       FALSE,  FALSE}, /* 13 */
  {"CASE",      4, 3, TYPE_ONEBYTE,     TOKEN_XCASE,    TYPE_ONEBYTE, TOKEN_XCASE,      FALSE,  FALSE},
  {"CHAIN",     5, 2, TYPE_ONEBYTE,     TOKEN_CHAIN,    TYPE_ONEBYTE, TOKEN_CHAIN,      FALSE,  FALSE},
  {"CHR$",      4, 4, TYPE_FUNCTION,    TOKEN_CHR,      TYPE_FUNCTION, TOKEN_CHR,       FALSE,  FALSE},
  {"CIRCLE",    6, 2, TYPE_ONEBYTE,     TOKEN_CIRCLE,   TYPE_ONEBYTE, TOKEN_CIRCLE,     FALSE,  FALSE},
  {"CLEAR",     5, 2, TYPE_ONEBYTE,     TOKEN_CLEAR,    TYPE_ONEBYTE, TOKEN_CLEAR,      TRUE,   FALSE},
  {"CLOSE",     5, 3, TYPE_ONEBYTE,     TOKEN_CLOSE,    TYPE_ONEBYTE, TOKEN_CLOSE,      TRUE,   FALSE},
  {"CLG",       3, 3, TYPE_ONEBYTE,     TOKEN_CLG,      TYPE_ONEBYTE, TOKEN_CLG,        TRUE,   FALSE},
  {"CLS",       3, 3, TYPE_ONEBYTE,     TOKEN_CLS,      TYPE_ONEBYTE, TOKEN_CLS,        TRUE,   FALSE},
  {"COLOR",     5, 1, TYPE_ONEBYTE,     TOKEN_COLOUR,   TYPE_ONEBYTE, TOKEN_COLOUR,     FALSE,  FALSE}, /* 22 */
  {"COLOUR",    6, 1, TYPE_ONEBYTE,     TOKEN_COLOUR,   TYPE_ONEBYTE, TOKEN_COLOUR,     FALSE,  FALSE},
  {"COS",       3, 3, TYPE_FUNCTION,    TOKEN_COS,      TYPE_FUNCTION, TOKEN_COS,       FALSE,  FALSE},
  {"COUNT",     5, 3, TYPE_FUNCTION,    TOKEN_COUNT,    TYPE_FUNCTION, TOKEN_COUNT,     TRUE,   FALSE},
  {"DATA",      4, 1, TYPE_ONEBYTE,     TOKEN_DATA,     TYPE_ONEBYTE, TOKEN_DATA,       FALSE,  FALSE}, /* 26 */
  {"DEF",       3, 3, TYPE_ONEBYTE,     TOKEN_DEF,      TYPE_ONEBYTE, TOKEN_DEF,        FALSE,  FALSE},
  {"DEG",       3, 2, TYPE_FUNCTION,    TOKEN_DEG,      TYPE_FUNCTION, TOKEN_DEG,       FALSE,  FALSE},
  {"DIM",       3, 3, TYPE_ONEBYTE,     TOKEN_DIM,      TYPE_ONEBYTE, TOKEN_DIM,        FALSE,  FALSE},
  {"DIV",       3, 2, TYPE_ONEBYTE,     TOKEN_DIV,      TYPE_ONEBYTE, TOKEN_DIV,        FALSE,  FALSE},
  {"DRAWBY",    6, 5, TYPE_ONEBYTE,     TOKEN_DRAWBY,   TYPE_ONEBYTE, TOKEN_DRAWBY,     FALSE,  FALSE},
  {"DRAW",      4, 2, TYPE_ONEBYTE,     TOKEN_DRAW,     TYPE_ONEBYTE, TOKEN_DRAW,       FALSE,  FALSE},
  {"ELLIPSE",   7, 3, TYPE_ONEBYTE,     TOKEN_ELLIPSE,  TYPE_ONEBYTE, TOKEN_ELLIPSE,    FALSE,  FALSE}, /* 33 */
  {"ELSE",      4, 2, TYPE_ONEBYTE,     TOKEN_XELSE,    TYPE_ONEBYTE, TOKEN_XELSE,      FALSE,  TRUE},
  {"ENDCASE",   7, 4, TYPE_ONEBYTE,     TOKEN_ENDCASE,  TYPE_ONEBYTE, TOKEN_ENDCASE,    TRUE,   FALSE},
  {"ENDIF",     5, 4, TYPE_ONEBYTE,     TOKEN_ENDIF,    TYPE_ONEBYTE, TOKEN_ENDIF,      TRUE,   FALSE},
  {"ENDPROC",   7, 1, TYPE_ONEBYTE,     TOKEN_ENDPROC,  TYPE_ONEBYTE, TOKEN_ENDPROC,    TRUE,   FALSE},
  {"ENDWHILE",  8, 4, TYPE_ONEBYTE,     TOKEN_ENDWHILE, TYPE_ONEBYTE, TOKEN_ENDWHILE,   TRUE,   FALSE}, /* 38 */
  {"END",       3, 3, TYPE_ONEBYTE,     TOKEN_END,      TYPE_ONEBYTE, TOKEN_END,        TRUE,   FALSE},
  {"ENVELOPE",  8, 3, TYPE_ONEBYTE,     TOKEN_ENVELOPE, TYPE_ONEBYTE, TOKEN_ENVELOPE,   FALSE,  FALSE},
  {"EOF",       3, 3, TYPE_FUNCTION,    TOKEN_EOF,      TYPE_FUNCTION, TOKEN_EOF,       TRUE,   FALSE},
  {"EOR",       3, 3, TYPE_ONEBYTE,     TOKEN_EOR,      TYPE_ONEBYTE, TOKEN_EOR,        FALSE,  FALSE},
  {"ERL",       3, 3, TYPE_FUNCTION,    TOKEN_ERL,      TYPE_FUNCTION, TOKEN_ERL,       TRUE,   FALSE}, /* 43 */
  {"ERROR",     5, 3, TYPE_ONEBYTE,     TOKEN_ERROR,    TYPE_ONEBYTE, TOKEN_ERROR,      FALSE,  FALSE},
  {"ERR",       3, 3, TYPE_FUNCTION,    TOKEN_ERR,      TYPE_FUNCTION, TOKEN_ERR,       TRUE,   FALSE},
  {"EVAL",      4, 2, TYPE_FUNCTION,    TOKEN_EVAL,     TYPE_FUNCTION, TOKEN_EVAL,      FALSE,  FALSE},
  {"EXP",       3, 3, TYPE_FUNCTION,    TOKEN_EXP,      TYPE_FUNCTION, TOKEN_EXP,       FALSE,  FALSE},
  {"EXT",       3, 3, TYPE_FUNCTION,    TOKEN_EXT,      TYPE_FUNCTION, TOKEN_EXT,       TRUE,   FALSE},
  {"FALSE",     5, 2, TYPE_ONEBYTE,     TOKEN_FALSE,    TYPE_ONEBYTE, TOKEN_FALSE,      TRUE,   FALSE}, /* 49 */
  {"FILEPATH$", 9, 4, TYPE_FUNCTION,    TOKEN_FILEPATH, TYPE_FUNCTION, TOKEN_FILEPATH,  FALSE,  FALSE},
  {"FILL",      4, 2, TYPE_ONEBYTE,     TOKEN_FILL,     TYPE_ONEBYTE, TOKEN_FILL,       FALSE,  FALSE},
  {"FN",        2, 2, TYPE_ONEBYTE,     TOKEN_FN,       TYPE_ONEBYTE, TOKEN_FN,         FALSE,  FALSE},
  {"FOR",       3, 1, TYPE_ONEBYTE,     TOKEN_FOR,      TYPE_ONEBYTE, TOKEN_FOR,        FALSE,  FALSE},
  {"GCOL",      4, 2, TYPE_ONEBYTE,     TOKEN_GCOL,     TYPE_ONEBYTE, TOKEN_GCOL,       FALSE,  FALSE}, /* 54 */
  {"GET$",      4, 2, TYPE_FUNCTION,    TOKEN_GETDOL,   TYPE_FUNCTION, TOKEN_GETDOL,    FALSE,  FALSE},
  {"GET",       3, 3, TYPE_FUNCTION,    TOKEN_GET,      TYPE_FUNCTION, TOKEN_GET,       FALSE,  FALSE},
  {"GOSUB",     5, 3, TYPE_ONEBYTE,     TOKEN_GOSUB,    TYPE_ONEBYTE, TOKEN_GOSUB,      FALSE,  TRUE},
  {"GOTO",      4, 1, TYPE_ONEBYTE,     TOKEN_GOTO,     TYPE_ONEBYTE, TOKEN_GOTO,       FALSE,  TRUE},
  {"HIMEM",     5, 1, TYPE_FUNCTION,    TOKEN_HIMEM,    TYPE_FUNCTION, TOKEN_HIMEM,     TRUE,   FALSE}, /* 59 */
  {"IF",        2, 2, TYPE_ONEBYTE,     TOKEN_XIF,      TYPE_ONEBYTE, TOKEN_XIF,        FALSE,  FALSE}, /* 60 */
  {"INKEY$",    6, 3, TYPE_FUNCTION,    TOKEN_INKEYDOL, TYPE_FUNCTION, TOKEN_INKEYDOL,  FALSE, FALSE},
  {"INKEY",     5, 5, TYPE_FUNCTION,    TOKEN_INKEY,    TYPE_FUNCTION, TOKEN_INKEY,     FALSE,  FALSE},
  {"INPUT",     5, 1, TYPE_ONEBYTE,     TOKEN_INPUT,    TYPE_ONEBYTE, TOKEN_INPUT,      FALSE,  FALSE},
  {"INSTR(",    6, 3, TYPE_FUNCTION,    TOKEN_INSTR,    TYPE_FUNCTION, TOKEN_INSTR,     FALSE,  FALSE},
  {"INT",       3, 3, TYPE_FUNCTION,    TOKEN_INT,      TYPE_FUNCTION, TOKEN_INT,       FALSE,  FALSE},
  {"LEFT$(",    6, 2, TYPE_FUNCTION,    TOKEN_LEFT,     TYPE_FUNCTION, TOKEN_LEFT,      FALSE,  FALSE}, /* 66 */
  {"LEN",       3, 3, TYPE_FUNCTION,    TOKEN_LEN,      TYPE_FUNCTION, TOKEN_LEN,       FALSE,  FALSE},
  {"LET",       3, 3, TYPE_ONEBYTE,     TOKEN_LET,      TYPE_ONEBYTE, TOKEN_LET,        FALSE,  FALSE},
  {"LIBRARY",   7, 3, TYPE_ONEBYTE,     TOKEN_LIBRARY,  TYPE_ONEBYTE, TOKEN_LIBRARY,    FALSE,  FALSE}, /* 69 */
  {"LINE",      4, 3, TYPE_ONEBYTE,     TOKEN_LINE,     TYPE_ONEBYTE, TOKEN_LINE,       FALSE,  FALSE},
  {"LN",        2, 2, TYPE_FUNCTION,    TOKEN_LN,       TYPE_FUNCTION, TOKEN_LN,        FALSE,  FALSE},
  {"LOCAL",     5, 3, TYPE_ONEBYTE,     TOKEN_LOCAL,    TYPE_ONEBYTE, TOKEN_LOCAL,      FALSE,  FALSE},
  {"LOG",       3, 3, TYPE_FUNCTION,    TOKEN_LOG,      TYPE_FUNCTION, TOKEN_LOG,       FALSE,  FALSE},
  {"LOMEM",     5, 3, TYPE_FUNCTION,    TOKEN_LOMEM,    TYPE_FUNCTION, TOKEN_LOMEM,     TRUE,   FALSE},
  {"MID$(",     5, 1, TYPE_FUNCTION,    TOKEN_MID,      TYPE_FUNCTION, TOKEN_MID,       FALSE,  FALSE}, /* 75 */
  {"MODE",      4, 2, TYPE_ONEBYTE,     TOKEN_MODE,     TYPE_ONEBYTE, TOKEN_MODE,       FALSE,  FALSE},
  {"MOD",       3, 3, TYPE_ONEBYTE,     TOKEN_MOD,      TYPE_ONEBYTE, TOKEN_MOD,        FALSE,  FALSE},
  {"MOUSE",     5, 3, TYPE_ONEBYTE,     TOKEN_MOUSE,    TYPE_ONEBYTE, TOKEN_MOUSE,      FALSE,  FALSE},
  {"MOVEBY",    6, 6, TYPE_ONEBYTE,     TOKEN_MOVEBY,   TYPE_ONEBYTE, TOKEN_MOVEBY,     FALSE,  FALSE},
  {"MOVE",      4, 3, TYPE_ONEBYTE,     TOKEN_MOVE,     TYPE_ONEBYTE, TOKEN_MOVE,       FALSE,  FALSE},
  {"NEXT",      4, 1, TYPE_ONEBYTE,     TOKEN_NEXT,     TYPE_ONEBYTE, TOKEN_NEXT,       FALSE,  FALSE}, /* 81 */
  {"NOT",       3, 3, TYPE_ONEBYTE,     TOKEN_NOT,      TYPE_ONEBYTE, TOKEN_NOT,        FALSE,  FALSE},
  {"OFF",       3, 3, TYPE_ONEBYTE,     TOKEN_OFF,      TYPE_ONEBYTE, TOKEN_OFF,        FALSE,  FALSE}, /* 83 */
  {"OF",        2, 2, TYPE_ONEBYTE,     TOKEN_OF,       TYPE_ONEBYTE, TOKEN_OF,         FALSE,  FALSE},
  {"ON",        2, 2, TYPE_ONEBYTE,     TOKEN_ON,       TYPE_ONEBYTE, TOKEN_ON,         FALSE,  FALSE}, /* 85 */
  {"OPENIN",    6, 2, TYPE_FUNCTION,    TOKEN_OPENIN,   TYPE_FUNCTION, TOKEN_OPENIN,    FALSE,  FALSE},
  {"OPENOUT",   7, 5, TYPE_FUNCTION,    TOKEN_OPENOUT,  TYPE_FUNCTION, TOKEN_OPENOUT,   FALSE,  FALSE},
  {"OPENUP",    6, 5, TYPE_FUNCTION,    TOKEN_OPENUP,   TYPE_FUNCTION, TOKEN_OPENUP,    FALSE,  FALSE},
  {"ORIGIN",    6, 2, TYPE_ONEBYTE,     TOKEN_ORIGIN,   TYPE_ONEBYTE, TOKEN_ORIGIN,     FALSE,  FALSE},
  {"OR",        2, 2, TYPE_ONEBYTE,     TOKEN_OR,       TYPE_ONEBYTE, TOKEN_OR,         FALSE,  FALSE}, /* 90 */
  {"OSCLI",     5, 2, TYPE_ONEBYTE,     TOKEN_OSCLI,    TYPE_ONEBYTE, TOKEN_OSCLI,      FALSE,  FALSE},
  {"OTHERWISE", 9, 2, TYPE_ONEBYTE,     TOKEN_XOTHERWISE, TYPE_ONEBYTE, TOKEN_XOTHERWISE, FALSE, FALSE},
  {"OVERLAY",   7, 2, TYPE_ONEBYTE,     TOKEN_OVERLAY,  TYPE_ONEBYTE,   TOKEN_OVERLAY,  FALSE,  FALSE},
  {"PAGE",      4, 2, TYPE_FUNCTION,    TOKEN_PAGE,     TYPE_FUNCTION, TOKEN_PAGE,      TRUE,   FALSE}, /* 94 */
  {"PI",        2, 2, TYPE_FUNCTION,    TOKEN_PI,       TYPE_FUNCTION, TOKEN_PI,        TRUE,   FALSE},
  {"PLOT",      4, 2, TYPE_ONEBYTE,     TOKEN_PLOT,     TYPE_ONEBYTE, TOKEN_PLOT,       FALSE,  FALSE},
  {"POINTTO",   7, 7, TYPE_ONEBYTE,     TOKEN_POINTTO,  TYPE_ONEBYTE, TOKEN_POINTTO,    FALSE,  FALSE},
  {"POINTBY",   7, 7, TYPE_ONEBYTE,     TOKEN_POINTBY,  TYPE_ONEBYTE, TOKEN_POINTBY,    FALSE,  FALSE},
  {"POINT(",    6, 2, TYPE_FUNCTION,    TOKEN_POINTFN,  TYPE_FUNCTION, TOKEN_POINTFN,   FALSE,  FALSE},
  {"POINT",     5, 5, TYPE_ONEBYTE,     TOKEN_POINT,    TYPE_ONEBYTE, TOKEN_POINT,      FALSE,  FALSE},
  {"POS",       3, 3, TYPE_FUNCTION,    TOKEN_POS,      TYPE_FUNCTION, TOKEN_POS,       TRUE,   FALSE},
  {"PRINT",     5, 1, TYPE_ONEBYTE,     TOKEN_PRINT,    TYPE_ONEBYTE, TOKEN_PRINT,      FALSE,  FALSE}, /* 102 */
  {"PROC",      4, 4, TYPE_ONEBYTE,     TOKEN_PROC,     TYPE_ONEBYTE, TOKEN_PROC,       FALSE,  FALSE},
  {"PTR",       3, 3, TYPE_FUNCTION,    TOKEN_PTR,      TYPE_FUNCTION, TOKEN_PTR,       TRUE,   FALSE},
  {"QUIT",      4, 1, TYPE_ONEBYTE,     TOKEN_QUIT,     TYPE_ONEBYTE, TOKEN_QUIT,       TRUE,   FALSE}, /* 105 */
  {"RAD",       3, 2, TYPE_FUNCTION,    TOKEN_RAD,      TYPE_FUNCTION, TOKEN_RAD,       FALSE,  FALSE}, /* 106 */
  {"READ",      4, 3, TYPE_ONEBYTE,     TOKEN_READ,     TYPE_ONEBYTE, TOKEN_READ,       FALSE,  FALSE},
  {"RECTANGLE", 9, 3, TYPE_ONEBYTE,     TOKEN_RECTANGLE, TYPE_ONEBYTE, TOKEN_RECTANGLE, FALSE,  FALSE},
  {"REM",       3, 3, TYPE_ONEBYTE,     TOKEN_REM,      TYPE_ONEBYTE, TOKEN_REM,        FALSE,  FALSE},
  {"REPEAT",    6, 3, TYPE_ONEBYTE,     TOKEN_REPEAT,   TYPE_ONEBYTE, TOKEN_REPEAT,     FALSE,  FALSE},
  {"REPORT$",   7, 7, TYPE_FUNCTION,    TOKEN_REPORTDOL, TYPE_FUNCTION, TOKEN_REPORTDOL, FALSE, FALSE},
  {"REPORT",    6, 4, TYPE_ONEBYTE,     TOKEN_REPORT,   TYPE_ONEBYTE, TOKEN_REPORT,     TRUE,   FALSE}, /* 112 */
  {"RESTORE",   7, 3, TYPE_ONEBYTE,     TOKEN_RESTORE,  TYPE_ONEBYTE, TOKEN_RESTORE,    FALSE,  TRUE},
  {"RETURN",    6, 1, TYPE_ONEBYTE,     TOKEN_RETURN,   TYPE_ONEBYTE, TOKEN_RETURN,     TRUE,   FALSE},
  {"RIGHT$(",   7, 2, TYPE_FUNCTION,    TOKEN_RIGHT,    TYPE_FUNCTION, TOKEN_RIGHT,     FALSE,  FALSE},
  {"RND",       3, 2, TYPE_FUNCTION,    TOKEN_RND,      TYPE_FUNCTION, TOKEN_RND,       TRUE,   FALSE},
  {"RUN",       3, 2, TYPE_ONEBYTE,     TOKEN_RUN,      TYPE_ONEBYTE, TOKEN_RUN,        TRUE,   FALSE},
  {"SGN",       3, 2, TYPE_FUNCTION,    TOKEN_SGN,      TYPE_FUNCTION, TOKEN_SGN,       FALSE,  FALSE}, /* 118 */
  {"SIN",       3, 2, TYPE_FUNCTION,    TOKEN_SIN,      TYPE_FUNCTION, TOKEN_SIN,       FALSE,  FALSE},
  {"SOUND",     5, 2, TYPE_ONEBYTE,     TOKEN_SOUND,    TYPE_ONEBYTE, TOKEN_SOUND,      FALSE,  FALSE},
  {"SPC",       3, 3, TYPE_PRINTFN,     TOKEN_SPC,      TYPE_PRINTFN, TOKEN_SPC,        FALSE,  FALSE},
  {"SQR",       3, 3, TYPE_FUNCTION,    TOKEN_SQR,      TYPE_FUNCTION, TOKEN_SQR,       FALSE,  FALSE}, /* 122 */
  {"STEP",      4, 1, TYPE_ONEBYTE,     TOKEN_STEP,     TYPE_ONEBYTE, TOKEN_STEP,       FALSE,  FALSE},
  {"STEREO",    6, 4, TYPE_ONEBYTE,     TOKEN_STEREO,   TYPE_ONEBYTE, TOKEN_STEREO,     FALSE,  FALSE},
  {"STOP",      4, 3, TYPE_ONEBYTE,     TOKEN_STOP,     TYPE_ONEBYTE, TOKEN_STOP,       TRUE,   FALSE},
  {"STR$",      4, 3, TYPE_FUNCTION,    TOKEN_STR,      TYPE_FUNCTION, TOKEN_STR,       FALSE,  FALSE},
  {"STRING$(",  8, 4, TYPE_FUNCTION,    TOKEN_STRING,   TYPE_FUNCTION, TOKEN_STRING,    FALSE,  FALSE}, /* 127 */
  {"SUM",       3, 2, TYPE_FUNCTION,    TOKEN_SUM,      TYPE_FUNCTION, TOKEN_SUM,       FALSE,  FALSE},
  {"SWAP",      4, 2, TYPE_ONEBYTE,     TOKEN_SWAP,     TYPE_ONEBYTE, TOKEN_SWAP,       FALSE,  FALSE},
  {"SYS",       3, 2, TYPE_ONEBYTE,     TOKEN_SYS,      TYPE_ONEBYTE, TOKEN_SYS,        FALSE,  FALSE},
  {"TAB(",      4, 4, TYPE_PRINTFN,     TOKEN_TAB,      TYPE_PRINTFN, TOKEN_TAB,        FALSE,  FALSE}, /* 131 */
  {"TAN",       3, 1, TYPE_FUNCTION,    TOKEN_TAN,      TYPE_FUNCTION, TOKEN_TAN,       FALSE,  FALSE},
  {"TEMPO",     5, 2, TYPE_ONEBYTE,     TOKEN_TEMPO,    TYPE_FUNCTION, TOKEN_TEMPOFN,   FALSE,  FALSE},
  {"THEN",      4, 2, TYPE_ONEBYTE,     TOKEN_THEN,     TYPE_ONEBYTE, TOKEN_THEN,       FALSE,  TRUE},
  {"TIME$",     5, 5, TYPE_FUNCTION,    TOKEN_TIMEDOL,  TYPE_FUNCTION, TOKEN_TIMEDOL,   TRUE,   FALSE},
  {"TIME",      4, 2, TYPE_FUNCTION,    TOKEN_TIME,     TYPE_FUNCTION, TOKEN_TIME,      TRUE,   FALSE},
  {"TINT",      4, 3, TYPE_ONEBYTE,     TOKEN_TINT,     TYPE_ONEBYTE, TOKEN_TINT,       FALSE,  FALSE}, /* 137 */
  {"TO",        2, 3, TYPE_ONEBYTE,     TOKEN_TO,       TYPE_ONEBYTE, TOKEN_TO,         FALSE,  FALSE},
  {"TRACE",     5, 2, TYPE_ONEBYTE,     TOKEN_TRACE,    TYPE_ONEBYTE, TOKEN_TRACE,      FALSE,  FALSE},
  {"TRUE",      4, 3, TYPE_ONEBYTE,     TOKEN_TRUE,     TYPE_ONEBYTE, TOKEN_TRUE,       TRUE,   FALSE},
  {"UNTIL",     5, 1, TYPE_ONEBYTE,     TOKEN_UNTIL,    TYPE_ONEBYTE, TOKEN_UNTIL,      FALSE,  FALSE}, /* 141 */
  {"USR",       3, 2, TYPE_FUNCTION,    TOKEN_USR,      TYPE_FUNCTION, TOKEN_USR,       FALSE,  FALSE},
  {"VAL",       3, 2, TYPE_FUNCTION,    TOKEN_VAL,      TYPE_FUNCTION, TOKEN_VAL,       FALSE,  FALSE}, /* 143 */
  {"VDU",       3, 1, TYPE_ONEBYTE,     TOKEN_VDU,      TYPE_ONEBYTE, TOKEN_VDU,        FALSE,  FALSE},
  {"VERIFY(",   7, 2, TYPE_FUNCTION,    TOKEN_VERIFY,   TYPE_FUNCTION, TOKEN_VERIFY,    FALSE,  FALSE},
  {"VOICES",    6, 2, TYPE_ONEBYTE,     TOKEN_VOICES,   TYPE_ONEBYTE, TOKEN_VOICES,     FALSE,  FALSE},
  {"VOICE",     5, 5, TYPE_ONEBYTE,     TOKEN_VOICE,    TYPE_ONEBYTE, TOKEN_VOICE,      FALSE,  FALSE},
  {"VPOS",      4, 2, TYPE_FUNCTION,    TOKEN_VPOS,     TYPE_FUNCTION, TOKEN_VPOS,      TRUE,   FALSE},
  {"WAIT",      4, 2, TYPE_ONEBYTE,     TOKEN_WAIT,     TYPE_ONEBYTE, TOKEN_WAIT,       TRUE,   FALSE}, /* 149 */
  {"WHEN",      4, 3, TYPE_ONEBYTE,     TOKEN_XWHEN,    TYPE_ONEBYTE, TOKEN_XWHEN,      FALSE,  FALSE},
  {"WHILE",     5, 1, TYPE_ONEBYTE,     TOKEN_XWHILE,   TYPE_ONEBYTE, TOKEN_XWHILE,     FALSE,  FALSE},
  {"WIDTH",     5, 2, TYPE_ONEBYTE,     TOKEN_WIDTH,    TYPE_ONEBYTE, TOKEN_WIDTH,      FALSE,  FALSE},
  {"XLATE$(",   7, 2, TYPE_FUNCTION,    TOKEN_XLATEDOL, TYPE_FUNCTION, TOKEN_XLATEDOL,  FALSE,  FALSE}, /* 153 */
/*
** The following keywords are Basic commands. These can be entered in mixed case.
** Note that 'RUN' is also in here so that it can be entered in lower case too.
** Also note that in the case of commands where there is 'O' version, the
** 'O' version must come first, for example, EDITO must preceed EDIT
*/
  {"APPEND",    6, 2, TYPE_COMMAND,     TOKEN_APPEND,   TYPE_COMMAND, TOKEN_APPEND,     FALSE,  FALSE}, /* 154 */
  {"AUTO",      4, 2, TYPE_COMMAND,     TOKEN_AUTO,     TYPE_COMMAND, TOKEN_AUTO,       FALSE,  FALSE},
  {"CRUNCH",    6, 2, TYPE_COMMAND,     TOKEN_CRUNCH,   TYPE_COMMAND, TOKEN_CRUNCH,     FALSE,  FALSE}, /* 156 */
  {"DELETE",    6, 3, TYPE_COMMAND,     TOKEN_DELETE,   TYPE_COMMAND, TOKEN_DELETE,     FALSE,  FALSE}, /* 157 */
  {"EDITO",     5, 5, TYPE_COMMAND,     TOKEN_EDITO,    TYPE_COMMAND, TOKEN_EDITO,      FALSE,  FALSE},
  {"EDIT",      4, 2, TYPE_COMMAND,     TOKEN_EDIT,     TYPE_COMMAND, TOKEN_EDIT,       FALSE,  FALSE}, /* 159 */
  {"HELP",      4, 2, TYPE_COMMAND,     TOKEN_HELP,     TYPE_COMMAND, TOKEN_HELP,       TRUE,   FALSE}, /* 160 */
  {"INSTALL",   7, 5, TYPE_COMMAND,     TOKEN_INSTALL,  TYPE_COMMAND, TOKEN_INSTALL,    FALSE,  FALSE}, /* 161 */
  {"LISTB",     5, 5, TYPE_COMMAND,     TOKEN_LISTB,    TYPE_COMMAND, TOKEN_LISTB,      FALSE,  FALSE}, /* 162 */
  {"LISTIF",    6, 6, TYPE_COMMAND,     TOKEN_LISTIF,   TYPE_COMMAND, TOKEN_LISTIF,     FALSE,  FALSE},
  {"LISTL",     5, 5, TYPE_COMMAND,     TOKEN_LISTL,    TYPE_COMMAND, TOKEN_LISTL,      FALSE,  FALSE},
  {"LISTO",     5, 5, TYPE_COMMAND,     TOKEN_LISTO,    TYPE_FUNCTION, TOKEN_LISTOFN,   FALSE,  FALSE},
  {"LISTW",     5, 5, TYPE_COMMAND,     TOKEN_LISTW,    TYPE_COMMAND, TOKEN_LISTW,      FALSE,  FALSE},
  {"LIST",      4, 1, TYPE_COMMAND,     TOKEN_LIST,     TYPE_COMMAND, TOKEN_LIST,       FALSE,  FALSE},
  {"LOAD",      4, 2, TYPE_COMMAND,     TOKEN_LOAD,     TYPE_COMMAND, TOKEN_LOAD,       FALSE,  FALSE},
  {"LVAR",      4, 3, TYPE_COMMAND,     TOKEN_LVAR,     TYPE_COMMAND, TOKEN_LVAR,       TRUE,   FALSE},
  {"NEW",       3, 3, TYPE_COMMAND,     TOKEN_NEW,      TYPE_COMMAND, TOKEN_NEW,        TRUE,   FALSE}, /* 170 */
  {"OLD",       3, 1, TYPE_COMMAND,     TOKEN_OLD,      TYPE_COMMAND, TOKEN_OLD,        TRUE,   FALSE}, /* 171 */
  {"QUIT",      4, 1, TYPE_ONEBYTE,     TOKEN_QUIT,     TYPE_ONEBYTE, TOKEN_QUIT,       TRUE,   FALSE}, /* 172 */
  {"RENUMBER",  8, 3, TYPE_COMMAND,     TOKEN_RENUMBER, TYPE_COMMAND, TOKEN_RENUMBER,   FALSE,  FALSE}, /* 173 */
  {"RUN",       3, 2, TYPE_ONEBYTE,     TOKEN_RUN,      TYPE_ONEBYTE, TOKEN_RUN,        TRUE,   FALSE},
  {"SAVEO",     5, 5, TYPE_COMMAND,     TOKEN_SAVEO,    TYPE_COMMAND, TOKEN_SAVEO,      FALSE,  FALSE}, /* 175 */
  {"SAVE",      4, 2, TYPE_COMMAND,     TOKEN_SAVE,     TYPE_COMMAND, TOKEN_SAVE,       FALSE,  FALSE},
  {"TEXTLOAD",  8, 3, TYPE_COMMAND,     TOKEN_TEXTLOAD, TYPE_COMMAND, TOKEN_TEXTLOAD,   FALSE,  FALSE}, /* 177 */
  {"TEXTSAVEO", 9, 9, TYPE_COMMAND,     TOKEN_TEXTSAVEO, TYPE_COMMAND, TOKEN_TEXTSAVEO, FALSE,  FALSE},
  {"TEXTSAVE",  8, 5, TYPE_COMMAND,     TOKEN_TEXTSAVE, TYPE_COMMAND, TOKEN_TEXTSAVE,   FALSE,  FALSE},
  {"TWINO",     5, 2, TYPE_COMMAND,     TOKEN_TWINO,    TYPE_COMMAND, TOKEN_TWINO,      TRUE,   FALSE},
  {"TWIN",      4, 4, TYPE_COMMAND,     TOKEN_TWIN,     TYPE_COMMAND, TOKEN_TWIN,       TRUE,   FALSE},
  {"ZZ",        1, 1, 0, 0, 0, 0, FALSE, FALSE}                                                         /* 182 */
};

#define TOKTABSIZE (sizeof(tokens)/sizeof(token))

static int start_letter [] = {
  0, 9, 13, 26, 33, 49, 54, 59, 60, NOKEYWORD, NOKEYWORD, 66, 75, 81, 83, 94,
  105, 106, 118, 131, 141, 143, 149, 153, NOKEYWORD, NOKEYWORD
};

static int command_start [] = { /* Starting positions for commands in 'tokens' */
  154, NOKEYWORD, 156, 157, 158, NOKEYWORD, NOKEYWORD, 160, 161, NOKEYWORD,
  NOKEYWORD, 162, NOKEYWORD, 170, 171, NOKEYWORD, 172, 173, 175, 177,
  NOKEYWORD, NOKEYWORD, NOKEYWORD, NOKEYWORD, NOKEYWORD, NOKEYWORD
};

static char *lp;        /* Pointer to current position in untokenised Basic statement */

static int
  next,                 /* Index of next free byte in tokenised line buffer */
  source,               /* Index of next byte in source (used when compressing source) */
  brackets,             /* Current bracket nesting depth */
  indentation,          /* Current indentation when listing program */
  lasterror;            /* Number of last error detected when tokenising a line */

static boolean
  linestart,            /* TRUE if at the start of a tokenised line */
  firstitem,            /* TRUE if processing the start of an untokenised Basic statement */
  numbered;             /* TRUE if line starts with a line number */

/*
** 'isempty' returns true if the line passed to it has nothing on it
*/
boolean isempty(byte line[]) {
  return line[OFFSOURCE] == NUL;
}

void save_lineno(byte *where, int32 number) {
  *where = CAST(number, byte);
  *(where+1) = CAST(number>>BYTESHIFT, byte);
}

/*
** 'store_lineno' stores the line number at the start of the
** tokenised line. It is held in the form <low byte> <high byte>.
*/
static void store_lineno(int32 number) {
  if (next+LINESIZE>=MAXSTATELEN) error(ERR_STATELEN);
  tokenbase[next] = CAST(number, byte);
  tokenbase[next+1] = CAST(number>>BYTESHIFT, byte);
  next+=2;
}

/*
** 'store_linelen' stashes the length of the line at the start
** of the tokenised line
*/
static void store_linelen(int32 length) {
  tokenbase[OFFLENGTH] = CAST(length, byte);
  tokenbase[OFFLENGTH+1] = CAST(length>>BYTESHIFT, byte);
}

/*
** 'store_exec' stores the offset of the first executable token
** in the line at the start of the line
*/
static void store_exec(int32 offset) {
  tokenbase[OFFEXEC] = CAST(offset, byte);
  tokenbase[OFFEXEC+1] = CAST(offset>>BYTESHIFT, byte);
}

/*
** 'get_linelen' returns the length of a line. 'p' is assumed to point at the
** start of the line (that is, the line number)
*/
int32 get_linelen(byte *p) {
  return *(p+OFFLENGTH) | *(p+OFFLENGTH+1)<<BYTESHIFT;
}

/*
** 'get_lineno' returns the line number of the line starting at 'p'
*/
int32 get_lineno(byte *p) {
  return *(p+OFFLINE) | *(p+OFFLINE+1)<<BYTESHIFT;
}

/*
** 'get_exec' returns the offset in the line of the first executable
** token. 'p' points at the start of the line. Normally a macro is
** used to do this for speed
*/
int32 get_exec(byte *p) {
  return *(p+OFFEXEC) | *(p+OFFEXEC+1)<<BYTESHIFT;
}

/*
** 'store' is called to add a character to the tokenised line buffer
*/
static void store(byte token) {
  if (next+1>=MAXSTATELEN) error(ERR_STATELEN);
  tokenbase[next] = token;
  next++;
}

/*
** 'store_size' is called to store a two-byte length in the tokenised
** line buffer
*/
static void store_size(int32 size) {
  if (next+SIZESIZE>=MAXSTATELEN) error(ERR_STATELEN);
  tokenbase[next] = CAST(size, byte);
  tokenbase[next+1] = CAST(size>>BYTESHIFT, byte);
  next+=2;
}

/*
** 'store_longoffset' is called to add a long offset (offset from the
** start of the Basic workspace) to the tokenised line buffer.
*/
static void store_longoffset(int32 value) {
  int n;
  if (next+LOFFSIZE>=MAXSTATELEN) error(ERR_STATELEN);
  for (n=1; n<=LOFFSIZE; n++) {
    tokenbase[next] = CAST(value, byte);
    value = value>>BYTESHIFT;
    next++;
  }
}

/*
** 'store_shortoffset' stores a two byte offset in the tokenised
** line buffer These are used for references to lines from the
** current position in the Basic program.
*/
static void store_shortoffset(int32 value) {
  if (next+OFFSIZE>=MAXSTATELEN) error(ERR_STATELEN);
  tokenbase[next] = CAST(value, byte);
  tokenbase[next+1] = CAST(value>>BYTESHIFT, byte);
  next+=2;
}

/*
** 'store_intconst' is called to stow a four byte integer in the tokenised line
** buffer
*/
static void store_intconst(int32 value) {
  int n;
  if (next+INTSIZE>=MAXSTATELEN) error(ERR_STATELEN);
  for (n=1; n<=INTSIZE; n++) {
    tokenbase[next] = CAST(value, byte);
    value = value>>8;
    next++;
  }
}

/*
** 'store_fpvalue' is a grubby bit of code used to store an eight-byte
** floating point value in the tokenised line buffer
*/
static void store_fpvalue(float64 fpvalue) {
  byte temp[sizeof(float64)];
  int n;
  if (next+FLOATSIZE>=MAXSTATELEN) error(ERR_STATELEN);
  memcpy(temp, &fpvalue, sizeof(float64));
  for (n=0; n<sizeof(float64); n++) {
    tokenbase[next] = temp[n];
    next++;
  }
}

/*
** 'convert_lineno' is called when a line number is found to convert
** it to binary. If the line number is too large the error is flagged
** but tokenisation continues
*/
static int32 convert_lineno(void) {
  int32 line;
  line = 0;
  while (*lp>='0' && *lp<='9' && line<=MAXLINENO) {
    line = line*10+(*lp-'0');
    lp++;
  }
  if (line>MAXLINENO) {
    lasterror = ERR_LINENO;
    error(WARN_LINENO); /* Line number is too large */
    line = 0;
    while (*lp>='0' && *lp<='9') lp++;  /* Skip any remaining digits in line number */
  }
  return line;
}

/*
** 'copy_line' is called to copy the remainder of a line to the
** tokenised line buffer
*/
static char *copy_line(char *lp) {
  while (*lp != NUL) {
    store(*lp);
    lp++;
  }
  return lp;
}

/*
** 'nextis' is called to check if the next non-blank characters match
** the string given by 'string'. It returns 'true' if they do. This
** function is used when checking for statement types such as 'DRAW BY'
** as 'DRAW BY' and its ilk are represented by single tokens in this
** interpreter
*/
static boolean nextis(char *string) {
  char *cp;
  cp = skip_blanks(lp);
  return *cp != NUL && strncmp(cp, string, strlen(string)) == 0;
}

/*
** "kwsearch" checks to see if the text passed to it is a token, returning
** the index of the token entry or 'NOKEYWORD' if there is no match. As a
** side effect, it updates the pointer to the untokenised line if a keyword
** if found. Note that there are cases of strings that are tokens unless
** followed by another alphanumeric character, for example, COUNT is a token
** unless followed by a letter, so "COUNT" is a token, but "COUNTER" is not.
** Keywords start with a letter in the range A..W, excluding J and K.
** Commands are a little bit of a problem. To make things more convenient
** the case of Basic commands is normally ignored but this can give problems
** with programs that use variable names such as 'save%' which this code
** would identify as the command 'save'. To cope with this the code only
** ignores the case if the line does not start with a line number. This is
** not perfect but it should get around most problems.
*/
static int kwsearch(void) {
  int n, count, kwlength;
  char first, *cp;
  boolean nomatch, abbreviated;
  char keyword[MAXKWLEN+1];
  cp = lp;
  for (n=0; n<MAXKWLEN && (isalpha(*cp) || *cp == '$' || *cp == '('); n++) {
    keyword[n] = *cp;
    cp++;
  }
  abbreviated = n < MAXKWLEN && *cp == '.';
  if (!abbreviated && n == 1) return NOKEYWORD; /* Text is only one character long - Cannot be a keyword */
  keyword[n] = NUL;
  kwlength = n;
  first = keyword[0];
  if (islower(first))
    nomatch = TRUE;
  else {
    n = start_letter[first-'A'];
    if (n == NOKEYWORD) return NOKEYWORD;       /* No keyword starts with this letter */
    do {
      count = tokens[n].length; /* Decide on number of characters to compare */
      if (abbreviated && kwlength < count) {
        count = kwlength;
        if (kwlength < tokens[n].minlength) count = tokens[n].minlength;
      }
      if (strncmp(keyword, tokens[n].name, count) == 0) break;
      n++;
    } while (*(tokens[n].name) == first);
    nomatch = *(tokens[n].name) != first;
/*
 * Any '.' immediately after a keyword is taken to say that the
 * keyword has been abbreviated but this is not true in the case
 * where we get an exact match between the word read and a keyword,
 * that is, the number of characters in the word read and the
 * keyword are the same. Weed out that case here.
 */
    if (!nomatch && abbreviated) abbreviated = kwlength < tokens[n].length;
  }
  if (nomatch) {        /* Keyword not found. Check if it is a command */
/*
** Kludge time...If the line does not start with a line number, convert
** the keyword to upper case and check if it is a command
*/
    if (numbered && islower(first)) return NOKEYWORD;
    if (!numbered) {    /* Line is not numbered so ignore case of keyword */
      for (n=0; keyword[n] != NUL; n++) keyword[n] = toupper(keyword[n]);
      first = keyword[0];
    }
    n = command_start[first - 'A'];
    if (n == NOKEYWORD) return NOKEYWORD;       /* Text is not a keyword or a command */
    do {
      count = tokens[n].length; /* Decide on number of characters to compare */
      if (abbreviated && kwlength<count) {
        count = kwlength;
        if (kwlength<tokens[n].minlength) count = tokens[n].minlength;
      }
      if (strncmp(keyword, tokens[n].name, count) == 0) break;
      n++;
    } while (*(tokens[n].name) == first);
    nomatch = *(tokens[n].name) != first;
    if (!nomatch && abbreviated) abbreviated = kwlength < tokens[n].length;
  }
  if (nomatch || (!abbreviated && tokens[n].alone && isidchar(keyword[count]))) /* Not a keyword */
    return NOKEYWORD;
  else {        /* Found a keyword */
    lp+=count;
    if (abbreviated && *lp == '.') lp++;        /* Skip '.' after abbreviated keyword */
    return n;
  }
}

/*
** 'copy_keyword' is called when a keyword is found to store its equivalent
** token value and carry out any special processing needed for the type
** of keyword, in particular taking care of the 'firstitem' flag.
** The keywords 'THEN', 'ELSE', 'REPEAT' and 'OTHERWISE' can be followed
** by statements, so the flag has to be set to 'true' for these. Every
** other keyword sets it to 'false'
*/
static void copy_keyword(int token) {
  byte toktype, tokvalue;
  if (firstitem) {      /* Keyword is the first item in the statement */
    toktype = tokens[token].lhtype;
    tokvalue = tokens[token].lhvalue;
    if (linestart && toktype == TYPE_ONEBYTE && tokvalue == TOKEN_XELSE) tokvalue = TOKEN_XLHELSE;
  }
  else {
    toktype = tokens[token].type;
    tokvalue = tokens[token].value;
  }
  firstitem = FALSE;
  if (toktype == TYPE_ONEBYTE) {                /* Check for keywords such as 'DRAW' and 'MOVE' */
    if ((tokvalue == TOKEN_DRAW || tokvalue == TOKEN_MOVE || tokvalue == TOKEN_POINT) && nextis("BY")) {
      tokvalue++;       /* Got 'DRAW BY', 'MOVE BY' or 'POINT BY' */
      lp = skip_blanks(lp)+2;
    }
    else if (tokvalue == TOKEN_POINT && nextis("TO")) {
      tokvalue = TOKEN_POINTTO; /* Got 'POINT TO' */
      lp = skip_blanks(lp)+2;
    }
  }
  if (toktype != TYPE_ONEBYTE) store(toktype);
  store(tokvalue);
  if (tokens[token].name[tokens[token].length-1] == '(') brackets++;    /* Allow for '(' in things like 'TAB(' */
  if (toktype == TYPE_ONEBYTE) {        /* Check for special cases */
    switch (tokvalue) {
    case TOKEN_REM: case TOKEN_DATA: /* Copy rest of line */
      lp = copy_line(lp);
      break;
    case TOKEN_THEN: case TOKEN_REPEAT: case TOKEN_XELSE: case TOKEN_XOTHERWISE:
      firstitem = TRUE; /* Next token must use the 'first in statement' token */
      break;
    case TOKEN_FN: case TOKEN_PROC:     /* Copy proc/function name */
      while(isidchar(*lp)) {
        store(*lp);
        lp++;
      }
      break;
    }
  }
  else if (toktype == TYPE_COMMAND) {
    if (tokvalue == TOKEN_LISTIF || tokvalue == TOKEN_LVAR) {   /* Copy rest of line untranslated */
      lp = copy_line(lp);
    }
  }
}

/*
** 'copy_token' deals with token values directly entered from the
** keyboard. It ensures that the token value is legal
*/
static void copy_token(void) {
  int n;
  byte toktype, tokvalue;
  toktype = TYPE_ONEBYTE;
  tokvalue = *lp;       /* Fetch the token */
  if (tokvalue>=TYPE_COMMAND) {
    toktype = tokvalue;
    lp++;
    tokvalue = *lp;     /* Fetch the actual token */
  }
  lp++;         /* Skip the token */
  if (firstitem) {      /* Token is first item in the statement */
    for (n=0; n<TOKTABSIZE; n++) {
      if (toktype == tokens[n].lhtype && tokvalue == tokens[n].lhvalue) break;
    }
  }
  else {        /* Token is not the first item in the statement */
    for (n=0; n<TOKTABSIZE; n++) {
      if (toktype == tokens[n].type && tokvalue == tokens[n].value) break;
    }
  }
  if (n<TOKTABSIZE)     /* Found token - Copy it to buffer and do token-specific processing */
    copy_keyword(n);
  else {        /* Cannot find token value */
    lasterror = ERR_SYNTAX;
    error(WARN_BADTOKEN);
  }
}

/*
** 'copy_variable' deals with variables. It copies the name to the
** token buffer. The name is preceded by a 'XVAR' token so that the name
** can be found easily when trying to replace pointers to variables'
** symbol table entries with references to their names (see function
** clear_varaddrs() below)
*/
static void copy_variable(void) {
  if (*lp>='@' && *lp<='Z' && lp[1] == '%' && lp[2] != '(' && lp[2] != '[') {   /* Static integer variable */
    store(*lp);
    lp++;
  }
  else {        /* Dynamic variable */
    store(TOKEN_XVAR);
    while (isidchar(*lp)) {
      store(*lp);
      lp++;
    }
  }
  if (*lp == '%' || *lp == '$') {       /* Integer or string variable */
    store(*lp);
    lp++;
  }
}

/*
** 'copy_lineno' copies a line number into the source part of the
** tokenised line. The number is converted to binary to make it
** easier to renumber lines
*/
static void copy_lineno(void) {
  store(TOKEN_XLINENUM);
  store_lineno(convert_lineno());
}

/*
** 'copy_number' copies hex, binary, integer and floating point
** constants to the token buffer
*/
static void copy_number(void) {
  char ch;
  int digits;
  ch = *lp;
  lp++;
  store(ch);
  digits = 0;
  switch (ch) {         /* Copy different types of number */
  case '&':             /* Hex number */
    while (isxdigit(*lp)) {
      store(*lp);
      lp++;
      digits++;
    }
    if (digits == 0) {  /* Number contains no digits */
      lasterror = ERR_SYNTAX;
      error(WARN_BADHEX);
    }
    break;
  case '%':             /* Binary number */
    while (*lp == '0' || *lp == '1') {
      store(*lp);
      lp++;
      digits++;
    }
    if (digits == 0) {  /* Number contains no digits */
      lasterror = ERR_SYNTAX;
      error(WARN_BADBIN);
    }
    break;
  default:              /* Integer or floating point number */
    while (*lp>='0' && *lp<='9') {
      store(*lp);
      lp++;
    }
    if (*lp == '.') {   /* Got digits after a decimal point */
      store('.');
      lp++;
      while (*lp>='0' && *lp<='9') {
        store(*lp);
        lp++;
      }
    }
/*
** Check for an exponent. The code also looks at the character after
** the 'E' and if it is another letter it assumes that the 'E' is part
** of a word that follows the number and does not mark the start of
** an exponent
*/
    if ((*lp == 'e' || *lp == 'E') && !isalpha(*(lp+1))) {
      store(*lp);
      lp++;
      if (*lp == '+' || *lp == '-') {   /* Deal with 'E+<exponent>' or 'E-<exponent>' */
        store(*lp);
        lp++;
      }
      while (*lp>='0' && *lp<='9') {
        store(*lp);
        lp++;
        digits++;
      }
    }
  }
}

/*
** 'copy_string' copies a character string to the tokenised
** line buffer
*/
static void copy_string(void) {
  store('"');           /* Store the quote at the start of the string */
  lp++;
  while (TRUE) {
    if (*lp == NUL) break;      /* Error - Reached end of line without finding a '"' */
    store(*lp);
    if (*lp == '"') {   /* Found a '"' */
      if (*(lp+1) != '"') break;                /* '"' is not followed by '"' so end of string found */
      store('"');       /* Got '""' */
      lp+=2;
    }
    else {      /* Any other character */
      lp++;
    }
  }
  if (*lp == '"')               /* End of string was a '"' */
    lp++;               /* Skip to character after the '"' */
  else {
    lasterror = ERR_QUOTEMISS;
    error(WARN_QUOTEMISS);      /* No terminating '"' found */
    store('"');
  }
}

/*
** 'copy_other' deals with any other characters and special tokens
*/
static void copy_other(void) {
  byte tclass, token;
  tclass = TYPE_ONEBYTE;
  token = *lp;
  switch (token) {      /* Deal with special tokens */
  case '(':
    brackets++;
    break;
  case '[':
    if (!firstitem) brackets++;
    break;
  case ')':
    brackets--;
    if (brackets < 0) { /* More ')' than '(' */
      lasterror = ERR_LPMISS;
      error(WARN_PARNEST);
    }
    break;
  case ']':
    if (!firstitem) {
      brackets--;
      if (brackets < 0) {       /* More ')' than '(' */
        lasterror = ERR_LPMISS;
        error(WARN_PARNEST);
      }
    }
    break;
  case 172:         /* This is '¬' which is a hi-bit char and causes a compiler warning if used directly */
    tclass = TYPE_FUNCTION;
    token = TOKEN_NOT;
    break;
  case '+':
    if (*(lp+1) == '=') {       /* Found '+=' */
      token = TOKEN_PLUSAB;
      lp++;
    }
    break;
  case '-':
    if (*(lp+1) == '=') {       /* Found '-=' */
      token = TOKEN_MINUSAB;
      lp++;
    }
    break;
  case '>':
    switch (*(lp+1)) {
    case '=':           /* Found '>=' */
      token = TOKEN_GE;
      lp++;
      break;
    case '>':
      if (*(lp+2) == '>') {     /* Found '>>>' */
        token = TOKEN_LSR;
        lp+=2;
      }
      else {            /* Found '>>' */
        token = TOKEN_ASR;
        lp++;
      }
    }
    break;
  case '<':
    switch (*(lp+1)) {
    case '=':           /* Found '<=' */
      token = TOKEN_LE;
      lp++;
      break;
    case '>':           /* Found '<>' */
      token = TOKEN_NE;
      lp++;
      break;
    case '<':           /* Found '<<' */
      token = TOKEN_LSL;
      lp++;
    }
    break;
#if defined(TARGET_WIN32) | defined(TARGET_BCC32) | defined(TARGET_MINGW)
  case '|':     /* Window's code for vertical bar is 221, not 124 */
    token = VBAR;
    break;
#endif
  default:
    if (token<' ' && token != TAB) token = ' ';
  }
  if (tclass != TYPE_ONEBYTE) store(tclass);
  store(token);
  if (token == ':')     /* Update the 'first item in statement' flag */
    firstitem = TRUE;
  else if (token != ' ' && token != TAB) {
    firstitem = FALSE;
  }
  lp++;
}

/*
** 'tokenise_sourceline' copies the line starting at 'start' to
** the tokenised line buffer, replacing keywords with tokens
*/
static void tokenise_source(char *start, boolean haslineno) {
  int token;
  char ch;
  boolean linenoposs;
  next = OFFLINE;
  store_lineno(NOLINENO);
  store_linelen(0);
  store_exec(0);
  brackets = 0;
  lasterror = 0;
  linenoposs = FALSE;
  numbered = FALSE;
  lp = skip_blanks(start);
  if (haslineno) {      /* If line can start with a line number, check for one and deal with it */
    next = OFFLINE;
    numbered = *lp>='0' && *lp<='9';
    if (numbered) store_lineno(convert_lineno());       /* Line number found */
    if (basicvars.list_flags.indent)    /* Ignore leading blanks if indenting LISTO option is in effect */
      lp = skip_blanks(lp);
    else {
      while (*lp == ' ' || *lp == TAB) {        /* Copy leading white space characters */
        store(*lp);
        lp++;
      }
    }
  }
  next = OFFSOURCE;
  ch = *lp;
  firstitem = TRUE;     /* Use 'first item in line' tokens where necessary */
  linestart = TRUE;     /* Say that this is the very start of the tokenised line */
  while (ch != NUL) {
    if (isidstart(ch)) {                /* Keyword or identifier */
      if (toupper(ch)>='A' && toupper(ch)<='X') /* Possible keyword */
        token = kwsearch();
      else {
        token = NOKEYWORD;
      }
      if (token != NOKEYWORD) { /* Found keyword */
        copy_keyword(token);
        linenoposs = tokens[token].linefollow;  /* Be ready for a line number */
      }
      else {    /* Variable */
        copy_variable();
        linenoposs = firstitem = FALSE;
      }
    }
    else if (ch == '@' && *(lp+1) == '%') {     /* Buiit-in variable @% */
      copy_variable();
      linenoposs = firstitem = FALSE;
    }
    else if (linenoposs && ch>='0' && ch<='9') {        /* Line number reference */
      copy_lineno();
      firstitem = FALSE;
    }
    else if ((ch>='0' && ch<='9') || ch == '&' || ch == '%' || ch == '.') {     /* Any form of number */
      copy_number();
      linenoposs = firstitem = FALSE;
    }
    else if (ch == '"') {       /* String */
      copy_string();
      linenoposs = firstitem = FALSE;
    }
    else if (firstitem && ch == '*') {  /* Got a '*' command - Copy rest of line */
      store(TOKEN_STAR);
      lp = copy_line(lp+1);
    }
    else if (CAST(ch, byte)>=TOKEN_LOWEST)      /* Token value directly entered */
      copy_token();
    else {      /* Anything else */
      copy_other();
      linenoposs = linenoposs && (ch == ' ' || ch == TAB || ch == ',');
    }
    linestart = FALSE;
    ch = *lp;
  }
  store(NUL);                           /* Add a NUL to make it easier to find the end of the line */
  store_exec(next);
  store(NUL);
  store_linelen(next);                  /* Not necessary but it keeps things tidy */
  next--;                               /* So that the next byte will overwrite the NUL */
  if (brackets<0) {                     /* Too many ')' in line */
    lasterror = ERR_LPMISS;
    error(WARN_RPAREN);
  }
  else if (brackets>0) {                /* Too many '(' in line */
    lasterror = ERR_RPMISS;
    error(WARN_RPMISS);
  }
}

/*
** 'do_keyword' carries out any special processing such as adding
** pointers to the executable version of the tokenised line when a
** keyword token is found
*/
static void do_keyword(void) {
  byte token;
  token = tokenbase[source];
  source++;
  if (token>=TYPE_COMMAND) {            /* Two byte token */
    store(token);
    store(tokenbase[source]);
    if (token == TYPE_COMMAND && (tokenbase[source] == TOKEN_LISTIF || tokenbase[source] == TOKEN_LVAR)) {
     do         /* Find text after LISTIF or LVAR */
       source++;
     while (tokenbase[source] == ' ' || tokenbase[source] == TAB);
     store_shortoffset(next-1-source);  /* Add point to text in source part of line */
     source = -1;       /* That's it for this line */
    }
    else {
      source++;
      firstitem = FALSE;
    }
  }
  else {        /* Found a single byte token */
    store(token);
    firstitem = token == TOKEN_REPEAT || token == TOKEN_THEN || token == TOKEN_XELSE || token == TOKEN_XOTHERWISE;
    switch (token) {            /* Check for special cases */
    case TOKEN_XIF:
      store_shortoffset(0);             /* Store offset of code after 'THEN' */
      store_shortoffset(0);             /* Store offset of code after 'ELSE' */
      break;
    case TOKEN_XELSE: case TOKEN_XLHELSE: case TOKEN_XWHEN: case TOKEN_XOTHERWISE:
    case TOKEN_XWHILE:
      store_shortoffset(0);             /* Store offset of code at end of statement */
      break;
    case TOKEN_XCASE:
      store_longoffset(0);              /* Store pointer to case value table */
      break;
    case TOKEN_FN: case TOKEN_PROC:     /* Replace token with 'X' token and add offset to name */
      next--;           /* Hack, hack... */
      store(TOKEN_XFNPROCALL);
      store_longoffset(next-source);    /* Store offset to PROC/FN name */
      while (isident(tokenbase[source])) source++;      /* Skip PROC/FN name */
      break;
    case TOKEN_REM:     /* Skip rest of tokenised line */
       next--;          /* Remove REM token */
       source = -1;     /* Flag value to say we have finished this line */
       break;
    case TOKEN_DATA:    /* Insert the offset back to the data itself after the DATA token */
      store_shortoffset(next-1-source); /* -1 so that offset is from the DATA token itself */
      source = -1;      /* Flag value to say we have finished this line */
      break;
    case TOKEN_TRACE:   /* Just copy the token that follows TRACE so that it is unmangled */
      while (tokenbase[source] == ' ' || tokenbase[source] == TAB) source++;
      if (tokenbase[source]>TOKEN_LOWEST) {     /* TRACE is followed by a token */
        store(tokenbase[source]);
        source++;
      }
    }
  }
}

/*
** 'do_statvar' deals with the static integer variables
*/
static void do_statvar(void) {
  byte first = tokenbase[source];
  if (tokenbase[source+2] == '?' || tokenbase[source+2] == '!') /* Variable is followed by an indirection operator */
    store(TOKEN_STATINDVAR);
  else {        /* Nice, plain, simple reference to variable */
    store(TOKEN_STATICVAR);
  }
  store(first-'@');             /* Store identifer name mapped to range 0..26 */
  source+=2;
  firstitem = FALSE;
}

/*
** 'do_dynamvar' handles dynamic variables. A 'XVAR' token is inserted into
** the executable portion of the line for the variable followed by the offset
** of the name of the variable from the the XVAR token in the source part
** of the line
*/
static void do_dynamvar(void) {
  source++;
  store(TOKEN_XVAR);
  store_longoffset(next-1-source);      /* Store offset back to name from here */
  while (isident(tokenbase[source])) source++;  /* Skip name */
  if (tokenbase[source] == '%' || tokenbase[source] == '$') source++;   /* Skip integer or string variable marker */
  if (tokenbase[source] == '(' || tokenbase[source] == '[') source++;   /* Skip '(' (is part of name if an array) */
  firstitem = FALSE;
}

/*
** 'do_linenumber' converts a line number to binary and stores it in
** executable token buffer. The line number is preceded by an 'XLINENUM'
** token. When the reference is found the line number will be replaced
** by a pointer to the destination
*/
static void do_linenumber(void) {
  int32 line;
  line = tokenbase[source+1]+(tokenbase[source+2]<<BYTESHIFT);
  store(TOKEN_XLINENUM);
  store_longoffset(line);
  source+=1+LINESIZE;
  firstitem = FALSE;
}

/*
** 'do_number' converts all forms of number (integer, hex, binary and
** floating point) to binary and stores them in the executable token
** buffer
*/
#define INTCONV (MAXINTVAL/10)

static void do_number(void) {
  int32 value;
  static float64 fpvalue;
  boolean isintvalue;
  char *p;
  value = 0;
  isintvalue = TRUE;    /* Number is an integer */
  switch(tokenbase[source]) {
  case '&':     /* Hex value */
    source++;
    while (isxdigit(tokenbase[source])) {
      value = (value<<4)+todigit(tokenbase[source]);
      source++;
    }
    break;
  case '%':     /* Binary value */
    source++;
    while (tokenbase[source] == '0' || tokenbase[source] == '1') {
      value = (value<<1)+(tokenbase[source]-'0');
      source++;
    }
    break;
  default:      /* Decimal or floating point */
    p = tonumber(CAST(&tokenbase[source], char *), &isintvalue, &value, &fpvalue);
    if (p == NIL) {
      lasterror = ERR_BADEXPR;
      error(value);     /* Error found in number - flag it */
      return;
    }
    source = p-CAST(&tokenbase[0], char *);     /* Figure out new value of 'source' */
  }
  firstitem = FALSE;
/* Store the constant in the executable token portion of the line */
  if (isintvalue) {     /* Decide on type of integer constant */
    if (value == 0)
    store(TOKEN_INTZERO);               /* Integer 0 */
    else if (value == 1)
      store(TOKEN_INTONE);              /* Integer 1 */
    else if (value>1 && value<=SMALLCONST) {
      store(TOKEN_SMALLINT);            /* Integer 1..256 */
      store(value-1);                   /* Move number 1..256 to range 0..255 when saved */
    }
    else {
      store(TOKEN_INTCON);              /* Anything else */
      store_intconst(value);
    }
  }
  else {        /* Decide on type of floating point constant */
    if (fpvalue == 0.0)
      store(TOKEN_FLOATZERO);
    else if (fpvalue == 1.0)
      store(TOKEN_FLOATONE);
    else {
      store(TOKEN_FLOATCON);
      store_fpvalue(fpvalue);
    }
  }
}

/*
** 'do_string' is called to copy a character string to the tokenised
** version of a line. The string is held in the form
** <TOKEN> <offset> <length>
** <TOKEN> can be either TOKEN_STRINGCON or TOKEN_QSTRINGCON. The
** difference is that 'QSTRINGCON' strings contain one or more '"'
** characters which means that it is not possible to just push a
** pointer to the string on to the Basic stack. A copy of the string
** has to be made with '""' sequences replaced by '"'. This is
** not going to be very common so two string tokens are used. The
** first one, TOKEN_STRINGCON, will be used in the majority of cases.
** When this token is encountered all the code does is push a pointer
** to the string on to the stack. TOKEN_QSTRINGCON is used as given
** above.
** <offset> is the two byte offset to the first character of the
** string from <TOKEN> and <length> is the two byte length.
** Note that the length stored is the length of the string as it will
** be on the Basic stack, that is, the length allows for '""' pairs,
** for example, the length of "ab""cd" (and the value stored) is
** five characters.
** On entry 'source' is the index of the first '"' in tokenbase. It
** is left pointing at the first character after the '"' at the
** end of the string
*/
static void do_string(void) {
  int start, length;
  boolean quotes;
  source++;             /* Skip the first '"' */
  start = source;       /* Note where the string starts */
  quotes = FALSE;
  length = 0;
  while (TRUE) {
    if (tokenbase[source] == '"') {/* Check for '""' or just '"' */
      source++;         /* Skip one '"' of a '""' pair or the '"' at the end of the string */
      if (tokenbase[source] != '"') break;      /* Found just '"' - At the end of the string */
      quotes = TRUE;    /* String contains '""' */
    }
    source++;
    length++;
  }
  if (quotes)
    store(TOKEN_QSTRINGCON);
  else {
    store(TOKEN_STRINGCON);
  }
  store_shortoffset(next-1-start);      /* Store offset to string in source part of line */
  store_size(length);           /* Store string length */
  firstitem = FALSE;
}

/*
** 'do_star' processes a '*' star (operating system) command
*/
static void do_star(void) {
  do    /* Skip the '*' token at the start of the command */
    source++;
  while (tokenbase[source] == ' ' || tokenbase[source] == TAB || tokenbase[source] == '*');
  if (tokenbase[source] != NUL) {       /* There is something after the '*' */
    store(TOKEN_STAR);
    store_shortoffset(next-1-source);   /* -1 so that offset is from the '*' token itself */
    source = -1;                /* Flag value to say we have finished this line */
  }
}


/*
** 'translate' goes through the tokenised line and constructs the translated
** form of the line. It removes comments as well as any spaces and tabs,
** replaces references to variables with pointers and converts numbers
** to binary.
** On entry, 'next' contains the index of the first byte after the data
** of the 'source' token
*/
static void translate(void) {
  byte token;
  source = OFFSOURCE;           /* Offset of first byte of tokenised source */
  token = tokenbase[source];
  firstitem = TRUE;
  while (token != NUL) {        /* Scan through the tokenised source */
    if (token == TOKEN_STAR)    /* '*' command */
      do_star();
    else if (token>=TOKEN_LOWEST)       /* Have found a keyword token */
      do_keyword();
    else if (token>='@' && token<='Z' && tokenbase[source+1] == '%')
      do_statvar();
    else if (token == TOKEN_XVAR)
      do_dynamvar();
    else if (token == ')' || token == ']') {    /* Handle ')' and ']' ( */
      if (!firstitem && token == ']') token = ')';      /* Assume an array ref - Replace ']' with ')' */
      store(token);
      firstitem = FALSE;
/*
** Now check if the ')' is followed by a '.' (matrix multiplication
** operator) If it is, copy the '.' so that it is not seen as the
** start of a floating point number
*/
      source++;
      if (token == ')') {
        while (tokenbase[source] == ' ' || tokenbase[source] == TAB) source++;
        if (tokenbase[source] == '.') { /* ')' is followed by a '.' - Assume '.' is an operator */
          store('.');
          source++;
        }
      }
    }
    else if (token == TOKEN_XLINENUM)   /* Line number */
      do_linenumber();
    else if ((token>='0' && token<='9') || token == '.' || token == '&' || token == '%')        /* Any form of number */
      do_number();
    else if (token == '\"')     /* String */
      do_string();
    else if (token == ' ' || token == TAB)      /* Discard white space characters */
      source++;
    else if (token == ':') {    /* Handle statement separators */
      store(':');
      do
        source++;
      while (tokenbase[source] == ':' || tokenbase[source] == ' ' || tokenbase[source] == TAB);
      firstitem = TRUE;
    }
    else {      /* Anything else */
      store(token);
      source++;
      firstitem = FALSE;
    }
    if (source == -1 || lasterror>0) break;     /* Translation finished early or an error was found */
    token = tokenbase[source];
  }
  store(NUL);
  store_linelen(next);
}

/*
** 'mark_badline' inserts either an 'END' or a 'BADLINE' token so that
** an attempt to run a program that contains an error detected by
** the tokenisation code will halt if that line is executed. 'END'
** is used if the line has no line number, that is, will be executed
** immediately as an error message will already have been displayed.
** If the line has a line number then it will be included in a program
** and the BADLINE token is halt the program and repeat the error
** message. It assumes that the message is fully contained, that is,
** it will not include the name of a variable or the like
*/
static void mark_badline(void) {
  if (get_lineno(tokenbase) == NOLINENO)        /* Line has no line number - Put an 'END' here */
    store(TOKEN_END);
  else {        /* Line has number - Put a 'bad line' token here */
    store(BADLINE_MARK);
    store(lasterror);           /* Store number of error after token */
  }
  store(NUL);
  store_linelen(next);
}

/*
** "tokenize" is called to tokenize the line of Basic passed to it
** that starts at 'start'. The tokenized version of the line is put
** in the array given by 'tokenbuf'.
** Tokenisation is carried out in two passes:
** 1)  The source is tokenised by replacing keywords with tokens
** 2)  The executable version of the line is created
*/
void tokenize(char *start, byte tokenbuf[], boolean haslineno) {
  tokenbase = tokenbuf;
  tokenise_source(start, haslineno);
  if (lasterror>0)
    mark_badline();
  else {
    translate();
  }
}

/*
** The following table gives the number of characters to skip for each
** token in addition to the one character for the token.
** '-1' indicates that the token is invalid, that is, the program has
** probably been corrupted
*/
static int skiptable [] = {
  0, LOFFSIZE, 1, LOFFSIZE, LOFFSIZE, LOFFSIZE, LOFFSIZE, LOFFSIZE,     /* 00..07 */
  LOFFSIZE, LOFFSIZE, LOFFSIZE, 1, LOFFSIZE, LOFFSIZE, -1, -1,  /* 08..0F */
  0, 0, SMALLSIZE, INTSIZE, 0, 0, FLOATSIZE, OFFSIZE+SIZESIZE,  /* 10..17 */
  OFFSIZE+SIZESIZE, -1, -1, -1, -1, -1, LOFFSIZE, LOFFSIZE,     /* 18..1F */
  -1,  0, -1,  0,  0,  0,  0,  0,                               /* 20..27 */
   0,  0,  0,  0,  0,  0,  0,  0,                               /* 28..2F */
  -1, -1, -1, -1, -1, -1, -1, -1,                               /* 30..37 */
  -1, -1,  0,  0,  0,  0,  0,  0,                               /* 38..3F */
   0, -1, -1, -1, -1, -1, -1, -1,                               /* 40..47 */
  -1, -1, -1, -1, -1, -1, -1, -1,                               /* 48..4F */
  -1, -1, -1, -1, -1, -1, -1, -1,                               /* 50..57 */
  -1, -1, -1,  0,  0,  0,  0,  0,                               /* 58..5F */
   0, -1, -1, -1, -1, -1, -1, -1,                               /* 60..67 */
  -1, -1, -1, -1, -1, -1, -1, -1,                               /* 68..6F */
  -1, -1, -1, -1, -1, -1, -1, -1,                               /* 70..77 */
  -1, -1, -1,  0,  0,  0,  0, -1,                               /* 78..7F */
  0, 0, 0, 0, 0, 0, 0, 0,                                       /* 80..87 */
  0, 0, 0, 0, 0, 0, 0, 0,                                       /* 88..8F */
  LOFFSIZE, LOFFSIZE, 0, 0, 0, 0, 0, 0,                         /* 90..97 */ /* CASE */
  0, OFFSIZE, 0, 0, 0, 0, 0, OFFSIZE,                           /* 98..9F */ /* DATA, ELSE */
  OFFSIZE, OFFSIZE, OFFSIZE, 0, 0, 0, 0, 0,                     /* A0..A7 */ /* ELSE */
  0, 0, 0, 0, 0, 0, 0, 0,                                       /* A8..AF */
  0, 0, 2*OFFSIZE, 2*OFFSIZE, 2*OFFSIZE, 0, 0, 0,               /* B0..B7 */ /* IF */
  0, 0, 0, 0, 0, 0, 0, 0,                                       /* B8..BF */
  0, 0, 0, 0, 0, OFFSIZE, OFFSIZE, 0,                           /* C0..C7 */ /* OTHERWISE */
  0, 0, 0, 0, 0, 0, 0, 0,                                       /* C8..CF */
  0, 0, 0, 0, 0, 0, 0, 0,                                       /* D0..D7 */
  OFFSIZE, 0, 0, 0, 0, 0, 0, 0,                                 /* D8..DF */ /* *command */
  0, 0, 0, 0, 0, 0, 0, 0,                                       /* E0..E7 */
  0, OFFSIZE, OFFSIZE, OFFSIZE, OFFSIZE, 0, -1, -1,             /* E8..EF */ /* WHEN, WHILE */
  -1, -1, -1, -1, -1, -1, -1, -1,                               /* F0..F7 */
  -1, -1, -1, -1, 1, 1, 1, 1                                    /* F8..FF */
};

/*
** 'skip_token' returns a pointer to the token following the
** one pointed at by 'p'.
** Note that this code does not handle 'listif' and 'lvar'
** properly
*/
byte *skip_token(byte *p) {
  int size;
  if (*p == NUL) return p;      /* At end of line */
  size = skiptable[*p];
  if (size>=0) return p+1+size;
  error(ERR_BADPROG);   /* Not a legal token value - Program has been corrupted */
  return 0;
}

/*
** 'skip_name' returns a pointer to the byte after the variable name that
** starts at 'p'
*/
byte *skip_name(byte *p) {
  do
    p++;
  while (ISIDCHAR(*p));
  if (*p == '%' || *p == '$') p++;      /* If integer or string, skip the suffix character */
  if (*p == '(' || *p == '[') p++;      /* If an array, the first '(' or '[' is part of the name so skip it */
  return p;
}

/*
** 'get_intvalue' extracts a four byte integer constant from the tokenised
** form of a Basic statement. 'ip' points at the 'integer constant' token.
** Beware of this: the macro version of the function, GET_INTVALUE, expects
** a pointer *to the value itself*
*/
int32 get_intvalue(byte *ip) {
  return *(ip+1) | *(ip+2)<<8 | *(ip+3)<<16 | *(ip+4)<<24;
}

/*
** 'get_address' returns an address derived from a four byte offset
** in the tokenised code. 'p' points at the token *before* the offset.
** The offset is a four byte unsigned integer and the offset is from
** the start of the Basic workspace. This is used rather than an
** offset from the token (the usual way in this program) so that the
** offset will always be positive. Using the token address ('p' in
** this case) could result in negative offsets, which would give sign
** problems on machines where an integer is not four bytes long
*/
byte *get_address(byte *p) {
  return basicvars.workspace+(*(p+1) | *(p+2)<<8 | *(p+3)<<16 | *(p+4)<<24);
}


/*
** 'get_linenum' returns the line number following the line number token
** at 'lp'
*/
int32 get_linenum(byte *lp) {
  return *(lp+1) | *(lp+2)<<BYTESHIFT;
}

/*
** 'set_linenum' stores a line number following the line number token
** at 'lp'
*/
static void set_linenum(byte *lp, int32 line) {
  *(lp+1) = CAST(line, byte);
  *(lp+2) = CAST(line>>BYTESHIFT, byte);
}

/*
** 'get_fpvalue' extracts an eight-byte floating point constant from the
** tokenised form of a Basic statement. 'fp' points at the 'floating point
** constant' token
*/
float64 get_fpvalue(byte *fp) {
  static float64 fpvalue;
  memcpy(&fpvalue, fp+1, sizeof(float64));
  return fpvalue;
}

static char *onebytelist [] = { /* Token -> keyword name */
  "AND", ">>", "DIV", "EOR", ">=", "<=", "<<", ">>>",                           /* 80..87 */
  "-=", "MOD", "<>", "OR", "+=", "BEATS", "BPUT", "CALL",                       /* 88..8F */
  "CASE", "CASE", "CHAIN", "CIRCLE", "CLG", "CLEAR", "CLOSE", "CLS",            /* 90..97 */
  "COLOUR", "DATA", "DEF", "DIM", "DRAW", "DRAW BY", "ELLIPSE", "ELSE",         /* 98..9F */
  "ELSE", "ELSE", "ELSE", "END", "ENDCASE","ENDIF", "ENDPROC", "ENDWHILE",      /* A0..A7 */
  "ENVELOPE", "ERROR", "FALSE", "FILL", "FILL BY", "FN", "FOR", "GCOL",         /* A8..AF */
  "GOSUB", "GOTO", "IF","IF", "IF", "INPUT", "LET", "LIBRARY",                  /* B0..B7 */
  "LINE", "LOCAL", "MODE", "MOUSE", "MOVE", "MOVE BY", "NEXT", "NOT",           /* B8..BF */
  "OF", "OFF", "ON", "ORIGIN", "OSCLI", "OTHERWISE", "OTHERWISE", "OVERLAY",    /* C0..C7 */
  "PLOT", "POINT", "POINT BY", "POINT TO", "PRINT", "PROC", "QUIT", "READ",     /* C8..CF */
  "RECTANGLE", "REM", "REPEAT", "REPORT", "RESTORE", "RETURN", "RUN", "SOUND",  /* D0..D7 */
  "*", "STEP", "STEREO", "STOP", "SWAP", "SYS", "TEMPO", "THEN",                /* D8..DF */
  "TINT", "TO", "TRACE", "TRUE", "UNTIL", "VDU", "VOICE", "VOICES",             /* E0..E7 */
  "WAIT", "WHEN", "WHEN", "WHILE", "WHILE", "WIDTH",  NIL, NIL,                 /* E8..EF */
  NIL, NIL, NIL, NIL, NIL, NIL, NIL, NIL,                                       /* F0..F7 */
  NIL, NIL, NIL, NIL, NIL, NIL, NIL, NIL                                        /* F8..FF */
};

static char *commandlist [] = { /* Token -> command name */
  NIL, "APPEND", "AUTO", "CRUNCH", "DELETE", "EDIT", "EDITO", "HELP",           /* 00..07 */
  "INSTALL", "LIST", "LISTB", "LISTIF", "LISTL", "LISTO", "LISTW", "LOAD",      /* 08..0F */
  "LVAR", "NEW", "OLD", "RENUMBER", "SAVE", "SAVEO", "TEXTLOAD", "TEXTSAVE",    /* 10..17 */
  "TEXTSAVEO", "TWIN", "TWINO"                                                  /* 18..1A */
};

static char *functionlist [] = {        /* Functions -> function name */
  NIL, "HIMEM", "EXT", "FILEPATH$", "LEFT$(", "LOMEM", "MID$(", "PAGE",         /* 00..07 */
  "PTR", "RIGHT$(", "TIME", "TIME$", NIL, NIL, NIL, NIL,                        /* 08..0F */
  "ABS", "ACS", "ADVAL", "ARGC", "ARGV$", "ASC", "ASN", "ATN",                  /* 10..17 */
  "BEAT", "BGET", "CHR$", "COS", "COUNT", "DEG", "EOF", "ERL",                  /* 18..1F */
  "ERR", "EVAL", "EXP", "GET", "GET$", "INKEY", "INKEY$", "INSTR(",             /* 20..27 */
  "INT", "LEN", "LISTO", "LN", "LOG", "OPENIN","OPENOUT", "OPENUP",             /* 28..2F */
  "PI", "POINT(", "POS", "RAD", "REPORT$", "RETCODE", "RND", "SGN",             /* 30..37 */
  "SIN", "SQR", "STR$", "STRING$(", "SUM", "TAN", "TEMPO", "USR",               /* 38..3F */
  "VAL", "VERIFY(", "VPOS", "XLATE$("                                                   /* 40..44 */
};

static char *printlist [] = {NIL, "SPC", "TAB("};

/*
** 'expand_token' is called to expand the token passed to it to its textual
** form. The function returns the length of the expanded form
*/
static int expand_token(char *cp, char *namelist[], byte token) {
  int n, count;
  char *name;
  name = namelist[token];
  if (name == NIL) error(ERR_BROKEN, __LINE__, "tokens");       /* Sanity check for bad token value */
  strcpy(cp, name);
  count = strlen(name);
  if (basicvars.list_flags.lower) {     /* Lower case version of name required */
    for (n=0; n<count; n++) {
      *cp = tolower(*cp);
      cp++;
    }
  }
  return count;
}

static byte *skip_source(byte *p) {
  byte token;
  token = *p;
  if (token == NUL) return p;
  if (token == TOKEN_XLINENUM) return p+1+LINESIZE;
  if (token>=TYPE_COMMAND) return p+2;  /* Two byte token */
  return p+1;
}

/*
** 'expand' takes the tokenised line passed to it and expands it to its
** original form in the buffer provided. 'line' points at the very start of
** the tokenised line
*/
void expand(byte *line, char *text) {
  byte token;
  byte *lp;
  int n, count, thisindent, nextindent;
  if (!basicvars.list_flags.noline) {   /* Include line number */
    sprintf(text, "%5d", get_lineno(line));
    text+=5;
    if (basicvars.list_flags.space) {   /* Need a blank before the expanded line */
      *text = ' ';
      text++;
    }
  }
  lp = line+OFFSOURCE;  /* Point at start of code after source token */
  if (basicvars.list_flags.indent) {    /* Indent line */
    lp = skip(lp);      /* Start by figuring out if indentation changes */
    thisindent = nextindent = indentation;
    switch (*lp) {      /* First look at special cases where first token on line affects indentation */
    case TOKEN_DEF:
      thisindent = nextindent = 0;
      break;
    case TOKEN_LHELSE: case TOKEN_XLHELSE: case TOKEN_WHEN: case TOKEN_XWHEN:
    case TOKEN_OTHERWISE: case TOKEN_XOTHERWISE:
      thisindent-=INDENTSIZE;
      if (thisindent<0) thisindent = 0;
      nextindent = thisindent+INDENTSIZE;
      break;
/*    case TOKEN_REM: case TOKEN_DATA:
      thisindent = 0;
      break;*/
    case TOKEN_ENDIF: case TOKEN_ENDCASE:
      thisindent-=INDENTSIZE;
      nextindent-=INDENTSIZE;
      break;
    }
    while (*lp != NUL) {
      switch(*lp) {
      case TOKEN_WHILE: case TOKEN_XWHILE: case TOKEN_REPEAT: case TOKEN_FOR:
      case TOKEN_CASE: case TOKEN_XCASE:
        nextindent+=INDENTSIZE;
        break;
      case TOKEN_THEN:
        if (*(lp+1) == NUL) nextindent+=INDENTSIZE;     /* Block IF */
        break;
      case TOKEN_ENDWHILE: case TOKEN_UNTIL:
        if (nextindent == thisindent) thisindent-=INDENTSIZE;
        nextindent-=INDENTSIZE;
        break;
      case TOKEN_NEXT:
        if (nextindent == thisindent) thisindent-=INDENTSIZE;
        nextindent-=INDENTSIZE;
        lp = skip_source(lp);
        while (*lp != NUL && *lp != ':' && *lp != TOKEN_XELSE && *lp != TOKEN_ELSE) { /* Check for 'NEXT I%,J%,K%' */
          if (*lp == ',')  nextindent-=INDENTSIZE;
          lp = skip_source(lp);
        }
        break;
      }
      lp = skip_source(lp);
    }
    if (thisindent<0) thisindent = 0;
    if (nextindent<0) nextindent = 0;
    for (n=1; n<=thisindent; n++) {
      *text = ' ';
      text++;
    }
    indentation = nextindent;
    lp = skip(line+OFFSOURCE);
  }
  token = *lp;
/* Indentation sorted out. Now expand the line */
  while (token != NUL) {
/* Deal with special cases first */
    if (token == TOKEN_XLINENUM) {      /* Line number */
      lp++;
      count = sprintf(text, "%d", get_lineno(lp));
      text+=count;
      lp+=LINESIZE;
    }
    else if (token == TOKEN_XVAR)       /* Marks start of variable name - Ignore */
      lp++;
    else if (token == '"') {    /* Character string */
      do {      /* Copy characters up to next '"' */
        *text = *lp;
        text++;
        lp++;
      } while (*lp != '"' && *lp != NUL);
      if (*lp == '"') { /* '"' at end of string */
        *text = '"';
        text++;
        lp++;
      }
    }
    else if (token<TOKEN_LOWEST) {      /* Normal characters */
      *text = token;
      text++;
      lp++;
    }
    else if (token == TOKEN_DATA || token == TOKEN_REM) {       /* 'DATA' and 'REM' are a special case */
      count = expand_token(text, onebytelist, token-TOKEN_LOWEST);
      text+=count;
      lp++;
      while (*lp != NUL) {      /* Copy rest of line after 'DATA' or ' REM' */
        *text = *lp;
        text++;
        lp++;
      }
    }
    else {      /* Single byte tokens */
      switch (token) {
      case TYPE_PRINTFN:
        lp++;
        token = *lp;
        if (token>TOKEN_TAB) error(ERR_BADPROG);
        count = expand_token(text, printlist, token);
        break;
      case TYPE_FUNCTION:       /* Built-in Function */
        lp++;
        token = *lp;
        if (token>TOKEN_XLATEDOL) error(ERR_BADPROG);
        count = expand_token(text, functionlist, token);
        break;
      case TYPE_COMMAND:
        lp++;
        token = *lp;
        if (token>TOKEN_TWINO) error(ERR_BADPROG);
        count = expand_token(text, commandlist, token);
        break;
      default:
        count = expand_token(text, onebytelist, token-TOKEN_LOWEST);
      }
      text+=count;
      lp++;
    }
    token = *lp;
  }
  *text = NUL;
}

void reset_indent(void) {
  indentation = 0;
}

/*
** 'set_dest' stores a branch destination in the tokenised code at 'tp'.
** The destination is given as the number of bytes to skip from the address
** of the first byte of the offset. This will always be a forward branch,
** so the offset will be an integer value in the range 0..65535. There is no
** check to ensure that the value is in range. It would require something like
** a 1,000 line-long 'IF' statement to exceed it.
*/
void set_dest(byte *tp, byte *dest) {
  int32 offset;
  offset = dest-tp;
  *(tp) = CAST(offset, byte);
  *(tp+1) = CAST(offset>>BYTESHIFT, byte);
  basicvars.runflags.has_offsets = TRUE;
}

/*
** 'set_address' is called to store a pointer in the tokenised code.
** 'tp' points *at the token before* where the address is to go and 'p'
** is the address to be stored.
** The value stored is the four byte offset of the variable
** from the start of the Basic workspace, not its address.
*/
void set_address(byte *tp, void *p) {
  int n, offset;
  basicvars.runflags.has_offsets = TRUE;
  offset = CAST(p, byte *)-basicvars.workspace;
  for (n=0; n<LOFFSIZE; n++) {
    tp++;
    *tp = CAST(offset, byte);
    offset = offset>>BYTESHIFT;
  }
}

/*
** 'get_srcaddr' returns the address of a byte in the source part
** of a line. This is given as an offset from the address of the
** token at 'p'. The offset is stored in the two bytes after the
** token
*/
byte *get_srcaddr(byte *p) {
  return p-(*(p+1)+(*(p+2)<<BYTESHIFT));
}

/*
** 'clear_varaddrs' goes through the line passed to it and resets any variable
** or procedure references to 'unknown'.
*/
static void clear_varaddrs(byte *bp) {
  byte *sp, *tp;
  int offset;
  sp = bp+OFFSOURCE;            /* Point at start of source code */
  tp = FIND_EXEC(bp);           /* Get address of start of executable tokens */
  while (*tp != NUL) {
    if (*tp == TOKEN_XVAR || (*tp >= TOKEN_INTVAR && *tp <= TOKEN_FLOATINDVAR)) {
      while (*sp != TOKEN_XVAR && *sp != NUL) sp = skip_source(sp);     /* Locate variable in source part of line */
      if (*sp == NUL) error(ERR_BROKEN, __LINE__, "tokens");            /* Cannot find variable - Logic error */
      sp++;     /* Point at first char of name */
      if (*tp != TOKEN_XVAR) {
        *tp = TOKEN_XVAR;
        offset = tp-sp;         /* Offset from 'XVAR' token to variable name */
        *(tp+1) = CAST(offset, byte);
        *(tp+2) = CAST(offset>>BYTESHIFT, byte);
      }
    }
    else if (*tp == TOKEN_FNPROCALL || *tp == TOKEN_XFNPROCALL) {
      while (*sp != TOKEN_PROC && *sp != TOKEN_FN && *sp != NUL) sp++;  /* Find PROC/FN name */
      if (*sp == NUL) error(ERR_BROKEN, __LINE__, "tokens");
      if (*tp == TOKEN_FNPROCALL) {     /* Reset PROC/FN ref that has been filled in */
        *tp = TOKEN_XFNPROCALL;
        offset = tp-sp;         /* Offset from 'XVAR' token to variable name */
        *(tp+1) = CAST(offset, byte);
        *(tp+2) = CAST(offset>>BYTESHIFT, byte);
      }
      sp++;     /* Skip PROC or FN token */
    }
    else if (*tp == TOKEN_CASE) {
      *tp = TOKEN_XCASE;
    }
    tp = skip_token(tp);
  }
}

/*
** 'clear_branches' goes through the line passed to it and resets any
** branch-type to their 'unknown destination' versions. The code, in
** general, assumes that the 'unknown destination' token has a value
** one less than the 'offset filled in' version
*/
void clear_branches(byte *bp) {
  byte *tp, *lp;
  int line;
  tp = FIND_EXEC(bp);
  while (*tp != NUL) {
    switch (*tp) {
    case TOKEN_LINENUM:
      *tp = TOKEN_XLINENUM;     /* Reset to 'X' version of token */
      lp = get_address(tp);     /* Find the line the token refers to */
      line = get_lineno(find_linestart(lp));    /* Find the number of the line refered to */
      *(tp+1) = CAST(line, byte);       /* Store the line number */
      *(tp+2) = CAST(line>>BYTESHIFT, byte);
      break;
    case TOKEN_BLOCKIF: case TOKEN_SINGLIF:
      *tp = TOKEN_XIF;
      break;
    case TOKEN_ELSE: case TOKEN_LHELSE: case TOKEN_WHEN: case TOKEN_OTHERWISE: case TOKEN_WHILE:
      (*tp)--;  /* Reset to 'X' version of token */
    }
    tp = skip_token(tp);
  }
}

void clear_linerefs(byte *bp) {
  clear_branches(bp);
  clear_varaddrs(bp);
}

/*
** 'clear refs' is called to restore all the 'embedded pointer' tokens
** to their 'no address' versions in the program loaded and any
** permanent libraries loaded via the 'install' command. This is needed
** when a program is edited or when the 'CLEAR' statement is executed.
** This process is not needed for libraries loaded via the 'library'
** statement as these libraries will have been discarded at this point
*/
void clear_varptrs(void) {
  byte *bp;
  library *lp;
  bp = basicvars.start;
  while (!AT_PROGEND(bp)) {
    clear_varaddrs(bp);
    bp = bp+GET_LINELEN(bp);
  }
  lp = basicvars.installist;    /* Now clear the pointers in any installed libraries */
  while (lp != NIL) {
    bp = lp->libstart;
    while (!AT_PROGEND(bp)) {
      clear_varaddrs(bp);
      bp = bp+GET_LINELEN(bp);
    }
    lp = lp->libflink;
  }
}


/*
** 'isvalid' is called to examine the line passed to it to check that it is
** valid. It checks:
** 1) That the line number is in the range 0..65279
** 2) That the line length is between 4 and 1024 bytes
** 3) That all the tokens in the line are in range
*/
static boolean legalow [] = {   /* Tokens in range 00.1F */
  FALSE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE,              /* 00..07 */
  TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, FALSE,             /* 08..0F */
  TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE,               /* 10..17 */
  TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, TRUE           /* 18..1F */
};

/*
** 'isvalid' checks that the line passed to it contains legal tokens.
** It ruturns TRUE if the line is okay otherwise it returns FALSE.
** The function only checks the executable tokens
*/
boolean isvalid(byte *bp) {
  int length, execoff;
  byte *base, *cp;
  byte token;
  if (get_lineno(bp)>MAXLINENO) return FALSE;   /* Line number is out of range */
  length = get_linelen(bp);
  if (length<MINSTATELEN || length>MAXSTATELEN) return FALSE;
  execoff = get_exec(bp);
  if (execoff<OFFSOURCE || execoff>length) return FALSE;
  base = cp = bp+execoff;
  while (cp-base<=length && *cp != NUL) {
    token = *cp;
    if (token<=LOW_HIGHEST) {   /* In lower block of tokens */
      if (!legalow[token]) return FALSE;        /* Bad token value found */
    }
    else if (token>=TOKEN_LOWEST) {
      switch (token) {
      case TYPE_PRINTFN:
        if (cp[1] == 0 || cp[1] > TOKEN_TAB) return FALSE;
        break;
     case TYPE_FUNCTION:
        if (cp[1] == 0 || (cp[1] > TOKEN_TIMEDOL && cp[1] < TOKEN_ABS) || cp[1] > TOKEN_VPOS) return FALSE;
        break;
      case TYPE_COMMAND:
        if (cp[1] == 0 || cp[1] > TOKEN_TWINO) return FALSE;
        break;
      default:
        if (token > TOKEN_HIGHEST) return FALSE;
      }
    }
    cp = skip_token(cp);
  }
  return (*cp == NUL);
}

/*
** 'resolve_linenums' goes through a line and resolves all line number
** references. It replaces line number references with pointers to the
** *start* of the wanted line
*/
void resolve_linenums(byte *bp) {
  byte *dest;
  int32 line;
  bp = FIND_EXEC(bp);
  while (*bp != NUL) {
    if (*bp == TOKEN_XLINENUM) {        /* Unresolved reference */
      line = get_linenum(bp);
      dest = find_line(line);
      if (line == get_lineno(dest)) {   /* Found line number */
        set_address(bp, dest);
        *bp = TOKEN_LINENUM;
      }
    }
    else if (*bp == TOKEN_LINENUM) {    /* Resolved reference - Replace with ref to start of line */
      dest = get_address(bp);
      set_address(bp, find_linestart(dest));
    }
    bp = skip_token(bp);
  }
}

/*
** 'reset_linenums' goes through a line and changes any line numbers
** referenced to their new values. As a bonus, it leaves all the
** line number pointers set to their correct values
** FIX code does not work after token changes
*/
void reset_linenums(byte *bp) {
  byte *dest, *sp;
  int32 line;
  sp = bp+OFFSOURCE;
  bp = FIND_EXEC(bp);
  while (*bp != NUL) {
    if (*bp == TOKEN_LINENUM || *bp == TOKEN_XLINENUM) {        /* Find corresponding ref in source */
      while (*sp != TOKEN_XLINENUM && *sp != NUL) sp++;
      if (*sp == NUL) error(ERR_BROKEN, __LINE__, "tokens");            /* Sanity check */
    }
    if (*bp == TOKEN_LINENUM) { /* Line number reference that has to be updated */
      dest = get_address(bp);
      line = get_lineno(dest);  /* Fetch the new line number */
/* Update the line number in the source part of the line */
      set_linenum(sp, line);
      sp+=1+LINESIZE;   /* Skip the line number in the source */
/* Change the address to point at the executable tokens */
      set_address(bp, FIND_EXEC(dest));
    }
    else if (*bp == TOKEN_XLINENUM) {   /* Line number missing - Issue a warning */
      byte *savedcurr = basicvars.current;
      basicvars.current = bp;   /* Ensure error message shows the line number of the bad line */
      error(WARN_LINEMISS, get_linenum(bp));
      basicvars.current = savedcurr;
      sp+=1+LINESIZE;   /* Skip the line number in the source */
    }
    bp = skip_token(bp);
  }
}

/*  =============== Acorn -> Brandy token conversion =============== */

/* Legal range of values for each Acorn token type */

#define ACORNONE_LOWEST         0x7Fu
#define ACORNONE_HIGHEST        0xFFu
#define ACORNTWO_LOWEST         0x8Eu
#define ACORNTWO_HIGHEST        0xA3u
#define ACORNCMD_LOWEST         0x8Eu
#define ACORNCMD_HIGHEST        0x9Fu
#define ACORNOTH_LOWEST         0x8Eu
#define ACORNOTH_HIGHEST        0x8Fu

#define ACORN_OTHER     0xC6u   /* Two byte tokens preceded by C6 */
#define ACORN_COMMAND   0xC7u   /* Two byte tokens preceded by C7 (commands) */
#define ACORN_TWOBYTE   0xC8u   /* Two byte tokens preceded by C8 */

#define ACORN_ENDLINE   0x0Du   /* Marks the end of a tokenised line */
#define ACORN_LINENUM   0x8Du   /* Token that preceeds a line number */

#define ACORN_TIME1     0x91u
#define ACORN_FN        0xA4u
#define ACORN_TO        0xB8u
#define ACORN_TIME2     0xD1u
#define ACORN_DATA      0xDCu
#define ACORN_PROC      0xF2u
#define ACORN_REM       0xF4u
#define ACORN_TAB       0x8Au
#define ACORN_INSTR     0xA7u
#define ACORN_POINT     0xB0u
#define ACORN_LEFT_DOL  0xC0u
#define ACORN_MID_DOL   0xC1u
#define ACORN_RIGHT_DOL 0xC2u
#define ACORN_STRING_DOL        0xC4u

#define ACORNLEN        1024    /* Size of buffer to hold plain text version of line */

#define ACORN_START  3          /* Bytes occupied by line number and length byte at start of line */
#define ACORN_LINESIZE 4        /* Size of line number token plus encoded line number */
/*
** This function converts a tokenised line number from the format that
** Acorn use to something printable. It returns the numeric version
** of the line number from the three-byte value at 'p'.
**
** Piers wrote the original version of this
*/
static int32 expand_linenum(byte *p) {
  int32 a, b, c, line;
  a = *p;
  b = *(p+1);
  c = *(p+2);
  line = ((a<<4) ^ c) & 0xff;
  return (line<<8) | ((((a<<2) & 0xc0) ^ b) & 0xff);
}

static char *onebyte_token [] = {
  "OTHERWISE", "AND", "DIV", "EOR", "MOD",      /* 0x7F..0x83 */
  "OR", "ERROR", "LINE", "OFF",                 /* 0x84..0x87 */
  "STEP", "SPC", "TAB(", "ELSE",                /* 0x88..0x8B */
  "THEN", NIL, "OPENIN", "PTR",
  "PAGE", "TIME", "LOMEM", "HIMEM",             /* 0x90..0x93 */
  "ABS", "ACS", "ADVAL", "ASC",
  "ASN", "ATN", "BGET", "COS",                  /* 0x98..0x9B */
  "COUNT", "DEG", "ERL", "ERR",
  "EVAL", "EXP", "EXT", "FALSE",                /* 0xA0..0xA3 */
  "FN", "GET", "INKEY", "INSTR(",
  "INT", "LEN", "LN", "LOG",                    /* 0xA8..0xAB */
  "NOT", "OPENUP", "OPENOUT", "PI",
  "POINT(", "POS", "RAD", "RND",                /* 0xB0..0xB3 */
  "SGN", "SIN", "SQR", "TAN",
  "TO", "TRUE", "USR", "VAL",                   /* 0xB8..0xBB */
  "VPOS", "CHR$", "GET$", "INKEY$",
  "LEFT$(", "MID$(", "RIGHT$(", "STR$",         /* 0xC0..0xC3 */
  "STRING$(", "EOF", NIL, NIL,
  NIL, "WHEN", "OF", "ENDCASE",                 /* 0xC8..0xCB */
  "ELSE", "ENDIF", "ENDWHILE", "PTR",
  "PAGE", "TIME", "LOMEM", "HIMEM",             /* 0xD0..0xD3 */
  "SOUND", "BPUT", "CALL", "CHAIN",
  "CLEAR", "CLOSE", "CLG", "CLS",               /* 0xD8..0xDB */
  "DATA", "DEF", "DIM", "DRAW",
  "END", "ENDPROC", "ENVELOPE", "FOR",          /* 0xE0..0xE3 */
  "GOSUB", "GOTO", "GCOL", "IF",
  "INPUT", "LET", "LOCAL", "MODE",              /* 0xE8..0xEB */
  "MOVE", "NEXT", "ON", "VDU",
  "PLOT", "PRINT", "PROC", "READ",              /* 0xF0..0xF3 */
  "REM", "REPEAT", "REPORT", "RESTORE",
  "RETURN", "RUN", "STOP", "COLOUR",            /* 0xF8..0xFB */
  "TRACE", "UNTIL", "WIDTH", "OSCLI"            /* 0xFC..0xFF */
};

/* Basic statement types - Two byte tokens preceded by 0xC8 */

static char *twobyte_token [] = {
  "CASE", "CIRCLE", "FILL", "ORIGIN",           /* 0x8E..0x91 */
  "POINT", "RECTANGLE", "SWAP", "WHILE",
  "WAIT", "MOUSE", "QUIT", "SYS",               /* 0x96..0x99 */
  "INSTALL", "LIBRARY", "TINT", "ELLIPSE",
  "BEATS", "TEMPO", "VOICES", "VOICE",          /* 0x9E..0xA1 */
  "STEREO", "OVERLAY"                           /* 0xA2..0xA3 */
};

/* Basic commands - Two byte tokens preceded by 0xC7 */

static char *command_token [] = {
  "APPEND", "AUTO", "CRUNCH", "DELETE",         /* 0x8E..0x91 */
  "EDIT", "HELP", "LIST", "LOAD",
  "LVAR", "NEW", "OLD", "RENUMBER",             /* 0x96..0x99 */
  "SAVE", "TEXTLOAD", "TEXTSAVE", "TWIN",
  "TWINO", "INSTALL"                            /* 0x9E..0x9F */
};

static char *other_token [] = {"SUM", "BEAT"};  /* 0x8E..0x8F */

/*
 * nospace - Tokens that should not or need not be followed by a
 * space if expanding crunched code
 */
static byte nospace [] = {
  ACORN_FN, ACORN_PROC, ACORN_TO, ACORN_TIME1, ACORN_TIME2,
  ACORN_TAB, ACORN_INSTR, ACORN_POINT, ACORN_LEFT_DOL, ACORN_MID_DOL,
  ACORN_RIGHT_DOL, ACORN_STRING_DOL, 0
};


/*
** 'reformat' takes a line formatted using Acorn Basic tokens and
** changes them to the tokens used by this interpreter. The reformatted
** line is saved in 'tokenbuf'. The function returns the length
** of the reformatted line. 'tp' points at the start of the Acorn
** Basic tokenised line.
** This code is a little crude in that it expands the Acorn Basic line
** to its textual form before calling this program's tokenisation
** function. Direct translation would be possible but there would be a
** large number of special cases so it is cleaner to expand the line
** and then tokenise it afresh
** Crunched programs can give problems in that it is possible to lose
** tokens when they are expanded. This code get around the problem
** by inserting blanks around keywords if there are none present.
*/
int32 reformat(byte *tp, byte *tokenbuf) {
  int count;
  char *cp, *p;
  byte token;
  char line[ACORNLEN];
  cp = &line[0];
  count = sprintf(cp, "%d", (*tp<<8) + *(tp+1));        /* Start with two byte line number */
  cp+=count;
  tp+=ACORN_START;      /* Skip line number and length byte */
  token = *tp;
  while (token != ACORN_ENDLINE) {
    if (token<ACORNONE_LOWEST) {        /* Normal characters */
      *cp = token;
      cp++;
      tp++;
      if (token == '\"') {      /* Got a character string */
        do {    /* Copy string as far as next '"' or end of line */
          *cp = token = *tp;
          cp++;
          tp++;
        } while (token != '\"' && *tp != ACORN_ENDLINE);
      }
    }
    else if (token == ACORN_LINENUM) {
      count = sprintf(cp, "%d", expand_linenum(tp+1));
      cp+=count;
      tp+=ACORN_LINESIZE;
    }
    else if (token == ACORN_REM || token == ACORN_DATA) {       /* REM or DATA - Copy rest of line */
      p = onebyte_token[token-ACORNONE_LOWEST];
      strcpy(cp, p);
      cp+=strlen(p);
      tp++;
      while (*tp != ACORN_ENDLINE) {
        *cp = *tp;
        cp++;
        tp++;
      }
    }
    else {      /* Tokens */
      switch (token) {
      case ACORN_TWOBYTE:
        token = *(tp+1);
        if (token<ACORNTWO_LOWEST || token>ACORNTWO_HIGHEST) error(ERR_BADPROG);
        p = twobyte_token[token-ACORNTWO_LOWEST];
        tp+=2;
        break;
      case ACORN_COMMAND:
        token = *(tp+1);
        if (token<ACORNCMD_LOWEST || token>ACORNCMD_HIGHEST) error(ERR_BADPROG);
        p = command_token[token-ACORNCMD_LOWEST];
        tp+=2;
        break;
      case ACORN_OTHER:
        token = *(tp+1);
        if (token<ACORNOTH_LOWEST || token>ACORNOTH_HIGHEST) error(ERR_BADPROG);
        p = other_token[token-ACORNOTH_LOWEST];
        tp+=2;
        break;
      default:
        p = onebyte_token[token-ACORNONE_LOWEST];
        tp++;
      }
/*
** Because the code expands tokenised Acorn Basic to text then
** retokenises it, blanks are added around keywords if there are
** none there already to prevent keywords being missed. Acorn
** Basic programs are often crunched, that is, spaces are
** removed after tokenising the program, leading to cases where
** keywords and text form one great long string of characters.
** As the keywords are tokenised they can be identified but they
** will be lost when the program is expanded to text form.
*/
      if (cp  !=  &line[0] && isalnum(*(cp-1))) {       /* If keyword is preceded by a letter or a digit, add a blank */
        *cp = ' ';
        cp++;
      }
      strcpy(cp, p);
      cp+=strlen(p);
/*
 * If keyword is followed by a letter or a digit, add a blank.
 * Some keywords have to be followed by a string, for example
 * PROC, so filter those out
 */
      if (isalnum(*tp)) {
        int n;
        for (n = 0; nospace[n] != 0 && nospace[n] != token; n++);
        if (nospace[n] == 0) {  /* Token is not in the 'no space' table */
          *cp = ' ';
          cp++;
        }
      }
    }
    token = *tp;
  }
  *cp = NUL;    /* Complete the line */
  tokenize(line, tokenbuf, HASLINE);
  return get_linelen(tokenbuf);
}
