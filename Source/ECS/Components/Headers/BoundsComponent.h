/*
Copyright (c) 2018-2026 DIVIDE-Studio
Copyright (c) 2009-2026 Ionut Cava

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
#ifndef DVD_BOUNDS_COMPONENT_H_
#define DVD_BOUNDS_COMPONENT_H_

#include "SGNComponent.h"
#include "BoundsUpdateState.h"
#include "Core/Math/BoundingVolumes/Headers/OBB.h"
#include "Core/Math/BoundingVolumes/Headers/BoundingBox.h"
#include "Core/Math/BoundingVolumes/Headers/BoundingSphere.h"

namespace Divide {
BEGIN_COMPONENT_EXT1(Bounds, ComponentType::BOUNDS, GUIDWrapper)
    public:
        BoundsComponent(SceneGraphNode* sgn, PlatformContext& context);

        [[nodiscard]] const BoundingBox& getBoundingBox() const noexcept { return _boundingBox; }
        [[nodiscard]] const BoundingSphere& getBoundingSphere() const noexcept { return _boundingSphere; }

        [[nodiscard]] OBB getOBB() const;

        void updateBoundingBoxTransform();
        void appendChildBBs();
        void appendChildRefBBs();

        PROPERTY_RW(bool, collisionsEnabled, true);
        PROPERTY_RW(bool, showAABB, false);
        PROPERTY_RW(bool, showOBB, false);
        PROPERTY_RW(bool, showBS, false);

    protected:
        friend class SceneGraph;
        friend class BoundsSystem;
        friend class TransformSystem;
        template<typename T, typename U>
        friend class ECSSystem;

        void setRefBoundingBox(const BoundingBox& nodeBounds) noexcept;

        // Flag the current BB as dirty and also flag all of the parents' bbs as dirty as well
        void flagBoundingBoxDirty(U32 transformMask, bool recursive);

    private:
        BoundsUpdateState _boundsUpdate;
        BoundsChangeTracker _boundsChanges;
        BoundingBox _boundingBox{};
        BoundingBox _refBoundingBox{};
        BoundingSphere _boundingSphere{};
        U64 _sourceBoundsRevision = U64_MAX;
        bool _containsWorldSpaceBounds = false;
        bool _rebuildChildBounds = false;


END_COMPONENT(Bounds)

[[nodiscard]] bool Collision(const BoundsComponent& lhs, const BoundsComponent& rhs) noexcept;

}; //namespace Divide

#endif //DVD_BOUNDS_COMPONENT_H_
