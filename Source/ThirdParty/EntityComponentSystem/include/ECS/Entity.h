/*
	Author : Tobias Stein
	Date   : 3rd July, 2016
	File   : Entity.h
	
	Enity class.

	All Rights Reserved. (c) Copyright 2016.
*/

#pragma once
#ifndef ECS__ENTITY_H__
#define ECS__ENTITY_H__


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
	public:

		static const EntityTypeId STATIC_ENTITY_TYPE_ID;

	public:

		EntityTypeId GetStaticEntityTypeID() const override { return STATIC_ENTITY_TYPE_ID; }

		Entity() 
		{}

		virtual ~Entity()
		{}
	};

	// set unique type id for this Entity<T>
	template<class E>
	const EntityTypeId Entity<E>::STATIC_ENTITY_TYPE_ID = util::Internal::FamilyTypeID<IEntity>::Get<E>();
}

#endif // ECS__ENTITY_H__
