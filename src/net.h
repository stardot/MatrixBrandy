#ifndef BRANDY_NET_H
#define BRANDY_NET_H

extern void brandynet_init();
extern int brandynet_connect(char *dest);
extern int brandynet_close(int handle);
extern int32 net_bget(int handle);

#endif
