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
**
** 20th August 2002 Crispian Daniels:
**	Included a Mac OS X target for conditional compilation.
**
** 04-Dec-2018 JGH: Rearranged to make for easier human parsing.
**
*/

#define BRANDY_NAME  "Matrix"
#define BRANDY_MAJOR "1"
#define BRANDY_MINOR "21"
#define BRANDY_DATE  "28 Dec 2018"
#define BRANDY_PATCHLEVEL "18"
#define BRANDY_PATCHDATE  "JGH190117"

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
typedef long long int int64;		/* Type for 64-bit integer variables */
typedef unsigned long long int uint64;	/* 64-bit unsigned integer */


/*
** The following macros define the OS under which the program is being
** compiled and run. It uses macros predefined in various compilers to
** figure this out. Alternatively, the 'TARGET_xxx' macro can be hard-
** coded here. This is the most important macro and is used to control
** the compilation of OS-specific parts of the program.
**
** BRANDY_OS is displayed by the startup and *HELP string.
** MACTYPE indicates the system, returned by OSBYTE 0, and indicates the filing system type.
**  0x0600 for directory.file/ext (eg RISC OS)
**  0x0800 for directory/file.ext (eg UNIX)
**  0x2000 for directory\file.ext (eg Win/DOS)
** OSVERSION indicates the host OS, returned by INKEY-256 and OSBYTE 129,-256.
**  These values are made up, but see beebwiki.mdfs.net/OSBYTE_&81
**
** Name of editor invoked by Basic 'EDIT' command.
** EDITOR_VARIABLE is the name of an environment variable that can be
**		read to find the name of the editor to use.
** DEFAULT_EDITOR is the name of the editor to use if there is no
**		environment variable.
**
** Characters used to separate directories in names of files
** DIR_SEPS	is a string containing all the characters that can be
** 	    	be used to separate components of a file name (apart
**		from the file name's extension).
** DIR_SEP	gives the character to be used to separate directory names.
*/

#ifdef __riscos
#define TARGET_RISCOS
#define BRANDY_OS "RISC OS"
// OSVERSION returned by OS call
#define MACTYPE   0x0600
#define EDITOR_VARIABLE "Brandy$Editor"
#define DEFAULT_EDITOR  "Filer_Run"
#define DIR_SEPS ".:"
#define DIR_SEP  '.'
#endif

#ifdef __NetBSD__
#define TARGET_NETBSD
#define TARGET_UNIX
#define BRANDY_OS "NetBSD"
#define OSVERSION 0xFE
#define MACTYPE   0x0800
#define EDITOR_VARIABLE "BRANDY$EDITOR"
#define DEFAULT_EDITOR  "vi"
#define DIR_SEPS "/"
#define DIR_SEP  '/'
#endif

#ifdef __FreeBSD__
#define TARGET_FREEBSD
#define TARGET_UNIX
#define BRANDY_OS "FreeBSD"
#define OSVERSION 0xF7
#define MACTYPE   0x0800
#define EDITOR_VARIABLE "BRANDY$EDITOR"
#define DEFAULT_EDITOR  "vi"
#define DIR_SEPS "/"
#define DIR_SEP  '/'
#endif

#ifdef __OpenBSD__
#define TARGET_OPENBSD
#define TARGET_UNIX
#define BRANDY_OS "OpenBSD"
#define OSVERSION 0xF6
#define MACTYPE   0x0800
#define EDITOR_VARIABLE "BRANDY$EDITOR"
#define DEFAULT_EDITOR  "vi"
#define DIR_SEPS "/"
#define DIR_SEP  '/'
#endif

#ifdef linux
#define TARGET_LINUX
#define TARGET_UNIX
#define BRANDY_OS "Linux"
#define OSVERSION 0xF9
#define MACTYPE   0x0800
#define EDITOR_VARIABLE "BRANDY$EDITOR"
#define DEFAULT_EDITOR  "vi"
#define DIR_SEPS "/"
#define DIR_SEP  '/'
#endif

/* Same as Linux, but can be treated exactly like it, see the Linux specific
 * XCASE in src/keyboard.c */
#ifdef __FreeBSD_kernel__
#define TARGET_GNUKFREEBSD
#define TARGET_UNIX
#define BRANDY_OS "GNU/kFreeBSD"
#define OSVERSION 0xF4
#define MACTYPE   0x0800
#define EDITOR_VARIABLE "BRANDY$EDITOR"
#define DEFAULT_EDITOR  "vi"
#define DIR_SEPS "/"
#define DIR_SEP  '/'
#endif

#ifdef __GNU__
#define TARGET_GNU
#define BRANDY_OS "GNU/Hurd"
#define OSVERSION 0xF3
#define MACTYPE   0x0800
#define EDITOR_VARIABLE "BRANDY$EDITOR"
#define DEFAULT_EDITOR  "vi"
#define DIR_SEPS "/"
#define DIR_SEP  '/'
#endif

