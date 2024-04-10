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
**      This file defines the token set
*/

#ifndef __token_h
#define __token_h

#include "common.h"

#define NOLINENO 0xFFFF                 /* Marks line as having no line number */

#define OFFLINE         0u              /* Offset of line number in tokenised line */
#define OFFLENGTH       2u              /* Offset of line length in tokenised line */
#define OFFEXEC         4u              /* Offset of first executable token */
#define OFFSOURCE       6u              /* Offset of first byte of source */

#define HASLINE TRUE
#define NOLINE FALSE

#define ENDMARKER (ENDLINENO>>BYTESHIFT) /* Used when checking if the end of program has been reached */

/*
** The token set. 'X' versions of tokens are used when addresses have not
** been filled in. They must precede the non-X versions as the code relies
** on this order
*/

#define LOW_HIGHEST             0x1Fu                   /* Highest token value in 0..0x1F block */
#define BASIC_TOKEN_LOWEST      BASIC_TOKEN_AND         /* Lowest single byte token value */
#define BASIC_TOKEN_HIGHEST     BASIC_TOKEN_WIDTH       /* Highest single byte token value */
#define COMMAND_LOWEST          BASIC_TOKEN_APPEND      /* Lowest direct command token value */
#define COMMAND_HIGHEST         BASIC_TOKEN_TWINO       /* Highest direct command token value */

#define TYPE_ONEBYTE            0u

#define TYPE_COMMAND            0xFCu           /* Basic commands */
#define TYPE_PRINTFN            0xFEu           /* Special I/O functions SPC() and TAB() */
#define TYPE_FUNCTION           0xFFu           /* Other functions */

#define BADLINE_MARK            0xFDu           /* Marks a bad line */

#define BASIC_TOKEN_NOWT        0u

/* Variables and constants */

#define BASIC_TOKEN_EOL         0u              /* End of line */

#define BASIC_TOKEN_XVAR        0x01u           /* Reference to variable in source code */
#define BASIC_TOKEN_STATICVAR   0x02u           /* Simple reference to a static variable */
#define BASIC_TOKEN_UINT8VAR    0x03u           /* Simple reference to an unsigned 8-bit int variable */
#define BASIC_TOKEN_INTVAR      0x04u           /* Simple reference to an integer variable */
#define BASIC_TOKEN_INT64VAR    0x05u           /* Simple reference to a 64-bit int variable */
#define BASIC_TOKEN_FLOATVAR    0x06u           /* Simple reference to a floating point variable */
#define BASIC_TOKEN_STRINGVAR   0x07u           /* Simple reference to a string variable */
#define BASIC_TOKEN_ARRAYVAR    0x08u           /* Array or array followed by an indirection operator */
#define BASIC_TOKEN_ARRAYREF    0x09u           /* Reference to whole array */
#define BASIC_TOKEN_ARRAYINDVAR 0x0Au           /* Array element followed by indirection operator */
#define BASIC_TOKEN_INTINDVAR   0x0Bu           /* 32-bit integer variable followed by indirection operator */
#define BASIC_TOKEN_INT64INDVAR 0x0Cu           /* 64-bit integer variable followed by indirection operator */
#define BASIC_TOKEN_FLOATINDVAR 0x0Du           /* Floating point variable followed by indirection operator */
#define BASIC_TOKEN_STATINDVAR  0x0Eu           /* Static variable followed by indirection operator */

#define BASIC_TOKEN_XFNPROCALL  0x0Fu           /* Reference to unknown PROC or FN */
#define BASIC_TOKEN_FNPROCALL   0x10u           /* Reference to known PROC or FN */

#define BASIC_TOKEN_INTZERO     0x11u           /* Integer 0 */
#define BASIC_TOKEN_INTONE      0x12u           /* Integer 1 */
#define BASIC_TOKEN_SMALLINT    0x13u           /* Small integer constant in range 1..256 */
#define BASIC_TOKEN_INTCON      0x14u           /* 32-bit integer constant */
#define BASIC_TOKEN_FLOATZERO   0x15u           /* Floating point 0.0 */
#define BASIC_TOKEN_FLOATONE    0x16u           /* Floating point 1.0 */
#define BASIC_TOKEN_FLOATCON    0x17u           /* Floating point constant */
#define BASIC_TOKEN_STRINGCON   0x18u           /* Ordinary string constant */
#define BASIC_TOKEN_QSTRINGCON  0x19u           /* String constant with a '"' in it */
#define BASIC_TOKEN_INT64CON    0x1Au           /* 64-bit integer constant */

