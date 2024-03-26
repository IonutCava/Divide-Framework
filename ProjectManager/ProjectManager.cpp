// ProjectManager.cpp : Defines the entry point for the application.
//

#include "ProjectManager.h"

#include "imgui.h"
#include "imgui_internal.h"

#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <stdio.h>
#include <SDL.h>

#if defined(_WIN32)
#pragma comment(linker, "/subsystem:\"windows\" /entry:\"mainCRTStartup\"")
#endif

#include <filesystem>
#include <fmt/format.h>
#include <regex>
#include <stack>
#include <cstdint>
#include <array>

namespace
{
    std::string g_globalMessage = "";
}

#if defined(IS_WINDOWS_BUILD)
constexpr const char* OS_PREFIX = "windows";
constexpr bool WINDOWS_BUILD = true;

#define _WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <SDL_surface.h>
#include <SDL_image.h>

//Returns the last Win32 error, in string format. Returns an empty string if there is no error.
std::string GetLastErrorAsString()
{
    //Get the error message ID, if any.
    DWORD errorMessageID = ::GetLastError();
    if ( errorMessageID == 0 )
    {
        return std::string(); //No error message has been recorded
    }

    LPSTR messageBuffer = nullptr;

    //Ask Win32 to give us the string version of that message ID.
    //The parameters we pass in, tell Win32 to create the buffer that holds the message for us (because we don't yet know how long the message string will be).
    size_t size = FormatMessageA( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                  NULL, errorMessageID, MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ), (LPSTR)&messageBuffer, 0, NULL );

    //Copy the error message into a std::string.
    std::string message( messageBuffer, size );

    //Free the Win32's string's buffer.
    LocalFree( messageBuffer );

    return message;
}

void Startup( const char* lpApplicationName, const char* params, const char* workingDir )
{
    int ret = (int)ShellExecute( NULL, "open", lpApplicationName, params, workingDir , SW_SHOWNORMAL );
    if (ret <= 32)
    {
        g_globalMessage = fmt::format( "Error: ShellExecute({}): {}", lpApplicationName, GetLastErrorAsString() );
    }
    else
    {
        g_globalMessage = fmt::format( "Launching application: {}", lpApplicationName );
    }

}

#else //IS_WINDOWS_BUILD
constexpr const char* OS_PREFIX = "unixlike";
constexpr bool WINDOWS_BUILD = false;
#endif //IS_WINDOWS_BUILD

constexpr const char* CLANG_PREFIX = "clang";
constexpr const char* MSVC_PREFIX = "msvc";

