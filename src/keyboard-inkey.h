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
**                             also: INKEY = scan EOR -1
** 
** 13-Nov-2018 JGH: Swapped Windows/Kana to correct positions
**                  Updated 46, 72, 90, 92, 94, 95, 125, 126, 127.
** 14-Nov-2018 JGH: Turns out Windows/Kana were correct way around
**                  See RiscOs/Sources/Internat/IntKey/Source/IntKeyBody
*/
#include "SDL.h"
#include "SDL_keysym.h"

int32 inkeylookup[] = {
  SDLK_LSHIFT,		/*   0  done at a higher level */
  SDLK_LCTRL,		/*   1  done at a higher level */
  SDLK_LALT,		/*   2  done at a higher level */
  SDLK_LSHIFT,		/*   3  */
  SDLK_LCTRL,		/*   4  */
  SDLK_LALT,		/*   5  */
  SDLK_RSHIFT,		/*   6  */
  SDLK_RCTRL,		/*   7  */
  SDLK_RALT,		/*   8  might want to use SDLK_MODE for Alt Gr key */
  -1,			/*   9  left mouse   - done at a higher level */
  -1,			/*  10  middle mouse - done at a higher level */
  -1,			/*  11  right mouse  - done at a higher level */
  -1,			/*  12  should be FN  */
  -1,			/*  13  reserved      */
  -1,			/*  14  reserved      */
  -1,			/*  15  reserved      */
  SDLK_q,		/*  16  Q             */
  SDLK_3,		/*  17  3             */
  SDLK_4,		/*  18  4             */
  SDLK_5,		/*  19  5             */
  SDLK_F4,		/*  20  F4            */
  SDLK_8,		/*  21  8             */
  SDLK_F7,		/*  22  F7            */
  SDLK_MINUS,		/*  23  -             */
  SDLK_CARET,		/*  24  ^             */
  SDLK_LEFT,		/*  25  Left          */
  SDLK_KP6,		/*  26  Keypad 6      */
  SDLK_KP7,		/*  27  Keypad 7      */
  SDLK_F11,		/*  28  F11           */
  SDLK_F12,		/*  29  F12           */
  SDLK_F10,		/*  30  F10           */
  SDLK_SCROLLOCK,	/*  31  Scroll Lock   */
  SDLK_PRINT,		/*  32  Print/F0      */
  SDLK_w,		/*  33  W             */
  SDLK_e,		/*  34  E             */
  SDLK_t,		/*  35  T             */
  SDLK_7,		/*  36  7             */
  SDLK_i,		/*  37  I             */
  SDLK_9,		/*  38  9             */
  SDLK_0,		/*  39  0             */
  SDLK_UNDERSCORE,	/*  40  _             */
  SDLK_DOWN,		/*  41  Down          */
  SDLK_KP8,		/*  42  Keypad 8      */
  SDLK_KP9,		/*  43  Keypad 9      */
  SDLK_BREAK,		/*  44  Break         */
  SDLK_BACKQUOTE,	/*  45  `/~/¬         */
  SDLK_HASH,		/*  46  £/Yen  #/~    CHECK  USB: &32 or &64 or &89 */
  SDLK_BACKSPACE,	/*  47  Backspace     */
  SDLK_1,		/*  48  1             */
  SDLK_2,		/*  49  2             */
  SDLK_d,		/*  50  D             */
  SDLK_r,		/*  51  R             */
  SDLK_6,		/*  52  6             */
  SDLK_u,		/*  53  U             */
  SDLK_o,		/*  54  O             */
  SDLK_p,		/*  55  P             */
  SDLK_LEFTBRACKET,	/*  56  [             */
  SDLK_UP,		/*  57  Up            */
  SDLK_KP_PLUS,		/*  58  Keypad +      */
  SDLK_KP_MINUS,	/*  59  Keypad -      */
  SDLK_KP_ENTER,	/*  60  Keypad Enter  */
  SDLK_INSERT,		/*  61  Insert        */
  SDLK_HOME,		/*  62  Home          */
  SDLK_PAGEUP,		/*  63  PgUp          */
  SDLK_CAPSLOCK,	/*  64  Caps Lock     */
  SDLK_a,		/*  65  A             */
  SDLK_x,		/*  66  X             */
  SDLK_f,		/*  67  F             */
  SDLK_y,		/*  68  Y             */
  SDLK_j,		/*  69  J             */
  SDLK_k,		/*  70  K             */
  SDLK_AT,		/*  71  @             */
  SDLK_COLON,		/*  72  :             */
  SDLK_RETURN,		/*  73  Return        */
  SDLK_KP_DIVIDE,	/*  74  Keypad /      */
  SDLK_KP_BACKSPACE,	/*  75  Keypad Del - only on Master */
  SDLK_KP_PERIOD,	/*  76  Keypad .      */
  SDLK_NUMLOCK,		/*  77  Num Lock      */
  SDLK_PAGEDOWN,	/*  78  PgDn          */
  SDLK_QUOTE,		/*  79  '/"  '/@      */
  -1,			/*  80  Shift Lock - only on BBC/Master */
  SDLK_s,		/*  81  S             */
  SDLK_c,		/*  82  C             */
  SDLK_g,		/*  83  G             */
  SDLK_h,		/*  84  H             */
  SDLK_n,		/*  85  N             */
  SDLK_l,		/*  86  L             */
  SDLK_SEMICOLON,	/*  87  ;             */
  SDLK_RIGHTBRACKET,	/*  88  ]             */
  SDLK_DELETE,		/*  89  Delete        */
  -1, // SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_NONUSHASH),	/*  90  Keypad #  #/~ CHECK  USB &32 */
  SDLK_KP_MULTIPLY,	/*  91  Keypad *      */
  SDLK_KP_COMMA,	/*  92  Keypad ,  - only on Master */
  SDLK_EQUALS,		/*  93  =/+           */
  -1, // SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_NONUSBACKSLASH),	/*  94  Left  \|  - between Shift and Z, USB &64  CHECK with US keyboard */
  -1, // SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_INTERNATIONAL1),	/*  95  Right |_  - between ? and Shift, USB &87  CHECK with JP keyboard */
  SDLK_TAB,		/*  96  TAB           */
  SDLK_z,		/*  97  Z             */
  SDLK_SPACE,		/*  98  Space         */
  SDLK_v,		/*  99  V             */
  SDLK_b,		/* 100  B             */
  SDLK_m,		/* 101  M             */
  SDLK_COMMA,		/* 102  ,             */
  SDLK_PERIOD,		/* 103  .             */
  SDLK_SLASH,		/* 104  /             */
  SDLK_END,		/* 105  Copy/End      */
  SDLK_KP0,		/* 106  Keypad 0      */
  SDLK_KP1,		/* 107  Keypad 1      */
  SDLK_KP3,		/* 108  Keypad 3      */
  -1, // SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_INTERNATIONAL5),	/* 109  No Convert  CHECK with JP keyboard, USB &8B */
  -1, // SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_INTERNATIONAL4),	/* 110  Convert     CHECK with JP keyboard, USB &8A */
  -1, // SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_INTERNATIONAL2),	/* 111  Kana        CHECK with JP keyboard, USB &88 */
  SDLK_ESCAPE,		/* 112  Escape        */
  SDLK_F1,		/* 113  F1            */
  SDLK_F2,		/* 114  F2            */
  SDLK_F3,		/* 115  F3            */
  SDLK_F5,		/* 116  F5            */
  SDLK_F6,		/* 117  F6            */
  SDLK_F8,		/* 118  F8            */
  SDLK_F9,		/* 119  F9            */
  SDLK_BACKSLASH,	/* 120  \|            */
  SDLK_RIGHT,		/* 121  Right         */
  SDLK_KP4,		/* 122  Keypad 4      */
  SDLK_KP5,		/* 123  Keypad 5      */
  SDLK_KP2,		/* 124  Keypad 2      */
  SDLK_LSUPER,		/* 125  Left Windows  */
  SDLK_RSUPER,		/* 126  Right Windows */
  SDLK_MENU,		/* 127  Windows Menu  */
  -1			/* 128  No key        */
};
