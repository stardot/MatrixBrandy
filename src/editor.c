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
**	This module contains the functions used to edit a program
**	as well as ones to read and write programs and libraries
**
** 05-Apr-2014 JGH: Can load Russell format BASIC programs.
**
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "common.h"
#include "target.h"
#include "basicdefs.h"
#include "errors.h"
#include "variables.h"
#include "heap.h"
#include "tokens.h"
#include "strings.h"
#include "miscprocs.h"
#include "stack.h"
#include "fileio.h"

#ifdef HAVE_ZLIB_H
# include <zlib.h>
#endif

#define MARKERSIZE 4
#define ENDMARKSIZE 8		/* Size of the sentinel value at the end of the program */

#define ACORN_ENDMARK 0xffu	/* Marker denoting end of Acorn Basic file */

static byte *last_added;	/* Address of last line added to program */
static boolean needsnumbers;	/* TRUE if a program need to be renumbered */

#ifdef BRANDYAPP
extern const char _binary_app_start;
extern const char _binary_app_end;
extern const char _binary_app_size;
static unsigned long int blockptr;
#endif

typedef enum {TEXTFILE, BBCFILE, Z80FILE} filetype;

/*
** The layout of a program in memory is as follows:
**
**	<start marker>
**	<lines>
**	<end marker>
**	<heap>
**
** <start marker> is the value 0xD7C1C7C5. This is used to decide whether the
** interpreter has been presented with a tokenised program or not. 'page'
** points at the 0xD7C1C7C5. Note that this value can be used to check if
** a Basic program is running under Brandy (by examining the contents of
** !PAGE, which always points at this value).
** <lines> is zero or more lines of Basic. 'start' points at the first line
** of the program.
** <end marker> denotes the end of the program. It is a line that contains
** a hidden 'END' token with the line number set to 65280 (0xFF00). 'top'
** points at the start of this hidden line.
** <heap> is the Basic heap used for variables, strings, arrays and so forth.
** 'lomem' points at the start of the heap and 'vartop' at the byte after it.
**
** The format of each line of Basic is:
**
**	<line number>
**	<length>
**	<tokenised source>
**	<tokenised executable line>
**	<NUL>
**
** <line number> and <length> are both two bytes long. Valid line numbers
** are in the range 0 to 65279 (0xFEFF). The length of a line can be up to
** 65535 bytes but internally it is limited to 1024 bytes.
** <tokenised source> is a copy of the original line with keywords replaced
** by tokens. <tokenised executable line> is a second version of the line
** with variables replaced with pointers to symbol table entries and so
** forth. This forms the executable part of each line.
** <NUL> is the 'C' null character, used to mark the end of the line. It
** is treated as a 'skip to next line' token when seen by the interpreter
*/

/*
** STARTMARK is a four byte value stored at the start of the Basic
** workspace. This value is fixed in both contents and size and MUST NOT
** be changed as it can be used to identify programs running under
** Brandy (by checking the contents of !PAGE).
*/
#define STARTMARKSIZE 4
static byte startmark [STARTMARKSIZE] = {0xC5, 0xC7, 0xC1, 0xD7};

/*
** 'mark_end' creates the pseudo line that marks the end of the
** program or the command line. This is a line with a line number of
** &FF00 which contains a hidden 'end' token. The purpose of this is
** to mark the end of the statements in the command line to be dealt
** with or to allow programs that drop off the end of their code to
** end gracefully
*/
static byte endline [8] = {0, 0, 8, 0, 6, 0, TOKEN_END, NUL};

void mark_end(byte *p) {
  memcpy(p, endline, 8);	/* Place an 'END' token at the end of the program */
  save_lineno(p, ENDLINENO);
}

/*
** 'preserve' is called when 'NEW' is issued to save a copy of the start
** of the program in memory in case 'OLD' is later used.
*/
static void preserve(void) {
  int n;
  for (n=0; n<PRESERVED; n++) basicvars.savedstart[n] = basicvars.start[n];
  basicvars.misc_flags.validsaved = TRUE;
}

/*
** 'reinstate' restores the start of a program in memory to the values
** saved when the 'NEW' command was last used.
*/
static void reinstate(void) {
  int n;
  for (n=0; n<PRESERVED; n++) basicvars.start[n] = basicvars.savedstart[n];
}

