/* Copyright (c) 2018 DIVIDE-Studio
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
#ifndef DVD_SGN_COMPONENT_FACTORY_H_
#define DVD_SGN_COMPONENT_FACTORY_H_

#include <ECS/Component.h>
#include "EditorComponent.h"

namespace Divide {

/// A generic component for the SceneGraphNode class
enum class RenderStage : U8;
class SceneGraphNode;
class SGNComponent;
class SceneRenderState;
struct RenderStagePass;

} //namespace Divide 

namespace Divide
{

template<typename T, typename... Args>
void AddComponentToNode(SceneGraphNode* node, Args... args);

template<typename T>
void RemoveComponentFromNode(SceneGraphNode* node);

//ref: http://www.nirfriedman.com/2018/04/29/unforgettable-factory/
template <typename Base, typename... Args>
struct Factory
{
    using ConstructFunc = DELEGATE_STD<void, SceneGraphNode*, Args...>;
    using DestructFunc = DELEGATE_STD<void, SceneGraphNode*>;
    using FactoryContainerConstruct = ska::bytell_hash_map<ComponentType, ConstructFunc>;
    using FactoryContainerDestruct = ska::bytell_hash_map<ComponentType, DestructFunc>;

    template <typename... ConstructArgs>
    static void construct(ComponentType type, SceneGraphNode* node, ConstructArgs&&... args)
    {
        ConstructData().at(type)(node, FWD(args)...);
    }

    static void destruct(const ComponentType type, SceneGraphNode* node)
    {
        DestructData().at(type)(node);
    }

    template <typename T, ComponentType C>
    struct Registrar : ECS::Component<T>,
                       Base
    {
        template<typename... InnerArgs>
        Registrar(InnerArgs&&... args)
            : Base(Key{ s_registered }, C, FWD(args)...)
        {
        }

        void OnData([[maybe_unused]] const ECS::CustomEvent& data) override {}

        static bool RegisterComponentType()
        {
            Factory<Base, Args...>::ConstructData().emplace(C,
                [](SceneGraphNode* node, Args... args) -> void
                {
                    AddComponentToNode<T>(node, FWD(args)...);
                });

            Factory<Base, Args...>::DestructData().emplace(C,
                [](SceneGraphNode* node) -> void
                {
                    RemoveComponentFromNode<T>(node);
                });
            return true;
        }

        static bool s_registered;

        friend T;
    };

    friend Base;

private:
    struct Key
    {
        Key(const bool registered) noexcept : _registered(registered) {}

      private:
        bool _registered = false;

        template <typename T, ComponentType C>
        friend struct Registrar;
    };

    Factory() = default;

    static FactoryContainerConstruct& ConstructData()
    {
        NO_DESTROY static FactoryContainerConstruct container;
        return container;
    }

    static FactoryContainerDestruct& DestructData()
    {
        NO_DESTROY static FactoryContainerDestruct container;
        return container;
    }
};

template <typename Base, typename... Args>
template <typename T, ComponentType C>
bool Factory<Base, Args...>::Registrar<T, C>::s_registered = RegisterComponentType();


}  // namespace Divide
#endif //DVD_SGN_COMPONENT_FACTORY_H_
