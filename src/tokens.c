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
  {"ABS",       3, 2, TYPE_FUNCTION, BASTOKEN_ABS,        TYPE_FUNCTION, BASTOKEN_ABS,        FALSE, FALSE}, /* 0 */
  {"ACS",       3, 2, TYPE_FUNCTION, BASTOKEN_ACS,        TYPE_FUNCTION, BASTOKEN_ACS,        FALSE, FALSE},
  {"ADVAL",     5, 2, TYPE_FUNCTION, BASTOKEN_ADVAL,      TYPE_FUNCTION, BASTOKEN_ADVAL,      FALSE, FALSE},
  {"AND",       3, 1, TYPE_ONEBYTE,  BASTOKEN_AND,        TYPE_ONEBYTE,  BASTOKEN_AND,        FALSE, FALSE},
  {"ARGC",      4, 4, TYPE_FUNCTION, BASTOKEN_ARGC,       TYPE_FUNCTION, BASTOKEN_ARGC,       FALSE, FALSE},
  {"ARGV$",     5, 5, TYPE_FUNCTION, BASTOKEN_ARGVDOL,    TYPE_FUNCTION, BASTOKEN_ARGVDOL,    FALSE, FALSE},
  {"ASC",       3, 2, TYPE_FUNCTION, BASTOKEN_ASC,        TYPE_FUNCTION, BASTOKEN_ASC,        FALSE, FALSE},
  {"ASN",       3, 3, TYPE_FUNCTION, BASTOKEN_ASN,        TYPE_FUNCTION, BASTOKEN_ASN,        FALSE, FALSE},
  {"ATN",       3, 2, TYPE_FUNCTION, BASTOKEN_ATN,        TYPE_FUNCTION, BASTOKEN_ATN,        FALSE, FALSE},
  {"BEATS",     5, 2, TYPE_ONEBYTE,  BASTOKEN_BEATS,      TYPE_ONEBYTE,  BASTOKEN_BEATS,      FALSE, FALSE}, /* 9 */
  {"BEAT",      4, 4, TYPE_FUNCTION, BASTOKEN_BEAT,       TYPE_FUNCTION, BASTOKEN_BEAT,       FALSE, FALSE},
  {"BGET",      4, 1, TYPE_FUNCTION, BASTOKEN_BGET,       TYPE_FUNCTION, BASTOKEN_BGET,       TRUE,  FALSE},
  {"BPUT",      4, 2, TYPE_ONEBYTE,  BASTOKEN_BPUT,       TYPE_ONEBYTE,  BASTOKEN_BPUT,       TRUE,  FALSE},
  {"BY",        2, 2, TYPE_ONEBYTE,  BASTOKEN_BY,         TYPE_ONEBYTE,  BASTOKEN_BY,         FALSE, FALSE},
  {"CALL",      4, 2, TYPE_ONEBYTE,  BASTOKEN_CALL,       TYPE_ONEBYTE,  BASTOKEN_CALL,       FALSE, FALSE}, /* 13 */
  {"CASE",      4, 3, TYPE_ONEBYTE,  BASTOKEN_XCASE,      TYPE_ONEBYTE,  BASTOKEN_XCASE,      FALSE, FALSE},
  {"CHAIN",     5, 2, TYPE_ONEBYTE,  BASTOKEN_CHAIN,      TYPE_ONEBYTE,  BASTOKEN_CHAIN,      FALSE, FALSE},
  {"CHR$",      4, 4, TYPE_FUNCTION, BASTOKEN_CHR,        TYPE_FUNCTION, BASTOKEN_CHR,        FALSE, FALSE},
  {"CIRCLE",    6, 2, TYPE_ONEBYTE,  BASTOKEN_CIRCLE,     TYPE_ONEBYTE,  BASTOKEN_CIRCLE,     FALSE, FALSE},
  {"CLEAR",     5, 2, TYPE_ONEBYTE,  BASTOKEN_CLEAR,      TYPE_ONEBYTE,  BASTOKEN_CLEAR,      TRUE,  FALSE},
  {"CLOSE",     5, 3, TYPE_ONEBYTE,  BASTOKEN_CLOSE,      TYPE_ONEBYTE,  BASTOKEN_CLOSE,      TRUE,  FALSE},
  {"CLG",       3, 3, TYPE_ONEBYTE,  BASTOKEN_CLG,        TYPE_ONEBYTE,  BASTOKEN_CLG,        TRUE,  FALSE},
  {"CLS",       3, 3, TYPE_ONEBYTE,  BASTOKEN_CLS,        TYPE_ONEBYTE,  BASTOKEN_CLS,        TRUE,  FALSE},
  {"COLOR",     5, 1, TYPE_ONEBYTE,  BASTOKEN_COLOUR,     TYPE_ONEBYTE,  BASTOKEN_COLOUR,     FALSE, FALSE}, /* 22 */
  {"COLOUR",    6, 1, TYPE_ONEBYTE,  BASTOKEN_COLOUR,     TYPE_ONEBYTE,  BASTOKEN_COLOUR,     FALSE, FALSE},
  {"COS",       3, 3, TYPE_FUNCTION, BASTOKEN_COS,        TYPE_FUNCTION, BASTOKEN_COS,        FALSE, FALSE},
  {"COUNT",     5, 3, TYPE_FUNCTION, BASTOKEN_COUNT,      TYPE_FUNCTION, BASTOKEN_COUNT,      TRUE,  FALSE},
  {"DATA",      4, 1, TYPE_ONEBYTE,  BASTOKEN_DATA,       TYPE_ONEBYTE,  BASTOKEN_DATA,       FALSE, FALSE}, /* 26 */
  {"DEF",       3, 3, TYPE_ONEBYTE,  BASTOKEN_DEF,        TYPE_ONEBYTE,  BASTOKEN_DEF,        FALSE, FALSE},
  {"DEG",       3, 2, TYPE_FUNCTION, BASTOKEN_DEG,        TYPE_FUNCTION, BASTOKEN_DEG,        FALSE, FALSE},
  {"DIM",       3, 3, TYPE_ONEBYTE,  BASTOKEN_DIM,        TYPE_ONEBYTE,  BASTOKEN_DIM,        FALSE, FALSE},
  {"DIV",       3, 2, TYPE_ONEBYTE,  BASTOKEN_DIV,        TYPE_ONEBYTE,  BASTOKEN_DIV,        FALSE, FALSE},
  {"DRAW",      4, 2, TYPE_ONEBYTE,  BASTOKEN_DRAW,       TYPE_ONEBYTE,  BASTOKEN_DRAW,       FALSE, FALSE},
  {"ELLIPSE",   7, 3, TYPE_ONEBYTE,  BASTOKEN_ELLIPSE,    TYPE_ONEBYTE,  BASTOKEN_ELLIPSE,    FALSE, FALSE}, /* 33 */
  {"ELSE",      4, 2, TYPE_ONEBYTE,  BASTOKEN_XELSE,      TYPE_ONEBYTE,  BASTOKEN_XELSE,      FALSE, TRUE},
  {"ENDCASE",   7, 4, TYPE_ONEBYTE,  BASTOKEN_ENDCASE,    TYPE_ONEBYTE,  BASTOKEN_ENDCASE,    TRUE,  FALSE},
  {"ENDIF",     5, 4, TYPE_ONEBYTE,  BASTOKEN_ENDIF,      TYPE_ONEBYTE,  BASTOKEN_ENDIF,      TRUE,  FALSE},
  {"ENDPROC",   7, 1, TYPE_ONEBYTE,  BASTOKEN_ENDPROC,    TYPE_ONEBYTE,  BASTOKEN_ENDPROC,    TRUE,  FALSE},
  {"ENDWHILE",  8, 4, TYPE_ONEBYTE,  BASTOKEN_ENDWHILE,   TYPE_ONEBYTE,  BASTOKEN_ENDWHILE,   TRUE,  FALSE}, /* 38 */
  {"END",       3, 3, TYPE_ONEBYTE,  BASTOKEN_END,        TYPE_ONEBYTE,  BASTOKEN_END,        TRUE,  FALSE},
  {"ENVELOPE",  8, 3, TYPE_ONEBYTE,  BASTOKEN_ENVELOPE,   TYPE_ONEBYTE,  BASTOKEN_ENVELOPE,   FALSE, FALSE},
  {"EOF",       3, 3, TYPE_FUNCTION, BASTOKEN_EOF,        TYPE_FUNCTION, BASTOKEN_EOF,        TRUE,  FALSE},
  {"EOR",       3, 3, TYPE_ONEBYTE,  BASTOKEN_EOR,        TYPE_ONEBYTE,  BASTOKEN_EOR,        FALSE, FALSE},
  {"ERL",       3, 3, TYPE_FUNCTION, BASTOKEN_ERL,        TYPE_FUNCTION, BASTOKEN_ERL,        TRUE,  FALSE}, /* 43 */
  {"ERROR",     5, 3, TYPE_ONEBYTE,  BASTOKEN_ERROR,      TYPE_ONEBYTE,  BASTOKEN_ERROR,      FALSE, FALSE},
  {"ERR",       3, 3, TYPE_FUNCTION, BASTOKEN_ERR,        TYPE_FUNCTION, BASTOKEN_ERR,        TRUE,  FALSE},
  {"EVAL",      4, 2, TYPE_FUNCTION, BASTOKEN_EVAL,       TYPE_FUNCTION, BASTOKEN_EVAL,       FALSE, FALSE},
  {"EXIT",      4, 3, TYPE_ONEBYTE,  BASTOKEN_EXIT,       TYPE_ONEBYTE,  BASTOKEN_EXIT,       FALSE, FALSE}, /* 47 */
  {"EXP",       3, 3, TYPE_FUNCTION, BASTOKEN_EXP,        TYPE_FUNCTION, BASTOKEN_EXP,        FALSE, FALSE},
  {"EXT",       3, 3, TYPE_FUNCTION, BASTOKEN_EXT,        TYPE_FUNCTION, BASTOKEN_EXT,        TRUE,  FALSE},
  {"FALSE",     5, 2, TYPE_ONEBYTE,  BASTOKEN_FALSE,      TYPE_ONEBYTE,  BASTOKEN_FALSE,      TRUE,  FALSE}, /* 50 */
  {"FILEPATH$", 9, 4, TYPE_FUNCTION, BASTOKEN_FILEPATH,   TYPE_FUNCTION, BASTOKEN_FILEPATH,   FALSE, FALSE},
  {"FILL",      4, 2, TYPE_ONEBYTE,  BASTOKEN_FILL,       TYPE_ONEBYTE,  BASTOKEN_FILL,       FALSE, FALSE},
  {"FN",        2, 2, TYPE_ONEBYTE,  BASTOKEN_FN,         TYPE_ONEBYTE,  BASTOKEN_FN,         FALSE, FALSE},
  {"FOR",       3, 1, TYPE_ONEBYTE,  BASTOKEN_FOR,        TYPE_ONEBYTE,  BASTOKEN_FOR,        FALSE, FALSE},
  {"GCOL",      4, 2, TYPE_ONEBYTE,  BASTOKEN_GCOL,       TYPE_ONEBYTE,  BASTOKEN_GCOL,       FALSE, FALSE}, /* 55 */
  {"GET$",      4, 2, TYPE_FUNCTION, BASTOKEN_GETDOL,     TYPE_FUNCTION, BASTOKEN_GETDOL,     FALSE, FALSE},
  {"GET",       3, 3, TYPE_FUNCTION, BASTOKEN_GET,        TYPE_FUNCTION, BASTOKEN_GET,        FALSE, FALSE},
  {"GOSUB",     5, 3, TYPE_ONEBYTE,  BASTOKEN_GOSUB,      TYPE_ONEBYTE,  BASTOKEN_GOSUB,      FALSE, TRUE},
  {"GOTO",      4, 1, TYPE_ONEBYTE,  BASTOKEN_GOTO,       TYPE_ONEBYTE,  BASTOKEN_GOTO,       FALSE, TRUE},
  {"HIMEM",     5, 1, TYPE_FUNCTION, BASTOKEN_HIMEM,      TYPE_FUNCTION, BASTOKEN_HIMEM,      TRUE,  FALSE}, /* 60 */
  {"IF",        2, 2, TYPE_ONEBYTE,  BASTOKEN_XIF,        TYPE_ONEBYTE,  BASTOKEN_XIF,        FALSE, FALSE}, /* 61 */
  {"INKEY$",    6, 3, TYPE_FUNCTION, BASTOKEN_INKEYDOL,   TYPE_FUNCTION, BASTOKEN_INKEYDOL,   FALSE, FALSE},
  {"INKEY",     5, 5, TYPE_FUNCTION, BASTOKEN_INKEY,      TYPE_FUNCTION, BASTOKEN_INKEY,      FALSE, FALSE},
  {"INPUT",     5, 1, TYPE_ONEBYTE,  BASTOKEN_INPUT,      TYPE_ONEBYTE,  BASTOKEN_INPUT,      FALSE, FALSE},
  {"INSTR(",    6, 3, TYPE_FUNCTION, BASTOKEN_INSTR,      TYPE_FUNCTION, BASTOKEN_INSTR,      FALSE, FALSE},
  {"INT",       3, 3, TYPE_FUNCTION, BASTOKEN_INT,        TYPE_FUNCTION, BASTOKEN_INT,        FALSE, FALSE},
  {"LEFT$(",    6, 2, TYPE_FUNCTION, BASTOKEN_LEFT,       TYPE_FUNCTION, BASTOKEN_LEFT,       FALSE, FALSE}, /* 67 */
  {"LEN",       3, 3, TYPE_FUNCTION, BASTOKEN_LEN,        TYPE_FUNCTION, BASTOKEN_LEN,        FALSE, FALSE},
  {"LET",       3, 3, TYPE_ONEBYTE,  BASTOKEN_LET,        TYPE_ONEBYTE,  BASTOKEN_LET,        FALSE, FALSE},
  {"LIBRARY",   7, 3, TYPE_ONEBYTE,  BASTOKEN_LIBRARY,    TYPE_ONEBYTE,  BASTOKEN_LIBRARY,    FALSE, FALSE}, /* 70 */
  {"LINE",      4, 3, TYPE_ONEBYTE,  BASTOKEN_LINE,       TYPE_ONEBYTE,  BASTOKEN_LINE,       FALSE, FALSE},
  {"LN",        2, 2, TYPE_FUNCTION, BASTOKEN_LN,         TYPE_FUNCTION, BASTOKEN_LN,         FALSE, FALSE},
  {"LOCAL",     5, 3, TYPE_ONEBYTE,  BASTOKEN_LOCAL,      TYPE_ONEBYTE,  BASTOKEN_LOCAL,      FALSE, FALSE},
  {"LOG",       3, 3, TYPE_FUNCTION, BASTOKEN_LOG,        TYPE_FUNCTION, BASTOKEN_LOG,        FALSE, FALSE},
  {"LOMEM",     5, 3, TYPE_FUNCTION, BASTOKEN_LOMEM,      TYPE_FUNCTION, BASTOKEN_LOMEM,      TRUE,  FALSE},
  {"MID$(",     5, 1, TYPE_FUNCTION, BASTOKEN_MID,        TYPE_FUNCTION, BASTOKEN_MID,        FALSE, FALSE}, /* 76 */
  {"MODE",      4, 2, TYPE_ONEBYTE,  BASTOKEN_MODE,       TYPE_ONEBYTE,  BASTOKEN_MODE,       FALSE, FALSE},
  {"MOD",       3, 3, TYPE_ONEBYTE,  BASTOKEN_MOD,        TYPE_ONEBYTE,  BASTOKEN_MOD,        FALSE, FALSE},
  {"MOUSE",     5, 3, TYPE_ONEBYTE,  BASTOKEN_MOUSE,      TYPE_ONEBYTE,  BASTOKEN_MOUSE,      FALSE, FALSE},
  {"MOVE",      4, 3, TYPE_ONEBYTE,  BASTOKEN_MOVE,       TYPE_ONEBYTE,  BASTOKEN_MOVE,       FALSE, FALSE},
  {"NEXT",      4, 1, TYPE_ONEBYTE,  BASTOKEN_NEXT,       TYPE_ONEBYTE,  BASTOKEN_NEXT,       FALSE, FALSE}, /* 82 */
  {"NOT",       3, 3, TYPE_ONEBYTE,  BASTOKEN_NOT,        TYPE_ONEBYTE,  BASTOKEN_NOT,        FALSE, FALSE},
  {"OFF",       3, 3, TYPE_ONEBYTE,  BASTOKEN_OFF,        TYPE_ONEBYTE,  BASTOKEN_OFF,        FALSE, FALSE}, /* 84 */
  {"OF",        2, 2, TYPE_ONEBYTE,  BASTOKEN_OF,         TYPE_ONEBYTE,  BASTOKEN_OF,         FALSE, FALSE},
  {"ON",        2, 2, TYPE_ONEBYTE,  BASTOKEN_ON,         TYPE_ONEBYTE,  BASTOKEN_ON,         FALSE, FALSE}, /* 86 */
  {"OPENIN",    6, 2, TYPE_FUNCTION, BASTOKEN_OPENIN,     TYPE_FUNCTION, BASTOKEN_OPENIN,     FALSE, FALSE},
  {"OPENOUT",   7, 5, TYPE_FUNCTION, BASTOKEN_OPENOUT,    TYPE_FUNCTION, BASTOKEN_OPENOUT,    FALSE, FALSE},
  {"OPENUP",    6, 5, TYPE_FUNCTION, BASTOKEN_OPENUP,     TYPE_FUNCTION, BASTOKEN_OPENUP,     FALSE, FALSE},
  {"ORIGIN",    6, 2, TYPE_ONEBYTE,  BASTOKEN_ORIGIN,     TYPE_ONEBYTE,  BASTOKEN_ORIGIN,     FALSE, FALSE},
  {"OR",        2, 2, TYPE_ONEBYTE,  BASTOKEN_OR,         TYPE_ONEBYTE,  BASTOKEN_OR,         FALSE, FALSE}, /* 91 */
  {"OSCLI",     5, 2, TYPE_ONEBYTE,  BASTOKEN_OSCLI,      TYPE_ONEBYTE,  BASTOKEN_OSCLI,      FALSE, FALSE},
  {"OTHERWISE", 9, 2, TYPE_ONEBYTE,  BASTOKEN_XOTHERWISE, TYPE_ONEBYTE,  BASTOKEN_XOTHERWISE, FALSE, FALSE},
  {"OVERLAY",   7, 2, TYPE_ONEBYTE,  BASTOKEN_OVERLAY,    TYPE_ONEBYTE,  BASTOKEN_OVERLAY,    FALSE, FALSE},
  {"PAGE",      4, 2, TYPE_FUNCTION, BASTOKEN_PAGE,       TYPE_FUNCTION, BASTOKEN_PAGE,       TRUE,  FALSE}, /* 95 */
  {"PI",        2, 2, TYPE_FUNCTION, BASTOKEN_PI,         TYPE_FUNCTION, BASTOKEN_PI,         TRUE,  FALSE},
  {"PLOT",      4, 2, TYPE_ONEBYTE,  BASTOKEN_PLOT,       TYPE_ONEBYTE,  BASTOKEN_PLOT,       FALSE, FALSE},
  {"POINT(",    6, 2, TYPE_FUNCTION, BASTOKEN_POINTFN,    TYPE_FUNCTION, BASTOKEN_POINTFN,    FALSE, FALSE},
  {"POINT",     5, 5, TYPE_ONEBYTE,  BASTOKEN_POINT,      TYPE_ONEBYTE,  BASTOKEN_POINT,      FALSE, FALSE},
  {"POS",       3, 3, TYPE_FUNCTION, BASTOKEN_POS,        TYPE_FUNCTION, BASTOKEN_POS,        TRUE,  FALSE},
  {"PRINT",     5, 1, TYPE_ONEBYTE,  BASTOKEN_PRINT,      TYPE_ONEBYTE,  BASTOKEN_PRINT,      FALSE, FALSE}, /* 103 */
  {"PROC",      4, 4, TYPE_ONEBYTE,  BASTOKEN_PROC,       TYPE_ONEBYTE,  BASTOKEN_PROC,       FALSE, FALSE},
  {"PTR",       3, 3, TYPE_FUNCTION, BASTOKEN_PTR,        TYPE_FUNCTION, BASTOKEN_PTR,        TRUE,  FALSE},
  {"QUIT",      4, 1, TYPE_ONEBYTE,  BASTOKEN_QUIT,       TYPE_ONEBYTE,  BASTOKEN_QUIT,       TRUE,  FALSE}, /* 106 */
  {"RAD",       3, 2, TYPE_FUNCTION, BASTOKEN_RAD,        TYPE_FUNCTION, BASTOKEN_RAD,        FALSE, FALSE}, /* 107 */
  {"READ",      4, 3, TYPE_ONEBYTE,  BASTOKEN_READ,       TYPE_ONEBYTE,  BASTOKEN_READ,       FALSE, FALSE},
  {"RECTANGLE", 9, 3, TYPE_ONEBYTE,  BASTOKEN_RECTANGLE,  TYPE_ONEBYTE,  BASTOKEN_RECTANGLE,  FALSE, FALSE},
  {"REM",       3, 3, TYPE_ONEBYTE,  BASTOKEN_REM,        TYPE_ONEBYTE,  BASTOKEN_REM,        FALSE, FALSE},
  {"REPEAT",    6, 3, TYPE_ONEBYTE,  BASTOKEN_REPEAT,     TYPE_ONEBYTE,  BASTOKEN_REPEAT,     FALSE, FALSE},
  {"REPORT$",   7, 7, TYPE_FUNCTION, BASTOKEN_REPORTDOL,  TYPE_FUNCTION, BASTOKEN_REPORTDOL,  FALSE, FALSE},
  {"REPORT",    6, 4, TYPE_ONEBYTE,  BASTOKEN_REPORT,     TYPE_ONEBYTE,  BASTOKEN_REPORT,     TRUE,  FALSE}, /* 113 */
  {"RESTORE",   7, 3, TYPE_ONEBYTE,  BASTOKEN_RESTORE,    TYPE_ONEBYTE,  BASTOKEN_RESTORE,    FALSE, TRUE},
  {"RETURN",    6, 1, TYPE_ONEBYTE,  BASTOKEN_RETURN,     TYPE_ONEBYTE,  BASTOKEN_RETURN,     TRUE,  FALSE},
  {"RIGHT$(",   7, 2, TYPE_FUNCTION, BASTOKEN_RIGHT,      TYPE_FUNCTION, BASTOKEN_RIGHT,      FALSE, FALSE},
  {"RND(",      4, 4, TYPE_FUNCTION, BASTOKEN_RNDPAR,     TYPE_FUNCTION, BASTOKEN_RNDPAR,     FALSE, FALSE},
  {"RND",       3, 2, TYPE_FUNCTION, BASTOKEN_RND,        TYPE_FUNCTION, BASTOKEN_RND,        TRUE,  FALSE},
  {"RUN",       3, 2, TYPE_ONEBYTE,  BASTOKEN_RUN,        TYPE_ONEBYTE,  BASTOKEN_RUN,        TRUE,  FALSE},
  {"SGN",       3, 2, TYPE_FUNCTION, BASTOKEN_SGN,        TYPE_FUNCTION, BASTOKEN_SGN,        FALSE, FALSE}, /* 120 */
  {"SIN",       3, 2, TYPE_FUNCTION, BASTOKEN_SIN,        TYPE_FUNCTION, BASTOKEN_SIN,        FALSE, FALSE},
  {"SOUND",     5, 2, TYPE_ONEBYTE,  BASTOKEN_SOUND,      TYPE_ONEBYTE,  BASTOKEN_SOUND,      FALSE, FALSE},
  {"SPC",       3, 3, TYPE_PRINTFN,  BASTOKEN_SPC,        TYPE_PRINTFN,  BASTOKEN_SPC,        FALSE, FALSE},
  {"SQR",       3, 3, TYPE_FUNCTION, BASTOKEN_SQR,        TYPE_FUNCTION, BASTOKEN_SQR,        FALSE, FALSE}, /* 124 */
  {"STEP",      4, 1, TYPE_ONEBYTE,  BASTOKEN_STEP,       TYPE_ONEBYTE,  BASTOKEN_STEP,       FALSE, FALSE},
  {"STEREO",    6, 4, TYPE_ONEBYTE,  BASTOKEN_STEREO,     TYPE_ONEBYTE,  BASTOKEN_STEREO,     FALSE, FALSE},
  {"STOP",      4, 3, TYPE_ONEBYTE,  BASTOKEN_STOP,       TYPE_ONEBYTE,  BASTOKEN_STOP,       TRUE,  FALSE},
  {"STR$",      4, 3, TYPE_FUNCTION, BASTOKEN_STR,        TYPE_FUNCTION, BASTOKEN_STR,        FALSE, FALSE},
  {"STRING$(",  8, 4, TYPE_FUNCTION, BASTOKEN_STRING,     TYPE_FUNCTION, BASTOKEN_STRING,     FALSE, FALSE}, /* 129 */
  {"SUM",       3, 2, TYPE_FUNCTION, BASTOKEN_SUM,        TYPE_FUNCTION, BASTOKEN_SUM,        FALSE, FALSE},
  {"SWAP",      4, 2, TYPE_ONEBYTE,  BASTOKEN_SWAP,       TYPE_ONEBYTE,  BASTOKEN_SWAP,       FALSE, FALSE},
  {"SYS(",      4, 4, TYPE_FUNCTION, BASTOKEN_SYSFN,      TYPE_FUNCTION, BASTOKEN_SYSFN,      FALSE, FALSE},
  {"SYS",       3, 3, TYPE_ONEBYTE,  BASTOKEN_SYS,        TYPE_ONEBYTE,  BASTOKEN_SYS,        FALSE, FALSE},
  {"TAB(",      4, 4, TYPE_PRINTFN,  BASTOKEN_TAB,        TYPE_PRINTFN,  BASTOKEN_TAB,        FALSE, FALSE}, /* 134 */
  {"TAN",       3, 1, TYPE_FUNCTION, BASTOKEN_TAN,        TYPE_FUNCTION, BASTOKEN_TAN,        FALSE, FALSE},
  {"TEMPO",     5, 2, TYPE_ONEBYTE,  BASTOKEN_TEMPO,      TYPE_FUNCTION, BASTOKEN_TEMPOFN,    FALSE, FALSE},
  {"THEN",      4, 2, TYPE_ONEBYTE,  BASTOKEN_THEN,       TYPE_ONEBYTE,  BASTOKEN_THEN,       FALSE, TRUE},
  {"TIME",      4, 2, TYPE_FUNCTION, BASTOKEN_TIME,       TYPE_FUNCTION, BASTOKEN_TIME,       TRUE,  FALSE},
  {"TINT",      4, 3, TYPE_ONEBYTE,  BASTOKEN_TINT,       TYPE_ONEBYTE,  BASTOKEN_TINT,       FALSE, FALSE}, /* 139 */
  {"TO",        2, 3, TYPE_ONEBYTE,  BASTOKEN_TO,         TYPE_ONEBYTE,  BASTOKEN_TO,         FALSE, FALSE},
  {"TRACE",     5, 2, TYPE_ONEBYTE,  BASTOKEN_TRACE,      TYPE_ONEBYTE,  BASTOKEN_TRACE,      FALSE, FALSE},
  {"TRUE",      4, 3, TYPE_ONEBYTE,  BASTOKEN_TRUE,       TYPE_ONEBYTE,  BASTOKEN_TRUE,       TRUE,  FALSE},
  {"UNTIL",     5, 1, TYPE_ONEBYTE,  BASTOKEN_UNTIL,      TYPE_ONEBYTE,  BASTOKEN_UNTIL,      FALSE, FALSE}, /* 143 */
  {"USR",       3, 2, TYPE_FUNCTION, BASTOKEN_USR,        TYPE_FUNCTION, BASTOKEN_USR,        FALSE, FALSE},
  {"VAL",       3, 2, TYPE_FUNCTION, BASTOKEN_VAL,        TYPE_FUNCTION, BASTOKEN_VAL,        FALSE, FALSE}, /* 145 */
  {"VDU",       3, 1, TYPE_ONEBYTE,  BASTOKEN_VDU,        TYPE_ONEBYTE,  BASTOKEN_VDU,        FALSE, FALSE},
  {"VERIFY(",   7, 2, TYPE_FUNCTION, BASTOKEN_VERIFY,     TYPE_FUNCTION, BASTOKEN_VERIFY,     FALSE, FALSE},
  {"VOICES",    6, 2, TYPE_ONEBYTE,  BASTOKEN_VOICES,     TYPE_ONEBYTE,  BASTOKEN_VOICES,     FALSE, FALSE},
  {"VOICE",     5, 5, TYPE_ONEBYTE,  BASTOKEN_VOICE,      TYPE_ONEBYTE,  BASTOKEN_VOICE,      FALSE, FALSE},
  {"VPOS",      4, 2, TYPE_FUNCTION, BASTOKEN_VPOS,       TYPE_FUNCTION, BASTOKEN_VPOS,       TRUE,  FALSE},
  {"WAIT",      4, 2, TYPE_ONEBYTE,  BASTOKEN_WAIT,       TYPE_ONEBYTE,  BASTOKEN_WAIT,       TRUE,  FALSE}, /* 151 */
  {"WHEN",      4, 3, TYPE_ONEBYTE,  BASTOKEN_XWHEN,      TYPE_ONEBYTE,  BASTOKEN_XWHEN,      FALSE, FALSE},
  {"WHILE",     5, 1, TYPE_ONEBYTE,  BASTOKEN_XWHILE,     TYPE_ONEBYTE,  BASTOKEN_XWHILE,     FALSE, FALSE},
  {"WIDTH",     5, 2, TYPE_ONEBYTE,  BASTOKEN_WIDTH,      TYPE_ONEBYTE,  BASTOKEN_WIDTH,      FALSE, FALSE},
  {"XLATE$(",   7, 2, TYPE_FUNCTION, BASTOKEN_XLATEDOL,   TYPE_FUNCTION, BASTOKEN_XLATEDOL,   FALSE, FALSE}, /* 155 */
