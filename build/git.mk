gitcommit=\""$(shell git log --abbrev-commit -1 2>/dev/null| head -1 |cut -d ' ' -f 2)"\"
ifeq ($(gitcommit),\"""\")
  GITFLAGS =
else
  gitbranch=\""$(shell git status | head -1 | sed 's/\# //' | cut -d ' ' -f 3 )"\"
  gitdate=\""$(shell git log --abbrev-commit -1 | grep 'Date:' | cut -d ' ' -f 4-9)"\"
  GITFLAGS = -DBRANDY_GITCOMMIT=$(gitcommit) -DBRANDY_GITBRANCH=$(gitbranch) -DBRANDY_GITDATE=$(gitdate)
endif
