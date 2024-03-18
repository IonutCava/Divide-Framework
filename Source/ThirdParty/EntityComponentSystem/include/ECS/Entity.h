/*
	Author : Tobias Stein
	Date   : 3rd July, 2016
	File   : Entity.h
	
	Enity class.

	All Rights Reserved. (c) Copyright 2016.
*/

#pragma once
#ifndef __ENTITY_H__
#define __ENTITY_H__


#include "IEntity.h"

namespace ECS {

	///-------------------------------------------------------------------------------------------------
	/// Class:	Entity
	///
	/// Summary:	CRTP class. Any entity object should derive form the Entity class and passes itself
	/// as template parameter to the Entity class.
	///
	/// Author:	Tobias Stein
	///
	/// Date:	30/09/2017
	///
	/// Typeparams:
	/// E - 	Type of the e.
	///-------------------------------------------------------------------------------------------------

	template<class E>
	class Entity : public IEntity
	{
	#if defined(USING_MSVC)
		// Entity destruction always happens through EntityManager !!! (ToDo: This only kinda works on MSVC)
		void operator delete(void*) = delete;
		void operator delete[](void*) = delete;
	#endif

	public:

		static const EntityTypeId STATIC_ENTITY_TYPE_ID;

	public:

		const EntityTypeId GetStaticEntityTypeID() const override { return STATIC_ENTITY_TYPE_ID; }

		Entity() 
		{}

		virtual ~Entity()
		{}
	};

	// set unique type id for this Entity<T>
	template<class E>
	const EntityTypeId Entity<E>::STATIC_ENTITY_TYPE_ID = util::Internal::FamilyTypeID<IEntity>::Get<E>();
}

#endif // __ENTITY_H__