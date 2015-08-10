REM NOTE: This program will only run using a version of the
REM interpreter that supports graphics.
:
MODE 27
ORIGIN 640, 512
xlow = -10
xhigh = 10
ylow = -10
yhigh = 10
depth = 10
xscale = 30
yscale = 12
c = -4000
:
FOR x = xlow TO xhigh
  MOVE xscale*(x+ylow), yscale*(ylow-x)+c/(x*x+ylow*ylow+depth)
  FOR y = ylow TO yhigh
    DRAW xscale*(x+y), yscale*(y-x)+c/(x*x+y*y+depth)
  NEXT
NEXT
FOR y = ylow TO yhigh
  MOVE xscale*(xlow+y), yscale*(y-xlow)+c/(xlow*xlow+y*y+depth)
  FOR x = xlow TO xhigh
    DRAW xscale*(x+y), yscale*(y-x)+c/(x*x+y*y+depth)
  NEXT
NEXT
END
