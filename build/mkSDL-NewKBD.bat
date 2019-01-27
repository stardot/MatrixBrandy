@rem Make Brandy Basic for Windows-SDL with MinGW compiler
@rem Edit PATH to GCC tools to match your installation

@set PATH=C:\Apps\Programming\TDM-GCC-32\bin;%PATH%
@set BRANDY_BUILD_FLAGS=-DNEWKBD
@
@cd %0\..
@mingw32-make -f makefile.mingw-sdl nodebug
@mkdir ..\binaries >NUL: 2>NUL:
@if exist brandy.exe copy brandy.exe ..\binaries\brandySDL.exe >NUL:
@if exist brandy del brandy
@if exist brandy.exe del brandy.exe
@pause
