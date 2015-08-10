REM NOTE: this library can only be used in an xterm session under
REM Linux and NetBSD
:
REM This is a simple library that allows graphics to be plotted
REM in a xterm window running under NetBSD or Linux using the
REM Tektronics graphics terminal emulation that xterm offers.
:
REM The procedures and functions work in the same way that their
REM Basic statement counterparts do. The Tektronics screen has
REM a size of 1024 by 780 pixels. This library works in terms in
REM RISC OS graphics units so the screen dimensions according to
REM this code is 2048 graphics units by 1560.
:
:
REM PROCmode initialises the variables used by the library and
REM clears the graphics screen. It has to be called before any
REM other procedure in this library
:
DEF PROCmode(M%)
tek_gcx%=0
tek_gcy%=0
tek_originx%=0
tek_originy%=0
VDU 1, 27, 1, 12
ENDPROC
:
:
REM PROCorigin sets the coordinates of the graphics origin
:
DEF PROCorigin(X%, Y%)
tek_originx%=X%
tek_originy%=Y%
ENDPROC
:
:
REM PROCclg clears the graphics screen
:
DEF PROCclg
VDU 1, 27, 1, 12
ENDPROC
:
:
REM PROCmove moves the graphics cursor to the coordinates given
:
DEF PROCmove(X%, Y%)
tek_gcx%=(X%+tek_originx%) DIV 2
tek_gcy%=(Y%+tek_originy%) DIV 2
ENDPROC
:
:
REM PROCdraw draws a line from the last graphics cursor position
REM to the coordinates given, making them the new cursor position
:
DEF PROCdraw(X%, Y%)
X%=(X%+tek_originx%) DIV 2
Y%=(Y%+tek_originy%) DIV 2
VDU 1, 29
VDU 1, (tek_gcy%>>5)+32, 1, (tek_gcy% AND 31)+96, 1, (tek_gcx%>>5)+32, 1, (tek_gcx% AND 31)+64
VDU 1,(Y%>>5)+32, 1,(Y% AND 31)+96, 1,(X%>>5)+32, 1,(X% AND 31)+64
VDU 1, 31
tek_gcx%=X%
tek_gcy%=Y%
ENDPROC
:
:
REM PROCline draws a line from the coordinates (sx, sy) to (ex, ey).
REM The graphics cursor position is set to (ex, ey) 
:
DEF PROCline(sx%, sy%, ex%, ey%)
sx%=(sx%+tek_originx%) DIV 2
sy%=(sy%+tek_originy%) DIV 2
ex%=(ex%+tek_originx%) DIV 2
ey%=(ey%+tek_originy%) DIV 2
VDU 1, 29
VDU 1, (sy%>>5)+32, 1, (sy% AND 31)+96, 1, (sx%>>5)+32, 1, (sx% AND 31)+64
VDU 1, (ey%>>5)+32, 1, (ey% AND 31)+96, 1, (ex%>>5)+32, 1, (ex% AND 31)+64
VDU 1, 31
tek_gcx%=ex%
tek_gcy%=ey%
ENDPROC
:
:
REM PROCpoint plots a single point
:
DEF PROCpoint(X%, Y%)
X%=(X%+tek_originx%) DIV 2
Y%=(Y%+tek_originy%) DIV 2
VDU 1, 29
VDU 1,(Y%>>5)+32, 1,(Y% AND 31)+96, 1,(X%>>5)+32, 1,(X% AND 31)+64
VDU 1,(Y%>>5)+32, 1,(Y% AND 31)+96, 1,(X%>>5)+32, 1,(X% AND 31)+64
VDU 1, 31
tek_gcx%=X%
tek_gcy%=Y%
ENDPROC
