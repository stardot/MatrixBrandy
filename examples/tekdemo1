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
x%=0
PROCorigin(0, 768)
PROCline(0, 0, 1800, 0)
PROCline(0, -500, 0, 500)
PROCmove(0, 0)
FOR angle=0 TO 2*PI STEP PI/50
PROCdraw(x%, 500*SIN(angle))
x%+=16
NEXT
END
