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
**	This file contains the bulk of the Basic interpreter itself
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "target.h"
#include "basicdefs.h"
#include "tokens.h"
#include "commands.h"
#include "stack.h"
#include "heap.h"
#include "errors.h"
#include "editor.h"
#include "miscprocs.h"
#include "variables.h"
#include "evaluate.h"
#include "screen.h"
#include "fileio.h"
#include "strings.h"
#include "iostate.h"
#include "mainstate.h"
#include "assign.h"
#include "statement.h"

/* #define DEBUG */

/* 'ateol' says whether a token is an end-of-line (statement) token */

byte ateol[256] = {
  TRUE,  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* 00..07 (null) */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* 08..0F */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* 10..17 */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* 18..1F */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* 20..27 */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* 28..2F */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* 30..37 */
  FALSE, FALSE, TRUE,  FALSE, FALSE, FALSE, FALSE, FALSE,	/* 38..3F (':') */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* 40..47 */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* 48..4F */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* 50..57 */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* 58..5F */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* 60..67 */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* 68..6F */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* 70..77 */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* 78..7F */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* 80..87 */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* 88..8F */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* 90..97 */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE,	/* 98..9F (ELSE) */
  TRUE,  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* A0..A7 (ELSE) */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* A8..AF */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* B0..B7 */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* B8..BF */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* C0..C7 */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* C8..CF */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* D0..D7 */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* D8..DF */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* E0..E7 */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* E8..EF */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* F0..F7 */
  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,	/* F8..FF */
};

/*
** 'init_interpreter' is called to initialise the interpreter when it
** starts running
*/
void init_interpreter(void) {
  basicvars.current = NIL;
  init_stack();
  init_expressions();
  init_staticvars();
}


/*
** 'trace_line' prints out a line number when tracing program execution
*/
void trace_line(int32 lineno) {
  int32 len;
  len = sprintf(basicvars.stringwork, "[%d]", lineno);
  if (basicvars.tracehandle == 0)	/* Trace output goes to screen */
    emulate_vdustr(basicvars.stringwork, len);
  else {	/* Trace output goes to a file */
    fileio_bputstr(basicvars.tracehandle, basicvars.stringwork, len);
  }
#ifdef DEBUG
  if (basicvars.debug_flags.debug) fprintf(stderr, "Basic line trace - %s\n", basicvars.stringwork);
#endif
}

/*
** 'trace_proc' is used to trace a call to procedure or function 'name'.
** 'entering' is set to 'true' if 'name' is being entered or 'false'
** if leaving it
*/
void trace_proc(char *np, boolean entering) {
  int32 len;
  char *what = *CAST(np, byte *) == TOKEN_PROC ? "PROC" : "FN";
  np++;
  if (entering)	/* Entering procedure or function */
    len = sprintf(basicvars.stringwork, "==>%s%s ", what, np);
  else {	/* Leaving procedure or function */
    len = sprintf(basicvars.stringwork, "%s%s--> ", what, np);
  }
  if (basicvars.tracehandle == 0)	/* Trace output goes to screen */
    emulate_vdustr(basicvars.stringwork, len);
  else {	/* Trace output goes to a file */
    fileio_bputstr(basicvars.tracehandle, basicvars.stringwork, len);
  }
#ifdef DEBUG
  if (basicvars.debug_flags.debug) fprintf(stderr, "Basic PROC/FN call - %s\n", basicvars.stringwork);
#endif
}

/*
** 'trace_branch' traces a branch in the program flow, giving the
** line number at which the branch occured and the line number of
** the destination
*/
void trace_branch(byte *from, byte *to) {
  int32 len;
  byte *fromline, *toline;
  fromline = find_linestart(from);
  toline = find_linestart(to);
  if (fromline == NIL || toline == NIL) return;	/* Do not trace anything if at command line */
  len = sprintf(basicvars.stringwork, "[%d->%d]", get_lineno(fromline), get_lineno(toline));
  if (basicvars.tracehandle == 0)	/* Trace output goes to screen */
    emulate_vdustr(basicvars.stringwork, len);
  else {	/* Trace output goes to a file */
    fileio_bputstr(basicvars.tracehandle, basicvars.stringwork, len);
  }
#ifdef DEBUG
  if (basicvars.debug_flags.debug) fprintf(stderr, "Basic branch trace - %s\n", basicvars.stringwork);
#endif
}

/*
** 'bad_token' is called when an invalid token is found. This generally
** means that the Basic program is corrupt although it might also mean
** that the interpreter is broken
*/
void bad_token(void) {
  fprintf(stderr, "Bad token at %p, value=&%02x\n", basicvars.current, *basicvars.current);
  error(ERR_BROKEN, __LINE__, "statement");
}

