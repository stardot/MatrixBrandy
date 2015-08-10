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
**	This file defines the functions handling Basic I/O, sound
**	and graphics statements
*/

#ifndef __iostate_h
#define __iostate_h

extern void exec_beats(void);
extern void exec_bput(void);
extern void exec_circle(void);
extern void exec_clg(void);
extern void exec_close(void);
extern void exec_cls(void);
extern void exec_colour(void);
extern void exec_draw(void);
extern void exec_drawby(void);
extern void exec_ellipse(void);
extern void exec_envelope(void);
extern void exec_fill(void);
extern void exec_fillby(void);
extern void exec_gcol(void);
extern void exec_input(void);
extern void exec_line(void);
extern void exec_mode(void);
extern void exec_mouse(void);
extern void exec_move(void);
extern void exec_moveby(void);
extern void exec_off(void);
extern void exec_origin(void);
extern void exec_plot(void);
extern void exec_point(void);
extern void exec_pointby(void);
extern void exec_pointto(void);
extern void exec_print(void);
extern void exec_rectangle(void);
extern void exec_sound(void);
extern void exec_stereo(void);
extern void exec_tempo(void);
extern void exec_tint(void);
extern void exec_vdu(void);
extern void exec_voice(void);
extern void exec_voices(void);
extern void exec_width(void);

#endif
