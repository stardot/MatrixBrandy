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
**	Functions that emulate the OS-specific parts of Basic live here
*/

#ifndef __emulation_h
#define __emulation_h

#include "common.h"

extern void emulate_oscli(char *, char *);
extern void emulate_endeq(int32);
extern void emulate_waitdelay(int32);
extern int32 emulate_getswino(char *, int32);
extern void emulate_sys(int32, int32[], int32[], int32*);
extern void emulate_call(int32, int32, int32 []);
extern int32 emulate_usr(int32);
extern int32 emulate_time(void);
extern void emulate_setime(int32);
extern void emulate_setimedol(char *);
extern void emulate_mouse_on(int32);
extern void emulate_mouse_off(void);
extern void emulate_mouse_to(int32, int32);
extern void emulate_mouse_step(int32, int32);
extern void emulate_mouse_colour(int32, int32, int32, int32);
extern void emulate_mouse_rectangle(int32, int32, int32, int32);
extern void emulate_mouse(int32 []);
extern int32 emulate_adval(int32);
extern void emulate_sound_on(void);
extern void emulate_sound_off(void);
extern void emulate_sound(int32, int32, int32, int32, int32);
extern void emulate_beats(int32);
extern int32 emulate_beatfn(void);
extern void emulate_tempo(int32);
extern int32 emulate_tempofn(void);
extern void emulate_voice(int32, char *);
extern void emulate_voices(int32);
extern void emulate_stereo(int32, int32);
extern boolean init_emulation(void);
extern void end_emulation(void);

#endif
