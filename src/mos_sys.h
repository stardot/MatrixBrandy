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
#define SWI_OS_UpdateMEMC				0x1A
#define SWI_OS_Mouse					0x1C
#define SWI_OS_ReadVduVariables				0x31
#define SWI_OS_ReadModeVariable				0x35
#define SWI_OS_SWINumberFromString			0x39
#define SWI_OS_ReadMonotonicTime			0x42
#define SWI_OS_Plot					0x45
#define SWI_OS_WriteN					0x46
#define SWI_OS_ScreenMode				0x65
#define SWI_OS_ReadLine32				0x7D

#define SWI_ColourTrans_SetGCOL				0x40743
#define SWI_ColourTrans_GCOLToColourNumber		0x4074C
#define SWI_ColourTrans_ColourNumberToGCOL		0x4074D
#define SWI_ColourTrans_SetTextColour			0x40761

/* Tank's GPIO module for Risc OS - not all will be implemented, and only RasPi ones listed */
#define SWI_GPIO_ReadData				0x58F80
#define SWI_GPIO_WriteData				0x58F81
#define SWI_GPIO_ReadOE					0x58F82
#define SWI_GPIO_WriteOE				0x58F83
#define SWI_GPIO_ExpAsGPIO				0x58F85
#define SWI_GPIO_ExpAsUART				0x58F8B
#define SWI_GPIO_ExpAsMMC				0x58F8D
#define SWI_GPIO_ReadMode				0x58F8F
#define SWI_GPIO_WriteMode				0x58F90
#define SWI_GPIO_ReadLevel0				0x58F91
#define SWI_GPIO_WriteLevel0				0x58F92
#define SWI_GPIO_ReadLevel1				0x58F93
#define SWI_GPIO_WriteLevel1				0x58F94
#define SWI_GPIO_ReadRising				0x58F95
#define SWI_GPIO_WriteRising				0x58F96
#define SWI_GPIO_ReadFalling				0x58F97
#define SWI_GPIO_WriteFalling				0x58F98
#define SWI_GPIO_ReadExp32				0x58F9F
#define SWI_GPIO_WriteExp32				0x58FA2
#define SWI_GPIO_ReadExpOE32				0x58FA5
#define SWI_GPIO_WriteExpOE32				0x58FA8
#define SWI_GPIO_ReadEvent				0x58FAB
#define SWI_GPIO_WriteEvent				0x58FAC
#define SWI_GPIO_ReadAsync				0x58FAD
#define SWI_GPIO_WriteAsync				0x58FAE
#define SWI_GPIO_FlashOn				0x58FB4
#define SWI_GPIO_FlashOff				0x58FB5
#define SWI_GPIO_Info					0x58FB6
#define SWI_GPIO_I2CInfo				0x58FB7
#define SWI_GPIO_LoadConfig				0x58FBB
#define SWI_GPIO_ReadConfig				0x58FBC
#define SWI_GPIO_EnableI2C				0x58FBD
#define SWI_GPIO_GetBoard				0x58FBE
#define SWI_GPIO_RescanI2C				0x58FBF


/* Stuff that's local to Matrix Brandy */
#define SWI_Brandy_Version				0x140000
#define SWI_Brandy_Swap16Palette			0x140001
#define SWI_Brandy_GetVideoDriver			0x140002
#define SWI_Brandy_SetFailoverMode			0x140003

#define SWI_RaspberryPi_GPIOInfo			0x140100
#define SWI_RaspberryPi_GetGPIOPortMode			0x140101
#define SWI_RaspberryPi_SetGPIOPortMode			0x140102
#define SWI_RaspberryPi_SetGPIOPortPullUpDownMode	0x140103
#define SWI_RaspberryPi_ReadGPIOPort			0x140104
#define SWI_RaspberryPi_WriteGPIOPort			0x140105

