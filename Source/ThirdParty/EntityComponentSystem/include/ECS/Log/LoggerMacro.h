///-------------------------------------------------------------------------------------------------
/// File:	include\Log\LoggerMacro.h.
///
/// Summary:	Declares some macros to simply logging.
///-------------------------------------------------------------------------------------------------


#pragma once
#ifndef ECS__LOGGER_MACRO_H__
#define ECS__LOGGER_MACRO_H__

//#define ECS_DISABLE_LOGGING

#if !defined(ECS_DISABLE_LOGGING)
	#define DECLARE_LOGGER									Log::Logger* LOGGER;
	#define DECLARE_STATIC_LOGGER							static Log::Logger* LOGGER;

	#define DEFINE_LOGGER(name)								LOGGER = ECS::Log::Internal::GetLogger(name);
	#define DEFINE_STATIC_LOGGER(clazz, name)				Log::Logger* clazz::LOGGER = ECS::Log::Internal::GetLogger(name);
	#define DEFINE_STATIC_LOGGER_TEMPLATE(clazz, T, name)	template<class T> Log::Logger* clazz<T>::LOGGER = ECS::Log::Internal::GetLogger(name);


	#define LOG_TRACE(format, ...)							LOGGER->LogTrace(format, ##__VA_ARGS__);
	#define LOG_DEBUG(format, ...)							LOGGER->LogDebug(format, ##__VA_ARGS__);
	#define LOG_INFO(format, ...)							LOGGER->LogInfo(format, ##__VA_ARGS__);
	#define LOG_WARNING(format, ...)						LOGGER->LogWarning(format, ##__VA_ARGS__);
	#define LOG_ERROR(format, ...)							LOGGER->LogError(format, ##__VA_ARGS__);
	#define LOG_FATAL(format, ...)							LOGGER->LogFatal(format, ##__VA_ARGS__);
#else

	#define DECLARE_LOGGER
	#define DECLARE_STATIC_LOGGER

	#define DEFINE_LOGGER(name)							
	#define DEFINE_STATIC_LOGGER(class, name)			
	#define DEFINE_STATIC_LOGGER_TEMPLATE(class, T, name)

	#define LOG_TRACE(format, ...)	
	#define LOG_DEBUG(format, ...)	
	#define LOG_INFO(format, ...)	
	#define LOG_WARNING(format, ...) 
	#define LOG_ERROR(format, ...)	
	#define LOG_FATAL(format, ...)	
#endif

#endif // ECS__LOGGER_MACRO_H__