/*
** 'bad_syntax' is called when a syntax error is discovered
*/
void bad_syntax(void) {
  error(ERR_SYNTAX);
}

/*
** flag_badline - Flag attempt to execute program that contains
** an error detected when tokenising it. The byte after the
** 'BADLINE' token contains the error number
*/
static void flag_badline(void) {
  basicvars.current++;
  error(*basicvars.current);
}

/*
** 'isateol' returns TRUE if the token at 'p' is an end-of-line
** token
*/
boolean isateol(byte *p) {
  return ateol[*p];
}

/*
** 'check_ateol' is called to ensure that a statement ends correctly
** at either the end of a line, at a ':' or the keyword 'ELSE'
*/
void check_ateol(void) {
  if (!ateol[*basicvars.current]) error(ERR_SYNTAX);
}

/*
** 'skip_colon' skips the ':' between statements
*/
static void skip_colon(void) {
  basicvars.current++;
}

/*
** 'end_run' tidies up once a program has finished running and branches
** back to the main command interpreter loop
*/
void end_run(void) {
  basicvars.runflags.running = FALSE;
  basicvars.escape = FALSE;		/* Clear ESCAPE state at end of run */
  basicvars.procstack = NIL;
  basicvars.gosubstack = NIL;
  basicvars.current = NIL;
  clear_error();
#ifdef DEBUG
  if (basicvars.debug_flags.debug) check_alloc();
  if (basicvars.debug_flags.stats) show_stringstats();
#endif
  if (basicvars.runflags.quitatend) exit_interpreter(EXIT_SUCCESS);	/* Exit from the interpreter once program has finished */
  longjmp(basicvars.restart, 1);	/* Restart at the command line */
}

static void next_line(void) {
  byte *lp;
  lp = basicvars.current+1;		/* Skip NUL and point at start of next line */
  if (AT_PROGEND(lp)) end_run();	/* Have reached end of program */
  if (basicvars.traces.lines) trace_line(get_lineno(lp));
  basicvars.current = FIND_EXEC(lp);	/* Find first executable token on line */
}

/*
** 'store_value' is called to save an integer value at the
** address given by 'lvalue'.
*/

void store_value(lvalue destination, int32 value) {
  switch (destination.typeinfo) {
  case VAR_INTWORD:
    *destination.address.intaddr = value;
    break;
  case VAR_FLOAT:
    *destination.address.floataddr = TOFLOAT(value);
    break;
  case VAR_INTBYTEPTR:
    check_write(destination.address.offset, sizeof(byte));
    basicvars.offbase[destination.address.offset] = value;
    break;
  case VAR_INTWORDPTR:
    store_integer(destination.address.offset, value);
    break;
  case VAR_FLOATPTR:
    store_float(destination.address.offset, TOFLOAT(value));
    break;
  default:
    error(ERR_VARNUM);
  }
}

#ifdef TARGET_RISCOS
/*
** 'store_stg_value' is called to save a string value at the
** address given by 'lvalue'. It is only required on RISCOS targets.
**
** The code assumes that pointers are 32 bits wide. This affects
** string variables in that the value returned by the SWI is
** assumed to be a pointer to a null-terminated string. This is
** passed back as a 32-bit integer. This problem cannot be avoided
** with the SYS statement, which was only designed for use on a
** 32-bit ARM processor
*/

void store_stg_value(lvalue destination, int32 value) {
  int32 length;
  char *cp;
  switch (destination.typeinfo) {
  case VAR_STRINGDOL:
    length = strlen(TOSTRING(value));
    if (length>MAXSTRING) error(ERR_STRINGLEN);
    free_string(*destination.address.straddr);
    cp = alloc_string(length);
    if (length>0) memmove(cp, TOSTRING(value), length);
    destination.address.straddr->stringlen = length;
    destination.address.straddr->stringaddr = cp;
    break;
  case VAR_DOLSTRPTR:
    length = strlen(TOSTRING(value));
    if (length>MAXSTRING) error(ERR_STRINGLEN);
    check_write(destination.address.offset, length+1);
    if (length>0) memmove(&basicvars.offbase[destination.address.offset], TOSTRING(value), length);
    basicvars.offbase[destination.address.offset+length] = CR;
    break;
  default:
    error(ERR_VARNUM);
  }
}
#endif

