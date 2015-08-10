REM this program draws Pascal's triangle

SIZE% = 12
DIM prev%(SIZE%), next%(SIZE%)
next%() = 0
next%(0) = 1
FOR I%=0 TO SIZE%-1
  PRINT TAB(35-3*I%);next%(0);
  IF I%>0 THEN
    FOR J%=1 TO I%
      next%(J%) = prev%(J%)+prev%(J%-1)
      PRINT RIGHT$("     "+STR$next%(J%), 6);
    NEXT
  ENDIF
  PRINT
  prev%() = next%()
NEXT
END
