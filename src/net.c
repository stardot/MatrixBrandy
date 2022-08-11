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
#ifndef NONET /* matching endif at end of file */
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
#ifdef TARGET_RISCOS
#include <ctype.h>
#include "kernel.h"
#include "swis.h"
#endif

#include "basicdefs.h"
#include "errors.h"
#include "net.h"

#ifdef TARGET_MINIX
#include <minix/config.h>

/* OS_REV is new to Minix 3.4 - it's not present in 3.3 */
#ifndef OS_REV
#define MINIX_OLDNET
#endif
#endif /* TARGET_MINIX */

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

#ifdef __TARGET_SCL__
/* SharedCLibrary is missing inet_aton(). Here'a an implementation */
in_addr_t inet_aton(const char *cp, struct in_addr *addr) {
	uint32 quads[4];
	in_addr_t ipaddr = 0;
	const char *c;
	char *endptr;
	int atend = 0, n = 0;

	c = (const char *)cp;
  while (!atend) {
    uint32 l;

    l = strtoul(c, &endptr, 0);
    if (l == ULONG_MAX || (l == 0 && endptr == c)) return (0);
    ipaddr = (in_addr_t)l;
    if (endptr == c) return (0);
    quads[n] = ipaddr;
    c = endptr;
    switch (*c) {
    case '.' :
      if (n == 3) return (0);
      n++;
      c++;
      break;
    case '\0':
      atend = 1;
      break;
    default:
      if (isspace((unsigned char)*c)) {
        atend = 1;
        break;
      } else return (0);
    }
  }

  switch (n) {
  case 0:
    /* Nothing required here, already checked by strtoul. */
    break;
  case 1:
    if (ipaddr > 0xffffff || quads[0] > 0xff)
      return (0);
    ipaddr |= quads[0] << 24;
    break;
  case 2:
    if (ipaddr > 0xffff || quads[0] > 0xff || quads[1] > 0xff)
      return (0);
    ipaddr |= (quads[0] << 24) | (quads[1] << 16);
    break;
  case 3:
    if (ipaddr > 0xff || quads[0] > 0xff || quads[1] > 0xff || quads[2] > 0xff)
      return (0);
    ipaddr |= (quads[0] << 24) | (quads[1] << 16) | (quads[2] << 8);
    break;
  }
  if (addr != NULL) addr->s_addr = htonl(ipaddr);
  return (1);
}
#endif /* __TARGET_SCL__ */

/* This function only called on startup, cleans the buffer and socket stores */
void brandynet_init() {
  int n;
#ifdef TARGET_MINGW
  WSADATA wsaData;
#endif
#ifdef TARGET_RISCOS
  _kernel_oserror *oserror;
  _kernel_swi_regs regs;
  char *swiname;
#endif

  matrixflags.networking = 1;
  for (n=0; n<MAXNETSOCKETS; n++) {
    netsockets[n]=bufptr[n]=bufendptr[n]=neteof[n]=0;
    memset(netbuffer, 0, (MAXNETSOCKETS * (MAXNETRCVLEN+1)));
  }
#ifdef TARGET_MINGW
  if(WSAStartup(MAKEWORD(2,2), &wsaData)) matrixflags.networking=0;
#endif

#ifdef TARGET_RISCOS
  /* This code checks to see if the underlying SWIs needed for
   * internet networking are present. If not, we disable networking.
   */
  swiname=malloc(256);
  regs.r[0] = 0x41200; /* Socket_Creat */
  regs.r[1] = (size_t)swiname;
  regs.r[2] = 255;
  oserror = _kernel_swi(OS_SWINumberToString, &regs, &regs);
  if (oserror != NIL) error(ERR_CMDFAIL, oserror->errmess);
  /* LEN("Socket_Creat"+CHR$(0)) = 13 */
  if (regs.r[2] != 13) matrixflags.networking=0; /* SWI not found */
  free(swiname);
#endif
}

int brandynet_connect(char *dest, char type) {
#if defined(TARGET_RISCOS) | defined(MINIX_OLDNET)
  char *host, *port;
  int n, mysocket, portnum, result;
  struct sockaddr_in netdest;
  struct hostent *he = NULL;
  struct in_addr *inaddr = NULL;

  if(matrixflags.networking==0) {
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
  inaddr=malloc(sizeof(struct in_addr));
  result = inet_aton(host, inaddr);
  if (0 == result) {
    inaddr = NULL;
    if ((he = gethostbyname(host)) == NULL) {
      free(host);
      error(ERR_NET_NOTFOUND);
      return(-1);
    }
    inaddr=(struct in_addr *)he->h_addr;
  }
  netdest.sin_addr = *inaddr;                /* set destination IP address */
  netdest.sin_port = htons(portnum);         /* set destination port number */
  free(host);                                /* Don't need this any more */
  if (connect(mysocket, (struct sockaddr *)&netdest, sizeof(struct sockaddr_in))) {
    error(ERR_NET_CONNREFUSED);
    return(-1);
  }
  free(inaddr);                              /* Don't need this any more */

#ifndef __TARGET_SCL__
  /* SharedCLibrary doesn't support this */
  fcntl(mysocket, F_SETFL, O_NONBLOCK);
#endif
  netsockets[n] = mysocket;
  return(n);

#else /* not TARGET_RISCOS */
  char *host, *port;
  int n, mysocket=0, ret;
  struct addrinfo hints, *addrdata, *rp;
#ifdef TARGET_MINGW
  unsigned long opt;
#endif

  if(matrixflags.networking==0) {
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
}

int brandynet_close(int handle) {
  close(netsockets[handle]);
  netsockets[handle] = neteof[handle] = 0;
  return(0);
}

/* Some systems don't have this, make it a null value in that case. */
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0
#endif

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
#endif /* NONET ... right at top of the file */
