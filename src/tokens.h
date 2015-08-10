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
**	This file defines the token set
*/

#ifndef __token_h
#define __token_h

#include "common.h"

#define NOLINENO 0xFFFF			/* Marks line as having no line number */

#define OFFLINE 	0u		/* Offset of line number in tokenised line */
#define OFFLENGTH 	2u		/* Offset of line length in tokenised line */
#define OFFEXEC		4u		/* Offset of first executable token */
#define OFFSOURCE 	6u		/* Offset of first byte of source */

#define HASLINE TRUE
#define NOLINE FALSE

#define ENDMARKER (ENDLINENO>>BYTESHIFT) /* Used when checking if the end of program has been reached */

/*
** The token set. 'X' versions of tokens are used when addresses have not
** been filled in. They must precede the non-X versions as the code relies
** on this order
*/

#define LOW_HIGHEST	0x1Fu		/* Highest token value in 0..0x1F block */
#define TOKEN_LOWEST	TOKEN_AND	/* Lowest single byte token value */
#define TOKEN_HIGHEST	TOKEN_WIDTH	/* Highest single byte token value */
#define COMMAND_LOWEST	TOKEN_APPEND	/* Lowest direct command token value */
#define COMMAND_HIGHEST TOKEN_TWINO	/* Highest direct command token value */

#define TYPE_ONEBYTE	0u

#define TYPE_COMMAND	0xFCu		/* Basic commands */
#define TYPE_PRINTFN	0xFEu		/* Special I/O functions SPC() and TAB() */
#define TYPE_FUNCTION	0xFFu		/* Other functions */

#define BADLINE_MARK	0xFDu		/* Marks a bad line */

#define TOKEN_NOWT	0u

/* Variables and constants */

#define TOKEN_EOL	0u		/* End of line */

#define TOKEN_XVAR	0x01u		/* Reference to variable in source code */
#define TOKEN_STATICVAR 0x02u		/* Simple reference to a static variable */
#define TOKEN_INTVAR	0x03u		/* Simple reference to an integer variable */
#define TOKEN_FLOATVAR	0x04u		/* Simple reference to a floating point variable */
#define TOKEN_STRINGVAR	0x05u		/* Simple reference to a string variable */
#define TOKEN_ARRAYVAR	0x06u		/* Array or array followed by an indirection operator */
#define TOKEN_ARRAYREF	0x07u		/* Reference to whole array */
#define TOKEN_ARRAYINDVAR 0x08u		/* Array element followed by indirection operator */
#define TOKEN_INTINDVAR	0x09u		/* Integer variable followed by indirection operator */
#define TOKEN_FLOATINDVAR 0x0Au		/* Floating point variable followed by indirection operator */
#define TOKEN_STATINDVAR 0x0Bu		/* Static variable followed by indirection operator */

#define TOKEN_XFNPROCALL 0x0Cu		/* Reference to unknown PROC or FN */
#define TOKEN_FNPROCALL	0x0Du		/* Reference to known PROC or FN */

#define TOKEN_INTZERO	0x10u		/* Integer 0 */
#define TOKEN_INTONE	0x11u		/* Integer 1 */
#define TOKEN_SMALLINT	0x12u		/* Small integer constant in range 1..256 */
#define TOKEN_INTCON	0x13u		/* Integer constant */
#define TOKEN_FLOATZERO	0x14u		/* Floating point 0.0 */
#define TOKEN_FLOATONE	0x15u		/* Floating point 1.0 */
#define TOKEN_FLOATCON	0x16u		/* Floating point constant */
#define TOKEN_STRINGCON	0x17u		/* Ordinary string constant */
#define TOKEN_QSTRINGCON 0x18u		/* String constant with a '"' in it */

#define TOKEN_XLINENUM	0x1Eu		/* Unresolved line number reference */
#define TOKEN_LINENUM   0x1Fu		/* Resolved line number reference */

/* Unused tokens */

#define UNUSED_0E	0x0Eu
#define UNUSED_0F	0x0Fu
#define UNUSED_19	0x19u
#define UNUSED_1A	0x1Au
#define UNUSED_1B	0x1Bu
#define UNUSED_1C	0x1Cu
#define UNUSED_1D	0x1Du

/* Operators */

