@echo off

if "%~1"=="" goto run_Default
if "%~1"=="-D" goto run_Debug
if "%~1"=="Debug" goto run_Debug
if "%~1"=="-P" goto run_Profile
if "%~1"=="Profile" goto run_Profile
if "%~1"=="-R" goto run_Release
if "%~1"=="Release" goto run_Release

:run_Default
ECHO No arguments specified. Running default (release) mode!
goto run_Release

:run_Release
ECHO Running Release Mode
ECHO Launching Executable
Build\windows-msvc-release-editor\bin\Divide-Framework.exe %*
exit/B %errlev%

:run_Profile
ECHO Running Profile Mode
ECHO Launching Executable
Build\windows-msvc-profile-editor\bin\Divide-Framework.exe %*
exit/B %errlev%

:run_Debug
ECHO Running Debug Mode
ECHO Launching Executable
Build\windows-msvc-debug-editor\bin\Divide-Framework_d.exe %*

exit/B %errlev%