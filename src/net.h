#ifndef BRANDY_NET_H
#define BRANDY_NET_H

#include "common.h"

extern void brandynet_init();
extern int brandynet_connect(char *dest);
extern int brandynet_close(int handle);
extern int32 net_bget(int handle);
extern boolean net_eof(int handle);
extern int net_bput(int handle, int32 value);
extern int net_bputstr(int handle, char *string, int32 length);

#endif
