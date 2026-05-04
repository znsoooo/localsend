@echo off
rmdir /s /q recv
md recv
cd recv
..\..\localsend.exe
cd ..
pause
