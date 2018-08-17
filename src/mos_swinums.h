#ifndef MOS_SWINUMS_H
#define MOS_SWINUMS_H

typedef struct {
  int32 swinum;		/* RISC OS SWI number */
  char *swiname;	/* SWI Name */
} switable;

/* Source: http://www.riscos.com/support/developers/prm_index/numswilist.html */

/* Used by mos.c: int32 mos_getswinum(char *name, int32 length) */

#define SWI_OS_WriteC			0x00
#define SWI_OS_Write0			0x02
#define SWI_OS_NewLine			0x03
#define SWI_OS_ReadC			0x04
#define SWI_OS_CLI			0x05
#define SWI_OS_Byte			0x06
#define SWI_OS_ReadLine			0x0E
#define SWI_OS_SWINumberFromString	0x39

#define SWI_ColourTrans_SetGCOL		0x40743
#define SWI_ColourTrans_SetTextColour	0x40761

static switable swilist[] = {
	{SWI_OS_WriteC,			"OS_WriteC"},
	{SWI_OS_Write0,			"OS_Write0"},
	{SWI_OS_NewLine,		"OS_NewLine"},
	{SWI_OS_ReadC,			"OS_ReadC"},
	{SWI_OS_CLI,			"OS_CLI"},
	{SWI_OS_Byte,			"OS_Byte"},
	{SWI_OS_ReadLine,		"OS_ReadLine"},
	{SWI_OS_SWINumberFromString,	"OS_SWINumberFromString"},

	{SWI_ColourTrans_SetGCOL,	"ColourTrans_SetGCOL"},
	{SWI_ColourTrans_SetTextColour,	"ColourTrans_SetTextColour"},

	{0xFFFFFFFF,		"End_of_list"}
};

#endif