namespace 
{
void HelpMarker( const char* desc )
{
    ImGui::TextDisabled( "(?)" );
    if ( ImGui::BeginItemTooltip() )
    {
        ImGui::PushTextWrapPos( ImGui::GetFontSize() * 35.0f );
        ImGui::TextUnformatted( desc );
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

static std::stack<bool> g_readOnlyFaded;

void PushReadOnly( const bool fade )
{
    ImGui::PushItemFlag( ImGuiItemFlags_Disabled, true );
    if ( fade )
    {
        ImGui::PushStyleVar( ImGuiStyleVar_Alpha, std::max( 0.5f, ImGui::GetStyle().Alpha - 0.35f ) );
    }
    g_readOnlyFaded.push( fade );
}

void PopReadOnly()
{
    ImGui::PopItemFlag();
    if ( g_readOnlyFaded.top() )
    {
        ImGui::PopStyleVar();
    }
    g_readOnlyFaded.pop();
}

enum class BuildTarget : uint8_t
{
    Release,
    Profile,
    Debug,
    COUNT
};

static const char* BuildTargetNames[] = { "release", "profile", "debug", "COUNT" };
static_assert(std::size( BuildTargetNames ) == uint8_t( BuildTarget::COUNT ) + 1u, "BuildTarget name array out of sync!");

enum class BuildType : uint8_t
{
    Editor,
    Game,
    COUNT
};

static const char* BuildTypetNames[] = { "Editor", "Game", "COUNT" };
static_assert(std::size( BuildTypetNames ) == uint8_t( BuildType::COUNT ) + 1u, "BuildType name array out of sync!");

struct BuildConfig
{
    using Builds = std::array<bool, uint8_t(BuildType::COUNT)>;
    using Targets = std::array<Builds, uint8_t( BuildTarget::COUNT )>;
    
    std::string _toolchainName;
    Targets _targets;
};

[[nodiscard]] std::optional<std::string> find_directory( const std::string& search_path, const std::regex& regex )
{
    const std::filesystem::directory_iterator end;
    try
    {
        for ( std::filesystem::directory_iterator iter{ search_path }; iter != end; iter++ )
        {
            if ( std::filesystem::is_directory( *iter ) )
            {
                if ( std::regex_match( iter->path().filename().string(), regex ) )
                {
                    return (iter->path().string());
                }
            }
        }
    }
    catch ( std::exception& )
    {
        g_globalMessage = fmt::format( "Error finding directory: {}", std::filesystem::current_path().string().c_str() );
    }
    return std::nullopt;
}


[[nodiscard]] std::pair<bool, bool> InitBuildConfigs(BuildConfig& config)
{
    auto buildFolderExists = []( const BuildTarget target, const std::string_view toolchain, const bool editor )
    {
        const std::string buildName = editor ? fmt::format( "{}-editor", BuildTargetNames[uint8_t( target )] ) : BuildTargetNames[uint8_t( target )];

        return find_directory( "../Build", std::regex( fmt::format( "\\{}-{}-{}", OS_PREFIX, toolchain, buildName ) ) ).has_value();
    };

    bool haveGame = false, haveEditor = false;
    for ( uint8_t i = 0; i < uint8_t( BuildTarget::COUNT ); ++i )
    {
        for ( uint8_t j = 0; j < uint8_t( BuildType::COUNT ); ++j )
        {
            const bool exists = buildFolderExists( static_cast<BuildTarget>(i), config._toolchainName, j == uint8_t( BuildType::Editor ) );;
            if (exists)
            {
                j == uint8_t( BuildType::Editor ) ? haveEditor = true : haveGame = true;
            }
            config._targets[i][j] = exists;
        }
    }

    return { haveGame, haveEditor };
}

}

int main( int, char** )
{

    // Setup SDL
    if ( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER ) != 0 )
    {
        printf( "Error: %s\n", SDL_GetError() );
        return -1;
    }
    // From 2.0.18: Enable native IME.
#ifdef SDL_HINT_IME_SHOW_UI
    SDL_SetHint( SDL_HINT_IME_SHOW_UI, "1" );
#endif

    // Create window with SDL_Renderer graphics context
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow( "Divide-Framework project manager", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags );
    if ( window == nullptr )
    {
        printf( "Error: SDL_CreateWindow(): %s\n", SDL_GetError() );
        return -1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer( window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED );
    if ( renderer == nullptr )
    {
        SDL_Log( "Error creating SDL_Renderer!" );
        return 0;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForSDLRenderer( window, renderer );
    ImGui_ImplSDLRenderer2_Init( renderer );

    auto logoSurface = SDL_LoadBMP( (std::filesystem::current_path().string() + "/../Assets/MiscImages/divideLogo.bmp").c_str() );
    auto logoManager = IMG_Load( (std::filesystem::current_path().string() + "/../Assets/Icons/divide.png").c_str() );
    

    if ( logoSurface == nullptr || logoManager == nullptr)
    {
        return 0;
    }

    auto projectBtnTexture = SDL_CreateTextureFromSurface( renderer, logoSurface );
    auto projectLogoTexture = SDL_CreateTextureFromSurface( renderer, logoManager );

    int w, h, access;
    Uint32 format;
    SDL_QueryTexture( projectBtnTexture, &format, &access, &w, &h);
    constexpr uint8_t logoImageSize = 96u;
    constexpr uint8_t projectImageSize = 128u;
    const float aspect = w / float( h );

    BuildConfig MSVCConfig = { MSVC_PREFIX };
    BuildConfig CLANGConfig = { CLANG_PREFIX };

    const auto [haveMSVCGame, haveMSVCEditor] = InitBuildConfigs(MSVCConfig);
    const auto [haveClangGame, haveClangEditor] = InitBuildConfigs(CLANGConfig);
    const bool haveMSVCBuilds = haveClangEditor || haveMSVCGame;
    const bool haveClangBuilds = haveMSVCEditor || haveClangGame;
    
    const bool haveBuilds = haveMSVCBuilds || haveClangBuilds;

    ImVec4 clear_color = ImVec4( 0.45f, 0.55f, 0.60f, 1.00f );

    int launch_mode = 0;
    int build_toolset = haveMSVCBuilds ? 0 : 1;
    BuildTarget selectedTarget = BuildTarget::Debug;
    const char* current_build_cfg = BuildTargetNames[uint8_t(selectedTarget)];
    std::string selectedProject = "Default";

    bool haveMissingBuildTargets = false;
    auto OnSelectionChanged = [&](bool isComboBox = false)
    {
        g_globalMessage = fmt::format( "Current working dir: {}", std::filesystem::current_path().string().c_str() );

        if (isComboBox)
        {
            return;
        }

        if ( launch_mode == 0)
        {
            selectedTarget = static_cast<BuildTarget>(BuildTarget::Debug);
            current_build_cfg = BuildTargetNames[uint8_t( selectedTarget )];
            return;
        }

        selectedTarget = BuildTarget::COUNT;
        for ( uint8_t i = 0; i < uint8_t( BuildTarget::COUNT ); ++i )
        {
            const bool buildAvailable = build_toolset == 0 ? MSVCConfig._targets[i][launch_mode - 1] : CLANGConfig._targets[i][launch_mode - 1];
            if (!buildAvailable)
            {
                haveMissingBuildTargets = true;
            }
            else if ( selectedTarget == BuildTarget::COUNT)
            {
                selectedTarget = static_cast<BuildTarget>(i);
                current_build_cfg = BuildTargetNames[uint8_t(selectedTarget)];
            }
        }
    };

    OnSelectionChanged();

    // Main loop
    bool done = false;
    while ( !done )
    {
        SDL_Event event;
        while ( SDL_PollEvent( &event ) )
        {
            ImGui_ImplSDL2_ProcessEvent( &event );
            switch (event.type)
            {
                case SDL_QUIT : done = true; break;
                case SDL_KEYUP:
                {
                    switch ( event.key.keysym.sym )
                    {
                        case SDLK_ESCAPE: done = true; break;
                        default: break;
                    }
                } break;
                case SDL_WINDOWEVENT :
                {
                    if (event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID( window ) )
                    {
                        done = true;
                    }
                } break;
                default: break;
            }
        }

        // Start the Dear ImGui frame
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        bool p_open = true;
        {
            static bool use_work_area = true;
            static ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

            // We demonstrate using the full viewport area or the work area (without menu-bars, task-bars etc.)
            // Based on your use case you may want one or the other.
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos( use_work_area ? viewport->WorkPos : viewport->Pos );
            ImGui::SetNextWindowSize( use_work_area ? viewport->WorkSize : viewport->Size );

            if ( ImGui::Begin( "Project Manager", &p_open, flags ) )
            {
                ImVec2 logoSize = ImVec2( logoImageSize, logoImageSize / aspect );
                ImGui::Image( (ImTextureID)(intptr_t)projectLogoTexture, logoSize );
                ImGui::SameLine();

                ImVec2 center = ImGui::GetMainViewport()->GetCenter();
                ImGui::SetNextWindowPos( ImVec2(center.x, 0.f), ImGuiCond_Always, ImVec2( 0.5f, -0.2f ) );
                if ( ImGui::BeginChild( "Options", ImVec2( ImGui::GetContentRegionAvail().x * 0.45f, ImGui::GetContentRegionAvail().y * 0.125f ), ImGuiChildFlags_Border, ImGuiWindowFlags_None ) )
                {
                    ImGui::Text( "Launch Config:" ); ImGui::SameLine();

                    { // Launch mode: VS, Editor, Game
                        if constexpr ( !WINDOWS_BUILD ) { PushReadOnly( true ); }
                        if (ImGui::RadioButton( "Visual Studio", &launch_mode, 0 )) { OnSelectionChanged(); }
                        if constexpr ( !WINDOWS_BUILD ) { PopReadOnly( ); }

                        ImGui::SameLine();

                        if ( !haveMSVCEditor && !haveClangEditor) { PushReadOnly(true); }
                        if (ImGui::RadioButton( "Editor Mode", &launch_mode, 1 )) { OnSelectionChanged(); }
                        if ( !haveMSVCEditor && !haveClangEditor) { PopReadOnly(); ImGui::SameLine(); HelpMarker("No prebuild editor executables detected! Build a configuration first!"); }

                        ImGui::SameLine();

                        if ( !haveMSVCGame && !haveClangGame) { PushReadOnly(true); }
                        if (ImGui::RadioButton( "Game Mode", &launch_mode, 2 )) { OnSelectionChanged(); }
                        if ( !haveMSVCGame && !haveClangGame) { PopReadOnly(); ImGui::SameLine(); HelpMarker( "No prebuild game executables detected! Build a configuration first!" ); }

                    }

                    if ( launch_mode == 0 || !haveBuilds) { PushReadOnly(true); }
                    {
                        { // Toolset for Editor and Game launch modes only
                            ImGui::Text("Build Toolset:"); ImGui::SameLine();

                            if ( !haveMSVCBuilds ) { PushReadOnly( true ); }
                            if (ImGui::RadioButton( "MSVC", &build_toolset, 0 )) { OnSelectionChanged(); } ImGui::SameLine();
                            if ( !haveMSVCBuilds ) { PopReadOnly(); }

                            if ( !haveClangBuilds ) { PushReadOnly(true); }
                            if (ImGui::RadioButton( "Clang", &build_toolset, 1 )) { OnSelectionChanged(); }
                            if ( !haveClangBuilds ) { PopReadOnly(); }

                        }

                        { // Build preset for Editor and Game launch modes only
                            ImGui::Text( "Build Type:"); ImGui::SameLine();
                            ImGui::SetNextItemWidth( ImGui::GetFontSize() * 15 );
                            if (ImGui::BeginCombo( "##configCombo", current_build_cfg ))
                            {
                                for ( uint8_t i = 0; i < uint8_t( BuildTarget::COUNT ); ++i )
                                {
                                    bool isSelected = (current_build_cfg == BuildTargetNames[i]);
                                
                                    const bool buildAvailable = build_toolset == 0 ? MSVCConfig._targets[i][launch_mode - 1] : CLANGConfig._targets[i][launch_mode - 1];

                                    if ( ImGui::Selectable( BuildTargetNames[i], isSelected, buildAvailable ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_Disabled ) )
                                    {
                                        selectedTarget = static_cast<BuildTarget>(i);
                                        OnSelectionChanged(true);
                                    }
                                    if ( isSelected )
                                    {
                                        ImGui::SetItemDefaultFocus();
                                    }
                                }

                                ImGui::EndCombo();
                            }
                            if ( haveMissingBuildTargets )
                            {
                                ImGui::SameLine();
                                HelpMarker("Some build types are not available for this toolset!");
                            }
                        }
                    }
                    if ( launch_mode == 0 || !haveBuilds ) { PopReadOnly( ); }

                    ImGui::EndChild();
                }
                {
                    ImGuiWindowFlags window_flags = ImGuiWindowFlags_HorizontalScrollbar;
                    ImGui::SetNextWindowPos( center, ImGuiCond_Always, ImVec2( 0.5f, 0.45f ) );
                    if (ImGui::BeginChild( "Project List", ImVec2( ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y * 0.9f ), ImGuiChildFlags_Border, window_flags ))
                    {
                        if ( ImGui::BeginTable( "Projects", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_NoBordersInBody ) )
                        {

                            const std::filesystem::directory_iterator end;
                            ImVec2 size = ImVec2( projectImageSize, projectImageSize / aspect );
                            try
                            {
                                for ( std::filesystem::directory_iterator iter{ "../Projects" }; iter != end; iter++ )
                                {
                                    if ( std::filesystem::is_directory( *iter ) )
                                    {
                                        const auto projectName = iter->path().filename().string();

                                        ImGui::TableNextColumn();
                                        if ( ImGui::ImageButton( projectName.c_str(), (ImTextureID)(intptr_t)projectBtnTexture, size ) )
                                        {
                                            g_globalMessage = fmt::format("Selected project ( {} )", projectName );
                                            selectedProject = projectName;
                                        }

                                        ImGui::Text( projectName.c_str() ); 
                                        if (projectName.compare("Default") != 0)
                                        {
                                            ImGui::Button( "Delete" );
                                        }
                                    }
                                }
                            }
                            catch ( std::exception& )
                            {
                                g_globalMessage = fmt::format( "Error listing project directory: {}", std::filesystem::current_path().string().c_str() );
                            }
                            ImGui::TableNextColumn();
                            if ( ImGui::ImageButton( "New Project", (ImTextureID)(intptr_t)projectBtnTexture, size ) )
                            {
                                
                            }
                            ImGui::Text( "New Project" );
                            ImGui::EndTable();
                        }
                        ImGui::EndChild();
                    }
                }

                auto GetExecutablePath = [&](int launch_mode, int build_toolset, BuildTarget selectedTarget, std::string_view workingDir)
                {
                        const std::string toolset = build_toolset == 0 ? MSVCConfig._toolchainName : CLANGConfig._toolchainName;
                        const std::string editor_flag = launch_mode == 1 ? "-editor" : "";

                        std::string build_type = "";
                        std::string debug_suffix = "";

                        switch(selectedTarget)
                        {
                            case BuildTarget::Debug:   build_type = "debug"; debug_suffix = "_d"; break;
                            case BuildTarget::Profile: build_type = "profile"; break;
                            case BuildTarget::Release: build_type = "release"; break;
                            default: break;
                        }
                        
                        const std::string name = fmt::format("{}\\Build\\{}-{}-{}{}\\bin\\Divide-Framework{}.exe", workingDir, OS_PREFIX, toolset, build_type, editor_flag, debug_suffix);
                        return name;
                };

                float buttonWidth1 = ImGui::CalcTextSize( "Launch" ).x + ImGui::GetStyle().FramePadding.x * 2.f;
                float buttonWidth2 = ImGui::CalcTextSize( "Cancel" ).x + ImGui::GetStyle().FramePadding.x * 2.f;
                float widthNeeded = buttonWidth1 + ImGui::GetStyle().ItemSpacing.x + buttonWidth2;
                if ( ImGui::BeginChild( "Status", ImVec2( ImGui::GetContentRegionAvail().x * 0.85f, ImGui::GetContentRegionAvail().y * 0.9f ), ImGuiChildFlags_Border, ImGuiWindowFlags_None ) )
                {
                    ImGui::Text( g_globalMessage.c_str() );
                    ImGui::EndChild();
                }
                ImGui::SameLine();
                ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - widthNeeded * 1.5f);
                ImGui::SetCursorPosY( ImGui::GetCursorPosY() + 10);
                if (ImGui::Button( "Launch" ))
                {
                    const auto workingDir = std::filesystem::current_path().string() + "\\..\\";
                    const auto cmdLine = fmt::format( "--project={}", selectedProject );
                    switch(launch_mode)
                    {
                        case 0: Startup("devenv.exe", workingDir.c_str() , workingDir.c_str()); break;
                        case 1: 
                        case 2: Startup( GetExecutablePath( launch_mode, build_toolset, selectedTarget, workingDir ).c_str(), cmdLine.c_str(), workingDir.c_str() ); break;
                        default: break;
                    }
                    p_open = false;
                }
                ImGui::SameLine();
                ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - widthNeeded * 0.85f);
                ImGui::SetCursorPosY( ImGui::GetCursorPosY() + 10 );
                if (ImGui::Button( "Cancel" ))
                {
                    p_open = false;
                }
            }

            ImGui::End();
        }

        if ( !p_open )
            done = true;

        // Rendering
        ImGui::Render();
        SDL_RenderSetScale( renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y );
        SDL_SetRenderDrawColor( renderer, (Uint8)(clear_color.x * 255), (Uint8)(clear_color.y * 255), (Uint8)(clear_color.z * 255), (Uint8)(clear_color.w * 255) );
        SDL_RenderClear( renderer );
        ImGui_ImplSDLRenderer2_RenderDrawData( ImGui::GetDrawData() );
        SDL_RenderPresent( renderer );
    }

    // Cleanup
    SDL_DestroyTexture( projectLogoTexture );
    SDL_DestroyTexture( projectBtnTexture );
    SDL_FreeSurface( logoManager );
    SDL_FreeSurface( logoSurface );
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer( renderer );
    SDL_DestroyWindow( window );
    SDL_Quit();
	return 0;
}
