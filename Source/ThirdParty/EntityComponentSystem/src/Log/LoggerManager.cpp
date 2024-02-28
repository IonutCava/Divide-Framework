

///-------------------------------------------------------------------------------------------------
/// File:	src\Log\LoggerManager.cpp.
///
/// Summary:	Implements the logger manager class.


#include "Log/LoggerManager.h"
#include "Log/Logger.h"

namespace ECS { namespace Log { namespace Internal {

	LoggerManager::LoggerManager()
	{

	}

	LoggerManager::~LoggerManager()
	{
		// cleanup logger
		for (auto& it : this->m_Cache)
		{
			delete it.second;
			it.second = nullptr;
		}
	}

	Logger* LoggerManager::GetLogger(const char* name)
	{
		auto it = this->m_Cache.find(name);
		if (it == this->m_Cache.end())
		{		
			this->m_Cache[name] = new Logger();
		}

		return this->m_Cache[name];
	}

}}} // namespace ECS::Log::Internal
