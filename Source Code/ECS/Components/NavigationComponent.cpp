#include "stdafx.h"

#include "Headers/NavigationComponent.h"
#include "Graphs/Headers/SceneGraphNode.h"

namespace Divide {

NavigationComponent::NavigationComponent(SceneGraphNode* parentSGN, PlatformContext& context)
    : BaseComponentType<NavigationComponent, ComponentType::NAVIGATION>(parentSGN, context),
      _navigationContext(NavigationContext::NODE_IGNORE),
      _overrideNavMeshDetail(false)
{
}

void NavigationComponent::navigationContext(const NavigationContext& newContext) {
    _navigationContext = newContext;
    
    const SceneGraphNode::ChildContainer& children = _parentSGN->getChildren();
    SharedLock<SharedMutex> w_lock(children._lock);
    const U32 childCount = children._count;
    for (U32 i = 0u; i < childCount; ++i) {
        NavigationComponent* navComp = children._data[i]->get<NavigationComponent>();
        if (navComp != nullptr) {
            navComp->navigationContext(newContext);
        }
    }
}

void NavigationComponent::navigationDetailOverride(const bool detailOverride) {
    _overrideNavMeshDetail = detailOverride;

    const SceneGraphNode::ChildContainer& children = _parentSGN->getChildren();
    SharedLock<SharedMutex> w_lock(children._lock);
    const U32 childCount = children._count;
    for (U32 i = 0u; i < childCount; ++i) {
        NavigationComponent* navComp = children._data[i]->get<NavigationComponent>();
        if (navComp != nullptr) {
            navComp->navigationDetailOverride(detailOverride);
        }
    }
}
}