/*
** The following keywords are Basic commands. These can be entered in mixed case.
** Note that 'RUN' is also in here so that it can be entered in lower case too.
** Also note that in the case of commands where there is 'O' version, the
** 'O' version must come first, for example, EDITO must preceed EDIT
*/
  {"APPEND",    6, 2, TYPE_COMMAND, BASTOKEN_APPEND,    TYPE_COMMAND,  BASTOKEN_APPEND,    FALSE, FALSE}, /* 156 */
  {"AUTO",      4, 2, TYPE_COMMAND, BASTOKEN_AUTO,      TYPE_COMMAND,  BASTOKEN_AUTO,      FALSE, FALSE},
  {"CRUNCH",    6, 2, TYPE_COMMAND, BASTOKEN_CRUNCH,    TYPE_COMMAND,  BASTOKEN_CRUNCH,    FALSE, FALSE}, /* 158 */
  {"DELETE",    6, 3, TYPE_COMMAND, BASTOKEN_DELETE,    TYPE_COMMAND,  BASTOKEN_DELETE,    FALSE, FALSE}, /* 159 */
  {"EDITO",     5, 5, TYPE_COMMAND, BASTOKEN_EDITO,     TYPE_COMMAND,  BASTOKEN_EDITO,     FALSE, FALSE}, /* 160 */
  {"EDIT",      4, 2, TYPE_COMMAND, BASTOKEN_EDIT,      TYPE_COMMAND,  BASTOKEN_EDIT,      FALSE, FALSE},
  {"HELP",      4, 2, TYPE_COMMAND, BASTOKEN_HELP,      TYPE_COMMAND,  BASTOKEN_HELP,      TRUE,  FALSE}, /* 162 */
  {"INSTALL",   7, 5, TYPE_COMMAND, BASTOKEN_INSTALL,   TYPE_COMMAND,  BASTOKEN_INSTALL,   FALSE, FALSE}, /* 163 */
  {"LISTB",     5, 5, TYPE_COMMAND, BASTOKEN_LISTB,     TYPE_COMMAND,  BASTOKEN_LISTB,     FALSE, FALSE}, /* 164 */
  {"LISTIF",    6, 6, TYPE_COMMAND, BASTOKEN_LISTIF,    TYPE_COMMAND,  BASTOKEN_LISTIF,    FALSE, FALSE},
  {"LISTL",     5, 5, TYPE_COMMAND, BASTOKEN_LISTL,     TYPE_COMMAND,  BASTOKEN_LISTL,     FALSE, FALSE},
  {"LISTO",     5, 5, TYPE_COMMAND, BASTOKEN_LISTO,     TYPE_FUNCTION, BASTOKEN_LISTOFN,   FALSE, FALSE},
  {"LISTW",     5, 5, TYPE_COMMAND, BASTOKEN_LISTW,     TYPE_COMMAND,  BASTOKEN_LISTW,     FALSE, FALSE},
  {"LIST",      4, 1, TYPE_COMMAND, BASTOKEN_LIST,      TYPE_COMMAND,  BASTOKEN_LIST,      FALSE, FALSE},
  {"LOAD",      4, 2, TYPE_COMMAND, BASTOKEN_LOAD,      TYPE_COMMAND,  BASTOKEN_LOAD,      FALSE, FALSE},
  {"LVAR",      4, 3, TYPE_COMMAND, BASTOKEN_LVAR,      TYPE_COMMAND,  BASTOKEN_LVAR,      TRUE,  FALSE},
  {"NEW",       3, 3, TYPE_COMMAND, BASTOKEN_NEW,       TYPE_COMMAND,  BASTOKEN_NEW,       TRUE,  FALSE}, /* 172 */
  {"OLD",       3, 1, TYPE_COMMAND, BASTOKEN_OLD,       TYPE_COMMAND,  BASTOKEN_OLD,       TRUE,  FALSE}, /* 173 */
  {"QUIT",      4, 1, TYPE_ONEBYTE, BASTOKEN_QUIT,      TYPE_ONEBYTE,  BASTOKEN_QUIT,      TRUE,  FALSE}, /* 174 */
  {"RENUMBER",  8, 3, TYPE_COMMAND, BASTOKEN_RENUMBER,  TYPE_COMMAND,  BASTOKEN_RENUMBER,  FALSE, FALSE}, /* 175 */
  {"RUN",       3, 2, TYPE_ONEBYTE, BASTOKEN_RUN,       TYPE_ONEBYTE,  BASTOKEN_RUN,       TRUE,  FALSE},
  {"SAVEO",     5, 5, TYPE_COMMAND, BASTOKEN_SAVEO,     TYPE_COMMAND,  BASTOKEN_SAVEO,     FALSE, FALSE}, /* 177 */
  {"SAVE",      4, 2, TYPE_COMMAND, BASTOKEN_SAVE,      TYPE_COMMAND,  BASTOKEN_SAVE,      FALSE, FALSE},
  {"TEXTLOAD",  8, 3, TYPE_COMMAND, BASTOKEN_TEXTLOAD,  TYPE_COMMAND,  BASTOKEN_TEXTLOAD,  FALSE, FALSE}, /* 179 */
  {"TEXTSAVEO", 9, 9, TYPE_COMMAND, BASTOKEN_TEXTSAVEO, TYPE_COMMAND,  BASTOKEN_TEXTSAVEO, FALSE, FALSE},
  {"TEXTSAVE",  8, 5, TYPE_COMMAND, BASTOKEN_TEXTSAVE,  TYPE_COMMAND,  BASTOKEN_TEXTSAVE,  FALSE, FALSE},
  {"TWINO",     5, 2, TYPE_COMMAND, BASTOKEN_TWINO,     TYPE_COMMAND,  BASTOKEN_TWINO,     TRUE,  FALSE},
  {"TWIN",      4, 4, TYPE_COMMAND, BASTOKEN_TWIN,      TYPE_COMMAND,  BASTOKEN_TWIN,      TRUE,  FALSE},
  {"ZZ",        1, 1, 0, 0, 0, 0, FALSE, FALSE}                                                         /* 184 */
};

