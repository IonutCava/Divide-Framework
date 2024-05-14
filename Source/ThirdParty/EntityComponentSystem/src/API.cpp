///-------------------------------------------------------------------------------------------------
/// File:	src\API.cpp.
///
/// Summary:	Implements the API.




#include "API.h"

#include "Log/LoggerManager.h"
#include "Memory/ECSMM.h"

#include "Engine.h"

namespace ECS
{
	namespace Log {

		namespace Internal {

#if !defined(ECS_DISABLE_LOGGING)

			LoggerManager*				ECSLoggerManager = new LoggerManager();

			Log::Logger* GetLogger(const char* logger)
			{
                return ECSLoggerManager != nullptr ? ECSLoggerManager->GetLogger(logger) : nullptr;
			}
#endif
		}

	} // namespace Log


	namespace Memory { 


		GlobalMemoryUser::GlobalMemoryUser()
		{
			ECS_MEMORY_MANAGER = std::make_unique<Internal::MemoryManager>();
		}

		GlobalMemoryUser::~GlobalMemoryUser()
		{
			// check for memory leaks
			ECS_MEMORY_MANAGER->CheckMemoryLeaks();
		}

		const void* GlobalMemoryUser::Allocate(size_t memSize, const char* user)
		{
			return ECS_MEMORY_MANAGER->Allocate(memSize, user);
		}

		void GlobalMemoryUser::Free(void* pMem)
		{
			ECS_MEMORY_MANAGER->Free(pMem);
		}

	} // namespace Memory

} // namespace ECS
