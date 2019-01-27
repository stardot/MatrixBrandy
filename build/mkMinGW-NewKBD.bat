@rem Make Brandy Basic for DOS with MinGW compiler
@rem Edit PATH to GCC tools to match your installation

@cd %0\..
@set PATH=C:\Apps\Programming\TDM-GCC-32\bin;%PATH%
@set BRANDY_BUILD_FLAGS=-DNEWKBD
@
@rem mingw32-make -f makefile.mingwKBD nodebug
@mingw32-make -f makefile.mingw nodebug
@mkdir ..\binaries >NUL: 2>NUL:
@if exist brandy.exe copy brandy.exe ..\binaries\brandyMGW.exe >NUL:
@if exist brandy del brandy
@if exist brandy.exe del brandy.exe
@pause