/*
** 'statements' is an important table. It controls the dispatch
** of the functions that handle the various Basic statement types
*/
static void (*statements[256])(void) = {
  next_line, exec_assignment, assign_staticvar,	assign_intvar,	/* 00.03 */
  assign_floatvar, assign_stringvar, exec_assignment, exec_assignment,	/* 04..07 */
  exec_assignment, exec_assignment, exec_assignment, exec_assignment, 	/* 08..0B */
  exec_xproc, exec_proc, bad_token, bad_token,			/* 0C..0F */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* 10..13 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* 14..17 */
  bad_syntax, bad_token, bad_token, bad_token,			/* 18..1B */
  bad_token, bad_token, bad_token, bad_token,			/* 1C..1F */
  bad_token, exec_assignment, bad_syntax, bad_syntax,		/* 20..23 */
  exec_assignment, bad_syntax, bad_syntax, bad_syntax,		/* 24..27 */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* 28..2B */
  bad_syntax, bad_syntax, bad_syntax, bad_syntax,		/* 2C..2F */
  bad_token, bad_token, bad_token, bad_token,			/* 30..33 */
  bad_token, bad_token, bad_token, bad_token,			/* 34..37 */
  bad_token, bad_token, skip_colon, bad_syntax,			/* 38..3B */
  bad_syntax, exec_fnreturn, bad_syntax, exec_assignment,	/* 3C..3F */
  bad_token, bad_token, bad_token, bad_token,			/* 40..43 */
  bad_token, bad_token, bad_token, bad_token,			/* 44..47 */
  bad_token, bad_token, bad_token, bad_token,			/* 48..4B */
  bad_token, bad_token, bad_token, bad_token,			/* 4C..4F */
  bad_token, bad_token, bad_token, bad_token,			/* 50..53 */
  bad_token, bad_token, bad_token, bad_token,			/* 54..57 */
  bad_token, bad_token, bad_token, exec_assembler,		/* 58..5B */
  bad_syntax, exec_asmend, bad_syntax, bad_token,		/* 5C..5F */
  bad_token, bad_token, bad_token, bad_token,			/* 60..63 */
  bad_token, bad_token, bad_token, bad_token,			/* 64..67 */
  bad_token, bad_token, bad_token, bad_token,			/* 68..6B */
  bad_token, bad_token, bad_token, bad_token,			/* 6C..6F */
  bad_token, bad_token, bad_token, bad_token,			/* 70..73 */
  bad_token, bad_token, bad_token, bad_token,			/* 74..77 */
  bad_token, bad_token, bad_token, bad_syntax,			/* 78..7B */
  exec_assignment, bad_syntax, bad_syntax, bad_token,		/* 7C..7F */
  bad_syntax, bad_syntax, exec_oscmd, bad_syntax,		/* 80..83 */
  bad_syntax, bad_syntax, exec_oscmd, bad_syntax,		/* 84..87 */
  bad_syntax, bad_syntax, exec_oscmd, bad_syntax,		/* 88..8B */
  bad_syntax, exec_beats, exec_bput, exec_call,			/* 8C..8F */
  exec_xcase, exec_case, exec_chain, exec_circle,		/* 90..93 */
  exec_clg, exec_clear, exec_close, exec_cls,			/* 94..97 */
  exec_colour, exec_data, exec_def, exec_dim,			/* 98..9B */
  exec_draw, exec_drawby, exec_ellipse, exec_xelse,		/* 9C..9F */
  exec_elsewhen, exec_xlhelse, exec_elsewhen, exec_end,		/* A0..A3 */
  exec_endifcase, exec_endifcase, exec_endproc, exec_endwhile,	/* A4..A7 */
  exec_envelope, exec_error, bad_syntax, exec_fill,		/* A8..AB */
  exec_fillby, bad_token, exec_for, exec_gcol,			/* AC..AF */
  exec_gosub, exec_goto, exec_xif, exec_blockif,		/* B0..B3 */
  exec_singlif, exec_input, exec_let, exec_library,		/* B4..B7 */
  exec_line, exec_local, exec_mode, exec_mouse,			/* B8..BB */
  exec_move, exec_moveby, exec_next, bad_syntax,		/* BC..BF */
  bad_syntax, exec_off, exec_on, exec_origin,			/* C0..C3 */
  exec_oscli, exec_xwhen, exec_elsewhen, exec_overlay,		/* C4..C7 */
  exec_plot, exec_point, exec_pointby, exec_pointto,		/* C8..CB */
  exec_print, exec_proc, exec_quit, exec_read,			/* CC..CF */
  exec_rectangle, bad_token, exec_repeat, exec_report,		/* D0..D3 */
  exec_restore, exec_return, exec_run, exec_sound,		/* D4..D7 */
  exec_oscmd, bad_syntax, exec_stereo, exec_stop,		/* D8..DB */
  exec_swap, exec_sys, exec_tempo, bad_syntax,			/* DC..DF */
  exec_tint, bad_syntax, exec_trace, bad_syntax,		/* E0..E3 */
  exec_until, exec_vdu, exec_voice, exec_voices, 		/* E4..E7 */
  exec_wait, exec_xwhen, exec_elsewhen, exec_while, 		/* E8..EB */
  exec_while, exec_width, bad_token, bad_token,			/* EC..EF */
  bad_token, bad_token, bad_token, bad_token,			/* F0..F3 */
  bad_token, bad_token, bad_token, bad_token,			/* F4..F7 */
  bad_token, bad_token, bad_token, bad_token,			/* F8..FB */
  exec_command, flag_badline, bad_syntax, assign_pseudovar	/* FC..FF */
};

