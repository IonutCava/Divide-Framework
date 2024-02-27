/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the Software
is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef _ECS_SYSTEM_INL_
#define _ECS_SYSTEM_INL_

#include "Core/Headers/ByteBuffer.h"

namespace Divide {
    constexpr U16 BYTE_BUFFER_VERSION_ECS_MANAGER = 1u;

    template<class T, class U>
    ECSSystem<T, U>::ECSSystem(ECS::ECSEngine& engine)
        : _engine(engine)
    {
        _serializer._parent = this;
        _componentCache.reserve(Config::MAX_VISIBLE_NODES);
    }

    template<class T, class U>
    bool ECSSystem<T, U>::saveCache([[maybe_unused]] const SceneGraphNode* sgn, ByteBuffer& outputBuffer)
    {
        outputBuffer << BYTE_BUFFER_VERSION_ECS_MANAGER;
        return true;
    }

    template<class T, class U>
    bool ECSSystem<T, U>::loadCache([[maybe_unused]] SceneGraphNode* sgn, ByteBuffer& inputBuffer)
    {
        auto tempVer = decltype(BYTE_BUFFER_VERSION_ECS_MANAGER){0};
        inputBuffer >> tempVer;
        return tempVer == BYTE_BUFFER_VERSION_ECS_MANAGER;
    }

    template<class T, class U>
    void ECSSystem<T, U>::PreUpdate([[maybe_unused]] const F32 dt)
    {
    }

    template<class T, class U>
    void ECSSystem<T, U>::Update([[maybe_unused]] const F32 dt)
    {
        PROFILE_SCOPE_AUTO(Divide::Profiler::Category::GameLogic);
    }

    template<class T, class U>
    void ECSSystem<T, U>::PostUpdate( [[maybe_unused]] const F32 dt)
    {
        PROFILE_SCOPE_AUTO( Divide::Profiler::Category::GameLogic );
    }

    template<class T, class U>
    void ECSSystem<T, U>::OnFrameStart() 
    {
        PROFILE_SCOPE_AUTO( Divide::Profiler::Category::GameLogic );

        bool expected = true;
        if (ECS::ComponentMonitor<U>::s_ComponentsChanged.compare_exchange_strong(expected, false))
        {
            const auto container = _engine.GetComponentManager()->GetComponentContainer<U>();
            const size_t compCount = container->size();
            _componentCache.resize(compCount);

            auto iterBegin = container->begin();
            for (size_t idx = 0u; idx < compCount; ++idx)
            {
                _componentCache[idx] = &*iterBegin;
                ++iterBegin;
            }
        }
    }

    template<class T, class U>
    void ECSSystem<T, U>::OnFrameEnd() {

    }
}
#endif //_ECS_SYSTEM_INL_