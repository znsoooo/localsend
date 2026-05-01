@echo off
del localsend.exe
g++ localsend.cpp -lws2_32 -o localsend.exe
localsend.exe
