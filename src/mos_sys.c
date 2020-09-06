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
** This file defines SWI calls that are implemented in Matrix Brandy.
** We don't actually implement a Software Interrupt, however these
** are the implemented system calls available through the BASIC "SYS"
** command.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "target.h"
#if defined(TARGET_UNIX) || defined(TARGET_MINGW)
#ifndef __USE_GNU
#define __USE_GNU
#endif /* USE_GNU */
#include <dlfcn.h>
#endif /* UNIX || MINGW */
#include "common.h"
#include "errors.h"
#include "basicdefs.h"
#include "mos.h"
#include "mos_sys.h"
#include "screen.h"
#include "keyboard.h"
#ifdef USE_SDL
#include "SDL.h"
#include "graphsdl.h"
#endif
#ifdef TARGET_MINGW
#include <minwindef.h>
#include <libloaderapi.h>
#include <psapi.h>
#endif

typedef struct {
  uint32 model;
  uint32 boardtype;
} boardtypes;
typedef struct {
  uint32 boardtype;
  uint32 newtype;
} gpio2rpistruct;

/* The RISC OS GPIO module doesn't define board numbers > 19 to the best of my knowledge */
static boardtypes boards[]={
 {   0x0002, 11},
 {   0x0003, 11},
 {   0x0004, 12},
 {   0x0005, 12},
 {   0x0006, 12},
 {   0x0007, 13},
 {   0x0008, 13},
 {   0x0009, 13},
 {   0x000D, 12},
 {   0x000E, 12},
 {   0x000F, 12},
 {   0x0010, 17},
 {   0x0011, 18},
 {   0x0012, 16},
 {   0x0013, 17},
 {   0x0014, 18},
 {   0x0015, 16},
 { 0x900032, 17},
 { 0xA01041, 19},
 { 0xA21041, 19},
 { 0xA22042, 19},
 { 0x900092, 20}, /* Pi Zero - not defined in RISC OS GPIO module */
 { 0x900093, 20},
 { 0x9000C1, 21}, /* Pi Zero W - not defined in RISC OS GPIO module */
 { 0xA02082, 22}, /* RasPi 3 Model B - not defined in RISC OS GPIO module */
 { 0xA22082, 22},
 { 0xA020D3, 23}, /* RasPi 3 Model B+ - not defined in RISC OS GPIO module */
 { 0xC03111, 25}, /* RasPi 4 */
 { 0xFFFFFFFF,0}, /* End of list */
};

static gpio2rpistruct rpiboards[]={
 { 11,  1},
 { 12,  1},
 { 13,  0},
 { 16,  2},
 { 17,  3},
 { 18,  6},
 { 19,  4},
 { 20,  9}, /* Items from here down not in RISC OS GPIO module */
 { 21, 12},
 { 22,  8},
 { 23, 13},
 { 24, 14},
 { 25, 16},
 { 26, 17},
 {255,255},
};

char outstring[65536];

static char*ostype=BRANDY_OS;
#if defined(__i386__)
static char*cputype="x86";
#elif defined(__x86_64__)
static char*cputype="x86-64";
#elif defined(__ARM_EABI__)
static char*cputype="ARM";
#else
static char*cputype="Unknown";
#endif

static uint32 mossys_getboardfrommodel(uint32 model) {
  int32 ptr;
  for (ptr=0; boards[ptr].model!=0xFFFFFFFF; ptr++) {
    if (boards[ptr].model == model) break;
  }
  if (boards[ptr].model==0xFFFFFFFF) return 0;
  return (boards[ptr].boardtype);
}

#ifdef TARGET_MINGW
static void *rtrdlsym (void *handle, const char *symbol) {
  void *procaddr ;
  HMODULE modules[100] ;
  long unsigned int i, needed ;
  EnumProcessModules ((HANDLE)-1, modules, sizeof (modules), &needed) ; /* was K32EnumProcessModules */
  for (i = 0; i < needed / sizeof (HMODULE); i++) {
    procaddr = GetProcAddress (modules[i], symbol) ;
    if (procaddr != NULL) break ;
  }
  return procaddr ;
}
#endif

