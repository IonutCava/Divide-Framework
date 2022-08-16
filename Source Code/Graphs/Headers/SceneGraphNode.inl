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
    template<class T, typename... Args>
    void AddSGNComponent(SceneGraphNode* node, Args... args) {
        node->template AddSGNComponent<T>(FWD(args)...);
    }

    template<class T>
    void RemoveSGNComponent(SceneGraphNode* node) {
        node->template RemoveSGNComponent<T>();
    }

    inline [[nodiscard]] bool SceneGraphNode::hasFlag(const Flags flag) const noexcept {
        return BitCompare(_nodeFlags, flag); 
    }

    template <>
    inline TransformComponent* SceneGraphNode::get<TransformComponent>() const noexcept {
        return Hacks._transformComponentCache;
    }

    template <>
    inline BoundsComponent* SceneGraphNode::get<BoundsComponent>() const noexcept {
        return Hacks._boundsComponentCache;
    }  
    
    template <>
    inline RenderingComponent* SceneGraphNode::get<RenderingComponent>() const noexcept {
        return Hacks._renderingComponent;
    }


    template<bool checkInternalNode>
    inline SceneGraphNode* SceneGraphNode::findChildInternal(const U64 nameHash, const bool recursive) const {
        if (nameHash != 0u) {
            SharedLock<SharedMutex> r_lock(_children._lock);

            for (SceneGraphNode* child : _children._data) {
                if_constexpr(checkInternalNode) {
                    const U64 cmpHash = _ID(child->getNode().resourceName().c_str());
                    if (cmpHash == nameHash) {
                        return child;
                    }
                } else {
                    if (child->nameHash() == nameHash) {
                        return child;
                    }
                }
                if (recursive) {
                    SceneGraphNode* recChild = child->findChildInternal<checkInternalNode>(nameHash, recursive);
                    if (recChild != nullptr) {
                        return recChild;
                    }
                }
            }
        }

        return nullptr;
    }

    template<bool checkInternalNode>
    inline SceneGraphNode* SceneGraphNode::findChildInternal(const I64 GUID, const bool recursive) const {
        if (GUID != -1) {
            SharedLock<SharedMutex> r_lock(_children._lock);
            for (SceneGraphNode* child : _children._data) {
                if_constexpr(checkInternalNode) {
                    if (child->getNode().getGUID() == GUID) {
                        return child;
                    }
                } else {
                    if (child->getGUID() == GUID) {
                        return child;
                    }
                }
                if (recursive) {
                    SceneGraphNode* recChild = child->findChildInternal<checkInternalNode>(GUID, true);
                    if (recChild != nullptr) {
                        return recChild;
                    }
                }
            }
        }

        return nullptr;
    }
}; //namespace Divide

#endif //_SCENE_GRAPH_NODE_INL_