

#include "config.h"

#include "engineMain.h"

#include "Platform/File/Headers/FileManagement.h"
#include "Utility/Headers/Localization.h"

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


    //Win32: SetProcessDpiAwareness
    EnforceDPIScaling();

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

            Console::printfn("Engine shutdown request : {}\nDivide engine shutdown after {} engine steps and {} restart(s). Total time: {} seconds.",
                              TypeUtil::AppStepResultToString( result ),
                              stepCount,
                              restartCount,
                              std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - startTime).count());

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
