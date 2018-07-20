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

#ifndef __mos_h
#define __mos_h

#include "common.h"

// Aliases for functions for old code
#define emulate_oscli      mos_oscli
#define emulate_endeq      mos_setend
#define emulate_waitdelay  mos_waitdelay
#define emulate_getswino   mos_getswinum
#define emulate_sys        mos_sys
#define emulate_call       mos_call
#define emulate_usr        mos_usr
#define emulate_time       mos_rdtime
#define emulate_setime     mos_wrtime
#define emulate_setimedol  mos_wrrtc
#define emulate_mouse_on   mos_mouse_on
#define emulate_mouse_off  mos_mouse_off
#define emulate_mouse_to   mos_mouse_to
#define emulate_mouse_step mos_mouse_step
#define emulate_mouse_colour    mos_mouse_colour
#define emulate_mouse_rectangle mos_mouse_rectangle
#define emulate_mouse      mos_mouse
#define emulate_adval      mos_adval
#define emulate_sound_on   mos_sound_on
#define emulate_sound_off  mos_sound_off
#define emulate_sound      mos_sound
#define emulate_beats      mos_wrbeat
#define emulate_beatfn     mos_rdbeat
#define emulate_tempo      mos_wrtempo
#define emulate_tempofn    mos_rdtempo
#define emulate_voice      mos_voice
#define emulate_voices     mos_voices
#define emulate_stereo     mos_stereo
#define init_emulation     mos_init
#define end_emulation      mos_final

extern char *mos_gstrans(char *);
extern void  mos_oscli(char *, char *);
extern int32 mos_osbyte(int32, int32, int32);
extern int32 mos_adval(int32);
extern void  mos_sound_on(void);
extern void  mos_sound_off(void);
extern void  mos_sound(int32, int32, int32, int32, int32);
extern int32 mos_rdtime(void);
extern void  mos_wrtime(int32);
extern void  mos_rdrtc(char *);
extern void  mos_call(int32, int32, int32 []);
extern int32 mos_usr(int32);
extern void  mos_sys(int32, int32[], int32[], int32*);
extern int32 mos_getswinum(char *, int32);
extern void  mos_setend(int32);
extern void  mos_waitdelay(int32);
extern void  mos_mouse_on(int32);
extern void  mos_mouse_off(void);
extern void  mos_mouse_to(int32, int32);
extern void  mos_mouse_step(int32, int32);
extern void  mos_mouse_colour(int32, int32, int32, int32);
extern void  mos_mouse_rectangle(int32, int32, int32, int32);
extern void  mos_mouse(int32 []);
extern void  mos_wrbeat(int32);
extern int32 mos_rdbeat(void);
extern void  mos_wrtempo(int32);
extern int32 mos_rdtempo(void);
extern void  mos_voice(int32, char *);
extern void  mos_voices(int32);
extern void  mos_stereo(int32, int32);
extern boolean mos_init(void);
extern void  mos_final(void);

#endif
