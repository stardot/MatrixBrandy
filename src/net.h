/*
** This file is part of the Matrix Brandy Basic VI Interpreter.
** Copyright (C) 2018-2024 Michael McConnell and contributors
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
*/
#ifndef BRANDY_NET_H
#define BRANDY_NET_H
#ifndef NONET
#include "common.h"
extern void brandynet_init();
extern int brandynet_connect(char *dest, char type, int reporterrors);
extern int brandynet_close(int handle);
extern int32 net_bget(int handle);
extern boolean net_eof(int handle);
extern int net_bput(int handle, int32 value);
extern int net_bputstr(int handle, char *string, int32 length);
#ifndef BRANDY_NOVERCHECK
extern int checkfornewer(void);
#endif
#endif /* NONET */
#endif /* BRANDY_NET_H */
