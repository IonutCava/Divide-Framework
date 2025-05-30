/*
	Author : Tobias Stein
	Date   : 7th September, 2017
	File   : ComponentManager.h

	Manages all component container.

	All Rights Reserved. (c) Copyright 2016.
*/

#pragma once
#ifndef ECS__COMPONENT_MANAGER_H__
#define ECS__COMPONENT_MANAGER_H__


#include "API.h"
#include "Engine.h"

#include "IComponent.h"

#include "util/FamilyTypeID.h"

#include "Memory/MemoryChunkAllocator.h"



namespace ECS
{
	template<class U>
	struct ComponentMonitor {
		static std::atomic_bool s_ComponentsChanged;
	};

	template<class U>
	std::atomic_bool ComponentMonitor<U>::s_ComponentsChanged = false;

	class ECS_API ComponentManager : Memory::GlobalMemoryUser
	{
		friend class IComponent;

		DECLARE_LOGGER
		
		

		class IComponentContainer
		{
		public:
		
			virtual ~IComponentContainer()
			{}

			virtual const char* GetComponentContainerTypeName() const = 0;

			virtual void DestroyComponent(IComponent* object) = 0;
		};

    public:
		template<class T>
		class ComponentContainer : public Memory::MemoryChunkAllocator<T, COMPONENT_T_CHUNK_SIZE>, public IComponentContainer
		{
			ComponentContainer(const ComponentContainer&) = delete;
			ComponentContainer& operator=(ComponentContainer&) = delete;
		

		public:
		
			ComponentContainer() : Memory::MemoryChunkAllocator<T, COMPONENT_T_CHUNK_SIZE>("ComponentManager")
			{}

			virtual ~ComponentContainer()
			{}

			virtual const char* GetComponentContainerTypeName() const override
			{
				static const char* COMPONENT_TYPE_NAME{ typeid(T).name() };
				return COMPONENT_TYPE_NAME;
			}		

			virtual void DestroyComponent(IComponent* object) override
			{		
				// call d'tor
				object->~IComponent();

				this->DestroyObject(object);

				ComponentMonitor<T>::s_ComponentsChanged.store(true);
			}

		}; // class ComponentContainer

    private:

		ComponentManager(const ComponentManager&) = delete;
		ComponentManager& operator=(ComponentManager&) = delete;

		using ComponentContainerRegistry = eastl::unordered_map<ComponentTypeId, IComponentContainer*>;

		ComponentContainerRegistry m_ComponentContainerRegistry;

    
		using ComponentLookupTable = eastl::vector<IComponent*>;
		ComponentLookupTable	m_ComponentLUT;

		using EntityComponentMap = eastl::vector<eastl::vector<ComponentId>>;
		EntityComponentMap		m_EntityComponentMap;


		ComponentId	AqcuireComponentId(IComponent* component);
		void		ReleaseComponentId(ComponentId id);

		void		MapEntityComponent(EntityId entityId, ComponentId componentId, ComponentTypeId componentTypeId);
		void		UnmapEntityComponent(EntityId entityId, ComponentId componentId, ComponentTypeId componentTypeId);

	public:
		
		template<class T>
		using TComponentIterator = typename ComponentContainer<T>::iterator;

		ComponentManager();
		~ComponentManager();

		///-------------------------------------------------------------------------------------------------
		/// Fn:	template<class T, class ...ARGS> T* ComponentManager::AddComponent(const EntityId entityId,
		/// ARGS&&... args)
		///
		/// Summary:	Adds a component of type T to entity described by entityId.
		///
		/// Author:	Tobias Stein
		///
		/// Date:	30/09/2017
		///
		/// Typeparams:
		/// T - 	   	Generic type parameter.
		/// ...ARGS -  	Type of the ...args.
		/// Parameters:
		/// entityId - 	Identifier for the entity.
		/// args - 	   	Variable arguments providing [in,out] The arguments.
		///
		/// Returns:	Null if it fails, else a pointer to a T.
		///-------------------------------------------------------------------------------------------------

