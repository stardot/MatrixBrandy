cmake_minimum_required(VERSION 3.0.1)
project(brandy)

set(BRANDY_USE_GRAPHICS ON CACHE BOOL "Use Graphics")
set(BRANDY_USE_ANSI ON CACHE BOOL "Use ANSI Escape Sequences")

set(SRCDIR src)
set(SRC ${SRCDIR}/variables.c ${SRCDIR}/tokens.c ${SRCDIR}/strings.c
	${SRCDIR}/statement.c ${SRCDIR}/stack.c ${SRCDIR}/miscprocs.c
	${SRCDIR}/mainstate.c ${SRCDIR}/lvalue.c ${SRCDIR}/keyboard.c
	${SRCDIR}/iostate.c ${SRCDIR}/heap.c ${SRCDIR}/functions.c
	${SRCDIR}/fileio.c ${SRCDIR}/evaluate.c ${SRCDIR}/errors.c
	${SRCDIR}/mos.c ${SRCDIR}/editor.c ${SRCDIR}/convert.c
	${SRCDIR}/commands.c ${SRCDIR}/brandy.c ${SRCDIR}/assign.c
	${SRCDIR}/net.c ${SRCDIR}/mos_sys.c)

if(BRANDY_USE_GRAPHICS)
	set(SRC ${SRC} ${SRCDIR}/graphsdl.c)
	if(WIN32)
  		link_libraries(wsock32 ws2_32)
	else()
		find_package(X11 REQUIRED)
		include_directories(${X11_INCLUDE_DIR})
		link_libraries(${X11_LIBRARIES})
	endif()
	find_package(SDL REQUIRED)
	include_directories(${SDL_INCLUDE_DIR})
	link_libraries(${SDL_LIBRARY})
	add_definitions(-DUSE_SDL)
else()
	if(BRANDY_USE_ANSI)
		set(SRC ${SRC} ${SRCDIR}/textonly.c)
	else()
		set(SRC ${SRC} ${SRCDIR}/simpletext.c)
	endif()
	add_definitions(-DNO_SDL)
endif()

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

add_definitions(-DBRANDY_GITCOMMIT=\"${GIT_COMMIT}\" -DBRANDY_GITBRANCH=\"${GIT_BRANCH}\" -DBRANDY_GITDATE=\"${GIT_DATE}\")

add_definitions(-DDEFAULT_IGNORE)
link_libraries(m)
add_executable(brandy ${SRC})
add_library(brandyapp ${SRC})
target_compile_definitions(brandyapp PUBLIC -DBRANDYAPP)
