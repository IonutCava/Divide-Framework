/*
	Author : Tobias Stein
	Date   : 9th July, 2016
	File   : EventHandler.h

	EventHandler class.

	All Rights Reserved. (c) Copyright 2016.
*/

#pragma once
#ifndef __EVENT_HANDLER_H__
#define __EVENT_HANDLER_H__


#include "API.h"

#include "Memory/Allocator/LinearAllocator.h"

#include "IEvent.h"
#include "EventDispatcher.h"

namespace ECS { namespace Event {

	class ECS_API EventHandler final : Memory::GlobalMemoryUser
	{
		// allow IEventListener access private methods for Add/Remove callbacks
		friend class ECS::ECSEngine;

		using EventDispatcherMap = Divide::hashMap<EventTypeId, Internal::IEventDispatcher*>;
	
		using EventStorage = Divide::vector<IEvent*>;
	
		using EventMemoryAllocator = Memory::Allocator::LinearAllocator;
	
		DECLARE_LOGGER


		EventHandler(const EventHandler&) = delete;
		EventHandler& operator=(EventHandler&) = delete;

		EventDispatcherMap			m_EventDispatcherMap;
		
	
		EventMemoryAllocator*		m_EventMemoryAllocator;

		// Holds a list of all sent events since last EventHandler::DispatchEvents() call
		EventStorage				m_EventStorage;
	
	
		// Add event callback
		template<class E>
		inline void AddEventCallback(Internal::IEventDelegate* const eventDelegate)
		{
			EventTypeId ETID = E::STATIC_EVENT_TYPE_ID;

			EventDispatcherMap::const_iterator iter = this->m_EventDispatcherMap.find(ETID);
			if (iter == this->m_EventDispatcherMap.end())
			{
				eastl::pair<EventTypeId, Internal::IEventDispatcher*> kvp(ETID, new Internal::EventDispatcher<E>());
	
				kvp.second->AddEventCallback(eventDelegate);
	
				this->m_EventDispatcherMap.insert(kvp);
			}
			else
			{
				this->m_EventDispatcherMap[ETID]->AddEventCallback(eventDelegate);
			}
	
		}
	
		// Remove event callback
		inline void RemoveEventCallback(Internal::IEventDelegate* eventDelegate)
		{
			const auto typeId = eventDelegate->GetStaticEventTypeId();
			const EventDispatcherMap::const_iterator iter = this->m_EventDispatcherMap.find(typeId);
			if (iter != this->m_EventDispatcherMap.end())
			{
				this->m_EventDispatcherMap[typeId]->RemoveEventCallback(eventDelegate);
			}
		}
	
	
	
	public:
	
		EventHandler();
		~EventHandler();
	
		// clear buffer, that is, simply reset index buffer
		inline void ClearEventBuffer()
		{
			this->m_EventMemoryAllocator->clear();
			this->m_EventStorage.clear();
		}
	
		inline void ClearEventDispatcher()
		{
			this->m_EventDispatcherMap.clear();
		}
	
		template<class E, class... ARGS>
		void Send(ECSEngine* engine, ARGS&&... eventArgs)
		{
			// check if type of object is trivially copyable
			static_assert(std::is_trivially_copyable<E>::value, "Event is not trivially copyable.");
	
	
			// allocate memory to store event data
			void* pMem = this->m_EventMemoryAllocator->allocate(sizeof(E), alignof(E));

			// add new event to buffer and event storage
			if (pMem != nullptr)
			{
				this->m_EventStorage.push_back(new (pMem)E(engine, FWD(eventArgs)...));

				LogInfo("\'%s\' event buffered.", typeid(E).name());
			}
			else
			{
				LogWarning("Event buffer is full! Cut off new incoming events !!!");
			}
		}

        template<class E, class... ARGS>
        void SendAndDispatchEvent(ECSEngine* engine, ARGS&&... eventArgs)
        {
            void* pMem = this->m_EventMemoryAllocator->allocate(sizeof(E), alignof(E));
            auto event = new (pMem)E(engine, FWD(eventArgs)...);
            auto it = this->m_EventDispatcherMap.find(event->GetEventTypeID());
            if (it != eastl::cend(this->m_EventDispatcherMap)) {
                it->second->Dispatch(event);
            }
        }

		// dispatches all stores events and clears buffer
		void DispatchEvents()
		{
			PROFILE_SCOPE_AUTO( Divide::Profiler::Category::GameLogic );

			size_t lastIndex = this->m_EventStorage.size();
			size_t thisIndex = 0;

			while (thisIndex < lastIndex)
			{
                IEvent* event = this->m_EventStorage[thisIndex++];
                if (event != nullptr) {
                    const auto it = this->m_EventDispatcherMap.find(event->GetEventTypeID());
                    if (it != eastl::cend(this->m_EventDispatcherMap)) {
                        it->second->Dispatch(event);
                        // update last index, after dispatch operation there could be new events
                        lastIndex = this->m_EventStorage.size();
                    }
                } else {
					LogError("Skip corrupted event [ %zu ]", event->GetEventTypeID());
                    continue;
                }
			}
			
			// reset
			ClearEventBuffer();
		}
	};

}} // namespace ECS::Event

#endif // __EVENT_HANDLER_H__ 