#define TOKEN_AND	0x80u
#define TOKEN_ASR	0x81u	/* >> */
#define TOKEN_DIV	0x82u
#define TOKEN_EOR	0x83u
#define TOKEN_GE	0x84u	/* >= */
#define TOKEN_LE	0x85u	/* <= */
#define TOKEN_LSL	0x86u	/* << */
#define TOKEN_LSR	0x87u	/* >>> */
#define TOKEN_MINUSAB	0x88u	/* -= */
#define TOKEN_MOD	0x89u
#define TOKEN_NE	0x8Au	/* <> */
#define TOKEN_OR	0x8Bu
#define TOKEN_PLUSAB	0x8Cu	/* += */

/* Statements */

#define TOKEN_BEATS	0x8Du
#define TOKEN_BPUT	0x8Eu
#define TOKEN_CALL	0x8Fu
#define TOKEN_XCASE	0x90u
#define TOKEN_CASE	0x91u
#define TOKEN_CHAIN	0x92u
#define TOKEN_CIRCLE	0x93u
#define TOKEN_CLG	0x94u
#define TOKEN_CLEAR	0x95u
#define TOKEN_CLOSE	0x96u
#define TOKEN_CLS	0x97u
#define TOKEN_COLOUR	0x98u
#define TOKEN_DATA	0x99u
#define TOKEN_DEF	0x9Au
#define TOKEN_DIM	0x9Bu
#define TOKEN_DRAW	0x9Cu
#define TOKEN_DRAWBY	0x9Du	/* 'DRAWBY' has to follow 'DRAW */
#define TOKEN_ELLIPSE	0x9Eu
#define TOKEN_XELSE	0x9Fu
#define TOKEN_ELSE	0xA0u
#define TOKEN_XLHELSE	0xA1u
#define TOKEN_LHELSE	0xA2u
#define TOKEN_END	0xA3u
#define TOKEN_ENDCASE	0xA4u
#define TOKEN_ENDIF	0xA5u
#define TOKEN_ENDPROC	0xA6u
#define TOKEN_ENDWHILE	0xA7u
#define TOKEN_ENVELOPE	0xA8u
#define TOKEN_ERROR	0xA9u
#define TOKEN_FALSE	0xAAu
#define TOKEN_FILL	0xABu
#define TOKEN_FILLBY	0xACu
#define TOKEN_FN	0xADu
#define TOKEN_FOR	0xAEu
#define TOKEN_GCOL	0xAFu
#define TOKEN_GOSUB	0xB0u
#define TOKEN_GOTO	0xB1u
#define TOKEN_XIF	0xB2u
#define TOKEN_BLOCKIF	0xB3u
#define TOKEN_SINGLIF	0xB4u
#define TOKEN_INPUT	0xB5u
#define TOKEN_LET	0xB6u
#define TOKEN_LIBRARY	0xB7u
#define TOKEN_LINE	0xB8u
#define TOKEN_LOCAL	0xB9u
#define TOKEN_MODE	0xBAu
#define TOKEN_MOUSE	0xBBu
#define TOKEN_MOVE	0xBCu
#define TOKEN_MOVEBY	0xBDu	/* 'MOVEBY' has to follow 'MOVE' */
#define TOKEN_NEXT	0xBEu
#define TOKEN_NOT	0xBFu
#define TOKEN_OF	0xC0u
#define TOKEN_OFF	0xC1u
#define TOKEN_ON	0xC2u
#define TOKEN_ORIGIN	0xC3u
#define TOKEN_OSCLI	0xC4u
#define TOKEN_XOTHERWISE 0xC5u
#define TOKEN_OTHERWISE	0xC6u
#define TOKEN_OVERLAY	0xC7u
#define TOKEN_PLOT	0xC8u
#define TOKEN_POINT	0xC9u	/* POINT at a statement */
#define TOKEN_POINTBY	0xCAu	/* 'POINTBY' has to follow 'POINT' */
#define TOKEN_POINTTO	0xCBu
#define TOKEN_PRINT	0xCCu
#define TOKEN_PROC	0xCDu
#define TOKEN_QUIT	0xCEu
#define TOKEN_READ	0xCFu
#define TOKEN_RECTANGLE	0xD0u
#define TOKEN_REM	0xD1u
#define TOKEN_REPEAT	0xD2u
#define TOKEN_REPORT	0xD3u
#define TOKEN_RESTORE	0xD4u
#define TOKEN_RETURN	0xD5u
#define TOKEN_RUN	0xD6u
#define TOKEN_SOUND	0xD7u
#define TOKEN_STAR	0xD8u	/* '*' command */
#define TOKEN_STEP	0xD9u
#define TOKEN_STEREO	0xDAu
#define TOKEN_STOP	0xDBu
#define TOKEN_SWAP	0xDCu
#define TOKEN_SYS	0xDDu
#define TOKEN_TEMPO	0xDEu
#define TOKEN_THEN	0xDFu
#define TOKEN_TINT	0xE0u
#define TOKEN_TO	0xE1u
#define TOKEN_TRACE	0xE2u
#define TOKEN_TRUE	0xE3u
#define TOKEN_UNTIL	0xE4u
#define TOKEN_VDU	0xE5u
#define TOKEN_VOICE	0xE6u
#define TOKEN_VOICES	0xE7u
#define TOKEN_WAIT	0xE8u
#define TOKEN_XWHEN	0xE9u
#define TOKEN_WHEN	0xEAu
#define TOKEN_XWHILE	0xEBu
#define TOKEN_WHILE	0xECu
#define TOKEN_WIDTH	0xEDu

