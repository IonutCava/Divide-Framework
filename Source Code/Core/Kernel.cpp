#include "stdafx.h"

#include "config.h"

#include "Headers/Kernel.h"
#include "Headers/Configuration.h"
#include "Headers/EngineTaskPool.h"
#include "Headers/PlatformContext.h"
#include "Headers/XMLEntryData.h"

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
#include "Managers/Headers/SceneManager.h"
#include "Physics/Headers/PXDevice.h"
#include "Platform/Audio/Headers/SFXDevice.h"
#include "Platform/File/Headers/FileWatcherManager.h"
#include "Platform/Headers/SDLEventManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Rendering/Camera/Headers/FreeFlyCamera.h"
#include "Resources/Headers/ResourceCache.h"
#include "Scripting/Headers/Script.h"
#include "Utility/Headers/XMLParser.h"

namespace Divide {

namespace {
    constexpr U32 g_backupThreadPoolSize = 2u;
    constexpr U32 g_printTimerBase = 15u;

    U32 g_printTimer = g_printTimerBase;

};

Kernel::Kernel(const I32 argc, char** argv, Application& parentApp)
    : _platformContext(PlatformContext(parentApp, *this)),
      _appLoopTimer(Time::ADD_TIMER("Main Loop Timer")),
      _frameTimer(Time::ADD_TIMER("Total Frame Timer")),
      _appIdleTimer(Time::ADD_TIMER("Loop Idle Timer")),
      _appScenePass(Time::ADD_TIMER("Loop Scene Pass Timer")),
      _physicsUpdateTimer(Time::ADD_TIMER("Physics Update Timer")),
      _physicsProcessTimer(Time::ADD_TIMER("Physics Process Timer")),
      _sceneUpdateTimer(Time::ADD_TIMER("Scene Update Timer")),
      _sceneUpdateLoopTimer(Time::ADD_TIMER("Scene Update Loop timer")),
      _cameraMgrTimer(Time::ADD_TIMER("Camera Manager Update Timer")),
      _flushToScreenTimer(Time::ADD_TIMER("Flush To Screen Timer")),
      _preRenderTimer(Time::ADD_TIMER("Pre-render Timer")),
      _postRenderTimer(Time::ADD_TIMER("Post-render Timer")),
      _argc(argc),
      _argv(argv)
{
    InitConditionalWait(_platformContext);

    std::atomic_init(&_splashScreenUpdating, false);
    _appLoopTimer.addChildTimer(_appIdleTimer);
    _appLoopTimer.addChildTimer(_frameTimer);
    _frameTimer.addChildTimer(_appScenePass);
    _appScenePass.addChildTimer(_cameraMgrTimer);
    _appScenePass.addChildTimer(_physicsUpdateTimer);
    _appScenePass.addChildTimer(_physicsProcessTimer);
    _appScenePass.addChildTimer(_sceneUpdateTimer);
    _appScenePass.addChildTimer(_flushToScreenTimer);
    _flushToScreenTimer.addChildTimer(_preRenderTimer);
    _flushToScreenTimer.addChildTimer(_postRenderTimer);
    _sceneUpdateTimer.addChildTimer(_sceneUpdateLoopTimer);

    _sceneManager = MemoryManager_NEW SceneManager(*this); // Scene Manager
    _resourceCache = MemoryManager_NEW ResourceCache(_platformContext);
    _renderPassManager = MemoryManager_NEW RenderPassManager(*this, _platformContext.gfx());
}

Kernel::~Kernel()
{
    DIVIDE_ASSERT(sceneManager() == nullptr && resourceCache() == nullptr && renderPassManager() == nullptr,
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
        [this, &splash](const Task& /*task*/) {
        U64 previousTimeUS = 0;
        while (_splashScreenUpdating) {
            const U64 deltaTimeUS = Time::App::ElapsedMicroseconds() - previousTimeUS;
            previousTimeUS += deltaTimeUS;
            _platformContext.beginFrame(PlatformContext::SystemComponentType::GFXDevice);
            splash.render(_platformContext.gfx());
            _platformContext.endFrame(PlatformContext::SystemComponentType::GFXDevice);
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

void Kernel::idle(const bool fast) {
    OPTICK_EVENT();

    if_constexpr(!Config::Build::IS_SHIPPING_BUILD) {
        Locale::Idle();
    }

    _platformContext.idle(fast);

    _sceneManager->idle();
    Script::idle();

    if (!fast && --g_printTimer == 0) {
        Console::flush();
        g_printTimer = g_printTimerBase;
    }

    if_constexpr(Config::Build::ENABLE_EDITOR) {
        const bool freezeLoopTime = _platformContext.editor().simulationPaused() && _platformContext.editor().stepQueue() == 0u;
        if (_timingData.freezeTime(freezeLoopTime)) {
            _platformContext.app().mainLoopPaused(_timingData.freezeLoopTime());
        }
    }
}

void Kernel::onLoop() {
    if (!_timingData.keepAlive()) {
        // exiting the rendering loop will return us to the last control point
        _platformContext.app().mainLoopActive(false);
        if (!sceneManager()->saveActiveScene(true, false)) {
            DIVIDE_UNEXPECTED_CALL();
        }
        return;
    }

    if_constexpr(!Config::Build::IS_SHIPPING_BUILD) {
        // Check for any file changes (shaders, scripts, etc)
        FileWatcherManager::update();
    }

    // Update internal timer
    _platformContext.app().timer().update();
    {
        Time::ScopedTimer timer(_appLoopTimer);
   
        // Update time at every render loop
        _timingData.update(Time::Game::ElapsedMicroseconds());
        FrameEvent evt = {};

        // Restore GPU to default state: clear buffers and set default render state
        _platformContext.beginFrame();
        {
            Time::ScopedTimer timer3(_frameTimer);
            // Launch the FRAME_STARTED event
            _timingData.keepAlive(frameListenerMgr().createAndProcessEvent(Time::Game::ElapsedMicroseconds(), FrameEventType::FRAME_EVENT_STARTED, evt));

            const bool timeFrozen = _timingData.freezeLoopTime();

            const U64 deltaTimeUSApp   = _timingData.currentTimeDeltaUS();
            const U64 deltaTimeUSReal  = timeFrozen ? 0ULL : deltaTimeUSApp;
            const U64 deltaTimeUSFixed = timeFrozen ? 0ULL : Time::SecondsToMicroseconds(1) / TICKS_PER_SECOND;

            // Process the current frame
            _timingData.keepAlive(_timingData.keepAlive() && mainLoopScene(evt, deltaTimeUSFixed, deltaTimeUSReal, deltaTimeUSApp));

            // Launch the FRAME_PROCESS event (a.k.a. the frame processing has ended event)
            _timingData.keepAlive(_timingData.keepAlive() && frameListenerMgr().createAndProcessEvent(Time::App::ElapsedMicroseconds(), FrameEventType::FRAME_EVENT_PROCESS, evt));
        }
        _platformContext.endFrame();

        // Launch the FRAME_ENDED event (buffers have been swapped)

        _timingData.keepAlive(_timingData.keepAlive() && frameListenerMgr().createAndProcessEvent(Time::App::ElapsedMicroseconds(), FrameEventType::FRAME_EVENT_ENDED, evt));

        _timingData.keepAlive(_timingData.keepAlive() && !_platformContext.app().ShutdownRequested());
    
        const ErrorCode err = _platformContext.app().errorCode();

        if (err != ErrorCode::NO_ERR) {
            Console::errorfn("Error detected: [ %s ]", getErrorCodeName(err));
            _timingData.keepAlive(false);
        }
    }

    const U32 frameCount = _platformContext.gfx().frameCount();
    if (platformContext().debug().enabled()) {
        static bool statsEnabled = false;
        // Turn on perf metric measuring 2 seconds before perf dump
        if (frameCount % (Config::TARGET_FRAME_RATE * Time::Seconds(8)) == 0) {
            statsEnabled = platformContext().gfx().queryPerformanceStats();
            platformContext().gfx().queryPerformanceStats(true);
        }
        // Our stats should be up to date now
        if (frameCount % (Config::TARGET_FRAME_RATE * Time::Seconds(10)) == 0) {
            Console::printfn(platformContext().debug().output().c_str());
            if (!statsEnabled) {
                platformContext().gfx().queryPerformanceStats(false);
            }
        }

        if (frameCount % (Config::TARGET_FRAME_RATE / 8) == 0u) {
            _platformContext.gui().modifyText("ProfileData", platformContext().debug().output(), true);
        }
    }

    if_constexpr(!Config::Build::IS_SHIPPING_BUILD) {
        if (frameCount % 6 == 0u) {
        
            DisplayWindow& window = _platformContext.mainWindow();
            static string originalTitle;
            if (originalTitle.empty()) {
                originalTitle = window.title();
            }

            F32 fps = 0.f, frameTime = 0.f;
            _platformContext.app().timer().getFrameRateAndTime(fps, frameTime);
            const Str256& activeSceneName = _sceneManager->getActiveScene().resourceName();
            constexpr const char* buildType = Config::Build::IS_DEBUG_BUILD ? "DEBUG" : Config::Build::IS_PROFILE_BUILD ? "PROFILE" : "RELEASE";
            constexpr const char* titleString = "[%s] - %s - %s - %5.2f FPS - %3.2f ms - FrameIndex: %d";
            window.title(titleString, buildType, originalTitle.c_str(), activeSceneName.c_str(), fps, frameTime, platformContext().gfx().frameCount());
        }
    }

    // Cap FPS
    const I16 frameLimit = _platformContext.config().runtime.frameRateLimit;
    const F32 deltaMilliseconds = Time::MicrosecondsToMilliseconds<F32>(_timingData.currentTimeDeltaUS());
    const F32 targetFrameTime = 1000.0f / frameLimit;

    if (deltaMilliseconds < targetFrameTime) {
        {
            Time::ScopedTimer timer2(_appIdleTimer);
            idle(true);
        }

        if (frameLimit > 0) {
            //Sleep the remaining frame time 
            std::this_thread::sleep_for(std::chrono::milliseconds(to_I32(std::floorf(targetFrameTime - deltaMilliseconds))));
        }
    }
}

bool Kernel::mainLoopScene(FrameEvent& evt,
                           U64 deltaTimeUSFixed, //Framerate independent deltaTime. Can be paused. (e.g. used by scene updates)
                           U64 deltaTimeUSReal,  //Framerate dependent deltaTime. Can be paused. (e.g. used by physics)
                           U64 deltaTimeUSApp)   //Real app delta time between frames. Can't be paused (e.g. used by editor)
{
    OPTICK_EVENT();

    Time::ScopedTimer timer(_appScenePass);
    {
        Time::ScopedTimer timer2(_cameraMgrTimer);
        // Update cameras. Always use app timing as pausing time would freeze the cameras in place
        // ToDo: add a speed slider in the editor -Ionut
        Camera::update(deltaTimeUSApp);
    }

    if (_platformContext.mainWindow().minimized()) {
        idle(false);
        SDLEventManager::pollEvents();
        return true;
    }

    {// We should pause physics simulations if needed, but the framerate dependency is handled by whatever 3rd party pfx library we are using
        Time::ScopedTimer timer2(_physicsProcessTimer);
        _platformContext.pfx().process(deltaTimeUSReal);
    }
    {
        Time::ScopedTimer timer2(_sceneUpdateTimer);

        const U8 playerCount = _sceneManager->getActivePlayerCount();
        U8 loopCount = 0;
        while (_timingData.runUpdateLoop()) {
            // Everything inside here should use fixed timesteps, apart from GFX updates which should use both!
            // Some things (e.g. tonemapping) need to resolve even if the simulation is paused (might not remain true in the future)

            if (loopCount == 0) {
                _sceneUpdateLoopTimer.start();
            }
            _sceneManager->onStartUpdateLoop(loopCount);

            _sceneManager->processGUI(deltaTimeUSFixed);

            // Flush any pending threaded callbacks
            for (U8 i = 0u; i < to_U8(TaskPoolType::COUNT); ++i) {
                _platformContext.taskPool(static_cast<TaskPoolType>(i)).flushCallbackQueue();
            }

            // Update scene based on input
            for (U8 i = 0; i < playerCount; ++i) {
                _sceneManager->processInput(i, deltaTimeUSFixed);
            }
            // process all scene events
            _sceneManager->processTasks(deltaTimeUSFixed);
            // Update the scene state based on current time (e.g. animation matrices)
            _sceneManager->updateSceneState(deltaTimeUSFixed);
            // Update visual effect timers as well
            Attorney::GFXDeviceKernel::update(_platformContext.gfx(), deltaTimeUSFixed, deltaTimeUSApp);

            if (loopCount == 0) {
                _sceneUpdateLoopTimer.stop();
            }

            _timingData.endUpdateLoop(deltaTimeUSFixed, true);
            ++loopCount;
        }
    }

    const U32 frameCount = _platformContext.gfx().frameCount();

    if (frameCount % (Config::TARGET_FRAME_RATE / Config::Networking::NETWORK_SEND_FREQUENCY_HZ) == 0) {
        U32 retryCount = 0;
        while (!Attorney::SceneManagerKernel::networkUpdate(_sceneManager, frameCount)) {
            if (retryCount++ > Config::Networking::NETWORK_SEND_RETRY_COUNT) {
                break;
            }
        }
    }

    D64 interpolationFactor = 1.0;
    if (!_timingData.freezeLoopTime()) {
        interpolationFactor = static_cast<D64>(_timingData.currentTimeUS() + deltaTimeUSApp - _timingData.nextGameTickUS()) / deltaTimeUSApp;
        CLAMP_01(interpolationFactor);
    }

    GFXDevice::FrameInterpolationFactor(interpolationFactor);
    
    // Update windows and get input events
    SDLEventManager::pollEvents();

    WindowManager& winManager = _platformContext.app().windowManager();
    winManager.update(deltaTimeUSApp);
    {
        Time::ScopedTimer timer3(_physicsUpdateTimer);
        // Update physics
        _platformContext.pfx().update(deltaTimeUSReal);
    }

    // Update the graphical user interface
    _platformContext.gui().update(deltaTimeUSReal);

    if_constexpr(Config::Build::ENABLE_EDITOR) {
        _platformContext.editor().update(deltaTimeUSApp);
    }

    return presentToScreen(evt);
}

void ComputeViewports(const Rect<I32>& mainViewport, vector<Rect<I32>>& targetViewports, const U8 count) {
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

    OPTICK_EVENT();

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
    if (extraColumns > 0) {
        ViewportRow& lastRow = rows.back();
        const I32 screenMidPoint = width / 2;
        const I32 rowMidPoint = to_I32(lastRow.size() * playerWidth / 2);
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

Time::ProfileTimer& getTimer(Time::ProfileTimer& parentTimer, vector<Time::ProfileTimer*>& timers, const U8 index, const char* name) {
    while (timers.size() < to_size(index) + 1) {
        timers.push_back(&Time::ADD_TIMER(Util::StringFormat("%s %d", name, timers.size()).c_str()));
        parentTimer.addChildTimer(*timers.back());
    }

    return *timers[index];
}

bool Kernel::presentToScreen(FrameEvent& evt) {
    OPTICK_EVENT();

    Time::ScopedTimer time(_flushToScreenTimer);

    {
        Time::ScopedTimer time1(_preRenderTimer);
        if (!frameListenerMgr().createAndProcessEvent(Time::App::ElapsedMicroseconds(), FrameEventType::FRAME_PRERENDER, evt)) {
            return false;
        }
    }

    const U8 playerCount = _sceneManager->getActivePlayerCount();
    const bool editorRunning = Config::Build::ENABLE_EDITOR && _platformContext.editor().running();

    const Rect<I32> mainViewport = _platformContext.mainWindow().renderingViewport();
    
    if (_prevViewport != mainViewport || _prevPlayerCount != playerCount) {
        ComputeViewports(mainViewport, _targetViewports, playerCount);
        _prevViewport.set(mainViewport);
        _prevPlayerCount = playerCount;
    }

    if (editorRunning) {
        ComputeViewports(_platformContext.editor().targetViewport(), _editorViewports, playerCount);
    }

    RenderPassManager::RenderParams renderParams = {};
    renderParams._editorRunning = editorRunning;
    renderParams._sceneRenderState = &_sceneManager->getActiveScene().state()->renderState();

    for (U8 i = 0; i < playerCount; ++i) {
        if (!frameListenerMgr().createAndProcessEvent(Time::App::ElapsedMicroseconds(), FrameEventType::FRAME_SCENERENDER_START, evt)) {
            return false;
        }

        renderParams._targetViewport = editorRunning ? _editorViewports[i] : _targetViewports[i];

        Attorney::SceneManagerKernel::currentPlayerPass(_sceneManager, i);
        {
            Time::ProfileTimer& timer = getTimer(_flushToScreenTimer, _renderTimer, i, "Render Timer");
            renderParams._parentTimer = &timer;
            Time::ScopedTimer time2(timer);
            _renderPassManager->render(renderParams);
        }

        if (!frameListenerMgr().createAndProcessEvent(Time::App::ElapsedMicroseconds(), FrameEventType::FRAME_SCENERENDER_END, evt)) {
            return false;
        }
    }

    {
        Time::ScopedTimer time4(_postRenderTimer);
        if(!frameListenerMgr().createAndProcessEvent(Time::App::ElapsedMicroseconds(), FrameEventType::FRAME_POSTRENDER, evt)) {
            return false;
        }
    }

    for (U32 i = playerCount; i < to_U32(_renderTimer.size()); ++i) {
        Time::ProfileTimer::removeTimer(*_renderTimer[i]);
        _renderTimer.erase(begin(_renderTimer) + i);
    }

    for (U8 i = 0; i < playerCount; ++i) {
        _sceneManager->savePreviousCamera(i);
    }

    return true;
}

// The first loops compiles all the visible data, so do not render the first couple of frames
void Kernel::warmup() {
    Console::printfn(Locale::Get(_ID("START_RENDER_LOOP")));

    _timingData.freezeTime(true);
    onLoop();
    _timingData.freezeTime(false);

    _timingData.update(Time::App::ElapsedMicroseconds());

    stopSplashScreen();

    Attorney::SceneManagerKernel::initPostLoadState(_sceneManager);
}

ErrorCode Kernel::initialize(const string& entryPoint) {
    const SysInfo& systemInfo = const_sysInfo();
    if (Config::REQUIRED_RAM_SIZE_IN_BYTES > systemInfo._availableRamInBytes) {
        return ErrorCode::NOT_ENOUGH_RAM;
    }

    // Don't log parameter requests
    _platformContext.paramHandler().setDebugOutput(false);
    // Load info from XML files
    XMLEntryData& entryData = _platformContext.entryData();
    Configuration& config = _platformContext.config();
    loadFromXML(entryData, (systemInfo._workingDirectory + Paths::g_xmlDataLocation.c_str() + entryPoint).c_str());
    loadFromXML(config, (systemInfo._workingDirectory + Paths::g_xmlDataLocation.c_str() + "config.xml").c_str());

    if (Util::FindCommandLineArgument(_argc, _argv, "disableRenderAPIDebugging")) {
        config.debug.enableRenderAPIDebugging = false;
    }
    if (config.runtime.targetRenderingAPI >= to_U8(RenderAPI::COUNT)) {
        config.runtime.targetRenderingAPI = to_U8(RenderAPI::OpenGL);
    }

    DIVIDE_ASSERT(g_backupThreadPoolSize >= 2u, "Backup thread pool needs at least 2 threads to handle background tasks without issues!");

    I32 threadCount = config.runtime.maxWorkerThreads;
    if (config.runtime.maxWorkerThreads < 0) {
        threadCount = std::max(HardwareThreadCount(), to_U32(RenderStage::COUNT) + g_backupThreadPoolSize);
    }
    totalThreadCount(threadCount);

    // Create mem log file
    const Str256& mem = config.debug.memFile.c_str();
    _platformContext.app().setMemoryLogFile(mem.compare("none") == 0 ? "mem.log" : mem);
    _platformContext.pfx().setAPI(PXDevice::PhysicsAPI::PhysX);
    _platformContext.sfx().setAPI(SFXDevice::AudioAPI::SDL);

    ASIO::SET_LOG_FUNCTION([](const std::string_view msg, const bool isError) {
        if (isError) {
            Console::errorfn(string(msg).c_str());
        } else {
            Console::printfn(string(msg).c_str());
        }
    });

    _platformContext.server().init(static_cast<U16>(443), "127.0.0.1", true);

    if (!_platformContext.client().connect(entryData.serverAddress, 443)) {
        _platformContext.client().connect("127.0.0.1", 443);
    }

    Paths::updatePaths(_platformContext);

    Locale::ChangeLanguage(config.language.c_str());
    ECS::Initialize();

    Console::printfn(Locale::Get(_ID("START_RENDER_INTERFACE")));

    const RenderAPI renderingAPI = static_cast<RenderAPI>(config.runtime.targetRenderingAPI);

    WindowManager& winManager = _platformContext.app().windowManager();
    ErrorCode initError = winManager.init(_platformContext,
                                          renderingAPI,
                                          vec2<I16>(-1),
                                          config.runtime.windowSize,
                                          static_cast<WindowMode>(config.runtime.windowedMode),
                                          config.runtime.targetDisplay);

    if (initError != ErrorCode::NO_ERR) {
        return initError;
    }

    Camera::initPool();
    initError = _platformContext.gfx().initRenderingAPI(_argc, _argv, renderingAPI);

    // If we could not initialize the graphics device, exit
    if (initError != ErrorCode::NO_ERR) {
        return initError;
    }
    std::atomic_size_t threadCounter = totalThreadCount();
    assert(threadCounter.load() > g_backupThreadPoolSize);

    if (!_platformContext.taskPool(TaskPoolType::HIGH_PRIORITY).init(to_U32(totalThreadCount()) - g_backupThreadPoolSize,
                                                                     TaskPool::TaskPoolType::TYPE_BLOCKING,
                                                                     [this, &threadCounter](const std::thread::id& threadID) {
                                                                         Attorney::PlatformContextKernel::onThreadCreated(platformContext(), threadID);
                                                                         threadCounter.fetch_sub(1);
                                                                     },
                                                                     "DIVIDE_WORKER_THREAD_"))
    {
        return ErrorCode::CPU_NOT_SUPPORTED;
    }

    if (!_platformContext.taskPool(TaskPoolType::LOW_PRIORITY).init(g_backupThreadPoolSize,
                                                                    TaskPool::TaskPoolType::TYPE_BLOCKING,
                                                                    [this, &threadCounter](const std::thread::id& threadID) {
                                                                        Attorney::PlatformContextKernel::onThreadCreated(platformContext(), threadID);
                                                                        threadCounter.fetch_sub(1);
                                                                    },
                                                                    "DIVIDE_BACKUP_THREAD_"))
    {
        return ErrorCode::CPU_NOT_SUPPORTED;
    }

    WAIT_FOR_CONDITION(threadCounter.load() == 0);

    initError = _platformContext.gfx().postInitRenderingAPI(config.runtime.resolution);
    // If we could not initialize the graphics device, exit
    if (initError != ErrorCode::NO_ERR) {
        return initError;
    }
    winManager.postInit();
    SceneEnvironmentProbePool::OnStartup(_platformContext.gfx());
    _inputConsumers.fill(nullptr);

    if_constexpr(Config::Build::ENABLE_EDITOR) {
        _inputConsumers[to_base(InputConsumerType::Editor)] = &_platformContext.editor();
    }

    _inputConsumers[to_base(InputConsumerType::GUI)] = &_platformContext.gui();
    _inputConsumers[to_base(InputConsumerType::Scene)] = _sceneManager;

    // Add our needed app-wide render passes. RenderPassManager is responsible for deleting these!
    _renderPassManager->addRenderPass("shadowPass",     0, RenderStage::SHADOW);
    _renderPassManager->addRenderPass("reflectionPass", 1, RenderStage::REFLECTION, { 0 });
    _renderPassManager->addRenderPass("refractionPass", 2, RenderStage::REFRACTION, { 0 });
    _renderPassManager->addRenderPass("displayStage",   3, RenderStage::DISPLAY, { 1, 2}, true);

    Console::printfn(Locale::Get(_ID("SCENE_ADD_DEFAULT_CAMERA")));

    Attorney::ShaderProgramKernel::UseShaderTextCache(config.debug.useShaderTextCache);
    Attorney::ShaderProgramKernel::UseShaderBinaryCache(config.debug.useShaderBinaryCache);
    Attorney::TextureKernel::UseTextureDDSCache(config.debug.useTextureDDSCache);

    winManager.mainWindow()->addEventListener(WindowEvent::LOST_FOCUS, [mgr = _sceneManager](const DisplayWindow::WindowEventArgs& ) {
        mgr->onChangeFocus(false);
        return true;
    });
    winManager.mainWindow()->addEventListener(WindowEvent::GAINED_FOCUS, [mgr = _sceneManager](const DisplayWindow::WindowEventArgs& ) {
        mgr->onChangeFocus(true);
        return true;
    });

    Script::OnStartup(_platformContext);
    SceneManager::OnStartup(_platformContext);
    // Initialize GUI with our current resolution
    if (!_platformContext.gui().init(_platformContext, resourceCache())) {
        return ErrorCode::GUI_INIT_ERROR;
    }

    startSplashScreen();

    Console::printfn(Locale::Get(_ID("START_SOUND_INTERFACE")));
    initError = _platformContext.sfx().initAudioAPI(_platformContext);
    if (initError != ErrorCode::NO_ERR) {
        return initError;
    }

    Console::printfn(Locale::Get(_ID("START_PHYSICS_INTERFACE")));
    initError = _platformContext.pfx().initPhysicsAPI(Config::TARGET_FRAME_RATE, config.runtime.simSpeed);
    if (initError != ErrorCode::NO_ERR) {
        return initError;
    }

    _platformContext.gui().addText("ProfileData",                                 // Unique ID
                                    RelativePosition2D(RelativeValue(0.75f, 0.0f),
                                                       RelativeValue(0.2f, 0.0f)), // Position
                                    Font::DROID_SERIF_BOLD,                        // Font
                                    UColour4(255,  50, 0, 255),                    // Colour
                                    "",                                            // Text
                                    true,                                          // Multiline
                                    12);                                           // Font size

    ShadowMap::initShadowMaps(_platformContext.gfx());
    _sceneManager->init(_platformContext, resourceCache());
    _platformContext.gfx().idle(true);

    const char* firstLoadedScene = Config::Build::IS_EDITOR_BUILD
                                        ? Config::DEFAULT_SCENE_NAME
                                        : entryData.startupScene.c_str();

    if (!_sceneManager->switchScene(firstLoadedScene, true, false, false)) {
        Console::errorfn(Locale::Get(_ID("ERROR_SCENE_LOAD")), firstLoadedScene);
        return ErrorCode::MISSING_SCENE_DATA;
    }

    if (!_sceneManager->loadComplete()) {
        Console::errorfn(Locale::Get(_ID("ERROR_SCENE_LOAD_NOT_CALLED")), firstLoadedScene);
        return ErrorCode::MISSING_SCENE_LOAD_CALL;
    }

    _renderPassManager->postInit();

    if_constexpr (Config::Build::ENABLE_EDITOR) {
        if (!_platformContext.editor().init(config.runtime.resolution)) {
            return ErrorCode::EDITOR_INIT_ERROR;
        }
        _sceneManager->addSelectionCallback([ctx = &_platformContext](const PlayerIndex idx, const vector<SceneGraphNode*>& nodes) {
            ctx->editor().selectionChangeCallback(idx, nodes);
        });
    }
    Console::printfn(Locale::Get(_ID("INITIAL_DATA_LOADED")));

    return initError;
}

void Kernel::shutdown() {
    Console::printfn(Locale::Get(_ID("STOP_KERNEL")));

    _platformContext.config().save();

    for (U8 i = 0u; i < to_U8(TaskPoolType::COUNT); ++i) {
        _platformContext.taskPool(static_cast<TaskPoolType>(i)).waitForAllTasks(true);
    }
    
    if_constexpr (Config::Build::ENABLE_EDITOR) {
        _platformContext.editor().toggle(false);
    }
    SceneManager::OnShutdown(_platformContext);
    Script::OnShutdown(_platformContext);
    MemoryManager::SAFE_DELETE(_sceneManager);
    ECS::Terminate();

    ShadowMap::destroyShadowMaps(_platformContext.gfx());
    MemoryManager::SAFE_DELETE(_renderPassManager);

    SceneEnvironmentProbePool::OnShutdown(_platformContext.gfx());
    _platformContext.terminate();
    Camera::destroyPool();
    resourceCache()->clear();
    MemoryManager::SAFE_DELETE(_resourceCache);

    Console::printfn(Locale::Get(_ID("STOP_ENGINE_OK")));
}

bool Kernel::onSizeChange(const SizeChangeParams& params) {

    const bool ret = Attorney::GFXDeviceKernel::onSizeChange(_platformContext.gfx(), params);

    if (!_splashScreenUpdating) {
        _platformContext.gui().onSizeChange(params);
    }

    if_constexpr (Config::Build::ENABLE_EDITOR) {
        _platformContext.editor().onSizeChange(params);
    }

    if (!params.isWindowResize) {
        _sceneManager->onSizeChange(params);
    }

    return ret;
}

#pragma region Input Management
vec2<I32> Kernel::remapMouseCoords(const vec2<I32>& absPositionIn, bool& remappedOut) const noexcept {
    remappedOut = false;
    if_constexpr(Config::Build::ENABLE_EDITOR) {
        const Editor& editor = _platformContext.editor();
        if (editor.running() && editor.scenePreviewFocused()) {
            const Rect<I32>& sceneRect = editor.scenePreviewRect(false);
            if (sceneRect.contains(absPositionIn)) {
                remappedOut = true;
                const Rect<I32>& viewport = _platformContext.gfx().getCurrentViewport();
                return COORD_REMAP(absPositionIn, sceneRect, viewport);
            }
        }
    }

    return absPositionIn;
}

bool Kernel::mouseMoved(const Input::MouseMoveEvent& arg) {
    if (_inputConsumers[to_base(InputConsumerType::Editor)] &&
        !_sceneManager->wantsMouse() &&
        _inputConsumers[to_base(InputConsumerType::Editor)]->mouseMoved(arg)) {
        return true;
    }

    //Remap coords in case we are using the Editor's scene view
    Input::MouseMoveEvent remapArg = arg;
    if_constexpr(Config::Build::ENABLE_EDITOR) {
        bool remapped = false;
        const vec2<I32> newPos = remapMouseCoords(arg.absolutePos(), remapped);
        if (remapped) {
            Input::Attorney::MouseEventKernel::absolutePos(remapArg, newPos);
        }
    } 

    for (U8 i = 1; i < to_base(InputConsumerType::COUNT); ++i) {
        if (_inputConsumers[i] && _inputConsumers[i]->mouseMoved(remapArg)) {
            return true;
        }
    }

    return false;
}

bool Kernel::mouseButtonPressed(const Input::MouseButtonEvent& arg) {
    if (_inputConsumers[to_base(InputConsumerType::Editor)] &&
        !_sceneManager->wantsMouse() &&
        _inputConsumers[to_base(InputConsumerType::Editor)]->mouseButtonPressed(arg))     {
        return true;
    }

    Input::MouseButtonEvent remapArg = arg;
    if_constexpr(Config::Build::ENABLE_EDITOR) {
        bool remapped = false;
        const vec2<I32> newPos = remapMouseCoords(arg.absPosition(), remapped);
        if (remapped) {
            Input::Attorney::MouseEventKernel::absolutePos(remapArg, newPos);
        }
    }

    for (U8 i = 1; i < to_base(InputConsumerType::COUNT); ++i) {
        if (_inputConsumers[i] && _inputConsumers[i]->mouseButtonPressed(remapArg)) {
            return true;
        }
    }

    return false;
}

bool Kernel::mouseButtonReleased(const Input::MouseButtonEvent& arg) {
    if (_inputConsumers[to_base(InputConsumerType::Editor)] &&
        !_sceneManager->wantsMouse() &&
        _inputConsumers[to_base(InputConsumerType::Editor)]->mouseButtonReleased(arg))
    {
        return true;
    }

    Input::MouseButtonEvent remapArg = arg;
    if_constexpr(Config::Build::ENABLE_EDITOR) {
        bool remapped = false;
        const vec2<I32> newPos = remapMouseCoords(arg.absPosition(), remapped);
        if (remapped) {
            Input::Attorney::MouseEventKernel::absolutePos(remapArg, newPos);
        }
    }

    for (U8 i = 1; i < to_base(InputConsumerType::COUNT); ++i) {
        if (_inputConsumers[i] && _inputConsumers[i]->mouseButtonReleased(remapArg)) {
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

bool Kernel::onUTF8(const Input::UTF8Event& arg) {
    for (auto inputConsumer : _inputConsumers) {
        if (inputConsumer && inputConsumer->onUTF8(arg)) {
            return true;
        }
    }

    return false;
}
#pragma endregion
};

