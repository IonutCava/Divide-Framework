/*
Author : Tobias Stein
Date   : 11th September, 2016
File   : Logger.h
	
Class that manages the logging.

All Rights Reserved. (c) Copyright 2016.
*/

#if !defined(ECS_DISABLE_LOGGING)

#pragma once
#ifndef __LOGGER_H__
#define __LOGGER_H__

#include "Platform.h"

#define ECS_DISABLE_INFO_LOG

namespace ECS { namespace Log {

	class ECS_API Logger
	{		
		Logger(const Logger&) = delete;
		Logger& operator=(Logger&) = delete;
					

	public:

		explicit Logger();

		~Logger();

		// trace 
		template<typename... Args>
		inline void LogTrace(const char* fmt, Args... args)
		{			
            Divide::Console::printfn(fmt, FWD(args)...);
		}

		// debug
		template<typename... Args>
		inline void LogDebug(const char* fmt, Args... args)
		{
            Divide::Console::d_printfn(fmt, std::forward<Args>(args)...);
		}

		// info
		template<typename... Args>
		inline void LogInfo([[maybe_unused]] const char* fmt, [[maybe_unused]] Args... args)
		{
        #if !defined(ECS_DISABLE_INFO_LOG)
            Divide::Console::printfn(fmt, std::forward<Args>(args)...);
        #endif
        }

		// warn
		template<typename... Args>
		inline void LogWarning(const char* fmt, Args... args)
		{
            Divide::Console::warnfn(fmt, std::forward<Args>(args)...);
		}

		// error
		template<typename... Args>
		inline void LogError(const char* fmt, Args... args)
		{
            Divide::Console::errorfn(fmt, std::forward<Args>(args)...);
		}

		// fatal
		template<typename... Args>
		inline void LogFatal(const char* fmt, Args... args)
		{
            Divide::Console::errorfn(fmt, std::forward<Args>(args)...);
            assert(false && "Fatal Error");
		}

	}; // class Logger


}} // namespace ECS::Log


#include "Log/LoggerMacro.h"


#endif // __LOGGER_H__
#endif // !ECS_DISABLE_LOGGING