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
ECHO Copying DLLs
robocopy 3rdParty\assimp\bin\Release\ Build\ assimp-vc142-mt.dll /NP /NJH /NJS
robocopy 3rdParty\physx4\physx\bin\win.x86_64.vc142.mt\release\ Build\ PhysX_64.dll  /NP /NJH /NJS
robocopy 3rdParty\physx4\physx\bin\win.x86_64.vc142.mt\release\ Build\ PhysXCommon_64.dll /NP /NJH /NJS
robocopy 3rdParty\physx4\physx\bin\win.x86_64.vc142.mt\release\ Build\ PhysXFoundation_64.dll /NP /NJH /NJS
robocopy 3rdParty\physx4\physx\bin\win.x86_64.vc142.mt\release\ Build\ PhysXCooking_64.dll /NP /NJH /NJS
robocopy 3rdParty\physx4\physx\bin\win.x86_64.vc142.mt\release\ Build\ PhysXGPU_64.dll /NP /NJH /NJS
ECHO Launching Executable
Build\Divide.exe %*
exit /b

:run_Profile
ECHO Running Profile Mode
ECHO Copying DLLs
robocopy 3rdParty\assimp\bin\Release\ Build\ assimp-vc142-mt.dll /NP /NJH /NJS
robocopy 3rdParty\physx4\physx\bin\win.x86_64.vc142.mt\profile\ Build\ PhysX_64.dll  /NP /NJH /NJS
robocopy 3rdParty\physx4\physx\bin\win.x86_64.vc142.mt\profile\ Build\ PhysXCommon_64.dll /NP /NJH /NJS
robocopy 3rdParty\physx4\physx\bin\win.x86_64.vc142.mt\profile\ Build\ PhysXFoundation_64.dll /NP /NJH /NJS
robocopy 3rdParty\physx4\physx\bin\win.x86_64.vc142.mt\profile\ Build\ PhysXCooking_64.dll /NP /NJH /NJS
robocopy 3rdParty\physx4\physx\bin\win.x86_64.vc142.mt\profile\ Build\ PhysXGPU_64.dll /NP /NJH /NJS
ECHO Launching Executable
Build\Divide_p.exe %*
exit /b

:run_Debug
ECHO Running Profile Mode
ECHO Copying DLLs
robocopy 3rdParty\assimp\bin\Debug\ Build\ assimp-vc142-mt.dll /NP /NJH /NJS
robocopy 3rdParty\physx4\physx\bin\win.x86_64.vc142.mt\debug\ Build\ PhysX_64.dll  /NP /NJH /NJS
robocopy 3rdParty\physx4\physx\bin\win.x86_64.vc142.mt\debug\ Build\ PhysXCommon_64.dll /NP /NJH /NJS
robocopy 3rdParty\physx4\physx\bin\win.x86_64.vc142.mt\debug\ Build\ PhysXFoundation_64.dll /NP /NJH /NJS
robocopy 3rdParty\physx4\physx\bin\win.x86_64.vc142.mt\debug\ Build\ PhysXCooking_64.dll /NP /NJH /NJS
robocopy 3rdParty\physx4\physx\bin\win.x86_64.vc142.mt\debug\ Build\ PhysXGPU_64.dll /NP /NJH /NJS
ECHO Launching Executable
Build\Divide_d.exe %*
exit /b