#define TOKTABSIZE (sizeof(tokens)/sizeof(token))

static int start_letter [] = {
  0, 9, 14, 27, 33, 50, 55, 60, 61, NOKEYWORD, NOKEYWORD, 67, 76, 81, 83, 94,
  103, 104, 117, 131, 140, 142, 148, 152, NOKEYWORD, NOKEYWORD
};

static int command_start [] = { /* Starting positions for commands in 'tokens' */
  153, NOKEYWORD, 155, 156, 157, NOKEYWORD, NOKEYWORD, 159, 160, NOKEYWORD,
  NOKEYWORD, 161, NOKEYWORD, 169, 170, NOKEYWORD, 171, 172, 174, 176,
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
  numbered,             /* TRUE if line starts with a line number */
  immediate;            /* TRUE if tokenising line in immediate mode */

/*
** 'isempty' returns true if the line passed to it has nothing on it
*/
boolean isempty(byte line[]) {
  DEBUGFUNCMSGIN;
  DEBUGFUNCMSGOUT;
  return line[OFFSOURCE] == asc_NUL;
}

void save_lineno(byte *where, int32 number) {
  DEBUGFUNCMSGIN;
  *where = CAST(number, byte);
  *(where+1) = CAST(number>>BYTESHIFT, byte);
  DEBUGFUNCMSGOUT;
}

/*
** 'store_lineno' stores the line number at the start of the
** tokenised line. It is held in the form <low byte> <high byte>.
*/
static void store_lineno(int32 number) {
  DEBUGFUNCMSGIN;
  if (next+LINESIZE>=MAXSTATELEN) {
    error(ERR_STATELEN);
    return;
  }
  tokenbase[next] = CAST(number, byte);
  tokenbase[next+1] = CAST(number>>BYTESHIFT, byte);
  next+=2;
  DEBUGFUNCMSGOUT;
}

/*
** 'store_linelen' stashes the length of the line at the start
** of the tokenised line
*/
static void store_linelen(int32 length) {
  DEBUGFUNCMSGIN;
  tokenbase[OFFLENGTH] = CAST(length, byte);
  tokenbase[OFFLENGTH+1] = CAST(length>>BYTESHIFT, byte);
  DEBUGFUNCMSGOUT;
}

/*
** 'store_exec' stores the offset of the first executable token
** in the line at the start of the line
*/
static void store_exec(int32 offset) {
  DEBUGFUNCMSGIN;
  tokenbase[OFFEXEC] = CAST(offset, byte);
  tokenbase[OFFEXEC+1] = CAST(offset>>BYTESHIFT, byte);
  DEBUGFUNCMSGOUT;
}

/*
** 'store' is called to add a character to the tokenised line buffer
*/
static void store(byte token) {
  DEBUGFUNCMSGIN;
  if (next+1>=MAXSTATELEN) {
    error(ERR_STATELEN);
    return;
  }
  tokenbase[next] = token;
  next++;
  DEBUGFUNCMSGOUT;
}

/*
** 'store_size' is called to store a two-byte length in the tokenised
** line buffer
*/
static void store_size(int32 size) {
  DEBUGFUNCMSGIN;
  if (next+SIZESIZE>=MAXSTATELEN) {
    error(ERR_STATELEN);
    return;
  }
  tokenbase[next] = CAST(size, byte);
  tokenbase[next+1] = CAST(size>>BYTESHIFT, byte);
  next+=2;
  DEBUGFUNCMSGOUT;
}

/*
** 'store_longoffset' is called to add a long offset (offset from the
** start of the Basic workspace) to the tokenised line buffer.
*/
static void store_longoffset(int32 value) {
  int n;

  DEBUGFUNCMSGIN;
  if (next+LOFFSIZE>=MAXSTATELEN) {
    error(ERR_STATELEN);
    return;
  }
  for (n=1; n<=LOFFSIZE; n++) {
    tokenbase[next] = CAST(value, byte);
    value = value>>BYTESHIFT;
    next++;
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'store_shortoffset' stores a two byte offset in the tokenised
** line buffer These are used for references to lines from the
** current position in the Basic program.
*/
static void store_shortoffset(int32 value) {
  DEBUGFUNCMSGIN;
  if (next+OFFSIZE>=MAXSTATELEN) {
    error(ERR_STATELEN);
    return;
  }
  tokenbase[next] = CAST(value, byte);
  tokenbase[next+1] = CAST(value>>BYTESHIFT, byte);
  next+=2;
  DEBUGFUNCMSGOUT;
}

/*
** 'store_intconst' is called to stow a four byte integer in the tokenised line
** buffer
*/
static void store_intconst(int32 value) {
  int n;

  DEBUGFUNCMSGIN;
  if (next+INTSIZE>=MAXSTATELEN) {
    error(ERR_STATELEN);
    return;
  }
  for (n=1; n<=INTSIZE; n++) {
    tokenbase[next] = CAST(value, byte);
    value = value>>8;
    next++;
  }
  DEBUGFUNCMSGOUT;
}

static void store_int64const(int64 value) {
  int n;

  DEBUGFUNCMSGIN;
  if (next+INT64SIZE>=MAXSTATELEN) {
    error(ERR_STATELEN);
    return;
  }
  for (n=1; n<=INT64SIZE; n++) {
    tokenbase[next] = CAST(value, byte);
    value = value>>8;
    next++;
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'store_fpvalue' is a grubby bit of code used to store an eight-byte
** floating point value in the tokenised line buffer
*/
static void store_fpvalue(float64 fpvalue) {
  byte temp[sizeof(float64)];
  int n;

  DEBUGFUNCMSGIN;
  if (next+FLOATSIZE>=MAXSTATELEN) {
    error(ERR_STATELEN);
    return;
  }
  memcpy(temp, &fpvalue, sizeof(float64));
  for (n=0; n<sizeof(float64); n++) {
    tokenbase[next] = temp[n];
    next++;
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'convert_lineno' is called when a line number is found to convert
** it to binary. If the line number is too large the error is flagged
** but tokenisation continues
*/
static int32 convert_lineno(void) {
  int32 line = 0;

  DEBUGFUNCMSGIN;
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
  DEBUGFUNCMSGOUT;
  return line;
}

/*
** 'copy_line' is called to copy the remainder of a line to the
** tokenised line buffer
*/
static char *copy_line(char *lp) {
  DEBUGFUNCMSGIN;
  while (*lp != asc_NUL) {
    store(*lp);
    lp++;
  }
  DEBUGFUNCMSGOUT;
  return lp;
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

  DEBUGFUNCMSGIN;
  cp = lp;
  for (n=0; n<MAXKWLEN && (isalpha(*cp) || *cp == '$' || *cp == '('); n++) {
    keyword[n] = *cp;
    cp++;
  }
  abbreviated = n < MAXKWLEN && *cp == '.';
  if (!abbreviated && n == 1) return NOKEYWORD; /* Text is only one character long - Cannot be a keyword */
  keyword[n] = asc_NUL;
  kwlength = n;
  first = keyword[0];
  if (matrixflags.lowercasekeywords) {
    for (n=0; keyword[n] != asc_NUL; n++) keyword[n] = toupper(keyword[n]);
    first = keyword[0];
  }
  if (islower(first)) {
    nomatch = TRUE;
  } else {
    n = start_letter[first-'A'];
    if (n == NOKEYWORD) return NOKEYWORD;       /* No keyword starts with this letter */
    do {
      count = tokens[n].length; /* Decide on number of characters to compare */
      if (abbreviated && kwlength < count) {
        count = kwlength;
        if (kwlength < tokens[n].minlength) count = tokens[n].minlength;
      }
      if (matrixflags.lowercasekeywords) {
        if (strncasecmp(keyword, tokens[n].name, count) == 0) break;
      } else {
        if (strncmp(keyword, tokens[n].name, count) == 0) break;
      }
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
    if (matrixflags.lowercasekeywords) {
      for (n=0; keyword[n] != asc_NUL; n++) keyword[n] = toupper(keyword[n]);
      first = keyword[0];
    } else {
      if (islower(first)) return NOKEYWORD;
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
  if (nomatch || (!abbreviated && tokens[n].alone && ISIDCHAR(keyword[count]))) { /* Not a keyword */
    DEBUGFUNCMSGOUT;
    return NOKEYWORD;
  }
  else {        /* Found a keyword */
    lp+=count;
    if (abbreviated && *lp == '.') lp++;        /* Skip '.' after abbreviated keyword */
    DEBUGFUNCMSGOUT;
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

  DEBUGFUNCMSGIN;
  if (firstitem) {      /* Keyword is the first item in the statement */
    toktype = tokens[token].lhtype;
    tokvalue = tokens[token].lhvalue;
    if (linestart && toktype == TYPE_ONEBYTE && tokvalue == BASTOKEN_XELSE) tokvalue = BASTOKEN_XLHELSE;
  }
  else {
    toktype = tokens[token].type;
    tokvalue = tokens[token].value;
  }
  firstitem = FALSE;
  if (toktype != TYPE_ONEBYTE) store(toktype);
  store(tokvalue);
  if (tokens[token].name[tokens[token].length-1] == '(') brackets++;    /* Allow for '(' in things like 'TAB(' */
  if (toktype == TYPE_ONEBYTE) {        /* Check for special cases */
    switch (tokvalue) {
    case BASTOKEN_REM: case BASTOKEN_DATA: /* Copy rest of line */
      lp = copy_line(lp);
      break;
    case BASTOKEN_THEN: case BASTOKEN_REPEAT: case BASTOKEN_XELSE: case BASTOKEN_XOTHERWISE:
      firstitem = TRUE; /* Next token must use the 'first in statement' token */
      break;
    case BASTOKEN_FN: case BASTOKEN_PROC:     /* Copy proc/function name */
      while(ISIDCHAR(*lp)) {
        store(*lp);
        lp++;
      }
      break;
    }
  }
  else if (toktype == TYPE_COMMAND) {
    if (tokvalue == BASTOKEN_LISTIF || tokvalue == BASTOKEN_LVAR) {   /* Copy rest of line untranslated */
      lp = copy_line(lp);
    }
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'copy_token' deals with token values directly entered from the
** keyboard. It ensures that the token value is legal
*/
static void copy_token(void) {
  int n;
  byte toktype, tokvalue;

  DEBUGFUNCMSGIN;
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
  DEBUGFUNCMSGOUT;
}

/*
** 'copy_variable' deals with variables. It copies the name to the
** token buffer. The name is preceded by a 'XVAR' token so that the name
** can be found easily when trying to replace pointers to variables'
** symbol table entries with references to their names (see function
** clear_varaddrs() below)
*/
static void copy_variable(void) {
  DEBUGFUNCMSGIN;
  if (*lp>='@' && *lp<='Z' && lp[1] == '%' && lp[2] != '%' && lp[2] != '(' && lp[2] != '[') {   /* Static integer variable */
    store(*lp);
    lp++;
  }
  else {        /* Dynamic variable */
    store(BASTOKEN_XVAR);
    while (ISIDCHAR(*lp)) {
      store(*lp);
      lp++;
    }
  }
  if (*lp == '%') {     /* Integer variable */
    store(*lp);
    lp++;
    if (*lp == '%') {   /* %% for 64-bit int */
      store(*lp);
      lp++;
    }
  }
  if (*lp == '&') {     /* Unsigned 8-bit int variable */
    store(*lp);
    lp++;
  }
  if (*lp == '#') {     /* 64-bit floating point variable */
    store(*lp);
    lp++;
  }
  if (*lp == '$') {     /* String variable */
    store(*lp);
    lp++;
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'copy_lineno' copies a line number into the source part of the
** tokenised line. The number is converted to binary to make it
** easier to renumber lines
*/
static void copy_lineno(void) {
  DEBUGFUNCMSGIN;
  store(BASTOKEN_XLINENUM);
  store_lineno(convert_lineno());
  DEBUGFUNCMSGOUT;
}

/*
** 'copy_number' copies hex, binary, integer and floating point
** constants to the token buffer
*/
static void copy_number(void) {
  char ch;
  int digits;

  DEBUGFUNCMSGIN;
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
  DEBUGFUNCMSGOUT;
}

/*
** 'copy_string' copies a character string to the tokenised
** line buffer
*/
static void copy_string(void) {
  DEBUGFUNCMSGIN;
  store('"');           /* Store the quote at the start of the string */
  lp++;
  while (TRUE) {
    if (*lp == asc_NUL) break;      /* Error - Reached end of line without finding a '"' */
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
  DEBUGFUNCMSGOUT;
}

/*
** 'copy_other' deals with any other characters and special tokens
*/
static void copy_other(void) {
  byte tclass, token;

  DEBUGFUNCMSGIN;
  tclass = TYPE_ONEBYTE;
  token = *lp;
  switch (token) {      /* Deal with special tokens */
  case '(':
    brackets++;
    break;
  case ')':
    brackets--;
    if (brackets < 0) { /* More ')' than '(' */
      lasterror = ERR_LPMISS;
      error(WARN_PARNEST);
    }
    break;
  case 172:         /* This is a hi-bit char and causes a compiler warning if used directly */
    tclass = TYPE_FUNCTION;
    token = BASTOKEN_NOT;
    break;
  case '+':
    if (*(lp+1) == '=') {       /* Found '+=' */
      token = BASTOKEN_PLUSAB;
      lp++;
    }
    break;
  case '-':
    if (*(lp+1) == '=') {       /* Found '-=' */
      token = BASTOKEN_MINUSAB;
      lp++;
    }
    break;
  case '^':
    if (*(lp+1) == '=') {       /* Found '^=' */
      token = BASTOKEN_POWRAB;
      lp++;
    }
    break;
  case '>':
    switch (*(lp+1)) {
    case '=':           /* Found '>=' */
      token = BASTOKEN_GE;
      lp++;
      break;
    case '>':
      if (*(lp+2) == '>') {     /* Found '>>>' */
        token = BASTOKEN_LSR;
        lp+=2;
      }
      else {            /* Found '>>' */
        token = BASTOKEN_ASR;
        lp++;
      }
    }
    break;
  case '<':
    switch (*(lp+1)) {
    case '=':           /* Found '<=' */
      token = BASTOKEN_LE;
      lp++;
      break;
    case '>':           /* Found '<>' */
      token = BASTOKEN_NE;
      lp++;
      break;
    case '<':           /* Found '<<' */
      token = BASTOKEN_LSL;
      lp++;
    }
    break;
#if defined(TARGET_WIN32) | defined(TARGET_MINGW)
  case '|':     /* Window's code for vertical bar is 221, not 124 */
    token = asc_VBAR;
    break;
#endif
  default:
    if (token<' ' && token != asc_TAB) token = ' ';
  }
  if (tclass != TYPE_ONEBYTE) store(tclass);
  store(token);
  if (token == ':')     /* Update the 'first item in statement' flag */
    firstitem = TRUE;
  else if (token != ' ' && token != asc_TAB) {
    firstitem = FALSE;
  }
  lp++;
  DEBUGFUNCMSGOUT;
}

/*
** 'tokenise_sourceline' copies the line starting at 'start' to
** the tokenised line buffer, replacing keywords with tokens
*/
static void tokenise_source(char *start, boolean haslineno) {
  int token;
  char ch;
  boolean linenoposs;

  DEBUGFUNCMSGIN;
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
      while (*lp == ' ' || *lp == asc_TAB) {        /* Copy leading white space characters */
        store(*lp);
        lp++;
      }
    }
  }
  next = OFFSOURCE;
  ch = *lp;
  firstitem = TRUE;     /* Use 'first item in line' tokens where necessary */
  linestart = TRUE;     /* Say that this is the very start of the tokenised line */
  while (ch != asc_NUL) {
    if (ISIDSTART(ch)) {                /* Keyword or identifier */
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
    else if (ch == '@' && *(lp+1) == '%' && *(lp+2) != '%') {     /* Built-in variable @% */
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
      store(BASTOKEN_STAR);
      lp = copy_line(lp+1);
    }
    else if (CAST(ch, byte)>=BASTOKEN_LOWEST)      /* Token value directly entered */
      copy_token();
    else {      /* Anything else */
      copy_other();
      linenoposs = linenoposs && (ch == ' ' || ch == asc_TAB || ch == ',');
    }
    linestart = FALSE;
    ch = *lp;
  }
  store(asc_NUL);                           /* Add a NUL to make it easier to find the end of the line */
  store_exec(next);
  store(asc_NUL);
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
  DEBUGFUNCMSGOUT;
}

/*
** 'do_keyword' carries out any special processing such as adding
** pointers to the executable version of the tokenised line when a
** keyword token is found
*/
static void do_keyword(void) {
  byte token, previous;

  DEBUGFUNCMSGIN;
  previous = PREVIOUS_TOKEN;
  token = tokenbase[source];
  source++;
  if (token>=TYPE_COMMAND) {            /* Two byte token */
    store(token);
    store(tokenbase[source]);
    if (token == TYPE_COMMAND && (tokenbase[source] == BASTOKEN_LISTIF || tokenbase[source] == BASTOKEN_LVAR)) {
     do         /* Find text after LISTIF or LVAR */
       source++;
     while (tokenbase[source] == ' ' || tokenbase[source] == asc_TAB);
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
    if (previous != BASTOKEN_EXIT) {
      firstitem = token == BASTOKEN_REPEAT || token == BASTOKEN_THEN || token == BASTOKEN_XELSE || token == BASTOKEN_XOTHERWISE;
      switch (token) {            /* Check for special cases */
      case BASTOKEN_XIF:
        store_shortoffset(0);             /* Store offset of code after 'THEN' */
        store_shortoffset(0);             /* Store offset of code after 'ELSE' */
        break;
      case BASTOKEN_XELSE: case BASTOKEN_XLHELSE: case BASTOKEN_XWHEN: case BASTOKEN_XOTHERWISE:
      case BASTOKEN_XWHILE:
        store_shortoffset(0);             /* Store offset of code at end of statement */
        break;
      case BASTOKEN_XCASE:
        store_longoffset(0);              /* Store pointer to case value table */
        break;
      case BASTOKEN_FN: case BASTOKEN_PROC:     /* Replace token with 'X' token and add offset to name */
        next--;           /* Hack, hack... */
        store(BASTOKEN_XFNPROCALL);
        store_longoffset(next-source);    /* Store offset to PROC/FN name */
        while (ISIDCHAR(tokenbase[source])) source++;      /* Skip PROC/FN name, was isident()*/
        break;
      case BASTOKEN_REM:     /* Skip rest of tokenised line */
         source = -1;     /* Flag value to say we have finished this line */
         break;
      case BASTOKEN_DATA:    /* Insert the offset back to the data itself after the DATA token */
        store_shortoffset(next-1-source); /* -1 so that offset is from the DATA token itself */
        source = -1;      /* Flag value to say we have finished this line */
        break;
      case BASTOKEN_TRACE:   /* Just copy the token that follows TRACE so that it is unmangled */
        while (tokenbase[source] == ' ' || tokenbase[source] == asc_TAB) source++;
        if (tokenbase[source]>BASTOKEN_LOWEST) {     /* TRACE is followed by a token */
          store(tokenbase[source]);
          source++;
        }
      }
    }
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'do_statvar' deals with the static integer variables
*/
static void do_statvar(void) {
  byte first = tokenbase[source];

  DEBUGFUNCMSGIN;
  if (tokenbase[source+2] == '?' || tokenbase[source+2] == '!') /* Variable is followed by an indirection operator */
    store(BASTOKEN_STATINDVAR);
  else {        /* Nice, plain, simple reference to variable */
    store(BASTOKEN_STATICVAR);
  }
  store(first-'@');             /* Store identifer name mapped to range 0..26 */
  source+=2;
  firstitem = FALSE;
  DEBUGFUNCMSGOUT;
}

/*
** 'do_dynamvar' handles dynamic variables. A 'XVAR' token is inserted into
** the executable portion of the line for the variable followed by the offset
** of the name of the variable from the the XVAR token in the source part
** of the line
*/
static void do_dynamvar(void) {
  DEBUGFUNCMSGIN;
  source++;
  store(BASTOKEN_XVAR);
  store_longoffset(next-1-source);      /* Store offset back to name from here */
  while (ISIDCHAR(tokenbase[source])) source++;  /* Skip name, was isident() */
  if (tokenbase[source] == '&' || tokenbase[source] == '%' || tokenbase[source] == '#' || tokenbase[source] == '$') source++;   /* Skip integer or string variable marker */
  if (tokenbase[source] == '%') source++;   /* Skip 64-bit integer second variable marker */
  if (tokenbase[source] == '(' || tokenbase[source] == '[') source++;   /* Skip '(' (is part of name if an array) */
  firstitem = FALSE;
  DEBUGFUNCMSGOUT;
}

/*
** 'do_linenumber' converts a line number to binary and stores it in
** executable token buffer. The line number is preceded by an 'XLINENUM'
** token. When the reference is found the line number will be replaced
** by a pointer to the destination
*/
static void do_linenumber(void) {
  int32 line;

  DEBUGFUNCMSGIN;
  line = tokenbase[source+1]+(tokenbase[source+2]<<BYTESHIFT);
  store(BASTOKEN_XLINENUM);
  store_longoffset(line);
  source+=1+LINESIZE;
  firstitem = FALSE;
  DEBUGFUNCMSGOUT;
}

/*
** 'do_number' converts all forms of number (integer, hex, binary and
** floating point) to binary and stores them in the executable token
** buffer
*/
#define INTCONV (MAXINTVAL/10)

static void do_number(void) {
  int32 value;
  int64 value64;
  static float64 fpvalue;
  boolean isintvalue;
  boolean isbinhex=FALSE;
  char *p;

  DEBUGFUNCMSGIN;
  value = 0;
  value64 = 0;
  isintvalue = TRUE;    /* Number is an integer */
  switch(tokenbase[source]) {
  case '&':     /* Hex value */
    source++;
    isbinhex=TRUE;
    while (isxdigit(tokenbase[source])) {
      value = (value<<4)+todigit(tokenbase[source]);
      value64 = (value64<<4)+todigit(tokenbase[source]);
      source++;
    }
    break;
  case '%':     /* Binary value */
    source++;
    isbinhex=TRUE;
    while (tokenbase[source] == '0' || tokenbase[source] == '1') {
      value = (value<<1)+(tokenbase[source]-'0');
      value64 = (value64<<1)+(tokenbase[source]-'0');
      source++;
    }
    break;
  default:      /* Decimal or floating point */
    p = tonumber(CAST(&tokenbase[source], char *), &isintvalue, &value, &value64, &fpvalue);
    if (p == NIL) {
      lasterror = ERR_BADEXPR;
      DEBUGFUNCMSGOUT;
      error(value);     /* Error found in number - flag it */
      return;
    }
    source = p-CAST(&tokenbase[0], char *);     /* Figure out new value of 'source' */
  }
  firstitem = FALSE;
/* Store the constant in the executable token portion of the line */
  if (isintvalue) {     /* Decide on type of integer constant */
    if ((!matrixflags.hex64 && isbinhex) || value == value64) {
      if (value64 == 0)
      store(BASTOKEN_INTZERO);               /* Integer 0 */
      else if (value64 == 1)
        store(BASTOKEN_INTONE);              /* Integer 1 */
      else if (value64>1 && value64<=SMALLCONST) {
        store(BASTOKEN_SMALLINT);            /* Integer 1..256 */
        store(value-1);                   /* Move number 1..256 to range 0..255 when saved */
      } else {
      store(BASTOKEN_INTCON);              /* 32-bit int */
      store_intconst(value);
      }
    } else {
      if (value64 == 0)
      store(BASTOKEN_INTZERO);               /* Integer 0 */
      else if (value64 == 1)
        store(BASTOKEN_INTONE);              /* Integer 1 */
      else if (value64>1 && value64<=SMALLCONST) {
        store(BASTOKEN_SMALLINT);            /* Integer 1..256 */
        store(value-1);                   /* Move number 1..256 to range 0..255 when saved */
      } else {
        store(BASTOKEN_INT64CON);              /* 64-bit int */
        store_int64const(value64);
      }
    }
  }
  else {        /* Decide on type of floating point constant */
    if (fpvalue == 0.0)
      store(BASTOKEN_FLOATZERO);
    else if (fpvalue == 1.0)
      store(BASTOKEN_FLOATONE);
    else {
      store(BASTOKEN_FLOATCON);
      store_fpvalue(fpvalue);
    }
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'do_string' is called to copy a character string to the tokenised
** version of a line. The string is held in the form
** <TOKEN> <offset> <length>
** <TOKEN> can be either BASTOKEN_STRINGCON or BASTOKEN_QSTRINGCON. The
** difference is that 'QSTRINGCON' strings contain one or more '"'
** characters which means that it is not possible to just push a
** pointer to the string on to the Basic stack. A copy of the string
** has to be made with '""' sequences replaced by '"'. This is
** not going to be very common so two string tokens are used. The
** first one, BASTOKEN_STRINGCON, will be used in the majority of cases.
** When this token is encountered all the code does is push a pointer
** to the string on to the stack. BASTOKEN_QSTRINGCON is used as given
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

  DEBUGFUNCMSGIN;
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
    store(BASTOKEN_QSTRINGCON);
  else {
    store(BASTOKEN_STRINGCON);
  }
  store_shortoffset(next-1-start);      /* Store offset to string in source part of line */
  store_size(length);           /* Store string length */
  firstitem = FALSE;
  DEBUGFUNCMSGOUT;
}

/*
** 'do_star' processes a '*' star (operating system) command
*/
static void do_star(void) {
  DEBUGFUNCMSGIN;
  do    /* Skip the '*' token at the start of the command */
    source++;
  while (tokenbase[source] == ' ' || tokenbase[source] == asc_TAB || tokenbase[source] == '*');
  if (tokenbase[source] != asc_NUL) {       /* There is something after the '*' */
    store(BASTOKEN_STAR);
    store_shortoffset(next-1-source);   /* -1 so that offset is from the '*' token itself */
    source = -1;                /* Flag value to say we have finished this line */
  }
  DEBUGFUNCMSGOUT;
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

  DEBUGFUNCMSGIN;
  source = OFFSOURCE;           /* Offset of first byte of tokenised source */
  token = tokenbase[source];
  firstitem = TRUE;
  while (token != asc_NUL) {        /* Scan through the tokenised source */
#ifdef DEBUG
    if (basicvars.debug_flags.debug) fprintf(stderr, "    translate: token=0x%X\n", token);
#endif
    if (token == BASTOKEN_STAR)    /* '*' command */
      do_star();
    else if (token>=BASTOKEN_LOWEST)       /* Have found a keyword token */
      do_keyword();
    else if (token>='@' && token<='Z' && tokenbase[source+1] == '%' && tokenbase[source+2] != '%')
      do_statvar();
    else if (token == BASTOKEN_XVAR)
      do_dynamvar();
    else if (token == ')') {    /* Handle ')' */
      store(token);
      firstitem = FALSE;
/*
** Now check if the ')' is followed by a '.' (matrix multiplication
** operator) If it is, copy the '.' so that it is not seen as the
** start of a floating point number
*/
      source++;
      while (tokenbase[source] == ' ' || tokenbase[source] == asc_TAB) source++;
      if (tokenbase[source] == '.') { /* ')' is followed by a '.' - Assume '.' is an operator */
        store('.');
        source++;
      }
    }
    else if (token == BASTOKEN_XLINENUM)   /* Line number */
      do_linenumber();
    else if ((token>='0' && token<='9') || token == '.' || token == '&' || token == '%')        /* Any form of number */
      do_number();
    else if (token == '\"')     /* String */
      do_string();
    else if (token == ' ' || token == asc_TAB)      /* Discard white space characters */
      source++;
    else if ((token == '?' || token == '!') && (tokenbase[source-1] == ' ')) {
      store(' ');
      store(token);
      source++;
    }
    else if (token == ':') {    /* Handle statement separators */
      store(':');
      do
        source++;
      while (tokenbase[source] == ':' || tokenbase[source] == ' ' || tokenbase[source] == asc_TAB);
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
  store(asc_NUL);
  store_linelen(next);
  DEBUGFUNCMSGOUT;
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
  DEBUGFUNCMSGIN;
  if (GET_LINENO(tokenbase) == NOLINENO)        /* Line has no line number - Put an 'END' here */
    store(BASTOKEN_END);
  else {        /* Line has number - Put a 'bad line' token here */
    store(BADLINE_MARK);
    store(lasterror);           /* Store number of error after token */
  }
  store(asc_NUL);
  store_linelen(next);
  DEBUGFUNCMSGOUT;
}

/*
** "tokenize" is called to tokenize the line of Basic passed to it
** that starts at 'start'. The tokenized version of the line is put
** in the array given by 'tokenbuf'.
** Tokenisation is carried out in two passes:
** 1)  The source is tokenised by replacing keywords with tokens
** 2)  The executable version of the line is created
*/
void tokenize(char *start, byte tokenbuf[], boolean haslineno, boolean immediatemode) {
  DEBUGFUNCMSGIN;
  immediate = immediatemode;
  tokenbase = tokenbuf;
  tokenise_source(start, haslineno);
  if (lasterror>0)
    mark_badline();
  else 
    translate();
  DEBUGFUNCMSGOUT;
}

/*
** The following table gives the number of characters to skip for each
** token in addition to the one character for the token.
** '-1' indicates that the token is invalid, that is, the program has
** probably been corrupted
*/
static int skiptable [] = {
  0,                LOFFSIZE,         1,         LOFFSIZE,  /* 00..03 */
  LOFFSIZE,         LOFFSIZE,         LOFFSIZE,  LOFFSIZE,  /* 04..07 */
  LOFFSIZE,         LOFFSIZE,         LOFFSIZE,  LOFFSIZE,  /* 08..0B */
  LOFFSIZE,         LOFFSIZE,         1,         LOFFSIZE,  /* 0C..0F */
  LOFFSIZE,         0,                0,         SMALLSIZE, /* 10..13 */
  INTSIZE,          0,                0,         FLOATSIZE, /* 14..17 */
  OFFSIZE+SIZESIZE, OFFSIZE+SIZESIZE, INT64SIZE, -1,        /* 18..1B */
  -1,               -1,               LOFFSIZE,  LOFFSIZE,  /* 1C..1F */
   0,  0, -1,  0,  0,  0,  0,  0,                           /* 20..27 */
   0,  0,  0,  0,  0,  0,  0,  0,                           /* 28..2F */
  -1, -1, -1, -1, -1, -1, -1, -1,                           /* 30..37 */
  -1, -1,  0,  0,  0,  0,  0,  0,                           /* 38..3F */
   0, -1, -1, -1, -1, -1, -1, -1,                           /* 40..47 */
  -1, -1, -1, -1, -1, -1, -1, -1,                           /* 48..4F */
  -1, -1, -1, -1, -1, -1, -1, -1,                           /* 50..57 */
  -1, -1, -1,  0,  0,  0,  0,  0,                           /* 58..5F */
   0, -1, -1, -1, -1, -1, -1, -1,                           /* 60..67 */
  -1, -1, -1, -1, -1, -1, -1, -1,                           /* 68..6F */
  -1, -1, -1, -1, -1, -1, -1, -1,                           /* 70..77 */
  -1, -1, -1,  0,  0,  0,  0, -1,                           /* 78..7F */
  0,   0,  0,  0,  0,  0,  0,  0,                           /* 80..87 */
  0,   0,  0,  0,  0,  0, -1, -1,                           /* 88..8F */
  0,          0,          0,          LOFFSIZE,             /* 90..93 */ /* XCASE at 93 */
  LOFFSIZE,   0,          0,          0,                    /* 94..97 */
  0,          0,          0,          0,                    /* 98..9B */ /* DATA */
  OFFSIZE,    0,          0,          0,                    /* 9C..9F */ /* ELSE */
  0,          0,          OFFSIZE,    OFFSIZE,              /* A0..A2 */ /* ELSE */
  OFFSIZE,    OFFSIZE,    0,          0,                    /* A4..A7 */
  0,          0,          0,          0,                    /* A8..AB */
  0,          0,          0,          0,                    /* AC..AF */
  0,          0,          0,          0,                    /* B0..B3 */ /* IF */
  0,          2*OFFSIZE,  2*OFFSIZE,  2*OFFSIZE,            /* B4..B7 */ /* IF */
  0,          0,          0,          0,                    /* B8..BB */
  0,          0,          0,          0,                    /* BC..BF */
  1,          0,          0,          0,                    /* C0..C3 */
  0,          0,          0,          0,                    /* C4..C7 */ /* OTHERWISE */
  OFFSIZE,    OFFSIZE,    0,          0,                    /* C8..CB */
  0,          0,          0,          0,                    /* CC..CF */
  0,          0,          0,          0,                    /* D0..D3 */
  0,          0,          0,          0,                    /* D4..D7 */
  0,          OFFSIZE,    0,          0,                    /* D8..DB */ /* *command */
  0,          0,          0,          0,                    /* DC..DF */
  0,          0,          0,          0,                    /* E0..E3 */
  0,          0,          0,          0,                    /* E4..E7 */
  0,          0,          OFFSIZE,    OFFSIZE,              /* E8..EB */ /* WHEN, WHILE */
  OFFSIZE,    OFFSIZE,    0,          -1,                   /* EC..EF */ /* WHEN, WHILE */
  -1, -1, -1, -1, -1, -1, -1, -1,                           /* F0..F7 */
  -1, -1, -1, -1, 1, 1, 1, 1                                /* F8..FF */
};

/*
** 'skip_token' returns a pointer to the token following the
** one pointed at by 'p'.
** Note that this code does not handle 'listif' and 'lvar'
** properly
*/
byte *skip_token(byte *p) {
  int size;

  DEBUGFUNCMSGIN;
  if (*p == asc_NUL) return p;      /* At end of line */
  size = skiptable[*p];
  if (size>=0) {
    DEBUGFUNCMSGOUT;
    return p+1+size;
  }
  DEBUGFUNCMSGOUT;
  error(ERR_BADPROG);   /* Not a legal token value - Program has been corrupted */
  return NULL;
}

/*
** 'skip_name' returns a pointer to the byte after the variable name that
** starts at 'p'
*/
byte *skip_name(byte *p) {
  DEBUGFUNCMSGIN;
  do
    p++;
  while (ISIDCHAR(*p));
  if (*p == '&' || *p == '%' || *p == '#' || *p == '$') p++;      /* If integer or string, skip the suffix character */
  if (*p == '%') p++;      /* If 64-bit integer skip the second suffix character */
  if (*p == '(' || *p == '[') p++;      /* If an array, the first '(' or '[' is part of the name so skip it */
  DEBUGFUNCMSGOUT;
  return p;
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
static byte *get_address(byte *p) {
  DEBUGFUNCMSGIN;
  DEBUGFUNCMSGOUT;
  return basicvars.workspace+(*(p+1) | *(p+2)<<8 | *(p+3)<<16 | *(p+4)<<24);
}

/*
** 'set_linenum' stores a line number following the line number token
** at 'lp'
*/
static void set_linenum(byte *lp, int32 line) {
  DEBUGFUNCMSGIN;
  *(lp+1) = CAST(line, byte);
  *(lp+2) = CAST(line>>BYTESHIFT, byte);
  DEBUGFUNCMSGOUT;
}

/*
** 'get_fpvalue' extracts an eight-byte floating point constant from the
** tokenised form of a Basic statement. 'fp' points at the 'floating point
** constant' token
*/
float64 get_fpvalue(byte *fp) {
  static float64 fpvalue;

  DEBUGFUNCMSGIN;
  memcpy(&fpvalue, fp+1, sizeof(float64));
  DEBUGFUNCMSGOUT;
  return fpvalue;
}

static char *onebytelist [] = { /* Token -> keyword name */
  "AND",       ">>",        "DIV",       "EOR",             /* 80..83 */
  ">=",        "<=",        "<<",        ">>>",             /* 84..87 */
  "-=",        "MOD",       "<>",        "OR",              /* 88..8B */
  "+=",        "^=",         NIL,         NIL,              /* 8C..8F */
  "BEATS",     "BPUT",      "CALL",      "CASE",            /* 90..93 */
  "CASE",      "CHAIN",     "CIRCLE",    "CLG",             /* 94..97 */
  "CLEAR",     "CLOSE",     "CLS",       "COLOUR",          /* 98..9B */
  "DATA",      "DEF",       "DIM",       "DRAW",            /* 9C..9F */
  "BY",        "ELLIPSE",   "ELSE",      "ELSE",            /* A0..93 */
  "ELSE",      "ELSE",      "END",       "ENDCASE",         /* A4..A7 */
  "ENDIF",     "ENDPROC",   "ENDWHILE",  "ENVELOPE",        /* A8..AB */
  "ERROR",     "FALSE",     "FILL",      "FILL BY",         /* AC..AF */
  "FN",        "FOR",       "GCOL",      "GOSUB",           /* B0..B3 */
  "GOTO",      "IF",        "IF",        "IF",              /* B4..B7 */
  "INPUT",     "LET",       "LIBRARY",   "LINE",            /* B8..BB */
  "LOCAL",     "MODE",      "MOUSE",     "MOVE",            /* BC..BF */
  "EXIT",      "NEXT",      "NOT",       "OF",              /* C0..C3 */
  "OFF",       "ON",        "ORIGIN",    "OSCLI",           /* C4..C7 */
  "OTHERWISE", "OTHERWISE", "OVERLAY",   "PLOT",            /* C8..CB */
  "POINT",     "PRINT",     "PROC",      "QUIT",            /* CC..CF */
  "READ",      "RECTANGLE", "REM",       "REPEAT",          /* D0..D3 */
  "REPORT",    "RESTORE",   "RETURN",    "RUN",             /* D4..D7 */
  "SOUND",     "*",         "STEP",      "STEREO",          /* D8..DB */
  "STOP",      "SWAP",      "SYS",       "TEMPO",           /* DC..DF */
  "THEN",      "TINT",      "TO",        "TRACE",           /* E0..E3 */
  "TRUE",      "UNTIL",     "VDU",       "VOICE",           /* E4..E7 */
  "VOICES",    "WAIT",      "WHEN",      "WHEN",            /* E8..EB */
  "WHILE",     "WHILE",     "WIDTH",      NIL,              /* EC..EF */
   NIL,  NIL,   NIL,  NIL,   NIL,  NIL,   NIL,  NIL,        /* F0..F7 */
   NIL,  NIL,   NIL,  NIL,   NIL,  NIL,   NIL,  NIL         /* F8..FF */
};

static char *commandlist [] = { /* Token -> command name */
   NIL,        "APPEND", "AUTO",     "CRUNCH",              /* 00..03 */
  "DELETE",    "EDIT",   "EDITO",    "HELP",                /* 04..07 */
  "INSTALL",   "LIST",   "LISTB",    "LISTIF",              /* 08..0B */
  "LISTL",     "LISTO",  "LISTW",    "LOAD",                /* 0C..0F */
  "LVAR",      "NEW",    "OLD",      "RENUMBER",            /* 10..13 */
  "SAVE",      "SAVEO",  "TEXTLOAD", "TEXTSAVE",            /* 14..17 */
  "TEXTSAVEO", "TWIN",   "TWINO"                            /* 18..1A */
};

static char *functionlist [] = {        /* Functions -> function name */
   NIL,      "HIMEM",   "EXT",     "FILEPATH$",             /* 00..03 */
  "LEFT$(",  "LOMEM",   "MID$(",   "PAGE",                  /* 04..07 */
  "PTR",     "RIGHT$(", "TIME",    "TIME$",                 /* 08..0B */
   NIL,       NIL,       NIL,       NIL,                    /* 0C..0F */
  "ABS",     "ACS",     "ADVAL",   "ARGC",                  /* 10..13 */
  "ARGV$",   "ASC",     "ASN",     "ATN",                   /* 14..17 */
  "BEAT",    "BGET",    "CHR$",    "COS",                   /* 18..1B */
  "COUNT",   "DEG",     "EOF",     "ERL",                   /* 1C..1F */
  "ERR",     "EVAL",    "EXP",     "GET",                   /* 20..23 */
  "GET$",    "INKEY",   "INKEY$",  "INSTR(",                /* 24..27 */
  "INT",     "LEN",     "LISTO",   "LN",                    /* 28..2B */
  "LOG",     "OPENIN",  "OPENOUT", "OPENUP",                /* 2C..2F */
  "PI",      "POINT(",  "POS",     "RAD",                   /* 30..33 */
  "REPORT$", "RETCODE", "RND",     "SGN",                   /* 34..37 */
  "SIN",     "SQR",     "STR$",    "STRING$(",              /* 38..3B */
  "SUM",     "TAN",     "TEMPO",   "USR",                   /* 3C..3F */
  "VAL",     "VERIFY(", "VPOS",    "SYS(",                  /* 40..43 */
  "RND(",    "XLATE$("                                      /* 44..45 */
};

static char *printlist [] = {NIL, "SPC", "TAB("};

/*
** 'expand_token' is called to expand the token passed to it to its textual
** form. The function returns the length of the expanded form
*/
static int expand_token(char *cp, char *namelist[], byte token, int bufsz) {
  int count;
  char *name;

  DEBUGFUNCMSGIN;
  name = namelist[token];
  if (name == NIL) {
    error(ERR_BROKEN, __LINE__, "tokens");       /* Sanity check for bad token value */
    return 0;
  }
  STRLCPY(cp, name, bufsz);
  count = strlen(name);
  if (basicvars.list_flags.lower) {     /* Lower case version of name required */
    int n;
    for (n=0; n<count; n++) {
      *cp = tolower(*cp);
      cp++;
    }
  }
  DEBUGFUNCMSGOUT;
  return count;
}

static byte *skip_source(byte *p) {
  byte token;

  DEBUGFUNCMSGIN;
  token = *p;
  if (token == asc_NUL) {
    DEBUGFUNCMSGOUT;
    return p;
  }
  if (token == BASTOKEN_XLINENUM) {
    DEBUGFUNCMSGOUT;
    return p+1+LINESIZE;
  }
  if (token>=TYPE_COMMAND) {
    DEBUGFUNCMSGOUT;
    return p+2;  /* Two byte token */
  }
  DEBUGFUNCMSGOUT;
  return p+1;
}

/*
** 'expand' takes the tokenised line passed to it and expands it to its
** original form in the buffer provided. 'line' points at the very start of
** the tokenised line
*/
void expand(byte *line, char *text) {
  byte token;
  byte *elp;
  int count;
  int bufsz = MAXSTRING;

  DEBUGFUNCMSGIN;
  if (!basicvars.list_flags.noline) {   /* Include line number */
    snprintf(text, MAXSTRING, "%5d", GET_LINENO(line));
    text+=5;
    bufsz-=5;
    if (basicvars.list_flags.space) {   /* Need a blank before the expanded line */
      *text = ' ';
      text++;
      bufsz--;
    }
  }
  elp = line+OFFSOURCE;  /* Point at start of code after source token */
  if (basicvars.list_flags.indent) {    /* Indent line */
    int n, thisindent, nextindent;
    elp = skip(elp);      /* Start by figuring out if indentation changes */
    thisindent = nextindent = indentation;
    switch (*elp) {      /* First look at special cases where first token on line affects indentation */
    case BASTOKEN_DEF:
      thisindent = nextindent = 0;
      break;
    case BASTOKEN_LHELSE: case BASTOKEN_XLHELSE: case BASTOKEN_WHEN: case BASTOKEN_XWHEN:
    case BASTOKEN_OTHERWISE: case BASTOKEN_XOTHERWISE:
      thisindent-=INDENTSIZE;
      if (thisindent<0) thisindent = 0;
      nextindent = thisindent+INDENTSIZE;
      break;
/*    case BASTOKEN_REM: case BASTOKEN_DATA:
      thisindent = 0;
      break;*/
    case BASTOKEN_ENDIF: case BASTOKEN_ENDCASE:
      thisindent-=INDENTSIZE;
      nextindent-=INDENTSIZE;
      break;
    }
    while (*elp != asc_NUL) {
      switch(*elp) {
      case BASTOKEN_WHILE: case BASTOKEN_XWHILE: case BASTOKEN_REPEAT: case BASTOKEN_FOR:
      case BASTOKEN_CASE: case BASTOKEN_XCASE:
        nextindent+=INDENTSIZE;
        break;
      case BASTOKEN_THEN:
        if (*(elp+1) == asc_NUL) nextindent+=INDENTSIZE;     /* Block IF */
        break;
      case BASTOKEN_ENDWHILE: case BASTOKEN_UNTIL:
        if (nextindent == thisindent) thisindent-=INDENTSIZE;
        nextindent-=INDENTSIZE;
        break;
      case BASTOKEN_NEXT:
        if (nextindent == thisindent) thisindent-=INDENTSIZE;
        nextindent-=INDENTSIZE;
        elp = skip_source(elp);
        while (*elp != asc_NUL && *elp != ':' && *elp != BASTOKEN_XELSE && *elp != BASTOKEN_ELSE) { /* Check for 'NEXT I%,J%,K%' */
          if (*elp == ',')  nextindent-=INDENTSIZE;
          elp = skip_source(elp);
        }
        break;
      }
      elp = skip_source(elp);
    }
    if (thisindent<0) thisindent = 0;
    if (nextindent<0) nextindent = 0;
    for (n=1; n<=thisindent; n++) {
      *text = ' ';
      text++;
      bufsz--;
    }
    indentation = nextindent;
    elp = skip(line+OFFSOURCE);
  }
  token = *elp;
/* Indentation sorted out. Now expand the line */
  while (token != asc_NUL) {
/* Deal with special cases first */
    if (token == BASTOKEN_XLINENUM) {      /* Line number */
      elp++;
      count = snprintf(text, MAXSTRING, "%d", GET_LINENO(elp));
      text+=count;
      bufsz-=count;
      elp+=LINESIZE;
    }
    else if (token == BASTOKEN_XVAR)       /* Marks start of variable name - Ignore */
      elp++;
    else if (token == '"') {    /* Character string */
      do {      /* Copy characters up to next '"' */
        *text = *elp;
        text++;
        bufsz--;
        elp++;
      } while (*elp != '"' && *elp != asc_NUL);
      if (*elp == '"') { /* '"' at end of string */
        *text = '"';
        text++;
        bufsz--;
        elp++;
      }
    }
    else if (token<BASTOKEN_LOWEST) {      /* Normal characters */
      *text = token;
      text++;
      bufsz--;
      elp++;
    }
    else if (token == BASTOKEN_DATA || token == BASTOKEN_REM) {       /* 'DATA' and 'REM' are a special case */
      count = expand_token(text, onebytelist, token-BASTOKEN_LOWEST, bufsz);
      text+=count;
      elp++;
      while (*elp != asc_NUL) {      /* Copy rest of line after 'DATA' or ' REM' */
        *text = *elp;
        text++;
        bufsz--;
        elp++;
      }
    }
    else {      /* Single byte tokens */
      switch (token) {
      case TYPE_PRINTFN:
        elp++;
        token = *elp;
        if (token>BASTOKEN_TAB) {
          error(ERR_BADPROG);
          return;
        }
        count = expand_token(text, printlist, token, bufsz);
        break;
      case TYPE_FUNCTION:       /* Built-in Function */
        elp++;
        token = *elp;
        if (token>BASTOKEN_XLATEDOL) {
          error(ERR_BADPROG);
          return;
        }
        count = expand_token(text, functionlist, token, bufsz);
        break;
      case TYPE_COMMAND:
        elp++;
        token = *elp;
        if (token>BASTOKEN_TWINO) {
          error(ERR_BADPROG);
          return;
        }
        count = expand_token(text, commandlist, token, bufsz);
        break;
      default:
        count = expand_token(text, onebytelist, token-BASTOKEN_LOWEST, bufsz);
      }
      text+=count;
      bufsz--;
      elp++;
    }
    token = *elp;
  }
  *text = asc_NUL;
  DEBUGFUNCMSGOUT;
}

void reset_indent(void) {
  DEBUGFUNCMSGIN;
  DEBUGFUNCMSGOUT;
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

  DEBUGFUNCMSGIN;
  offset = dest-tp;
  *(tp) = CAST(offset, byte);
  *(tp+1) = CAST(offset>>BYTESHIFT, byte);
  basicvars.runflags.has_offsets = TRUE;
  DEBUGFUNCMSGOUT;
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

  DEBUGFUNCMSGIN;
  basicvars.runflags.has_offsets = TRUE;
  offset = CAST(p, byte *)-basicvars.workspace;
  for (n=0; n<LOFFSIZE; n++) {
    tp++;
    *tp = CAST(offset, byte);
    offset = offset>>BYTESHIFT;
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'clear_varaddrs' goes through the line passed to it and resets any variable
** or procedure references to 'unknown'.
*/
static void clear_varaddrs(byte *bp) {
  byte *sp, *tp;
  int offset;

  DEBUGFUNCMSGIN;
  if (*bp == BASTOKEN_REM) {
    DEBUGFUNCMSGOUT;
    return;
  }
  sp = bp+OFFSOURCE;            /* Point at start of source code */
  tp = FIND_EXEC(bp);           /* Get address of start of executable tokens */
  while (*tp != asc_NUL) {
    if (*tp == BASTOKEN_XVAR || (*tp >= BASTOKEN_UINT8VAR && *tp <= BASTOKEN_FLOATINDVAR)) {
      while (*sp != BASTOKEN_XVAR && *sp != asc_NUL) sp = skip_source(sp);     /* Locate variable in source part of line */
      if (*sp == asc_NUL) {
        error(ERR_BROKEN, __LINE__, "tokens");            /* Cannot find variable - Logic error */
        return;
      }
      sp++;     /* Point at first char of name */
      if (*tp != BASTOKEN_XVAR) {
        *tp = BASTOKEN_XVAR;
        offset = tp-sp;         /* Offset from 'XVAR' token to variable name */
        *(tp+1) = CAST(offset, byte);
        *(tp+2) = CAST(offset>>BYTESHIFT, byte);
      }
    }
    else if (*tp == BASTOKEN_FNPROCALL || *tp == BASTOKEN_XFNPROCALL) {
      while (*sp != BASTOKEN_PROC && *sp != BASTOKEN_FN && *sp != asc_NUL) sp++;  /* Find PROC/FN name */
      if (*tp == BASTOKEN_FNPROCALL) {     /* Reset PROC/FN ref that has been filled in */
        *tp = BASTOKEN_XFNPROCALL;
        offset = tp-sp;         /* Offset from 'XVAR' token to variable name */
        *(tp+1) = CAST(offset, byte);
        *(tp+2) = CAST(offset>>BYTESHIFT, byte);
      }
      sp++;     /* Skip PROC or FN token */
    }
    else if (*tp == BASTOKEN_CASE) {
      *tp = BASTOKEN_XCASE;
    }
    tp = skip_token(tp);
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'clear_branches' goes through the line passed to it and resets any
** branch-type to their 'unknown destination' versions. The code, in
** general, assumes that the 'unknown destination' token has a value
** one less than the 'offset filled in' version
*/
static void clear_branches(byte *bp) {
  byte *tp, *blp;
  int line;

  DEBUGFUNCMSGIN;
  tp = FIND_EXEC(bp);
  while (*tp != asc_NUL) {
    switch (*tp) {
    case BASTOKEN_LINENUM:
      *tp = BASTOKEN_XLINENUM;     /* Reset to 'X' version of token */
      blp = get_address(tp);     /* Find the line the token refers to */
      line = GET_LINENO(find_linestart(blp));    /* Find the number of the line refered to */
      *(tp+1) = CAST(line, byte);       /* Store the line number */
      *(tp+2) = CAST(line>>BYTESHIFT, byte);
      break;
    case BASTOKEN_BLOCKIF: case BASTOKEN_SINGLIF:
      *tp = BASTOKEN_XIF;
      break;
    case BASTOKEN_ELSE: case BASTOKEN_LHELSE: case BASTOKEN_WHEN: case BASTOKEN_OTHERWISE: case BASTOKEN_WHILE:
      (*tp)--;  /* Reset to 'X' version of token */
    }
    tp = skip_token(tp);
  }
  DEBUGFUNCMSGOUT;
}

void clear_linerefs(byte *bp) {
  DEBUGFUNCMSGIN;
  clear_branches(bp);
  clear_varaddrs(bp);
  DEBUGFUNCMSGOUT;
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
  library *libp;

  DEBUGFUNCMSGIN;
  bp = basicvars.start;
  while (!AT_PROGEND(bp)) {
    clear_varaddrs(bp);
    bp = bp+GET_LINELEN(bp);
  }
  libp = basicvars.installist;    /* Now clear the pointers in any installed libraries */
  while (libp != NIL) {
    bp = libp->libstart;
    while (!AT_PROGEND(bp)) {
      clear_varaddrs(bp);
      bp = bp+GET_LINELEN(bp);
    }
    libp = libp->libflink;
  }
  DEBUGFUNCMSGOUT;
}


/*
** 'isvalid' is called to examine the line passed to it to check that it is
** valid. It checks:
** 1) That the line number is in the range 0..65279
** 2) That the line length is between 4 and 1024 bytes
** 3) That all the tokens in the line are in range
*/
static boolean legalow [] = {   /* Tokens in range 00.1F */
  FALSE, TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,    /* 00..07 */
  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  FALSE, FALSE,   /* 08..0F */
  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,    /* 10..17 */
  TRUE,  FALSE, FALSE, FALSE, FALSE, FALSE, TRUE,  TRUE     /* 18..1F */
};

/*
** 'isvalid' checks that the line passed to it contains legal tokens.
** It ruturns TRUE if the line is okay otherwise it returns FALSE.
** The function only checks the executable tokens
*/
boolean isvalid(byte *bp) {
  int length, execoff;
  byte *base, *cp;

  DEBUGFUNCMSGIN;
  if (GET_LINENO(bp)>MAXLINENO) {   /* Line number is out of range */
    DEBUGFUNCMSGOUT;
    return FALSE;
  }
  length = GET_LINELEN(bp);
  if (length<MINSTATELEN || length>MAXSTATELEN) {
    DEBUGFUNCMSGOUT;
    return FALSE;
  }
  execoff = get_exec(bp);
  if (execoff<OFFSOURCE || execoff>length) {
    DEBUGFUNCMSGOUT;
    return FALSE;
  }
  base = cp = bp+execoff;
  while (cp-base<=length && *cp != asc_NUL) {
    byte token = *cp;
    if (token<=LOW_HIGHEST) {       /* In lower block of tokens */
      if (!legalow[token]) {        /* Bad token value found */
        DEBUGFUNCMSGOUT;
        return FALSE;
      }
    }
    else if (token>=BASTOKEN_LOWEST) {
      switch (token) {
      case TYPE_PRINTFN:
        if (cp[1] == 0 || cp[1] > BASTOKEN_TAB) {
          DEBUGFUNCMSGOUT;
          return FALSE;
        }
        break;
     case TYPE_FUNCTION:
        if (cp[1] == 0 || (cp[1] > BASTOKEN_TIME && cp[1] < BASTOKEN_ABS) || cp[1] > BASTOKEN_VPOS) {
          DEBUGFUNCMSGOUT;
          return FALSE;
        }
        break;
      case TYPE_COMMAND:
        if (cp[1] == 0 || cp[1] > BASTOKEN_TWINO) {
          DEBUGFUNCMSGOUT;
          return FALSE;
        }
        break;
      default:
        if (token > BASTOKEN_HIGHEST) {
          DEBUGFUNCMSGOUT;
          return FALSE;
        }
      }
    }
    cp = skip_token(cp);
  }
  DEBUGFUNCMSGOUT;
  return (*cp == asc_NUL);
}

/*
** 'resolve_linenums' goes through a line and resolves all line number
** references. It replaces line number references with pointers to the
** *start* of the wanted line
*/
void resolve_linenums(byte *bp) {
  byte *dest;
  int32 line;

  DEBUGFUNCMSGIN;
  bp = FIND_EXEC(bp);
  while (*bp != asc_NUL) {
    if (*bp == BASTOKEN_XLINENUM) {        /* Unresolved reference */
      line = GET_LINENUM(bp);
      dest = find_line(line);
      if (line == GET_LINENO(dest)) {   /* Found line number */
        set_address(bp, dest);
        *bp = BASTOKEN_LINENUM;
      }
    }
    else if (*bp == BASTOKEN_LINENUM) {    /* Resolved reference - Replace with ref to start of line */
      dest = get_address(bp);
      set_address(bp, find_linestart(dest));
    }
    bp = skip_token(bp);
  }
  DEBUGFUNCMSGOUT;
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

  DEBUGFUNCMSGIN;
  sp = bp+OFFSOURCE;
  bp = FIND_EXEC(bp);
  while (*bp != asc_NUL) {
    if (*bp == BASTOKEN_LINENUM || *bp == BASTOKEN_XLINENUM) {        /* Find corresponding ref in source */
      while (*sp != BASTOKEN_XLINENUM && *sp != asc_NUL) sp++;
      if (*sp == asc_NUL) {
        error(ERR_BROKEN, __LINE__, "tokens");            /* Sanity check */
        return;
      }
    }
    if (*bp == BASTOKEN_LINENUM) { /* Line number reference that has to be updated */
      dest = get_address(bp);
      line = GET_LINENO(dest);  /* Fetch the new line number */
/* Update the line number in the source part of the line */
      set_linenum(sp, line);
      sp+=1+LINESIZE;   /* Skip the line number in the source */
/* Change the address to point at the executable tokens */
      set_address(bp, FIND_EXEC(dest));
    }
    else if (*bp == BASTOKEN_XLINENUM) {   /* Line number missing - Issue a warning */
      byte *savedcurr = basicvars.current;
      basicvars.current = bp;   /* Ensure error message shows the line number of the bad line */
      error(WARN_LINEMISS, GET_LINENUM(bp));
      basicvars.current = savedcurr;
      sp+=1+LINESIZE;   /* Skip the line number in the source */
    }
    bp = skip_token(bp);
  }
  DEBUGFUNCMSGOUT;
}

/*  =============== Acorn -> Brandy token conversion =============== */

/* Legal range of values for each Acorn token type */

#define ACORNONE_LOWEST         0x7Fu
#define ACORNONE_HIGHEST        0xFFu
#define RUSSELL_LOWEST          0x01u
#define RUSSELL_HIGHEST         0x10u
#define RUSSELL_OVERLAY_LOWEST  0xC6u
#define RUSSELL_OVERLAY_HIGHEST 0xCEu

#define ACORN_OTHER             0xC6u   /* Two byte tokens preceded by C6 (functions) */
#define ACORN_COMMAND           0xC7u   /* Two byte tokens preceded by C7 (immediate commands) */
#define ACORN_TWOBYTE           0xC8u   /* Two byte tokens preceded by C8 (commands) */

#define ACORNTWO_LOWEST         0x8Eu
#define ACORNTWO_HIGHEST        0xA6u
#define ACORNCMD_LOWEST         0x8Eu
#define ACORNCMD_HIGHEST        0x9Fu
#define ACORNOTH_LOWEST         0x8Eu
#define ACORNOTH_HIGHEST        0x96u

#define ACORN_ENDLINE           0x0Du   /* Marks the end of a tokenised line */
#define ACORN_LINENUM           0x8Du   /* Token that preceeds a line number */

#define ACORN_TIME1             0x91u
#define ACORN_FN                0xA4u
#define ACORN_TO                0xB8u
#define ACORN_TIME2             0xD1u
#define ACORN_DATA              0xDCu
#define ACORN_PROC              0xF2u
#define ACORN_REM               0xF4u
#define ACORN_TAB               0x8Au
#define ACORN_INSTR             0xA7u
#define ACORN_POINT             0xB0u
#define ACORN_LEFT_DOL          0xC0u
#define ACORN_MID_DOL           0xC1u
#define ACORN_RIGHT_DOL         0xC2u
#define ACORN_STRING_DOL        0xC4u

#define ACORNLEN                1024    /* Size of buffer to hold plain text version of line */

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

  DEBUGFUNCMSGIN;
  a = *p;
  b = *(p+1);
  c = *(p+2);
  line = ((a<<4) ^ c) & 0xff;
  DEBUGFUNCMSGOUT;
  return (line<<8) | ((((a<<2) & 0xc0) ^ b) & 0xff);
}

static char *lowbyte_token [] = {
  "CIRCLE",    "ELLIPSE", "FILL",      "MOUSE",   /* 0x01..0x04 */
  "ORIGIN",    "QUIT",    "RECTANGLE", "SWAP",    /* 0x05..0x08 */
  "SYS",       "TINT",    "WAIT",      "INSTALL", /* 0x09..0x0C */
   NIL,        "PRIVATE", "BY",        "EXIT"     /* 0x0D..0x10 */
};

static char *winbyte_token [] = {
  "SUM",     "WHILE",     "CASE",  "WHEN", "OF",  /* 0xC6..0xCA */
  "ENDCASE", "OTHERWISE", "ENDIF", "ENDWHILE"     /* 0xCB..0xCE */
};

static char *bbcbyte_token [] = {
  "AUTO", "DELETE",   "LOAD", "LIST", "NEW",      /* 0xC6..0xCA */
  "OLD",  "RENUMBER", "SAVE", "EDIT"              /* 0xCB..0xCE */
};

static char *onebyte_token [] = {
  "OTHERWISE",                                    /* 0x7F */
  "AND",       "DIV",     "EOR",      "MOD",      /* 0x80..0x83 */
  "OR",        "ERROR",   "LINE",     "OFF",      /* 0x84..0x87 */
  "STEP",      "SPC",     "TAB(",     "ELSE",     /* 0x88..0x8B */
  "THEN",       NIL,      "OPENIN",   "PTR",      /* 0x8C..0x8F */
  "PAGE",      "TIME",    "LOMEM",    "HIMEM",    /* 0x90..0x93 */
  "ABS",       "ACS",     "ADVAL",    "ASC",      /* 0x94..0x97 */
  "ASN",       "ATN",     "BGET",     "COS",      /* 0x98..0x9B */
  "COUNT",     "DEG",     "ERL",      "ERR",      /* 0x9C..0x9F */
  "EVAL",      "EXP",     "EXT",      "FALSE",    /* 0xA0..0xA3 */
  "FN",        "GET",     "INKEY",    "INSTR(",   /* 0xA4..0xA7 */
  "INT",       "LEN",     "LN",       "LOG",      /* 0xA8..0xAB */
  "NOT",       "OPENUP",  "OPENOUT",  "PI",       /* 0xAC..0xAF */
  "POINT(",    "POS",     "RAD",      "RND",      /* 0xB0..0xB3 */
  "SGN",       "SIN",     "SQR",      "TAN",      /* 0xB4..0xB7 */
  "TO",        "TRUE",    "USR",      "VAL",      /* 0xB8..0xBB */
  "VPOS",      "CHR$",    "GET$",     "INKEY$",   /* 0xBC..0xBF */
  "LEFT$(",    "MID$(",   "RIGHT$(",  "STR$",     /* 0xC0..0xC3 */
  "STRING$(",  "EOF",      NIL,        NIL,       /* 0xC4..0xC7 */
  NIL,         "WHEN",    "OF",       "ENDCASE",  /* 0xC8..0xCB */
  "ELSE",      "ENDIF",   "ENDWHILE", "PTR",      /* 0xCC..0xCF */
  "PAGE",      "TIME",    "LOMEM",    "HIMEM",    /* 0xD0..0xD3 */
  "SOUND",     "BPUT",    "CALL",     "CHAIN",    /* 0xD4..0xD7 */
  "CLEAR",     "CLOSE",   "CLG",      "CLS",      /* 0xD8..0xDB */
  "DATA",      "DEF",     "DIM",      "DRAW",     /* 0xDC..0xDF */
  "END",       "ENDPROC", "ENVELOPE", "FOR",      /* 0xE0..0xE3 */
  "GOSUB",     "GOTO",    "GCOL",     "IF",       /* 0xE4..0xE7 */
  "INPUT",     "LET",     "LOCAL",    "MODE",     /* 0xE8..0xEB */
  "MOVE",      "NEXT",    "ON",       "VDU",      /* 0xEC..0xEF */
  "PLOT",      "PRINT",   "PROC",     "READ",     /* 0xF0..0xF3 */
  "REM",       "REPEAT",  "REPORT",   "RESTORE",  /* 0xF4..0xF7 */
  "RETURN",    "RUN",     "STOP",     "COLOUR",   /* 0xF8..0xFB */
  "TRACE",     "UNTIL",   "WIDTH",    "OSCLI"     /* 0xFC..0xFF */
};

/* Basic statement types - Two byte tokens preceded by 0xC8 */

static char *twobyte_token [] = {
  "CASE",      "CIRCLE",    "FILL",   "ORIGIN",   /* 0x8E..0x91 */
  "POINT",     "RECTANGLE", "SWAP",   "WHILE",    /* 0x92..0x95 */
  "WAIT",      "MOUSE",     "QUIT",   "SYS",      /* 0x96..0x99 */
  "INSTALL",   "LIBRARY",   "TINT",   "ELLIPSE",  /* 0x9A..0x9D */
  "BEATS",     "TEMPO",     "VOICES", "VOICE",    /* 0x9E..0xA1 */
  "STEREO",    "OVERLAY",   "MANDEL", "PRIVATE",  /* 0xA2..0xA6 */
  "EXIT"                                          /* A7 */
};

/* Basic commands - Two byte tokens preceded by 0xC7 */

static char *command_token [] = {
  "APPEND", "AUTO",     "CRUNCH",   "DELETE",     /* 0x8E..0x91 */
  "EDIT",   "HELP",     "LIST",     "LOAD",       /* 0x92..0x95 */
  "LVAR",   "NEW",      "OLD",      "RENUMBER",   /* 0x96..0x99 */
  "SAVE",   "TEXTLOAD", "TEXTSAVE", "TWIN",       /* 0x9A..0x9D */
  "TWINO",  "INSTALL"                             /* 0x9E..0x9F */
};

/* Basic functions - Two byte tokens preceded by 0xC6 */

static char *other_token [] = {
  "SUM",       "BEAT",     "ANSWER",  "SFOPENIN", /* 0x8E..0x91 */
  "SFOPENOUT", "SFOPENUP", "SFNAME$", "MENU"      /* 0x92..0x96 */
};

/*
 * nospace - Tokens that should not or need not be followed by a
 * space if expanding crunched code
 */
static byte nospace [] = {
  ACORN_FN,       ACORN_PROC,    ACORN_TO,        ACORN_TIME1,
  ACORN_TIME2,    ACORN_TAB,     ACORN_INSTR,     ACORN_POINT,
  ACORN_LEFT_DOL, ACORN_MID_DOL, ACORN_RIGHT_DOL, ACORN_STRING_DOL, 0
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
int32 reformat(byte *tp, byte *tokenbuf, int32 ftype) {
  int count;
  char *cp = NULL, *cporig, *p = NULL;
  byte token, token2;
  char line[ACORNLEN];

  DEBUGFUNCMSGIN;
  cp = cporig = &line[0];
  count = snprintf(cp, ACORNLEN, "%d", (*tp<<8) + *(tp+1));          /* Start with two byte line number */
  cp+=count;
  tp+=ACORN_START;                                        /* Skip line number and length byte */
  token = *tp;
  while (token != ACORN_ENDLINE) {
    if (token>RUSSELL_HIGHEST && token<ACORNONE_LOWEST) { /* Normal characters */
      *cp = token;
      cp++;
      tp++;
      if (token == '\"') {                                /* Got a character string */
        do {                                              /* Copy string as far as next '"' or end of line */
          *cp = token = *tp;
          cp++;
          tp++;
        } while (token != '\"' && *tp != ACORN_ENDLINE);
      }
    } else if (token == ACORN_LINENUM) {
      count = snprintf(cp, ACORNLEN - (cp - cporig), "%d", expand_linenum(tp+1));
      cp+=count;
      tp+=ACORN_LINESIZE;
    } else if (token == ACORN_REM || token == ACORN_DATA) { /* REM or DATA - Copy rest of line */
      p = onebyte_token[token-ACORNONE_LOWEST];
      STRLCPY(cp, p, MAXSTRING);
      cp+=strlen(p);
      tp++;
      while (*tp != ACORN_ENDLINE) {
        *cp = *tp;
        cp++;
        tp++;
      }
    } else {      /* Tokens */
      if (token == 0xCDu) {                                           /* CD    */
        p=(char *)tp+1;
        while(*p == ' ') p++;
        if (*p == asc_CR || *p == ':') {
          p = onebyte_token[token-ACORNONE_LOWEST];
        } else {
          p = bbcbyte_token[token-ACORN_OTHER];
        }
      } else if (token >= RUSSELL_LOWEST && token <= RUSSELL_HIGHEST) {    /* 01-10 */
        p = lowbyte_token[token-RUSSELL_LOWEST];
      } else if (ftype == 2 && token >= RUSSELL_OVERLAY_LOWEST && token <= RUSSELL_OVERLAY_HIGHEST) {
        p = winbyte_token[token-ACORN_OTHER];
      } else if (token < ACORN_OTHER || token > ACORN_TWOBYTE) {         /* 7F-C5, C9-FF */ 
        p = onebyte_token[token-ACORNONE_LOWEST];
      } else {
        token2 = *(tp+1);
        if (token2 < ACORNTWO_LOWEST) {
          p = bbcbyte_token[token-ACORN_OTHER];                 /* Cx <8E  */
        } else {
          switch (token) {
            case ACORN_TWOBYTE:                                 /* C8 nn   */
              if (token2>ACORNTWO_HIGHEST) {
                if((int32)(token2-ACORN_OTHER) < 0) {           /* Sanity check */
                  error(ERR_BROKEN, __LINE__, "tokens");
                  return 0;
                }
                p = bbcbyte_token[token2-ACORN_OTHER];          /* C8      */
              } else {
                p = twobyte_token[token2-ACORNTWO_LOWEST];      /* C8 8E+n */
                tp++;
                break;
              }
            case ACORN_COMMAND:                                 /* C7 nn   */
              if (token2>ACORNCMD_HIGHEST) {
                if((int32)(token2-ACORN_OTHER) < 0) {           /* Sanity check */
                  error(ERR_BROKEN, __LINE__, "tokens");
                  return 0;
                }
                p = bbcbyte_token[token2-ACORN_OTHER];          /* C7      */
              } else {
                if(token-ACORNCMD_LOWEST >= 18) {
                  error(WARN_BADTOKEN);
                } else {
                  p = command_token[token-ACORNCMD_LOWEST];       /* C7 8E+n */
                }
                tp++;
                break;
              }
            case ACORN_OTHER:                                   /* C6 nn   */
              if (token2>ACORNOTH_HIGHEST) {
                if((int32)(token2-ACORN_OTHER) < 0) {           /* Sanity check */
                  error(ERR_BROKEN, __LINE__, "tokens");
                  return 0;
                }
                p = bbcbyte_token[token2-ACORN_OTHER];          /* C6      */
              } else {
                if (token-ACORNOTH_LOWEST >= 8) {
                  error(WARN_BADTOKEN);
                } else {
                  p = other_token[token-ACORNOTH_LOWEST];
                }
                tp++;
                break;
              }
          } /* switch */
        }
      }
      tp++;
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
      if (p == NULL) {
        error(ERR_BROKEN, __LINE__, "tokens");
        return(-1);
      }
      STRLCPY(cp, p, MAXSTRING);
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
  *cp = asc_NUL;    /* Complete the line */
  tokenize(line, tokenbuf, HASLINE, FALSE);
  DEBUGFUNCMSGOUT;
  return GET_LINELEN(tokenbuf);
}