/*
** 'clear_program' is called when a 'NEW' command is issued to clear the old
** program from memory. The start of an existing program is preserved in
** 'oldstart' in case 'OLD' is used so that the old program can be restored
** to health
*/
void clear_program(void) {
  clear_varlists();
  clear_strings();
  clear_heap();
  basicvars.start = basicvars.top = basicvars.page+MARKERSIZE;
  memmove(basicvars.page, startmark, STARTMARKSIZE);
  preserve();	/* Preserve the start of program in memory (if any) */
  mark_end(basicvars.top);
  basicvars.lomem = basicvars.vartop = basicvars.top+ENDMARKSIZE;
  basicvars.stacklimit.bytesp = basicvars.top+STACKBUFFER;
  basicvars.stacktop.bytesp = basicvars.himem;
  basicvars.lastsearch = basicvars.start;
  basicvars.procstack = NIL;
  basicvars.liblist = NIL;
  basicvars.error_line = 0;
  basicvars.error_number = 0;
  basicvars.error_handler.current = NIL;
  basicvars.escape = FALSE;
  basicvars.misc_flags.badprogram = FALSE;
  basicvars.runflags.running = FALSE;
  basicvars.runflags.has_offsets = FALSE;
  basicvars.runflags.has_variables = FALSE;
  basicvars.runflags.closefiles = TRUE;
  basicvars.runflags.make_array = FALSE;
  basicvars.tracehandle = 0;
  basicvars.traces.lines = FALSE;
  basicvars.traces.pause = FALSE;
  basicvars.traces.procs = FALSE;
  basicvars.traces.branches = FALSE;
  basicvars.traces.backtrace = TRUE;
  basicvars.staticvars[ATPERCENT].varentry.varinteger = STDFORMAT;
  basicvars.curcount = 0;
  basicvars.printcount = 0;
  basicvars.printwidth = DEFWIDTH;
  basicvars.program[0] = NUL;
  basicvars.linecount = 0;
  last_added = NIL;
  init_stack();
}

/*
** 'adjust_heaplimits' is called to ensure that the pointers for the
** Basic heap such as 'lomem' are set to good values when a program
** is being edited
*/
static void adjust_heaplimits(void) {
  basicvars.lomem = basicvars.vartop = alignaddr(basicvars.top+ENDMARKSIZE);
  basicvars.stacklimit.bytesp = basicvars.vartop+STACKBUFFER;
}

/*
** 'isvalidprog' checks the program in memory to make sure that it is okay.
** It returns 'true' if the program is okay, otherwise it returns 'false'
*/
static boolean isvalidprog(void) {
  int32 lastline;
  byte *p;
  boolean isnotfirst;
  lastline = 0;
  isnotfirst = FALSE;	/* So that line number 0 is not flagged as an error */
  p = basicvars.start;
  while (!AT_PROGEND(p)) {
    if (!isvalid(p) || (isnotfirst && get_lineno(p) <= lastline)) return FALSE;
    lastline = get_lineno(p);
    isnotfirst = TRUE;
    p+=get_linelen(p);
  }
  return AT_PROGEND(p);
}

/*
** 'recover_program' is called to verify that the program at 'page'
** is legal. It resets the various program pointers if it is, or
** flags the program as invalid if not (after resetting the various
** pointer to 'safe' values)
*/
void recover_program(void) {
  byte *bp;
  if (basicvars.misc_flags.validsaved) {	/* Only check if the command 'new' has been used */
    reinstate();		/* Restore start of program */
    bp = basicvars.start;
    basicvars.misc_flags.validsaved = isvalidprog();
  }
  if (basicvars.misc_flags.validsaved) {	/* Old program is okay */
    while (!AT_PROGEND(bp)) bp+=get_linelen(bp);	/* Find end */
    basicvars.top = bp;
    adjust_heaplimits();
  }
  else {	/* Program is not valid - Ensure everything is set to safe values */
    clear_varlists();
    clear_strings();
    clear_heap();
    basicvars.misc_flags.badprogram = TRUE;
    save_lineno(basicvars.start, ENDLINENO);
    basicvars.current = basicvars.datacur = basicvars.top = basicvars.page+MARKERSIZE;
    adjust_heaplimits();
    error(ERR_BADPROG);
  }
}

