#include "stdafx.h"
/*
	Author : Tobias Stein
	Date   : 3rd July, 2016
	File   : Entity.cpp
	
	Entity class.

	All Rights Reserved. (c) Copyright 2016.
*/

#include "IEntity.h"
#include "ComponentManager.h"

namespace ECS
{
	std::shared_mutex IEntity::s_ComponentManagerLock;

	DEFINE_STATIC_LOGGER(IEntity, "Entity")
		
	IEntity::IEntity() :
		m_Active(true)
	{}

	IEntity::~IEntity()
	{}

	void IEntity::SetActive(bool active)
	{
		if (this->m_Active == active)
			return;

		if (active == false)
		{
			this->OnDisable();
		}
		else
		{
			this->OnEnable();
		}

		this->m_Active = active;
	}

	void IEntity::RemoveAllComponents()
	{
		std::scoped_lock<std::shared_mutex> r_lock(s_ComponentManagerLock);
		this->m_ComponentManagerInstance->RemoveAllComponents(this->m_EntityID);
	}

	void IEntity::PassDataToAllComponents(const ECS::CustomEvent& evt)
	{
		std::shared_lock<std::shared_mutex> r_lock(s_ComponentManagerLock);
		this->m_ComponentManagerInstance->PassDataToAllComponents(this->m_EntityID, evt);
	}
}