@echo off
call "Z:\visual stdio 2022\IDE\VC\Auxiliary\Build\vcvars64.bat"
cd /d "Z:\github\MusicPlayer2-gw\test_hotkey_ctrl"
cl.exe /EHsc /Fe:test.exe test_hotkey_ctrl.cpp /link user32.lib
test.exe
