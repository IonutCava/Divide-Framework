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
#ifndef DVD_PCH_
#define DVD_PCH_

#include "CXXConfig.h"

// As of November 2025
#if !DIVIDE_HAS_CXX23
#error "Divide Framework requires C++23 support as a minimum!"
#endif 

#if !defined(HAS_SSE41) && !defined(HAS_NEON)
#   error "Divide Framework requires SSE4.1 or Neon at a minimum! (e.g. for _mm_dp_ps)"
#endif //HAS_SSE41

#if defined(IS_WINDOWS_BUILD) && defined(HAS_NEON)
#define _DISABLE_SOFTINTRIN_ 1
#endif //IS_WINDOWS_BUILD && HAS_NEON

#if defined(ENABLE_MIMALLOC)
#include <mimalloc.h>
#endif //ENABLE_MIMALLOC

#include "Platform/Headers/PlatformDefinesOS.h"

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif //IMGUI_DEFINE_MATH_OPERATORS

#if defined(HAS_NEON)
#include "sse2neon/sse2neon.h"
#else //HAS_NEON
#include <xmmintrin.h>
#endif //HAS_NEON

#include <stdexcept>
#include <stdio.h>
#include <bytell_hash_map.hpp>

DISABLE_GCC_WARNING_PUSH(nonnull)
#include <EASTL/list.h>
#include <EASTL/array.h>
#include <EASTL/stack.h>
#include <EASTL/queue.h>
#include <EASTL/set.h>
#include <EASTL/fixed_set.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/map.h>
#include <EASTL/fixed_string.h>
#include <EASTL/unordered_set.h>
DISABLE_GCC_WARNING_POP()

#include <stack>
#include <span>

#include <fmt/printf.h>

#include "Core/TemplateLibraries/Headers/TemplateAllocator.h"
#include "Core/TemplateLibraries/Headers/HashMap.h"
#include "Core/TemplateLibraries/Headers/Vector.h"
#include "Core/TemplateLibraries/Headers/String.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/functional/factory.hpp>

#include <SDL3/SDL.h>

#include "Core/Headers/Profiler.h"

#if defined(VKAPI_PTR)
#undef VKAPI_PTR
#endif //VKAPI_PTR

// Let's face it. This will never get fixed
#define freeSlots freeSlots_
#include <StackAlloc.h>
#include <C-11/MemoryPool.h>
#undef freeSlots

#include <concurrentqueue/moodycamel/concurrentqueue.h>
#include <concurrentqueue/moodycamel/blockingconcurrentqueue.h>

#include <ctre.hpp>

DISABLE_MSVC_WARNING_PUSH(4702) // unreachable code
#include <chaiscript/chaiscript.hpp>
#include <chaiscript/utility/utility.hpp>
DISABLE_MSVC_WARNING_POP()

#include <Jolt/Jolt.h>

#include "Platform/Threading/Headers/SharedMutex.h"

#include "Core/Headers/StringHelper.h"
#include "Core/Headers/Console.h"
#include <EntityComponentSystem/include/ECS/ComponentManager.h>
#include <EntityComponentSystem/include/ECS/Entity.h>

#include "Platform/Headers/ConditionalWait.h"
#include "Core/Headers/NonCopyable.h"
#include "Core/Headers/NonMovable.h"
#include "Core/Headers/GUIDWrapper.h"

#include "Platform/Headers/PlatformDefines.h"
#include "Core/Math/Headers/MathMatrices.h"
#include "Core/Math/Headers/Quaternion.h"
#include "Core/Headers/TaskPool.h"
#include "Platform/Video/Headers/RenderAPIEnums.h"

#endif //DVD_PCH_
