

#include "Headers/RigidBodySystem.h"

#include "Core/Headers/PlatformContext.h"
#include "Physics/Headers/PXDevice.h"

namespace Divide {
    RigidBodySystem::RigidBodySystem(ECS::ECSEngine& parentEngine, PlatformContext& context)
        : PlatformContextComponent(context),
          ECSSystem(parentEngine)
    {
    }

    void RigidBodySystem::OnFrameStart()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        Parent::OnFrameStart();
        for (RigidBodyComponent* comp : _componentCache) {
            if (comp->_rigidBody == nullptr) {
                comp->_rigidBody.reset(context().pfx().createRigidActor(comp->parentSGN(), *comp));
            }
        }
    }

} //namespace Divide
