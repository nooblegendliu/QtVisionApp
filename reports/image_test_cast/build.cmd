@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
cd /d "%~dp0"
cl /nologo /std:c++20 /EHsc /MD /O2 /utf-8 /I"..\..\VisionAlgorithm\include" /I"..\..\VisionCore\include" /I"..\..\..\..\Libraries\OpenCV-4.12\build-vs\install\include" probe.cpp /Fe:probe.exe /Fo:probe.obj /link "..\..\x64\Release\VisionAlgorithm.lib" "..\..\x64\Release\VisionCore.lib" /LIBPATH:"..\..\..\..\Libraries\OpenCV-4.12\build-vs\install\x64\vc18\lib" opencv_world4140.lib
exit /b %errorlevel%
