DEPCOMMON = $(SRCDIR)/common.h \
	$(SRCDIR)/target.h \
	$(SRCDIR)/basicdefs.h \
	$(SRCDIR)/errors.h

# Build VARIABLES.C
VARIABLES_C = $(DEPCOMMON) \
	$(SRCDIR)/variables.h \
	$(SRCDIR)/evaluate.h \
	$(SRCDIR)/tokens.h \
	$(SRCDIR)/stack.h \
	$(SRCDIR)/heap.h \
	$(SRCDIR)/miscprocs.h \
	$(SRCDIR)/screen.h \
	$(SRCDIR)/lvalue.h

$(SRCDIR)/variables.o: $(VARIABLES_C)

# Build TOKENS.C
TOKENS_C = $(DEPCOMMON) \
	$(SRCDIR)/tokens.h \
	$(SRCDIR)/miscprocs.h \
	$(SRCDIR)/convert.h

$(SRCDIR)/tokens.o: $(TOKENS_C)

# Build GRAPHSDL.C
GSDL_C = $(DEPCOMMON) \
	$(SRCDIR)/scrcommon.h \
	$(SRCDIR)/screen.h \
	$(SRCDIR)/mos.h \
	$(SRCDIR)/graphsdl.h \
	$(SRCDIR)/textfonts.h \
	$(SRCDIR)/keyboard.h

$(SRCDIR)/graphsdl.o: $(GSDL_C)

# Build STRINGS.C
STRINGS_C = $(DEPCOMMON) \
	$(SRCDIR)/strings.h \
	$(SRCDIR)/heap.h

$(SRCDIR)/strings.o: $(STRINGS_C)

# Build STATEMENT.C
STATEMENT_C = $(DEPCOMMON) \
	$(SRCDIR)/tokens.h \
	$(SRCDIR)/commands.h \
	$(SRCDIR)/stack.h \
	$(SRCDIR)/heap.h \
	$(SRCDIR)/editor.h \
	$(SRCDIR)/miscprocs.h \
	$(SRCDIR)/variables.h \
	$(SRCDIR)/evaluate.h \
	$(SRCDIR)/screen.h \
	$(SRCDIR)/fileio.h \
	$(SRCDIR)/strings.h \
	$(SRCDIR)/iostate.h \
	$(SRCDIR)/mainstate.h \
	$(SRCDIR)/assign.h \
	$(SRCDIR)/statement.h \
	$(SRCDIR)/keyboard.h

$(SRCDIR)/statement.o: $(STATEMENT_C)

# Build STACK.C
STACK_C = $(DEPCOMMON) \
	$(SRCDIR)/stack.h \
	$(SRCDIR)/miscprocs.h \
	$(SRCDIR)/strings.h \
	$(SRCDIR)/tokens.h

$(SRCDIR)/stack.o: $(STACK_C)

# Build MISCPROCS.C
MISCPROCS_C = $(DEPCOMMON) \
	$(SRCDIR)/tokens.h \
	$(SRCDIR)/keyboard.h \
	$(SRCDIR)/screen.h \
	$(SRCDIR)/miscprocs.h

$(SRCDIR)/miscprocs.o: $(MISCPROCS_C)

# Build MAINSTATE.C
MAINSTATE_C = $(DEPCOMMON) \
	$(SRCDIR)/tokens.h \
	$(SRCDIR)/variables.h \
	$(SRCDIR)/stack.h \
	$(SRCDIR)/heap.h \
	$(SRCDIR)/strings.h \
	$(SRCDIR)/statement.h \
	$(SRCDIR)/evaluate.h \
	$(SRCDIR)/convert.h \
	$(SRCDIR)/miscprocs.h \
	$(SRCDIR)/editor.h \
	$(SRCDIR)/mos.h \
	$(SRCDIR)/screen.h \
	$(SRCDIR)/lvalue.h \
	$(SRCDIR)/fileio.h \
	$(SRCDIR)/mainstate.h

$(SRCDIR)/mainstate.o: $(MAINSTATE_C)