#define BASIC_TOKEN_XLINENUM    0x1Eu           /* Unresolved line number reference */
#define BASIC_TOKEN_LINENUM     0x1Fu           /* Resolved line number reference */

/* Unused tokens */

#define UNUSED_1B       0x1Bu
#define UNUSED_1C       0x1Cu
#define UNUSED_1D       0x1Du

/* Operators */

#define BASIC_TOKEN_AND         0x80u
#define BASIC_TOKEN_ASR         0x81u   /* >> */
#define BASIC_TOKEN_DIV         0x82u
#define BASIC_TOKEN_EOR         0x83u
#define BASIC_TOKEN_GE          0x84u   /* >= */
#define BASIC_TOKEN_LE          0x85u   /* <= */
#define BASIC_TOKEN_LSL         0x86u   /* << */
#define BASIC_TOKEN_LSR         0x87u   /* >>> */
#define BASIC_TOKEN_MINUSAB     0x88u   /* -= */
#define BASIC_TOKEN_MOD         0x89u
#define BASIC_TOKEN_NE          0x8Au   /* <> */
#define BASIC_TOKEN_OR          0x8Bu
#define BASIC_TOKEN_PLUSAB      0x8Cu   /* += */

/* Statements */

#define BASIC_TOKEN_BEATS       0x8Du
#define BASIC_TOKEN_BPUT        0x8Eu
#define BASIC_TOKEN_CALL        0x8Fu
#define BASIC_TOKEN_XCASE       0x90u
#define BASIC_TOKEN_CASE        0x91u
#define BASIC_TOKEN_CHAIN       0x92u
#define BASIC_TOKEN_CIRCLE      0x93u
#define BASIC_TOKEN_CLG         0x94u
#define BASIC_TOKEN_CLEAR       0x95u
#define BASIC_TOKEN_CLOSE       0x96u
#define BASIC_TOKEN_CLS         0x97u
#define BASIC_TOKEN_COLOUR      0x98u
#define BASIC_TOKEN_DATA        0x99u
#define BASIC_TOKEN_DEF         0x9Au
#define BASIC_TOKEN_DIM         0x9Bu
#define BASIC_TOKEN_DRAW        0x9Cu
#define BASIC_TOKEN_DRAWBY      0x9Du   /* 'DRAWBY' has to follow 'DRAW */
#define BASIC_TOKEN_ELLIPSE     0x9Eu
#define BASIC_TOKEN_XELSE       0x9Fu
#define BASIC_TOKEN_ELSE        0xA0u
#define BASIC_TOKEN_XLHELSE     0xA1u
#define BASIC_TOKEN_LHELSE      0xA2u
#define BASIC_TOKEN_END         0xA3u
#define BASIC_TOKEN_ENDCASE     0xA4u
#define BASIC_TOKEN_ENDIF       0xA5u
#define BASIC_TOKEN_ENDPROC     0xA6u
#define BASIC_TOKEN_ENDWHILE    0xA7u
#define BASIC_TOKEN_ENVELOPE    0xA8u
#define BASIC_TOKEN_ERROR       0xA9u
#define BASIC_TOKEN_FALSE       0xAAu
#define BASIC_TOKEN_FILL        0xABu
#define BASIC_TOKEN_FILLBY      0xACu
#define BASIC_TOKEN_FN          0xADu
#define BASIC_TOKEN_FOR         0xAEu
#define BASIC_TOKEN_GCOL        0xAFu
#define BASIC_TOKEN_GOSUB       0xB0u
#define BASIC_TOKEN_GOTO        0xB1u
#define BASIC_TOKEN_XIF         0xB2u
#define BASIC_TOKEN_BLOCKIF     0xB3u
#define BASIC_TOKEN_SINGLIF     0xB4u
#define BASIC_TOKEN_INPUT       0xB5u
#define BASIC_TOKEN_LET         0xB6u
#define BASIC_TOKEN_LIBRARY     0xB7u
#define BASIC_TOKEN_LINE        0xB8u
#define BASIC_TOKEN_LOCAL       0xB9u
#define BASIC_TOKEN_MODE        0xBAu
#define BASIC_TOKEN_MOUSE       0xBBu
#define BASIC_TOKEN_MOVE        0xBCu
#define BASIC_TOKEN_MOVEBY      0xBDu   /* 'MOVEBY' has to follow 'MOVE' */
#define BASIC_TOKEN_NEXT        0xBEu
#define BASIC_TOKEN_NOT         0xBFu
#define BASIC_TOKEN_OF          0xC0u
#define BASIC_TOKEN_OFF         0xC1u
#define BASIC_TOKEN_ON          0xC2u
#define BASIC_TOKEN_ORIGIN      0xC3u
#define BASIC_TOKEN_OSCLI       0xC4u
#define BASIC_TOKEN_XOTHERWISE  0xC5u
#define BASIC_TOKEN_OTHERWISE   0xC6u
#define BASIC_TOKEN_OVERLAY     0xC7u
#define BASIC_TOKEN_PLOT        0xC8u
#define BASIC_TOKEN_POINT       0xC9u   /* POINT at a statement */
#define BASIC_TOKEN_POINTBY     0xCAu   /* 'POINTBY' has to follow 'POINT' */
#define BASIC_TOKEN_POINTTO     0xCBu
#define BASIC_TOKEN_PRINT       0xCCu
#define BASIC_TOKEN_PROC        0xCDu
#define BASIC_TOKEN_QUIT        0xCEu
#define BASIC_TOKEN_READ        0xCFu
#define BASIC_TOKEN_RECTANGLE   0xD0u
#define BASIC_TOKEN_REM         0xD1u
#define BASIC_TOKEN_REPEAT      0xD2u
#define BASIC_TOKEN_REPORT      0xD3u
#define BASIC_TOKEN_RESTORE     0xD4u
#define BASIC_TOKEN_RETURN      0xD5u
#define BASIC_TOKEN_RUN         0xD6u
#define BASIC_TOKEN_SOUND       0xD7u
#define BASIC_TOKEN_STAR        0xD8u   /* '*' command */
#define BASIC_TOKEN_STEP        0xD9u
#define BASIC_TOKEN_STEREO      0xDAu
#define BASIC_TOKEN_STOP        0xDBu
#define BASIC_TOKEN_SWAP        0xDCu
#define BASIC_TOKEN_SYS         0xDDu
#define BASIC_TOKEN_TEMPO       0xDEu
#define BASIC_TOKEN_THEN        0xDFu
#define BASIC_TOKEN_TINT        0xE0u
#define BASIC_TOKEN_TO          0xE1u
#define BASIC_TOKEN_TRACE       0xE2u
#define BASIC_TOKEN_TRUE        0xE3u
#define BASIC_TOKEN_UNTIL       0xE4u
#define BASIC_TOKEN_VDU         0xE5u
#define BASIC_TOKEN_VOICE       0xE6u
#define BASIC_TOKEN_VOICES      0xE7u
#define BASIC_TOKEN_WAIT        0xE8u
#define BASIC_TOKEN_XWHEN       0xE9u
#define BASIC_TOKEN_WHEN        0xEAu
#define BASIC_TOKEN_XWHILE      0xEBu
#define BASIC_TOKEN_WHILE       0xECu
#define BASIC_TOKEN_WIDTH       0xEDu

