REM This program uses the ARGC and ARGV$ functions to display what was
REM passed to the program on the command line
:
REM ARGV$ 0 is the name of the interpreter or an empty string
REM ARGV$ 1 onwards are the parameters
REM ARGC returns the index of the highest parameter
:
count% = ARGC
IF count%=0 THEN
  PRINT"No parameters supplied"
ELSE
  PRINT"Parameters:"
  FOR N%=1 TO count%
  PRINT;N%;TAB(5);ARGV$(N%)
  NEXT
ENDIF
