/*
** This file is part of the Matrix Brandy Basic VI Interpreter.
** Copyright (C) 2018-2024 Michael McConnell and contributors
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
#ifndef NONET /* matching endif at end of file */
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
#ifdef TARGET_RISCOS
#include <ctype.h>
#ifdef __TARGET_SCL__
#include <sys/ioctl.h>
#endif /* __TARGET_SCL__ */
#include "kernel.h"
#include "swis.h"
#endif /* TARGET_RISCOS */

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

  DEBUGFUNCMSGIN;
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
      } else {
        DEBUGFUNCMSGOUT;
        return (0);
      }
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
  DEBUGFUNCMSGOUT;
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

  DEBUGFUNCMSGIN;
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
  if (oserror != NIL) {
    error(ERR_CMDFAIL, oserror->errmess);
    return;
  }
  /* LEN("Socket_Creat"+CHR$(0)) = 13 */
  if (regs.r[2] != 13) matrixflags.networking=0; /* SWI not found */
  free(swiname);
#endif
  DEBUGFUNCMSGOUT;
}

int brandynet_connect(char *dest, char type, int reporterrors) {
#if defined(TARGET_RISCOS) | defined(MINIX_OLDNET)
  char *host, *port;
  int n, mysocket, portnum, result;
#ifndef __TARGET_SCL__
  int flags;
#endif
  struct sockaddr_in netdest;
  struct hostent *he = NULL;
  struct in_addr *inaddr = NULL;
#ifdef __TARGET_SCL__
  unsigned long opt;
#endif

  DEBUGFUNCMSGIN;
  if(matrixflags.networking==0) {
    if (reporterrors) error(ERR_NET_NOTSUPP);
    return(-1);
  }

  for (n=0; n<MAXNETSOCKETS; n++) {
    if (!netsockets[n]) break;
  }
  if (MAXNETSOCKETS == n) {
    if (reporterrors) error(ERR_NET_MAXSOCKETS);
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
      if (reporterrors) error(ERR_NET_NOTFOUND);
      return(-1);
    }
    inaddr=(struct in_addr *)he->h_addr;
  }
  netdest.sin_addr = *inaddr;                /* set destination IP address */
  netdest.sin_port = htons(portnum);         /* set destination port number */
  free(host);                                /* Don't need this any more */
  if (connect(mysocket, (struct sockaddr *)&netdest, sizeof(struct sockaddr_in))) {
    if (reporterrors) error(ERR_NET_CONNREFUSED);
    return(-1);
  }
  free(inaddr);                              /* Don't need this any more */

#ifdef __TARGET_SCL__
  /* SharedCLibrary doesn't support fcntl */
  opt=1;
  socketioctl(mysocket, FIONBIO, &opt);
#else
  flags = fcntl(mysocket, F_GETFL, 0);
  fcntl(mysocket, F_SETFL, flags | O_NONBLOCK);
#endif
  netsockets[n] = mysocket;
  DEBUGFUNCMSGOUT;
  return(n);

#else /* not TARGET_RISCOS */
  char *host, *port;
  int n, mysocket=0, ret, sockres=-1;
  struct addrinfo hints, *addrdata, *rp;
#ifdef TARGET_MINGW
  unsigned long opt;
#else
  struct timeval timeout;
  int flags;
