REM This program draws a simple fractal landscape and plots it
REM using characters to represent different heights. The landscape
REM can be changed by entering different values for seed%
:
S%=256
limit%=2     :REM 64 by 64 plot
xlimit%=64
zlimit%=24
DIM Y%(S%,S%),filler$(16)
filler$()=".", "_", "+", "%", "o", "$", "=", "U", "E", "B", "F", "O", "Q", "N", "M", "#", "@"
:
INPUT"Seed? " seed%
CLS
start=TIME
T%=1024 DIV S%*limit%*2
K%=1024 DIV S%
step%=limit%*2
S2%=S% DIV 2
Y%(0,0)=FNrandom(S%)-S2%-20
Y%(0,S%)=FNrandom(S%)-S2%-20
Y%(S%,0)=FNrandom(S%)-S2%-20
Y%(S%,S%)=FNrandom(S%)-S2%-20
PROClandscape(0,0,S%)
COLOUR 7
PRINT'"Time=";TIME-start
END
:
:
DEF PROClandscape(X%,Z%,D%)
LOCAL A%,XA%,ZA%
A%=D%>>1
A2%=A%>>1
XA%=X%+A%: ZA%=Z%+A%
XD%=X%+D%: ZD%=Z%+D%
IF Y%(XA%,Z%)=0 THEN Y%(XA%,Z%)=(Y%(X%,Z%)+Y%(XD%,Z%)>>1)+FNrandom(A%)-A2%
PROCcol2d(Y%(XA%,Z%))
IF Y%(X%,ZA%)=0 THEN Y%(X%,ZA%)=(Y%(X%,Z%)+Y%(X%,ZD%)>>1)+FNrandom(A%)-A2%
PROCcol2d(Y%(X%,ZA%))
IF Y%(XA%,ZD%)=0 THEN Y%(XA%,ZD%)=(Y%(X%,ZD%)+Y%(XD%,ZD%)>>1)+FNrandom(A%)-A2%
PROCcol2d(Y%(XA%,ZD%))
IF Y%(XD%,ZA%)=0 THEN Y%(XD%,ZA%)=(Y%(XD%,Z%)+Y%(XD%,ZD%)>>1)+FNrandom(A%)-A2%
PROCcol2d(Y%(XD%,ZA%))
Y%(XA%,ZA%)=(Y%(X%,ZA%)+Y%(XD%,ZA%)+Y%(XA%,Z%)+Y%(XA%,ZD%)>>2)+FNrandom(A%)-A2%
PROCcol2d(Y%(XA%,ZA%))
IF A%>limit% THEN
  PROClandscape(X%,Z%,A%)
  PROClandscape(XA%,Z%,A%)
  PROClandscape(X%,ZA%,A%)
  PROClandscape(XA%,ZA%,A%)
ENDIF
ENDPROC
:
:
DEF PROCcol2d(P%)
LOCAL xx%, zz%
xx%=X% DIV step%
IF xx%>=xlimit% THEN ENDPROC
zz%=Z% DIV step%
IF zz%>=zlimit% THEN ENDPROC
IF P%<=0 THEN
REM Sea
  IF P%<-100 THEN COLOUR 4 ELSE COLOUR 6
  PRINT TAB(xx%, zz%);"~";
ELSE
REM Land
  colour%=P% DIV 18
  IF colour%=0 THEN COLOUR 3 ELSE COLOUR 2
  PRINT TAB(xx%, zz%);filler$(colour%);
ENDIF
ENDPROC
:
:
DEF FNrandom(range%)
seed%=48271*(seed% MOD 44488)-3399*(seed% DIV 44488)
IF seed%<0 THEN seed%+=&80000000
=seed% MOD range%+1
