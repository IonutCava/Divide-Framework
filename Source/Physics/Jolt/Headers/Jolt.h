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
#ifndef DVD_PHYSICS_JOLT_H_
#define DVD_PHYSICS_JOLT_H_

#ifndef DVD_PHYSICS_API_FOUND_
#define DVD_PHYSICS_API_FOUND_
#endif

#include "Physics/Headers/PhysicsAPIWrapper.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>

namespace Divide
{

class PhysicsAsset;
class SceneGraphNode;
class PhysicsJolt final : public PhysicsAPIWrapper
{

public:
    explicit PhysicsJolt( PlatformContext& context );

    [[nodiscard]] ErrorCode initPhysicsAPI(U8 targetFrameRate, F32 simSpeed) override;
    [[nodiscard]] bool closePhysicsAPI() override;
    void frameStartedInternal(U64 deltaTimeGameUS ) override;
    void frameEndedInternal(U64 deltaTimeGameUS ) override;
    void idle() override;


    [[nodiscard]] bool initPhysicsScene(Scene& scene) override;
    [[nodiscard]] bool destroyPhysicsScene(const Scene& scene) override;

    [[nodiscard]] bool intersect(const Ray& intersectionRay, float2 range, vector<SGNRayResult>& intersectionsOut) const override;

    [[nodiscard]] PhysicsAsset* createRigidActor(SceneGraphNode* node, RigidBodyComponent& parentComp) override;

    [[nodiscard]] bool convertActor(PhysicsAsset* actor, PhysicsGroup newGroup) override;

private:
    F32 _simulationSpeed = 1.0f;

    std::unique_ptr<JPH::Factory> _factory;
    std::unique_ptr<JPH::JobSystemThreadPool> _threadPool;
    std::unique_ptr<JPH::PhysicsSystem> _physicsSystem;
    std::unique_ptr<JPH::TempAllocatorImpl> _allocator;
};

};  // namespace Divide

#endif //DVD_PHYSICS_JOLT_H_
