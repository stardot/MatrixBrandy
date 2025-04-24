#!sbrandy
REM https://testanything.org/
PRINT "1..3"

Colour% = 1
Pi = 0

FOR I% = 0 TO 10
Colour% += I%
Pi += I%
NEXT

REM Assertions
IF Colour% = 56     THEN PRINT "ok 1" ELSE PRINT "not ok 1"
IF Pi = 55          THEN PRINT "ok 2" ELSE PRINT "not ok 2"
IF Colour% - Pi = 1 THEN PRINT "ok 3" ELSE PRINT "not ok 3"
