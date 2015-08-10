REM Example program to create a binary tree and display
REM its contents in alphabetical order of the name.
REM This is the same as 'trees1' but it uses indirection
REM operators
:
DIM heap% 1000
heaptop% = heap%
:
REM 'struct' for a tree entry
name%=0: value%=20: left%=24: right%=28: nodesize%=32
root% = 0
:
FOR N%=1 TO 10
READ X$, X%
PROCadd(X$, X%)
NEXT
PROCshow(root%)
END
:
DATA red, 5, green, 10, yellow, 15, blue, 20, black, 25, white, 30
DATA orange, 35, pink, 40, cyan, 45, purple, 50
:
:
DEF PROCadd(name$, data%)
LOCAL node%
node% = FNalloc(nodesize%)
$(node%+name%) = name$
node%!value% = data%
node%!left% = 0
node%!right% = 0
IF root%=0 THEN root% = node%: ENDPROC
P% = root%
REPEAT
IF name$<$(P%+name%) THEN
IF P%!left%<>0 THEN P% = P%!left% ELSE P%!left% = node%: ENDPROC
ELSE
IF P%!right%<>0 THEN P% = P%!right% ELSE P%!right% = node%: ENDPROC
ENDIF
UNTIL FALSE
ENDPROC
:
:
DEF PROCshow(P%)
IF P%!left%<>0 THEN PROCshow(P%!left%)
PRINT $(P%+name%);TAB(20);P%!value%
IF P%!right%<>0 THEN PROCshow(P%!right%)
ENDPROC
:
:
DEF FNalloc(size%)
LOCAL P%
P% = heaptop%
heaptop%+=size%
=P%