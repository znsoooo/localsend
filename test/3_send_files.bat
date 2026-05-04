@echo off
cd send
..\..\localsend.exe 127.0.0.1 file.txt file.txt empty.txt large.txt "with space.txt" жпнд.txt empty dir dir/subdir dir\subdir
cd ..
pause
