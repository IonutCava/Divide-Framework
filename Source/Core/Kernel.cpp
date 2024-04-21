

#include "config.h"

#include "Headers/Kernel.h"
#include "Headers/Configuration.h"
#include "Headers/EngineTaskPool.h"
#include "Headers/PlatformContext.h"

#include "Core/Debugging/Headers/DebugInterface.h"
#include "Core/Headers/ParamHandler.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Networking/Headers/LocalClient.h"
#include "Core/Networking/Headers/Server.h"
#include "Core/Time/Headers/ApplicationTimer.h"
#include "Core/Time/Headers/ProfileTimer.h"
#include "Editor/Headers/Editor.h"
#include "GUI/Headers/GUI.h"
#include "GUI/Headers/GUIConsole.h"
#include "GUI/Headers/GUISplash.h"
#include "Managers/Headers/FrameListenerManager.h"
#include "Managers/Headers/RenderPassManager.h"
#include "Managers/Headers/ProjectManager.h"
#include "Scenes/Headers/SceneEnvironmentProbePool.h"
#include "Physics/Headers/PXDevice.h"
#include "Platform/Audio/Headers/SFXDevice.h"
#include "Platform/File/Headers/FileWatcherManager.h"
#include "Platform/Headers/SDLEventManager.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "Resources/Headers/ResourceCache.h"
#include "Scripting/Headers/Script.h"
#include "Utility/Headers/XMLParser.h"

namespace Divide
{

namespace
{
    constexpr U8 g_minimumGeneralPurposeThreadCount = 8u;
    constexpr U8 g_backupThreadPoolSize = 2u;

    static_assert(g_backupThreadPoolSize < g_minimumGeneralPurposeThreadCount);

