REM NOTE: This program will ONLY run in an xterm window under NetBSD and Linux.
REM
REM It uses the Tektronics graphics terminal emulation that xterm provides
REM to draw a simple graph in the xterm 'Tek' window.
:
REM To run the program:
REM 1) Load it in the normal way
REM 2) Switch output to the xterm 'Tek' window
REM 3) Run the program in the normal way
REM 4) Switch output back to the xterm 'VT' window
:
REM To switch xterm between 'Tek' and 'vt' modes, press 'Ctrl' and the
REM middle mouse button to bring up the xterm 'VT Options' menu. The
REM last-but-one entry on this is 'Switch to Tek Mode'. Select this
REM option to display the window 'tektronix(Tek)'. To switch back,
REM press 'Ctrl' and the middle mouse button to display the 'Tek Options'
REM menu. Select the option 'Switch to VT Mode' to return xterm to
REM normal operation.
:
LIBRARY"examples/teklib"
PROCmode(0)
PROCorigin(1024, 750)
xlow = -10
xhigh = 10
ylow = -10
yhigh = 10
depth = 8
xscale = 50
yscale = 20
c = -4000
:
FOR x = xlow TO xhigh
  PROCmove(xscale*(x+ylow), yscale*(ylow-x)+c/(x*x+ylow*ylow+depth))
  FOR y = ylow TO yhigh
    PROCdraw(xscale*(x+y), yscale*(y-x)+c/(x*x+y*y+depth))
  NEXT
NEXT
FOR y = ylow TO yhigh
  PROCmove(xscale*(xlow+y), yscale*(y-xlow)+c/(xlow*xlow+y*y+depth))
  FOR x = xlow TO xhigh
    PROCdraw(xscale*(x+y), yscale*(y-x)+c/(x*x+y*y+depth))
  NEXT
NEXT
END
