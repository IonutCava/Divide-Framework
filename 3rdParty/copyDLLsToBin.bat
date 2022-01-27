if "%~1"=="" goto run_Debug
if "%~1"=="Debug" goto run_Debug
if "%~1"=="Profile" goto run_Profile
if "%~1"=="Release" goto run_Release

:run_Release
ECHO Copying Release DLLs
robocopy assimp/bin/Release/ ../Build/ assimp-vc142-mt.dll /NP /NJH /NJS
robocopy sdl/bin/ ../Build/ SDL2.dll /NP /NJH /NJS
robocopy sdl/bin/ ../Build/ SDL2_mixer.dll /NP /NJH /NJS
robocopy cegui/bin/ ../Build/ CEGUIBase-0.dll /NP /NJH /NJS
robocopy cegui/bin/ ../Build/ CEGUICommonDialogs-0.dll /NP /NJH /NJS
robocopy cegui/bin/ ../Build/ CEGUICoreWindowRendererSet.dll /NP /NJH /NJS
robocopy cegui/bin/ ../Build/ CEGUILuaScriptModule-0.dll /NP /NJH /NJS
robocopy cegui/bin/ ../Build/ CEGUISTBImageCodec.dll /NP /NJH /NJS
robocopy cegui/bin/ ../Build/ CEGUITinyXMLParser.dll /NP /NJH /NJS
robocopy physx4/install/vc15win64/PhysX/bin/win.x86_64.vc142.mt/release/ ../Build/ PhysX_64.dll  /NP /NJH /NJS
robocopy physx4/install/vc15win64/PhysX/bin/win.x86_64.vc142.mt/release/ ../Build/ PhysXCommon_64.dll /NP /NJH /NJS
robocopy physx4/install/vc15win64/PhysX/bin/win.x86_64.vc142.mt/release/ ../Build/ PhysXCooking_64.dll /NP /NJH /NJS
robocopy physx4/install/vc15win64/PhysX/bin/win.x86_64.vc142.mt/release/ ../Build/ PhysXFoundation_64.dll /NP /NJH /NJS
goto run_Common

:run_Profile
ECHO Copying Profile DLLs
robocopy assimp/bin/RelWithDebInfo/ ../Build/ assimp-vc142-mt.dll /NP /NJH /NJS
robocopy sdl/bin/ ../Build/ SDL2p.dll /NP /NJH /NJS
robocopy sdl/bin/ ../Build/ SDL2_mixer.dll /NP /NJH /NJS
robocopy cegui/bin/ ../Build/ CEGUIBase-0.dll /NP /NJH /NJS
robocopy cegui/bin/ ../Build/ CEGUICommonDialogs-0.dll /NP /NJH /NJS
robocopy cegui/bin/ ../Build/ CEGUICoreWindowRendererSet.dll /NP /NJH /NJS
robocopy cegui/bin/ ../Build/ CEGUILuaScriptModule-0.dll /NP /NJH /NJS
robocopy cegui/bin/ ../Build/ CEGUISTBImageCodec.dll /NP /NJH /NJS
robocopy cegui/bin/ ../Build/ CEGUITinyXMLParser.dll /NP /NJH /NJS
robocopy physx4/install/vc15win64/PhysX/bin/win.x86_64.vc142.mt/profile/ ../Build/ PhysX_64.dll  /NP /NJH /NJS
robocopy physx4/install/vc15win64/PhysX/bin/win.x86_64.vc142.mt/profile/ ../Build/ PhysXCommon_64.dll /NP /NJH /NJS
robocopy physx4/install/vc15win64/PhysX/bin/win.x86_64.vc142.mt/profile/ ../Build/ PhysXCooking_64.dll /NP /NJH /NJS
robocopy physx4/install/vc15win64/PhysX/bin/win.x86_64.vc142.mt/profile/ ../Build/ PhysXFoundation_64.dll /NP /NJH /NJS
goto run_Common

:run_Debug
ECHO Copying Debug DLLs
robocopy assimp/bin/Debug/ ../Build/ assimp-vc142-mtd.dll /NP /NJH /NJS
robocopy sdl/bin/ ../Build/ SDL2d.dll /NP /NJH /NJS
robocopy sdl/bin/ ../Build/ SDL2_mixer_d.dll /NP /NJH /NJS
robocopy cegui/bin/ ../Build/ CEGUIBase-0_d.dll /NP /NJH /NJS
robocopy cegui/bin/ ../Build/ CEGUICommonDialogs-0_d.dll /NP /NJH /NJS
robocopy cegui/bin/ ../Build/ CEGUICoreWindowRendererSet_d.dll /NP /NJH /NJS
robocopy cegui/bin/ ../Build/ CEGUILuaScriptModule-0_d.dll /NP /NJH /NJS
robocopy cegui/bin/ ../Build/ CEGUISTBImageCodec_d.dll /NP /NJH /NJS
robocopy cegui/bin/ ../Build/ CEGUITinyXMLParser_d.dll /NP /NJH /NJS
robocopy physx4/install/vc15win64/PhysX/bin/win.x86_64.vc142.mt/debug/ ../Build/ PhysX_64.dll  /NP /NJH /NJS
robocopy physx4/install/vc15win64/PhysX/bin/win.x86_64.vc142.mt/debug/ ../Build/ PhysXCommon_64.dll /NP /NJH /NJS
robocopy physx4/install/vc15win64/PhysX/bin/win.x86_64.vc142.mt/debug/ ../Build/ PhysXCooking_64.dll /NP /NJH /NJS
robocopy physx4/install/vc15win64/PhysX/bin/win.x86_64.vc142.mt/debug/ ../Build/ PhysXFoundation_64.dll /NP /NJH /NJS
goto run_Common

:run_Common
robocopy nvtt/bin64/ ../Build/ nvtt.dll /NP /NJH /NJS
robocopy sdl/bin/ ../Build/ libFLAC-8.dll /NP /NJH /NJS
robocopy sdl/bin/ ../Build/ libmpg123-0.dll /NP /NJH /NJS
robocopy sdl/bin/ ../Build/ libogg-0.dll /NP /NJH /NJS
robocopy sdl/bin/ ../Build/ libopus-0.dll /NP /NJH /NJS
robocopy sdl/bin/ ../Build/ libopusfile-0.dll /NP /NJH /NJS
robocopy sdl/bin/ ../Build/ libvorbis-0.dll /NP /NJH /NJS
robocopy sdl/bin/ ../Build/ libvorbisfile-3.dll /NP /NJH /NJS
robocopy sdl/bin/ ../Build/ libmodplug-1.dll /NP /NJH /NJS
robocopy sdl/bin/ ../Build/ libmpg123-0.dll /NP /NJH /NJS

set/A errlev="%ERRORLEVEL% & 24"
exit/B %errlev%