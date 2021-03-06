@if EXIST ..\version.h goto SHOW
@if NOT EXIST ..\Makefile.version goto ERR1
@if NOT EXIST mkvers1.bat goto ERR2
@echo updating/creating ..\version.h ...
@set TEMP1=1
@for /F "skip=8 tokens=3" %%i in (..\Makefile.version) do @call mkvers1 %%i
@if "%TEMPX1%." == "." goto NOX1
@if "%TEMPX2%." == "." goto NOX1
@if "%TEMPX3%." == "." goto NOX1
@set TEMP1=..\version.h
@echo #ifndef XMLRPC_C_VERSION_INCLUDED > %TEMP1%
@echo #define XMLRPC_C_VERSION_INCLUDED >> %TEMP1%
@echo /* generated by Windows/mkvers.bat on %DATE% ... */ >> %TEMP1%
@echo #define XMLRPC_C_VERSION "Xmlrpc-c %TEMPX1%.%TEMPX2%.%TEMPX3%" >> %TEMP1%
@echo #define XMLRPC_VERSION_MAJOR %TEMPX1% >> %TEMP1%
@echo #define XMLRPC_VERSION_MINOR %TEMPX2% >> %TEMP1%
@echo #define XMLRPC_VERSION_POINT %TEMPX3% >> %TEMP1%
@echo #endif >> %TEMP1%
type %TEMP1%
@echo ..\version.h set to the above ...
@set TEMP1=
@set TEMPX1=
@set TEMPX2=
@set TEMPX3=
@goto END 

:NOX1
@echo Some error occurred in the batch process ...
@goto NOVER

:NOVER
@echo Failed to create ..\version.h .
@pause
@goto END


:ERR1
@echo Can not locate ..\Makefile.version ... check name, location ...
@pause
@goto END
:ERR2
@echo Can not locate mkvers1.bat ... check name, location ...
@pause
@goto END

:SHOW
@echo ..\version.h already exist, with version ...
@type ..\version.h
@echo Delete this file if you wish to redo it ...
@pause
@goto END

:END
