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
#ifndef _SCENE_GRAPH_NODE_INL_
#define _SCENE_GRAPH_NODE_INL_

namespace Divide {
    inline [[nodiscard]] bool SceneGraphNode::hasFlag(const Flags flag) const noexcept {
        return BitCompare(_nodeFlags, flag); 
    }

    inline void SceneGraphNode::occlusionCull(const RenderStagePass& stagePass,
                                              const Texture_ptr& depthBuffer,
                                              GFX::SendPushConstantsCommand& HIZPushConstantsCMDInOut,
                                              GFX::CommandBuffer& bufferInOut) const {
        Attorney::SceneNodeSceneGraph::occlusionCullNode(_node.get(), stagePass, depthBuffer, HIZPushConstantsCMDInOut, bufferInOut);
    }

    template<class Predicate>
    bool SceneGraphNode::forEachChild(const U32 start, const U32 end, Predicate predicate) const {
        SharedLock<SharedMutex> r_lock(_childLock);
        for (U32 i = start; i < end; ++i) {
            //SharedLock<SharedMutex> r_lock(_childLock);
            if (!predicate(_children[i], i)) {
                return false;
            }
        }

        return true;
    }

    template<class Predicate>
    bool SceneGraphNode::forEachChild(Predicate predicate) const {
        return forEachChild(0u, getChildCount(), predicate);
    }

    template <>
    inline TransformComponent* SceneGraphNode::get<TransformComponent>() const noexcept {
        return Hacks._transformComponentCache;
    }

    template <>
    inline BoundsComponent* SceneGraphNode::get<BoundsComponent>() const noexcept {
        return Hacks._boundsComponentCache;
    }

}; //namespace Divide

#endif //_SCENE_GRAPH_NODE_INL_