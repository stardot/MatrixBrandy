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
extern void  mos_wrrtc(char *);
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
extern int32 mos_rdbeats(void);
extern void  mos_wrtempo(int32);
extern int32 mos_rdtempo(void);
extern void  mos_voice(int32, char *);
extern void  mos_voices(int32);
extern void  mos_stereo(int32, int32);
extern boolean mos_init(void);
extern void  mos_final(void);

#endif
