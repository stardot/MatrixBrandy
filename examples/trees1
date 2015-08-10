REM Example program to create a binary tree and display
REM its contents in alphabetical order of the name
:
DIM name$(100),value%(100),left%(100),right%(100)
next%=1
root%=0
FOR N%=1 TO 10
READ X$,X%
PROCadd(X$,X%)
NEXT
PROCshow(root%)
END
:
DATA red, 5, green, 10, yellow, 15, blue, 20, black, 25, white, 30
DATA orange, 35, pink, 40, cyan, 45, purple, 50
:
:
DEF PROCadd(name$,value%)
name$(next%)=name$
value%(next%)=value%
left%(next%)=0
right%(next%)=0
IF root%=0 THEN root%=1: next%=2: ENDPROC
P%=root%
done%=FALSE
REPEAT
IF name$<name$(P%) THEN
IF left%(P%)<>0 THEN P%=left%(P%) ELSE left%(P%)=next%: done%=TRUE
ELSE
IF right%(P%)<>0 THEN P%=right%(P%) ELSE right%(P%)=next%: done%=TRUE
ENDIF
UNTIL done%
next%+=1
ENDPROC
:
:
DEF PROCshow(P%)
IF left%(P%)<>0 THEN PROCshow(left%(P%))
PRINT name$(P%);TAB(20);value%(P%)
IF right%(P%)<>0 THEN PROCshow(right%(P%))
ENDPROC
