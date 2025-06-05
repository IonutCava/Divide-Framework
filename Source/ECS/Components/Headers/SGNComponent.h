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
#ifndef DVD_SGN_COMPONENT_H_
#define DVD_SGN_COMPONENT_H_

#include "EditorComponent.h"
#include "Core/Headers/PlatformContextComponent.h"
#include <ECS/ComponentManager.h>
#include <ECS/Component.h>

namespace Divide {

/// A generic component for the SceneGraphNode class
enum class RenderStage : U8;
class SceneGraphNode;
class SGNComponent;
class SceneRenderState;
struct RenderStagePass;

} //namespace Divide 

namespace ECS
{
    struct CustomEvent
    {
        enum class Type : Divide::U8
        {
            TransformUpdated = 0,
            AnimationUpdated,
            AnimationChanged,
            AnimationReSync,
            RelationshipCacheInvalidated,
            BoundsUpdated,
            EntityPostLoad,
            EntityFlagChanged,
            NewShaderReady,
            COUNT
        };

        Type _type = Type::COUNT;
        Divide::SGNComponent* _sourceCmp = nullptr;
        Divide::U32 _flag = 0u;

        struct DataPair
        {
            Divide::U16 _first;
            Divide::U16 _second;
        };

        union
        {
            DataPair _dataPair;
            Divide::U32 _data = 0u;
        };
    };
}

namespace Divide
{

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

    template<typename T, ComponentType C>
    static bool Register();

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

        FORCE_INLINE static bool RegisterComponentType()
        {
            return Factory::Register<T, C>();
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

struct EntityOnUpdate;

class SGNComponent : protected PlatformContextComponent,
                     public Factory<SGNComponent>
{
    public:
        explicit SGNComponent(Key key, ComponentType type, SceneGraphNode* parentSGN, PlatformContext& context);

        virtual void OnData(const ECS::CustomEvent& data);

        virtual void saveToXML(boost::property_tree::ptree& pt) const;
        virtual void loadFromXML(const boost::property_tree::ptree& pt);

        virtual bool saveCache(ByteBuffer& outputBuffer) const;
        virtual bool loadCache(ByteBuffer& inputBuffer);

        [[nodiscard]] U64 uniqueID() const;

        [[nodiscard]] virtual bool enabled() const;
                       virtual void enabled(bool state);

        [[nodiscard]] EditorComponent& editorComponent() noexcept { return _editorComponent; }

        POINTER_R_IW(SceneGraphNode, parentSGN, nullptr);
        PROPERTY_R_IW(ComponentType, type, ComponentType::COUNT);
        PROPERTY_R(EditorComponent, editorComponent);

    protected:
        friend class EditorComponent;

        std::atomic_bool _enabled{true};
        mutable std::atomic_bool _hasChanged{false};
};

template<typename T, ComponentType C>
using BaseComponentType = SGNComponent::Registrar<T, C>;

#define INIT_COMPONENT(X) static bool X##_registered = X::s_registered

#define DVD_COMPONENT_SIGNATURE(Name, Enum) class Name##Component final : public BaseComponentType<Name##Component, Enum>

#define DVD_COMPONENT_PARENT(Name, Enum) using Parent = BaseComponentType<Name##Component, Enum>; \
                                         friend class Name##System;
#define BEGIN_COMPONENT(Name, Enum) \
    DVD_COMPONENT_SIGNATURE(Name, Enum) { \
    DVD_COMPONENT_PARENT(Name, Enum)

#define BEGIN_COMPONENT_EXT1(Name, Enum, Base1) \
    DVD_COMPONENT_SIGNATURE(Name, Enum) , public Base1 {  \
    DVD_COMPONENT_PARENT(Name, Enum)
    
#define END_COMPONENT(Name) \
    }; \
    INIT_COMPONENT(Name##Component);

}  // namespace Divide
#endif //DVD_SGN_COMPONENT_H_

#include "SGNComponent.inl"
