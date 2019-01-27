@rem Make Brandy Basic for DOS with DJGPP compiler
@rem Edit DJGPP and PATH paths to match your installation

@set DJGPP=C:\Apps\Programming\djgpp\djgpp.env
@set PATH=C:\Apps\Programming\djgpp\bin;%PATH%
@set BRANDY_BUILD_FLAGS=-DNEWKBD
@
@cd %0\..
@make -f makefile.djgpp nodebug
@mkdir ..\binaries >NUL: 2>NUL:
@if exist brandy.exe copy brandy.exe ..\binaries\brandyDJP.exe >NUL:
@if exist brandy del brandy
@if exist brandy.exe del brandy.exe
@pause
