@echo off

if "%~1"=="" goto run_Default
if "%~1"=="-D" goto run_Debug
if "%~1"=="Debug" goto run_Debug
if "%~1"=="-R" goto run_Release
if "%~1"=="Release" goto run_Release

:run_Default
echo No arguments specified. Running default (release) mode!
goto run_Release

:run_Release
echo Running Release Mode
if exist "ProjectManager\Build\x64-release\bin\ProjectManager.exe" (
	echo Launching Executable
	ProjectManager\Build\x64-release\bin\ProjectManager.exe %*
) else (
	echo No release executable found. Trying debug build ..
	goto run_Debug
)
goto exit

:run_Debug
echo Running Debug Mode
if exist "ProjectManager\Build\x64-debug\bin\ProjectManager.exe" (
	echo Launching Executable
	ProjectManager\Build\x64-debug\bin\ProjectManager.exe %*
) else (
	echo No executable found. Exiting ...
	goto exit
)

:exit
echo Exiting ...
exit/B %errlev%