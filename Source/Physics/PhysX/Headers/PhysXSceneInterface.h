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
#ifndef DVD_PHYSX_SCENE_INTERFACE_H_
#define DVD_PHYSX_SCENE_INTERFACE_H_

#include "PhysXActor.h"
#include "Physics/Headers/PhysicsSceneInterface.h"

namespace physx
{
    class PxDefaultCpuDispatcher;
}

namespace Divide {
struct SGNRayResult;
struct Ray;
class Scene;

class PhysXSceneInterface final : public PhysicsSceneInterface {
    using LoadQueue = moodycamel::ConcurrentQueue<PhysXActor*>;

   public:
    PhysXSceneInterface(Scene& parentScene);
    virtual ~PhysXSceneInterface() override;

    [[nodiscard]] bool init() override;
    [[nodiscard]] bool isInit() const noexcept override;
    void idle() override;
    void release() override;
    void frameStarted(U64 deltaTimeGameUS ) override;
    void frameEnded(U64 deltaTimeGameUS ) override;

    // We DO NOT take ownership of actors. Ownership remains with RigidBodyComponent
    void addRigidActor(PhysXActor* actor);
    void updateRigidActor(physx::PxRigidActor* oldActor, physx::PxRigidActor* newActor) const;
    physx::PxScene* getPhysXScene() const noexcept { return _gScene; }

    bool intersect(const Ray& intersectionRay, float2 range, vector<SGNRayResult>& intersectionsOut) const;
   protected:
    static void UpdateActor(physx::PxActor* actor);

   private:
    using RigidMap = vector<PhysXActor*>;
    physx::PxScene* _gScene = nullptr;
    physx::PxDefaultCpuDispatcher* _cpuDispatcher = nullptr;
    RigidMap _sceneRigidActors;

    std::atomic_bool _rigidActorsQueued{false};
    std::atomic_bool _physxResultsPending{false};

    LoadQueue _sceneRigidQueue;
};

FWD_DECLARE_MANAGED_CLASS(PhysXSceneInterface);

};  // namespace Divide

#endif //DVD_PHYSX_SCENE_INTERFACE_H_
