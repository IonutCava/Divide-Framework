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
#ifndef DVD_INFINITE_PLANE_H_
#define DVD_INFINITE_PLANE_H_

#include "Graphs/Headers/SceneNode.h"

namespace Divide {

class GFXDevice;

FWD_DECLARE_MANAGED_CLASS(Quad3D);

DEFINE_NODE_TYPE(InfinitePlane, SceneNodeType::TYPE_INFINITEPLANE)
{

public:
    explicit InfinitePlane( const ResourceDescriptor<InfinitePlane>& descriptor );

protected:
    friend class ResourceCache;
    template <typename T> friend struct ResourcePool;

    bool postLoad() override;
    bool unload() override;
    void postLoad(SceneGraphNode* sgn) override;

    bool load( PlatformContext& context ) override;

    void prepareRender( SceneGraphNode* sgn,
                        RenderingComponent& rComp,
                        RenderPackage& pkg,
                        GFX::MemoryBarrierCommand& postDrawMemCmd,
                        RenderStagePass renderStagePass,
                        const CameraSnapshot& cameraSnapshot,
                        bool refreshData) override;
    void buildDrawCommands(SceneGraphNode* sgn, GenericDrawCommandContainer& cmdsOut) override;
    void sceneUpdate(U64 deltaTimeUS, SceneGraphNode* sgn, SceneState& sceneState) override;


private:
    uint2 _dimensions;
    Handle<Quad3D> _plane = INVALID_HANDLE<Quad3D>;
    size_t _planeRenderStateHash = 0u;
    size_t _planeRenderStateHashPrePass = 0u;
}; //InfinitePlane
} //namespace Divide

#endif //DVD_INFINITE_PLANE_H_
