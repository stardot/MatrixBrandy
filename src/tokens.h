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
#define BASTOKEN_LOWEST      BASTOKEN_AND         /* Lowest single byte token value */
#define BASTOKEN_HIGHEST     BASTOKEN_WIDTH       /* Highest single byte token value */
#define COMMAND_LOWEST          BASTOKEN_APPEND      /* Lowest direct command token value */
#define COMMAND_HIGHEST         BASTOKEN_TWINO       /* Highest direct command token value */

#define TYPE_ONEBYTE            0u

#define TYPE_COMMAND            0xFCu           /* Basic commands */
#define TYPE_PRINTFN            0xFEu           /* Special I/O functions SPC() and TAB() */
#define TYPE_FUNCTION           0xFFu           /* Other functions */

#define BADLINE_MARK            0xFDu           /* Marks a bad line */

#define BASTOKEN_NOWT        0u

/* Variables and constants */

#define BASTOKEN_EOL         0u              /* End of line */

#define BASTOKEN_XVAR        0x01u           /* Reference to variable in source code */
#define BASTOKEN_STATICVAR   0x02u           /* Simple reference to a static variable */
#define BASTOKEN_UINT8VAR    0x03u           /* Simple reference to an unsigned 8-bit int variable */
#define BASTOKEN_INTVAR      0x04u           /* Simple reference to an integer variable */
#define BASTOKEN_INT64VAR    0x05u           /* Simple reference to a 64-bit int variable */
#define BASTOKEN_FLOATVAR    0x06u           /* Simple reference to a floating point variable */
#define BASTOKEN_STRINGVAR   0x07u           /* Simple reference to a string variable */
#define BASTOKEN_ARRAYVAR    0x08u           /* Array or array followed by an indirection operator */
#define BASTOKEN_ARRAYREF    0x09u           /* Reference to whole array */
#define BASTOKEN_ARRAYINDVAR 0x0Au           /* Array element followed by indirection operator */
#define BASTOKEN_INTINDVAR   0x0Bu           /* 32-bit integer variable followed by indirection operator */
#define BASTOKEN_INT64INDVAR 0x0Cu           /* 64-bit integer variable followed by indirection operator */
#define BASTOKEN_FLOATINDVAR 0x0Du           /* Floating point variable followed by indirection operator */
#define BASTOKEN_STATINDVAR  0x0Eu           /* Static variable followed by indirection operator */

#define BASTOKEN_XFNPROCALL  0x0Fu           /* Reference to unknown PROC or FN */
#define BASTOKEN_FNPROCALL   0x10u           /* Reference to known PROC or FN */

#define BASTOKEN_INTZERO     0x11u           /* Integer 0 */
#define BASTOKEN_INTONE      0x12u           /* Integer 1 */
#define BASTOKEN_SMALLINT    0x13u           /* Small integer constant in range 1..256 */
#define BASTOKEN_INTCON      0x14u           /* 32-bit integer constant */
#define BASTOKEN_FLOATZERO   0x15u           /* Floating point 0.0 */
#define BASTOKEN_FLOATONE    0x16u           /* Floating point 1.0 */
#define BASTOKEN_FLOATCON    0x17u           /* Floating point constant */
#define BASTOKEN_STRINGCON   0x18u           /* Ordinary string constant */
#define BASTOKEN_QSTRINGCON  0x19u           /* String constant with a '"' in it */
#define BASTOKEN_INT64CON    0x1Au           /* 64-bit integer constant */

#define BASTOKEN_XLINENUM    0x1Eu           /* Unresolved line number reference */
#define BASTOKEN_LINENUM     0x1Fu           /* Resolved line number reference */

/* Unused tokens */

#define UNUSED_1B       0x1Bu
#define UNUSED_1C       0x1Cu
#define UNUSED_1D       0x1Du

/* Operators */

