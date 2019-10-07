@rem Clean source - remove *.o files

@cd %0\..
@del /q ..\src\*.o
@if exist brandy copy brandy ..\binaries\brandy >NUL:
@if exist brandy del brandy
@if exist sbrandy copy sbrandy ..\binaries\sbrandy >NUL:
@if exist sbrandy del sbrandy
@if exist tbrandy copy tbrandy ..\binaries\tbrandy >NUL:
@if exist tbrandy del tbrandy