/*
** 'clear_refs' is called to restore all the line number and case table tokens
** to their 'no address' versions. This is needed when a program is edited.
** The code also clears such tokens in installed libraries as any case tables
** built for statements in the libraries will have been put on the Basic heap.
*/
static void clear_refs(void) {
  byte *bp;
  library *lp;
  if (basicvars.runflags.has_variables) {
    clear_varlists();
    clear_heap();
    clear_strings();
  }
  if (basicvars.runflags.has_offsets) {
    bp = basicvars.start;
    while (!AT_PROGEND(bp)) {
      clear_linerefs(bp);
      bp+=get_linelen(bp);
    }
    lp = basicvars.installist;	/* Now clear the pointers in any installed libraries */
    while (lp!=NIL) {
      bp = lp->libstart;
      while (!AT_PROGEND(bp)) {
        clear_linerefs(bp);
        bp = bp+GET_LINELEN(bp);
      }
      lp = lp->libflink;
    }
  }
  basicvars.liblist = NIL;
  basicvars.runflags.has_offsets = FALSE;
  basicvars.runflags.has_variables = FALSE;
}


/*
** 'insert_line' adds the line in 'line' to the Basic program
** if new or replaces it if it already exists.
*/
static void insert_line(byte *line) {
  int32 lendiff, newline, newlength;
  byte *bp, *prev;
  newline = get_lineno(line);
  newlength = get_linelen(line);
  if (last_added!=NIL && newline>=get_lineno(last_added))
    bp = last_added;
  else {
    bp = basicvars.start;
  }
  prev = NIL;
  while (newline>=get_lineno(bp)) {	/* Work out where line goes */
    prev = bp;
    bp+=get_linelen(bp);
  }
  if (prev!=NIL && newline==get_lineno(prev)) {	/* Replacing a line */
    lendiff = newlength-get_linelen(prev);
    if (lendiff!=0) {	/* Old and new lines are not the same length */
      if (basicvars.top+lendiff>=basicvars.himem) error(ERR_NOROOM);	/* No room for line */
      memmove(prev+newlength, bp, basicvars.top-bp+ENDMARKSIZE);
      basicvars.top+=lendiff;
    }
    memmove(prev, line, newlength);
    last_added = prev;
  }
  else {	/* Adding a line */
    if (basicvars.top+newlength>=basicvars.himem) error(ERR_NOROOM);	/* No room for line */
    memmove(bp+newlength, bp, basicvars.top-bp+ENDMARKSIZE);
    memmove(bp, line, newlength);
    basicvars.top+=newlength;
    last_added = bp;
  }
  adjust_heaplimits();
}

/*
** 'delete_line' deletes the line passed to it in 'line', if it
** exists
*/
void delete_line(int32 line) {
  int32 length;
  byte *p;
  p = find_line(line);
  if (get_lineno(p)==line) {	/* Need an exact match. Cannot delete just anything... */
    length = get_linelen(p);
    memmove(p, p+length, basicvars.top-p-length+ENDMARKSIZE);
    basicvars.top-=length;
    adjust_heaplimits();
    last_added = NIL;
  }
}

/*
** 'delete range' is called to delete a range of lines from 'low' to
** 'high'. The lines themselves do not have to be present, in which case
** lines from the nearest one above to nearest below will be disposed of
*/
void delete_range(int32 low, int32 high) {
  byte *lowline, *highline;
  if (low>high) return;
  lowline = find_line(low);
  if (get_lineno(lowline)==ENDLINENO) return;	/* No lines are in the range to delete */
  clear_refs();
  highline = find_line(high);
  if (get_lineno(highline)==high) highline+=get_linelen(highline);
  memmove(lowline, highline, basicvars.top-highline+ENDMARKSIZE);
  basicvars.top-=(highline-lowline);
  adjust_heaplimits();
  last_added = NIL;
}