#define BASTOKEN_AND         0x80u
#define BASTOKEN_ASR         0x81u   /* >> */
#define BASTOKEN_DIV         0x82u
#define BASTOKEN_EOR         0x83u
#define BASTOKEN_GE          0x84u   /* >= */
#define BASTOKEN_LE          0x85u   /* <= */
#define BASTOKEN_LSL         0x86u   /* << */
#define BASTOKEN_LSR         0x87u   /* >>> */
#define BASTOKEN_MINUSAB     0x88u   /* -= */
#define BASTOKEN_MOD         0x89u
#define BASTOKEN_NE          0x8Au   /* <> */
#define BASTOKEN_OR          0x8Bu
#define BASTOKEN_PLUSAB      0x8Cu   /* += */
#define BASTOKEN_POWRAB      0x8Du   /* ^= */

/* Unused operator tokens */

#define UNUSED_8E       0x8Eu
#define UNUSED_8F       0x8Fu

/* Statements */

#define BASTOKEN_BEATS       0x90u
#define BASTOKEN_BPUT        0x91u
#define BASTOKEN_CALL        0x92u
#define BASTOKEN_XCASE       0x93u
#define BASTOKEN_CASE        0x94u
#define BASTOKEN_CHAIN       0x95u
#define BASTOKEN_CIRCLE      0x96u
#define BASTOKEN_CLG         0x97u
#define BASTOKEN_CLEAR       0x98u
#define BASTOKEN_CLOSE       0x99u
#define BASTOKEN_CLS         0x9Au
#define BASTOKEN_COLOUR      0x9Bu
#define BASTOKEN_DATA        0x9Cu
#define BASTOKEN_DEF         0x9Du
#define BASTOKEN_DIM         0x9Eu
#define BASTOKEN_DRAW        0x9Fu
#define BASTOKEN_BY          0xA0u
#define BASTOKEN_ELLIPSE     0xA1u
#define BASTOKEN_XELSE       0xA2u
#define BASTOKEN_ELSE        0xA3u
#define BASTOKEN_XLHELSE     0xA4u
#define BASTOKEN_LHELSE      0xA5u
#define BASTOKEN_END         0xA6u
#define BASTOKEN_ENDCASE     0xA7u
#define BASTOKEN_ENDIF       0xA8u
#define BASTOKEN_ENDPROC     0xA9u
#define BASTOKEN_ENDWHILE    0xAAu
#define BASTOKEN_ENVELOPE    0xABu
#define BASTOKEN_ERROR       0xACu
#define BASTOKEN_FALSE       0xADu
#define BASTOKEN_FILL        0xAEu
#define BASTOKEN_FILLBY      0xAFu
#define BASTOKEN_FN          0xB0u
#define BASTOKEN_FOR         0xB1u
#define BASTOKEN_GCOL        0xB2u
#define BASTOKEN_GOSUB       0xB3u
#define BASTOKEN_GOTO        0xB4u
#define BASTOKEN_XIF         0xB5u
#define BASTOKEN_BLOCKIF     0xB6u
#define BASTOKEN_SINGLIF     0xB7u
#define BASTOKEN_INPUT       0xB8u
#define BASTOKEN_LET         0xB9u
#define BASTOKEN_LIBRARY     0xBAu
#define BASTOKEN_LINE        0xBBu
#define BASTOKEN_LOCAL       0xBCu
#define BASTOKEN_MODE        0xBDu
#define BASTOKEN_MOUSE       0xBEu
#define BASTOKEN_MOVE        0xBFu
#define BASTOKEN_EXIT        0xC0u
#define BASTOKEN_NEXT        0xC1u
#define BASTOKEN_NOT         0xC2u
#define BASTOKEN_OF          0xC3u
#define BASTOKEN_OFF         0xC4u
#define BASTOKEN_ON          0xC5u
#define BASTOKEN_ORIGIN      0xC6u
#define BASTOKEN_OSCLI       0xC7u
#define BASTOKEN_XOTHERWISE  0xC8u
#define BASTOKEN_OTHERWISE   0xC9u
#define BASTOKEN_OVERLAY     0xCAu
#define BASTOKEN_PLOT        0xCBu
#define BASTOKEN_POINT       0xCCu   /* POINT at a statement */
#define BASTOKEN_PRINT       0xCDu
#define BASTOKEN_PROC        0xCEu
#define BASTOKEN_QUIT        0xCFu
#define BASTOKEN_READ        0xD0u
#define BASTOKEN_RECTANGLE   0xD1u
#define BASTOKEN_REM         0xD2u
#define BASTOKEN_REPEAT      0xD3u
#define BASTOKEN_REPORT      0xD4u
#define BASTOKEN_RESTORE     0xD5u
#define BASTOKEN_RETURN      0xD6u
#define BASTOKEN_RUN         0xD7u
#define BASTOKEN_SOUND       0xD8u
#define BASTOKEN_STAR        0xD9u   /* '*' command */
#define BASTOKEN_STEP        0xDAu
#define BASTOKEN_STEREO      0xDBu
#define BASTOKEN_STOP        0xDCu
#define BASTOKEN_SWAP        0xDDu
#define BASTOKEN_SYS         0xDEu
#define BASTOKEN_TEMPO       0xDFu
#define BASTOKEN_THEN        0xE0u
#define BASTOKEN_TINT        0xE1u
#define BASTOKEN_TO          0xE2u
#define BASTOKEN_TRACE       0xE3u
#define BASTOKEN_TRUE        0xE4u
#define BASTOKEN_UNTIL       0xE5u
#define BASTOKEN_VDU         0xE6u
#define BASTOKEN_VOICE       0xE7u
#define BASTOKEN_VOICES      0xE8u
#define BASTOKEN_WAIT        0xE9u
#define BASTOKEN_XWHEN       0xEAu
#define BASTOKEN_WHEN        0xEBu
#define BASTOKEN_XWHILE      0xECu
#define BASTOKEN_WHILE       0xEDu
#define BASTOKEN_WIDTH       0xEEu

