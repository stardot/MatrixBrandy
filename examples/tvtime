REM This program is a simple adventure-type game. The object is to
REM watch your favourite TV programme
:
PROCinit
PROCdescribe
REPEAT
  PROCread_line
  PROCparse_line
  PROCaction
UNTIL finished%
END
:
DEF PROCread_line
REPEAT
  LINE INPUT "Ok " text$
UNTIL LEN text$>0
FOR N%=1 TO LEN text$
  MID$(text$,N%,1) = FNtolower(MID$(text$,N%,1))
NEXT
ENDPROC
:
:
DEF FNtolower(ch$) = xlate$(ASC ch$)
:
:
DEF FNfind_verb(word$)
FOR N% = 1 TO verbcount%
  IF verb$(N%)=word$ THEN =N%
NEXT
=0
:
:
DEF FNfind_noun(word$)
FOR N% = 1 TO nouncount%
  IF noun$(N%) = word$ THEN =N%
NEXT
=0
:
:
DEF FNextract
LOCAL start%
WHILE index%<=len% AND MID$(text$, index%, 1) = " ": index%+=1: ENDWHILE
IF index%>len% THEN =""
start% = index%
WHILE index%<=len% AND MID$(text$, index%, 1)<>" ": index%+=1: ENDWHILE
=MID$(text$, start%, index%-start%)
:
:
DEF PROCparse_line
LOCAL len%, index%
len% = LEN text$
index% = 1
verb$ = FNextract
IF verb$="" THEN ENDPROC:  REM Nothing on line
IF FNfind_verb(verb$)=0 THEN PRINT"The verb '";verb$;"' is not known"
noun$ = FNextract
IF noun$="on" OR noun$="in" THEN noun$=FNextract
IF noun$<>"" THEN
  IF FNfind_noun(noun$)=0 THEN PRINT"The word '";noun$;"' is not known": verb$ = ""
ENDIF
prep$ = FNextract
IF prep$="" THEN ENDPROC
IF INSTR("in into on onto to from", prep$) THEN
  noun2$ = FNextract
  IF noun2$="" THEN
    PRINT"Another noun is needed after '";prep$;"'": verb$=""
  ELSE
    IF FNfind_noun(noun2$)=0 THEN PRINT"The word '";noun2$;"' is not known": verb$ = ""
  ENDIF
ENDIF
ENDPROC
:
:
DEF PROCdescribe
LOCAL excount%, N%, done%
PRINT "You are ";description$(location%);
excount%=0
FOR N%=1 TO 4
  IF exits%(location%, N%)<>0 THEN excount%+=1
NEXT
PRINT". You can go ";
done%=0
FOR N%=1 TO 4
  IF exits%(location%, N%)<>0 THEN 
    PRINT directions$(N%);
    done%+=1
    IF done%=excount%-1 THEN
      PRINT" and ";
    ELSE
      IF done%<>excount% THEN PRINT", ";
    ENDIF
  ENDIF
NEXT
PRINT
ENDPROC
:
:
DEF PROCaction
IF verb$="" THEN ENDPROC    :REM No verb supplied - Nothing to do
time%+=1
CASE verb$ OF
WHEN "north", "n", "east", "e", "south", "s", "west", "w": PROCmove
WHEN "open": PROCopen
WHEN "close": PROCclose
WHEN "unlock": PROCunlock
WHEN "lock": PROClock
WHEN "get", "take": PROCget
WHEN "drop": PROCdrop
WHEN "kick": PROCkick
WHEN "switch", "turn": PROCswitch
WHEN "screw", "attach": PROCattach
WHEN "buy": PROCbuy
WHEN "look": PROClook
WHEN "examine": PROCexamine
WHEN "watch": PROCwatch
WHEN "inv", "invent": PROCinventory
WHEN "save": PROCsave
WHEN "restore": PROCrestore
WHEN "quit": PROCquit
WHEN "help": PROChelp
ENDCASE
ENDPROC
:
:
REM FNart returns an indefinite article for the word 'word$'
:
DEF FNart(word$)
IF word$="money" THEN ="some "
IF INSTR("aeiou", LEFT$(word$, 1)) THEN ="an " ELSE ="a "
:
:
DEF PROCnonoun
PRINT verb$;" what?"
ENDPROC
:
:
DEF PROCnotpossible
PRINT"You cannot ";verb$;" ";FNart(noun$);noun$
ENDPROC
:
:
REM FNfind returns the index in the object tables of item 'item$'
:
DEF FNfind(item$)
FOR N%=1 TO objcount%
  IF objname$(N%)=item$ THEN =N%
