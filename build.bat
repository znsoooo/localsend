@echo off
del localsend.exe
g++ -std=c++17 localsend.cpp -lws2_32 -o localsend.exe
localsend.exe