#ifdef DJGPP
#define TARGET_DJGPP
#define TARGET_DOSWIN
#define BRANDY_OS "DJGPP"
#define OSVERSION 0xFA
#define MACTYPE   0x2000
#define EDITOR_VARIABLE "BRANDY$EDITOR"
#define DEFAULT_EDITOR  "edit"
#define DIR_SEPS "\\/:"
#define DIR_SEP  '\\'
#endif

#ifdef __MINGW32__
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600 /* Require Win7 or later */
#define TARGET_MINGW
#define TARGET_DOSWIN
#define BRANDY_OS "MinGW"
#define OSVERSION 0xFC
#define MACTYPE   0x2000
#define EDITOR_VARIABLE "BRANDY$EDITOR"
#define DEFAULT_EDITOR  "edit"
#define DIR_SEPS "\\/:"
#define DIR_SEP  '\\'
#endif

#if defined(__LCC__) & defined(WIN32)
#define TARGET_WIN32
#define TARGET_DOSWIN
#define BRANDY_OS "LCC-WIN32"
#define OSVERSION 0xFC
#define MACTYPE   0x2000
#define EDITOR_VARIABLE "BRANDY$EDITOR"
#define DEFAULT_EDITOR  "edit"
#define DIR_SEPS "\\/:"
#define DIR_SEP  '\\'
#endif

#ifdef __BORLANDC__
#define TARGET_BCC32
#define TARGET_DOSWIN
#define BRANDY_OS "BCC"
#define OSVERSION 0xFC
#define MACTYPE   0x2000
#define EDITOR_VARIABLE "BRANDY$EDITOR"
#define DEFAULT_EDITOR  "edit"
#define DIR_SEPS "\\/:"
#define DIR_SEP  '\\'
#endif

#if defined(__GNUC__) && ( defined(__APPLE_CPP__) || defined(__APPLE_CC__) )
#define TARGET_MACOSX
#define BRANDY_OS "MacOS X"
#define OSVERSION 0xF8
#define MACTYPE   0x0800
#define EDITOR_VARIABLE "BRANDY$EDITOR"
#define DEFAULT_EDITOR  "/Applications/TextEdit.app/Contents/MacOS/TextEdit"
#define DIR_SEPS "/"
#define DIR_SEP  '/'
#endif

#if defined(_AMIGA) || defined(__amigaos__)
#define TARGET_AMIGA
#define BRANDY_OS "Amiga"
#define OSVERSION 0xF5
#define MACTYPE   0x0800
#define EDITOR_VARIABLE "BRANDY$EDITOR"
#define DEFAULT_EDITOR  "ed"
#define DIR_SEPS "/:"
#define DIR_SEP  '/'
#endif

// Nothing tests for BEOS, but set up defines here to claim OSVER=&FB.
#ifdef __BEOS__
#define TARGET_BEOS
#define BRANDY_OS "BEOS"
#define OSVERSION 0xFB
// BEOS uses dir/file.ext filesystem, so MACTYPE must be %000x1xxx
// We use 8 because it is Unix-y
#define MACTYPE   0x0800
// Don't really know what the editor should be
#define EDITOR_VARIABLE "BRANDY$EDITOR"
#define DEFAULT_EDITOR  "vi"
#define DIR_SEPS "/"
#define DIR_SEP  '/'
#endif

#ifndef BRANDY_OS
#error Target operating system for interpreter is either missing or not supported
#endif

#ifdef NEWKBD
 #ifdef USE_SDL
  #define SUFFIX "/SDL/NEWKBD) "
 #else
  #define SUFFIX "/NEWKBD) "
 #endif
#else
 #ifdef USE_SDL
  #define SUFFIX "/SDL) "
 #else
  #define SUFFIX ") "
 #endif
#endif

#ifdef NODISPLAYOS
#define IDSTRING "Matrix Brandy BASIC V version " BRANDY_MAJOR "." BRANDY_MINOR "." BRANDY_PATCHLEVEL " (" BRANDY_DATE ")"
#else
#define IDSTRING "Matrix Brandy BASIC V version " BRANDY_MAJOR "." BRANDY_MINOR "." BRANDY_PATCHLEVEL " (" BRANDY_OS SUFFIX BRANDY_DATE
#endif

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

//#define DEFAULTSIZE (512*1024)
#define DEFAULTSIZE 651516
#define MINSIZE (10*1024)


/*
** The ALIGN macro is used to control the sizes of blocks of
** memory allocated from the heap. They are always a multiple
** of ALIGN bytes.
*/
#ifdef TARGET_HPUX
#define ALIGN(x) ((x+sizeof(double)-1) & -(int)sizeof(double))
#else
#define ALIGN(x) ((x+sizeof(int32)-1) & -(int)sizeof(int32))
#endif

#endif
