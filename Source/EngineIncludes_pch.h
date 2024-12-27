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

#if defined(ENABLE_MIMALLOC)
#include <mimalloc.h>
#endif //ENABLE_MIMALLOC

#include "Platform/Headers/PlatformDefinesOS.h"

// As of October 2022
#if __cplusplus < 201704L
#error "Divide Framework requires C++20 support at a minimum!"
#endif 

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif //IMGUI_DEFINE_MATH_OPERATORS

#include <stdexcept>
#include <bytell_hash_map.hpp>

DISABLE_GCC_WARNING_PUSH(nonnull)
#include <EASTL/list.h>
#include <EASTL/array.h>
#include <EASTL/stack.h>
#include <EASTL/queue.h>
#include <EASTL/set.h>
#include <EASTL/string.h>
#include <EASTL/fixed_set.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/map.h>
#include <EASTL/fixed_vector.h>
#include <EASTL/fixed_string.h>
#include <EASTL/unordered_set.h>
DISABLE_GCC_WARNING_POP()

#include <climits>
#include <xmmintrin.h>
#include <cstring>
#include <iomanip>
#include <random>
#include <stack>
#include <span>
#include <any>
#include <list>
#include <limits>
#include <execution>
#include <fstream>
#include <condition_variable>

#include <fmt/printf.h>

#include "Core/TemplateLibraries/Headers/TemplateAllocator.h"
#include "Core/TemplateLibraries/Headers/HashMap.h"
#include "Core/TemplateLibraries/Headers/Vector.h"
#include "Core/TemplateLibraries/Headers/String.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/functional/factory.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/deadline_timer.hpp>

#include <SDL2/SDL.h>

#include "Core/Headers/Profiler.h"

#if defined(VKAPI_PTR)
#undef VKAPI_PTR
#endif //VKAPI_PTR

// Let's face it. This will never get fixed
#define freeSlots freeSlots_
#include <StackAlloc.h>
#include <C-11/MemoryPool.h>
#undef freeSlots

#include <concurrentqueue/concurrentqueue.h>
#include <concurrentqueue/blockingconcurrentqueue.h>

#include <ctre.hpp>

DISABLE_MSVC_WARNING_PUSH(4702) // unreachable code
#include <chaiscript/chaiscript.hpp>
#include <chaiscript/utility/utility.hpp>
DISABLE_MSVC_WARNING_POP()

#include "Platform/Threading/Headers/SharedMutex.h"

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
