cmake_minimum_required(VERSION 3.19.0)
project(brandy LANGUAGES C)

add_compile_options(-Wall)

set(SRCDIR src)
set(SRC ${SRCDIR}/variables.c ${SRCDIR}/tokens.c ${SRCDIR}/strings.c
	${SRCDIR}/statement.c ${SRCDIR}/stack.c ${SRCDIR}/miscprocs.c
	${SRCDIR}/mainstate.c ${SRCDIR}/lvalue.c ${SRCDIR}/keyboard.c
	${SRCDIR}/iostate.c ${SRCDIR}/heap.c ${SRCDIR}/functions.c
	${SRCDIR}/fileio.c ${SRCDIR}/evaluate.c ${SRCDIR}/errors.c
	${SRCDIR}/mos.c ${SRCDIR}/editor.c ${SRCDIR}/convert.c
	${SRCDIR}/commands.c ${SRCDIR}/brandy.c ${SRCDIR}/assign.c
	${SRCDIR}/net.c ${SRCDIR}/mos_sys.c)

add_executable(sbrandy ${SRC} ${SRCDIR}/simpletext.c)
add_executable(tbrandy ${SRC} ${SRCDIR}/textonly.c)

target_link_libraries(sbrandy m dl pthread)
target_link_libraries(tbrandy m dl pthread)

install(TARGETS sbrandy DESTINATION bin)
install(TARGETS tbrandy DESTINATION bin)


find_package(SDL 1.2)
# We want to request a version before 2, but cmake does not support version
# ranges for SDL.

IF (SDL_FOUND)
	add_executable(brandy ${SRC} ${SRCDIR}/graphsdl.c ${SRCDIR}/soundsdl.c)

	target_link_libraries(brandy m dl pthread)
	target_include_directories(brandy PRIVATE ${SDL_INCLUDE_DIRS})
	target_link_libraries(brandy m dl pthread ${SDL_LIBRARIES})
	target_compile_definitions(brandy PRIVATE -DUSE_SDL)

	# libX11 may not be needed, eg., under Windows.
	find_package(X11)
	IF (X11_FOUND)
		target_link_libraries(brandy ${X11_LIBRARIES})
	ENDIF()

	install(TARGETS brandy DESTINATION bin)
ENDIF()

IF (WIN32)
	add_compile_definitions(-DCYGWINBUILD)
	add_compile_definitions(-D__USE_MINGW_ANSI_STDIO)

	target_link_libraries(sbrandy ws2_32 psapi)
	target_link_libraries(tbrandy ws2_32 psapi)

	IF (SDL_FOUND)
		target_link_libraries(brandy ws2_32 psapi)
	ENDIF()

ENDIF()

IF (LINUX)
	target_link_libraries(sbrandy rt)
	target_link_libraries(tbrandy rt)

	IF (SDL_FOUND)
		target_link_libraries(brandy rt)
	ENDIF()
ENDIF()

# Inside "build-push-action" we have no .git. Rather than fighting, we put up
# with it.
IF (EXISTS .git)
	execute_process(
		COMMAND git rev-parse --short HEAD
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		OUTPUT_VARIABLE GIT_COMMIT
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)

	execute_process(
		COMMAND git rev-parse --abbrev-ref HEAD
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		OUTPUT_VARIABLE GIT_BRANCH
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)

	execute_process(
		COMMAND git log -1 --format=%cd
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		OUTPUT_VARIABLE GIT_DATE
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)

	add_compile_definitions(-DBRANDY_GITCOMMIT=\"${GIT_COMMIT}\" -DBRANDY_GITBRANCH=\"${GIT_BRANCH}\" -DBRANDY_GITDATE=\"${GIT_DATE}\")
ENDIF()

# Do not throw an error on missing features.
add_compile_definitions(DEFAULT_IGNORE)

enable_testing()

# Shebang does not work on msys2, running "prove" directly does not work.
IF (WIN32)
	find_program(PERL NAMES perl)
	find_program(PROVE NAMES prove)

	add_test(NAME Regressions COMMAND ${PERL} ${PROVE} --exec ${CMAKE_BINARY_DIR}/sbrandy -r t/)
ELSE()
	add_test(NAME Regressions COMMAND prove --exec ${CMAKE_BINARY_DIR}/sbrandy -r t/)

	find_program(VALGRIND NAMES valgrind)
	IF (VALGRIND)
		add_test(NAME RegressionsValgrind COMMAND prove --exec "${VALGRIND} ${CMAKE_BINARY_DIR}/sbrandy" -r t/)
	ENDIF()
ENDIF()

install(FILES ${CMAKE_SOURCE_DIR}/brandy.1 DESTINATION share/man/man1)
install(FILES ${CMAKE_SOURCE_DIR}/brandy.1 DESTINATION share/man/man1 RENAME tbrandy.1)
install(FILES ${CMAKE_SOURCE_DIR}/brandy.1 DESTINATION share/man/man1 RENAME sbrandy.1)
