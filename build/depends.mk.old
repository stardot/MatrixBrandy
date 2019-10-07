# Build VARIABLES.C
VARIABLES_C = $(SRCDIR)/common.h $(SRCDIR)/target.h $(SRCDIR)/basicdefs.h \
	$(SRCDIR)/variables.h $(SRCDIR)/evaluate.h $(SRCDIR)/tokens.h \
	$(SRCDIR)/stack.h $(SRCDIR)/heap.h $(SRCDIR)/errors.h \
	$(SRCDIR)/miscprocs.h $(SRCDIR)/screen.h $(SRCDIR)/lvalue.h

$(SRCDIR)/variables.o: $(VARIABLES_C)

# Build TOKENS.C
TOKENS_C = $(SRCDIR)/common.h $(SRCDIR)/target.h $(SRCDIR)/basicdefs.h \
	$(SRCDIR)/tokens.h $(SRCDIR)/miscprocs.h $(SRCDIR)/convert.h \
	$(SRCDIR)/errors.h

$(SRCDIR)/tokens.o: $(TOKENS_C)

# Build GRAPHSDL.C
GSDL_C = $(SRCDIR)/common.h $(SRCDIR)/target.h $(SRCDIR)/basicdefs.h \
	$(SRCDIR)/errors.h $(SRCDIR)/scrcommon.h $(SRCDIR)/screen.h \
	$(SRCDIR)/mos.h $(SRCDIR)/graphsdl.h $(SRCDIR)/textfonts.h

$(SRCDIR)/graphsdl.o: $(GSDL_C)

# Build STRINGS.C
STRINGS_C = $(SRCDIR)/common.h $(SRCDIR)/target.h $(SRCDIR)/basicdefs.h \
	$(SRCDIR)/strings.h $(SRCDIR)/heap.h $(SRCDIR)/errors.h

$(SRCDIR)/strings.o: $(STRINGS_C)

# Build STATEMENT.C
STATEMENT_C = $(SRCDIR)/common.h $(SRCDIR)/target.h $(SRCDIR)/basicdefs.h \
	$(SRCDIR)/tokens.h $(SRCDIR)/commands.h $(SRCDIR)/stack.h \
	$(SRCDIR)/heap.h $(SRCDIR)/errors.h $(SRCDIR)/editor.h \
	$(SRCDIR)/miscprocs.h $(SRCDIR)/variables.h $(SRCDIR)/evaluate.h \
	$(SRCDIR)/screen.h $(SRCDIR)/fileio.h $(SRCDIR)/strings.h \
	$(SRCDIR)/iostate.h $(SRCDIR)/mainstate.h $(SRCDIR)/assign.h \
	$(SRCDIR)/statement.h

$(SRCDIR)/statement.o: $(STATEMENT_C)

# Build STACK.C
STACK_C = $(SRCDIR)/common.h $(SRCDIR)/target.h $(SRCDIR)/basicdefs.h \
	$(SRCDIR)/stack.h $(SRCDIR)/miscprocs.h $(SRCDIR)/strings.h \
	$(SRCDIR)/tokens.h $(SRCDIR)/errors.h

$(SRCDIR)/stack.o: $(STACK_C)

# Build MISCPROCS.C
MISCPROCS_C = $(SRCDIR)/common.h $(SRCDIR)/target.h $(SRCDIR)/basicdefs.h \
	$(SRCDIR)/tokens.h $(SRCDIR)/errors.h $(SRCDIR)/keyboard.h \
	$(SRCDIR)/screen.h $(SRCDIR)/miscprocs.h

$(SRCDIR)/miscprocs.o: $(MISCPROCS_C)

# Build MAINSTATE.C
MAINSTATE_C = $(SRCDIR)/common.h $(SRCDIR)/target.h $(SRCDIR)/basicdefs.h \
	$(SRCDIR)/tokens.h $(SRCDIR)/variables.h $(SRCDIR)/stack.h \
	$(SRCDIR)/heap.h $(SRCDIR)/strings.h $(SRCDIR)/errors.h \
	$(SRCDIR)/statement.h $(SRCDIR)/evaluate.h $(SRCDIR)/convert.h \
	$(SRCDIR)/miscprocs.h $(SRCDIR)/editor.h $(SRCDIR)/mos.h \
	$(SRCDIR)/screen.h $(SRCDIR)/lvalue.h $(SRCDIR)/fileio.h \
	$(SRCDIR)/mainstate.h

$(SRCDIR)/mainstate.o: $(MAINSTATE_C)

