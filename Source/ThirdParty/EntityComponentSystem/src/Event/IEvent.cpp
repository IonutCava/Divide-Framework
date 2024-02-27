
///-------------------------------------------------------------------------------------------------
/// File:	src\Event\IEvent.cpp.
///
/// Summary:	Declares the IEvent interface.
///-------------------------------------------------------------------------------------------------


#include "Event/IEvent.h"

#include "Engine.h"
#include "util/Timer.h"

namespace ECS { namespace Event {

	IEvent::IEvent(ECSEngine* engine, EntityId sourceEntityID, EventTypeId typeId) :
		m_TypeId(typeId),
        m_SourceEntityID(sourceEntityID)
	{
        assert(engine != nullptr && "ECS engine not initialized!");
		this->m_TimeCreated = engine->ECS_EngineTime->GetTimeStamp();
	}

}} // namespace ECS::Event