/*
** 'renumber' renumbers the lines in the program starting at 'progstart'.
** It does this in three passes. On the first, it goes through and resolves
** all the line number references it can. It then renumbers the program.
** The last step is to go through all the line numbers again and replace
** the old line numbers with the new. As a bonus, all of the line number
** pointers in the program are left pointing at the right lines.
*/
void renumber_program(byte *progstart, int32 start, int32 step) {
  byte *bp;
  boolean ok;
  bp = progstart;
  while (!AT_PROGEND(bp) && start<=MAXLINENO) {
    resolve_linenums(bp);
    bp+=get_linelen(bp);
  }
  bp = progstart;
  while (!AT_PROGEND(bp) && start<=MAXLINENO) {
    save_lineno(bp, start);
    start+=step;
    bp+=get_linelen(bp);
  }
  ok = AT_PROGEND(bp);
  if (!ok) {	/* Oops... Did not stop at end of program */
    if (step!=1) { 	   /* Try to fix line numbers */
      start = 1;
      bp = progstart;
      while (!AT_PROGEND(bp) && start<=MAXLINENO) {
        save_lineno(bp, start);
        start++;
        bp+=get_linelen(bp);
      }
    }
  }
  bp = progstart;
  while (!AT_PROGEND(bp) && start<=MAXLINENO) {
    reset_linenums(bp);
    bp+=get_linelen(bp);
  }
  if(!ok) error(ERR_RENUMBER);
}

/*
** 'open_file' opens the file 'name' for reading, returning the
** file handle or NIL if the file cannot be found.
** If the name consists of the name of the file only, that is, it
** does not contain any directory names, the function checks in each
** of the directories given by basicvars.loadpath for the file.
** 'loadpath' is a comma-separated list of directory names (supplied
** via the command line option '-path'). The name of the file that
** could be opened is left in 'basicvars.filename'
*/
static FILE *open_file(char *name) {
  char *srce, *dest;
  FILE *handle;
  strcpy(basicvars.filename, name);
  handle = fopen(name, "rb");
  if (handle!=NIL || basicvars.loadpath==NIL || isapath(name)) return handle;
/* File not found but there is a list of directories to search */
  srce = basicvars.loadpath;
  do {
    dest = basicvars.filename;
    if (*srce!=',') {		/* Not got a null directory name */
      while (*srce!=NUL && *srce!=',') {
        *dest = *srce;
        dest++;
        srce++;
      }
      if (*(srce-1)!=DIR_SEP) {	/* No separator after directory name */
        *dest = DIR_SEP;
        dest++;
      }
    }
    *dest = NUL;
    strcat(basicvars.filename, name);
    handle = fopen(basicvars.filename, "rb");
    if (handle!=NIL || *srce==NUL) break;	/* File found or end of directory list reached */
    srce++;
  } while (TRUE);
  return handle;	/* Return file handle or NIL if file not found */
}

/*
** 'read_bbcfile' reads a tokenised BBC BASIC file. It converts the
** file to this this interpreter's format, saving it in the Basic
** workspace at the address given by 'base'. It returns the number
** of bytes occupied by the loaded file. On entry, 'ftype' indicates
** whether it is Acorn format (6502/6809/PDP/32K/68K/ARM) or Russell
** format (Z80/80x86).
** On entry, the file pointer is pointing to the start of the file, the
** initial CR for Acorn files, the first line length for Russell files.
** Acorn format:    <cr><hi><lo><len><text....><cr>...<cr><ff>
** Russell format: <len><lo><hi><text....><cr>...<cr><00><ff><ff>
**
*/
static int32 read_bbcfile(FILE *bbcfile, byte *base, byte *limit, int32 ftype) {
  int length, count;
  byte line[INPUTLEN], *filebase;
  byte tokenline[MAXSTATELEN];
  basicvars.linecount = 0;	/* Number of line being read from file */
  filebase = base;

  if (ftype == BBCFILE) count=fgetc(bbcfile);	/* Skip initial CR */
  do {
    if (ftype == BBCFILE) {
      line[0] = fgetc(bbcfile);			/* High order byte of line number */
      if (line[0]==ACORN_ENDMARK) break;	/* Found 0xFF marking end of program so end */
      line[1] = fgetc(bbcfile);			/* Low order byte of line number */
      line[2] = length = fgetc(bbcfile);	/* Line length */
    } else {
      line[2] = length = fgetc(bbcfile);	/* Line length */
      if (line[2]==0) break;			/* Found 0x00 at end of file so end */
      line[1] = fgetc(bbcfile);			/* Low order byte of line number */
      line[0] = fgetc(bbcfile);			/* High order byte of line number */
    }
    count = fread(&line[3], sizeof(byte), length - 3, bbcfile);
    if (count != length - 3) {			/* Incorrect number of bytes read */
      fclose(bbcfile);
      bbcfile = NULL;
      error(ERR_READFAIL, basicvars.filename);
    }
    basicvars.linecount++;
    length = reformat(line, tokenline, ftype);
    if (length > 0) {				/* Line length is not zero so include line */
      if (base + length >= limit) {
        if (bbcfile) {
	  fclose(bbcfile);
	  bbcfile = NULL;
	}
	error(ERR_NOROOM);
      }
      memmove(base, tokenline, length);
      base+=length;
    }
  } while (!feof(bbcfile));
  if (bbcfile) fclose(bbcfile);
  basicvars.linecount = 0;
  if (base + ENDMARKSIZE >= limit) error(ERR_NOROOM);
  mark_end(base);
  return ALIGN(base - filebase + ENDMARKSIZE);
}

