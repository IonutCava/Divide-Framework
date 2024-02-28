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
#ifndef _RIGID_BODY_COMPONENT_H_
#define _RIGID_BODY_COMPONENT_H_

#include "SGNComponent.h"
#include "Physics/Headers/PhysicsAsset.h"

namespace Divide {

    enum class PhysicsGroup : U8 {
        GROUP_STATIC = 0,
        GROUP_DYNAMIC,
        GROUP_KINEMATIC,
        GROUP_RAGDOL,
        GROUP_VEHICLE,
        GROUP_IGNORE,
        GROUP_COUNT
    };

    BEGIN_COMPONENT(RigidBody, ComponentType::RIGID_BODY)
      public:
        RigidBodyComponent(SceneGraphNode* parentSGN, PlatformContext& context);
        ~RigidBodyComponent();

        void physicsCollisionGroup(PhysicsGroup group);

        void onCollision(const RigidBodyComponent& collider);

        void onCollisionCbk(const DELEGATE<void, const RigidBodyComponent&>& cbk) { _collisionCbk = cbk; }

        PROPERTY_R(PhysicsGroup, physicsCollisionGroup, PhysicsGroup::GROUP_COUNT);
        PROPERTY_R(bool, isKinematic, false);

      private:
        bool filterCollision(const RigidBodyComponent& collider);

      private:
        PhysicsAsset_uptr _rigidBody;
        DELEGATE<void, const RigidBodyComponent&> _collisionCbk;
    END_COMPONENT(RigidBody)

    

} //namespace Divide

#endif //_RIGID_BODY_COMPONENT_H_