NEXT
REM Should never get here
PRINT"??? Cannot find ";item$
STOP
=0
:
:
REM FNishere returns TRUE if the item object% is either being carried
REM by the player, is at this location or is on something at this location
:
DEF FNishere(object%)
LOCAL where%, holder%
where% = objloc%(object%)
IF where%>=0 THEN =where%=player% OR where%=location%
holder% = FNfind(objname$(-where%))
=objloc%(holder%)=player% OR objloc%(holder%)=location%
:
:
DEF FNcarried(item$)
LOCAL object%
object% = FNfind(item$)
=objloc%(object%)=player%
:
:
REM PROCscan lists the items found at location or object 'where'.
REM If where is -ve then the items in or on an object are being listed.
REM IF it is +ve then the items at a location are being listed
:
DEF PROCscan(where%)
LOCAL N%, count%, done%
count%=0
FOR N%=1 TO objcount%
  IF objloc%(N%)=where% THEN count%+=1
NEXT
IF count%=0 THEN
  IF where%<0 THEN
    PRINT"There is nothing special about the ";objname$(-where%)
  ELSE
    PRINT"There is nothing to see here"
  ENDIF
ELSE
  done% = 0
  PRINT"There is";
  FOR N%=1 TO objcount%
    IF objloc%(N%)=where% THEN
      PRINT" ";FNart(objname$(N%));objname$(N%);
      done%+=1
      IF done%=count%-1 THEN
        PRINT" and";
      ELSE
        IF done%<>count% THEN PRINT",";
      ENDIF
    ENDIF
  NEXT
  IF where%<0 THEN
    PRINT" ";objprep$(-where%);" the ";objname$(-where%)
  ELSE
    PRINT" here"
  ENDIF
ENDIF
ENDPROC
:
:
DEF PROCmove
LOCAL newloc%
direction% = INSTR("nesw", LEFT$(verb$, 1)):    REM Only look at first letter of direction
newloc% = exits%(location%, direction%)
IF newloc%=0 THEN
  PRINT"You cannot move that way"
ELSE
  CASE newloc% OF
  WHEN hall%: IF door_locked% THEN PRINT"The front door is locked" ELSE location% = newloc%: PROCdescribe
  WHEN lounge%:
    IF tv_stolen% THEN
      PROCfailed("When you enter the room you realise that the TV is missing. Somebody stole it whilst you were out.")
    ELSE
      location% = newloc%: PROCdescribe
    ENDIF
  WHEN shop%:
REM Bad neighbourhood this - Leave your front door open and things get nicked
    IF NOT door_locked% THEN tv_stolen% = TRUE
    location% = newloc%
    PROCdescribe
  OTHERWISE
    location% = newloc%
    PROCdescribe
  ENDCASE
ENDIF
ENDPROC
:
:
DEF PROCget
LOCAL object%, N%
IF noun$="" THEN PROCnonoun: ENDPROC
object% = FNfind(noun$)
IF objloc%(object%)=player% THEN PRINT"You are already carrying the ";noun$: ENDPROC
IF NOT FNishere(object%) OR noun$="screwdriver" AND cupboard_closed% THEN PRINT"The ";noun$;" is not here": ENDPROC
IF objsize%(object%)>maxsize% THEN PRINT"The ";noun$;" is too big and heavy to move": ENDPROC
IF noun$="plug" AND location%=shop% THEN
  PROCfailed("A store detective spots you and you soon find yourself at the police station being charged with theft.")