# Build LVALUE.C
LVALUE_C = $(DEPCOMMON) \
	$(SRCDIR)/tokens.h \
	$(SRCDIR)/evaluate.h \
	$(SRCDIR)/stack.h \
	$(SRCDIR)/variables.h \
	$(SRCDIR)/miscprocs.h \
	$(SRCDIR)/lvalue.h

$(SRCDIR)/lvalue.o: $(LVALUE_C)

# Build KEYBOARD.C
KEYBOARD_C = $(DEPCOMMON) \
	$(SRCDIR)/screen.h \
	$(SRCDIR)/keyboard.h \
	$(SRCDIR)/inkey.h \
	$(SRCDIR)/mos.h

$(SRCDIR)/keyboard.o: $(KEYBOARD_C)

# Build IOSTATE.C
IOSTATE_C = $(DEPCOMMON) \
	$(SRCDIR)/tokens.h \
	$(SRCDIR)/stack.h \
	$(SRCDIR)/strings.h \
	$(SRCDIR)/miscprocs.h \
	$(SRCDIR)/evaluate.h \
	$(SRCDIR)/convert.h \
	$(SRCDIR)/mos.h \
	$(SRCDIR)/fileio.h \
	$(SRCDIR)/screen.h \
	$(SRCDIR)/lvalue.h \
	$(SRCDIR)/statement.h \
	$(SRCDIR)/iostate.h

$(SRCDIR)/iostate.o: $(IOSTATE_C)

# Build HEAP.C
HEAP_C = $(DEPCOMMON) \
	$(SRCDIR)/heap.h \
	$(SRCDIR)/miscprocs.h

$(SRCDIR)/heap.o: $(HEAP_C)

# Build FUNCTIONS.C
FUNCTIONS_C = $(DEPCOMMON) \
	$(SRCDIR)/tokens.h \
	$(SRCDIR)/variables.h \
	$(SRCDIR)/strings.h \
	$(SRCDIR)/convert.h \
	$(SRCDIR)/stack.h \
	$(SRCDIR)/evaluate.h \
	$(SRCDIR)/keyboard.h \
	$(SRCDIR)/screen.h \
	$(SRCDIR)/mos.h \
	$(SRCDIR)/miscprocs.h \
	$(SRCDIR)/fileio.h \
	$(SRCDIR)/functions.h

$(SRCDIR)/functions.o: $(FUNCTIONS_C)

# Build FILEIO.C
FILEIO_C = $(DEPCOMMON) \
	$(SRCDIR)/miscprocs.h \
	$(SRCDIR)/fileio.h \
	$(SRCDIR)/strings.h \
	$(SRCDIR)/net.h \
	$(SRCDIR)/keyboard.h

$(SRCDIR)/fileio.o: $(FILEIO_C)

# Build NET.C
NET_C = $(DEPCOMMON) $(SRCDIR)/net.h

$(SRCDIR)/net.o: $(NET_C)

# Build EVALUATE.C
EVALUATE_C = $(DEPCOMMON) \
	$(SRCDIR)/tokens.h \
	$(SRCDIR)/variables.h \
	$(SRCDIR)/lvalue.h \
	$(SRCDIR)/strings.h \
	$(SRCDIR)/stack.h \
	$(SRCDIR)/evaluate.h \
	$(SRCDIR)/statement.h \
	$(SRCDIR)/miscprocs.h \
	$(SRCDIR)/functions.h

$(SRCDIR)/evaluate.o: $(EVALUATE_C)

# Build ERRORS.C
ERRORS_C = $(DEPCOMMON) \
	$(SRCDIR)/stack.h \
	$(SRCDIR)/fileio.h \
	$(SRCDIR)/tokens.h \
	$(SRCDIR)/screen.h \
	$(SRCDIR)/miscprocs.h \
	$(SRCDIR)/keyboard.h \
	$(SRCDIR)/graphsdl.h

$(SRCDIR)/errors.o: $(ERRORS_C)

