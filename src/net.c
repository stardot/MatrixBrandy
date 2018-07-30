#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

//#include "common.h"
//#include "basicdefs.h"

#include "target.h"
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

int brandynet_connect(char *dest) {
  char *host, *port;
  int n,ptr, len, mysocket, portnum;
  struct sockaddr_in netdest;
  struct hostent *he;
  struct in_addr *inaddr;

  for (n=0; n<MAXNETSOCKETS; n++) {
    if (!netsockets[n]) break;
  }
  if (MAXNETSOCKETS == n) {
    error(ERR_NET_MAXSOCKETS);
    return(-1);
  }

  host=strdup(dest);
  port=strchr(host,':');
  port[0]='\0';
  port++;
  portnum=atoi(port);

  mysocket = socket(AF_INET, SOCK_STREAM, 0);

  memset(&netdest, 0, sizeof(netdest));			/* zero the struct */
  netdest.sin_family = AF_INET;

  if ((he = gethostbyname(host)) == NULL) {
    error(ERR_NET_NOTFOUND);
    return(-1);
  }
  inaddr=(struct in_addr *)he->h_addr;
  netdest.sin_addr = *inaddr;				/* set destination IP address */
  netdest.sin_port = htons(portnum);			/* set destination port number */
  free(host);						/* Don't need this any more*/

  if (connect(mysocket, (struct sockaddr *)&netdest, sizeof(struct sockaddr_in))) {
    error(ERR_NET_CONNREFUSED);
    return(-1);
  }
  fcntl(mysocket, F_SETFL, O_NONBLOCK);
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
  int value, ptr;
  int retval=0;

  if (neteof[handle]) return(-2);
  if (bufptr[handle] >= bufendptr[handle]) retval=net_get_something(handle);
  if (retval) return(-2);				/* EOF */
  if (bufptr[handle] >= bufendptr[handle]) return(-1);	/* No data available. EOF NOT set */
  value=netbuffer[handle][(bufptr[handle])];
  bufptr[handle]++;
  return(value);
}

boolean net_eof(handle) {
  return(neteof[handle]);
}

int net_bput(int handle, int32 value) {
  char minibuf[2];
  int retval;

  minibuf[0]=(value & 0xFFu);
  minibuf[1]=0;
  retval=send(netsockets[handle], &minibuf, 1, 0);
  if (retval == -1) return(1);
  return(0);
}

int net_bputstr(int handle, char *string, int32 length) {
  int retval;

  retval=send(netsockets[handle], string, length, 0);
  if (retval == -1) return(1);
  return(0);
}
