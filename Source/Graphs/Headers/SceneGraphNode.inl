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
#ifndef DVD_SCENE_GRAPH_NODE_INL_
#define DVD_SCENE_GRAPH_NODE_INL_

namespace Divide
{
    template<typename T, typename... Args>
    void AddComponentToNode(SceneGraphNode* node, Args... args)
    {
        node->template AddSGNComponent<T>(FWD(args)...);
    }

    template<typename T>
    void RemoveComponentFromNode(SceneGraphNode* node)
    {
        node->template RemoveSGNComponent<T>();
    }

    template<class T, class ...P> requires std::is_base_of_v<SGNComponent, T>
    T* SceneGraphNode::AddSGNComponent( P&&... param )
    {
        SGNComponent* comp = static_cast<SGNComponent*>(AddComponent<T>( this, this->context(), FWD( param )... ));
        AddSGNComponentInternal( comp );
        return static_cast<T*>(comp);
    }

    template<class T> requires std::is_base_of_v<SGNComponent, T>
    void SceneGraphNode::RemoveSGNComponent()
    {
        RemoveSGNComponentInternal( static_cast<SGNComponent*>(GetComponent<T>()) );
        RemoveComponent<T>();
    }

    inline SceneGraphNode* SceneGraphNode::ChildContainer::getChild( const U32 idx )
    {
        DIVIDE_ASSERT( idx < _count );

        SharedLock<SharedMutex> r_lock( _lock );
        return _data[idx];
    }

    template<class E, class... ARGS>
    void SceneGraphNode::SendEvent( ARGS&&... eventArgs ) const
    {
        GetECSEngine().SendEvent<E>( FWD( eventArgs )... );
    }
    /// Sends a global event with dispatched happening immediately. Avoid using often. Bad for performance.
    template<class E, class... ARGS>
    void SceneGraphNode::SendAndDispatchEvent( ARGS&&... eventArgs ) const
    {
        GetECSEngine().SendEventAndDispatch<E>( FWD( eventArgs )... );
    }

    FORCE_INLINE bool SceneGraphNode::hasFlag(const Flags flag) const noexcept
    {
        return _nodeFlags & to_base(flag); 
    }

    template <>
    inline TransformComponent* SceneGraphNode::get<TransformComponent>() const
    {
        return Hacks._transformComponentCache;
    }

    template <>
    inline BoundsComponent* SceneGraphNode::get<BoundsComponent>() const
    {
        return Hacks._boundsComponentCache;
    }  
    
    template <>
    inline RenderingComponent* SceneGraphNode::get<RenderingComponent>() const
    {
        return Hacks._renderingComponent;
    }

    template<typename T> requires std::is_base_of_v<SceneNode, T> 
    Handle<T> ToHandle( const SceneNodeHandle handle )
    {
        static_assert(GetSceneNodeType<T>() != SceneNodeType::COUNT);
        DIVIDE_ASSERT(handle._type != SceneNodeType::COUNT);
        return handle._handle;
    }

    template<typename T> requires std::is_base_of_v<SceneNode, T>
    SceneNodeHandle FromHandle( const Handle<T> handle )
    {
        static_assert(GetSceneNodeType<T>() != SceneNodeType::COUNT);

        SceneNodeHandle ret { ._type = GetSceneNodeType<T>() };
        ret._handle._generation = handle._generation;
        ret._handle._index = handle._index;
        ret._nodePtr = Get( handle );
        ret._deleter = [handle]() { Handle<T> handleCpy = handle; DestroyResource( handleCpy ); };

        return ret;
    }
}; //namespace Divide

#endif //DVD_SCENE_GRAPH_NODE_INL_