# Build MOS.C
MOS_C = $(DEPCOMMON) \
	$(SRCDIR)/mos.h \
	$(SRCDIR)/mos_sys.h \
	$(SRCDIR)/screen.h \
	$(SRCDIR)/keyboard.h \
	$(SRCDIR)/graphsdl.h \
	$(SRCDIR)/soundsdl.h

$(SRCDIR)/mos.o: $(MOS_C)

# Build MOS_SYS.C
MOS_SYS_C = $(DEPCOMMON) \
	$(SRCDIR)/mos.h \
	$(SRCDIR)/mos_sys.h \
	$(SRCDIR)/screen.h \
	$(SRCDIR)/keyboard.h \
	$(SRCDIR)/graphsdl.h

$(SRCDIR)/mos_sys.o: $(MOS_SYS_C)

# Build EDITOR.C
EDITOR_C = $(DEPCOMMON) \
	$(SRCDIR)/variables.h \
	$(SRCDIR)/heap.h \
	$(SRCDIR)/tokens.h \
	$(SRCDIR)/strings.h \
	$(SRCDIR)/miscprocs.h \
	$(SRCDIR)/stack.h \
	$(SRCDIR)/fileio.h

$(SRCDIR)/editor.o: $(EDITOR_C)

# Build CONVERT.C
CONVERT_C = $(DEPCOMMON) \
	$(SRCDIR)/convert.h \
	$(SRCDIR)/miscprocs.h

$(SRCDIR)/convert.o: $(CONVERT_C)

# Build COMMANDS.C
COMMANDS_C = $(DEPCOMMON) \
	$(SRCDIR)/miscprocs.h \
	$(SRCDIR)/tokens.h \
	$(SRCDIR)/statement.h \
	$(SRCDIR)/variables.h \
	$(SRCDIR)/editor.h \
	$(SRCDIR)/heap.h \
	$(SRCDIR)/stack.h \
	$(SRCDIR)/strings.h \
	$(SRCDIR)/evaluate.h \
	$(SRCDIR)/screen.h \
	$(SRCDIR)/keyboard.h

$(SRCDIR)/commands.o: $(COMMANDS_C)

# Build BRANDY.C
BRANDY_C = $(DEPCOMMON) \
	$(SRCDIR)/tokens.h \
	$(SRCDIR)/heap.h \
	$(SRCDIR)/editor.h \
	$(SRCDIR)/commands.h \
	$(SRCDIR)/statement.h \
	$(SRCDIR)/fileio.h \
	$(SRCDIR)/mos.h \
	$(SRCDIR)/keyboard.h \
	$(SRCDIR)/screen.h \
	$(SRCDIR)/miscprocs.h \
	$(SRCDIR)/evaluate.h \
	$(SRCDIR)/net.h

$(SRCDIR)/brandy.o: $(BRANDY_C)

# Build ASSIGN.C
ASSIGN_C = $(DEPCOMMON) \
	$(SRCDIR)/tokens.h \
	$(SRCDIR)/heap.h \
	$(SRCDIR)/stack.h \
	$(SRCDIR)/strings.h \
	$(SRCDIR)/variables.h \
	$(SRCDIR)/miscprocs.h \
	$(SRCDIR)/editor.h \
	$(SRCDIR)/evaluate.h \
	$(SRCDIR)/lvalue.h \
	$(SRCDIR)/statement.h \
	$(SRCDIR)/assign.h \
	$(SRCDIR)/fileio.h \
	$(SRCDIR)/mos.h \
	$(SRCDIR)/graphsdl.h

$(SRCDIR)/assign.o: $(ASSIGN_C)

# Build TEXTONLY.C
TEXTONLY_C = $(DEPCOMMON) \
	$(SRCDIR)/scrcommon.h \
	$(SRCDIR)/screen.h \
	$(SRCDIR)/keyboard.h

$(SRCDIR)/textonly.o: $(TEXTONLY_C)

# Build SIMPLETEXT.C
$(SRCDIR)/simpletext.o: $(TEXTONLY_C)
