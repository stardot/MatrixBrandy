REM This example program solves the "Towers of Hanoi" puzzle
:
INPUT "Number of discs? " number%
steps% = 0
T = TIME
PROChanoi("left", "middle", "right", number%)
PRINT;steps%;" steps in ";(TIME-T)/100;" seconds"
END
:
:
DEF PROChanoi(from$, using$, to$, N%)
IF N%=0 THEN ENDPROC
PROChanoi(from$, to$, using$, N%-1)
PRINT STRING$(N%, " ");"Move disc "; N%; " from "; from$; " to "; to$
steps%+=1
PROChanoi(using$, from$, to$, N%-1)
ENDPROC