		template<class T, class ...ARGS>
		T* AddComponent(const EntityId entityId, ARGS&&... args)
		{
			// hash operator for hashing entity and component ids
			static constexpr eastl::hash<ComponentId> ENTITY_COMPONENT_ID_HASHER { eastl::hash<ComponentId>() };

			const ComponentTypeId CTID	= T::STATIC_COMPONENT_TYPE_ID;

			// aqcuire memory for new component object of type T
			void* pObjectMemory			= GetComponentContainer<T>()->CreateObject();

			const ComponentId componentId		= this->AqcuireComponentId((T*)pObjectMemory);
			((T*)pObjectMemory)->m_ComponentID = componentId;

			// create component inplace
			IComponent* component		= new (pObjectMemory)T(std::forward<ARGS>(args)...);

			component->m_Owner			= entityId;
			component->m_HashValue		= ENTITY_COMPONENT_ID_HASHER(entityId) ^ (ENTITY_COMPONENT_ID_HASHER(componentId) << 1);

			// create mapping from entity id its component id
			MapEntityComponent(entityId, componentId, CTID);

			ComponentMonitor<T>::s_ComponentsChanged.store(true);

			return static_cast<T*>(component);
		}

		///-------------------------------------------------------------------------------------------------
		/// Fn:	template<class T> void ComponentManager::RemoveComponent(const EntityId entityId)
		///
		/// Summary:	Removes the component of type T from an entity described by entityId.
		///
		/// Author:	Tobias Stein
		///
		/// Date:	30/09/2017
		///
		/// Typeparams:
		/// T - 	Generic type parameter.
		/// Parameters:
		/// entityId - 	Identifier for the entity.
		///-------------------------------------------------------------------------------------------------

		template<class T>
		void RemoveComponent(const EntityId entityId)
		{
			const ComponentTypeId CTID = T::STATIC_COMPONENT_TYPE_ID;

			const ComponentId componentId = this->m_EntityComponentMap[entityId.index][CTID];
			assert(componentId != INVALID_COMPONENT_ID && "FATAL: Trying to remove a component which is not used by this entity!");
                

			IComponent* component = this->m_ComponentLUT[componentId];

			assert(component != nullptr && "FATAL: Trying to remove a component which is not used by this entity!");

			// release object memory
			GetComponentContainer<T>()->DestroyObject(component);

			// unmap entity id to component id
			UnmapEntityComponent(entityId, componentId, CTID);

			ComponentMonitor<T>::s_ComponentsChanged.store(true);
		}

		void RemoveAllComponents(const EntityId entityId)
		{
			static const size_t NUM_COMPONENTS = this->m_EntityComponentMap[0].size();

			for (ComponentTypeId componentTypeId = 0; componentTypeId < NUM_COMPONENTS; ++componentTypeId)
			{
				const ComponentId componentId = this->m_EntityComponentMap[entityId.index][componentTypeId];
				if (componentId == INVALID_COMPONENT_ID)
					continue;

				IComponent* component = this->m_ComponentLUT[componentId];
				if (component != nullptr)
				{
					// get appropriate component container
					const auto it = this->m_ComponentContainerRegistry.find(componentTypeId);
					if (it != this->m_ComponentContainerRegistry.end())
						it->second->DestroyComponent(component);
					else
						assert(false && "Trying to release a component that wasn't created by ComponentManager!");

					// unmap entity id to component id
					UnmapEntityComponent(entityId, componentId, componentTypeId);
				}
			}
		}

		void PassDataToAllComponents(const EntityId entityId, const CustomEvent& data)
		{
			static const size_t NUM_COMPONENTS = this->m_EntityComponentMap[0].size();

			for (ComponentTypeId componentTypeId = 0; componentTypeId < NUM_COMPONENTS; ++componentTypeId)
			{
				const ComponentId componentId = this->m_EntityComponentMap[entityId.index][componentTypeId];
				if (componentId == INVALID_COMPONENT_ID)
					continue;

				if (IComponent* component = this->m_ComponentLUT[componentId])
				{
					component->OnData(data);
				}
			}
		}
		///-------------------------------------------------------------------------------------------------
		/// Fn:	template<class T> T* ComponentManager::GetComponent(const EntityId entityId)
		///
		/// Summary:	Get the component of type T of an entity. If component has no such component
		/// nullptr is returned.
		///
		/// Author:	Tobias Stein
		///
		/// Date:	30/09/2017
		///
		/// Typeparams:
		/// T - 	Generic type parameter.
		/// Parameters:
		/// entityId - 	Identifier for the entity.
		///
		/// Returns:	Null if it fails, else the component.
		///-------------------------------------------------------------------------------------------------

