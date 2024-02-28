/*
	Author : Tobias Stein
	Date   : 6th July, 2016
	File   : IEvent.h

	Base event class.

	All Rights Reserved. (c) Copyright 2016.
*/

#pragma once
#ifndef __I_EVENT_H__
#define __I_EVENT_H__

#include "API.h"
#include "util/Handle.h"

namespace ECS {
    class ECSEngine;
    namespace Event {

		using EventTypeId		= TypeID;
		using EventTimestamp	= TimeStamp;
        using EntityId          = util::Handle64;
		static const EventTypeId INVALID_EVENTTYPE = INVALID_TYPE_ID;
		

		class ECS_API IEvent
		{
		private:

			EventTypeId		m_TypeId;
			EventTimestamp	m_TimeCreated;
            EntityId        m_SourceEntityID;

		public:

			IEvent(ECSEngine* engine, EntityId sourceEntityID, EventTypeId typeId);
			 
			// ACCESSOR
			inline const EventTypeId	GetEventTypeID()	    const { return this->m_TypeId; }
			inline const EventTimestamp GetTimeCreated()	    const { return this->m_TimeCreated; }
            inline const EntityId       GetSourceEntityId( )	const { return this->m_SourceEntityID; }

		}; // class IEvent

}} // namespace ECS::Event

#endif // __I_EVENT_H__