/* Unused tokens */

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
#define BASTOKEN_APPEND      0x01u
#define BASTOKEN_AUTO        0x02u
#define BASTOKEN_CRUNCH      0x03u
#define BASTOKEN_DELETE      0x04u
#define BASTOKEN_EDIT        0x05u
#define BASTOKEN_EDITO       0x06u
#define BASTOKEN_HELP        0x07u
#define BASTOKEN_INSTALL     0x08u
#define BASTOKEN_LIST        0x09u
#define BASTOKEN_LISTB       0x0Au
#define BASTOKEN_LISTIF      0x0Bu
#define BASTOKEN_LISTL       0x0Cu
#define BASTOKEN_LISTO       0x0Du
#define BASTOKEN_LISTW       0x0Eu
#define BASTOKEN_LOAD        0x0Fu
#define BASTOKEN_LVAR        0x10u
#define BASTOKEN_NEW         0x11u
#define BASTOKEN_OLD         0x12u
#define BASTOKEN_RENUMBER    0x13u
#define BASTOKEN_SAVE        0x14u
#define BASTOKEN_SAVEO       0x15u
#define BASTOKEN_TEXTLOAD    0x16u
#define BASTOKEN_TEXTSAVE    0x17u
#define BASTOKEN_TEXTSAVEO   0x18u
#define BASTOKEN_TWIN        0x19u
#define BASTOKEN_TWINO       0x1Au

/*
** Pseudo variables and functions that can appear on the left hand side
** of an expression. These are preceded by 0xFF in the tokenised code.
*/
#define BASTOKEN_HIMEM       0x01u
#define BASTOKEN_EXT         0x02u
#define BASTOKEN_FILEPATH    0x03u
#define BASTOKEN_LEFT        0x04u
#define BASTOKEN_LOMEM       0x05u
#define BASTOKEN_MID         0x06u
#define BASTOKEN_PAGE        0x07u
#define BASTOKEN_PTR         0x08u
#define BASTOKEN_RIGHT       0x09u
#define BASTOKEN_TIME        0x0Au