		template<class T>
		T* GetComponent(const EntityId entityId) 
		{
			const ComponentTypeId CTID = T::STATIC_COMPONENT_TYPE_ID;

			const ComponentId componentId = this->m_EntityComponentMap[entityId.index][CTID];

			// entity has no component of type T
			if (componentId == INVALID_COMPONENT_ID)
				return nullptr;

			return static_cast<T*>(this->m_ComponentLUT[componentId]);
		}

        ///-------------------------------------------------------------------------------------------------
        /// Fn:	template<class T> inline ComponentContainer<T> ComponentManager::GetComponentContainer()
        ///
        /// Summary:	Returns the collection of all components of type T.
        ///
        /// Author:	Tobias Stein
        ///
        /// Date:	24/09/2017
        ///
        /// Typeparams:
        /// T - 	Generic type parameter.
        ///
        /// Returns:	A ComponentContainer&lt;T&gt;
        ///-------------------------------------------------------------------------------------------------
        template<class T>
        inline ComponentContainer<T>* GetComponentContainer()
        {
            const ComponentTypeId CID = T::STATIC_COMPONENT_TYPE_ID;

            const auto it = this->m_ComponentContainerRegistry.find(CID);
            ComponentContainer<T>* cc = nullptr;

            if (it == this->m_ComponentContainerRegistry.end())
            {
                cc = new ComponentContainer<T>();
                this->m_ComponentContainerRegistry[CID] = cc;
            }
            else
                cc = static_cast<ComponentContainer<T>*>(it->second);

            assert(cc != nullptr && "Failed to create ComponentContainer<T>!");
            return cc;
        }

		///-------------------------------------------------------------------------------------------------
		/// Fn:	template<class T> inline TComponentIterator<T> ComponentManager::begin()
		///
		/// Summary:	Returns an forward iterator object that points to the beginning of a collection of 
		/// all components of type T.
		///
		/// Author:	Tobias Stein
		///
		/// Date:	24/09/2017
		///
		/// Typeparams:
		/// T - 	Generic type parameter.
		///
		/// Returns:	A TComponentIterator&lt;T&gt;
		///-------------------------------------------------------------------------------------------------

		template<class T>
		inline TComponentIterator<T> begin()
		{
			return GetComponentContainer<T>()->begin();
		}

		///-------------------------------------------------------------------------------------------------
		/// Fn:	template<class T> inline TComponentIterator<T> ComponentManager::end()
		///
		/// Summary:	Returns an forward iterator object that points to the end of a collection of
		/// all components of type T.
		///
		/// Author:	Tobias Stein
		///
		/// Date:	24/09/2017
		///
		/// Typeparams:
		/// T - 	Generic type parameter.
		///
		/// Returns:	A TComponentIterator&lt;T&gt;
		///-------------------------------------------------------------------------------------------------

		template<class T>
		inline TComponentIterator<T> end()
		{
			return GetComponentContainer<T>()->end();
		}

		template<class T>
		inline size_t size() const noexcept {
			return GetComponentContainer<T>()->size();
		}
	}; // ComponentManager


	template<class T> T* GetComponent( ComponentManager* mgr, const EntityId id )
	{
		return mgr->GetComponent<T>( id );
	}

	template<class T, class ...P> T* AddComponent( ComponentManager* mgr, const EntityId id, P&&... param )
	{
		return mgr->AddComponent<T>( id, std::forward<P>( param )... );
	}

	template<class T> void RemoveComponent( ComponentManager* mgr, const EntityId id )
	{
		mgr->RemoveComponent<T>( id );
	}

} // namespace ECS

#endif // ECS__COMPONENT_MANAGER_H__
