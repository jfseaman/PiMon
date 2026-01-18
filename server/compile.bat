@echo off
call "%~dp0setenv.bat"
rc /nologo pimon.rc
cl /FePiMon_Server.exe -O2 server.c pimon.res ws2_32.lib user32.lib gdi32.lib /link /SUBSYSTEM:WINDOWS /MACHINE:X64
