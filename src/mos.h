/*
** This file is part of the Matrix Brandy Basic VI Interpreter.
** Copyright (C) 2000-2014 David Daniels
** Copyright (C) 2018-2019 Michael McConnell, Jonathan Harston and contributors
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

extern void  mos_oscli(char *, char *, FILE *);
extern int32 mos_adval(int32);
extern void  mos_sound_on(void);
extern void  mos_sound_off(void);
extern void  mos_sound(int32, int32, int32, int32, int32);
extern int64 mos_centiseconds(void);
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

extern byte *sysvar;
#define sv_KeyboardBase   172
#define sv_EscapeBreak    200
#define sv_KBDDisabled    201
#define sv_KBDStatus      202
#define sv_TabChar        219
#define sv_EscapeChar     220
#define sv_KeyBase        221
#define sv_EscapeAction   229
#define sv_EscapeEffect   230
#define sv_CursorKeys     237
#define sv_KeypadBase     238
#define sv_Country        240
#define sv_KeyOptions     254

#define sv_VideoVDU       250
#define sv_VideoDisplay   251

#endif
