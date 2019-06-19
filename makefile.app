# Makefile for brandy under NetBSD and Linux

# This makefile builds a static library, brandyapp.a.

# Use the following to generate your app.o:
# ld -r -b binary app -o app.o
# where 'app' is the name of the BASIC program.
# Since 'ld' uses the source name (app) to build its symbol table,
# the code has to assume the name "app" - so copy it first!

# Then, you can build your standalone app with:
# gcc -o yourapp app.o /path/to/brandyapp.a -lX11 -lm -lSDL

CC = gcc
LD = gcc
AR = ar
ADDFLAGS = ${BRANDY_BUILD_FLAGS}

include build/git.mk

#CFLAGS = -g -DDEBUG -I/usr/include/SDL -DUSE_SDL -DDEFAULT_IGNORE -DBRANDYAPP -Wall $(GITFLAGS) $(ADDFLAGS)
#CFLAGS = -g -I/usr/include/SDL -DUSE_SDL -DDEFAULT_IGNORE -DBRANDYAPP -Wall $(GITFLAGS) $(ADDFLAGS)
CFLAGS = -O3 -I/usr/include/SDL -DUSE_SDL -DDEFAULT_IGNORE -DBRANDYAPP -Wall $(GITFLAGS) $(ADDFLAGS)

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

brandyapp.a:	$(OBJ)
	$(AR) rcs brandyapp.a $(OBJ)

include build/depends.mk

.c.o:
	$(CC) $(CFLAGS) $< -c -o $@

clean:
	rm -f $(SRCDIR)/*.o brandyapp.a

all:	brandyapp.a
