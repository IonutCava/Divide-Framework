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
#ifndef _ECS_SYSTEM_H_
#define _ECS_SYSTEM_H_

#include <ECS/ComponentManager.h>
#include <ECS/System.h>
#include <ECS/Engine.h>

namespace Divide {
    class ByteBuffer;
    class SceneGraphNode;
    template<class T, class U>
    class ECSSystem;

    struct ECSSerializerProxy : ECS::ISystemSerializer {
        virtual ~ECSSerializerProxy() = default;

        virtual bool saveCache(const SceneGraphNode* sgn, ByteBuffer& outputBuffer) = 0;
        virtual bool loadCache(SceneGraphNode* sgn, ByteBuffer& inputBuffer) = 0;
    };

    template<class T, class U>
    struct ECSSerializer final : ECSSerializerProxy {
        [[nodiscard]] bool saveCache(const SceneGraphNode* sgn, ByteBuffer& outputBuffer) override {
            assert(_parent != nullptr);

            return _parent->saveCache(sgn, outputBuffer);
        }
        [[nodiscard]] bool loadCache(SceneGraphNode* sgn, ByteBuffer& inputBuffer) override {
            assert(_parent != nullptr);

            return _parent->loadCache(sgn, inputBuffer);
        }

        ECSSystem<T, U>* _parent = nullptr;
    };

    template<class T, class U>
    class ECSSystem : public ECS::System<T> {
    public:

        explicit ECSSystem(ECS::ECSEngine& engine);
        virtual ~ECSSystem() = default;

        virtual bool saveCache(const SceneGraphNode* sgn, ByteBuffer& outputBuffer);
        virtual bool loadCache(SceneGraphNode* sgn, ByteBuffer& inputBuffer);

        void PreUpdate(F32 dt) override;
        void Update(F32 dt) override;
        void PostUpdate(F32 dt) override;
        void OnFrameStart() override;
        void OnFrameEnd() override;

        [[nodiscard]] ECS::ISystemSerializer& GetSerializer() noexcept override { return _serializer; };
        [[nodiscard]] const ECS::ISystemSerializer& GetSerializer() const noexcept override { return _serializer; };

    protected:
        ECS::ECSEngine& _engine;

        ECSSerializer<T, U> _serializer;
        vector_fast<U*> _componentCache;

    };

}

#endif //_ECS_SYSTEM_H_

#include "ECSSystem.inl"