// Copyright (c) 2026 DIVIDE-Studio

#include "UnitTests/unitTestCommon.h"

#include "Core/Headers/DisplayManager.h"
#include "Core/Headers/WindowManager.h"
#include "Platform/Headers/DisplayWindow.h"

namespace Divide
{
    TEST_CASE( "Fullscreen toggle cycles windowed -> borderless -> exclusive -> windowed", "[window][fullscreen]" )
    {
        REQUIRE( NextFullscreenToggleType( WindowType::WINDOW ) == WindowType::FULLSCREEN_WINDOWED );
        REQUIRE( NextFullscreenToggleType( WindowType::FULLSCREEN_WINDOWED ) == WindowType::FULLSCREEN );
        REQUIRE( NextFullscreenToggleType( WindowType::FULLSCREEN ) == WindowType::WINDOW );
        REQUIRE( NextFullscreenToggleType( WindowType::COUNT ) == WindowType::COUNT );

        WindowType type = WindowType::WINDOW;
        type = NextFullscreenToggleType( type );
        REQUIRE( type == WindowType::FULLSCREEN_WINDOWED );
        type = NextFullscreenToggleType( type );
        REQUIRE( type == WindowType::FULLSCREEN );
        type = NextFullscreenToggleType( type );
        REQUIRE( type == WindowType::WINDOW );
    }

    TEST_CASE( "ResolveFullscreenMode picks closest resolution on the target display", "[window][fullscreen]" )
    {
        DisplayManager::OutputDisplayPropertiesContainer modes;

        DisplayManager::OutputDisplayProperties modeA{};
        modeA._resolution = { 1280u, 720u };
        modeA._internalMode.displayID = 1;
        modeA._internalMode.w = 1280;
        modeA._internalMode.h = 720;
        modes.push_back( modeA );

        DisplayManager::OutputDisplayProperties modeB{};
        modeB._resolution = { 1920u, 1080u };
        modeB._internalMode.displayID = 1;
        modeB._internalMode.w = 1920;
        modeB._internalMode.h = 1080;
        modes.push_back( modeB );

        DisplayManager::OutputDisplayProperties otherDisplay{};
        otherDisplay._resolution = { 1920u, 1080u };
        otherDisplay._internalMode.displayID = 2;
        otherDisplay._internalMode.w = 1920;
        otherDisplay._internalMode.h = 1080;
        modes.push_back( otherDisplay );

        SDL_DisplayMode resolved{};
        REQUIRE( WindowManager::ResolveFullscreenMode( modes, 1, { 1900u, 1060u }, resolved ) );
        CHECK( resolved.displayID == 1 );
        CHECK( resolved.w == 1920 );
        CHECK( resolved.h == 1080 );

        REQUIRE( WindowManager::ResolveFullscreenMode( modes, 1, { 1280u, 720u }, resolved ) );
        CHECK( resolved.w == 1280 );
        CHECK( resolved.h == 720 );

        SDL_DisplayMode fallback{};
        fallback.displayID = 3;
        fallback.w = 800;
        fallback.h = 600;
        REQUIRE( WindowManager::ResolveFullscreenMode( modes, 99, { 1920u, 1080u }, resolved, &fallback ) );
        CHECK( resolved.displayID == 3 );
        CHECK( resolved.w == 800 );
        CHECK( resolved.h == 600 );

        CHECK_FALSE( WindowManager::ResolveFullscreenMode( modes, 99, { 1920u, 1080u }, resolved, nullptr ) );
    }
} //namespace Divide
