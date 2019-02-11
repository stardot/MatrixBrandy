# Makefile for brandy under NetBSD and Linux

CC = gcc
LD = gcc
ADDFLAGS = ${BRANDY_BUILD_FLAGS}

include build/git.mk

#CFLAGS = -g -DDEBUG -I/usr/include/SDL -DUSE_SDL -DDEFAULT_IGNORE -Wall $(GITFLAGS) $(ADDFLAGS)
#CFLAGS = -g -I/usr/include/SDL -DUSE_SDL -DDEFAULT_IGNORE -Wall $(GITFLAGS) $(ADDFLAGS)
CFLAGS = -O2 -I/usr/include/SDL -DUSE_SDL -DDEFAULT_IGNORE -Wall $(GITFLAGS) $(ADDFLAGS)

LDFLAGS +=

LIBS = -lX11 -lm -lSDL

SRCDIR = src

OBJ = $(SRCDIR)/variables.o $(SRCDIR)/tokens.o $(SRCDIR)/graphsdl.o \
	$(SRCDIR)/strings.o $(SRCDIR)/statement.o $(SRCDIR)/stack.o \
	$(SRCDIR)/miscprocs.o $(SRCDIR)/mainstate.o $(SRCDIR)/lvalue.o \
	$(SRCDIR)/keyboard.o $(SRCDIR)/iostate.o $(SRCDIR)/heap.o \
	$(SRCDIR)/functions.o $(SRCDIR)/fileio.o $(SRCDIR)/evaluate.o \
	$(SRCDIR)/errors.o $(SRCDIR)/mos.o $(SRCDIR)/editor.o \
	$(SRCDIR)/convert.o $(SRCDIR)/commands.o $(SRCDIR)/brandy.o \
	$(SRCDIR)/assign.o $(SRCDIR)/net.o $(SRCDIR)/mos_sys.o

SRC = $(SRCDIR)/variables.c $(SRCDIR)/tokens.c $(SRCDIR)/graphsdl.c \
	$(SRCDIR)/strings.c $(SRCDIR)/statement.c $(SRCDIR)/stack.c \
	$(SRCDIR)/miscprocs.c $(SRCDIR)/mainstate.c $(SRCDIR)/lvalue.c \
	$(SRCDIR)/keyboard.c $(SRCDIR)/iostate.c $(SRCDIR)/heap.c \
	$(SRCDIR)/functions.c $(SRCDIR)/fileio.c $(SRCDIR)/evaluate.c \
	$(SRCDIR)/errors.c $(SRCDIR)/mos.c $(SRCDIR)/editor.c \
	$(SRCDIR)/convert.c $(SRCDIR)/commands.c $(SRCDIR)/brandy.c \
	$(SRCDIR)/assign.c $(SRCDIR)/net.c $(SRCDIR)/mos_sys.c

brandy:	$(OBJ)
	$(LD) $(LDFLAGS) -o brandy $(OBJ) $(LIBS)

include build/depends.mk

.c.o:
	$(CC) $(CFLAGS) $< -c -o $@

recompile:
	$(CC) $(CFLAGS) $(SRC) $(LIBS) -o brandy

nodebug:
	$(CC) $(CFLAGS2) $(SRC) $(LIBS) -o brandy
	strip brandy

check:
	$(CC) $(CFLAGS) -Wall -O2 $(SRC) $(LIBS) -o brandy

clean:
	rm -f $(SRCDIR)/*.o brandy

all:	brandy
