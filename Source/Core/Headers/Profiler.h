/*
Copyright (c) 2018-2026 DIVIDE-Studio
Copyright (c) 2009-2026 Ionut Cava

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
#ifndef DVD_PROFILER_H_
#define DVD_PROFILER_H_

#include "Platform/Headers/PlatformDefines.h"

#include <cstdint>

#if USE_OPTICK
DISABLE_NON_MSVC_WARNING_PUSH("ignored-attributes")
#include <optick.h>
DISABLE_NON_MSVC_WARNING_POP()
#endif //USE_OPTICK

namespace Divide
{
class Application;

namespace Profiler
{

void Initialise();
void RegisterApp(Application* app);
void Shutdown();
void OnThreadStart(std::string_view threadName);
void OnThreadStop();


namespace Category
{
#if USE_OPTICK
    constexpr Optick::Category::Type Graphics  = Optick::Category::Rendering;
    constexpr Optick::Category::Type Sound     = Optick::Category::Audio;
    constexpr Optick::Category::Type Physics   = Optick::Category::Physics;
    constexpr Optick::Category::Type GameLogic = Optick::Category::GameLogic;
    constexpr Optick::Category::Type GUI       = Optick::Category::UI;
    constexpr Optick::Category::Type Streaming = Optick::Category::Streaming;
    constexpr Optick::Category::Type Scene     = Optick::Category::Scene;
    constexpr Optick::Category::Type Threading = Optick::Category::Wait;
    constexpr Optick::Category::Type IO        = Optick::Category::IO;
#else //USE_OPTICK
    constexpr std::uint64_t Graphics  =  0u;
    constexpr std::uint64_t Sound     =  1u;
    constexpr std::uint64_t Physics   =  2u;
    constexpr std::uint64_t GameLogic =  3u;
    constexpr std::uint64_t GUI       =  4u;
    constexpr std::uint64_t Streaming =  5u;
    constexpr std::uint64_t Scene     =  6u;
    constexpr std::uint64_t Threading =  7u;
    constexpr std::uint64_t IO        =  8u;
#endif //USE_OPTICK
};

enum class State : U8
{
    STARTED = 0u,
    STOPPED,
    COUNT
};

bool OnProfilerStateChanged( const Profiler::State state );

}; //namespace Profiler

}; //namespace Divide

// static_assert(true, "") added at the end to force require a semicolon after the macros

#if USE_OPTICK
#define PROFILE_SCOPE(NAME, CATEGORY) OPTICK_EVENT(NAME, CATEGORY); static_assert(true, "")
#define PROFILE_SCOPE_AUTO(CATEGORY) OPTICK_EVENT(OPTICK_FUNC, CATEGORY); static_assert(true, "")
#define PROFILE_TAG(NAME, ...) OPTICK_TAG( NAME, __VA_ARGS__ ); static_assert(true, "")
#define PROFILE_FRAME(NAME) NO_DESTROY OPTICK_FRAME( NAME ); static_assert(true, "")
// Runtime-named scope (name is copied once into Optick's shared description table). Takes a lock per call: keep it off per-draw paths.
#define PROFILE_SCOPE_DYNAMIC(NAME) OPTICK_EVENT_DYNAMIC(NAME) static_assert(true, "")

#define PROFILE_VK_INIT(DEVICES, PHYSICAL_DEVICES, CMD_QUEUES, CMD_QUEUES_FAMILY, NUM_CMD_QUEUS, FUNCTIONS) OPTICK_GPU_INIT_VULKAN(DEVICES, PHYSICAL_DEVICES, CMD_QUEUES, CMD_QUEUES_FAMILY, NUM_CMD_QUEUS, FUNCTIONS); static_assert(true, "")
#define PROFILE_VK_PRESENT(SWAP_CHAIN) OPTICK_GPU_FLIP(SWAP_CHAIN); static_assert(true, "")

#define PROFILE_VK_EVENT_AUTO() OPTICK_GPU_EVENT(OPTICK_FUNC); static_assert(true, "")
#define PROFILE_VK_EVENT(NAME) OPTICK_GPU_EVENT(NAME); static_assert(true, "")
#define PROFILE_VK_EVENT_AND_CONTEX(NAME, BUFFER) OPTICK_GPU_CONTEXT(BUFFER) \
                                                  PROFILE_VK_EVENT(NAME); static_assert(true, "")
#define PROFILE_VK_EVENT_AUTO_AND_CONTEXT(BUFFER) PROFILE_VK_EVENT_AND_CONTEX(OPTICK_FUNC, BUFFER); static_assert(true, "")

#else

#define PROFILE_SCOPE(NAME, CATEGORY) static_assert(true, "")
#define PROFILE_SCOPE_AUTO(CATEGORY) static_assert(true, "")
#define PROFILE_TAG(NAME, ...) static_assert(true, "")
#define PROFILE_FRAME(NAME) static_assert(true, "")
#define PROFILE_SCOPE_DYNAMIC(NAME) static_assert(true, "")

#define PROFILE_VK_INIT(DEVICES, PHYSICAL_DEVICES, CMD_QUEUES, CMD_QUEUES_FAMILY, NUM_CMD_QUEUS, FUNCTIONS) static_assert(true, "")
#define PROFILE_VK_PRESENT(SWAP_CHAIN) static_assert(true, "")

#define PROFILE_VK_EVENT_AUTO() static_assert(true, "")
#define PROFILE_VK_EVENT(NAME) static_assert(true, "")
#define PROFILE_VK_EVENT_AND_CONTEX(NAME, BUFFER) static_assert(true, "")
#define PROFILE_VK_EVENT_AUTO_AND_CONTEXT(BUFFER) static_assert(true, "")

#endif

#endif //DVD_PROFILER_H_
