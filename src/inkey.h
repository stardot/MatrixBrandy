/*
** This file is part of the Matrix Brandy Basic VI Interpreter.
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
**	This file defines the keyboard lookup codes.
**
** These are RISC OS key scan codes, INKEY = (scan+1) * -1
**                             also: INKEY = scan EOR -1
** 
** 13-Nov-2018 JGH: Swapped Windows/Kana to correct positions
**                  Updated 46, 72, 90, 92, 94, 95, 125, 126, 127.
** 14-Nov-2018 JGH: Turns out Windows/Kana were correct way around
**                  See RiscOs/Sources/Internat/IntKey/Source/IntKeyBody.
** 28-Nov-2018 JGH: Some updates from testing, the updates need testing.
**                  Difficult to do as need SDL equivalent of
**                  mdfs.net/Software/BBCBasic/Testing/KeyScanWin.bbc to work out
**                  what the exact values need to be. Documentation is huuugely vague.
** 03-Dec-2018 JGH: Tested and updated keys as marked. EN also covers US,CN,KO.
**                  Probably impossible to get [ ] correct on JP keyboard layout.
**                  Some keys not visible through SDL: YEN \ KanaKeys, though they do
**                  give keypresses to GET.
** 09-Dec-2018 JGH: Added DOS/Windows VK keytable from JGH 'console' library.
**
*/

#ifdef USE_SDL
#include "SDL.h"
#include "SDL_keysym.h"
#define KBD_SDL 1

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
  SDLK_EQUALS,		/*  24  ^             EN:no key      JP:ok*/
  SDLK_LEFT,		/*  25  Left          */
  SDLK_KP6,		/*  26  Keypad 6      */
  SDLK_KP7,		/*  27  Keypad 7      */
  SDLK_F11,		/*  28  F11           */
  SDLK_F12,		/*  29  F12           */
  SDLK_F10,		/*  30  F10           */
  SDLK_SCROLLOCK,	/*  31  Scroll Lock   */
  SDLK_PRINT,		/*  32  Print/F0      EN:no response JP: no response */
  SDLK_w,		/*  33  W             */
  SDLK_e,		/*  34  E             */
  SDLK_t,		/*  35  T             */
  SDLK_7,		/*  36  7             */
  SDLK_i,		/*  37  I             */
  SDLK_9,		/*  38  9             */
  SDLK_0,		/*  39  0             */
  SDLK_MINUS,		/*  40  _             EN:ok,SDL_2D   JP:ok,SDL_2D */
  SDLK_DOWN,		/*  41  Down          */
  SDLK_KP8,		/*  42  Keypad 8      */
  SDLK_KP9,		/*  43  Keypad 9      */
  SDLK_BREAK,		/*  44  Break         */
  SDLK_BACKQUOTE,	/*  45  `/~/?         EN:ok          JP:ok,locks */
  SDLK_BACKSLASH,	/*  46  UKP/Yen/etc   EN:ok,SDL_5C   JP:ok,SLD_5C */
  SDLK_BACKSPACE,	/*  47  Backspace     */
  SDLK_1,		/*  48  1             */
  SDLK_2,		/*  49  2             */
  SDLK_d,		/*  50  D             */
  SDLK_r,		/*  51  R             */
  SDLK_6,		/*  52  6             */
  SDLK_u,		/*  53  U             */
  SDLK_o,		/*  54  O             */
  SDLK_p,		/*  55  P             */
  SDLK_LEFTBRACKET,	/*  56  [             EN:ok,SDL_5B   JP:wrong,SLD_5D */
  SDLK_UP,		/*  57  Up            */
  SDLK_KP_PLUS,		/*  58  Keypad +      */
  SDLK_KP_MINUS,	/*  59  Keypad -      */
  SDLK_KP_ENTER,	/*  60  Keypad Enter  */
  SDLK_INSERT,		/*  61  Insert        */
  SDLK_HOME,		/*  62  Home          */
  SDLK_PAGEUP,		/*  63  PgUp          */
  SDLK_CAPSLOCK,	/*  64  Caps Lock     locks */
  SDLK_a,		/*  65  A             */
  SDLK_x,		/*  66  X             */
  SDLK_f,		/*  67  F             */
  SDLK_y,		/*  68  Y             */
  SDLK_j,		/*  69  J             */
  SDLK_k,		/*  70  K             */
  SDLK_LEFTBRACKET,	/*  71  @             EN:no key      JP:ok,SLD_5B */
  SDLK_QUOTE,		/*  72  :             EN:no key      JP:ok,SDL_27 */
  SDLK_RETURN,		/*  73  Return        */
  SDLK_KP_DIVIDE,	/*  74  Keypad /      */
  SDLK_KP_PERIOD,	/*  75  Keypad Del - same as Keypad . on non-Master */
  SDLK_KP_PERIOD,	/*  76  Keypad .      */
  SDLK_NUMLOCK,		/*  77  Num Lock      locks */
  SDLK_PAGEDOWN,	/*  78  PgDn          */
  SDLK_QUOTE,		/*  79  '/"  '/@      EN:ok,SDL_27   JP:nokey */
  -1,			/*  80  Shift Lock - only on BBC/Master */
  SDLK_s,		/*  81  S             */
  SDLK_c,		/*  82  C             */
  SDLK_g,		/*  83  G             */
  SDLK_h,		/*  84  H             */
  SDLK_n,		/*  85  N             */
  SDLK_l,		/*  86  L             */
  SDLK_SEMICOLON,	/*  87  ;             EN:ok,SDL_3B   JP:ok,SLD_3B */
  SDLK_RIGHTBRACKET,	/*  88  ]             EN:ok,SDL_5D   JP:wrong,SLD_5C */
  SDLK_DELETE,		/*  89  Delete        */
  SDLK_BACKSLASH,	/*  90  Keypad #  #/~ EN:ok,SDL_5C   JP:no key */
  SDLK_KP_MULTIPLY,	/*  91  Keypad *      EN:ok          JP:ok */
  -1,			/*  92  Keypad ,      VK_SEPARATOR SDLK_KP_COMMA */
  SDLK_EQUALS,		/*  93  =/+           EN:ok,SDL_3D   JP:no key */
  SDLK_LESS,		/*  94  Left  \|      EN:ok,SDL_3C   JP:no key */
  -1,			/*  95  Right \_      EN:no key      JP:wrong,no response */
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
  -1,			/* 109  No Convert    EN:no key      JP:no response */
  -1,			/* 110  Convert       EN:no key      JP:no response */
  -1,			/* 111  Kana          EN:no key      JP:no response */
  SDLK_ESCAPE,		/* 112  Escape        */
  SDLK_F1,		/* 113  F1            */
  SDLK_F2,		/* 114  F2            */
  SDLK_F3,		/* 115  F3            */
  SDLK_F5,		/* 116  F5            */
  SDLK_F6,		/* 117  F6            */
  SDLK_F8,		/* 118  F8            */
  SDLK_F9,		/* 119  F9            */
  SDLK_LESS,		/* 120  \|            EN:ok,SDL_3C   JP:wrong,no response */
  SDLK_RIGHT,		/* 121  Right         */
  SDLK_KP4,		/* 122  Keypad 4      */
  SDLK_KP5,		/* 123  Keypad 5      */
  SDLK_KP2,		/* 124  Keypad 2      */
  SDLK_LSUPER,		/* 125  Left Windows  */
  SDLK_RSUPER,		/* 126  Right Windows */
  SDLK_MENU,		/* 127  Windows Menu  */
  -1			/* 128  No key        */
};
#else

