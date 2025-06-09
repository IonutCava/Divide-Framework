

#include "Headers/ECSManager.h"
#include "Headers/EnvironmentProbeSystem.h"
#include "Headers/NavigationSystem.h"
#include "Headers/RigidBodySystem.h"

#include "ECS/Systems/Headers/AnimationSystem.h"
#include "ECS/Systems/Headers/BoundsSystem.h"
#include "ECS/Systems/Headers/RenderingSystem.h"
#include "ECS/Systems/Headers/SelectionSystem.h"
#include "ECS/Systems/Headers/TransformSystem.h"
#include "ECS/Systems/Headers/DirectionalLightSystem.h"
#include "ECS/Systems/Headers/PointLightSystem.h"
#include "ECS/Systems/Headers/SpotLightSystem.h"

#include "ECS/Components/Headers/IKComponent.h"
#include "ECS/Components/Headers/NetworkingComponent.h"
#include "ECS/Components/Headers/RagdollComponent.h"
#include "ECS/Components/Headers/ScriptComponent.h"
#include "ECS/Components/Headers/SelectionComponent.h"
#include "ECS/Components/Headers/UnitComponent.h"

#include "Utility/Headers/Localization.h"

#include <ECS/SystemManager.h>

namespace Divide {
    constexpr U16 BYTE_BUFFER_VERSION = 1u;
    constexpr U32 g_cacheMarkerByteValue[2]{ 0xBADDCAFE, 0xDEADBEEF };

#define STUB_SYSTEM(Name) \
    class Name##System final : public ECSSystem<Name##System, Name##Component> {\
        public: explicit Name##System(ECS::ECSEngine& parentEngine) : ECSSystem(parentEngine) {}\
    }

STUB_SYSTEM(IK);
STUB_SYSTEM(Networking);
STUB_SYSTEM(Ragdoll);
STUB_SYSTEM(Script);
STUB_SYSTEM(Unit);

ECSManager::ECSManager(PlatformContext& context, ECS::ECSEngine& engine)
    : PlatformContextComponent(context),
      _ecsEngine(engine)
{
    auto* TSys = _ecsEngine.GetSystemManager()->AddSystem<TransformSystem>(_ecsEngine, _context);
    auto* ASys = _ecsEngine.GetSystemManager()->AddSystem<AnimationSystem>(_ecsEngine, _context);
    auto* RSys = _ecsEngine.GetSystemManager()->AddSystem<RenderingSystem>(_ecsEngine, _context);
    auto* BSys = _ecsEngine.GetSystemManager()->AddSystem<BoundsSystem>(_ecsEngine, _context);
    auto* PlSys = _ecsEngine.GetSystemManager()->AddSystem<PointLightSystem>(_ecsEngine, _context);
    auto* SlSys = _ecsEngine.GetSystemManager()->AddSystem<SpotLightSystem>(_ecsEngine, _context);
    auto* DlSys = _ecsEngine.GetSystemManager()->AddSystem<DirectionalLightSystem>(_ecsEngine, _context);
    auto* IKSys = _ecsEngine.GetSystemManager()->AddSystem<IKSystem>(_ecsEngine);
    auto* NavSys = _ecsEngine.GetSystemManager()->AddSystem<NavigationSystem>(_ecsEngine, _context);
    auto* NetSys = _ecsEngine.GetSystemManager()->AddSystem<NetworkingSystem>(_ecsEngine);
    auto* RagSys = _ecsEngine.GetSystemManager()->AddSystem<RagdollSystem>(_ecsEngine);
    auto* RBSys = _ecsEngine.GetSystemManager()->AddSystem<RigidBodySystem>(_ecsEngine, _context);
    auto* ScpSys = _ecsEngine.GetSystemManager()->AddSystem<ScriptSystem>(_ecsEngine);
    auto* SelSys = _ecsEngine.GetSystemManager()->AddSystem<SelectionSystem>(_ecsEngine, _context);
    auto* UnitSys = _ecsEngine.GetSystemManager()->AddSystem<UnitSystem>(_ecsEngine);
    auto* ProbeSys = _ecsEngine.GetSystemManager()->AddSystem<EnvironmentProbeSystem>(_ecsEngine, _context);
    
    ASys->AddDependencies(TSys);
    BSys->AddDependencies(ASys);
    RSys->AddDependencies(TSys);
    RSys->AddDependencies(BSys);
    DlSys->AddDependencies(BSys);
    PlSys->AddDependencies(DlSys);
    SlSys->AddDependencies(PlSys);
    IKSys->AddDependencies(ASys);
    UnitSys->AddDependencies(TSys);
    NavSys->AddDependencies(UnitSys);
    RagSys->AddDependencies(ASys);
    RBSys->AddDependencies(RagSys);
    NetSys->AddDependencies(RBSys);
    ScpSys->AddDependencies(UnitSys);
    SelSys->AddDependencies(UnitSys);
    ProbeSys->AddDependencies(UnitSys);

    _ecsEngine.GetSystemManager()->UpdateSystemWorkOrder();
}

bool ECSManager::saveCache(const SceneGraphNode* sgn, ByteBuffer& outputBuffer) const
{
    outputBuffer << BYTE_BUFFER_VERSION;

    const auto saveSystemCache = [sgn, &outputBuffer](ECS::ISystem* system)
    {
        ECSSerializerProxy& serializer = static_cast<ECSSerializerProxy&>(system->GetSerializer());
        if (!serializer.saveCache(sgn, outputBuffer))
        {
            Console::errorfn(LOCALE_STR("ECS_SAVE_ERROR"), system->GetSystemTypeName());
        }

        outputBuffer.addMarker(g_cacheMarkerByteValue);
        return true;
    };


    _ecsEngine.GetSystemManager()->ForEachSystem(saveSystemCache);
    return true;
}

bool ECSManager::loadCache(SceneGraphNode* sgn, ByteBuffer& inputBuffer) const
{
    auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
    inputBuffer >> tempVer;
    if (tempVer == BYTE_BUFFER_VERSION)
    {
        const auto loadSystemCache = [sgn, &inputBuffer](ECS::ISystem* system)
        {
            ECSSerializerProxy& serializer = static_cast<ECSSerializerProxy&>(system->GetSerializer());
            if (!serializer.loadCache(sgn, inputBuffer))
            {
                Console::errorfn(LOCALE_STR("ECS_LOAD_ERROR"), system->GetSystemTypeName());
            }
            inputBuffer.readSkipToMarker(g_cacheMarkerByteValue);
            return true;
        };

        _ecsEngine.GetSystemManager()->ForEachSystem(loadSystemCache);
        return true;
    }

    return false;
}

} //namespace Divide
