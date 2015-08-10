REM Sieve of Eratosthenes Prime Number Program
:
size%=10000
iterations%=10
:
DIM flags%(size%)
:
T=TIME
PRINT;iterations%;" iterations."
FOR C%=1 TO iterations%
  PRINT"Doing ";C%
  count%=0
  flags%()=TRUE
  FOR I%=0 TO size%
    IF flags%(I%) THEN
      prime%=I%+I%+3
      K%=I%+prime%
      WHILE K%<=size%
        flags%(K%)=FALSE
        K%+=prime%
      ENDWHILE
      count%+=1
    ENDIF
  NEXT
NEXT
PRINT"There are ";count%;" primes"
PRINT"Time taken=";(TIME-T)/100;" seconds"
END
