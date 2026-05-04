@echo off
setlocal enabledelayedexpansion

rmdir /s /q send 2>nul
md send 2>nul
cd send

md "empty" 2>nul
echo content > "file.txt" 2>nul
echo 中文 > "中文.txt" 2>nul
echo with space > "with space.txt" 2>nul
type nul > "empty.txt" 2>nul

md "dir" 2>nul
md "dir\empty" 2>nul
md "dir\subdir" 2>nul
echo hello > "dir\subdir\hello.txt" 2>nul
echo world > "dir\subdir\world.txt" 2>nul
type nul > "dir\empty.txt" 2>nul

copy /b dir\subdir\hello.txt large.txt >nul
for /l %%i in (1,1,20) do (
    copy /b large.txt + large.txt large_temp.txt 1>nul,2>nul
    move /y large_temp.txt large.txt 1>nul,2>nul
)

pause
