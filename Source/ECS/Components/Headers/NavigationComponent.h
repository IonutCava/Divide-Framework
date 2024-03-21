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
#ifndef DVD_NAVIGATION_COMPONENT_H_
#define DVD_NAVIGATION_COMPONENT_H_

#include "SGNComponent.h"

namespace Divide {

class SceneGraphNode;
BEGIN_COMPONENT(Navigation, ComponentType::NAVIGATION)
public:
    enum class NavigationContext :U32 {
        NODE_OBSTACLE = 0,
        NODE_IGNORE
    };

    NavigationComponent(SceneGraphNode* parentSGN, PlatformContext& context);

    [[nodiscard]] const NavigationContext& navigationContext() const noexcept {
        return _navigationContext;
    }

    [[nodiscard]] bool navMeshDetailOverride() const noexcept { return _overrideNavMeshDetail; }

    void navigationContext(const NavigationContext& newContext);

    void navigationDetailOverride(bool detailOverride);

   protected:
    NavigationContext _navigationContext;
    bool _overrideNavMeshDetail;
END_COMPONENT(Navigation);

}  // namespace Divide

#endif //DVD_NAVIGATION_COMPONENT_H_
