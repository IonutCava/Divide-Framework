

#include "Headers/NavigationSystem.h"

namespace Divide {
    NavigationSystem::NavigationSystem(ECS::ECSEngine& parentEngine, PlatformContext& context)
        : PlatformContextComponent(context),
          ECSSystem(parentEngine)
    {
    }

} //namespace Divide