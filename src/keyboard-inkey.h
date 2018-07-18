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
**	This file defines the keyboard lookup codes.
**
** These are RISC OS key scan codes, INKEY = (scan+1) * -1
**
*/
#include "SDL.h"
#include "SDL_keysym.h"

int32 inkeylookup[] = {
  SDLK_LSHIFT,		/*   0, really should be either, but SDL doesn't offer this */
  SDLK_LCTRL,		/*   1, really should be either, but SDL doesn't offer this */
  SDLK_LALT,		/*   2, really should be either, but SDL doesn't offer this */
  SDLK_LSHIFT,		/*   3 */
  SDLK_LCTRL,		/*   4 */
  SDLK_LALT,		/*   5 */
  SDLK_RSHIFT,		/*   6 */
  SDLK_RCTRL,		/*   7 */
  SDLK_RALT,		/*   8 - might want to use SDLK_MODE for Alt Gr key */
  -1,			/*   9 - should be left mouse */
  -1,			/*  10 - should be middle mouse */
  -1,			/*  11 - should be right mouse */
  -1,			/*  12 - should be FN */
  -1,			/*  13 - reserved */
  -1,			/*  14 - reserved */
  -1,			/*  15 - reserved */
  SDLK_q,		/*  16 */
  SDLK_3,		/*  17 */
  SDLK_4,		/*  18 */
  SDLK_5,		/*  19 */
  SDLK_F4,		/*  20 */
  SDLK_8,		/*  21 */
  SDLK_F7,		/*  22 */
  SDLK_MINUS,		/*  23 */
  SDLK_CARET,		/*  24 */
  SDLK_LEFT,		/*  25 */
  SDLK_KP6,		/*  26 */
  SDLK_KP7,		/*  27 */
  SDLK_F11,		/*  28 */
  SDLK_F12,		/*  29 */
  SDLK_F10,		/*  30 */
  SDLK_SCROLLOCK,	/*  31 */
  SDLK_PRINT,		/*  32 */
  SDLK_w,		/*  33 */
  SDLK_e,		/*  34 */
  SDLK_t,		/*  35 */
  SDLK_7,		/*  36 */
  SDLK_i,		/*  37 */
  SDLK_9,		/*  38 */
  SDLK_0,		/*  39 */
  SDLK_UNDERSCORE,	/*  40 */
  SDLK_DOWN,		/*  41 */
  SDLK_KP8,		/*  42 */
  SDLK_KP9,		/*  43 */
  SDLK_BREAK,		/*  44 */
  SDLK_BACKQUOTE,	/*  45 */
  SDLK_HASH,		/*  46 - this is probably UK keymap specific */
  SDLK_BACKSPACE,	/*  47 */
  SDLK_1,		/*  48 */
  SDLK_2,		/*  49 */
  SDLK_d,		/*  50 */
  SDLK_r,		/*  51 */
  SDLK_6,		/*  52 */
  SDLK_u,		/*  53 */
  SDLK_o,		/*  54 */
  SDLK_p,		/*  55 */
  SDLK_LEFTBRACKET,	/*  56 */
  SDLK_UP,		/*  57 */
  SDLK_KP_PLUS,		/*  58 */
  SDLK_KP_MINUS,	/*  59 */
  SDLK_KP_ENTER,	/*  60 */
  SDLK_INSERT,		/*  61 */
  SDLK_HOME,		/*  62 */
  SDLK_PAGEUP,		/*  63 */
  SDLK_CAPSLOCK,	/*  64 */
  SDLK_a,		/*  65 */
  SDLK_x,		/*  66 */
  SDLK_f,		/*  67 */
  SDLK_y,		/*  68 */
  SDLK_j,		/*  69 */
  SDLK_k,		/*  70 */
  SDLK_AT,		/*  71 */
  SDLK_COLON,		/*  72 */
  SDLK_RETURN,		/*  73 */
  SDLK_KP_DIVIDE,	/*  74 */
  -1,			/*  75 - Numberpad Delete, code only on BBC not RISC OS */
  SDLK_KP_PERIOD,	/*  76 */
  SDLK_NUMLOCK,		/*  77 */
  SDLK_PAGEDOWN,	/*  78 */
  SDLK_QUOTE,		/*  79 */
  -1,			/*  80 - Shift Lock, code only on BBC not RISC OS */
  SDLK_s,		/*  81 */
  SDLK_c,		/*  82 */
  SDLK_g,		/*  83 */
  SDLK_h,		/*  84 */
  SDLK_n,		/*  85 */
  SDLK_l,		/*  86 */
  SDLK_SEMICOLON,	/*  87 */
  SDLK_RIGHTBRACKET,	/*  88 */
  SDLK_DELETE,		/*  89 */
  SDLK_HASH,		/*  90 - Archimedes Numberpad hash, mapped to the regular # key */
  SDLK_KP_MULTIPLY,	/*  91 */
  -1,			/*  92 - Numberpad Colon, code only on BBC not RISC OS */
  SDLK_EQUALS,		/*  93 */
  -1,			/*  94 - officially "Not Fitted Left" */
  -1,			/*  95 - officially "Not Fitted Right" */
  SDLK_TAB,		/*  96 */
  SDLK_z,		/*  97 */
  SDLK_SPACE,		/*  98 */
  SDLK_v,		/*  99 */
  SDLK_b,		/* 100 */
  SDLK_m,		/* 101 */
  SDLK_COMMA,		/* 102 */
  SDLK_PERIOD,		/* 103 */
  SDLK_SLASH,		/* 104 */
  SDLK_END,		/* 105 - RISC OS "Copy" key */
  SDLK_KP0,		/* 106 */
  SDLK_KP1,		/* 107 */
  SDLK_KP3,		/* 108 */
  -1,			/* 109 - offically "No Convert" */
  -1,			/* 110 - officially "Convert" */
  -1,			/* 111 - officially "Kana" */
  SDLK_ESCAPE,		/* 112 */
  SDLK_F1,		/* 113 */
  SDLK_F2,		/* 114 */
  SDLK_F3,		/* 115 */
  SDLK_F5,		/* 116 */
  SDLK_F6,		/* 117 */
  SDLK_F8,		/* 118 */
  SDLK_F9,		/* 119 */
  SDLK_BACKSLASH,	/* 120 */
  SDLK_RIGHT,		/* 121 */
  SDLK_KP4,		/* 122 */
  SDLK_KP5,		/* 123 */
  SDLK_KP2,		/* 124 */
  SDLK_LSUPER,		/* 125 - officially "Acorn Left" - mapped to Left Windows key */
  SDLK_RSUPER,		/* 126 - officially "Acorn Right" - mapped to Right Windows key */
  SDLK_MENU,		/* 127 */
  -1			/* 128 - no key */
};
