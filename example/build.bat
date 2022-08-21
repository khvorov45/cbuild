if exist shell.bat call shell.bat

cl -FC -Wall -nologo -Zi ^
    -wd4204 -wd4100 -wd4820 -wd4255 -wd5045 -wd4668 -wd4201 -wd4464 ^
    -Foexample/ -Fdexample/ -Feexample/ ^
    example/build.c
    rem && .\example\build.exe