#endif

  if(matrixflags.networking==0) {
    if (reporterrors) error(ERR_NET_NOTSUPP);
    return(-1);
  }

  for (n=0; n<MAXNETSOCKETS; n++) {
    if (!netsockets[n]) break;
  }
  if (MAXNETSOCKETS == n) {
    if (reporterrors) error(ERR_NET_MAXSOCKETS);
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
  free(host);                           /* Don't need this any more */

  if(ret) {
#ifdef DEBUG
    if (basicvars.debug_flags.debug) fprintf(stderr, "getaddrinfo returns: %s\n", gai_strerror(ret));
#endif
    if (reporterrors) error(ERR_NET_NOTFOUND);
    return(-1);
  }

/* Set the timeout of the socket if we're not reporting errors */
  for(rp = addrdata; rp != NULL; rp = rp->ai_next) {
    mysocket = socket(rp->ai_family, SOCK_STREAM, 0);
    if (mysocket == -1) continue;
#ifndef TARGET_MINGW
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    if (!reporterrors) setsockopt(mysocket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
    sockres=connect(mysocket, rp->ai_addr, rp->ai_addrlen);
    if (!sockres)
      break; /* success! */
    close(mysocket);
  }

  freeaddrinfo(addrdata);               /* Don't need this any more either */

  if (sockres) {
    if (reporterrors) error(ERR_NET_CONNREFUSED);
    return(-1);
  }

#ifdef TARGET_MINGW
  opt=1;
  ioctlsocket(mysocket, FIONBIO, &opt);
#else
  flags = fcntl(mysocket, F_GETFL, 0);
  fcntl(mysocket, F_SETFL, flags | O_NONBLOCK);
#endif
  netsockets[n] = mysocket;
  DEBUGFUNCMSGOUT;
  return(n);
#endif /* not RISCOS */
}

int brandynet_close(int handle) {
  DEBUGFUNCMSGIN;
  close(netsockets[handle]);
  netsockets[handle] = neteof[handle] = 0;
  DEBUGFUNCMSGOUT;
  return(0);
}

/* Some systems don't have this, make it a null value in that case. */
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0
#endif

static int net_get_something(int handle) {
  int retval = 0;

  DEBUGFUNCMSGIN;
  bufendptr[handle] = recv(netsockets[handle], netbuffer[handle], MAXNETRCVLEN, MSG_DONTWAIT);
  if (bufendptr[handle] == 0) {
    retval=1; /* EOF - connection closed */
    neteof[handle] = 1;
  }
  if (bufendptr[handle] == -1) bufendptr[handle] = 0;
  bufptr[handle] = 0;
  DEBUGFUNCMSGOUT;
  return(retval);
}

int32 net_bget(int handle) {
  int value;

  DEBUGFUNCMSGIN;
  if (neteof[handle]) return(-2);
  if (bufptr[handle] >= bufendptr[handle]) {
    int retval=net_get_something(handle);
    if (retval) return(-2);                             /* EOF */
  }
  if (bufptr[handle] >= bufendptr[handle]) return(-1);  /* No data available. EOF NOT set */
  value=netbuffer[handle][(bufptr[handle])];
  bufptr[handle]++;
  DEBUGFUNCMSGOUT;
  return(value & 0xFF);
}

boolean net_eof(int handle) {
  DEBUGFUNCMSGIN;
  DEBUGFUNCMSGOUT;
  return(neteof[handle]);
}

int net_bput(int handle, int32 value) {
  char minibuf[2];
  int retval;

  DEBUGFUNCMSGIN;
  minibuf[0]=(value & 0xFFu);
  minibuf[1]=0;
  retval=send(netsockets[handle], (const char *)&minibuf, 1, 0);
  if (retval == -1) {
    DEBUGFUNCMSGOUT;
    return(1);
  }
  DEBUGFUNCMSGOUT;
  return(0);
}

int net_bputstr(int handle, char *string, int32 length) {
  int retval;

  DEBUGFUNCMSGIN;
  if (length == -1) length=strlen(string);
  retval=send(netsockets[handle], string, length, 0);
  if (retval == -1) {
    DEBUGFUNCMSGOUT;
    return(1);
  }
  DEBUGFUNCMSGOUT;
  return(0);
}

#ifndef BRANDY_NOVERCHECK
/* This function queries the Matrix Brandy web server to check for a newer
 * version. This is a quick and dirty implementation talking raw HTML!
 */
int checkfornewer() {
  int hndl, ptr, val, vermaj, vermin, verpatch, lc;
  char *inbuf, *verstr, *ptra, *request;

  DEBUGFUNCMSGIN;
  inbuf=malloc(8192);
  memset(inbuf, 0, 4096);
  hndl=brandynet_connect("brandy.matrixnetwork.co.uk:80", 0, 0);
  if (hndl < 0) {
    free(inbuf);
    DEBUGFUNCMSGOUT;
    return(0);
  }
  request=malloc(1024);
#ifdef BRANDY_RELEASE
  snprintf(request, 1023, "GET /latest HTTP/1.0\r\nHost: brandy.matrixnetwork.co.uk\r\nUser-Agent: MatrixBrandy/" BRANDY_MAJOR "." BRANDY_MINOR "." BRANDY_PATCHLEVEL "(" BRANDY_OS "/" CPUTYPE SFX1 SFX2 ")\r\n\r\n");
#else
  snprintf(request, 1023, "GET /latest HTTP/1.0\r\nHost: brandy.matrixnetwork.co.uk\r\nUser-Agent: MatrixBrandy/" BRANDY_MAJOR "." BRANDY_MINOR "." BRANDY_PATCHLEVEL "(" BRANDY_OS "/" CPUTYPE SFX1 SFX2 " " BRANDY_GITBRANCH ":" BRANDY_GITCOMMIT ")\r\n\r\n");
#endif
  net_bputstr(hndl, request, -1);
  free(request);
  ptr = 0;
  val=-1;
  lc=0;
  while (val != -2) {
    val=net_bget(hndl);
    if (val >= 0) {
      inbuf[ptr]=val;
      ptr++;
    } else {
      usleep(10000);
      lc++;                                             /* Increment loop counter */
      /* If we have hung for a second or have data in the buffer already, stop waiting */
      if ((lc >= 100) || ((val == -1) && (ptr > 0))) val=-2;
    }
  }
  brandynet_close(hndl);
  if (strlen(inbuf) == 0) return(2);
  verstr=strstr(inbuf, "\r\n\r\n");
  verstr+=4;
  if ((*verstr < 48) || (*verstr > 57)) return(2);
  ptra=strchr(verstr, '\n'); *ptra='\0';
  ptra=strchr(verstr, '.'); *ptra='\0';
  vermaj=atoi(verstr);
  verstr=ptra+1;
  ptra=strchr(verstr, '.'); *ptra='\0';
  vermin=atoi(verstr);
  verstr=ptra+1;
  verpatch=atoi(verstr);
  free(inbuf);
  if ((vermaj > atoi(BRANDY_MAJOR)) || \
      (vermin > atoi(BRANDY_MINOR)) || \
      (verpatch > atoi(BRANDY_PATCHLEVEL))) {
    DEBUGFUNCMSGOUT;
    return(1);
  } else {
    return(0);
  }
}
#endif /* BRANDY_NOVERCHECK */
#endif /* NONET ... right at top of the file */