/*
** 'read_textfile' reads a Basic program that is in text form,
** storing it at 'base'. 'limit' marks the highest address it can
** occupy. If any of the lines are missing line numbers then they
** are added at the end. Lines are numbered from 1 and go up by 1.
** The idea here is that the line numbers will correspond to the
** number of the line in the original text file making it easier
** to find out what line an error occured in.'silent' is set to
** TRUE if no message is to be displayed if line numbers are
** added. The program is tokenised as it is stored. The function
** returns the size of the tokenised program.
** This code assumes that fgets() returning a null pointer marks
** the end of the file. It could indicate an I/O error on the file.
*/
static int32 read_textfile(FILE *textfile, byte *base, byte *limit, boolean silent) {
  int length;
  byte *filebase;
  char *result;
  byte tokenline[MAXSTATELEN];
  boolean gzipped = FALSE;
#ifdef HAVE_ZLIB_H
  gzFile gzipfile;
#endif
  fseek (textfile, 0, 0);
  tokenline[2] = 0;
  fread (tokenline, 1, 3, textfile);
  gzipped = (tokenline[0] == 0x1F && tokenline[1] == 0x8B && tokenline[2] == 8);
  if (gzipped) {
#ifdef HAVE_ZLIB_H
    fclose (textfile);
    gzipfile = gzopen (basicvars.filename, "r");
    if (gzipfile==NIL) error(ERR_FILEIO, basicvars.filename);
#else
    error(ERR_NOGZIP);
#endif
  }
  else {
    textfile = freopen(basicvars.filename, "r", textfile);	/* Close and reopen the file as a text file */
    if (textfile==NIL) error(ERR_FILEIO, basicvars.filename);
  }
  needsnumbers = FALSE;		/* This will be set by tokenise_line() above */
  basicvars.linecount = 0;	/* Number of line being read from file */
  filebase = base;
#ifdef HAVE_ZLIB_H
  if (gzipped)
    result = gzgets(gzipfile, basicvars.stringwork, INPUTLEN);
  else
#endif
  result = fgets(basicvars.stringwork, INPUTLEN, textfile);
  if (result!=NIL && basicvars.stringwork[0]=='#') {	/* Ignore first line if it starts with a '#' */
    basicvars.runflags.quitatend=basicvars.runflags.loadngo;
#ifdef HAVE_ZLIB_H
    if (gzipped)
      result = gzgets(gzipfile, basicvars.stringwork, INPUTLEN);
    else
#endif
    result = fgets(basicvars.stringwork, INPUTLEN, textfile);
  }
  while (result!=NIL) {
    basicvars.linecount++;
    length = strlen(basicvars.stringwork);
/* First, get rid of any trailing blanks, line feeds and so forth */
    do
      length--;
    while (length>=0 && isspace(basicvars.stringwork[length]));
    length++;
    basicvars.stringwork[length] = NUL;
    tokenize(basicvars.stringwork, tokenline, HASLINE, FALSE);
//    tokenize(basicvars.stringwork, tokenline, HASLINE);
    if (get_lineno(tokenline)==NOLINENO) {
      save_lineno(tokenline, 0);	/* Otherwise renumber goes a bit funny */
      needsnumbers = TRUE;
    }
    length = get_linelen(tokenline);
    if (length>0) {	/* Line length is not zero so include line */
      if (base+length>=limit) {	/* No room left */
#ifdef HAVE_ZLIB_H
        if (gzipped)
          gzclose (gzipfile);
        else
#endif
        fclose(textfile);
        error(ERR_NOROOM);
      }
      memmove(base, tokenline, length);
      base+=length;
    }
#ifdef HAVE_ZLIB_H
    if (gzipped)
      result = gzgets(gzipfile, basicvars.stringwork, INPUTLEN);
    else
#endif
    result = fgets(basicvars.stringwork, INPUTLEN, textfile);
  }
#ifdef HAVE_ZLIB_H
  if (gzipped)
    gzclose (gzipfile);
  else
#endif
  fclose(textfile);
  basicvars.linecount = 0;
  if (base+ENDMARKSIZE>=limit) error(ERR_NOROOM);
  mark_end(base);
  if (needsnumbers) {		/* Line numbers are missing */
    renumber_program(filebase, 1, 1);
//    if (!silent) error(WARN_RENUMBERED);
  }
  return ALIGN(base-filebase+ENDMARKSIZE);
}

