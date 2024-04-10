

#include "config.h"

#include "engineMain.h"

#include "Platform/File/Headers/FileManagement.h"
#include "Utility/Headers/Localization.h"

#include <iostream>

namespace Divide {

ErrorCode Engine::init(const int argc, char** argv)
{
    _app = std::make_unique<Application>();
    Profiler::RegisterApp(_app.get());

    // Start our application based on XML configuration.
    // If it fails to start, it should automatically clear up all of its data
    const ErrorCode errorCode = _app->start("config.xml", argc, argv);

    if ( errorCode != ErrorCode::NO_ERR)
    {
        // If any error occurred, close the application as details should already be logged
        Console::errorfn(LOCALE_STR("GENERIC_ERROR"), TypeUtil::ErrorCodeToString( errorCode ) );
    }

    return errorCode;
}

ErrorCode Engine::run(const int argc, char** argv)
{
    ErrorCode errorCode = PlatformInit( argc, argv );
    if ( errorCode != ErrorCode::NO_ERR)
    {
        return errorCode;
    }

    if ( Paths::g_logPath.empty() )
    {
        return ErrorCode::PATHS_ERROR;
    }

    const std::ofstream outputStreamsCOUT{ (Paths::g_logPath / OUTPUT_LOG_FILE).string(), std::ofstream::out | std::ofstream::trunc };
    const std::ofstream outputStreamsCERR{ (Paths::g_logPath / ERROR_LOG_FILE).string(), std::ofstream::out | std::ofstream::trunc };

    std::cout.rdbuf( outputStreamsCOUT.rdbuf());
    std::cerr.rdbuf( outputStreamsCERR.rdbuf());


    //Win32: SetProcessDpiAwareness
    EnforceDPIScaling();

    // Print a copyright notice in the log file
    if ( !Util::FindCommandLineArgument( argc, argv, "disableCopyright" ) )
    {
        Console::PrintCopyrightNotice();
    }

    // Read language table
    errorCode = Locale::Init();
    if ( errorCode == ErrorCode::NO_ERR )
    {
        Profiler::Initialise();

        AppStepResult result = AppStepResult::COUNT;

        U64 restartCount = 0u;
        do
        {
            U64 stepCount = 0u;

            const auto startTime = std::chrono::high_resolution_clock::now();

            // Start the engine
            errorCode = init( argc, argv );
            if (errorCode == ErrorCode::NO_ERR)
            {
                // Step the entire application
                while ( (result = _app->step()) == AppStepResult::OK )
                {
                    ++stepCount;
                }
            }

            Profiler::Shutdown();
            _app->stop(result);
            _app.reset();

            const auto endTime = std::chrono::high_resolution_clock::now();

            std::cout << "Engine shutdown request: " << TypeUtil::AppStepResultToString(result) << std::endl;

            std::cout << "Divide engine shutdown after "
                      << stepCount
                      << " engine steps and " 
                      << restartCount
                      << " restart(s). "
                      << "Total time: "
                      << std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count()
                      << " seconds."
                      << std::endl;

            ++restartCount;

        } while (errorCode == ErrorCode::NO_ERR && (result == AppStepResult::RESTART || result == AppStepResult::RESTART_CLEAR_CACHE));

        Locale::Clear();
    }

    if ( !PlatformClose() )
    {
        errorCode = ErrorCode::PLATFORM_CLOSE_ERROR;
    }

    return errorCode;
}

}