/* Unused tokens */

#define UNUSED_EE       0xEEu
#define UNUSED_EF       0xEFu
#define UNUSED_F0       0xF0u
#define UNUSED_F1       0xF1u
#define UNUSED_F2       0xF2u
#define UNUSED_F3       0xF3u
#define UNUSED_F4       0xF4u
#define UNUSED_F5       0xF5u
#define UNUSED_F6       0xF6u
#define UNUSED_F7       0xF7u
#define UNUSED_F8       0xF8u
#define UNUSED_F9       0xF9u
#define UNUSED_FA       0xFAu
#define UNUSED_FB       0xFBu

#define UNUSED_FD       0xFDu

/*
** Direct commands
** These are statement types that are generally not allowed to appear
** in a program. This interpreter allows ones that are not going to
** hurt the program such as 'list' to be used.
*/
#define BASIC_TOKEN_APPEND      0x01u
#define BASIC_TOKEN_AUTO        0x02u
#define BASIC_TOKEN_CRUNCH      0x03u
#define BASIC_TOKEN_DELETE      0x04u
#define BASIC_TOKEN_EDIT        0x05u
#define BASIC_TOKEN_EDITO       0x06u
#define BASIC_TOKEN_HELP        0x07u
#define BASIC_TOKEN_INSTALL     0x08u
#define BASIC_TOKEN_LIST        0x09u
#define BASIC_TOKEN_LISTB       0x0Au
#define BASIC_TOKEN_LISTIF      0x0Bu
#define BASIC_TOKEN_LISTL       0x0Cu
#define BASIC_TOKEN_LISTO       0x0Du
#define BASIC_TOKEN_LISTW       0x0Eu
#define BASIC_TOKEN_LOAD        0x0Fu
#define BASIC_TOKEN_LVAR        0x10u
#define BASIC_TOKEN_NEW         0x11u
#define BASIC_TOKEN_OLD         0x12u
#define BASIC_TOKEN_RENUMBER    0x13u
#define BASIC_TOKEN_SAVE        0x14u
#define BASIC_TOKEN_SAVEO       0x15u
#define BASIC_TOKEN_TEXTLOAD    0x16u
#define BASIC_TOKEN_TEXTSAVE    0x17u
#define BASIC_TOKEN_TEXTSAVEO   0x18u
#define BASIC_TOKEN_TWIN        0x19u
#define BASIC_TOKEN_TWINO       0x1Au

