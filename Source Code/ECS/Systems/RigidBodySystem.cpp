#include "stdafx.h"

#include "Headers/RigidBodySystem.h"

#include "Core/Headers/PlatformContext.h"
#include "Physics/Headers/PXDevice.h"

namespace Divide {
    RigidBodySystem::RigidBodySystem(ECS::ECSEngine& parentEngine, PlatformContext& context)
        : PlatformContextComponent(context),
          ECSSystem(parentEngine)
    {
    }

    RigidBodySystem::~RigidBodySystem() 
    {
    }

    void RigidBodySystem::PreUpdate(const F32 dt) {
        Parent::PreUpdate(dt);
    }

    void RigidBodySystem::Update(const F32 dt) {
        Parent::Update(dt);
    }

    void RigidBodySystem::PostUpdate(const F32 dt) {
        Parent::PostUpdate(dt);
    }

    void RigidBodySystem::OnFrameStart() {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        Parent::OnFrameStart();
        for (RigidBodyComponent* comp : _componentCache) {
            if (comp->_rigidBody == nullptr) {
                comp->_rigidBody.reset(context().pfx().createRigidActor(comp->parentSGN(), *comp));
            }
        }
    }

    void RigidBodySystem::OnFrameEnd() {
        Parent::OnFrameEnd();
    }

    bool RigidBodySystem::saveCache(const SceneGraphNode * sgn, ByteBuffer & outputBuffer) {
        return Parent::saveCache(sgn, outputBuffer);
    }

    bool RigidBodySystem::loadCache(SceneGraphNode * sgn, ByteBuffer & inputBuffer) {
        return Parent::loadCache(sgn, inputBuffer);
    }
} //namespace Divide
