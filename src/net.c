#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>

//#include "common.h"
//#include "basicdefs.h"

#include "target.h"
#include "errors.h"
#include "net.h"

#define MAXNETRCVLEN 65536
#define MAXNETSOCKETS 4

static char netbuffer[MAXNETSOCKETS][MAXNETRCVLEN + 1];
static int netsockets[MAXNETSOCKETS];

/* This function only called on startup, cleans the buffer and socket stores */
void brandynet_init() {
  int n;

  for (n=0; n<MAXNETSOCKETS; n++) {
    netsockets[n]=0;
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
  netsockets[n] = mysocket;
  return(n);
}

int brandynet_close(int handle) {
  close(netsockets[handle]);
  netsockets[handle] = 0;
  return(0);
}