# Build LVALUE.C
LVALUE_C = $(SRCDIR)/common.h $(SRCDIR)/target.h $(SRCDIR)/basicdefs.h \
	$(SRCDIR)/tokens.h $(SRCDIR)/evaluate.h $(SRCDIR)/stack.h \
	$(SRCDIR)/errors.h $(SRCDIR)/variables.h $(SRCDIR)/miscprocs.h \
	$(SRCDIR)/lvalue.h

$(SRCDIR)/lvalue.o: $(LVALUE_C)

# Build KEYBOARD.C
KEYBOARD_C = $(SRCDIR)/common.h $(SRCDIR)/target.h $(SRCDIR)/basicdefs.h \
	$(SRCDIR)/errors.h $(SRCDIR)/keyboard.h $(SRCDIR)/screen.h \
	$(SRCDIR)/inkey.h

$(SRCDIR)/keyboard.o: $(KEYBOARD_C)

# Build IOSTATE.C
IOSTATE_C = $(SRCDIR)/common.h $(SRCDIR)/target.h $(SRCDIR)/basicdefs.h \
	$(SRCDIR)/tokens.h $(SRCDIR)/stack.h $(SRCDIR)/strings.h \
	$(SRCDIR)/errors.h $(SRCDIR)/miscprocs.h $(SRCDIR)/evaluate.h \
	$(SRCDIR)/convert.h $(SRCDIR)/mos.h $(SRCDIR)/fileio.h \
	$(SRCDIR)/screen.h $(SRCDIR)/lvalue.h $(SRCDIR)/statement.h \
	$(SRCDIR)/iostate.h

$(SRCDIR)/iostate.o: $(IOSTATE_C)

# Build HEAP.C
HEAP_C = $(SRCDIR)/common.h $(SRCDIR)/target.h $(SRCDIR)/basicdefs.h \
	$(SRCDIR)/heap.h $(SRCDIR)/target.h $(SRCDIR)/errors.h \
	$(SRCDIR)/miscprocs.h

$(SRCDIR)/heap.o: $(HEAP_C)

# Build FUNCTIONS.C
FUNCTIONS_C = $(SRCDIR)/common.h $(SRCDIR)/target.h $(SRCDIR)/basicdefs.h \
	$(SRCDIR)/tokens.h $(SRCDIR)/variables.h $(SRCDIR)/strings.h \
	$(SRCDIR)/convert.h $(SRCDIR)/stack.h $(SRCDIR)/errors.h \
	$(SRCDIR)/evaluate.h $(SRCDIR)/keyboard.h $(SRCDIR)/screen.h \
	$(SRCDIR)/mos.h $(SRCDIR)/miscprocs.h $(SRCDIR)/fileio.h \
	$(SRCDIR)/functions.h

$(SRCDIR)/functions.o: $(FUNCTIONS_C)

# Build FILEIO.C
FILEIO_C = $(SRCDIR)/common.h $(SRCDIR)/target.h $(SRCDIR)/basicdefs.h \
	$(SRCDIR)/errors.h $(SRCDIR)/fileio.h $(SRCDIR)/strings.h \
	$(SRCDIR)/net.h

$(SRCDIR)/fileio.o: $(FILEIO_C)

# Build NET.C
NET_C = $(SRCDIR)/net.h $(SRCDIR)/errors.h

$(SRCDIR)/net.o: $(NET_C)

# Build EVALUATE.C
EVALUATE_C = $(SRCDIR)/common.h $(SRCDIR)/target.h $(SRCDIR)/basicdefs.h \
	$(SRCDIR)/tokens.h $(SRCDIR)/variables.h $(SRCDIR)/lvalue.h \
	$(SRCDIR)/strings.h $(SRCDIR)/stack.h $(SRCDIR)/errors.h \
	$(SRCDIR)/evaluate.h $(SRCDIR)/statement.h $(SRCDIR)/miscprocs.h \
	$(SRCDIR)/functions.h

$(SRCDIR)/evaluate.o: $(EVALUATE_C)

# Build ERRORS.C
ERRORS_C = $(SRCDIR)/common.h $(SRCDIR)/target.h $(SRCDIR)/basicdefs.h \
	$(SRCDIR)/errors.h $(SRCDIR)/stack.h $(SRCDIR)/fileio.h \
	$(SRCDIR)/tokens.h $(SRCDIR)/screen.h $(SRCDIR)/miscprocs.h \
	$(SRCDIR)/keyboard.h $(SRCDIR)/graphsdl.h

$(SRCDIR)/errors.o: $(ERRORS_C)