/* Unused tokens */

#define UNUSED_EE	0xEEu
#define UNUSED_EF	0xEFu
#define UNUSED_F0	0xF0u
#define UNUSED_F1	0xF1u
#define UNUSED_F2	0xF2u
#define UNUSED_F3	0xF3u
#define UNUSED_F4	0xF4u
#define UNUSED_F5	0xF5u
#define UNUSED_F6	0xF6u
#define UNUSED_F7	0xF7u
#define UNUSED_F8	0xF8u
#define UNUSED_F9	0xF9u
#define UNUSED_FA	0xFAu
#define UNUSED_FB	0xFBu

#define UNUSED_FD	0xFDu

/*
** Direct commands
** These are statement types that are generally not allowed to appear
** in a program. This interpreter allows ones that are not going to
** hurt the program such as 'list' to be used.
*/
#define TOKEN_APPEND	0x01u
#define TOKEN_AUTO	0x02u
#define TOKEN_CRUNCH	0x03u
#define TOKEN_DELETE	0x04u
#define TOKEN_EDIT	0x05u
#define TOKEN_EDITO	0x06u
#define TOKEN_HELP	0x07u
#define TOKEN_INSTALL	0x08u
#define TOKEN_LIST	0x09u
#define TOKEN_LISTB     0x0Au
#define TOKEN_LISTIF	0x0Bu
#define TOKEN_LISTL     0x0Cu
#define TOKEN_LISTO	0x0Du
#define TOKEN_LISTW     0x0Eu
#define TOKEN_LOAD	0x0Fu
#define TOKEN_LVAR	0x10u
#define TOKEN_NEW	0x11u
#define TOKEN_OLD	0x12u
#define TOKEN_RENUMBER	0x13u
#define TOKEN_SAVE	0x14u
#define TOKEN_SAVEO	0x15u
#define TOKEN_TEXTLOAD	0x16u
#define TOKEN_TEXTSAVE	0x17u
#define TOKEN_TEXTSAVEO	0x18u
#define TOKEN_TWIN	0x19u
#define TOKEN_TWINO	0x1Au

/*
** Pseudo variables and functions that can appear on the left hand side
** of an expression. These are preceded by 0xFF in the tokenised code.
*/
#define TOKEN_HIMEM	0x01u
#define TOKEN_EXT	0x02u
#define TOKEN_FILEPATH	0x03u
#define TOKEN_LEFT	0x04u
#define TOKEN_LOMEM	0x05u
#define TOKEN_MID	0x06u
#define TOKEN_PAGE	0x07u
#define TOKEN_PTR	0x08u
#define TOKEN_RIGHT	0x09u
#define TOKEN_TIME	0x0Au
#define TOKEN_TIMEDOL	0x0Bu