ELSE
  IF encumbrance%+objsize%(object%)>maxload% THEN PRINT"You are already carrying as much as you can": ENDPROC
  encumbrance%+=objsize%(object%)
  objloc%(object%) = player%
  PRINT"Done"
ENDIF
ENDPROC
:
:
DEF PROCdrop
LOCAL object%
IF noun$="" THEN PROCnonoun: ENDPROC
object% = FNfind(noun$)
IF objloc%(object%)<>player% THEN PRINT"You are not carrying the ";noun$: ENDPROC
encumbrance%-=objsize%(object%)
objloc%(object%) = location%
PRINT"Done"
ENDPROC
:
:
DEF PROCopen
IF noun$="" THEN PROCnonoun: ENDPROC
IF NOT FNishere(FNfind(noun$)) THEN PRINT"There is no ";noun$;" here": ENDPROC
CASE noun$ OF
WHEN "door":
  IF NOT door_locked% THEN PRINT"The front door is already open": ENDPROC
  IF NOT FNcarried("key") THEN PRINT"You do not have a key": ENDPROC
  door_locked% = FALSE
  PRINT"Done"
WHEN "cupboard":
  IF cupboard_closed% THEN cupboard_closed% = FALSE: PRINT"Done" ELSE PRINT"The cupboard is already open"
OTHERWISE
   PROCnotpossible
ENDCASE
ENDPROC
:
:
DEF PROCclose
IF noun$="" THEN PROCnonoun: ENDPROC
IF NOT FNishere(FNfind(noun$)) THEN PRINT"There is no ";noun$;" here": ENDPROC
CASE noun$ OF
WHEN "door":
  IF door_locked% THEN PRINT"The front door is already closed": ELSE door_locked% = TRUE: PRINT"Done"
WHEN "cupboard":
  IF cupboard_closed% THEN PRINT"The cupboard is already open" ELSE cupboard_closed% = TRUE: PRINT"Done"
OTHERWISE
   PROCnotpossible
ENDCASE
ENDPROC
:
:
DEF PROCunlock
IF noun$="" THEN PROCnonoun: ENDPROC
IF NOT FNishere(FNfind(noun$)) THEN PRINT"There is no ";noun$;" here": ENDPROC
IF noun$<>"door" THEN PROCnotpossible: ENDPROC
IF NOT door_locked% THEN PRINT"The front door is already open": ENDPROC
IF NOT FNcarried("key") THEN PRINT"You do not have a key": ENDPROC
door_locked% = FALSE
PRINT"Done"
ENDPROC
:
:
DEF PROClock
IF noun$="" THEN PROCnonoun: ENDPROC
IF NOT FNishere(FNfind(noun$)) THEN PRINT"There is no ";noun$;" here": ENDPROC
IF noun$<>"door" THEN PROCnotpossible: ENDPROC
IF door_locked% THEN PRINT"The front door is already locked": ENDPROC
IF NOT FNcarried("key") THEN PRINT"You do not have a key": ENDPROC
door_locked% = TRUE
PRINT"Done"
ENDPROC
:
:
DEF PROCkick
LOCAL object%
IF noun$="" THEN PROCnonoun: ENDPROC
IF NOT FNishere(FNfind(noun$)) THEN PRINT"There is no ";noun$;" here": ENDPROC
CASE noun$ OF
WHEN "door":
  IF NOT door_locked% THEN PRINT"The front door is already open": ENDPROC
  PROCfailed("The door consists of a wood veneer over sheet steel. You break your toes and end up in hospital.")
WHEN "key", "screwdriver", "plug":
  PRINT"The ";noun$;" flies through the air and lands in a place you cannot reach"
  object% = FNfind(noun$)
  encumbrance%-=objsize%(object%)
  objloc%(object%) = 0
WHEN "cupboard", "settee", "table", "tv":
  PROCfailed("Light foot + heavy "+noun$+" = broken toes. It's hospital for you.")
