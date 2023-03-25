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
#ifndef PHYSX_H_
#define PHYSX_H_

#ifndef _PHYSICS_API_FOUND_
#define _PHYSICS_API_FOUND_
#endif

#include "PhysXActor.h"
#include "PhysXSceneInterface.h"
#include "Physics/Headers/PhysicsAPIWrapper.h"

constexpr auto MAX_ACTOR_QUEUE = 30;

namespace Divide {

class PhysX;
class PxDefaultAllocator final : public physx::PxAllocatorCallback {
    void* allocate(const size_t size, const char*, const char*, int) noexcept  override {
        return malloc_aligned(size, 16);
    }

    void deallocate(void* ptr) noexcept override {
        free_aligned(ptr);
    }
};

class PhysicsAsset;
class SceneGraphNode;
class PhysX final : public PhysicsAPIWrapper {

public:
    explicit PhysX( PlatformContext& context );

    [[nodiscard]] ErrorCode initPhysicsAPI(U8 targetFrameRate, F32 simSpeed) override;
    [[nodiscard]] bool closePhysicsAPI() override;
    void update(U64 deltaTimeUS) override;
    void process(U64 deltaTimeUS) override;
    void idle() override;

    void updateTimeStep(U8 timeStepFactor, F32 simSpeed) override;

    [[nodiscard]] bool initPhysicsScene(Scene& scene) override;
    [[nodiscard]] bool destroyPhysicsScene(const Scene& scene) override;

    [[nodiscard]] bool intersect(const Ray& intersectionRay, vec2<F32> range, vector<SGNRayResult>& intersectionsOut) const override;

    [[nodiscard]] physx::PxPhysics* getSDK() const noexcept { return _gPhysicsSDK; }

    [[nodiscard]] PhysicsAsset* createRigidActor(SceneGraphNode* node, RigidBodyComponent& parentComp) override;

    [[nodiscard]] bool convertActor(PhysicsAsset* actor, PhysicsGroup newGroup) override;
    void togglePvdConnection() const;
    void createPvdConnection(const char* ip, physx::PxU32 port, physx::PxU32 timeout, bool useFullConnection);

#if PX_SUPPORT_GPU_PHYSX
    POINTER_R(physx::PxCudaContextManager, cudaContextManager, nullptr);
#endif //PX_SUPPORT_GPU_PHYSX

protected:
    PhysXSceneInterface_uptr _targetScene;
    physx::PxRigidActor* createActorForGroup(PhysicsGroup group, const physx::PxTransform& pose);
private:
    F32 _simulationSpeed = 1.0f;
    physx::PxPhysics* _gPhysicsSDK = nullptr;
    physx::PxCooking* _cooking = nullptr;
    physx::PxFoundation* _foundation = nullptr;
    physx::PxMaterial* _defaultMaterial = nullptr;
    physx::PxReal _timeStep = 0.0f;
    physx::PxU8   _timeStepFactor = 0;
    physx::PxReal _accumulator = 0.0f;
    physx::PxPvd* _pvd = nullptr;
    physx::PxPvdTransport* _transport = nullptr;
    physx::PxPvdInstrumentationFlags  _pvdFlags{};

    static physx::PxDefaultAllocator _gDefaultAllocatorCallback;

    static SharedMutex s_meshCacheLock;
    static hashMap<U64, physx::PxTriangleMesh*> s_gMeshCache;
};

};  // namespace Divide

#endif
