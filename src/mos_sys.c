#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "common.h"
#include "target.h"
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

typedef struct {
  uint32 model;
  uint32 boardtype;
} boardtypes;

/* The RISC OS GPIO module doesn't define board numbers > 19 to the best of my knowledge */
static boardtypes boards[28]={
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
 {   0x0013, 17},
 { 0x900032, 17},
 {   0x0011, 18},
 {   0x0014, 18},
 {   0x0012, 16},
 {   0x0015, 16},
 { 0xA01041, 19},
 { 0xA21041, 19},
 { 0xA22042, 19},
 { 0x900092, 20}, /* Pi Zero - not defined in RISC OS GPIO module */
 { 0x900093, 20},
 { 0x9000C1, 21}, /* Pi Zero W - not defined in RISC OS GPIO module */
 { 0xA02082, 22}, /* RasPi 3 Model B - not defined in RISC OS GPIO module */
 { 0xA22082, 22},
 { 0xA020D3, 23}, /* RasPi 3 Model B+ - not defined in RISC OS GPIO module */
 { 0xFFFFFFFF,0}, /* End of list */
};


char outstring[65536];

static int32 mossys_getboardfrommodel(uint32 model) {
  int32 ptr;
  for (ptr=0; boards[ptr].model!=0xFFFFFFFF; ptr++) {
    if (boards[ptr].model == model) break;
  }
  if (boards[ptr].model==0xFFFFFFFF) return 0;
  return (boards[ptr].boardtype);
}

/* This function handles the SYS calls for the Raspberry Pi GPIO.
** This implementation is local to Brandy.
*/
static void mos_rpi_gpio_sys(int32 swino, int32 inregs[], int32 outregs[], int32 xflag) {
  if (!matrixflags.gpio) {
    if (!xflag) error(ERR_NO_RPI_GPIO);
    return;
  }

  switch (swino) {
    case SWI_RaspberryPi_GetGPIOPortMode:
      outregs[0]=(*(matrixflags.gpiomemint + (inregs[0]/10)) >> ((inregs[0]%10)*3)) & 7;
      break;
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
    case SWI_RaspberryPi_ReadGPIOPort:
      outregs[0]=(matrixflags.gpiomemint[13 + (inregs[0]>>5)] & (1<<(inregs[0]&0x1F))) ? 1 : 0;
      break;
    case SWI_RaspberryPi_WriteGPIOPort:
      if (inregs[1] == 0) matrixflags.gpiomemint[10 + (inregs[0]>>5)] = (1<<(inregs[0]&0x1F));
      else                matrixflags.gpiomemint[7 + (inregs[0]>>5)] = (1<<(inregs[0]&0x1F));
      break;
    default: /* SHOULD NEVER REACH HERE */
      error(ERR_SWINUMNOTKNOWN, swino);
  }
}