#ifdef _MOS_C
static switable swilist[] = {
	{SWI_OS_WriteC,					"OS_WriteC"},
	{SWI_OS_Write0,					"OS_Write0"},
	{SWI_OS_NewLine,				"OS_NewLine"},
	{SWI_OS_ReadC,					"OS_ReadC"},
	{SWI_OS_CLI,					"OS_CLI"},
	{SWI_OS_Byte,					"OS_Byte"},
	{SWI_OS_Word,					"OS_Word"},
	{SWI_OS_ReadLine,				"OS_ReadLine"},
	{SWI_OS_UpdateMEMC,				"OS_UpdateMEMC"}, /* Recognised, does nothing */
	{SWI_OS_Mouse,					"OS_Mouse"},
	{SWI_OS_ReadVduVariables,			"OS_ReadVduVariables"},
	{SWI_OS_ReadModeVariable,			"OS_ReadModeVariable"},
	{SWI_OS_SWINumberFromString,			"OS_SWINumberFromString"},
	{SWI_OS_ReadMonotonicTime,			"OS_ReadMonotonicTime"},
	{SWI_OS_Plot,					"OS_Plot"},
	{SWI_OS_WriteN,					"OS_WriteN"},
	{SWI_OS_ScreenMode,				"OS_ScreenMode"},
	{SWI_OS_ReadLine32,				"OS_ReadLine32"},

	{SWI_ColourTrans_SetGCOL,			"ColourTrans_SetGCOL"},
	{SWI_ColourTrans_GCOLToColourNumber,		"ColourTrans_GCOLToColourNumber"},
	{SWI_ColourTrans_ColourNumberToGCOL,		"ColourTrans_ColourNumberToGCOL"},
	{SWI_ColourTrans_SetTextColour,			"ColourTrans_SetTextColour"},

	{SWI_GPIO_ReadData,				"GPIO_ReadData"},
	{SWI_GPIO_WriteData,				"GPIO_WriteData"},
	{SWI_GPIO_ReadOE,				"GPIO_ReadOE"},
	{SWI_GPIO_WriteOE,				"GPIO_WriteOE"},
	{SWI_GPIO_ExpAsGPIO,				"GPIO_ExpAsGPIO"},
	{SWI_GPIO_ExpAsUART,				"GPIO_ExpAsUART"},
	{SWI_GPIO_ExpAsMMC,				"GPIO_ExpAsMMC"},
	{SWI_GPIO_ReadMode,				"GPIO_ReadMode"},
	{SWI_GPIO_WriteMode,				"GPIO_WriteMode"},
	{SWI_GPIO_ReadLevel0,				"GPIO_ReadLevel0"},
	{SWI_GPIO_WriteLevel0,				"GPIO_WriteLevel0"},
	{SWI_GPIO_ReadLevel1,				"GPIO_ReadLevel1"},
	{SWI_GPIO_WriteLevel1,				"GPIO_WriteLevel1"},
	{SWI_GPIO_ReadRising,				"GPIO_ReadRising"},
	{SWI_GPIO_WriteRising,				"GPIO_WriteRising"},
	{SWI_GPIO_ReadFalling,				"GPIO_ReadFalling"},
	{SWI_GPIO_WriteFalling,				"GPIO_WriteFalling"},
	{SWI_GPIO_ReadExp32,				"GPIO_ReadExp32"},
	{SWI_GPIO_WriteExp32,				"GPIO_WriteExp32"},
	{SWI_GPIO_ReadExpOE32,				"GPIO_ReadExpOE32"},
	{SWI_GPIO_WriteExpOE32,				"GPIO_WriteExpOE32"},
	{SWI_GPIO_ReadEvent,				"GPIO_ReadEvent"},
	{SWI_GPIO_WriteEvent,				"GPIO_WriteEvent"},
	{SWI_GPIO_ReadAsync,				"GPIO_ReadAsync"},
	{SWI_GPIO_WriteAsync,				"GPIO_WriteAsync"},
	{SWI_GPIO_FlashOn,				"GPIO_FlashOn"},
	{SWI_GPIO_FlashOff,				"GPIO_FlashOff"},
	{SWI_GPIO_Info,					"GPIO_Info"},
	{SWI_GPIO_I2CInfo,				"GPIO_I2CInfo"},
	{SWI_GPIO_LoadConfig,				"GPIO_LoadConfig"},
	{SWI_GPIO_ReadConfig,				"GPIO_ReadConfig"},
	{SWI_GPIO_EnableI2C,				"GPIO_EnableI2C"},
	{SWI_GPIO_GetBoard,				"GPIO_GetBoard"},
	{SWI_GPIO_RescanI2C,				"GPIO_RescanI2C"},

	{SWI_Brandy_Version,				"Brandy_Version"},
	{SWI_Brandy_Swap16Palette,			"Brandy_Swap16Palette"},
	{SWI_Brandy_GetVideoDriver,			"Brandy_GetVideoDriver"},
	{SWI_Brandy_SetFailoverMode,			"Brandy_SetFailoverMode"},

	{SWI_RaspberryPi_GPIOInfo,			"RaspberryPi_GPIOInfo"},
	{SWI_RaspberryPi_GetGPIOPortMode,		"RaspberryPi_GetGPIOPortMode"},
	{SWI_RaspberryPi_SetGPIOPortMode,		"RaspberryPi_SetGPIOPortMode"},
	{SWI_RaspberryPi_SetGPIOPortPullUpDownMode,	"RaspberryPi_SetGPIOPortPullUpDownMode"},
	{SWI_RaspberryPi_ReadGPIOPort,			"RaspberryPi_ReadGPIOPort"},
	{SWI_RaspberryPi_WriteGPIOPort,			"RaspberryPi_WriteGPIOPort"},

	{0xFFFFFFFF,		"End_of_list"}
};
#endif

#endif
