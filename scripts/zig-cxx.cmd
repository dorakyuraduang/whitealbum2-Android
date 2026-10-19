@echo off
"%~dp0..\tools\python\ziglang\zig.exe" c++ -target x86_64-windows-gnu %*