# Build MOS.C
MOS_C = $(SRCDIR)/common.h $(SRCDIR)/target.h $(SRCDIR)/errors.h \
	$(SRCDIR)/basicdefs.h $(SRCDIR)/mos.h $(SRCDIR)/graphsdl.h \
	$(SRCDIR)/screen.h $(SRCDIR)/keyboard.h $(SRCDIR)/mos_sys.h

$(SRCDIR)/mos.o: $(MOS_C)

# Build MOS_SYS.C
MOS_SYS_C = $(SRCDIR)/common.h $(SRCDIR)/target.h $(SRCDIR)/errors.h \
	$(SRCDIR)/basicdefs.h $(SRCDIR)/mos.h \
	$(SRCDIR)/screen.h $(SRCDIR)/keyboard.h $(SRCDIR)/mos_sys.h

$(SRCDIR)/mos_sys.o: $(MOS_SYS_C)

# Build EDITOR.C
EDITOR_C = $(SRCDIR)/common.h $(SRCDIR)/target.h $(SRCDIR)/basicdefs.h \
	$(SRCDIR)/errors.h $(SRCDIR)/variables.h $(SRCDIR)/heap.h \
	$(SRCDIR)/tokens.h $(SRCDIR)/strings.h $(SRCDIR)/miscprocs.h \
	$(SRCDIR)/stack.h $(SRCDIR)/fileio.h

$(SRCDIR)/editor.o: $(EDITOR_C)

# Build CONVERT.C
CONVERT_C = $(SRCDIR)/common.h $(SRCDIR)/target.h $(SRCDIR)/basicdefs.h \
	$(SRCDIR)/convert.h $(SRCDIR)/errors.h $(SRCDIR)/miscprocs.h

$(SRCDIR)/convert.o: $(CONVERT_C)

# Build COMMANDS.C
COMMANDS_C = $(SRCDIR)/common.h $(SRCDIR)/target.h $(SRCDIR)/basicdefs.h \
	$(SRCDIR)/miscprocs.h $(SRCDIR)/tokens.h $(SRCDIR)/statement.h \
	$(SRCDIR)/variables.h $(SRCDIR)/editor.h $(SRCDIR)/errors.h \
	$(SRCDIR)/heap.h $(SRCDIR)/stack.h $(SRCDIR)/strings.h \
	$(SRCDIR)/evaluate.h $(SRCDIR)/screen.h $(SRCDIR)/keyboard.h

$(SRCDIR)/commands.o: $(COMMANDS_C)

# Build BRANDY.C
BRANDY_C = $(SRCDIR)/common.h $(SRCDIR)/target.h $(SRCDIR)/basicdefs.h \
	$(SRCDIR)/tokens.h $(SRCDIR)/errors.h $(SRCDIR)/heap.h \
	$(SRCDIR)/editor.h $(SRCDIR)/commands.h $(SRCDIR)/statement.h \
	$(SRCDIR)/fileio.h $(SRCDIR)/mos.h $(SRCDIR)/keyboard.h \
	$(SRCDIR)/screen.h $(SRCDIR)/miscprocs.h $(SRCDIR)/net.h

$(SRCDIR)/brandy.o: $(BRANDY_C)

# Build ASSIGN.C
ASSIGN_C = $(SRCDIR)/common.h $(SRCDIR)/target.h $(SRCDIR)/basicdefs.h \
	$(SRCDIR)/target.h $(SRCDIR)/tokens.h $(SRCDIR)/heap.h \
	$(SRCDIR)/stack.h $(SRCDIR)/strings.h $(SRCDIR)/variables.h \
	$(SRCDIR)/errors.h $(SRCDIR)/miscprocs.h $(SRCDIR)/editor.h \
	$(SRCDIR)/evaluate.h $(SRCDIR)/lvalue.h $(SRCDIR)/statement.h \
	$(SRCDIR)/assign.h $(SRCDIR)/fileio.h $(SRCDIR)/mos.h

$(SRCDIR)/assign.o: $(ASSIGN_C)

# Build TEXTONLY.C
TEXTONLY_C = $(SRCDIR)/common.h $(SRCDIR)/target.h $(SRCDIR)/basicdefs.h \
	$(SRCDIR)/errors.h $(SRCDIR)/scrcommon.h $(SRCDIR)/screen.h

$(SRCDIR)/textonly.o: $(TEXTONLY_C)

# Build SIMPLETEXT.C
$(SRCDIR)/simpletext.o: $(TEXTONLY_C)
