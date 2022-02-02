/*
** This file is part of the Matrix Brandy Basic VI Interpreter.
** Copyright (C) 2018-2021 Michael McConnell and contributors
**
** Brandy is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2, or (at your option)
** any later version.
**
** Brandy is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with Brandy; see the file COPYING.  If not, write to
** the Free Software Foundation, 59 Temple Place - Suite 330,
** Boston, MA 02111-1307, USA.
*/
#include "target.h"
#ifndef NONET
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
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
#endif /* NONET */

//#include "common.h"
//#include "basicdefs.h"

#ifdef DEBUG
#include "basicdefs.h"
#endif
#include "errors.h"
#include "net.h"

#define MAXNETRCVLEN 65536
#define MAXNETSOCKETS 4

#ifdef TARGET_RISCOS
#ifdef __TARGET_SCL__
extern int close(int);
#endif
#endif

#ifndef NONET
static int netsockets[MAXNETSOCKETS];
static char netbuffer[MAXNETSOCKETS][MAXNETRCVLEN + 1];
static int bufptr[MAXNETSOCKETS];
static int bufendptr[MAXNETSOCKETS];
#endif /* NONET */
static int neteof[MAXNETSOCKETS];

static int networking=1;

/* This function only called on startup, cleans the buffer and socket stores */
void brandynet_init() {
#ifdef NONET /* Used by RISC OS and other targets that don't support POSIX network sockets */
  networking=0;
#else
  int n;
#ifdef TARGET_MINGW
  WSADATA wsaData;
#endif

  for (n=0; n<MAXNETSOCKETS; n++) {
    netsockets[n]=bufptr[n]=bufendptr[n]=neteof[n]=0;
    memset(netbuffer, 0, (MAXNETSOCKETS * (MAXNETRCVLEN+1)));
  }
#ifdef TARGET_MINGW
  if(WSAStartup(MAKEWORD(2,2), &wsaData)) networking=0;
#endif
#endif /* NONET */
}

int brandynet_connect(char *dest, char type) {
#ifdef NONET
  error(ERR_NET_NOTSUPP);
  return(-1);
#else

#ifdef TARGET_RISCOS
  char *host, *port;
  int n, mysocket, portnum, result;
  struct sockaddr_in netdest;
  struct hostent *he = NULL;
  struct in_addr *inaddr = NULL;

  if(networking==0) {
    error(ERR_NET_NOTSUPP);
    return(-1);
  }

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

  memset(&netdest, 0, sizeof(netdest));            /* zero the struct */
  netdest.sin_family = AF_INET;


  /* This is a dirty hack because RISC OS can't build a hostent struct from an IP address in gethostbyname() */
  result = inet_aton(host, inaddr);
  if (0 == result) {
    if ((he = gethostbyname(host)) == NULL) {
      free(host);
      error(ERR_NET_NOTFOUND);
      return(-1);
    }
    inaddr=(struct in_addr *)he->h_addr;
  }
  netdest.sin_addr = *inaddr;                /* set destination IP address */
  netdest.sin_port = htons(portnum);         /* set destination port number */
  free(host);                                /* Don't need this any more*/

  if (connect(mysocket, (struct sockaddr *)&netdest, sizeof(struct sockaddr_in))) {
    error(ERR_NET_CONNREFUSED);
    return(-1);
  }
  fcntl(mysocket, F_SETFL, O_NONBLOCK);
  netsockets[n] = mysocket;
  return(n);

#else /* not TARGET_RISCOS */
  char *host, *port;
  int n, mysocket=0, ret;
  struct addrinfo hints, *addrdata, *rp;
#ifdef TARGET_MINGW
  unsigned long opt;
#endif

  if(networking==0) {
    error(ERR_NET_NOTSUPP);
    return(-1);
  }

  for (n=0; n<MAXNETSOCKETS; n++) {
    if (!netsockets[n]) break;
  }
  if (MAXNETSOCKETS == n) {
    error(ERR_NET_MAXSOCKETS);
    return(-1);
  }

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
  free(host);				/* Don't need this any more */

  if(ret) {
#ifdef DEBUG
    if (basicvars.debug_flags.debug) fprintf(stderr, "getaddrinfo returns: %s\n", gai_strerror(ret));
#endif
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

  freeaddrinfo(addrdata);		/* Don't need this any more either */

  if (!rp) {
    error(ERR_NET_CONNREFUSED);
    return(-1);
  }

#ifdef TARGET_MINGW
  opt=1;
  ioctlsocket(mysocket, FIONBIO, &opt);
#else
  fcntl(mysocket, F_SETFL, O_NONBLOCK);
#endif
  netsockets[n] = mysocket;
  return(n);
#endif /* RISCOS */
#endif /* NONET */
}

int brandynet_close(int handle) {
#ifndef NONET
  close(netsockets[handle]);
  netsockets[handle] = neteof[handle] = 0;
#endif
  return(0);
}

/* Some systems don't have this, make it a null value in that case. */
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0
#endif

#ifndef NONET
static int net_get_something(int handle) {
  int retval = 0;

  bufendptr[handle] = recv(netsockets[handle], netbuffer[handle], MAXNETRCVLEN, MSG_DONTWAIT);
  if (bufendptr[handle] == 0) {
    retval=1; /* EOF - connection closed */
    neteof[handle] = 1;
  }
  if (bufendptr[handle] == -1) bufendptr[handle] = 0;
  bufptr[handle] = 0;
  return(retval);
}
#endif /* NONET */

int32 net_bget(int handle) {
#ifdef NONET
  error(ERR_NET_NOTSUPP);
  return(-1);
#else
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
#endif /* NONET */
}

boolean net_eof(int handle) {
  return(neteof[handle]);
}

int net_bput(int handle, int32 value) {
#ifdef NONET
  error(ERR_NET_NOTSUPP);
  return(-1);
#else
  char minibuf[2];
  int retval;

  minibuf[0]=(value & 0xFFu);
  minibuf[1]=0;
  retval=send(netsockets[handle], (const char *)&minibuf, 1, 0);
  if (retval == -1) return(1);
  return(0);
#endif /* NONET */
}

int net_bputstr(int handle, char *string, int32 length) {
#ifdef NONET
  error(ERR_NET_NOTSUPP);
  return(-1);
#else
  int retval;

  retval=send(netsockets[handle], string, length, 0);
  if (retval == -1) return(1);
  return(0);
#endif /* NONET */
}
