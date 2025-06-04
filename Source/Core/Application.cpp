

#include "Headers/Application.h"
#include "Headers/DisplayManager.h"

#include "Headers/Kernel.h"
#include "Headers/Configuration.h"

#include "Core/Time/Headers/ApplicationTimer.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Utility/Headers/Localization.h"

namespace Divide {

Application::Application() noexcept
    : SDLEventListener("Application")
    , _mainLoopPaused{false}
    , _mainLoopActive{false}
    , _freezeRendering{false}
{
}

ErrorCode Application::start(const string& entryPoint, const I32 argc, char** argv)
{
    assert( !entryPoint.empty() );
    assert( _kernel == nullptr );

    Console::ToggleFlag(Console::Flags::PRINT_IMMEDIATE, true);
    Console::printfn(LOCALE_STR("START_APPLICATION"));
    Console::printfn(LOCALE_STR("START_APPLICATION_CMD_ARGUMENTS"));
    if ( argc > 1 )
    {
        for (I32 i = 1; i < argc; ++i)
        {
            Console::printfn("{}", argv[i]);
        }
    }
    else
    {
        Console::printfn(LOCALE_STR("START_APPLICATION_CMD_ARGUMENTS_NONE" ));
    }

    // Create a new kernel
    _kernel = std::make_unique<Kernel>(argc, argv, *this);

    _timer.reset();

    // and load it via an XML file config
    const ErrorCode err = Attorney::KernelApplication::initialize(_kernel.get(), entryPoint);

    // failed to start, so cleanup
    if (err != ErrorCode::NO_ERR)
    {
        stop( AppStepResult::ERROR );
    }
    else
    {
        Attorney::KernelApplication::warmup(_kernel.get() );
        Console::printfn(LOCALE_STR("START_MAIN_LOOP"));
        Console::ToggleFlag( Console::Flags::PRINT_IMMEDIATE, false);
        mainLoopActive(true);
    }

    return err;
}

bool Application::onProfilerStateChanged( const Profiler::State state )
{
    if ( state == Profiler::State::COUNT ) [[unlikely]]
    {
        return false;
    }
    
    PlatformContext& context = _kernel->platformContext();
    static bool assertOnAPIError = context.config().debug.renderer.assertOnRenderAPIError;
    static bool apiDebugging = context.config().debug.renderer.enableRenderAPIDebugging;

    switch ( state )
    {
        case Profiler::State::STARTED:
        {
            context.config().debug.renderer.assertOnRenderAPIError = false;
            context.config().debug.renderer.enableRenderAPIDebugging = false;
        } break;
        case Profiler::State::STOPPED:
        {
            context.config().debug.renderer.assertOnRenderAPIError = assertOnAPIError;
            context.config().debug.renderer.enableRenderAPIDebugging = apiDebugging;
        } break;

        default:
        case Profiler::State::COUNT: break;
    }


    return true;
}

ErrorCode Application::setRenderingAPI( const RenderAPI api )
{
    PlatformContext& context = _kernel->platformContext();
    const Configuration& config = context.config();

    return _windowManager.init( context,
                                api,
                                {-1, -1},
                                config.runtime.windowSize,
                                static_cast<WindowMode>(config.runtime.windowedMode),
                                config.runtime.targetDisplay );
}

void Application::stop( const AppStepResult stepResult )
{
    Console::printfn( LOCALE_STR( "STOP_APPLICATION" ) );

    if ( _kernel == nullptr )
    {
        return;
    }

    Attorney::KernelApplication::shutdown(_kernel.get() );

    _windowManager.close();
    _kernel.reset();
    Attorney::DisplayManagerApplication::Reset();

    if ( stepResult == AppStepResult::RESTART_CLEAR_CACHE || stepResult == AppStepResult::STOP_CLEAR_CACHE )
    {
        if ( !deleteAllFiles( Paths::g_cacheLocation, nullptr, "keep") )
        {
            NOP();
        }
    }
}

AppStepResult Application::step()
{
    AppStepResult result = AppStepResult::OK;

    if ( mainLoopActive() )
    {
        PROFILE_FRAME( "Main Thread" );
        Attorney::KernelApplication::onLoop(_kernel.get() );
    }
    else
    {
        if ( RestartRequested() )
        {
            result = _clearCacheOnExit ? AppStepResult::RESTART_CLEAR_CACHE : AppStepResult::RESTART;
        }
        else if ( ShutdownRequested() )
        {
            result = _clearCacheOnExit ? AppStepResult::STOP_CLEAR_CACHE : AppStepResult::STOP;
        }
        else
        {
            result = AppStepResult::ERROR;
        }

        _clearCacheOnExit = false;

        CancelRestart();
        CancelShutdown();
        windowManager().hideAll();
    }

    return result;
}

bool Application::onSDLEvent(const SDL_Event event) noexcept
{
    switch ( event.type )
    {
        case SDL_EVENT_QUIT:
        {
            RequestShutdown( false );
        } break;
        case SDL_EVENT_TERMINATING:
        {
            Console::errorfn( LOCALE_STR( "ERROR_APPLICATION_SYSTEM_CLOSE_REQUEST" ) );
            RequestShutdown( false );
        } break;
        case SDL_EVENT_RENDER_TARGETS_RESET:
        {
            Console::warnfn( LOCALE_STR( "ERROR_APPLICATION_RENDER_TARGET_RESET" ) );
            //RequestShutdown( false );
        } break;
        case SDL_EVENT_RENDER_DEVICE_RESET:
        {
            Console::errorfn( LOCALE_STR("ERROR_APPLICATION_RENDER_DEVICE_RESET") );
            RequestShutdown( false );
        } break;
        case SDL_EVENT_LOW_MEMORY:
        {
            Console::errorfn( LOCALE_STR( "ERROR_APPLICATION_LOW_MEMORY" ) );
            RequestShutdown( false );
        } break;
        case SDL_EVENT_DROP_FILE:
        case SDL_EVENT_DROP_TEXT:
        case SDL_EVENT_DROP_BEGIN:
        case SDL_EVENT_DROP_COMPLETE:
        {
            Console::warnfn( LOCALE_STR("WARN_APPLICATION_DRAG_DROP") );
        } break;
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_MOTION:
        {
            Console::warnfn( LOCALE_STR( "WARN_APPLICATION_TOUCH_EVENT" ) );
        } break;
        default: break;
    }

    return ShutdownRequested();
}

bool Application::onWindowSizeChange(const SizeChangeParams& params) const
{
    return Attorney::KernelApplication::onWindowSizeChange(_kernel.get(), params);
}

bool Application::onResolutionChange(const SizeChangeParams& params) const
{
    return Attorney::KernelApplication::onResolutionChange(_kernel.get(), params);
}

void DisplayManager::RegisterDisplayMode( const OutputDisplayProperties& mode )
{
    OutputDisplayProperties& it = s_supportedDisplayModes.emplace_back(mode);
    Util::ReplaceStringInPlace(it._formatName, "SDL_PIXELFORMAT_", "");
}

const DisplayManager::OutputDisplayPropertiesContainer& DisplayManager::GetDisplayModes() noexcept
{
    return s_supportedDisplayModes;
}

U8 DisplayManager::MaxMSAASamples() noexcept
{
    return s_maxMSAASAmples;
}

void DisplayManager::MaxMSAASamples( const U8 maxSampleCount ) noexcept
{
    s_maxMSAASAmples = std::min( maxSampleCount, to_U8( 64u ) );
}

void DisplayManager::Reset() noexcept
{
    s_supportedDisplayModes.clear();
}

}; //namespace Divide
