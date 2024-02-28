
///-------------------------------------------------------------------------------------------------
/// File:	src\EntityManager.cpp.
///
/// Summary:	Implements the entity manager class.
///-------------------------------------------------------------------------------------------------


#include "EntityManager.h"

namespace ECS
{
	EntityManager::EntityManager(ComponentManager* componentManagerInstance) :
		m_PendingDestroyedEntities(1024),
		m_NumPendingDestroyedEntities(0),
		m_ComponentManagerInstance(componentManagerInstance)
	{
		DEFINE_LOGGER("EntityManager")
		LogInfo("Initialize EntityManager!","")
	}

	EntityManager::~EntityManager()
	{
		for (auto ec : this->m_EntityRegistry)
		{
			LogDebug("Releasing remaining entities of type '%s' ...", ec.second->GetEntityContainerTypeName());
			delete ec.second;
			ec.second = nullptr;
		}

		LogInfo("Release EntityManager!","")
	}

	EntityId EntityManager::AqcuireEntityId(IEntity* entity)
	{
		return this->m_EntityHandleTable.AcquireHandle(entity);
	}

	void EntityManager::ReleaseEntityId(EntityId id)
	{
		this->m_EntityHandleTable.ReleaseHandle(id);
	}

    void EntityManager::RemoveDestroyedEntity(EntityId id)
    {
        IEntity* entity = this->m_EntityHandleTable[id];

        const EntityTypeId ETID = entity->GetStaticEntityTypeID();

        // get appropriate entity container and destroy entity
        auto it = this->m_EntityRegistry.find(ETID);
        if (it != this->m_EntityRegistry.end())
        {
            // release entity's components
            this->m_ComponentManagerInstance->RemoveAllComponents(id);

            it->second->DestroyEntity(entity);
        }

        // free entity id
        this->ReleaseEntityId(id);
    }

	void EntityManager::RemoveDestroyedEntities()
	{
		PROFILE_SCOPE_AUTO( Divide::Profiler::Category::GameLogic );

		for (size_t i = 0; i < this->m_NumPendingDestroyedEntities; ++i)
		{
			EntityId entityId = this->m_PendingDestroyedEntities[i];
            RemoveDestroyedEntity(entityId);
		}

		this->m_NumPendingDestroyedEntities = 0;
	}

} // namespace ECS