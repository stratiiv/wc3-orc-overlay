@echo off
rem Збирає orc_hints.dll (32-біт, як і гра). Потрібен llvm-mingw або mingw-w64:
rem   winget install MartinStorsjo.LLVM-MinGW.MSVCRT
i686-w64-mingw32-gcc -O2 -Wall -shared -static -s -o orc_hints.dll orc.c d3d8hook.c d3d9hook.c -lgdi32 -lole32 -lshell32 -luser32