#if defined(TARGET_DJGPP) || defined(TARGET_MINGW) || defined(TARGET_WIN32) || defined(TARGET_BCC32)
#define KBD_PC 1
#ifndef VK_SHIFT
#include "keysym.h"
#endif

/* Lookup table from jgh 'console' library */
unsigned char inkeylookup[]={
VK_SHIFT,	/* -001  Shift        */
VK_CONTROL,	/* -002  Ctrl         */
VK_MENU,	/* -003  Alt          */
VK_LSHIFT,	/* -004  Left Shift   */
VK_LCONTROL,	/* -005  Left Ctrl    */
VK_LMENU,	/* -006  Left Alt     */
VK_RSHIFT,	/* -007  Right Shift  */
VK_RCONTROL,	/* -008  Right Ctrl   */
VK_RMENU,	/* -009  Right Alt    */
VK_LBUTTON,	/* -010  Mouse Select */
VK_RBUTTON,	/* -011  Mouse Menu   */
VK_MBUTTON,	/* -012  Mouse Adjust */
0,		/* -013  FN           */
0,		/* -014               */
0,		/* -015               */
0,		/* -016               */
'Q',		/* -017  Q            */
'3',		/* -018  3            */
'4',		/* -019  4            */
'5',		/* -020  5            */
VK_F4,		/* -021  F4           */
'8',		/* -022  8            */
VK_F7,		/* -023  F7           */
0xbd,		/* -024  -            */
0,		/* -025  ^            */
VK_LEFT,	/* -026  Left         */
VK_NUMPAD6,	/* -027  Keypad 6     */
VK_NUMPAD7,	/* -028  Keypad 7     */
VK_F11,		/* -029  F11          */
VK_F12,		/* -030  F12          */
VK_F10,		/* -031  F10          */
VK_SCROLL,	/* -032  Scroll Lock  */
VK_SNAPSHOT,	/* -033  F0/Print     */
'W',		/* -034  W            */
'E',		/* -035  E            */
'T',		/* -036  T            */
'7',		/* -037  7            */
'I',		/* -038  I            */
'9',		/* -039  9            */
'0',		/* -040  0            */
0xbd,		/* -041  _            */
VK_DOWN,	/* -042  Down         */
VK_NUMPAD8,	/* -043  Keypad 8     */
VK_NUMPAD9,	/* -044  Keypad 9     */
VK_PAUSE,	/* -045  Break        */
0xdf,		/* -046  `/~/?        */
0,		/* -047  UKP/Yen      */
VK_BACK,	/* -048  Backspace    */
'1',		/* -049  1            */
'2',		/* -050  2            */
'D',		/* -051  D            */
'R',		/* -052  R            */
'6',		/* -053  6            */
'U',		/* -054  U            */
'O',		/* -055  O            */
'P',		/* -056  P            */
0xdb,		/* -057  [            */
VK_UP,		/* -058  Up           */
VK_ADD,		/* -059  Keypad +     */
VK_SUBTRACT,	/* -060  Keypad -     */
VK_RETURN,	/* -061  Keypad Enter - same as Return */
VK_INSERT,	/* -062  Insert       */
VK_HOME,	/* -063  Home         */
VK_PRIOR,	/* -064  PgUp         */
VK_CAPITAL,	/* -065  Caps Lock    */
'A',		/* -066  A            */
'X',		/* -067  X            */
'F',		/* -068  F            */
'Y',		/* -069  Y            */
'J',		/* -070  J            */
'K',		/* -071  K            */
0xc0,		/* -072  @            */
0,		/* -073  :            */
VK_RETURN,	/* -074  Return - Same as Keypad Enter */
VK_DIVIDE,	/* -075  Keypad /     */
VK_DECIMAL,	/* -076  Keypad Del   */
VK_DECIMAL,	/* -077  Keypad .     */
VK_NUMLOCK,	/* -078  Num Lock     */
VK_NEXT,	/* -079  PgDn         */
0xc0,		/* -080  '/"  '/@     */
0,		/* -081  Shift Lock   */
'S',		/* -082  S            */
'C',		/* -083  C            */
'G',		/* -084  G            */
'H',		/* -085  H            */
'N',		/* -086  N            */
'L',		/* -087  L            */
0xba,		/* -088  ;            */
0xdd,		/* -089  ]            */
VK_DELETE,	/* -090  Delete       */
0xde,		/* -091  Keypad # #/~ */
VK_MULTIPLY,	/* -092  Keypad *     */
VK_SEPARATOR,	/* -093  Keypad ,     */
0xbb,		/* -094  =/+          */
0xdc,		/* -095  Left \ |     */
0xe2,		/* -096  Right \ _    */
VK_TAB,		/* -097  TAB          */
'Z',		/* -098  Z            */
' ',		/* -099  Space        */
'V',		/* -100  V            */
'B',		/* -101  B            */
'M',		/* -102  M            */
0xbc,		/* -103  ,            */
0xbe,		/* -104  .            */
0xbf,		/* -105  /            */
VK_END,		/* -106  Copy/End     */
VK_NUMPAD0,	/* -107  Keypad 0     */
VK_NUMPAD1,	/* -108  Keypad 1     */
VK_NUMPAD3,	/* -109  Keypad 3     */
VK_NONCONVERT,	/* -110  NoConvert    */
VK_CONVERT,	/* -111  Convert      */
VK_KANA,	/* -112  Kana         */
VK_ESCAPE,	/* -113  Escape       */
VK_F1,		/* -114  F1           */
VK_F2,		/* -115  F2           */
VK_F3,		/* -116  F3           */
VK_F5,		/* -117  F5           */
VK_F6,		/* -118  F6           */
VK_F8,		/* -119  F8           */
VK_F9,		/* -120  F9           */
0xdc,		/* -121  \ |          */
VK_RIGHT,	/* -122  Right        */
VK_NUMPAD4,	/* -123  Keypad 4     */
VK_NUMPAD5,	/* -124  Keypad 5     */
VK_NUMPAD2,	/* -125  Keypad 2     */
VK_LWIN,	/* -126  WinLeft      */
VK_RWIN,	/* -127  WinRight     */
VK_APPS};	/* -128  WinMenu      */
#endif

#endif
