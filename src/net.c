#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "target.h"
#ifdef TARGET_MINGW
#include <winsock2.h>
#include <windows.h>
#include <wininet.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#endif
#include <sys/types.h>
#include <errno.h>

//#include "common.h"
//#include "basicdefs.h"

#include "errors.h"
#include "net.h"

#define MAXNETRCVLEN 65536
#define MAXNETSOCKETS 4

static char netbuffer[MAXNETSOCKETS][MAXNETRCVLEN + 1];
static int netsockets[MAXNETSOCKETS];
static int bufptr[MAXNETSOCKETS];
static int bufendptr[MAXNETSOCKETS];
static int neteof[MAXNETSOCKETS];

/* This function only called on startup, cleans the buffer and socket stores */
void brandynet_init() {
  int n;

  for (n=0; n<MAXNETSOCKETS; n++) {
    netsockets[n]=bufptr[n]=bufendptr[n]=neteof[n]=0;
    memset(netbuffer, 0, (MAXNETSOCKETS * (MAXNETRCVLEN+1)));
  }
}

int brandynet_connect(char *dest, char type) {
  char *host, *port;
  int n, mysocket, ret;
  struct addrinfo hints, *addrdata, *rp;
#ifdef TARGET_MINGW
  WSADATA wsaData;
  unsigned long opt;
#endif

  for (n=0; n<MAXNETSOCKETS; n++) {
    if (!netsockets[n]) break;
  }
  if (MAXNETSOCKETS == n) {
    error(ERR_NET_MAXSOCKETS);
    return(-1);
  }

#ifdef TARGET_MINGW
  if(WSAStartup(MAKEWORD(2,2), &wsaData)) return(-1);
#endif

  memset(&hints, 0, sizeof(hints));
  if (type == '0') hints.ai_family=AF_UNSPEC;
  else if (type == '4') hints.ai_family=AF_INET;
  else if (type == '6') hints.ai_family=AF_INET6;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_ADDRCONFIG;
  hints.ai_protocol = IPPROTO_TCP;

  host=strdup(dest);
  port=strchr(host,':');
  port[0]='\0';
  port++;

  ret=getaddrinfo(host, port, &hints, &addrdata);

  if(ret) {
    printf("getaddrinfo returns: %s\n", gai_strerror(ret));
    error(ERR_NET_NOTFOUND);
    return(-1);
  }

  for(rp = addrdata; rp != NULL; rp = rp->ai_next) {
    mysocket = socket(rp->ai_family, SOCK_STREAM, 0);
    if (mysocket == -1) continue;
    if (connect(mysocket, rp->ai_addr, rp->ai_addrlen) != -1)
      break; /* success! */
    close(mysocket);
  }

  if (!rp) {
    error(ERR_NET_CONNREFUSED);
    return(-1);
  }

  free(host);				/* Don't need this any more */
  freeaddrinfo(addrdata);		/* Don't need this any more either */

#ifdef TARGET_MINGW
  opt=1;
  ioctlsocket(mysocket, FIONBIO, &opt);
#else
  fcntl(mysocket, F_SETFL, O_NONBLOCK);
#endif
  netsockets[n] = mysocket;
  return(n);
}

int brandynet_close(int handle) {
  close(netsockets[handle]);
  netsockets[handle] = neteof[handle] = 0;
  return(0);
}

static int net_get_something(int handle) {
  int retval = 0;

  bufendptr[handle] = recv(netsockets[handle], netbuffer[handle], MAXNETRCVLEN, 0);
  if (bufendptr[handle] == -1) { /* try again */
    usleep(10000);
    bufendptr[handle] = recv(netsockets[handle], netbuffer[handle], MAXNETRCVLEN, 0);
  }
  if (bufendptr[handle] == 0) {
    retval=1; /* EOF - connection closed */
    neteof[handle] = 1;
  }
  if (bufendptr[handle] == -1) bufendptr[handle] = 0;
  bufptr[handle] = 0;
  return(retval);
}

int32 net_bget(int handle) {
  int value;
  int retval=0;

  if (neteof[handle]) return(-2);
  if (bufptr[handle] >= bufendptr[handle]) {
    retval=net_get_something(handle);
    if (retval) return(-2);				/* EOF */
  }
  if (bufptr[handle] >= bufendptr[handle]) return(-1);	/* No data available. EOF NOT set */
  value=netbuffer[handle][(bufptr[handle])];
  bufptr[handle]++;
  return(value);
}

boolean net_eof(int handle) {
  return(neteof[handle]);
}

int net_bput(int handle, int32 value) {
  char minibuf[2];
  int retval;

  minibuf[0]=(value & 0xFFu);
  minibuf[1]=0;
  retval=send(netsockets[handle], (const char *)&minibuf, 1, 0);
  if (retval == -1) return(1);
  return(0);
}

int net_bputstr(int handle, char *string, int32 length) {
  int retval;

  retval=send(netsockets[handle], string, length, 0);
  if (retval == -1) return(1);
  return(0);
}
