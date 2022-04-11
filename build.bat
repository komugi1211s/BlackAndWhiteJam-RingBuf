@echo off

IF NOT EXIST dist (
    REM "[Build]: Making dist directory."
    mkdir dist
)

setlocal
set COMPILEROPTION=/O1 /MDd /W1 /WX /Fo"./dist/" /Fd"./dist/"
set INCLUDES=/I "W:\CppProject\CppLib\RayLib\include"

set LIBPATH=/LIBPATH:"W:\CppProject\CppLib\RayLib\lib"
set LINK=opengl32.lib raylib.lib user32.lib winmm.lib shell32.lib gdi32.lib
set LINKOPTION=/INCREMENTAL:NO /pdb:"./dist/" /out:"./dist/main.exe" /NODEFAULTLIB:library /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup

set FILE=./src/main.cpp

rem "[Build]: Building executables."
cl.exe %COMPILEROPTION% %INCLUDES% %FILE% /link %LINKOPTION% %LIBPATH% %LINKS% 
endlocal


IF EXIST assets (
    REM "[Build]: Copying assets into dist directory."
    xcopy .\assets\ .\dist\assets\ /Y /E
) ELSE (
    REM "[Build]: WARNING - asset directory does not exist. skipping the copy of assets."
)

