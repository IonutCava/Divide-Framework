

#include "config.h"

#include "engineMain.h"

#include "Platform/File/Headers/FileManagement.h"
#include "Utility/Headers/Localization.h"
#include "Core/Headers/Application.h"

namespace Divide {

ErrorCode Engine::Init(Application& app, const int argc, char** argv)
{
    Profiler::RegisterApp(&app);

    // Start our application based on XML configuration.
    // If it fails to start, it should automatically clear up all of its data
    const ErrorCode errorCode = app.start("config.xml", argc, argv);

    if ( errorCode != ErrorCode::NO_ERR)
    {
        // If any error occurred, close the application as details should already be logged
        Console::errorfn(LOCALE_STR("GENERIC_ERROR"), TypeUtil::ErrorCodeToString( errorCode ) );
    }

    return errorCode;
}

ErrorCode Engine::Run(const int argc, char** argv)
{
    const ErrorCode ret = RunInternal(argc, argv);
    if (ret != ErrorCode::NO_ERR)
    {
        Console::errorfn(LOCALE_STR("GENERIC_ERROR"), TypeUtil::ErrorCodeToString(ret));
    }

    return ret;
}

ErrorCode Engine::RunInternal(const int argc, char** argv)
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
            Application app;
            errorCode = Init( app, argc, argv );
            if (errorCode == ErrorCode::NO_ERR)
            {
                // Step the entire application
                while ( (result = app.step()) == AppStepResult::OK )
                {
                    ++stepCount;
                }
            }

            Profiler::Shutdown();
            app.stop(result);

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
