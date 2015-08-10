/*
** This file is part of the Brandy Basic V Interpreter.
** Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005 David Daniels
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
**	Target-specific declarations
*/
/*
** Crispian Daniels August 20th 2002:
**	Included a Mac OS X target for conditional compilation.
*/

#define BRANDY_VERSION "1.20.1"
#define BRANDY_DATE    "24-Sep-2014"

#ifndef __target_h
#define __target_h

/*
** Define the operating system-specific types used for integer
** and floating point types in Basic. 32-bit integer (signed
** and unsigned) and 64-bit floating point types are needed.
**
** The following are suitable for ARM and X86
*/
typedef int int32;			/* Type for 32-bit integer variables in Basic */
typedef unsigned int uint32;		/* 32-bit unsigned integer */
typedef double float64;			/* Type for 64-bit floating point variables in Basic */

/*
** The following macros define the OS under which the program is being
** compiled and run. It uses macros predefined in various compilers to
** figure this out. Alternatively, the 'TARGET_xxx' macro can be hard-
** coded here. This is the most important macro and is used to control
** the compilation of OS-specific parts of the program.
*/

#ifdef __unix
#define TARGET_UNIX
#endif

#ifdef __riscos
#define TARGET_RISCOS
#define BRANDY_OS "RISC OS"
#endif

#ifdef __NetBSD__
#define TARGET_NETBSD
#define BRANDY_OS "NetBSD"
#endif

#ifdef __FreeBSD__
#define TARGET_FREEBSD
#define BRANDY_OS "FreeBSD"
#endif

#ifdef __OpenBSD__
#define TARGET_OPENBSD
#define TARGET_UNIX
#define BRANDY_OS "OpenBSD"
#endif

#ifdef linux
#define TARGET_LINUX
#define BRANDY_OS "Linux"
#endif

/* Same as Linux, but can be treated exactly like it, see the Linux specific
 * XCASE in src/keyboard.c */
#if (defined __FreeBSD_kernel__)
#define TARGET_GNUKFREEBSD
#define BRANDY_OS "GNU/kFreeBSD"
#endif

#if (defined __GNU__)
#define TARGET_GNU
#define BRANDY_OS "GNU/Hurd"
#endif

#ifdef DJGPP
#define TARGET_DJGPP
#define BRANDY_OS "DJGPP"
#endif

#ifdef __MINGW32__
#define TARGET_MINGW
#if _WIN32_WINNT < 0x0500 /* Require Win2k or later */
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#endif
#define BRANDY_OS "MinGW"
#endif

#if defined(__LCC__) & defined(WIN32)
#define TARGET_WIN32
#define BRANDY_OS "LCC-WIN32"
#endif

#ifdef __BORLANDC__
#define TARGET_BCC32
#define BRANDY_OS "BCC"
#endif

#if defined(__GNUC__) && ( defined(__APPLE_CPP__) || defined(__APPLE_CC__) )
#define TARGET_MACOSX
#define BRANDY_OS "MacOS X"
#endif

#if defined(_AMIGA) || defined(__amigaos__)
#define TARGET_AMIGA
#define BRANDY_OS "Amiga"
#endif


#ifndef BRANDY_OS
#error Target operating system for interpreter is either missing or not supported
#endif

#define IDSTRING "Brandy Basic V Interpreter Version " BRANDY_VERSION " (" BRANDY_OS ") " BRANDY_DATE

/*
** MAXSTRING is the length of the longest string the interpreter
** allows. This value can be safely reduced but not increased
** without altering the string memory allocation code in strings.c
** 1024 is probably a sensible minimum value
*/

#define MAXSTRING 65536

/*
** DEFAULTSIZE and MINSIZE give the default and minimum Basic
** workspace sizes in bytes. DEFAULTSIZE is the amount of memory
** acquired when the interpreter first starts up and MINSIZE
** is the minimum it can be changed to.
*/

#define DEFAULTSIZE (512*1024)
#define MINSIZE (10*1024)

/*
** The ALIGN macro is used to control the sizes of blocks of
** memory allocated from the heap. They are always a multiple
** of ALIGN bytes.
**
** Note the use of 'char *': this is to make it automatically
** align things on a 32-bit or 64-bit boundary according to
** the size of a pointer (32 or 64 bits).
*/

#define ALIGN(x) ((x+sizeof(char *)-1) & -(int)sizeof(char *))

/*
** Name of editor invoked by Basic 'EDIT' command
** EDITOR_VARIABLE is the name of an environment variable that can be
**		read to find the name of the editor to use.
** DEFAULT_EDITOR is the name of the editor to use if there is no
**		environment variable.
*/

#if defined(TARGET_DJGPP) | defined(TARGET_WIN32) | defined(TARGET_BCC32) | defined(TARGET_MINGW)
#define EDITOR_VARIABLE "EDITOR"
#define DEFAULT_EDITOR "edit"
#elif defined(TARGET_LINUX) | defined(TARGET_NETBSD) | defined(TARGET_FREEBSD)\
 | defined(TARGET_OPENBSD) | defined(TARGET_GNUKFREEBSD) | defined(TARGET_GNU)
#define EDITOR_VARIABLE "EDITOR"
#define DEFAULT_EDITOR "vi"
#elif defined(TARGET_MACOSX)
#define EDITOR_VARIABLE "EDITOR"
#define DEFAULT_EDITOR "/Applications/TextEdit.app/Contents/MacOS/TextEdit"
#elif defined(TARGET_RISCOS)
#define EDITOR_VARIABLE "Brandy$$Editor"
#define DEFAULT_EDITOR "filer_run"
#elif defined(TARGET_AMIGA)
#define EDITOR_VARIABLE "EDITOR"
#define DEFAULT_EDITOR "ed"
#endif

/*
** Characters used to separate directories in names of files
** DIR_SEPS	is a string containing all the characters that can be
** 	    	be used to separate components of a file name (apart
**		from the file name's extension)
** DIR_SEP	gives the character to be used to separate directory names.
*/

#if defined(TARGET_DJGPP) | defined(TARGET_WIN32) | defined(TARGET_BCC32) | defined(TARGET_MINGW)
#define DIR_SEPS "\\/:"
#define DIR_SEP '\\'
#elif defined(TARGET_LINUX) | defined(TARGET_NETBSD) | defined(TARGET_MACOSX)\
 | defined(TARGET_FREEBSD) | defined(TARGET_OPENBSD) | defined(TARGET_GNUKFREEBSD) | defined(TARGET_GNU)
#define DIR_SEPS "/"
#define DIR_SEP '/'
#elif defined(TARGET_RISCOS)
#define DIR_SEPS ".:"
#define DIR_SEP '.'
#elif defined(TARGET_AMIGA)
#define DIR_SEPS "/:"
#define DIR_SEP '/'
#endif

/* Host type values returned by OSBYTE 0 */

#if defined(__arm)
#define MACTYPE 0x600
#elif defined(TARGET_LINUX) | defined(TARGET_NETBSD) | defined(TARGET_MACOSX)\
 | defined(TARGET_FREEBSD) | defined(TARGET_OPENBSD) | defined(TARGET_AMIGA)\
 | defined(TARGET_GNUKFREEBSD) | defined(TARGET_GNU)
#define MACTYPE 0x800
#elif defined(TARGET_DJGPP) | defined(TARGET_WIN32) | defined(TARGET_BCC32) | defined(TARGET_MINGW)
#define MACTYPE 0x2000
#endif

#endif
