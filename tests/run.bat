@echo off
set SCRIPT_DIR=%~dp0
set RUN_BIN=%SCRIPT_DIR%\run.exe
clang -g -Wall -Wextra -Werror -Wfatal-errors %SCRIPT_DIR%\run.c -o %RUN_BIN%
%RUN_BIN% %*