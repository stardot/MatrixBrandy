#ifndef MOS_SWINUMS_H
#define MOS_SWINUMS_H

typedef struct {
  int32 swinum;		/* RISC OS SWI number */
  char *swiname;	/* SWI Name */
} switable;

/* Source: http://www.riscos.com/support/developers/prm_index/numswilist.html */

/* Used by mos.c: int32 mos_getswinum(char *name, int32 length) */

#define SWI_OS_WriteC					0x00
#define SWI_OS_Write0					0x02
#define SWI_OS_NewLine					0x03
#define SWI_OS_ReadC					0x04
#define SWI_OS_CLI					0x05
#define SWI_OS_Byte					0x06
#define SWI_OS_Word					0x07
#define SWI_OS_ReadLine					0x0E
#define SWI_OS_SWINumberFromString			0x39
#define SWI_OS_ReadLine32				0x7D

#define SWI_ColourTrans_SetGCOL				0x40743
#define SWI_ColourTrans_SetTextColour			0x40761

#define SWI_Brandy_Version				0x140000

#define SWI_RaspberryPi_GPIOInfo			0x140100
#define SWI_RaspberryPi_GetGPIOPinMode			0x140101
#define SWI_RaspberryPi_SetGPIOPinMode			0x140102
#define SWI_RaspberryPi_SetGPIOPinPullUpDownMode	0x140103
#define SWI_RaspberryPi_ReadGPIOPin			0x140104
#define SWI_RaspberryPi_WriteGPIOPin			0x140105

static switable swilist[] = {
	{SWI_OS_WriteC,					"OS_WriteC"},
	{SWI_OS_Write0,					"OS_Write0"},
	{SWI_OS_NewLine,				"OS_NewLine"},
	{SWI_OS_ReadC,					"OS_ReadC"},
	{SWI_OS_CLI,					"OS_CLI"},
	{SWI_OS_Byte,					"OS_Byte"},
	{SWI_OS_Word,					"OS_Word"},
	{SWI_OS_ReadLine,				"OS_ReadLine"},
	{SWI_OS_SWINumberFromString,			"OS_SWINumberFromString"},
	{SWI_OS_ReadLine32,				"OS_ReadLine32"},

	{SWI_ColourTrans_SetGCOL,			"ColourTrans_SetGCOL"},
	{SWI_ColourTrans_SetTextColour,			"ColourTrans_SetTextColour"},

	{SWI_Brandy_Version,				"Brandy_Version"},

	{SWI_RaspberryPi_GPIOInfo,			"RaspberryPi_GPIOInfo"},
	{SWI_RaspberryPi_GetGPIOPinMode,		"RaspberryPi_GetGPIOPinMode"},
	{SWI_RaspberryPi_SetGPIOPinMode,		"RaspberryPi_SetGPIOPinMode"},
	{SWI_RaspberryPi_SetGPIOPinPullUpDownMode,	"RaspberryPi_SetGPIOPinPullUpDownMode"},
	{SWI_RaspberryPi_ReadGPIOPin,			"RaspberryPi_ReadGPIOPin"},
	{SWI_RaspberryPi_WriteGPIOPin,			"RaspberryPi_WriteGPIOPin"},

	{0xFFFFFFFF,		"End_of_list"}
};

#endif
