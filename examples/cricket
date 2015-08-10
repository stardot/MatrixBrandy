   10REM This program is a simple cricket simulation
   20REM It plays a test match between England and New Zealand
   30REM The teams are from the 1999 New Zealand tour.
   40:
   50REM Press the space bar at the end of each innings to start
   60REM the next one
   70:
   80pl=11: stroke%=15
   90rev$=CHR$17+CHR$129+CHR$17+CHR$0
  100norm$=CHR$17+CHR$0+CHR$17+CHR$129
  110MODE 6
  120OFF
  130VDU 23,255,0,0,0,0,255,0,0,0
  140DIM players_score%(2),inplay%(2),runs%(stroke%),skill(2),innings%(4),player$(11,2),bat(11,2),bowl(11,2),teams$(2),wk%(2),cap%(2)
  150ps=0.23
  160n=1: followon%=FALSE: needed%=100000000
  170FOR I%=1 TO stroke%
  180READ runs%(I%)
  190NEXT
  200FOR I%=1 TO 2
  210READ teams$(I%)
  220FOR J%=1 TO 11
  230READ N$,b,bowl(J%,I%)
  240player$(J%,I%)=N$
  250IF LEFT$(N$,1)="^" THEN wk%(I%)=J%
  260IF LEFT$(N$,1)="*" THEN cap%(I%)=J%
  270bat(J%,I%)=FNff(b)
  280NEXT J%,I%
  290batting%=RND(2)
  300:
  310REM -- Start of Innings --
  320:
  330CLS
  340PRINT TAB(7);teams$(batting%);
  350IF n<3 THEN PRINT" - First Innings" ELSE PRINT" - Second Innings"
  360overs%=0: facing%=1: wickets%=0: score%=0: nextin%=3: balls%=1: extras%=0
  370VDU 31,3,13: PRINT"Extras": VDU 31,39,13,ASC"0"
  380VDU 31,37,14: PRINT"ÿÿÿ"
  390VDU 31,0,15: PRINT"    Overs:   0.0  Wkts:  0  Total:"
  400PRINT'"Fall of Wickets:"
  410PRINT"  1   2   3   4   5   6   7   8   9  10"
  420fall%=0
  430inplay%(1)=1: skill(1)=bat(1,batting%): players_score%(1)=0
  440inplay%(2)=2: skill(2)=bat(2,batting%): players_score%(2)=0
  450VDU 31,0,2: PRINT" 1 ";player$(1,batting%);TAB(39);"0"
  460VDU 31,0,3: PRINT" 2 ";player$(2,batting%);TAB(39);"0"
  470VDU 17,1,31,2,2,62,17,3
  480:
  490REM  -- Play loop --
  500:
  510REM Change the +8 to +4500 to make game run in real time
  520t2=TIME+8
  530WHILE TIME<t2: ENDWHILE
  540VDU 31,15,15,balls%+48
  550IF RND(1)>ps THEN 1130
  560IF RND(20)=1 THEN 1080
  570IF RND(1)>skill(facing%) THEN 730
  580S%=runs%(RND(stroke%))
  590players_score%(facing%)+=S%
  600score%+=S%
  610VDU 31,37,1+inplay%(facing%): PRINT;FNdec(players_score%(facing%),3)
  620VDU 31,37,15: PRINT;FNdec(score%,3)
  630IF(S% AND 1)=1 THEN
  640VDU 31,2,1+inplay%(facing%),32
  650facing%=3-facing%
  660VDU 17,1,31,2,1+inplay%(facing%),62,17,3
  670ENDIF
  680IF score%>needed% THEN 1670
  690GOTO 1130
  700:
  710REM -- Batsman out --
  720:
  730howout%=RND(24)-2
  740IF howout%<1 THEN 1080
  750wickets%=wickets%+1
  760fielders%=3-batting%
  770o$=MID$("CCCCCCCCBBBBBBBLLLLLRS",howout%,1)
  780VDU 31,2,1+inplay%(facing%),32
  790VDU 31,13,inplay%(facing%)+1
  800IF o$="R" THEN PRINT"Run out": GOTO 950
  810REPEAT
  820B%=RND(11)
  830IF B%<6 AND RND(1)<0.8-overs%/200 THEN B%=RND(11)
  840UNTIL RND(1)<bowl(B%,fielders%)
  850IF o$<>"C" THEN 920
  860C%=RND(11)
  870IF C%=B% AND RND(1)<0.5 THEN C%=RND(11)
  880name$=player$(C%,fielders%)
  890IF LEFT$(name$,1)="^" THEN name$=MID$(name$,2)
  900IF C%=B% THEN PRINT"C.& "; ELSE PRINT"C.";name$;" ";
  910GOTO 940
  920IF o$="L" THEN PRINT"LBW ";
  930IF o$="S" THEN PRINT"St.";MID$(player$(wk%(fielders%),fielders%),2)
  940VDU 31,25,inplay%(facing%)+1: PRINT"B.";player$(B%,fielders%)
  950VDU 31,24,15: PRINT;FNdec(wickets%,2)
  960VDU 31,fall%,19: PRINT;FNdec(score%,3): fall%=fall%+4
  970IF wickets%>9 THEN 1280
  980REM
  990REM -- New batsman --
 1000REM
 1010players_score%(facing%)=0
 1020inplay%(facing%)=nextin%
 1030skill(facing%)=bat(nextin%,batting%)
 1040VDU 31,0,nextin%+1: PRINT;FNdec(nextin%,2);" ";player$(nextin%,batting%):  VDU 31,39,nextin%+1,ASC"0"
 1050VDU 17,1,31,2,1+inplay%(facing%),62,17,3
 1060nextin%=nextin%+1
 1070GOTO 1130
 1080extras%=extras%+1
 1090score%=score%+1
 1100balls%=balls%-1
 1110VDU 31,37,13: PRINT;FNdec(extras%,3)
 1120VDU 31,37,15: PRINT;FNdec(score%,3)
 1130balls%=balls%+1
 1140IF balls%<7 THEN 520
 1150REM
 1160REM -- New Over --
 1170REM
 1180overs%=overs%+1
 1190balls%=1
 1200VDU 31,2,1+inplay%(facing%),32
 1210facing%=3-facing%
 1220VDU 17,1,31,2,1+inplay%(facing%),62,17,3
 1230VDU 31,11,15: PRINT;FNdec(overs%,3);".0"
 1240GOTO 520
 1250REM
 1260REM -- End of Innings --
 1270REM
 1280VDU 31,13,inplay%(3-facing%)+1
 1290PRINT"Not out";
 1300VDU 31,0,21
 1310innings%(n)=score%
 1320IF followon% THEN 1830
 1330ON n GOTO 1340,1390,1570,1670
 1340PRINT teams$(batting%);" first innings total=";score%
 1350n=2
 1360A%=GET
 1370batting%=3-batting%
 1380GOTO 330
 1390diff%=innings%(2)-innings%(1)
 1400IF diff%<-199 THEN 1520
 1410IF diff%<0 THEN 1470
 1420PRINT teams$(batting%);" lead by ";diff%;" on first innings"
 1430n=3
 1440A%=GET
 1450batting%=3-batting%
 1460GOTO 330
 1470PRINT teams$(batting%);" trail by ";-diff%;" on first innings"
 1480n=3
 1490A%=GET
 1500batting%=3-batting%
 1510GOTO 330
 1520PRINT teams$(batting%);" are forced to follow on ";-diff%;" runs behind"
 1530n=4
 1540followon%=TRUE
 1550A%=GET
 1560GOTO 330
 1570needed%=innings%(1)+innings%(3)-innings%(2)
 1580batting%=3-batting%
 1590IF needed%<0 THEN 1640
 1600PRINT teams$(batting%);" need ";needed%;" runs to win"
 1610n=4
 1620A%=GET
 1630GOTO 330
 1640PRINT teams$(batting%);" win by an innings and ";-needed%;" runs"
 1650ON
 1660END
 1670VDU 31,0,21
 1680IF needed%<score% THEN 1770
 1690IF needed%=score% THEN 1740
 1700batting%=3-batting%
 1710PRINT teams$(batting%);" win by ";needed%-score%;" runs"
 1720ON
 1730END
 1740PRINT"The match is a tie"
 1750ON
 1760END
 1770wickets%=10-wickets%
 1780PRINT teams$(batting%);" win by ";wickets%;" wicket";
 1790IF wickets%>1 THEN PRINT"s";
 1800PRINT
 1810ON
 1820END
 1830needed%=innings%(1)-innings%(2)-innings%(4)
 1840IF needed%>0 THEN 1920
 1850needed%=1-needed%
 1860batting%=3-batting%
 1870PRINT teams$(batting%);" need ";needed%;" runs to win"
 1880n=4
 1890followon%=FALSE
 1900A%=GET
 1910GOTO 330
 1920batting%=3-batting%
 1930PRINT teams$(batting%);" win by an innings and ";needed%;" runs"
 1940ON
 1950END
 1960:
 1970DEF FNdec(X%,W%)=RIGHT$("         "+STR$X%,W%)
 1980DEF FNff(x)=(x+0.7)*0.45
 1990DATA 1,1,1,1,1,2,2,2,2,3,3,3,4,4,6
 2000:
 2010:
 2020DATA"England"
 2030DATA"Stewart"     ,1.40, 0
 2040DATA"Butcher"     ,1.40, 0.55
 2050DATA"Hussain"     ,1.40, 0
 2060DATA"Thorpe"      ,1.40, 0
 2070DATA"Ramprakash"  ,1.40, 0.55
 2080DATA"Habib"       ,1.40, 0
 2090DATA"^Read"       ,1.20, 0
 2100DATA"Tudor"       ,1.05, 0.70
 2110DATA"Caddick"     ,1.05, 0.7
 2120DATA"Mullally"    ,1.05, 0.68
 2130DATA"Tuffnell"    ,1.05, 0.7
 2140:
 2150:
 2160DATA"New Zealand"
 2170DATA"Horne"       ,1.40, 0
 2180DATA"Astle"       ,1.40, 0
 2190DATA"Fleming"     ,1.40, 0
 2200DATA"Twose"       ,1.40, 0.65
 2210DATA"McMillan"    ,1.40, 0
 2220DATA"^Parore"     ,1.25, 0
 2230DATA"Doull"       ,1.20, 0.67
 2240DATA"Cairns"      ,1.15, 0.67
 2250DATA"Nash"        ,1.05, 0.67
 2260DATA"Vettori"     ,1.05, 0.67
 2270DATA"Allott"      ,1.05, 0.67