void *get_dladdr(size_t nameptr, int32 xflag) {
  void *dlsh;
#ifndef TARGET_MINGW
  char *errcond;
#endif

  dlerror(); /* Flush the error state */
#ifdef TARGET_MINGW
  *(void **)(&dlsh)=rtrdlsym(RTLD_DEFAULT, (void *)nameptr);
  if (dlsh == NULL) { 
    if (!xflag) error(ERR_DL_NOSYM, "Symbol not found");
    dlsh = (void *)-1;
  }
#else
  *(void **)(&dlsh)=dlsym(RTLD_DEFAULT, (void *)nameptr);
  errcond=dlerror();
  if (errcond != NULL) { 
    if (!xflag) error(ERR_DL_NOSYM, errcond);
    dlsh = (void *)-1;
  }
#endif
  return (dlsh);
}

static uint32 gpio2rpi(uint32 boardtype) {
  int32 ptr;
  for (ptr=0; rpiboards[ptr].boardtype!=255; ptr++) {
    if (rpiboards[ptr].boardtype == boardtype) break;
  }
  return (rpiboards[ptr].newtype);
}

static uint32 rpi2gpio(uint32 newtype) {
  int32 ptr;
  for (ptr=0; rpiboards[ptr].newtype!=255; ptr++) {
    if (rpiboards[ptr].newtype == newtype) break;
  }
  return (rpiboards[ptr].boardtype);
}

