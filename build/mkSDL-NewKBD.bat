@rem Make Brandy Basic for Windows-SDL with MinGW compiler
@rem Edit PATH to GCC tools to match your installation

@cd %0\..
@set PATH=C:\Apps\Programming\TDM-GCC-32\bin;%PATH%
@mingw32-make -f makefile.mingw-sdlKBD nodebug
@mkdir ..\binaries >NUL: 2>NUL:
@if exist brandy.exe copy brandy.exe ..\binaries\brandySDL.exe >NUL:
@if exist brandy del brandy
@if exist brandy.exe del brandy.exe
@pause