OTHERWISE
  PROCnotpossible
ENDCASE
ENDPROC
:
:
DEF PROCswitch
LOCAL object%
IF noun$="" THEN PROCnonoun: ENDPROC
object% = FNfind(noun$)
IF NOT FNishere(object%) THEN PRINT"There is no ";noun$;" here": ENDPROC
IF noun$<>"tv" THEN PROCnotpossible
IF NOT tv_off% THEN PRINT"The TV is already switched on"
IF no_plug% THEN PRINT"Nothing happens when you switch on the TV": ENDPROC
tv_off% = FALSE
PROCfinish
ENDPROC
:
:
REM PROCattach handles the 'attach' verb. The only thing it allows
REM is for the plug to be attached to the TV's mains lead
:
DEF PROCattach
LOCAL object%
IF noun$="" THEN PROCnonoun: ENDPROC
IF NOT FNcarried(noun$) THEN PRINT"You are not carrying the ";noun$: ENDPROC
IF noun$<>"plug" THEN PROCnotpossible
IF NOT FNcarried("screwdriver") THEN PRINT"You do not have everything you need to do this": ENDPROC
IF NOT FNishere(FNfind("tv")) THEN PRINT"There is no ";noun$;" here": ENDPROC
no_plug% = FALSE
object% = FNfind(noun$)
encumbrance%-=objsize%(object%)
objloc%(object%) = -tv%
PRINT"Done"
ENDPROC
:
:
REM PROCbuy deals with the 'buy' verb. The only thing that can be
REM bought is a plug at the electrical shop
:
DEF PROCbuy
LOCAL object%
IF noun$="" THEN PROCnonoun: ENDPROC
IF location%<>shop% THEN PRINT"You cannot buy anything here": ENDPROC
object% = FNfind(noun$)
IF objloc%(object%)=player% THEN PRINT"You are already carrying the ";noun$: ENDPROC
IF NOT FNishere(object%) THEN PRINT"The ";noun$;" is not here": ENDPROC
IF NOT FNcarried("money") THEN PRINT"You do not have any money on you": ENDPROC
PRINT"You pay the shop assistant for the ";noun$;
IF objsize%(object%)>maxsize% THEN
  PRINT" but it is too large to carry away"
ELSE
  objloc%(object%) = player%
  PRINT
ENDIF
objloc%(FNfind("money")) = 0
ENDPROC
:
:
DEF PROCwatch
LOCAL object%
IF noun$="" THEN PROCnonoun: ENDPROC
IF noun$<>"tv" THEN PROCnotpossible: ENDPROC
object% = FNfind(noun$)
IF NOT FNishere(object%) THEN PRINT"The ";noun$;" is not here": ENDPROC
IF no_plug% THEN PRINT"The TV set does not appear to work": ENDPROC
tv_off% = FALSE
PROCfinish
ENDPROC
:
:
REM PROClook says what it as the current location
:
DEF PROClook
PROCscan(location%)
ENDPROC
:
:
REM PROCexamine lets the player find out interesting things about objects
:
DEF PROCexamine
LOCAL where%, object%, holder%
IF noun$="" THEN PROCnonoun: ENDPROC
object% = FNfind(noun$)
where% = objloc%(object%)
IF where%<>player% THEN
  IF where%>0 THEN
    IF where%<>location% THEN PRINT"There is no ";noun$;" here": ENDPROC
  ELSE
REM Object is in or on something. Is that item at this location?
    holder% = FNfind(objname$(-where%))
    where% = objloc%(holder%)
    IF where%<>player% AND where%<>location% THEN PRINT"There is no ";noun$;" here": ENDPROC
  ENDIF
ENDIF
CASE TRUE OF
WHEN noun$="door":
  IF door_locked% THEN PRINT"The door is locked" ELSE PRINT"The door is open"
WHEN noun$="cupboard":
  IF cupboard_closed% THEN PRINT"The cupboard door is closed" ELSE PROCscan(-object%)
