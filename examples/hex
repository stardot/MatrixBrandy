REM Work out solutions to the "hex" problem. The idea here
REM is to find all the solutions to a magic square where the
REM numbers 1 to 19 have to be arranged in a hex pattern
REM so that all the straight lines add up to 38 thus:
REM
REM          xx  xx  xx
REM        xx  xx  xx  xx
REM      xx  xx  xx  xx  xx
REM        xx  xx  xx  xx
REM          xx  xx  xx
:
threesize%=200
:
DIM jval%(threesize%), kval%(threesize%), nextpos%(threesize%)
DIM xlistpos%(19), used%(19), ring%(19), checks%(6,3), does%(6)
:
T=TIME
checks%(1,1)=8:   checks%(1,2)=12:   checks%(1,3)=18
checks%(2,1)=2:   checks%(2,2)=10:   checks%(2,3)=18
checks%(3,1)=4:   checks%(3,2)=12:   checks%(3,3)=13
checks%(4,1)=6:   checks%(4,2)=10:   checks%(4,3)=17
checks%(5,1)=2:   checks%(5,2)=6:    checks%(5,3)=14
checks%(6,1)=4:   checks%(6,2)=8:    checks%(6,3)=16
does%(1)=17
does%(2)=13
does%(3)=14
does%(4)=16
does%(5)=15
does%(6)=15
nosol%=0
num%=0
FOR I%=1 TO 19
  used%(I%)=FALSE
  xlistpos%(I%)=0
NEXT
:
FOR I%=3 TO 19
  FOR J%=1 TO 19
    IF I%<>J% THEN
      FOR K%=3 TO 19
        IF (K%<>J%) AND (K%<>I%) AND (I%+J%+K%=38) THEN
          num%+=1
          next%=num%
          jval%(next%)=J%
          kval%(next%)=K%
          nextpos%(next%)=xlistpos%(I%)
          xlistpos%(I%)=next%
        ENDIF
      NEXT
    ENDIF
  NEXT
NEXT
PRINT FNdec(num%,3);" number groups generated"
:
FOR I%=3 TO 19
  P%=xlistpos%(I%)
  used%(I%)=TRUE
  WHILE P%<>0
    J%=jval%(P%)
    K%=kval%(P%)
    used%(J%)=TRUE
    used%(K%)=TRUE
    ring%(1)=I%
    ring%(2)=J%
    curr%=2
    PROCfillin(K%)
    used%(J%)=FALSE
    used%(K%)=FALSE
    P%=nextpos%(P%)
  ENDWHILE
  used%(I%)=FALSE
NEXT
PRINT"There are";FNdec(nosol%,3);" solutions"
PRINT"Time taken: ";(TIME-T)/100;" seconds"
END
:
:
DEF FNdec(X%,W%)=RIGHT$("          "+STR$X%,W%)
:
:
DEF PROCblockin(target%)
LOCAL total%, poss%, I%
IF target%>5 THEN
  IF (38-ring%(4)-ring%(8)-ring%(16))=ring%(15) THEN
    poss%=1
    WHILE used%(poss%)
      poss%+=1
    ENDWHILE
    IF (38-ring%(11)-ring%(18)-ring%(15)-ring%(5))=poss% THEN
      ring%(19)=poss%
      nosol%+=1
      PRINT"Solution no.";FNdec(nosol%,3)''
      PRINT"          ";FNdec(ring%(1),2);FNdec(ring%(2),4);FNdec(ring%(3),4)
      PRINT"        ";FNdec(ring%(12),2);FNdec(ring%(13),4);FNdec(ring%(14),4);FNdec(ring%(4),4)
      PRINT"      ";FNdec(ring%(11),2);FNdec(ring%(18),4);FNdec(ring%(19),4);FNdec(ring%(15),4);FNdec(ring%(5),4)
      PRINT"        ";FNdec(ring%(10),2);FNdec(ring%(17),4);FNdec(ring%(16),4);FNdec(ring%(6),4)
      PRINT"          ";FNdec(ring%(9),2);FNdec(ring%(8),4);FNdec(ring%(7),4)''
    ENDIF
  ENDIF
ELSE
  total%=38
  FOR I%=1 TO 3
    total%=total%-ring%(checks%(target%,I%))
  NEXT
  IF (total%>0) AND (total%<20) THEN
    IF NOT used%(total%) THEN
      used%(total%)=TRUE
      ring%(does%(target%))=total%
      PROCblockin(target%+1)
      used%(total%)=FALSE
    ENDIF
  ENDIF
ENDIF
ENDPROC
:
DEF PROCfillin(pi%)
LOCAL I%, pj%, pk%, pos1%, ptr%
ptr%=xlistpos%(pi%)
WHILE ptr%<>0
  pj%=jval%(ptr%)
  pk%=kval%(ptr%)
  IF NOT used%(pj%) AND NOT used%(pk%) THEN
    used%(pj%)=TRUE
    used%(pk%)=TRUE
    ring%(curr%+curr%-1)=pi%
    ring%(curr%+curr%)=pj%
    curr%+=1
    IF curr%<7 THEN PROCfillin(pk%)
    used%(pk%)=FALSE
    used%(pj%)=FALSE
    curr%-=1
  ELSE
    IF curr%=6 THEN
      IF NOT used%(pj%) AND pk%=ring%(1) THEN
        used%(pj%)=TRUE
        ring%(11)=pi%
        ring%(12)=pj%
        FOR I%=1 TO 19
          IF NOT used%(I%) THEN
            ring%(18)=I%
            used%(I%)=TRUE
            PROCblockin(1)
            used%(I%)=FALSE
          ENDIF
        NEXT
        used%(pj%)=FALSE
      ENDIF
    ENDIF
  ENDIF
  ptr%=nextpos%(ptr%)
ENDWHILE
ENDPROC