/*
** Pseudo variables and functions that can appear on the left hand side
** of an expression. These are preceded by 0xFF in the tokenised code.
*/
#define BASIC_TOKEN_HIMEM       0x01u
#define BASIC_TOKEN_EXT         0x02u
#define BASIC_TOKEN_FILEPATH    0x03u
#define BASIC_TOKEN_LEFT        0x04u
#define BASIC_TOKEN_LOMEM       0x05u
#define BASIC_TOKEN_MID         0x06u
#define BASIC_TOKEN_PAGE        0x07u
#define BASIC_TOKEN_PTR         0x08u
#define BASIC_TOKEN_RIGHT       0x09u
#define BASIC_TOKEN_TIME        0x0Au

/*
** Functions.
** These are preceded with 0xFF in the tokenised code.
*/
#define BASIC_TOKEN_ABS         0x10u
#define BASIC_TOKEN_ACS         0x11u
#define BASIC_TOKEN_ADVAL       0x12u
#define BASIC_TOKEN_ARGC        0x13u
#define BASIC_TOKEN_ARGVDOL     0x14u
#define BASIC_TOKEN_ASC         0x15u
#define BASIC_TOKEN_ASN         0x16u
#define BASIC_TOKEN_ATN         0x17u
#define BASIC_TOKEN_BEAT        0x18u
#define BASIC_TOKEN_BGET        0x19u
#define BASIC_TOKEN_CHR         0x1Au
#define BASIC_TOKEN_COS         0x1Bu
#define BASIC_TOKEN_COUNT       0x1Cu
#define BASIC_TOKEN_DEG         0x1Du
#define BASIC_TOKEN_EOF         0x1Eu
#define BASIC_TOKEN_ERL         0x1Fu
#define BASIC_TOKEN_ERR         0x20u
#define BASIC_TOKEN_EVAL        0x21u
#define BASIC_TOKEN_EXP         0x22u
#define BASIC_TOKEN_GET         0x23u
#define BASIC_TOKEN_GETDOL      0x24u
#define BASIC_TOKEN_INKEY       0x25u
#define BASIC_TOKEN_INKEYDOL    0x26u
#define BASIC_TOKEN_INSTR       0x27u
#define BASIC_TOKEN_INT         0x28u
#define BASIC_TOKEN_LEN         0x29u
#define BASIC_TOKEN_LISTOFN     0x2Au
#define BASIC_TOKEN_LN          0x2Bu
#define BASIC_TOKEN_LOG         0x2Cu
#define BASIC_TOKEN_OPENIN      0x2Du
#define BASIC_TOKEN_OPENOUT     0x2Eu
#define BASIC_TOKEN_OPENUP      0x2Fu
#define BASIC_TOKEN_PI          0x30u
#define BASIC_TOKEN_POINTFN     0x31u   /* The function POINT( */
#define BASIC_TOKEN_POS         0x32u
#define BASIC_TOKEN_RAD         0x33u
#define BASIC_TOKEN_REPORTDOL   0x34u   /* The function REPORT$ */
#define BASIC_TOKEN_RETCODE     0x35u
#define BASIC_TOKEN_RND         0x36u   /* The function RND (with no parameter) */
#define BASIC_TOKEN_SGN         0x37u
#define BASIC_TOKEN_SIN         0x38u
#define BASIC_TOKEN_SQR         0x39u
#define BASIC_TOKEN_STR         0x3Au
#define BASIC_TOKEN_STRING      0x3Bu
#define BASIC_TOKEN_SUM         0x3Cu
#define BASIC_TOKEN_TAN         0x3Du
#define BASIC_TOKEN_TEMPOFN     0x3Eu
#define BASIC_TOKEN_USR         0x3Fu
#define BASIC_TOKEN_VAL         0x40u
#define BASIC_TOKEN_VERIFY      0x41u
#define BASIC_TOKEN_VPOS        0x42u
#define BASIC_TOKEN_SYSFN       0x43u   /* The function SYS( */
#define BASIC_TOKEN_RNDPAR      0x44u   /* The function RND( */
#define BASIC_TOKEN_XLATEDOL    0x45u   /* Must remain the last in the list */

