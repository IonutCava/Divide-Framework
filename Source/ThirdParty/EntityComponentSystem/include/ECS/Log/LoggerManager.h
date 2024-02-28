///-------------------------------------------------------------------------------------------------
/// File:	include\Log\LoggerManager.h.
///
/// Summary:	Declares the logger manager class.

#if !defined(ECS_DISABLE_LOGGING)

#pragma once
#ifndef __LOGGER_MANAGER_H__
#define __LOGGER_MANAGER_H__


#include "Platform.h"


namespace ECS { namespace Log { 
	
	class Logger;

	namespace Internal {

	

	class LoggerManager
	{
		using LoggerCache = eastl::unordered_map<std::string, Logger*>;

		static constexpr const char* LOG_FILE_NAME = "ECS.log";
		static constexpr const char* DEFAULT_LOGGER = "ECS";
		static constexpr const char* LOG_PATTERN = "%d{%H:%M:%S,%q} [%t] %-5p %c{1} %x- %m%n";

		// This class is not inteeded to be initialized
		LoggerManager(const LoggerManager&) = delete;
		LoggerManager& operator=(LoggerManager&) = delete;

		/// Summary:	Holds all acquired logger
		LoggerCache m_Cache;

	public:

		LoggerManager();
		~LoggerManager();


		Logger* GetLogger(const char* logger = DEFAULT_LOGGER);

	}; // class LoggerManager

}}} // namespace ECS::Log::Internal


#endif // __LOGGER_MANAGER_H__
#endif // !ECS_DISABLE_LOGGING