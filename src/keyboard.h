/*
** This file is part of the Matrix Brandy Basic VI Interpreter.
** Copyright (C) 2000-2014 David Daniels
** Copyright (C) 2018-2019 Michael McConnell and contributors
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
**	This file defines the keyboard handling routines
*/

#ifndef __keyboard_h
#define __keyboard_h

#include "common.h"

typedef enum {READ_OK, READ_ESC, READ_EOF} readstate;
// This is original source of BGET at EOF giving &FE:
//             -0        -1        -2
//           000000:00 FFFFFF:FF FFFFFF:FE

extern int32 read_key(void);
extern void set_escint(int i);
extern void set_escmul(int i);
extern void osbyte44(int x);
extern readstate emulate_readline(char [], int32, int32);
extern void purge_keys(void);
#ifdef NEWKBD
extern boolean kbd_init();
extern void  kbd_quit(void);
extern int32 kbd_get(void);
extern int32 kbd_get0(void);
extern int32 kbd_inkey(int32);
extern int32 kbd_modkeys(int32);
extern int   kbd_fnkeyset(int key, char *string, int length);
extern char *kbd_fnkeyget(int key, int *length);
extern int32 kbd_readline(char *buffer, int32 length, int32 chars);
extern int32 kbd_buffered(void);
extern int32 kbd_pending(void);
extern void  kbd_escchar(char, char);
extern int   kbd_escpoll(void);
extern int   kbd_esctest(void);
extern int   kbd_escack(void);
extern void  osbyte21(int32 xreg);
extern void checkforescape(void);
#else
extern int32 emulate_get(void);
extern int32 emulate_inkey(int32);
extern int32 emulate_inkey2(int32);
extern int set_fn_string(int key, char *string, int length);
extern char *get_fn_string(int key, int *len);
extern boolean init_keyboard(void);
extern void end_keyboard(void);
#endif
#endif
