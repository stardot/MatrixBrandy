REM This program compares a comb sort to a bubble sort
:
size%=2000
DIM table%(size%)
PROCsetup4
PROCbubblesort
PROCsetup4
PROCcombsort
END
:
:
DEF PROCsetup1
PRINT"Ascending order ";
FOR I%=1 TO size%
table%(I%)=I%
NEXT
ENDPROC
:
:
DEF PROCsetup2
PRINT"Descending order ";
FOR I%=1 TO size%
table%(I%)=size%-I%
NEXT
ENDPROC
:
:
DEF PROCsetup3
PRINT"Random order ";
I%=RND(-65656)
FOR I%=1 TO size%
table%(I%)=RND(100000)
NEXT
ENDPROC
:
:
DEF PROCsetup4
PRINT"Semi-ordered ";
I%=RND(-18940606)
N%=1
WHILE N%<=size%
L%=RND(20)+1
IF N%+L%>size% THEN L%=size%-N%
X%=RND(100000)
FOR I%=1 TO L%
table%(N%)=X%
X%+=RND(100)
N%+=1
NEXT
ENDWHILE
ENDPROC
:
:
DEF PROCcheck
FOR N%=1 TO size%-1
  IF table%(N%)>table%(N%+1) THEN PRINT"Out of order at ";N%
NEXT
ENDPROC
::
DEF PROCcombsort
PRINT"Comb sort ";
T=TIME
gap%=size%
REPEAT
gap%=gap%/1.3: IF gap%<1 THEN gap%=1
switch%=FALSE
FOR I%=1 TO size%-gap%
J%=I%+gap%
IF table%(I%)>table%(J%) THEN SWAP table%(I%),table%(J%): switch%=TRUE
NEXT
UNTIL NOT switch% AND gap%=1
PRINT TIME-T
PROCcheck
ENDPROC
:
:
DEF PROCbubblesort
PRINT"Bubble sort ";
T=TIME
H%=size%-1
REPEAT
M%=0
FOR I%=1 TO H%
IF table%(I%)>table%(I%+1) THEN SWAP table%(I%),table%(I%+1): M%=I%
NEXT
H%=M%
UNTIL M%=0
PRINT TIME-T
PROCcheck
ENDPROC