WHEN noun$="tv":
  IF no_plug% THEN
    PRINT"It is an ordinary TV but you notice that there is no plug on the mains lead"
  ELSE
    PRINT"The TV is switched ";
    IF tv_off% THEN PRINT "off" ELSE PRINT"on"
  ENDIF
OTHERWISE
  PROCscan(-object%)
ENDCASE
ENDPROC
:
:
DEF PROCinventory
LOCAL N%
IF encumbrance%=0 THEN PRINT"You are not carrying anything": ENDPROC
PRINT"You are carrying:"
FOR N%=1 TO objcount%
  IF objloc%(N%)=player% THEN PRINT"  ";FNart(objname$(N%));objname$(N%)
NEXT
ENDPROC
:
:
DEF PROCsave
REPEAT
  INPUT "Name of game file: " filename$
UNTIL filename$<>""
F%=0
ON ERROR LOCAL PRINT"Could not create game file: ";REPORT$: IF F%<>0 THEN CLOSE#F%: ENDPROC ELSE ENDPROC
F%=OPENOUT filename$
PRINT#F%,location%, encumbrance%, time%, door_locked%, cupboard_closed%, no_plug%, tv_off%, tv_stolen%
FOR N%=1 TO objcount%
  PRINT#F%,objloc%(N%)
NEXT
CLOSE#F%
PRINT"Done"
ENDPROC
:
:
DEF PROCrestore
REPEAT
  INPUT "Name of game file to be restored: " filename$
UNTIL filename$<>""
F%=OPENIN filename$
IF F%=0 THEN PRINT"File '";filename$;"' not found": ENDPROC
INPUT#F%,location%, encumbrance%, time%, door_locked%, cupboard_closed%, no_plug%, tv_off%, tv_stolen%
FOR N%=1 TO objcount%
  INPUT#F%,objloc%(N%)
NEXT
CLOSE#F%
PROCdescribe
ENDPROC
:
:
REM PROCfinish is called when the player completes the game
:
DEF PROCfinish
IF time%>timelimit% THEN
  PROCfailed("Unfortunately you find that the news is on, which follows what you wanted to see.")
ELSE
  PRINT"Done it! You settle back to enjoy your favourite programme"
ENDIF
finished% = TRUE
ENDPROC
:
:
DEF PROCfailed(message$)
PRINT message$'"It looks like you have missed the TV programme. That's it, I'm afraid"
finished% = TRUE
ENDPROC
:
:
DEF PROCquit
PRINT"Goodbye"
finished% = TRUE
ENDPROC
:
:
DEF PROChelp
PRINT'"This is a little adventure-type game. The object is to watch"
PRINT"your favourite TV programme."
PRINT"The game is played by typing in commands. These are limited to"
PRINT"two words at most, a verb which is sometimes followed by a noun."
PRINT"Type 'quit' to leave the game"'
ENDPROC
:
REM --------------------------------------------------------
:
DEF PROCinit
maxlocs% = 25     :REM Maximum number of locations allowed in game
maxobjs% = 25     :REM Maximum number of objects allowed in game
maxsize% = 10     :REM Size of largest object that can be carried
maxload% = 10     :REM Maximum total load that can be carried
:
DIM location$(maxlocs%), description$(maxlocs%), exits%(maxlocs%, 4)
DIM objname$(maxobjs%), objprep$(maxobjs%), objsize%(maxobjs%), objloc%(maxobjs%)
DIM verb$(50), noun$(50)
DIM xlate$(255), directions$(4)
:
FOR N% = 0 TO 255: xlate$(N%) = CHR$N%: NEXT
X% = ASC"a"-ASC"A"
FOR N% = ASC"A" TO ASC"Z": xlate$(N%) = CHR$(N%+X%): NEXT
:
directions$() = "", "north", "east", "south", "west"
:
finished% = FALSE
time% = 0
location% = 1
encumbrance% = 0
:
door_locked% = TRUE        :REM Front door is locked
cupboard_closed% = TRUE    :REM Kitchen cupboard is closed
no_plug% = TRUE            :REM There is no plug on the TV
tv_off% = TRUE             :REM The TV is switched off
tv_stolen% = FALSE         :REM The TV has not been stolen yet
timelimit% = 40            :REM TV programme is missed if time limit is exceeded
:
REM Directions
north% = 1: east% = 2: south% = 3: west% = 4
:
REM Location numbers
road1% = 1: road2% = 2: garden1% = 3: garden2% = 4: doorstep% = 5
hall% = 6: kitchen% = 7: lounge% = 8: shop% = 9
player% = 999
:
REM Object numbers
door% = 1: key% = 2: money% = 3: box% = 4
plug% = 5: screwdriver% = 6: cupboard% = 7: settee% = 8: table% = 9
tv% = 10
:
REM Read the vocabulary
:
verbcount% = 0
REPEAT
  verbcount%+=1
  READ verb$(verbcount%)