/* This function handles the SYS calls for the Raspberry Pi GPIO.
** This implementation is local to Brandy.
*/
#ifdef TARGET_RISCOS
static void mos_rpi_gpio_sys(int32 swino, int32 inregs[], int32 outregs[], int32 xflag) {
#else
static void mos_rpi_gpio_sys(int64 swino, int64 inregs[], int64 outregs[], int32 xflag) {
#endif
  if (!matrixflags.gpio) {
    if (!xflag) error(ERR_NO_RPI_GPIO);
    return;
  }

  switch (swino) {
    case SWI_GPIO_ReadMode:
    case SWI_RaspberryPi_GetGPIOPortMode:
      outregs[0]=(*(matrixflags.gpiomemint + (inregs[0]/10)) >> ((inregs[0]%10)*3)) & 7;
      break;
    case SWI_GPIO_WriteMode:
    case SWI_RaspberryPi_SetGPIOPortMode:
      matrixflags.gpiomemint[(inregs[0]/10)] = (matrixflags.gpiomemint[(inregs[0]/10)] & ~(7<<((inregs[0]%10)*3))) | (inregs[1]<<((inregs[0]%10)*3));
      break;
    case SWI_RaspberryPi_SetGPIOPortPullUpDownMode:
      matrixflags.gpiomemint[37] = inregs[1];
      usleep(50);
      matrixflags.gpiomemint[38+(inregs[0]>>5)] = (1<<(inregs[0]&0x1F));
      usleep(50);
      matrixflags.gpiomemint[37] = 0;
      usleep(50);
      matrixflags.gpiomemint[38+(inregs[0]>>5)] = 0;
      break;
    case SWI_GPIO_ReadData:
    case SWI_RaspberryPi_ReadGPIOPort:
      outregs[0]=(matrixflags.gpiomemint[13 + (inregs[0]>>5)] & (1<<(inregs[0]&0x1F))) ? 1 : 0;
      break;
    case SWI_GPIO_WriteData:
    case SWI_RaspberryPi_WriteGPIOPort:
      if (inregs[1] == 0) matrixflags.gpiomemint[10 + (inregs[0]>>5)] = (1<<(inregs[0]&0x1F));
      else                matrixflags.gpiomemint[7 + (inregs[0]>>5)] = (1<<(inregs[0]&0x1F));
      break;
    default: /* Defined but unimplemented GPIO SWIs */
      error(ERR_SWINUMNOTKNOWN, swino);
  }
}

/* This is the handler for almost all SYS calls on non-RISC OS platforms.
** OS_CLI, OS_Byte, OS_Word and OS_SWINumberFromString are in mos.c
*/
#ifdef TARGET_RISCOS
void mos_sys_ext(int32 swino, int32 inregs[], int32 outregs[], int32 xflag, int32 *flags) {
#else
void mos_sys_ext(int64 swino, int64 inregs[], int64 outregs[], int32 xflag, int64 *flags) {
#endif
  int32 a;
  FILE *file_handle;

  memset(outstring,0,65536); /* Clear the output string buffer */
  if ((swino >= 256) && (swino <= 511)) { /* Handle the OS_WriteI block */
    inregs[0]=swino-256;
    swino=SWI_OS_WriteC;
  }
  switch (swino) {
#ifndef TARGET_RISCOS
    case SWI_OS_WriteC:
      outregs[0]=inregs[0];
      if ((inregs[1]==42) && (inregs[2]==42)) {
        fprintf(stderr,"%c\r\n", (int32)(inregs[0] & 0xFF));
      } else {
        emulate_vdu(inregs[0] & 0xFF);
      }
      break;
    case SWI_OS_Write0: /* This is extended in Brandy - normally all args apart
			   from R0 are ignored; in Brandy, if R1 and R2 are set
			   to 42, the text is output to the controlling terminal. */
      outregs[0]=inregs[0]+1+strlen((char *)(size_t)inregs[0]);
      if ((inregs[1]==42) && (inregs[2]==42)) {
        fprintf(stderr,"%s\r\n", (char *)(size_t)inregs[0]);
      } else {
        emulate_printf("%s", (char *)(size_t)inregs[0]);
      }
      break;
    case SWI_OS_NewLine:
      emulate_printf("\r\n"); break;
    case SWI_OS_ReadC:
#ifdef NEWKBD
      outregs[0]=kbd_get() & 0xFF; break;
#else
      outregs[0]=emulate_get(); break;
#endif
#ifdef NEWKBD
    case SWI_OS_ReadLine:
// RISC OS method is to tweek entry parameters then drop into ReadLine32
// R0=b31-b28=flags, b27-b0=address
// R1=length (buffer size-1)
// R2=lowest acceptable character
// R3=highest acceptable character
// R4=b31-b24=reserved, b23-b16=reserved, b15-b8=reserved, b7-b0=echochar
//
      inregs[4]=(inregs[4] & 0x00FFFFFF) | (inregs[0] & 0xFF000000);
      inregs[0]=(inregs[0] & 0x00FFFFFF);	/* Move flags to R4			*/
    case SWI_OS_ReadLine32:
// R0=address
// R1=length (buffer size-1)
// R2=lowest acceptable character
// R3=highest acceptable character
// R4=b31-b24=flags, b23-b16=reserved, b15-b8=reserved, b7-b0=echochar
//
      *outstring='\0';
//                       addr   length        lochar           hichar                     flags  echo
      a=kbd_readline(outstring, inregs[1]+1, (inregs[2]<<8) | (inregs[3]<<16) | (inregs[4] & 0xFF0000FF));
      outregs[1]=a;				/* Returned length			*/
						/* Should also set Carry if Escape	*/
      outregs[0]=(int64)(size_t)outstring;
      break;
#else
    case SWI_OS_ReadLine:
      *outstring='\0';
      (void)emulate_readline(outstring, inregs[1], (inregs[0] & 0x40000000) ? (inregs[4] & 0xFF) : 0);
      a=strlen(outstring);
      outregs[1]=a;
      outregs[0]=out64;
      break;
    case SWI_OS_ReadLine32:
      vptr=outstring;
      *vptr='\0';
      (void)emulate_readline(vptr, inregs[1], (inregs[4] & 0x40000000) ? (inregs[4] & 0xFF) : 0);
      a=outregs[1]=strlen(outstring);
      outregs[0]=(int64)(size_t)outstring;
      break;
#endif
    case SWI_OS_GetEnv:
      outregs[0]=-1;
      outregs[1]=(size_t)basicvars.end;
      outregs[2]=-1;
      break;
    case SWI_OS_UpdateMEMC:
      break; /* Recognise, but do nothing with it */
    case SWI_OS_Mouse:
      mos_mouse(outregs);
      break;
    case SWI_OS_ReadModeVariable:
      outregs[0]=inregs[0];
      outregs[1]=inregs[1];
#ifdef USE_SDL
      outregs[2]=readmodevariable(inregs[0],inregs[1]);
#else
      outregs[2]=0;
#endif
      break;
    case SWI_OS_ReadVduVariables:
#ifdef USE_SDL
      {
	int32 *ptra, *ptrb;
	ptra = (int32 *)(size_t)inregs[0];
	ptrb = (int32 *)(size_t)inregs[1];
	while (*ptra != -1) {
	  *ptrb = readmodevariable(-1,*ptra);
	  ptra++;
	  ptrb++;
	}
      }
#endif
      break;
    case SWI_OS_ReadMonotonicTime:
      outregs[0]=mos_centiseconds() - basicvars.monotonictimebase;
      outregs[1]=basicvars.monotonictimebase;
      break;
    case SWI_OS_Plot:
      emulate_plot(inregs[0], inregs[1], inregs[2]);
      break;
    case SWI_OS_WriteN:	/* This is extended in Brandy - normally only R0 and R1
			   are acted upon; in Brandy, if R2 is set to 42, the
			   characters are output to the controlling terminal. */
      outregs[0]=inregs[0];
      if (inregs[2]==42) {
        for (a=0; a<inregs[1]; a++) fprintf(stderr,"%c", *((byte *)(size_t)inregs[0]+a));
      } else {
        for (a=0; a<inregs[1]; a++) emulate_vdu(*((byte *)(size_t)inregs[0]+a));
      }
      break;
    case SWI_OS_ScreenMode:
#ifdef USE_SDL
      outregs[0]=inregs[0];
      outregs[1]=inregs[1];
      switch (inregs[0]) {
	case 0: 	emulate_mode(inregs[1]);break;
	case 1: 	outregs[1]=emulate_modefn();break;
	case 7: 	outregs[1]=get_maxbanks();break; /* MAXBANKS defined in graphsdl.c */
	case 8: 	osbyte113(inregs[1]);break;
	case 9: 	osbyte112(inregs[1]);break;
	case 10:	screencopy(inregs[1], inregs[2]);break;
      }
#endif
      break;
    case SWI_ColourTrans_SetGCOL:
      outregs[0]=emulate_gcolrgb(inregs[4], (inregs[3] & 0x80), ((inregs[0] >> 8) & 0xFF), ((inregs[0] >> 16) & 0xFF), ((inregs[0] >> 24) & 0xFF));
      outregs[2]=0; outregs[3]=inregs[3] & 0x80; outregs[4]=inregs[4];
      break;
    case SWI_ColourTrans_GCOLToColourNumber:
      inregs[0]=inregs[0] & 0xFF; /* Only care about the bottom 8 bits */
      outregs[0]=((inregs[0] & 0x87) + ((inregs[0] & 0x38) << 1) + ((inregs[0] & 0x40) >> 3));
      break;
    case SWI_ColourTrans_ColourNumberToGCOL:
      inregs[0]=inregs[0] & 0xFF; /* Only care about the bottom 8 bits */
      outregs[0]=((inregs[0] & 0x87) + ((inregs[0] & 0x70) >> 1) + ((inregs[0] & 8) << 3));
      break;
    case SWI_ColourTrans_SetTextColour:
      outregs[0]=emulate_setcolour((inregs[3] & 0x80), ((inregs[0] >> 8) & 0xFF), ((inregs[0] >> 16) & 0xFF), ((inregs[0] >> 24) & 0xFF));
      break;
#endif /* not TARGET_RISCOS */
    case SWI_Brandy_Version:
      strncpy(outstring,BRANDY_OS,64);
#ifdef TARGET_RISCOS
      outregs[4]=(size_t)outstring;
#else
      outregs[4]=(int64)(size_t)outstring;
#endif
      outregs[0]=atoi(BRANDY_MAJOR); outregs[1]=atoi(BRANDY_MINOR); outregs[2]=atoi(BRANDY_PATCHLEVEL);
#ifdef BRANDY_GITCOMMIT
      outregs[3]=strtol(BRANDY_GITCOMMIT,NULL,16);
#else
      outregs[3]=0;
#endif
#ifdef USE_SDL
      outregs[5]=1;
#else
      outregs[5]=0;
#endif
#ifdef TARGET_RISCOS
      outregs[6]=0x9ABCDEF0;
#else
      outregs[6]=0x123456789ABCDEF0ll;
#endif
#if defined(__LP64__) || defined(__WIN64__)
      outregs[7]=1;
#else
      outregs[7]=0;
#endif
      break;
    case SWI_Brandy_Swap16Palette:
#ifdef USE_SDL
      swi_swap16palette();
#endif
      break;
    case SWI_Brandy_GetVideoDriver:
#ifdef USE_SDL
      SDL_VideoDriverName(outstring, 64);
      outregs[2]=(size_t)matrixflags.modescreen_ptr;
      outregs[3]=matrixflags.modescreen_sz;
      outregs[4]=matrixflags.mode7fb;
      outregs[5]=(size_t)matrixflags.surface;
      outregs[6]=(size_t)matrixflags.surface->format;
#else
      strncpy(outstring,"no_sdl",64);
      outregs[2] = 0;
      outregs[3] = 0;
      outregs[4] = 0;
      outregs[5] = 0;
      outregs[6] = 0;
#endif
#ifdef TARGET_RISCOS
      strncpy(outstring,"riscos",64);
#endif
      outregs[1]=strlen(outstring);
      outregs[0]=(int64)(size_t)outstring;
      break;
    case SWI_Brandy_SetFailoverMode:
      matrixflags.failovermode=inregs[0];
      break;
    case SWI_Brandy_AccessVideoRAM:
#ifdef USE_SDL
      /* R0=0 to read into R2 from offset R1, R0 nonzero to write R2 into offset R1 */
      if (inregs[1] < matrixflags.modescreen_sz) {
	if (inregs[0]==0) {
	  outregs[2]=*(uint32 *)(matrixflags.modescreen_ptr+(inregs[1]*4));
	} else {
	  *(uint32 *)(matrixflags.modescreen_ptr+(inregs[1]*4))=inregs[2];
	  refresh_location(inregs[1]);
	}
      }
#endif
      break;
    case SWI_Brandy_INTusesFloat:
        matrixflags.int_uses_float = inregs[0];
      break;
    case SWI_Brandy_LegacyIntMaths:
        matrixflags.legacyintmaths = inregs[0];
      break;
    case SWI_Brandy_Hex64:
        matrixflags.hex64 = inregs[0];
        break;
    case SWI_Brandy_DELisBS:
        matrixflags.delcandelete = inregs[0];
      break;
    case SWI_Brandy_PseudovarsUnsigned:
        matrixflags.pseudovarsunsigned = inregs[0];
      break;
    case SWI_Brandy_TekEnabled:
        matrixflags.tekenabled = inregs[0];
        matrixflags.tekspeed = inregs[1];
      break;
    case SWI_Brandy_uSleep:
        usleep(inregs[0]);
      break;
    case SWI_Brandy_dlopen:
#if defined(TARGET_UNIX) || defined(TARGET_MINGW)
      outregs[0]=(size_t)dlopen((char *)(size_t)inregs[0], RTLD_LAZY);
#else
      if (!xflag) error(ERR_DL_NODL);
      outregs[0]=0;
#endif
      break;
    case SWI_Brandy_dlcall:
#if defined(TARGET_UNIX) || defined(TARGET_MINGW)
      {
        size_t (*dlsh)(size_t, ...);

        dlerror(); /* Flush the error state */
        *(void **)(&dlsh)=get_dladdr(inregs[0], xflag);
        if (dlsh != (void *)-1) outregs[0]=(*dlsh)((size_t)inregs[1], (size_t)inregs[2], (size_t)inregs[3], (size_t)inregs[4], (size_t)inregs[5], (size_t)inregs[6], (size_t)inregs[7], (size_t)inregs[8], (size_t)inregs[9]);
      }
#else
      if (!xflag) error(ERR_DL_NODL);
      outregs[0]=0;
#endif
      break;
    case SWI_Brandy_MAlloc:
        outregs[0]=(size_t)malloc((size_t)inregs[0]);
        if (!xflag && outregs[0] == 0) error(ERR_NOMEMORY);
      break;
    case SWI_Brandy_Free:
        if ((size_t)inregs[0] == (size_t)basicvars.workspace) error(ERR_ADDREXCEPT); /* Don't be silly. */
        if (matrixflags.gpio) {
          if ((size_t)inregs[0] == (size_t)matrixflags.gpiomem) error(ERR_ADDREXCEPT); /* Don't deallocate GPIO mmap */
        }
#ifdef USE_SDL
        if ((size_t)inregs[0] == (size_t)matrixflags.modescreen_ptr) error(ERR_ADDREXCEPT); /* Don't allow deallocation of screen memory */
#endif
        free((void *)(size_t)inregs[0]);
      break;
    case SWI_Brandy_BitShift64:
        matrixflags.bitshift64 = inregs[0];
        break;
    case SWI_Brandy_Platform:
        outregs[0]=(int64)(size_t)ostype;
        outregs[1]=(int64)(size_t)cputype;
#if defined(__LP64__) || defined(__WIN64__)
        outregs[2]=1;
#else
        outregs[2]=0;
#endif
#ifdef USE_SDL
        outregs[3]=1;
#else
        outregs[3]=0;
#endif
        outregs[4]=(MACTYPE >> 8);
        outregs[5]=LEGACY_OSVERSION;
#ifdef TARGET_MINGW
        outregs[6]=getpid();
#else
        outregs[6]=getpid() + ((uint64)getppid() << 32);
#endif
        break;
    case SWI_Brandy_RefreshInterval:
#ifdef USE_SDL
        set_refresh_interval(inregs[0]-1);
#endif
        break;
    case SWI_Brandy_MouseEventExpire:
#ifdef USE_SDL
        set_mouseevent_expiry((uint32)inregs[0]);
#endif
        break;
    case SWI_Brandy_dlgetaddr:
#if defined(TARGET_UNIX) || defined(TARGET_MINGW)
        outregs[0]=(size_t)get_dladdr(inregs[0], xflag);
#else
        if (!xflag) error(ERR_DL_NODL);
        outregs[0]=0;
#endif
        break;
    case SWI_Brandy_dlcalladdr:
#if defined(TARGET_UNIX) || defined(TARGET_MINGW)
      {
        size_t (*dlsh)(size_t, ...);

        dlerror(); /* Flush the error state */
        *(void **)(&dlsh)=(void *)(size_t)inregs[0];
        if (dlsh != (void *)-1) {
          outregs[0]=(*dlsh)((size_t)inregs[1], (size_t)inregs[2], (size_t)inregs[3], (size_t)inregs[4], (size_t)inregs[5], (size_t)inregs[6], (size_t)inregs[7], (size_t)inregs[8], (size_t)inregs[9]);
        } else {
          error(ERR_ADDREXCEPT);
        }
      }
#else
      if (!xflag) error(ERR_DL_NODL);
      outregs[0]=0;
#endif
      break;
    case SWI_RaspberryPi_GPIOInfo:
      outregs[0]=matrixflags.gpio; outregs[1]=(size_t)matrixflags.gpiomem;
      break;
    case SWI_GPIO_GetBoard:
      file_handle=fopen("/proc/device-tree/model","r");
      outregs[0]=0;
      outregs[2]=0;
      outregs[3]=0;
      if (NULL == file_handle) {
	strncpy(outstring, "No machine type detected",25);
      } else {
	if(fgets(outstring, 65534, file_handle))
	  ;
	fclose(file_handle);
	file_handle=fopen("/proc/device-tree/system/linux,revision","r");
	if (NULL != file_handle) {
	  outregs[2]=(fgetc(file_handle) << 24);
	  outregs[2]+=(fgetc(file_handle) << 16);
	  outregs[2]+=(fgetc(file_handle) << 8);
	  outregs[2]+=fgetc(file_handle);
	  if (outregs[2] < 256) {
	    outregs[0]=mossys_getboardfrommodel(outregs[2]);
	    outregs[3]=gpio2rpi(outregs[0]);
	  } else {
	    outregs[3]=((outregs[2] & 0xFF0) >> 4);
	    outregs[0]=rpi2gpio(outregs[3]);
	  }
	  fclose(file_handle);
	}
      }
      outregs[1]=(int64)(size_t)outstring;
      break;
    /* ALL OTHER GPIO stuff down here */
    case SWI_RaspberryPi_GetGPIOPortMode:
    case SWI_RaspberryPi_SetGPIOPortMode:
    case SWI_RaspberryPi_SetGPIOPortPullUpDownMode:
    case SWI_RaspberryPi_ReadGPIOPort:
    case SWI_RaspberryPi_WriteGPIOPort:
    case SWI_GPIO_ReadData:
    case SWI_GPIO_WriteData:
    case SWI_GPIO_ReadOE:
    case SWI_GPIO_WriteOE:
    case SWI_GPIO_ExpAsGPIO:
    case SWI_GPIO_ExpAsUART:
    case SWI_GPIO_ExpAsMMC:
    case SWI_GPIO_ReadMode:
    case SWI_GPIO_WriteMode:
    case SWI_GPIO_ReadLevel0:
    case SWI_GPIO_WriteLevel0:
    case SWI_GPIO_ReadLevel1:
    case SWI_GPIO_WriteLevel1:
    case SWI_GPIO_ReadRising:
    case SWI_GPIO_WriteRising:
    case SWI_GPIO_ReadFalling:
    case SWI_GPIO_WriteFalling:
    case SWI_GPIO_ReadExp32:
    case SWI_GPIO_WriteExp32:
    case SWI_GPIO_ReadExpOE32:
    case SWI_GPIO_WriteExpOE32:
    case SWI_GPIO_ReadEvent:
    case SWI_GPIO_WriteEvent:
    case SWI_GPIO_ReadAsync:
    case SWI_GPIO_WriteAsync:
    case SWI_GPIO_FlashOn:
    case SWI_GPIO_FlashOff:
    case SWI_GPIO_Info:
    case SWI_GPIO_I2CInfo:
    case SWI_GPIO_LoadConfig:
    case SWI_GPIO_ReadConfig:
    case SWI_GPIO_EnableI2C:
    case SWI_GPIO_RescanI2C:
      mos_rpi_gpio_sys(swino, inregs, outregs, xflag);
      break;
    default:
      error(ERR_SWINUMNOTKNOWN, swino);
  }
}