    constexpr U32 g_printTimerBase = 15u;
    constexpr U8  g_warmupFrameCount = 8u;
    U32 g_printTimer = g_printTimerBase;
};

Kernel::Kernel(const I32 argc, char** argv, Application& parentApp)
    : _platformContext(parentApp)
    , _appLoopTimerMain(Time::ADD_TIMER("Main Loop Timer"))
    , _appLoopTimerInternal(Time::ADD_TIMER("Internal Main Loop Timer"))
    , _frameTimer(Time::ADD_TIMER("Total Frame Timer"))
    , _appIdleTimer(Time::ADD_TIMER("Loop Idle Timer"))
    , _appScenePass(Time::ADD_TIMER("Loop Scene Pass Timer"))
    , _sceneUpdateTimer(Time::ADD_TIMER("Scene Update Timer"))
    , _sceneUpdateLoopTimer(Time::ADD_TIMER("Scene Update Loop timer"))
    , _cameraMgrTimer(Time::ADD_TIMER("Camera Manager Update Timer"))
    , _flushToScreenTimer(Time::ADD_TIMER("Flush To Screen Timer"))
    , _preRenderTimer(Time::ADD_TIMER("Pre-render Timer"))
    , _postRenderTimer(Time::ADD_TIMER("Post-render Timer"))
    , _splashScreenUpdating( false )
    , _argc(argc)
    , _argv(argv)
{
    _platformContext.init(*this);
    InitConditionalWait(_platformContext);

    _appLoopTimerMain.addChildTimer(_appLoopTimerInternal);
    _appLoopTimerInternal.addChildTimer(_appIdleTimer);
    _appLoopTimerInternal.addChildTimer(_frameTimer);
    _frameTimer.addChildTimer(_appScenePass);
    _appScenePass.addChildTimer(_cameraMgrTimer);
    _appScenePass.addChildTimer(_sceneUpdateTimer);
    _appScenePass.addChildTimer(_flushToScreenTimer);
    _flushToScreenTimer.addChildTimer(_preRenderTimer);
    _flushToScreenTimer.addChildTimer(_postRenderTimer);
    _sceneUpdateTimer.addChildTimer(_sceneUpdateLoopTimer);

    _projectManager = MemoryManager_NEW ProjectManager(*this); // Scene Manager
    _resourceCache = MemoryManager_NEW ResourceCache(_platformContext);
    _renderPassManager = MemoryManager_NEW RenderPassManager(*this, _platformContext.gfx());
}

Kernel::~Kernel()
{
    DIVIDE_ASSERT(projectManager() == nullptr && resourceCache() == nullptr && renderPassManager() == nullptr,
                  "Kernel destructor: not all resources have been released properly!");
}

void Kernel::startSplashScreen() {
    bool expected = false;
    if (!_splashScreenUpdating.compare_exchange_strong(expected, true)) {
        return;
    }

    DisplayWindow& window = _platformContext.mainWindow();
    window.changeType(WindowType::WINDOW);
    window.decorated(false);
    WAIT_FOR_CONDITION(window.setDimensions(_platformContext.config().runtime.splashScreenSize));

    window.centerWindowPosition();
    window.hidden(false);
    SDLEventManager::pollEvents();

    GUISplash splash(resourceCache(), "divideLogo.jpg", _platformContext.config().runtime.splashScreenSize);

    // Load and render the splash screen
    _splashTask = CreateTask(
        [this, &splash, &window](const Task& /*task*/) {
        U64 previousTimeUS = 0;
        while (_splashScreenUpdating)
        {
            const U64 deltaTimeUS = Time::App::ElapsedMicroseconds() - previousTimeUS;
            previousTimeUS += deltaTimeUS;

            _platformContext.app().windowManager().drawToWindow(window);
            splash.render(_platformContext.gfx());
            _platformContext.app().windowManager().flushWindow();
            std::this_thread::sleep_for(std::chrono::milliseconds(15));

            break;
        }
    });
    Start(*_splashTask, _platformContext.taskPool(TaskPoolType::HIGH_PRIORITY), TaskPriority::REALTIME/*HIGH*/);
}

void Kernel::stopSplashScreen() {
    DisplayWindow& window = _platformContext.mainWindow();
    const vec2<U16> previousDimensions = window.getPreviousDimensions();
    _splashScreenUpdating = false;
    Wait(*_splashTask, _platformContext.taskPool(TaskPoolType::HIGH_PRIORITY));

    window.changeToPreviousType();
    window.decorated(true);
    WAIT_FOR_CONDITION(window.setDimensions(previousDimensions));
    window.setPosition(vec2<I32>(-1));
    if (window.type() == WindowType::WINDOW && _platformContext.config().runtime.maximizeOnStart) {
        window.maximized(true);
    }
    SDLEventManager::pollEvents();
}

void Kernel::idle(const bool fast, const U64 deltaTimeUSGame, const U64 deltaTimeUSApp )
{
    PROFILE_SCOPE_AUTO( Profiler::Category::IO );

    if constexpr(!Config::Build::IS_SHIPPING_BUILD)
    {
        Locale::Idle();
    }

    _platformContext.idle(fast, deltaTimeUSGame, deltaTimeUSApp );

    _projectManager->idle();
    Script::idle();

    if (!fast && --g_printTimer == 0) {
        Console::Flush();
        g_printTimer = g_printTimerBase;
    }

    if constexpr(Config::Build::ENABLE_EDITOR) {
        const bool freezeLoopTime = _platformContext.editor().simulationPaused() && _platformContext.editor().stepQueue() == 0u;
        _timingData.freezeGameTime(freezeLoopTime);
        _platformContext.app().mainLoopPaused(freezeLoopTime);

    }
}

void Kernel::onLoop()
{
    PROFILE_SCOPE_AUTO( Profiler::Category::IO );

    {
        Time::ScopedTimer timer(_appLoopTimerMain);

        if (!keepAlive()) {
            // exiting the rendering loop will return us to the last control point
            _platformContext.app().mainLoopActive(false);

            if (!projectManager()->saveActiveScene(true, false))
            {
                DIVIDE_UNEXPECTED_CALL();
            }
            return;
        }

        if constexpr(!Config::Build::IS_SHIPPING_BUILD) {
            // Check for any file changes (shaders, scripts, etc)
            FileWatcherManager::update();
        }

        // Update internal timer
        _platformContext.app().timer().update();

        keepAlive(true);

        // Update time at every render loop
        _timingData.update( Time::App::ElapsedMicroseconds(), FIXED_UPDATE_RATE_US );

        FrameEvent evt = {};
        evt._time._app._currentTimeUS = _timingData.appCurrentTimeUS();
        evt._time._app._deltaTimeUS = _timingData.appTimeDeltaUS();

        evt._time._game._currentTimeUS += _timingData.gameCurrentTimeUS();
        evt._time._game._deltaTimeUS = _timingData.gameTimeDeltaUS();

        {
            _platformContext.componentMask( to_base( PlatformContext::SystemComponentType::ALL ) );
            {
                Time::ScopedTimer timer3(_frameTimer);

                // Launch the FRAME_STARTED event
                if (!frameListenerMgr().createAndProcessEvent(FrameEventType::FRAME_EVENT_STARTED, evt))
                {
                    keepAlive(false);
                }

                // Process the current frame
                if (!mainLoopScene(evt))
                {
                    keepAlive(false);
                }

                // Launch the FRAME_PROCESS event (a.k.a. the frame processing has ended event)
                if (!frameListenerMgr().createAndProcessEvent(FrameEventType::FRAME_EVENT_PROCESS, evt))
                {
                    keepAlive(false);
                }
            }

            if (!frameListenerMgr().createAndProcessEvent(FrameEventType::FRAME_EVENT_ENDED, evt))
            {
                keepAlive(false);
            }

            if (_platformContext.app().RestartRequested() || _platformContext.app().ShutdownRequested() )
            {
                keepAlive(false);
            }

            const ErrorCode err = _platformContext.app().errorCode();

            if (err != ErrorCode::NO_ERR)
            {
                Console::errorfn(LOCALE_STR("GENERIC_ERROR"), TypeUtil::ErrorCodeToString(err));
                keepAlive(false);
            }
        }

        if (platformContext().debug().enabled())
        {
            static bool statsEnabled = false;
            // Turn on perf metric measuring 2 seconds before perf dump
            if (GFXDevice::FrameCount() % (Config::TARGET_FRAME_RATE * Time::Seconds(8)) == 0)
            {
                statsEnabled = platformContext().gfx().queryPerformanceStats();
                platformContext().gfx().queryPerformanceStats(true);
            }
            // Our stats should be up to date now
            if (GFXDevice::FrameCount() % (Config::TARGET_FRAME_RATE * Time::Seconds(10)) == 0)
            {
                Console::printfn(platformContext().debug().output().c_str());
                if (!statsEnabled)
                {
                    platformContext().gfx().queryPerformanceStats(false);
                }
            }

            if (GFXDevice::FrameCount() % (Config::TARGET_FRAME_RATE / 8) == 0u)
            {
                _platformContext.gui().modifyText("ProfileData", platformContext().debug().output(), true);
            }
        }

        if constexpr(!Config::Build::IS_SHIPPING_BUILD)
        {
            if (GFXDevice::FrameCount() % (Config::TARGET_FRAME_RATE / 8) == 0u)
            {
                DisplayWindow& window = _platformContext.mainWindow();
                NO_DESTROY static string originalTitle = window.title();

                F32 fps = 0.f, frameTime = 0.f;
                _platformContext.app().timer().getFrameRateAndTime(fps, frameTime);
                const Str<256>& activeSceneName = _projectManager->activeProject()->getActiveScene()->resourceName();
                constexpr const char* buildType = Config::Build::IS_DEBUG_BUILD ? "DEBUG" : Config::Build::IS_PROFILE_BUILD ? "PROFILE" : "RELEASE";
                constexpr const char* titleString = "[{} - {}] - {} - {} - {:5.2f} FPS - {:3.2f} ms - FrameIndex: {} - Update Calls : {} - Alpha : {:1.2f}";
                window.title(titleString,
                             buildType,
                             Names::renderAPI[to_base(_platformContext.gfx().renderAPI())],
                             originalTitle.c_str(),
                             activeSceneName.c_str(),
                             fps,
                             frameTime,
                             GFXDevice::FrameCount(),
                             _timingData.updateLoops(),
                             _timingData.alpha());
            }
        }

        {
            Time::ScopedTimer timer2(_appIdleTimer);
            idle(false, evt._time._game._deltaTimeUS, evt._time._app._deltaTimeUS );
        }
    }

    // Cap FPS
    const I16 frameLimit = _platformContext.config().runtime.frameRateLimit;
    if (frameLimit > 0)
    {
        const F32 elapsedMS = Time::MicrosecondsToMilliseconds<F32>(_appLoopTimerMain.get());
        const F32 deltaMilliseconds = std::floorf(elapsedMS - (elapsedMS * 0.015f));
        const F32 targetFrameTime = 1000.0f / frameLimit;

        if (deltaMilliseconds < targetFrameTime)
        {
            //Sleep the remaining frame time 
            SetThreadPriority(ThreadPriority::ABOVE_NORMAL);
            std::this_thread::sleep_for(std::chrono::milliseconds(to_I32(targetFrameTime - deltaMilliseconds)));
            SetThreadPriority(ThreadPriority::NORMAL);
        }
    }
}

bool Kernel::mainLoopScene(FrameEvent& evt)
{
    PROFILE_SCOPE_AUTO( Profiler::Category::IO );

    Time::ScopedTimer timer(_appScenePass);
    {
        Time::ScopedTimer timer2(_cameraMgrTimer);
        // Update cameras. Always use app timing as pausing time would freeze the cameras in place
        // ToDo: add a speed slider in the editor -Ionut
        Camera::Update( evt._time._app._deltaTimeUS );
    }

    if (_platformContext.mainWindow().minimized())
    {
        idle(false, 0u, evt._time._app._deltaTimeUS );
        SDLEventManager::pollEvents();
        return true;
    }

    if (!_platformContext.app().freezeRendering())
    {
        {
            Time::ScopedTimer timer2(_sceneUpdateTimer);

            const U8 playerCount = _projectManager->activePlayerCount();

            _timingData.updateLoops(0u);

            constexpr U8 MAX_FRAME_SKIP = 4u;

            {
                PROFILE_SCOPE("GUI Update", Profiler::Category::IO );
                _projectManager->activeProject()->getActiveScene()->processGUI( evt._time._game._deltaTimeUS, evt._time._app._deltaTimeUS );
            }

            while (_timingData.accumulator() >= FIXED_UPDATE_RATE_US)
            {
                PROFILE_SCOPE("Run Update Loop", Profiler::Category::IO);
                // Everything inside here should use fixed timesteps, apart from GFX updates which should use both!
                // Some things (e.g. tonemapping) need to resolve even if the simulation is paused (might not remain true in the future)

                if (_timingData.updateLoops() == 0u)
                {
                    _sceneUpdateLoopTimer.start();
                }


                // Flush any pending threaded callbacks
                for (U8 i = 0u; i < to_U8(TaskPoolType::COUNT); ++i)
                {
                    _platformContext.taskPool(static_cast<TaskPoolType>(i)).flushCallbackQueue();
                }

                // Update scene based on input
                {
                    PROFILE_SCOPE("Process input", Profiler::Category::IO );
                    for (U8 i = 0u; i < playerCount; ++i) {
                        PROFILE_TAG("Player index", i);
                        _projectManager->activeProject()->getActiveScene()->processInput(i, evt._time._game._deltaTimeUS, evt._time._app._deltaTimeUS );
                    }
                }

                // process all scene events
                {
                    PROFILE_SCOPE("Process scene events", Profiler::Category::IO );
                    _projectManager->activeProject()->getActiveScene()->processTasks( evt._time._game._deltaTimeUS, evt._time._app._deltaTimeUS );
                }

                // Update the scene state based on current time (e.g. animation matrices)
                _projectManager->updateSceneState( evt._time._game._deltaTimeUS, evt._time._app._deltaTimeUS );
                // Update visual effect timers as well
                Attorney::GFXDeviceKernel::update(_platformContext.gfx(), evt._time._game._deltaTimeUS, evt._time._app._deltaTimeUS );

                _timingData.updateLoops(_timingData.updateLoops() + 1u);
                _timingData.accumulator(_timingData.accumulator() - FIXED_UPDATE_RATE_US);

                const U8 loopCount = _timingData.updateLoops();
                if (loopCount == 1u)
                {
                    _sceneUpdateLoopTimer.stop();
                }
                else if (loopCount == MAX_FRAME_SKIP)
                {
                    _timingData.accumulator(FIXED_UPDATE_RATE_US);
                    break;
                }
            }
        }
    }

    if (GFXDevice::FrameCount() % (Config::TARGET_FRAME_RATE / Config::Networking::NETWORK_SEND_FREQUENCY_HZ) == 0)
    {
        U32 retryCount = 0;
        while (!Attorney::ProjectManagerKernel::networkUpdate(_projectManager, GFXDevice::FrameCount()))
        {
            if (retryCount++ > Config::Networking::NETWORK_SEND_RETRY_COUNT)
            {
                break;
            }
        }
    }

    GFXDevice::FrameInterpolationFactor(_timingData.alpha());
    
    // Update windows and get input events
    SDLEventManager::pollEvents();

    // Update the graphical user interface
    _platformContext.gui().update( evt._time._game._deltaTimeUS );

    if constexpr(Config::Build::ENABLE_EDITOR)
    {
        _platformContext.editor().update( evt._time._app._deltaTimeUS );
    }

    return presentToScreen(evt);
}

static void ComputeViewports(const Rect<I32>& mainViewport, vector<Rect<I32>>& targetViewports, const U8 count) {
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    const I32 xOffset = mainViewport.x;
    const I32 yOffset = mainViewport.y;
    const I32 width   = mainViewport.z;
    const I32 height  = mainViewport.w;

    targetViewports.resize(count);
    if (count == 0) {
        return;
    } else if (count == 1) { //Single Player
        targetViewports[0].set(mainViewport);
        return;
    } else if (count == 2) { //Split Screen
        const I32 halfHeight = height / 2;
        targetViewports[0].set(xOffset, halfHeight + yOffset, width, halfHeight);
        targetViewports[1].set(xOffset, 0 + yOffset,          width, halfHeight);
        return;
    }

    // Basic idea (X - viewport):
    // Odd # of players | Even # of players
    // X X              |      X X
    //  X               |      X X
    //                  |
    // X X X            |     X X X 
    //  X X             |     X X X
    // etc
    // Always try to match last row with previous one
    // If they match, move the first viewport from the last row
    // to the previous row and add a new entry to the end of the
    // current row

    using ViewportRow = vector<Rect<I32>>;
    using ViewportRows = vector<ViewportRow>;
    ViewportRows rows;

    // Allocates storage for a N x N matrix of viewports that will hold numViewports
    // Returns N;
    const auto resizeViewportContainer = [&rows](const U32 numViewports) {
        //Try to fit all viewports into an appropriately sized matrix.
        //If the number of resulting rows is too large, drop empty rows.
        //If the last row has an odd number of elements, center them later.
        const U8 matrixSize = to_U8(minSquareMatrixSize(numViewports));
        rows.resize(matrixSize);
        std::for_each(std::begin(rows), std::end(rows), [matrixSize](ViewportRow& row) { row.resize(matrixSize); });

        return matrixSize;
    };

    // Remove extra rows and columns, if any
    const U8 columnCount = resizeViewportContainer(count);
    const U8 extraColumns = columnCount * columnCount - count;
    const U8 extraRows = extraColumns / columnCount;
    for (U8 i = 0u; i < extraRows; ++i) {
        rows.pop_back();
    }
    const U8 columnsToRemove = extraColumns - extraRows * columnCount;
    for (U8 i = 0u; i < columnsToRemove; ++i) {
        rows.back().pop_back();
    }

    const U8 rowCount = to_U8(rows.size());

    // Calculate and set viewport dimensions
    // The number of columns is valid for the width;
    const I32 playerWidth = width / columnCount;
    // The number of rows is valid for the height;
    const I32 playerHeight = height / to_I32(rowCount);

    for (U8 i = 0u; i < rowCount; ++i) {
        ViewportRow& row = rows[i];
        const I32 playerYOffset = playerHeight * (rowCount - i - 1);
        for (U8 j = 0; j < to_U8(row.size()); ++j) {
            const I32 playerXOffset = playerWidth * j;
            row[j].set(playerXOffset, playerYOffset, playerWidth, playerHeight);
        }
    }

    //Slide the last row to center it
    if (extraColumns > 0)
    {
        ViewportRow& lastRow = rows.back();
        const I32 screenMidPoint = width / 2;
        const I32 rowMidPoint = (to_I32(lastRow.size()) * playerWidth) / 2;
        const I32 slideFactor = screenMidPoint - rowMidPoint;
        for (Rect<I32>& viewport : lastRow) {
            viewport.x += slideFactor;
        }
    }

    // Update the system viewports
    U8 idx = 0u;
    for (const ViewportRow& row : rows) {
        for (const Rect<I32>& viewport : row) {
            targetViewports[idx++].set(viewport);
        }
    }
}

static Time::ProfileTimer& GetTimer(Time::ProfileTimer& parentTimer, vector<Time::ProfileTimer*>& timers, const U8 index, const char* name) {
    while (timers.size() < to_size(index) + 1) {
        timers.push_back(&Time::ADD_TIMER(Util::StringFormat("{} {}", name, timers.size()).c_str()));
        parentTimer.addChildTimer(*timers.back());
    }

    return *timers[index];
}

bool Kernel::presentToScreen(FrameEvent& evt) {
    PROFILE_SCOPE_AUTO( Profiler::Category::IO );

    Time::ScopedTimer time(_flushToScreenTimer);

    {
        Time::ScopedTimer time1(_preRenderTimer);
        if (!frameListenerMgr().createAndProcessEvent(FrameEventType::FRAME_PRERENDER, evt)) {
            return false;
        }
    }

    const U8 playerCount = _projectManager->activePlayerCount();

    const auto& backBufferRT = _platformContext.gfx().renderTargetPool().getRenderTarget( RenderTargetNames::BACK_BUFFER );
    const Rect<I32> mainViewport{0, 0, to_I32(backBufferRT->getWidth()), to_I32(backBufferRT->getHeight())};
    
    if (_prevViewport != mainViewport || _prevPlayerCount != playerCount) {
        ComputeViewports(mainViewport, _targetViewports, playerCount);
        _prevViewport.set(mainViewport);
        _prevPlayerCount = playerCount;
    }


    RenderPassManager::RenderParams renderParams{};
    renderParams._sceneRenderState = &_projectManager->activeProject()->getActiveScene()->state()->renderState();

    for (U8 i = 0u; i < playerCount; ++i) {
        Attorney::ProjectManagerKernel::currentPlayerPass(_projectManager, i);
        renderParams._playerPass = i;

        if (!frameListenerMgr().createAndProcessEvent(FrameEventType::FRAME_SCENERENDER_START, evt))
        {
            return false;
        }

        renderParams._targetViewport = _targetViewports[i];

        {
            Time::ProfileTimer& timer = GetTimer(_flushToScreenTimer, _renderTimer, i, "Render Timer");
            renderParams._parentTimer = &timer;
            Time::ScopedTimer time2(timer);
            _renderPassManager->render(renderParams);
        }

        if (!frameListenerMgr().createAndProcessEvent(FrameEventType::FRAME_SCENERENDER_END, evt)) {
            return false;
        }
    }

    {
        Time::ScopedTimer time4(_postRenderTimer);
        if(!frameListenerMgr().createAndProcessEvent(FrameEventType::FRAME_POSTRENDER, evt)) {
            return false;
        }
    }

    for (U32 i = playerCount; i < to_U32(_renderTimer.size()); ++i) {
        Time::ProfileTimer::removeTimer(*_renderTimer[i]);
        _renderTimer.erase(begin(_renderTimer) + i);
    }

    return true;
}

// The first loops compiles all the visible data, so do not render the first couple of frames
void Kernel::warmup()
{
    Console::printfn(LOCALE_STR("START_RENDER_LOOP"));

    _timingData.freezeGameTime(true);

    for (U8 i = 0u; i < g_warmupFrameCount; ++i)
    {
        onLoop();
    }
    _timingData.freezeGameTime(false);

    _timingData.update(Time::App::ElapsedMicroseconds(), FIXED_UPDATE_RATE_US );

    stopSplashScreen();

    Attorney::ProjectManagerKernel::initPostLoadState(_projectManager);
}

ErrorCode Kernel::initialize(const string& entryPoint)
{
    const SysInfo& systemInfo = const_sysInfo();
    if (Config::REQUIRED_RAM_SIZE_IN_BYTES > systemInfo._availableRamInBytes)
    {
        return ErrorCode::NOT_ENOUGH_RAM;
    }

    g_printTimer = g_printTimerBase;

    // Don't log parameter requests
    _platformContext.paramHandler().setDebugOutput(false);
    // Load info from XML files
    Configuration& config = _platformContext.config();
    loadFromXML( config, Paths::g_xmlDataLocation, entryPoint.c_str() );

    if (Util::FindCommandLineArgument(_argc, _argv, "disableRenderAPIDebugging"))
    {
        config.debug.renderer.enableRenderAPIDebugging = false;
        config.debug.renderer.enableRenderAPIBestPractices = false;
    }
    if (Util::FindCommandLineArgument(_argc, _argv, "disableAssertOnRenderAPIError"))
    {
        config.debug.renderer.assertOnRenderAPIError = false;
    }
    if (Util::FindCommandLineArgument(_argc, _argv, "disableAPIExtensions"))
    {
        config.debug.renderer.useExtensions = false;
    }

    if ( Util::ExtractStartupProject( _argc, _argv, config.startupProject ) )
    {
        Console::printfn( LOCALE_STR( "START_APPLICATION_PROJECT_ARGUMENT" ) , config.startupProject.c_str() );
    }

    if (config.runtime.targetRenderingAPI >= to_U8(RenderAPI::COUNT))
    {
        config.runtime.targetRenderingAPI = to_U8(RenderAPI::OpenGL);
    }

    DIVIDE_ASSERT(g_backupThreadPoolSize >= 2u, "Backup thread pool needs at least 2 threads to handle background tasks without issues!");

    totalThreadCount(std::max( config.runtime.maxWorkerThreads > 0 ? config.runtime.maxWorkerThreads : to_I32( HardwareThreadCount() ), to_I32(g_minimumGeneralPurposeThreadCount)));

    _platformContext.pfx().apiID(PXDevice::PhysicsAPI::PhysX);
    _platformContext.sfx().apiID(SFXDevice::AudioAPI::SDL);

    ASIO::SET_LOG_FUNCTION([](const std::string_view msg, const bool isError) {
        if (isError) {
            Console::errorfn(string(msg).c_str());
        } else {
            Console::printfn(string(msg).c_str());
        }
    });

    Console::printfn( LOCALE_STR( "START_APPLICATION_WORKING_DIRECTORY" ) , systemInfo._workingDirectory.string() );

    Console::printfn( LOCALE_STR( "START_RENDER_INTERFACE" ) ) ;

    _platformContext.server().init(static_cast<U16>(443), "127.0.0.1", true);

    if (!_platformContext.client().connect(config.serverAddress, 443))
    {
        _platformContext.client().connect("127.0.0.1", 443);
    }

    Locale::ChangeLanguage(config.language.c_str());
    ECS::Initialize();

    Console::printfn(LOCALE_STR("START_RENDER_INTERFACE"));

    const RenderAPI renderingAPI = static_cast<RenderAPI>(config.runtime.targetRenderingAPI);

    ErrorCode initError = Attorney::ApplicationKernel::SetRenderingAPI(_platformContext.app(), renderingAPI);
    if (initError != ErrorCode::NO_ERR)
    {
        return initError;
    }

    Attorney::TextureKernel::UseTextureDDSCache( config.debug.cache.enabled && config.debug.cache.textureDDS );

    Camera::InitPool();
    initError = _platformContext.gfx().initRenderingAPI(_argc, _argv, renderingAPI);

    // If we could not initialize the graphics device, exit
    if (initError != ErrorCode::NO_ERR)
    {
        return initError;
    }
    { // Start thread pools
        const size_t cpuThreadCount = totalThreadCount();
        std::atomic_size_t threadCounter = cpuThreadCount;

        const auto initTaskPool = [&](const TaskPoolType taskPoolType, const U32 threadCount)
        {
            if (!_platformContext.taskPool(taskPoolType).init(threadCount, [this, taskPoolType, &threadCounter](const std::thread::id& threadID)
                {
                    Attorney::PlatformContextKernel::onThreadCreated(platformContext(), taskPoolType, threadID, false);
                    threadCounter.fetch_sub(1);
                }))
            {
                return ErrorCode::CPU_NOT_SUPPORTED;
            }
                return ErrorCode::NO_ERR;
        };

        initError = initTaskPool(TaskPoolType::HIGH_PRIORITY, to_U32(cpuThreadCount - g_backupThreadPoolSize) );
        if (initError != ErrorCode::NO_ERR) {
            return initError;
        }
        initError = initTaskPool(TaskPoolType::LOW_PRIORITY, to_U32(g_backupThreadPoolSize) );
        if (initError != ErrorCode::NO_ERR) {
            return initError;
        }
        WAIT_FOR_CONDITION(threadCounter.load() == 0);
    }

    initError = _platformContext.gfx().postInitRenderingAPI(config.runtime.resolution);
    // If we could not initialize the graphics device, exit
    if (initError != ErrorCode::NO_ERR) {
        return initError;
    }

    SceneEnvironmentProbePool::OnStartup(_platformContext.gfx());
    _inputConsumers.fill(nullptr);

    if constexpr(Config::Build::ENABLE_EDITOR) {
        _inputConsumers[to_base(InputConsumerType::Editor)] = &_platformContext.editor();
    }

    _inputConsumers[to_base(InputConsumerType::GUI)] = &_platformContext.gui();
    _inputConsumers[to_base(InputConsumerType::Scene)] = _projectManager;

    // Add our needed app-wide render passes. RenderPassManager is responsible for deleting these!
    _renderPassManager->setRenderPass(RenderStage::SHADOW,       {   });
    _renderPassManager->setRenderPass(RenderStage::REFLECTION,   { RenderStage::SHADOW });
    _renderPassManager->setRenderPass(RenderStage::REFRACTION,   { RenderStage::SHADOW });
    _renderPassManager->setRenderPass(RenderStage::DISPLAY,      { RenderStage::REFLECTION, RenderStage::REFRACTION });
    _renderPassManager->setRenderPass(RenderStage::NODE_PREVIEW, { RenderStage::REFLECTION, RenderStage::REFRACTION });

    Console::printfn(LOCALE_STR("SCENE_ADD_DEFAULT_CAMERA"));

    WindowManager& winManager = _platformContext.app().windowManager();
    winManager.mainWindow()->addEventListener(WindowEvent::LOST_FOCUS, [mgr = _projectManager](const DisplayWindow::WindowEventArgs& ) {
        mgr->onChangeFocus(false);
        return true;
    });
    winManager.mainWindow()->addEventListener(WindowEvent::GAINED_FOCUS, [mgr = _projectManager](const DisplayWindow::WindowEventArgs& ) {
        mgr->onChangeFocus(true);
        return true;
    });

    Script::OnStartup();
    ProjectManager::OnStartup(_platformContext);
    // Initialize GUI with our current resolution
    initError = _platformContext.gui().init(_platformContext, resourceCache());
    if ( initError != ErrorCode::NO_ERR )
    {
        return initError;
    }

    startSplashScreen();

    Console::printfn(LOCALE_STR("START_SOUND_INTERFACE"));
    initError = _platformContext.sfx().initAudioAPI();
    if (initError != ErrorCode::NO_ERR)
    {
        return initError;
    }

    Console::printfn(LOCALE_STR("START_PHYSICS_INTERFACE"));
    initError = _platformContext.pfx().initPhysicsAPI(Config::TARGET_FRAME_RATE, config.runtime.simSpeed);
    if (initError != ErrorCode::NO_ERR)
    {
        return initError;
    }

    Str<256> startupProject = config.startupProject.c_str();
    if constexpr ( Config::Build::IS_EDITOR_BUILD )
    {
        startupProject = Config::DEFAULT_PROJECT_NAME;
        Console::printfn(LOCALE_STR("START_FRAMEWORK_EDITOR"), startupProject.c_str() );
    }
    else
    {
        Console::printfn( LOCALE_STR( "START_FRAMEWORK_GAME" ), startupProject.c_str() );
    }

    ProjectIDs& projects = Attorney::ProjectManagerKernel::init(_projectManager);
    if ( projects.empty() )
    {
        Console::errorfn( LOCALE_STR( "ERROR_PROJECTS_LOAD" ) );
        return ErrorCode::MISSING_PROJECT_DATA;
    }

    ProjectID targetProject{};
    for ( auto& project : projects )
    {
        if (project._name == startupProject)
        {
            targetProject = project;
            break;
        }
    }

    while ( targetProject._guid == 0 )
    {
        targetProject = projects.front();
        Console::warnfn( LOCALE_STR( "WARN_PROJECT_NOT_FOUND" ), startupProject.c_str(), targetProject._name.c_str() );
        startupProject = targetProject._name;
    }

    if ( targetProject._guid == 0 )
    {
        Console::errorfn( LOCALE_STR( "WARN_PROJECT_NOT_FOUND" ), startupProject.c_str(), targetProject._name.c_str() );
        return ErrorCode::MISSING_PROJECT_DATA;
    }

    initError = _projectManager->loadProject( targetProject, false );

    if ( initError != ErrorCode::NO_ERR )
    {
        return initError;
    }

    idle(true, 0u, 0u);

    if (!_projectManager->loadComplete())
    {
        Console::errorfn(LOCALE_STR("ERROR_SCENE_LOAD_NOT_CALLED"), startupProject.c_str() );
        return ErrorCode::MISSING_SCENE_LOAD_CALL;
    }

    _platformContext.gui().addText("ProfileData",           // Unique ID
                                    RelativePosition2D{
                                         ._x = RelativeValue{
                                             ._scale = 0.75f, 
                                             ._offset = 0.0f
                                         },
                                         ._y = RelativeValue{
                                             ._scale = 0.2f,
                                             ._offset = 0.0f
                                         }
                                    },                           // Position
                                    Font::DROID_SERIF_BOLD,      // Font
                                    UColour4(255,  50, 0, 255),  // Colour
                                    "",                     // Text
                                    true,               // Multiline
                                    12);                 // Font size

    ShadowMap::initShadowMaps(_platformContext.gfx());

    _renderPassManager->postInit();

    if constexpr (Config::Build::ENABLE_EDITOR) 
    {
        if (!_platformContext.editor().init(config.runtime.resolution)) 
        {
            return ErrorCode::EDITOR_INIT_ERROR;
        }
        _projectManager->addSelectionCallback([ctx = &_platformContext](const PlayerIndex idx, const vector_fast<SceneGraphNode*>& nodes)
        {
            ctx->editor().selectionChangeCallback(idx, nodes);
        });
    }

    Console::printfn(LOCALE_STR("INITIAL_DATA_LOADED"));

    return initError;
}

void Kernel::shutdown()
{
    Console::printfn(LOCALE_STR("STOP_KERNEL"));

    _platformContext.config().save();

    for (U8 i = 0u; i < to_U8(TaskPoolType::COUNT); ++i)
    {
        _platformContext.taskPool( static_cast<TaskPoolType>(i)).waitForAllTasks(true);
    }
    
    if constexpr (Config::Build::ENABLE_EDITOR)
    {
        _platformContext.editor().toggle(false);
    }

    ProjectManager::OnShutdown(_platformContext);
    Script::OnShutdown();
    MemoryManager::SAFE_DELETE(_projectManager);
    ECS::Terminate();

    ShadowMap::destroyShadowMaps(_platformContext.gfx());
    MemoryManager::SAFE_DELETE(_renderPassManager);

    SceneEnvironmentProbePool::OnShutdown(_platformContext.gfx());
    _platformContext.terminate();
    Camera::DestroyPool();
    resourceCache()->clear();
    MemoryManager::SAFE_DELETE(_resourceCache);
    for ( U8 i = 0u; i < to_U8( TaskPoolType::COUNT ); ++i )
    {
        _platformContext.taskPool( static_cast<TaskPoolType>(i) ).shutdown();
    }
    Console::printfn(LOCALE_STR("STOP_ENGINE_OK"));
}

void Kernel::onWindowSizeChange(const SizeChangeParams & params) {
    Attorney::GFXDeviceKernel::onWindowSizeChange(_platformContext.gfx(), params);

    if constexpr (Config::Build::ENABLE_EDITOR) {
        _platformContext.editor().onWindowSizeChange(params);
    }
}

void Kernel::onResolutionChange(const SizeChangeParams& params) {
    _projectManager->onResolutionChange(params);

    Attorney::GFXDeviceKernel::onResolutionChange(_platformContext.gfx(), params);

    if (!_splashScreenUpdating) {
        _platformContext.gui().onResolutionChange(params);
    }

    if constexpr(Config::Build::ENABLE_EDITOR) {
        _platformContext.editor().onResolutionChange(params);
    }
}

#pragma region Input Management
void Kernel::remapAbsolutePosition( Input::MouseEvent& eventInOut ) const noexcept
{
    vec2<I32> absPositionIn = { eventInOut.state().X.abs, eventInOut.state().Y.abs };

    const Rect<I32> renderingViewport = _platformContext.mainWindow().renderingViewport();
    CLAMP_IN_RECT(absPositionIn.x, absPositionIn.y, renderingViewport);

    if (Config::Build::ENABLE_EDITOR &&
        _platformContext.editor().running() &&
        !_platformContext.editor().hasFocus())
    {
        const Rect<I32> previewRect = _platformContext.editor().scenePreviewRect( false );
        absPositionIn = COORD_REMAP( absPositionIn, previewRect, renderingViewport );
        if ( !previewRect.contains(absPositionIn) )
        {
            CLAMP_IN_RECT( absPositionIn.x, absPositionIn.y, renderingViewport );
            eventInOut.inScenePreviewRect(true);
        }
    }

    const vec2<U16> resolution = _platformContext.gfx().renderingResolution();
    absPositionIn = COORD_REMAP( absPositionIn, renderingViewport, { 0, 0, to_I32( resolution.width ), to_I32( resolution.height ) } );
    Input::MouseState& state = Input::Attorney::MouseEventKernel::state(eventInOut);
    state.X.abs = absPositionIn.x;
    state.Y.abs = absPositionIn.y;
}

bool Kernel::mouseMoved(const Input::MouseMoveEvent& arg)
{
    if (_inputConsumers[to_base(InputConsumerType::Editor)] &&
        !_projectManager->wantsMouse() &&
        _inputConsumers[to_base(InputConsumerType::Editor)]->mouseMoved(arg))
    {
        return true;
    }

    Input::MouseMoveEvent remapArg = arg;
    remapAbsolutePosition( remapArg );

    for (U8 i = 1u; i < to_base(InputConsumerType::COUNT); ++i) 
    {
        if (_inputConsumers[i] && _inputConsumers[i]->mouseMoved(remapArg))
        {
            return true;
        }
    }

    return false;
}

bool Kernel::mouseButtonPressed(const Input::MouseButtonEvent& arg) {
    if (_inputConsumers[to_base(InputConsumerType::Editor)] &&
        !_projectManager->wantsMouse() &&
        _inputConsumers[to_base(InputConsumerType::Editor)]->mouseButtonPressed(arg))
    {
        return true;
    }

    Input::MouseButtonEvent remapArg = arg;
    remapAbsolutePosition( remapArg );

    for (U8 i = 1u; i < to_base(InputConsumerType::COUNT); ++i)
    {
        if (_inputConsumers[i] && _inputConsumers[i]->mouseButtonPressed(remapArg))
        {
            return true;
        }
    }

    return false;
}

bool Kernel::mouseButtonReleased(const Input::MouseButtonEvent& arg) {
    if (_inputConsumers[to_base(InputConsumerType::Editor)] &&
        !_projectManager->wantsMouse() &&
        _inputConsumers[to_base(InputConsumerType::Editor)]->mouseButtonReleased(arg))
    {
        return true;
    }

    Input::MouseButtonEvent remapArg = arg;
    remapAbsolutePosition( remapArg );

    for (U8 i = 1u; i < to_base(InputConsumerType::COUNT); ++i)
    {
        if (_inputConsumers[i] && _inputConsumers[i]->mouseButtonReleased(remapArg))
        {
            return true;
        }
    }

    return false;
}

bool Kernel::onKeyDown(const Input::KeyEvent& key) {
    for (auto inputConsumer : _inputConsumers) {
        if (inputConsumer && inputConsumer->onKeyDown(key)) {
            return true;
        }
    }

    return false;
}

bool Kernel::onKeyUp(const Input::KeyEvent& key) {
    for (auto inputConsumer : _inputConsumers) {
        if (inputConsumer && inputConsumer->onKeyUp(key)) {
            return true;
        }
    }

    return false;
}

bool Kernel::joystickAxisMoved(const Input::JoystickEvent& arg) {
    for (auto inputConsumer : _inputConsumers) {
        if (inputConsumer && inputConsumer->joystickAxisMoved(arg)) {
            return true;
        }
    }

    return false;
}

bool Kernel::joystickPovMoved(const Input::JoystickEvent& arg) {
    for (auto inputConsumer : _inputConsumers) {
        if (inputConsumer && inputConsumer->joystickPovMoved(arg)) {
            return true;
        }
    }

    return false;
}

bool Kernel::joystickButtonPressed(const Input::JoystickEvent& arg) {
    for (auto inputConsumer : _inputConsumers) {
        if (inputConsumer && inputConsumer->joystickButtonPressed(arg)) {
            return true;
        }
    }

    return false;
}

bool Kernel::joystickButtonReleased(const Input::JoystickEvent& arg) {
    for (auto inputConsumer : _inputConsumers) {
        if (inputConsumer && inputConsumer->joystickButtonReleased(arg)) {
            return true;
        }
    }

    return false;
}

bool Kernel::joystickBallMoved(const Input::JoystickEvent& arg) {
    for (auto inputConsumer : _inputConsumers) {
        if (inputConsumer && inputConsumer->joystickBallMoved(arg)) {
            return true;
        }
    }

    return false;
}

bool Kernel::joystickAddRemove(const Input::JoystickEvent& arg) {
    for (auto inputConsumer : _inputConsumers) {
        if (inputConsumer && inputConsumer->joystickAddRemove(arg)) {
            return true;
        }
    }

    return false;
}

bool Kernel::joystickRemap(const Input::JoystickEvent &arg) {
    for (auto inputConsumer : _inputConsumers) {
        if (inputConsumer && inputConsumer->joystickRemap(arg)) {
            return true;
        }
    }

    return false;
}

bool Kernel::onTextEvent(const Input::TextEvent& arg) {
    for (auto inputConsumer : _inputConsumers) {
        if (inputConsumer && inputConsumer->onTextEvent(arg)) {
            return true;
        }
    }

    return false;
}
#pragma endregion
};