/*
** Functions.
** These are preceded with 0xFF in the tokenised code.
*/
#define BASTOKEN_ABS         0x10u
#define BASTOKEN_ACS         0x11u
#define BASTOKEN_ADVAL       0x12u
#define BASTOKEN_ARGC        0x13u
#define BASTOKEN_ARGVDOL     0x14u
#define BASTOKEN_ASC         0x15u
#define BASTOKEN_ASN         0x16u
#define BASTOKEN_ATN         0x17u
#define BASTOKEN_BEAT        0x18u
#define BASTOKEN_BGET        0x19u
#define BASTOKEN_CHR         0x1Au
#define BASTOKEN_COS         0x1Bu
#define BASTOKEN_COUNT       0x1Cu
#define BASTOKEN_DEG         0x1Du
#define BASTOKEN_EOF         0x1Eu
#define BASTOKEN_ERL         0x1Fu
#define BASTOKEN_ERR         0x20u
#define BASTOKEN_EVAL        0x21u
#define BASTOKEN_EXP         0x22u
#define BASTOKEN_GET         0x23u
#define BASTOKEN_GETDOL      0x24u
#define BASTOKEN_INKEY       0x25u
#define BASTOKEN_INKEYDOL    0x26u
#define BASTOKEN_INSTR       0x27u
#define BASTOKEN_INT         0x28u
#define BASTOKEN_LEN         0x29u
#define BASTOKEN_LISTOFN     0x2Au
#define BASTOKEN_LN          0x2Bu
#define BASTOKEN_LOG         0x2Cu
#define BASTOKEN_OPENIN      0x2Du
#define BASTOKEN_OPENOUT     0x2Eu
#define BASTOKEN_OPENUP      0x2Fu
#define BASTOKEN_PI          0x30u
#define BASTOKEN_POINTFN     0x31u   /* The function POINT( */
#define BASTOKEN_POS         0x32u
#define BASTOKEN_RAD         0x33u
#define BASTOKEN_REPORTDOL   0x34u   /* The function REPORT$ */
#define BASTOKEN_RETCODE     0x35u
#define BASTOKEN_RND         0x36u   /* The function RND (with no parameter) */
#define BASTOKEN_SGN         0x37u
#define BASTOKEN_SIN         0x38u
#define BASTOKEN_SQR         0x39u
#define BASTOKEN_STR         0x3Au
#define BASTOKEN_STRING      0x3Bu
#define BASTOKEN_SUM         0x3Cu
#define BASTOKEN_TAN         0x3Du
#define BASTOKEN_TEMPOFN     0x3Eu
#define BASTOKEN_USR         0x3Fu
#define BASTOKEN_VAL         0x40u
#define BASTOKEN_VERIFY      0x41u
#define BASTOKEN_VPOS        0x42u
#define BASTOKEN_SYSFN       0x43u   /* The function SYS( */
#define BASTOKEN_RNDPAR      0x44u   /* The function RND( */
#define BASTOKEN_XLATEDOL    0x45u   /* Must remain the last in the list */

/*
** Print functions preceded with 0xFE
** These can only appear in 'INPUT' and 'PRINT' statements
*/
#define BASTOKEN_SPC 0x01u
#define BASTOKEN_TAB 0x02u

extern byte thisline[];                 /* tokenised version of command line */

extern void tokenize(char *, byte [], boolean, boolean);
extern void expand(byte *, char *);
extern byte *skip_token(byte *);
extern byte *skip_name(byte *);
extern void set_dest(byte *, byte *);
extern void set_address(byte *, void *);
extern void save_lineno(byte *, int32);
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
#define GET_LINENUM(p) (*(p+1) | *(p+2)<<BYTESHIFT)
#define GET_SRCADDR(p) (p-(*(p+1)+(*(p+2)<<BYTESHIFT)))
#define AT_PROGEND(p) (*(p+OFFLINE+1)==ENDMARKER)
#define FIND_EXEC(p) (p+*(p+OFFEXEC)+(*(p+OFFEXEC+1)<<BYTESHIFT))

#define get_exec(p) (*(p+OFFEXEC) | *(p+OFFEXEC+1)<<BYTESHIFT)
#define PREVIOUS_TOKEN (tokenbase[next-1])

#endif
