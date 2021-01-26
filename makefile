# Makefile for brandy under NetBSD and Linux

CC = gcc
LD = gcc
STRIP = strip
ADDFLAGS = ${BRANDY_BUILD_FLAGS}

include build/git.mk

#CFLAGS = -g -DDEBUG $(shell sdl-config --cflags)  -I/usr/local/include/SDL -DUSE_SDL -DDEFAULT_IGNORE -Wall $(GITFLAGS) $(ADDFLAGS)
#CFLAGS = -g $(shell sdl-config --cflags)  -I/usr/local/include/SDL -DUSE_SDL -DDEFAULT_IGNORE -Wall $(GITFLAGS) $(ADDFLAGS)
CFLAGS = -O3 $(shell sdl-config --cflags) -DUSE_SDL -DDEFAULT_IGNORE -Wall $(GITFLAGS) $(ADDFLAGS)

LDFLAGS +=

LIBS = -lm $(shell sdl-config --libs) -ldl -lrt -lX11

SRCDIR = src

OBJ = \
	$(SRCDIR)/evaluate.o \
	$(SRCDIR)/graphsdl.o \
	$(SRCDIR)/assign.o \
	$(SRCDIR)/mainstate.o \
	$(SRCDIR)/tokens.o \
	$(SRCDIR)/mos.o \
	$(SRCDIR)/commands.o \
	$(SRCDIR)/functions.o \
	$(SRCDIR)/iostate.o \
	$(SRCDIR)/variables.o \
	$(SRCDIR)/fileio.o \
	$(SRCDIR)/soundsdl.o \
	$(SRCDIR)/keyboard.o \
	$(SRCDIR)/miscprocs.o \
	$(SRCDIR)/editor.o \
	$(SRCDIR)/stack.o \
	$(SRCDIR)/mos_sys.o \
	$(SRCDIR)/strings.o \
	$(SRCDIR)/lvalue.o \
	$(SRCDIR)/errors.o \
	$(SRCDIR)/convert.o \
	$(SRCDIR)/brandy.o \
	$(SRCDIR)/statement.o \
	$(SRCDIR)/net.o \
	$(SRCDIR)/heap.o

SRC = \
	$(SRCDIR)/evaluate.c \
	$(SRCDIR)/graphsdl.c \
	$(SRCDIR)/assign.c \
	$(SRCDIR)/mainstate.c \
	$(SRCDIR)/tokens.c \
	$(SRCDIR)/mos.c \
	$(SRCDIR)/commands.c \
	$(SRCDIR)/functions.c \
	$(SRCDIR)/iostate.c \
	$(SRCDIR)/variables.c \
	$(SRCDIR)/fileio.c \
	$(SRCDIR)/soundsdl.c \
	$(SRCDIR)/keyboard.c \
	$(SRCDIR)/miscprocs.c \
	$(SRCDIR)/editor.c \
	$(SRCDIR)/stack.c \
	$(SRCDIR)/mos_sys.c \
	$(SRCDIR)/strings.c \
	$(SRCDIR)/lvalue.c \
	$(SRCDIR)/errors.c \
	$(SRCDIR)/convert.c \
	$(SRCDIR)/brandy.c \
	$(SRCDIR)/statement.c \
	$(SRCDIR)/net.c \
	$(SRCDIR)/heap.c

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