#ifdef BRANDYAPP

static void blockread(void *ptr, size_t size, size_t nmemb) {
  unsigned char *blob = (unsigned char *)&_binary_app_start;
  unsigned char *blobend = (unsigned char *)&_binary_app_end;
  unsigned long int blobsize = (unsigned long int)&_binary_app_size;

  memcpy(ptr, (void *)(blob + blockptr), size*nmemb);
  blockptr += (size*nmemb);
}

static char *blockgets(char *s, int size) {
  unsigned char *blob = (unsigned char *)&_binary_app_start;
  unsigned char *blobend = (unsigned char *)&_binary_app_end;
  unsigned long int blobsize = (unsigned long int)&_binary_app_size;

  unsigned int p = 0;
  int l = 1;

  while (l && (p < (size-1)) && (blob+blockptr <= blobend)) {
    *(s+p) = *(blob+blockptr);
    if (*(s+p)=='\n') l=0;
    p++; blockptr++;
  }
  *(s+p)='\0';
  if (blob+blockptr <= blobend) return s;
  return NULL;
}

static int32 read_textblock(byte *base, byte *limit, boolean silent) {
  int length;
  byte *filebase;
  char *result;
  byte tokenline[MAXSTATELEN];

  blockptr = 0; // fseek (textfile, 0, 0);
  tokenline[2] = 0;
  blockread (tokenline, 1, 3);
  blockptr = 0;  /* Close and reopen the file as a text file */
  needsnumbers = FALSE;		/* This will be set by tokenise_line() above */
  basicvars.linecount = 0;	/* Number of line being read from file */
  filebase = base;
  result = blockgets(basicvars.stringwork, INPUTLEN); // result = fgets(basicvars.stringwork, INPUTLEN, textfile);
  if (result!=NIL && basicvars.stringwork[0]=='#') {	/* Ignore first line if it starts with a '#' */
  result = blockgets(basicvars.stringwork, INPUTLEN); // result = fgets(basicvars.stringwork, INPUTLEN, textfile);
  }
  while (result!=NIL) {
    basicvars.linecount++;
    length = strlen(basicvars.stringwork);
/* First, get rid of any trailing blanks, line feeds and so forth */
    do
      length--;
    while (length>=0 && isspace(basicvars.stringwork[length]));
    length++;
    basicvars.stringwork[length] = NUL;
    tokenize(basicvars.stringwork, tokenline, HASLINE, FALSE);
    if (get_lineno(tokenline)==NOLINENO) {
      save_lineno(tokenline, 0);	/* Otherwise renumber goes a bit funny */
      needsnumbers = TRUE;
    }
    length = get_linelen(tokenline);
    if (length>0) {	/* Line length is not zero so include line */
      if (base+length>=limit) {	/* No room left */
        error(ERR_NOROOM);
      }
      memmove(base, tokenline, length);
      base+=length;
    }
    result = blockgets(basicvars.stringwork, INPUTLEN); // result = fgets(basicvars.stringwork, INPUTLEN, textfile);
  }
  basicvars.linecount = 0;
  if (base+ENDMARKSIZE>=limit) error(ERR_NOROOM);
  mark_end(base);
  if (needsnumbers) {		/* Line numbers are missing */
    renumber_program(filebase, 1, 1);
//    if (!silent) error(WARN_RENUMBERED);
  }
  return ALIGN(base-filebase+ENDMARKSIZE);
}
#endif

