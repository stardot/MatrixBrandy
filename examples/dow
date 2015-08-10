DIM month$(12)
month$()="", "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"
REPEAT
  INPUT"Enter date in the form dd,mm,yy: " dd%, mm%, yy%
UNTIL dd%>=1 AND dd%<=31 AND mm%>=1 AND mm%<=12
IF yy%<=99 THEN 
  IF yy%<70 THEN yy%+=2000 ELSE yy%+=1900
ENDIF
IF mm%<3 THEN
factor=365*yy%+dd%+31*(mm%-1)+(yy%-1) DIV 4-INT(0.75*INT(((yy%-1)/100)+1))
ELSE
factor=365*yy%+dd%+31*(mm%-1)-INT(0.4*mm%+2.3)+INT(yy%/4)-INT(0.75*(yy% DIV 100+1))
ENDIF
dow%=factor-factor DIV 7*7
PRINT month$(mm%);" ";dd%;", ";yy%;" is a ";
CASE dow% OF
WHEN 0: PRINT"Saturday"
WHEN 1: PRINT"Sunday"
WHEN 2: PRINT"Monday"
WHEN 3: PRINT"Tuesday"
WHEN 4: PRINT"Wednesday"
WHEN 5: PRINT"Thursday"
WHEN 6: PRINT"Friday"
ENDCASE
END

