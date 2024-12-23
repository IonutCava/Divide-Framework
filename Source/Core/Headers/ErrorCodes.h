/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
IN CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef DVD_CORE_ERROR_CODES_H_
#define DVD_CORE_ERROR_CODES_H_

namespace Divide
{
    enum class ErrorCode : I8
    {
        NO_ERR = 0,
        MISSING_SCENE_DATA,
        MISSING_PROJECT_DATA,
        MISSING_SCENE_LOAD_CALL,
        CPU_NOT_SUPPORTED,
        GFX_NOT_SUPPORTED,
        GFX_NON_SPECIFIED,
        SFX_NON_SPECIFIED,
        PFX_NON_SPECIFIED,
        WINDOW_INIT_ERROR,
        SDL_WINDOW_INIT_ERROR,
        FONT_INIT_ERROR,
        GLBINGING_INIT_ERROR,
        GLSL_INIT_ERROR,
        GL_OLD_HARDWARE,
        VK_OLD_HARDWARE,
        GFX_OLD_HARDWARE,
        VK_SURFACE_CREATE,
        VK_DEVICE_CREATE_FAILED,
        VK_NO_GRAHPICS_QUEUE,
        VK_NO_PRESENT_QUEUE,
        SDL_AUDIO_INIT_ERROR,
        SDL_AUDIO_MIX_INIT_ERROR,
        FMOD_AUDIO_INIT_ERROR,
        OAL_INIT_ERROR,
        OCL_INIT_ERROR,
        PHYSX_INIT_ERROR,
        PHYSX_EXTENSION_ERROR,
        NO_LANGUAGE_INI,
        NOT_ENOUGH_RAM,
        WRONG_WORKING_DIRECTORY,
        PLATFORM_INIT_ERROR,
        PLATFORM_CLOSE_ERROR,
        PATHS_ERROR,
        EDITOR_INIT_ERROR,
        GUI_INIT_ERROR,
        NETWORK_CONNECT_ERROR,
        COUNT
    };

    namespace Names
    {
        static const char* errorCode[] = {
            "NO ERROR",
            "MISSING SCENE DATA",
            "MISSING PROJECT DATA",
            "MISSING SCENE LOAD CALL",
            "CPU NOT SUPPORTED",
            "RENDERING API NOT SUPPORTED",
            "RENDERING API NOT SPECIFIED",
            "SOUND API NOT SPECIFIED",
            "PHYSICS API NON SPECIFIED",
            "WINDOW INIT ERROR",
            "SDL WINDOW INIT ERROR",
            "FONT INIT ERROR",
            "GLBINGING INIT ERROR",
            "GLSL INIT ERROR",
            "OPENGL OLD HARDWARE",
            "VULKAN OLD HARDWARE",
            "GPU UNDER MIN SPEC",
            "VULKAN SURFACE CREATE",
            "VULKAN DEVICE CREATE FAILED",
            "VULKAN NO GRAHPICS QUEUE",
            "VULKAN NO PRESENT QUEUE",
            "SDL AUDIO INIT ERROR",
            "SDL AUDIO MIX INIT ERROR",
            "FMOD AUDIO INIT ERROR",
            "OPENAL INIT ERROR",
            "OPENCL INIT ERROR",
            "PHYSX INIT ERROR",
            "PHYSX EXTENSION ERROR",
            "NO LANGUAGE INI",
            "NOT ENOUGH RAM",
            "WRONG WORKING DIRECTORY",
            "PLATFORM INIT ERROR",
            "PLATFORM CLOSE ERROR",
            "PATHS ERROR",
            "EDITOR INIT ERROR",
            "GUI INIT ERROR",
            "NETWORK CONNECTION ERROR",
            "UNKNOWN"
        };
    }

    namespace TypeUtil
    {
        [[nodiscard]] inline const char* ErrorCodeToString( const ErrorCode err ) noexcept
        {
            return Names::errorCode[to_base( err )];
        }
    }
}

#endif //DVD_CORE_ERROR_CODES_H_