/*
** 'identify' tries to identify the type of file passed to it,
** that is, is it a tokenised Basic program, plain text or what.
** Three file formats are supported, Acorn or Russell tokenised
** Basic and plain text.
** Acorn Basic files start with <cr><hi><lo><len>..., len-><cr>
** Russell Basic files start with <len><hi><lo>... len-1-><cr>
** This is not a 100% rigourous test, a carefully constructed
** text file that starts with a <cr> could be recognised as an
** Acorn Basic file. Strictly speaking, the entire file needs
** to be scanned following the line length links and checking
** what scanning procedure gives a valid file.
*/
static filetype identify(FILE *thisfile, char *name) {
  int32 count;

  count = fread(basicvars.stringwork, sizeof(byte), 260, thisfile);
  fseek(thisfile, 0, SEEK_SET);				/* Rewind to start */
  if (count < 2) return TEXTFILE;			/* Too short to be tokenised */

  if (basicvars.stringwork[0] == CR)			/* Simple check for Acorn format */
    if ((unsigned char)basicvars.stringwork[3] > 3)
      if (basicvars.stringwork[(unsigned char)basicvars.stringwork[3]] == CR)
        return BBCFILE;

  if ((unsigned char)basicvars.stringwork[0] > 3)	/* Simple check for Russell format */
    if (basicvars.stringwork[(unsigned char)basicvars.stringwork[0]-1] == CR)
      return Z80FILE;

  return TEXTFILE;					/* Everything else is text */

}

/*
** 'read_basic' reads a Basic program into memory and sets the various
** pointers in 'basicvars' for it.
** The file is opened as a binary file as that appears to be the only
** case where the value returned by 'ftell' can be interpreted as the
** number of characters in the file.
*/
void read_basic(char *name) {
  FILE *loadfile;
  int32 length, ftype;
  loadfile = open_file(name);
  if (loadfile==NIL) error(ERR_NOTFOUND, name);
  last_added = NIL;
  if ((ftype=identify(loadfile, name)) != TEXTFILE) {	/* Tokenised BBC BASIC file */
    clear_program();
    length = read_bbcfile(loadfile, basicvars.top, basicvars.himem, ftype);
  }
  else {						/* Plain text */
    clear_program();
    length = read_textfile(loadfile, basicvars.top, basicvars.himem, basicvars.runflags.loadngo);
  }
  basicvars.top+=length;
  basicvars.misc_flags.badprogram = FALSE;
  adjust_heaplimits();
  if (basicvars.debug_flags.debug) {
    fprintf(stderr, "Program is loaded at page=&%p,  top=&%p\n", basicvars.page, basicvars.top);
  }
}

#ifdef BRANDYAPP
void read_basic_block() {
  int32 length, ftype;

  last_added = NIL;
  clear_program();
  length = read_textblock(basicvars.top, basicvars.himem, basicvars.runflags.loadngo);
  basicvars.top+=length;
  basicvars.misc_flags.badprogram = FALSE;
  adjust_heaplimits();
}
#endif

/*
** 'link_library' is called to add a library to the relevant library list
*/
static void link_library(char *name, byte *base, int32 size, boolean onheap) {
  library *lp;
  int n;
  if (onheap) {		/* Library is held on Basic heap */
    lp = allocmem(sizeof(library));	/* Add library to list */
    lp->libname = allocmem(strlen(name)+1);	/* +1 for NULL at end */
    lp->libflink = basicvars.liblist;
    basicvars.liblist = lp;
  }
  else {	/* Library is held in permanent memory */
    lp = malloc(sizeof(library));
    if (lp==NIL) error(ERR_LIBSIZE, name);	/* Run out of memory */
    lp->libname = malloc(strlen(name)+1);
    lp->libflink = basicvars.installist;
    basicvars.installist = lp;
  }
  strcpy(lp->libname, name);
  lp->libstart = base;
  lp->libsize = size;
  lp->libfplist = NIL;
  for (n=0; n<VARLISTS; n++) lp->varlists[n] = NIL;
}

