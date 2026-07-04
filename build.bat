@echo off
call "Z:\visual stdio 2022\IDE\VC\Auxiliary\Build\vcvars64.bat"
msbuild "Z:\github\MusicPlayer2-gw\MusicPlayer2.sln" /p:Configuration=Release /p:Platform=x64 /m /verbosity:minimal
