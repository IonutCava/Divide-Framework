///-------------------------------------------------------------------------------------------------
/// File:	include\IEntity.h.
///
/// Summary:	Base entity class containing no morte than entity id and state falgs.
///-------------------------------------------------------------------------------------------------

#pragma once
#ifndef ECS__I_ENTITY_H__
#define ECS__I_ENTITY_H__

#include "util/Handle.h"

namespace ECS
{
	using EntityTypeId						= TypeID;

	using EntityId							= util::Handle64;

	static const EntityId INVALID_ENTITY_ID = util::Handle64::INVALID_HANDLE;

	struct CustomEvent;

	template<class T, class ...P> T*   AddComponent(    ComponentManager*, EntityId, P&&... param );
	template<class T>             T*   GetComponent(    ComponentManager*, EntityId );
	template<class T>             void RemoveComponent( ComponentManager*, EntityId );

	class ECS_API IEntity
	{
		friend class EntityManager;

	private:

		// set on create; in EntityManager
		ComponentManager*	m_ComponentManagerInstance;

	protected:

		DECLARE_STATIC_LOGGER

		

		// set on create; in EntityManager
		EntityId				m_EntityID;		

		// if false, entity won't be updated
		bool					m_Active;
		static std::shared_mutex s_ComponentManagerLock;
	public:

		IEntity();
		virtual ~IEntity();

		template<class T>
		T* GetComponent() const
		{
			std::shared_lock<std::shared_mutex> r_lock(s_ComponentManagerLock);
			return ECS::GetComponent<T>( this->m_ComponentManagerInstance, this->m_EntityID );
		}

		template<class T, class ...P>
		T* AddComponent(P&&... param)
		{
			std::scoped_lock<std::shared_mutex > r_lock(s_ComponentManagerLock);
			return ECS::AddComponent<T>( this->m_ComponentManagerInstance, this->m_EntityID, std::forward<P>( param )... );
		}

		template<class T>
		void RemoveComponent()
		{
			std::scoped_lock<std::shared_mutex> r_lock(s_ComponentManagerLock);
			ECS::RemoveComponent<T>( this->m_ComponentManagerInstance, this->m_EntityID );
		}

		void RemoveAllComponents();

		void PassDataToAllComponents(const ECS::CustomEvent& evt);
		// COMPARE ENTITIES

		inline bool operator==(const IEntity& rhs) const { return this->m_EntityID == rhs.m_EntityID; }
		inline bool operator!=(const IEntity& rhs) const { return this->m_EntityID != rhs.m_EntityID; }
		inline bool operator==(const IEntity* rhs) const { return this->m_EntityID == rhs->m_EntityID; }
		inline bool operator!=(const IEntity* rhs) const { return this->m_EntityID != rhs->m_EntityID; }

		// ACCESORS
		virtual EntityTypeId GetStaticEntityTypeID() const = 0;
		
		inline EntityId GetEntityID() const { return this->m_EntityID; }
		
		void SetActive(bool active);

		inline bool IsActive() const { return this->m_Active; }

		virtual void OnEnable() {}
		virtual void OnDisable() {}
	};	

} // namespace ECS

#endif // ECS__I_ENTITY_H__