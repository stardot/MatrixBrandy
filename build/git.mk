gitcommit=\""$(shell git log -1 --format=%h)"\"
ifeq ($(gitcommit),\"""\")
  GITFLAGS = -DBRANDY_RELEASE
else
  gitbranch=\""$(shell git rev-parse --abbrev-ref HEAD)"\"
  gitdate=\""$(shell git log -1 --format=%cd)"\"
  GITFLAGS = -DBRANDY_GITCOMMIT=$(gitcommit) -DBRANDY_GITBRANCH=$(gitbranch) -DBRANDY_GITDATE=$(gitdate)
  ifeq ($(gitbranch),\""master"\")
    GITFLAGS += -DBRANDY_RELEASE
  endif
endif