/* This is the handler for almost all SYS calls on non-RISC OS platforms.
** OS_CLI, OS_Byte, OS_Word and OS_SWINumberFromString are in mos.c
*/
void mos_sys_ext(int32 swino, int32 inregs[], int32 outregs[], int32 xflag, int32 *flags) {
  int32 a, v;
  FILE *file_handle;
  char *vptr;

  memset(outstring,0,65536); /* Clear the output string buffer */
  v=((long int)outstring & 0xFFFFFFFF);
  switch (swino) {
    case SWI_OS_WriteC:
      outregs[0]=inregs[0];
      emulate_vdu(inregs[0] & 0xFF);
      break;
    case SWI_OS_Write0: /* This is extended in Brandy - normally all args apart
			   from R0 are ignored; in Brandy, if R1 and R2 are set
			   to 42, the text is output to the controlling terminal. */
      outregs[0]=inregs[0]+1+strlen((char *)basicvars.offbase+inregs[0]);
      if ((inregs[1]==42) && (inregs[2]==42)) {
        fprintf(stderr,"%s\r\n", basicvars.offbase+inregs[0]);
      } else {
        emulate_printf("%s", basicvars.offbase+inregs[0]);
      }
      break;
    case SWI_OS_NewLine:
      emulate_printf("\r\n"); break;
    case SWI_OS_ReadC:
#ifdef NEWKBD
      outregs[0]=kbd_get(); break;
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
      vptr=outstring;
      *vptr='\0';
//                       addr   length        lochar           hichar                     flags  echo
      a=kbd_readline(vptr, inregs[1]+1, (inregs[2]<<8) | (inregs[3]<<16) | (inregs[4] & 0xFF0000FF));
      outregs[1]=a;				/* Returned length			*/
						/* Should also set Carry if Escape	*/
      break;
#else
    case SWI_OS_ReadLine:
      vptr=outstring;
      *vptr='\0';
      (void)emulate_readline(vptr, inregs[1], (inregs[0] & 0x40000000) ? (inregs[4] & 0xFF) : 0);
      a=strlen(vptr);
      outregs[1]=a;
      outregs[0]=v;
      break;
    case SWI_OS_ReadLine32:
      vptr=outstring;
      *vptr='\0';
      (void)emulate_readline(vptr, inregs[1], (inregs[4] & 0x40000000) ? (inregs[4] & 0xFF) : 0);
      a=outregs[1]=strlen(vptr);
      outregs[0]=v;
      break;
#endif
    case SWI_ColourTrans_SetGCOL:
      outregs[0]=emulate_gcolrgb(inregs[4], (inregs[3] & 0x80), ((inregs[0] >> 8) & 0xFF), ((inregs[0] >> 16) & 0xFF), ((inregs[0] >> 24) & 0xFF));
      outregs[2]=0; outregs[3]=inregs[3] & 0x80; outregs[4]=inregs[4];
      break;
    case SWI_ColourTrans_SetTextColour:
      outregs[0]=emulate_setcolour((inregs[3] & 0x80), ((inregs[0] >> 8) & 0xFF), ((inregs[0] >> 16) & 0xFF), ((inregs[0] >> 24) & 0xFF));
      break;
    case SWI_Brandy_Version:
      strncpy(outstring,BRANDY_OS,64);
      outregs[4]=v;
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
      break;
    case SWI_Brandy_Swap16Palette:
#ifdef USE_SDL
      swi_swap16palette();
#endif
      break;
    case SWI_Brandy_GetVideoDriver:
      vptr=outstring;
#ifdef USE_SDL
      SDL_VideoDriverName(vptr, 64);
#else
     strncpy(vptr,"no_sdl",64);
#endif
      a=outregs[1]=strlen(vptr);
      outregs[0]=v;
      break;
    case SWI_Brandy_SetFailoverMode:
      matrixflags.failovermode=inregs[0];
      break;
    case SWI_RaspberryPi_GPIOInfo:
      outregs[0]=matrixflags.gpio; outregs[1]=(matrixflags.gpiomem - basicvars.offbase);
      break;
    case SWI_GPIO_GetBoard:
      file_handle=fopen("/proc/device-tree/model","r");
      if (NULL == file_handle) {
	outregs[0]=0;
	strncpy(outstring, "No machine type detected",24);
	outregs[1]=v;
	outregs[2]=0;
      } else {
	outregs[0]=0;
	outregs[1]=v;
	outregs[2]=0;
	fgets(outstring, 65534, file_handle);
	fclose(file_handle);
	file_handle=fopen("/proc/device-tree/system/linux,revision","r");
	if (NULL != file_handle) {
	  outregs[2]=(fgetc(file_handle) << 24);
	  outregs[2]+=(fgetc(file_handle) << 16);
	  outregs[2]+=(fgetc(file_handle) << 8);
	  outregs[2]+=fgetc(file_handle);
	  outregs[0]=mossys_getboardfrommodel(outregs[2]);
	  fclose(file_handle);
	}
      }
      break;
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