/*
** 'exec_fnstatements' is called to run the statements in a function.
** On entry 'lp' points at the start of the tokens to be interpreted.
** As this code will be called from the expression code and has to
** return there once an '='<result> statement has been interpreted
*/
void exec_fnstatements(byte *lp) {
  byte token;
  basicvars.current = lp;
  do {	/* This is the main statement execution loop */
    token = *basicvars.current;
    (*statements[token])();	/* Dispatch a statement */
  } while (token != '=');
}

/*
** 'exec_statements' deals with the statements in either a procedure
** or the main program
*/
void exec_statements(byte *lp) {
  basicvars.current = lp;
  do {	/* This is the main statement execution loop */
    (*statements[*basicvars.current])();	/* Dispatch a statement */
  } while (TRUE);
}

/*
** 'run_program' runs a program. On entry, 'lp' points at the start
** of the line from which to start program execution. If it is 'nil'
** then execution starts at the beginning of the program.
**
** Control returns here when an error occurs and the program has used
** an 'ON ERROR' block to trap the error. 'ON ERROR' (as opposed to
** 'ON ERROR LOCAL') returns the Basic stack and all other control
** structures to their initial state, so as far as the interpreter
** is concerned it is as if the program has started running afresh at
** the point of the 'ON ERROR' statement. An error trapped when an
** 'ON ERROR LOCAL' handler is in force effectively causes a branch
** to the code after the 'ON ERROR LOCAL' whilst leaving everything
** else intact. The handling of 'ON ERROR LOCAL' is much more complex.
** See the comments in 'errors.c' for the grubby details. All of the
** tidying up for this is carried out in errors.c too.
*/
void run_program(byte *lp) {
  if (basicvars.misc_flags.badprogram) error(ERR_BADPROG);
  clear_error();
  if (basicvars.runflags.has_offsets) clear_varptrs();
  if (basicvars.runflags.has_variables) clear_varlists();
  clear_strings();
  clear_heap();
  clear_stack();
  init_expressions();
  if (lp == NIL) lp = basicvars.start;	/* Check starting position in program */
  basicvars.lastsearch = basicvars.start;
  basicvars.curcount = 0;
  basicvars.printcount = 0;
  basicvars.datacur = NIL;
  basicvars.runflags.outofdata = FALSE;
  basicvars.runflags.running = TRUE;	/* Say that ' RUN' command has been issued */
  if (setjmp(basicvars.error_restart) == 0) {	/* Mark restart point */
    basicvars.local_restart = &basicvars.error_restart;
    exec_statements(FIND_EXEC(lp));	/* Start normal run at first token */
  }
  else {
/*
** Restart here after an error has been trapped by 'ON ERROR' or
** by 'ON ERROR LOCAL' when the error did not occur in something
** called from a function, that is, the call chain contains only
** procedures
*/
    reset_opstack();	   	 /* Reset the operator stack to a known state code */
    exec_statements(basicvars.error_handler.current);
  }
}

/*
** 'exec_thisline' is called to interpret the statement in 'thisline'.
** If the length of the line is zero, that is, nothing was entered on
** the line typed in, the function exits as there is nothing to do.
** Note the slightly hacky way it modifies the line so that the
** interpreter know when to stop. The problem is that the code would
** otherwise interpret the 'null' at the end of a line as a token
** telling it to skip to the next line, except that this is the only
** line. I'm probably missing something pretty obvious here, but
** adding an 'END' token seemed to be the cleanest way to bring
** matters to a satisfactory conclusion.
*/
void exec_thisline(void) {
  int32 linelen;
  linelen = get_linelen(thisline);
  if (linelen == 0) return;		/* There is nothing to do */
  mark_end(&thisline[linelen]);		/* Mark end of command line */
  basicvars.lastsearch = basicvars.start;
  basicvars.curcount = 0;
  basicvars.datacur = NIL;
  basicvars.runflags.outofdata = FALSE;
  clear_error();
  reset_opstack();
  exec_statements(FIND_EXEC(thisline));
}
