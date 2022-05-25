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
#ifndef _SCENE_NODE_FWD_H_
#define _SCENE_NODE_FWD_H_

#include "Core/Math/Headers/Ray.h"

namespace Divide {

    /// Usage context affects lighting, navigation, physics, etc
    enum class NodeUsageContext : U8 {
        NODE_DYNAMIC = 0,
        NODE_STATIC
    };

    /// ToDo: Move particle emitter and triggers to components (it will make them way more dynamic) - Ionut
    enum class SceneNodeType : U16 {
        TYPE_OBJECT3D = 0,       ///< 3d objects in the scene
        TYPE_TRANSFORM,          ///< dummy node to stack multiple transforms
        TYPE_WATER,              ///< water node
        TYPE_TRIGGER,            ///< a scene trigger (perform action on contact)
        TYPE_PARTICLE_EMITTER,   ///< a particle emitter
        TYPE_SKY,                ///< sky node
        TYPE_INFINITEPLANE,      ///< the infinite plane that sits beneath everything in the world
        TYPE_VEGETATION,         ///< grass node
        COUNT
    };

    namespace Names {
        static const char* sceneNodeType[] = {
              "OBJECT3D", "TRANSFORM", "WATER", "TRIGGER", "PARTICLE_EMITTER", "SKY",
              "INFINITE_PLANE", "VEGETATION_GRASS", "UNKNOWN"
        };
    };

    enum class EditorDataState : U8
    {
        CHANGED = 0,
        QUEUED,
        PROCESSED,
        IDLE,
        COUNT
    };

    FWD_DECLARE_MANAGED_CLASS(SceneNode);

    struct SGNRayResult {
        I64 sgnGUID = -1;
        F32 dist = std::numeric_limits<F32>::max();
        const char* name = nullptr;
    };

    struct SGNIntersectionParams
    {
        Ray _ray;
        vec2<F32> _range = { 0.f, 1.f };
        const SceneNodeType* _ignoredTypes = nullptr;
        size_t _ignoredTypesCount = 0;
        bool _includeTransformNodes = true;
    };

}; //namespace Divide

#endif //_SCENE_NODE_FWD_H_