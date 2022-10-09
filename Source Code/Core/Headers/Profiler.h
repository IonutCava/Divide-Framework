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
#ifndef _PROFILER_H_
#define _PROFILER_H_

#include <Optick/src/optick.h>
#include "config.h"

namespace Divide
{
namespace Profiler
{
namespace detail
{
    constexpr bool enabled = Config::Profile::ENABLE_FUNCTION_PROFILING;
};

void Init();
void Shutdown();
void OnThreadStart(std::string_view threadName);
void OnThreadStop();

namespace Category
{
    constexpr Optick::Category::Type Graphics = Optick::Category::Rendering;
    constexpr Optick::Category::Type Sound = Optick::Category::Audio;
    constexpr Optick::Category::Type Physics = Optick::Category::Physics;
    constexpr Optick::Category::Type GameLogic = Optick::Category::GameLogic;
    constexpr Optick::Category::Type GUI = Optick::Category::UI;
    constexpr Optick::Category::Type Streaming = Optick::Category::Streaming;
    constexpr Optick::Category::Type Scene = Optick::Category::Scene;
    constexpr Optick::Category::Type Threading = Optick::Category::Wait;
    constexpr Optick::Category::Type IO = Optick::Category::IO;
};
}; //namespace Profiler

}; //namespace Divide

// static_assert(true, "") added at the end to force require a semicolon after the macros

#define PROFILE_SCOPE(NAME, CATEGORY) OPTICK_EVENT(NAME, CATEGORY); static_assert(true, "")
#define PROFILE_SCOPE_AUTO(CATEGORY) OPTICK_EVENT(OPTICK_FUNC, CATEGORY); static_assert(true, "")
#define PROFILE_TAG(NAME, ...) OPTICK_TAG( NAME, __VA_ARGS__ ); static_assert(true, "")
#define PROFILE_FRAME(NAME) OPTICK_FRAME( NAME ); static_assert(true, "")

#endif //_PROFILER_H_
