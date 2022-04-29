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
#ifndef _DIVIDE_PCH_
#define _DIVIDE_PCH_

#define _ENFORCE_MATCHING_ALLOCATORS 0
#define _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS

#include "Platform/Headers/PlatformDefinesOS.h"

#define IMGUI_USER_CONFIG "Core\Headers\ImGUICustomConfig.h"

#if !defined(CPP_VERSION)
#   define CPP_VERSION __cplusplus
#endif

#if CPP_VERSION > 1
#   define CPP_98_SUPPORT
#   define CPP_03_SUPPORT
#   if CPP_VERSION >= 201103L
#       define CPP_11_SUPPORT
#           if CPP_VERSION >= 201402L
#               define CPP_14_SUPPORT
#               if CPP_VERSION > 201402L
#                   if HAS_CPP17
#                       define CPP_17_SUPPORT
#                   endif
#               endif
#           endif
#   endif
#endif 

// As of May 2020
#if !defined(CPP_17_SUPPORT)
#error "Divide Framework requires C++17 support at a minimum!."
#endif 

#ifndef BOOST_EXCEPTION_DISABLE
#define BOOST_EXCEPTION_DISABLE
#endif

#ifndef BOOST_CONFIG_SUPPRESS_OUTDATED_MESSAGE
#define BOOST_CONFIG_SUPPRESS_OUTDATED_MESSAGE
#endif 

#ifndef GLBINDING_STATIC_DEFINE
#define GLBINDING_STATIC_DEFINE
#endif

#ifndef GLBINDING_AUX_STATIC_DEFINE
#define GLBINDING_AUX_STATIC_DEFINE
#endif

#ifndef GLEW_STATIC
#define GLEW_STATIC
#endif 

#ifndef CEGUI_BUILD_STATIC_FACTORY_MODULE
#define CEGUI_BUILD_STATIC_FACTORY_MODULE
#endif

#ifndef TINYXML_STATIC
#define TINYXML_STATIC
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244) //warning C4244: 'return': conversion from 'int' to 'int8_t', possible loss of data
#pragma warning(disable: 4458) //warning C4458: declaration of 'shift' hides class member
#pragma warning(disable: 4310) //warning C4310: cast truncates constant value 
#endif
#include <skarupke/bytell_hash_map.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "Core/TemplateLibraries/Headers/TemplateAllocator.h"

#include <EASTL/include/EASTL/set.h>
#include <EASTL/include/EASTL/list.h>
#include <EASTL/include/EASTL/array.h>
#include <EASTL/include/EASTL/stack.h>
#include <EASTL/include/EASTL/queue.h>
#include <EASTL/include/EASTL/string.h>
#include <EASTL/include/EASTL/fixed_set.h>
#include <EASTL/include/EASTL/fixed_vector.h>
#include <EASTL/include/EASTL/weak_ptr.h>
#include <EASTL/include/EASTL/unordered_set.h>

#include <climits>
#include <xmmintrin.h>
#include <cstring>
#include <iomanip>
#include <random>
#include <stack>
#include <any>
#include <limits>
#include <execution>

#include <boost/property_tree/ptree.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/functional/factory.hpp>

#define HAVE_M_PI
#define SDL_MAIN_HANDLED
#include <sdl/include/SDL.h>

#include <Vulkan/vulkan.hpp>
#include <Optick/src/optick.h>

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable: 4624) // warning C4624: destructor was implicitly defined as deleted (union Slot_{})
#endif
// Let's face it. This will never get fixed
#define freeSlots freeSlots_
#include <MemoryPool/StackAlloc.h>
#include <MemoryPool/C-11/MemoryPool.h>
#undef freeSlots
#ifdef _MSC_VER
#pragma warning (pop)
#endif

#include <simplefilewatcher/include/FileWatcher/FileWatcher.h>

#include <ArenaAllocator/arena_allocator.h>

#include <ChaiScript/include/chaiscript/chaiscript.hpp>
#include <ChaiScript/include/chaiscript/chaiscript_stdlib.hpp>
#include <ChaiScript/include/chaiscript/utility/utility.hpp>

#include <ConcurrentQueue/concurrentqueue.h>
#include <ConcurrentQueue/blockingconcurrentqueue.h>

#include <fmt/include/fmt/format.h>
#include <fmt/include/fmt/printf.h>

#include <imgui.h>

#include <CEGUI/CEGUI.h>

#include "Platform/Threading/Headers/SharedMutex.h"

#include "Core/TemplateLibraries/Headers/HashMap.h"
#include "Core/TemplateLibraries/Headers/Vector.h"
#include "Core/TemplateLibraries/Headers/String.h"
#include "Core/TemplateLibraries/Headers/CircularBuffer.h"

#include <EntityComponentSystem/include/ECS/ECS.h>

#undef _ENFORCE_MATCHING_ALLOCATORS
#define _ENFORCE_MATCHING_ALLOCATORS 1
#undef _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS

#include "Platform/Headers/ConditionalWait.h"
#include "Core/Headers/NonCopyable.h"
#include "Core/Headers/NonMovable.h"
#include "Core/Headers/GUIDWrapper.h"

#include "Platform/File/Headers/ResourcePath.h"
#include "Core/Math/Headers/MathMatrices.h"
#include "Core/Math/Headers/Quaternion.h"
#include "Core/Headers/TaskPool.h"
#include "Core/Headers/Console.h"

#endif //_DIVIDE_PCH_
