
#include "EngineMain.h"
#include "EngineConfig.h"

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
        Console::errorfn(LOCALE_STR("APP_INIT_ERROR"), TypeUtil::ErrorCodeToString( errorCode ) );
    }

    return errorCode;
}

ErrorCode Engine::Run(const int argc, char** argv)
{
    ErrorCode errorCode = PlatformInit(argc, argv);
    {
        if ( errorCode != ErrorCode::NO_ERR )
        {
            if ( !PlatformClose() )
            {
                NOP();
            }

            std::cerr << TypeUtil::ErrorCodeToString(errorCode) << std::endl;
            return errorCode;
        }

        Console::Start(Paths::g_logPath, OUTPUT_LOG_FILE, ERROR_LOG_FILE, !Util::FindCommandLineArgument(argc, argv, "disableCopyright"));
        {
            errorCode = Locale::Init();
            {
                if ( errorCode != ErrorCode::NO_ERR)
                {
                    Locale::Clear();
                    Console::errorfn("Error detected during engine startup: [ {} ]", TypeUtil::ErrorCodeToString(errorCode));
                }
                else
                {
                    errorCode = RunInternal(argc, argv);
                    if (errorCode != ErrorCode::NO_ERR)
                    {
                        Console::errorfn(LOCALE_STR("ENGINE_INIT_ERROR"), TypeUtil::ErrorCodeToString(errorCode));
                    }
                }
            }
            Locale::Clear();
        }
        Console::Stop();
    }

    if (!PlatformClose())
    {
        errorCode = ErrorCode::PLATFORM_CLOSE_ERROR;
        std::cerr << TypeUtil::ErrorCodeToString(errorCode) << std::endl;
    }

    return errorCode;
}


ErrorCode Engine::RunInternal(const int argc, char** argv)
{  
    //Win32: SetProcessDpiAwareness
    EnforceDPIScaling();

    Profiler::Initialise();

    AppStepResult result = AppStepResult::COUNT;

    U64 restartCount = 0u;

    ErrorCode errorCode = ErrorCode::NO_ERR;

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
        else
        {
            result = AppStepResult::ERROR;
        }

        Profiler::Shutdown();
        app.stop(result);

        Console::printfn( LOCALE_STR("SHUTDOWN_REQUEST"),
                            TypeUtil::AppStepResultToString( result ),
                            stepCount,
                            restartCount,
                            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - startTime).count());

        ++restartCount;

    } while (errorCode == ErrorCode::NO_ERR && (result == AppStepResult::RESTART || result == AppStepResult::RESTART_CLEAR_CACHE));

    return errorCode;
}

}