UNTIL verb$(verbcount%)="***"
nouncount% = 0
REPEAT
  nouncount%+=1
  READ noun$(nouncount%)
UNTIL noun$(nouncount%)="***"
:
REM Read the map data
:
READ thisloc%
WHILE thisloc%<>0
  READ description$(thisloc%)
  FOR N% = 1 TO 4
    READ direction%, destination%
    IF direction%<>0 THEN exits%(thisloc%, direction%) = destination%
  NEXT
  READ thisloc%
ENDWHILE
:
REM Read object data and place objects at locations
:
objcount% = 0
READ object%
WHILE object%<>0
  READ objname$(object%), objprep$(object%), objsize%(object%), objloc%(object%)
  objcount%+=1
  READ object%
ENDWHILE
:
ENDPROC
:
REM ==============================================================
:
REM Vocabulary - Verbs
:
DATA north, n, east, e, south, s, west, w, open, close, lock, unlock
DATA look, examine, watch, kick, switch, turn, screw, attach
DATA get, take, drop, buy, inv, invent, save, restore, quit, help
DATA ***
:
REM Vocabulary - Nouns
:
DATA door, key, money, box, plug, screwdriver, cupboard
DATA settee, table, tv
DATA ***
:
REM Locations and connections to other locations
:
DATA road1%, in the road outside your house
DATA east%, road2%, south%, garden1%, 0, 0, 0, 0
DATA road2%, in the road outside the electrical shop
DATA west%, road1%, south%, shop%, 0, 0, 0, 0
DATA garden1%,in your front garden by the front door 
DATA north%, road1%, south%, doorstep%, west%, garden2%, 0, 0
DATA garden2%, in the garden at the side of your house
DATA east%, garden1%, 0, 0, 0, 0, 0, 0
DATA doorstep%, standing on the front doorstep
DATA north%, garden1%, south%, hall%, 0, 0, 0, 0
DATA hall%, in the hall of your house
DATA north%, doorstep%, south%, kitchen%, east%, lounge%, 0, 0
DATA kitchen%, in the kitchen
DATA north%, hall%, 0, 0, 0, 0, 0, 0
DATA lounge%, in the lounge
DATA west%, hall%, 0, 0, 0, 0, 0, 0
DATA shop%, in the electrical shop
DATA north%, road2%, 0, 0, 0, 0, 0, 0
DATA 0
:
REM Objects and their starting locations
:
REM Negative location numbers indicate that the object is placed
REM in or on another object. 'xx' says that nothing can be put
REM in or on that object, 'in' says it can hold things and 'on'
REM says that things can be put on it
:
DATA key%, key, xx, 1, garden2%
DATA door%, door, xx, 100, doorstep% 
DATA settee%, settee, on, 100, lounge%
DATA table%, table, on, 100, lounge%
DATA money%, money, xx, 1, -table%
DATA tv%, tv, xx, 100, lounge%
DATA cupboard%, cupboard, in, 100, kitchen%
DATA screwdriver%, screwdriver, xx, 1, -cupboard%
DATA box%, box, in, 100, shop%
DATA plug%, plug, xx, 1, -box%
DATA 0
