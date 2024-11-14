

#include "Headers/Application.h"

#include "Headers/Kernel.h"
#include "Headers/ParamHandler.h"
#include "Headers/Configuration.h"

#include "Core/Time/Headers/ApplicationTimer.h"
#include "Platform/File/Headers/FileManagement.h"

namespace Divide {

U8 DisplayManager::s_activeDisplayCount{1u};
U8 DisplayManager::s_maxMSAASAmples{0u};
NO_DESTROY std::array<DisplayManager::OutputDisplayPropertiesContainer, DisplayManager::g_maxDisplayOutputs> DisplayManager::s_supportedDisplayModes;

Application::Application() noexcept
    : SDLEventListener("Application")
    , _mainLoopPaused{false}
    , _mainLoopActive{false}
    , _freezeRendering{false}
{
}

Application::~Application()
{
    DIVIDE_ASSERT( _kernel == nullptr );
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
        case SDL_QUIT:
        {
            RequestShutdown( false );
        } break;
        case SDL_APP_TERMINATING:
        {
            Console::errorfn( LOCALE_STR( "ERROR_APPLICATION_SYSTEM_CLOSE_REQUEST" ) );
            RequestShutdown( false );
        } break;
        case SDL_RENDER_TARGETS_RESET :
        {
            Console::warnfn( LOCALE_STR( "ERROR_APPLICATION_RENDER_TARGET_RESET" ) );
            //RequestShutdown( false );
        } break;
        case SDL_RENDER_DEVICE_RESET :
        {
            Console::errorfn( LOCALE_STR("ERROR_APPLICATION_RENDER_DEVICE_RESET") );
            RequestShutdown( false );
        } break;
        case SDL_APP_LOWMEMORY :
        {
            Console::errorfn( LOCALE_STR( "ERROR_APPLICATION_LOW_MEMORY" ) );
            RequestShutdown( false );
        } break;
        case SDL_DROPFILE:
        case SDL_DROPTEXT:
        case SDL_DROPBEGIN:
        case SDL_DROPCOMPLETE:
        {
            Console::warnfn( LOCALE_STR("WARN_APPLICATION_DRAG_DROP") );
        } break;
        case SDL_FINGERDOWN :
        case SDL_FINGERUP :
        case SDL_FINGERMOTION :
        case SDL_DOLLARGESTURE :
        case SDL_DOLLARRECORD :
        case SDL_MULTIGESTURE :
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

void DisplayManager::SetActiveDisplayCount( const U8 displayCount )
{
    s_activeDisplayCount = std::min( displayCount, g_maxDisplayOutputs );
}

void DisplayManager::RegisterDisplayMode( const U8 displayIndex, const OutputDisplayProperties& mode )
{
    DIVIDE_ASSERT( displayIndex < g_maxDisplayOutputs );
    s_supportedDisplayModes[displayIndex].push_back( mode );
}

const DisplayManager::OutputDisplayPropertiesContainer& DisplayManager::GetDisplayModes( const size_t displayIndex ) noexcept
{
    DIVIDE_ASSERT( displayIndex < g_maxDisplayOutputs );
    return s_supportedDisplayModes[displayIndex];
}

U8 DisplayManager::ActiveDisplayCount() noexcept
{
    return s_activeDisplayCount;
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
    for ( auto& entries : s_supportedDisplayModes )
    {
        entries.clear();
    }
}

}; //namespace Divide
