#ifndef MOS_SWINUMS_H
#define MOS_SWINUMS_H

typedef struct {
  int32 swinum;		/* RISC OS SWI number */
  char *msgtext;	/* SWI Name */
} switable;

#define SWI_OS_WriteC		0x00
#define SWI_OS_WriteS		0x01
#define SWI_OS_Write0		0x02
#define SWI_OS_NewLine		0x03
#define SWI_OS_ReadC		0x04
#define SWI_OS_CLI		0x05
#define SWI_OS_Byte		0x06

static switable swilist[] = {
	{SWI_OS_WriteC,		"OS_WriteC"},
	{SWI_OS_WriteS,		"OS_WriteS"},
	{SWI_OS_Write0,		"OS_WriteO"},
	{SWI_OS_NewLine,	"OS_NewLine"},
	{SWI_OS_ReadC,		"OS_ReadC"},
	{SWI_OS_CLI,		"OS_CLI"},
	{SWI_OS_Byte,		"OS_Byte"},
};

#endif
