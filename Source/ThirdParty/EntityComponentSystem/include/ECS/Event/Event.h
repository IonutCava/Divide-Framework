/*
	Author : Tobias Stein
	Date   : 6th July, 2016
	File   : Event.h

	Event class.

	All Rights Reserved. (c) Copyright 2016.
*/

#pragma once
#ifndef ECS__EVENT_H__
#define ECS__EVENT_H__

#include "Event/IEvent.h"

#include "util/FamilyTypeID.h"

namespace ECS { namespace Event {

	template<class T>
	class Event : public IEvent
	{	 
	public:
	
		// note: wont be part of stored event memory DATA
		static const EventTypeId STATIC_EVENT_TYPE_ID;
	
		Event(ECSEngine* engine, ECS::EntityId sourceEntityID)
            : IEvent(engine, sourceEntityID, STATIC_EVENT_TYPE_ID)
		{}

	}; // class Event<T>
	
	template<class T>
	const EventTypeId Event<T>::STATIC_EVENT_TYPE_ID { typeid(T).hash_code() };

}} // namespace ECS::Event

#endif // ECS__EVENT_H__