/*
** Print functions preceded with 0xFE
** These can only appear in 'INPUT' and 'PRINT' statements
*/
#define BASIC_TOKEN_SPC 0x01u
#define BASIC_TOKEN_TAB 0x02u

extern byte thisline[];                 /* tokenised version of command line */

extern void tokenize(char *, byte [], boolean, boolean);
extern void expand(byte *, char *);
extern byte *skip_token(byte *);
extern byte *skip_name(byte *);
extern void set_dest(byte *, byte *);
extern void set_address(byte *, void *);
extern byte *get_srcaddr(byte *);
extern void save_lineno(byte *, int32);
extern int32 get_lineno(byte *);        /* Returns line number at start of line */
extern int32 get_linelen(byte *);
extern int32 get_linenum(byte *);       /* Returns line number after 'linenum' token */
extern float64 get_fpvalue(byte *);
extern void clear_varptrs(void);
extern void clear_linerefs(byte *);
extern boolean isvalid(byte *);
extern void reset_indent(void);
extern void resolve_linenums(byte *);
extern void reset_linenums(byte *);
extern int32 reformat(byte *, byte *, int32);
extern boolean isempty(byte []);

#define GET_INTVALUE(p) (*p | (*(p+1)<<8) | (*(p+2)<<16) | (*(p+3)<<24))
#define GET_INT64VALUE(p) (int64)((int64)*p | ((int64)*(p+1)<<8) | ((int64)*(p+2)<<16) | ((int64)*(p+3)<<24) | ((int64)*(p+4)<<32) | ((int64)*(p+5)<<40) | ((int64)*(p+6)<<48) | ((int64)*(p+7)<<56))
#define GET_ADDRESS(p, type) (CAST(basicvars.workspace+(*(p+1) | (*(p+2)<<8) | (*(p+3)<<16) | (*(p+4)<<24)), type))
#define GET_SIZE(p) (*(p) | (*(p+1)<<BYTESHIFT))
#define GET_DEST(p) (p+(*p | (*(p+1)<<BYTESHIFT)))
#define GET_LINELEN(p) (*(p+OFFLENGTH) | (*(p+OFFLENGTH+1)<<BYTESHIFT))
#define GET_LINENO(p) (*(p+OFFLINE) | (*(p+OFFLINE+1)<<BYTESHIFT))
#define GET_SRCADDR(p) (p-(*(p+1)+(*(p+2)<<BYTESHIFT)))
#define AT_PROGEND(p) (*(p+OFFLINE+1)==ENDMARKER)
#define FIND_EXEC(p) (p+*(p+OFFEXEC)+(*(p+OFFEXEC+1)<<BYTESHIFT))

#endif