/*
** 'read_bbclib' reads a tokenised BBC BASIC library file.
** It has to be read a line at a line and translated into the tokens
** used by this interpreter
*/
static void read_bbclib(FILE *libfile, char *name, boolean onheap, int32 ftype) {
  int32 size;
  byte *base;
  base = basicvars.vartop;
  size = read_bbcfile(libfile, base, basicvars.stacktop.bytesp, ftype);
  if (onheap) {	/* Adjust heap pointers as library is on the heap */
    basicvars.vartop = basicvars.vartop+size;
    basicvars.stacklimit.bytesp = basicvars.vartop+STACKBUFFER;
  }
  else {	/* Library being loaded via 'INSTALL' - Move to permanent memory */
    byte *installbase;
    installbase = malloc(size);
    if (installbase==NIL) error(ERR_LIBSIZE, name);
    memmove(installbase, base, size);
    base = installbase;
    if (basicvars.debug_flags.debug) fprintf(stderr, "Loaded library '%s' at %p, size = %d\n",
     name, base, size);
  }
  link_library(name, base, size, onheap);
}

/*
** 'read_textlib' reads a plain text library file. It reads it into memory
** at 'VARTOP', directly adjusting basicvars.vartop rather than acquiring
** the memory via 'allocmem'. A weakness of this code is that libraries are
** always read and stored on the Basic heap first. In the case of libraries
** being loaded via 'LIBRARY' this is not a problem but those being dealt
** via 'INSTALL' have then to be copied to their permanent homes. There is
** a chance that the interpreter might run out memory in this case.
*/
static void read_textlib(FILE *libfile, char *name, boolean onheap) {
  int32 size;
  byte *base;
  base = basicvars.vartop;
  size = read_textfile(libfile, base, basicvars.stacktop.bytesp, TRUE);
  if (onheap) {
    basicvars.vartop+=size;
    basicvars.stacklimit.bytesp = basicvars.vartop+STACKBUFFER;
  }
  else {	/* Library being loaded via 'INSTALL' - Move to permanent memory */
    byte *installbase;
    installbase = malloc(size);
    if (installbase==NIL) error(ERR_LIBSIZE, name);
    memmove(installbase, base, size);
    base = installbase;
    if (basicvars.debug_flags.debug) fprintf(stderr, "Loaded library '%s' at %p, size = %d\n",
     name, base, size);
  }
  link_library(name, base, size, onheap);
}

/*
** 'read_library' is called to load a library into memory. 'onheap'
** says where it goes: if set to 'TRUE' then it is a temporary library
** and is stored on the Basic heap. If set to 'FALSE' then the
** library is a permament library and is kept separate from the
** Basic workspace. This is different to how Acorn Basic works, where
** the library is stored above 'HIMEM'.
*/
void read_library(char *name, boolean onheap) {
  library *lp;
  FILE *libfile;
  int32 ftype;

  if (onheap)	/* Check if library has already been loaded */
    lp = basicvars.liblist;
  else {
    lp = basicvars.installist;
  }
  while (lp != NIL && strcmp(lp->libname, name) != 0) lp = lp->libflink;
  if (lp != NIL) {	/* Library has already been loaded */
    error(WARN_LIBLOADED, name);
    return;
  }
  libfile = open_file(name);
  if (libfile == NIL) error(ERR_NOLIB, name);		/* Cannot find library */
  if ((ftype=identify(libfile, name)) != TEXTFILE)	/* Reading a BBC BASIC tokenised library */
    read_bbclib(libfile, name, onheap, ftype);
  else {						/* Reading a library in plain text form */
    read_textlib(libfile, name, onheap);
  }
}

/*
** 'write_text' is called to save a program in text form
*/
void write_text(char *name) {
  FILE *savefile;
  byte *bp;
  int32 x;
  savefile = fopen(name, "w");
  if (savefile==NIL) error(ERR_NOTCREATED, name);
  bp = basicvars.start;
  while (!AT_PROGEND(bp)) {
    expand(bp, basicvars.stringwork);
    x = fputs(basicvars.stringwork, savefile);
    if (x!=EOF) x = fputc('\n', savefile);
    if (x==EOF) {	/* Error occured writing to file */
      fclose(savefile);
      error(ERR_WRITEFAIL, name);
    }
    bp+=get_linelen(bp);
  }
  fclose(savefile);
}

/*
** 'edit_line' is the main line editing routine.
*/
void edit_line(void) {
  if (basicvars.misc_flags.badprogram) error(ERR_BADPROG);
  clear_refs();
  basicvars.misc_flags.validsaved = FALSE;	/* If program is edited mark save area contents as bad */
  if (isempty(thisline))			/* Empty line = delete line */
    delete_line(get_lineno(thisline));
  else {
    insert_line(thisline);
  }
}
