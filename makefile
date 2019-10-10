# Makefile for brandy under NetBSD and Linux

CC = gcc
LD = gcc
STRIP = strip
ADDFLAGS = ${BRANDY_BUILD_FLAGS}

include build/git.mk

#CFLAGS = -g -DDEBUG -I/usr/include/SDL -DUSE_SDL -DDEFAULT_IGNORE -Wall $(GITFLAGS) $(ADDFLAGS)
#CFLAGS = -g -I/usr/include/SDL -DUSE_SDL -DDEFAULT_IGNORE -Wall $(GITFLAGS) $(ADDFLAGS)
CFLAGS = -O3 -I/usr/include/SDL -DUSE_SDL -DDEFAULT_IGNORE -Wall $(GITFLAGS) $(ADDFLAGS)

LDFLAGS +=

LIBS = -lX11 -lm -lSDL

SRCDIR = src

OBJ = $(SRCDIR)/graphsdl.o \
	$(SRCDIR)/evaluate.o \
	$(SRCDIR)/assign.o \
	$(SRCDIR)/mainstate.o \
	$(SRCDIR)/tokens.o \
	$(SRCDIR)/commands.o \
	$(SRCDIR)/mos.o \
	$(SRCDIR)/functions.o \
	$(SRCDIR)/iostate.o \
	$(SRCDIR)/keyboard.o \
	$(SRCDIR)/stack.o \
	$(SRCDIR)/errors.o \
	$(SRCDIR)/variables.o \
	$(SRCDIR)/editor.o \
	$(SRCDIR)/statement.o \
	$(SRCDIR)/lvalue.o \
	$(SRCDIR)/mos_sys.o \
	$(SRCDIR)/soundsdl.o \
	$(SRCDIR)/miscprocs.o \
	$(SRCDIR)/strings.o \
	$(SRCDIR)/convert.o \
	$(SRCDIR)/brandy.o \
	$(SRCDIR)/fileio.o \
	$(SRCDIR)/heap.o \
	$(SRCDIR)/net.o

SRC = $(SRCDIR)/graphsdl.c \
	$(SRCDIR)/evaluate.c \
	$(SRCDIR)/assign.c \
	$(SRCDIR)/mainstate.c \
	$(SRCDIR)/tokens.c \
	$(SRCDIR)/commands.c \
	$(SRCDIR)/mos.c \
	$(SRCDIR)/functions.c \
	$(SRCDIR)/iostate.c \
	$(SRCDIR)/keyboard.c \
	$(SRCDIR)/stack.c \
	$(SRCDIR)/errors.c \
	$(SRCDIR)/variables.c \
	$(SRCDIR)/editor.c \
	$(SRCDIR)/statement.c \
	$(SRCDIR)/lvalue.c \
	$(SRCDIR)/mos_sys.c \
	$(SRCDIR)/soundsdl.c \
	$(SRCDIR)/miscprocs.c \
	$(SRCDIR)/strings.c \
	$(SRCDIR)/convert.c \
	$(SRCDIR)/brandy.c \
	$(SRCDIR)/fileio.c \
	$(SRCDIR)/heap.c \
	$(SRCDIR)/net.c

brandy:	$(OBJ)
	$(LD) $(LDFLAGS) -o brandy $(OBJ) $(LIBS)

include build/depends.mk

.c.o:
	$(CC) $(CFLAGS) $< -c -o $@

recompile:
	$(CC) $(CFLAGS) $(SRC) $(LIBS) -o brandy

nodebug:
	$(CC) $(CFLAGS2) $(SRC) $(LIBS) -o brandy
	$(STRIP) brandy

check:
	$(CC) $(CFLAGS) -Wall -O2 $(SRC) $(LIBS) -o brandy

clean:
	rm -f $(SRCDIR)/*.o brandy

cleanall:
	rm -f $(SRCDIR)/*.o brandy sbrandy tbrandy

text:
	$(MAKE) -f makefile.text

textclean:
	rm -f $(SRCDIR)/*.o sbrandy tbrandy

all:	brandy

