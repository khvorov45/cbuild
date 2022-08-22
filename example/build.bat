if exist shell.bat call shell.bat

cl -FC -Wall -nologo -Zi ^
    -wd4204 -wd4100 -wd4820 -wd4255 -wd5045 -wd4668 -wd4201 -wd4464 ^
    -Foexample/build.obj -Fdexample/build.pdb -Feexample/build.exe ^
    example/build.c && .\example\build.exe
