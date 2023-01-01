@echo off

set SCRIPT_DIR=%~dp0

set SHELL_BAT=%SCRIPT_DIR%\shell.bat
if exist %SHELL_BAT% call %SHELL_BAT%

set RUN_BIN=%SCRIPT_DIR%\run.exe
clang -g -Wall -Wextra -Werror -Wfatal-errors %SCRIPT_DIR%\run.c -o %RUN_BIN% -Xlinker /incremental:no
if %ERRORLEVEL% NEQ 0 (
  EXIT /B
)
%RUN_BIN% %*