/*
** Functions.
** These are preceded with 0xFF in the tokenised code.
*/
#define TOKEN_ABS	0x10u
#define TOKEN_ACS	0x11u
#define TOKEN_ADVAL	0x12u
#define TOKEN_ARGC	0x13u
#define TOKEN_ARGVDOL	0x14u
#define TOKEN_ASC	0x15u
#define TOKEN_ASN	0x16u
#define TOKEN_ATN	0x17u
#define TOKEN_BEAT	0x18u
#define TOKEN_BGET	0x19u
#define TOKEN_CHR	0x1Au
#define TOKEN_COS	0x1Bu
#define TOKEN_COUNT	0x1Cu
#define TOKEN_DEG	0x1Du
#define TOKEN_EOF	0x1Eu
#define TOKEN_ERL	0x1Fu
#define TOKEN_ERR	0x20u
#define TOKEN_EVAL	0x21u
#define TOKEN_EXP	0x22u
#define TOKEN_GET	0x23u
#define TOKEN_GETDOL	0x24u
#define TOKEN_INKEY	0x25u
#define TOKEN_INKEYDOL	0x26u
#define TOKEN_INSTR	0x27u
#define TOKEN_INT	0x28u
#define TOKEN_LEN	0x29u
#define TOKEN_LISTOFN	0x2Au
#define TOKEN_LN	0x2Bu
#define TOKEN_LOG	0x2Cu
#define TOKEN_OPENIN	0x2Du
#define TOKEN_OPENOUT	0x2Eu
#define TOKEN_OPENUP	0x2Fu
#define TOKEN_PI	0x30u
#define TOKEN_POINTFN	0x31u	/* The function POINT( */
#define TOKEN_POS	0x32u
#define TOKEN_RAD	0x33u
#define TOKEN_REPORTDOL	0x34u	/* The function REPORT$ */
#define TOKEN_RETCODE	0x35u
#define TOKEN_RND	0x36u
#define TOKEN_SGN	0x37u
#define TOKEN_SIN	0x38u
#define TOKEN_SQR	0x39u
#define TOKEN_STR	0x3Au
#define TOKEN_STRING	0x3Bu
#define TOKEN_SUM	0x3Cu
#define TOKEN_TAN	0x3Du
#define TOKEN_TEMPOFN	0x3Eu
#define TOKEN_USR	0x3Fu
#define TOKEN_VAL	0x40u
#define TOKEN_VERIFY	0x41u
#define TOKEN_VPOS	0x42u
#define TOKEN_XLATEDOL	0x43u

/*
** Print functions preceded with 0xFE
** These can only appear in 'INPUT' and 'PRINT' statements
*/
#define TOKEN_SPC	0x01u
#define TOKEN_TAB	0x02u

extern byte thisline[];			/* tokenised version of command line */

extern void tokenize(char *, byte [], boolean);
extern void expand(byte *, char *);
extern byte *skip_token(byte *);
extern byte *skip_name(byte *);
extern void set_dest(byte *, byte *);
extern void set_address(byte *, void *);
extern byte *get_srcaddr(byte *);
extern void save_lineno(byte *, int32);
extern int32 get_lineno(byte *);	/* Returns line number at start of line */
extern int32 get_linelen(byte *);
extern int32 get_linenum(byte *);	/* Returns line number after 'linenum' token */
extern int32 get_intvalue(byte *);
extern byte *get_address(byte *);
extern float64 get_fpvalue(byte *);
extern void clear_varptrs(void);
extern void clear_branches(byte *);
extern void clear_linerefs(byte *);
extern boolean isvalid(byte *);
extern void reset_indent(void);
extern void resolve_linenums(byte *);
extern void reset_linenums(byte *);
extern int32 reformat(byte *, byte *);
extern boolean isempty(byte []);

#define GET_INTVALUE(p) (*p | (*(p+1)<<8) | (*(p+2)<<16) | (*(p+3)<<24))
#define GET_ADDRESS(p, type) (CAST(basicvars.workspace+(*(p+1) | (*(p+2)<<8) | (*(p+3)<<16) | (*(p+4)<<24)), type))
#define GET_SIZE(p) (*(p) | (*(p+1)<<BYTESHIFT))
#define GET_DEST(p) (p+(*p | (*(p+1)<<BYTESHIFT)))
#define GET_LINELEN(p) (*(p+OFFLENGTH) | (*(p+OFFLENGTH+1)<<BYTESHIFT))
#define GET_LINENO(p) (*(p+OFFLINE) | (*(p+OFFLINE+1)<<BYTESHIFT))
#define GET_SRCADDR(p) (p-(*(p+1)+(*(p+2)<<BYTESHIFT))
#define AT_PROGEND(p) (*(p+OFFLINE+1)==ENDMARKER)
#define FIND_EXEC(p) (p+*(p+OFFEXEC)+(*(p+OFFEXEC+1)<<BYTESHIFT))

#endif
