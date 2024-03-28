// ProjectManager.cpp : Defines the entry point for the application.

#include "ProjectManager.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include <regex>
#include <stack>

#include <vector>
#include <exception>
#include <optional>
#include <stdio.h>
#include <cassert>
#include <SDL_image.h>
#include <SDL_surface.h>
#include <SDL.h>
#include "fmt/core.h"

#if defined(IS_WINDOWS_BUILD)
#pragma comment(linker, "/subsystem:\"windows\" /entry:\"mainCRTStartup\"")

constexpr const char* OS_PREFIX = "windows";
constexpr bool WINDOWS_BUILD = true;

#define _WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

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

[[nodiscard]] std::string Startup( const char* lpApplicationName, const char* params, const char* workingDir )
{
    const auto ret = ShellExecute( NULL, "open", lpApplicationName, params, workingDir , SW_SHOWNORMAL );
    if (int(ret) <= 32)
    {
        return fmt::format( "Error: ShellExecute({}): {}", lpApplicationName, GetLastErrorAsString() );
    }
    
    return fmt::format( "Launching application: {}", lpApplicationName );
}

#else //IS_WINDOWS_BUILD
constexpr const char* OS_PREFIX = "unixlike";
constexpr bool WINDOWS_BUILD = false;
std::string Startup( [[maybe_unused]] const char* lpApplicationName, [[maybe_unused]] const char* params, [[maybe_unused]] const char* workingDir )
{
    return "Not implemented!";
}
#endif //IS_WINDOWS_BUILD

Image::Image( const std::filesystem::path& path, SDL_Renderer* renderer )
{
    _surface = IMG_Load( path.string().c_str() );
    assert( _surface );
    _texture = SDL_CreateTextureFromSurface( renderer, _surface );
    assert( _texture );
    SDL_QueryTexture( _texture, &_format, &_access, &_width, &_height );
    _aspectRatio = _width / float( _height );
}

Image::~Image()
{
    SDL_DestroyTexture( _texture );
    SDL_FreeSurface( _surface );
}


static std::string g_globalMessage = "";
static std::stack<bool> g_readOnlyFaded;

static void HelpMarker( const char* desc )
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


static void PushReadOnly( const bool fade )
{
    ImGui::PushItemFlag( ImGuiItemFlags_Disabled, true );
    if ( fade )
    {
        ImGui::PushStyleVar( ImGuiStyleVar_Alpha, std::max( 0.5f, ImGui::GetStyle().Alpha - 0.35f ) );
    }
    g_readOnlyFaded.push( fade );
}

static void PopReadOnly()
{
    ImGui::PopItemFlag();
    if ( g_readOnlyFaded.top() )
    {
        ImGui::PopStyleVar();
    }
    g_readOnlyFaded.pop();
}

static void SetTooltip( const char* text )
{
    if ( ImGui::IsItemHovered() )
    {
        ImGui::BeginTooltip();
        ImGui::Text( text );
        ImGui::EndTooltip();
    }
}

[[nodiscard]] static std::optional<std::string> find_directory( const std::filesystem::path& search_path, const std::regex& regex )
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
        g_globalMessage = fmt::format( MISSING_DIRECTORY_ERROR, std::filesystem::current_path().string().c_str() );
    }

    return std::nullopt;
}


[[nodiscard]] static std::pair<bool, bool> InitBuildConfigs( BuildConfig& config )
{
    auto buildFolderExists = []( const BuildTarget target, const std::string_view toolchain, const bool editor )
        {
            const std::string buildName = editor ? fmt::format( "{}-editor", BuildTargetNames[uint8_t( target )] ) : BuildTargetNames[uint8_t( target )];

            return find_directory(std::filesystem::current_path().parent_path() / BUILD_FOLDER_NAME, std::regex( fmt::format( "\\{}-{}-{}", OS_PREFIX, toolchain, buildName ) ) ).has_value();
        };

    bool haveGame = false, haveEditor = false;
    for ( uint8_t i = 0; i < uint8_t( BuildTarget::COUNT ); ++i )
    {
        for ( uint8_t j = 0; j < uint8_t( BuildType::COUNT ); ++j )
        {
            const bool exists = buildFolderExists( static_cast<BuildTarget>(i), config._toolchainName, j == uint8_t( BuildType::Editor ) );;
            if ( exists )
            {
                j == uint8_t( BuildType::Editor ) ? haveEditor = true : haveGame = true;
            }
            config._targets[i][j] = exists;
        }
    }

    return { haveGame, haveEditor };
}

static void populateProjects( std::vector<ProjectEntry>& projects )
{
    projects.resize(0);

    try
    {
        const std::filesystem::directory_iterator end;
        for ( std::filesystem::directory_iterator iter{ std::filesystem::path{".."} / PROJECTS_FOLDER_NAME }; iter != end; iter++ )
        {
            if ( !std::filesystem::is_directory( *iter ) ||
                 iter->path().filename().string().compare( DELETED_FOLDER_NAME ) == 0 )
            {
                continue;
            }

            ProjectEntry& entry = projects.emplace_back();
            entry._name = iter->path().filename().string();
            entry._path = iter->path().parent_path().string();
            entry._isDefault = entry._name.compare( DEFAULT_PROJECT_NAME ) == 0;
        }
    }
    catch ( std::exception& )
    {
        g_globalMessage = fmt::format( "Error listing project directory: {}", std::filesystem::current_path().string().c_str() );
    }
}

static ProjectEntry* getProjectByName( std::vector<ProjectEntry>& projects, const char* name)
{
    for ( ProjectEntry& entry : projects )
    {
        if ( entry._name.compare( name ) == 0 )
        {
            return &entry;
        }
    }

    return nullptr;
}

static ProjectEntry* getDefaultProject(std::vector<ProjectEntry>& projects)
{
    for (ProjectEntry& entry : projects)
    {
        if (entry._isDefault)
        {
            return &entry;
        }
    }

    return nullptr;
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
    SDL_Window* window = SDL_CreateWindow( "Divide-Framework project manager", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALLOW_HIGHDPI );
    if ( window == nullptr )
    {
        printf( "Error: SDL_CreateWindow(): %s\n", SDL_GetError() );
        return -1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer( window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED );
    if ( renderer == nullptr )
    {
        printf( "Error creating SDL_Renderer!" );
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

    constexpr uint8_t iconImageSize = 32u;
    constexpr uint8_t logoImageSize = 96u;
    constexpr uint8_t projectImageSize = 128u;

    const std::filesystem::path iconsPath = std::filesystem::current_path().parent_path() / "Assets" / "Icons";
    std::unique_ptr<Image> existingProjectIcon = std::make_unique<Image> ( (iconsPath / "box-unpacking.png").c_str(), renderer );
    std::unique_ptr<Image> newProjectIcon = std::make_unique<Image> ( (iconsPath / "cardboard-box-closed.png").c_str(), renderer );
    std::unique_ptr<Image> divideLogo = std::make_unique<Image> ( (iconsPath / "divide.png").c_str(), renderer );
    std::unique_ptr<Image> deleteIcon = std::make_unique<Image> ( (iconsPath / "trash-can.png").c_str(), renderer );
    std::unique_ptr<Image> duplicateIcon = std::make_unique<Image> ( (iconsPath / "checkbox-tree.png").c_str(), renderer );
    std::unique_ptr<Image> launchIcon = std::make_unique<Image> ( (iconsPath / "play-button.png").c_str(), renderer );
    std::unique_ptr<Image> closeIcon = std::make_unique<Image> ( (iconsPath / "cancel.png").c_str(), renderer );

    const ImVec2 iconSize = ImVec2( iconImageSize, iconImageSize / deleteIcon->_aspectRatio );
    const ImVec2 logoSize = ImVec2( logoImageSize, logoImageSize / divideLogo->_aspectRatio );
    const ImVec2 projectSize = ImVec2( projectImageSize, projectImageSize / existingProjectIcon->_aspectRatio );

    std::vector<ProjectEntry> projects;
    populateProjects( projects );
    ProjectEntry* defaultProject = getDefaultProject( projects );

    ProjectEntry* selectedProject = nullptr;
    const auto setSelected = [&selectedProject]( ProjectEntry* project )
    {
        if ( selectedProject != nullptr )
        {
            selectedProject->_selected = false;
        }

        selectedProject = project;

        if ( selectedProject != nullptr )
        {
            selectedProject->_selected = true;
        }
    };


    BuildConfig MSVCConfig = { MSVC_PREFIX };
    BuildConfig CLANGConfig = { CLANG_PREFIX };

    const auto [haveMSVCGame, haveMSVCEditor] = InitBuildConfigs(MSVCConfig);
    const auto [haveClangGame, haveClangEditor] = InitBuildConfigs(CLANGConfig);
    const bool haveMSVCBuilds = haveClangEditor || haveMSVCGame;
    const bool haveClangBuilds = haveMSVCEditor || haveClangGame;
    
    int launch_mode = 0;
    int build_toolset = haveMSVCBuilds ? 0 : 1;
    BuildTarget selectedTarget = BuildTarget::Debug;
    const char* current_build_cfg = BuildTargetNames[uint8_t(selectedTarget)];

    bool haveMissingBuildTargets = false;
    auto OnSelectionChanged = [&](bool isComboBox = false)
    {
        g_globalMessage = fmt::format( "Current working dir: {}", std::filesystem::current_path().string().c_str() );

        if (isComboBox)
        {
            return;
        }

        setSelected(nullptr);
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

    const auto OnProjectsUpdated = [&projects, &defaultProject, setSelected]()
    {
        populateProjects( projects );
        defaultProject = getDefaultProject( projects );
        setSelected(nullptr);
    };

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
            static char InputBuf[256];
            static size_t InputLen = 0u;

            const auto DuplicateProject = [&projects, &defaultProject, setSelected, OnProjectsUpdated]( const ProjectEntry srcProject, const char* targetProjectName )
            {
                const auto targetPath = std::filesystem::path(srcProject._path) / targetProjectName;
                if (!std::filesystem::exists( targetPath ))
                {
                    std::filesystem::create_directories( targetPath );
                    std::filesystem::copy( std::filesystem::path(srcProject._path) / srcProject._name, targetPath, std::filesystem::copy_options::recursive );
                    OnProjectsUpdated();
                    setSelected( getProjectByName(projects, targetProjectName) );
                    memset( InputBuf, 0, sizeof( InputBuf ) );
                    InputLen = 0u;
                    return true;
                }

                g_globalMessage = fmt::format(DUPLICATE_ENTRY_ERROR, InputBuf);
                memset( InputBuf, 0, sizeof( InputBuf ) );
                InputLen = 0u;
                return false;
            };


            const auto ShowYesNoModal = []( const char* name, const char* text, bool showNameInput )
            {
                bool confirmed = false;
                if ( ImGui::BeginPopupModal( name, NULL, ImGuiWindowFlags_AlwaysAutoResize ) )
                {

                    ImGui::Text( text );
                    ImGui::Separator();

                    if ( showNameInput )
                    {
                        ImGui::Text("Target project name:"); ImGui::SameLine();
                        if ( ImGui::InputText( "##targetName", InputBuf, IM_ARRAYSIZE( InputBuf ) ) )
                        {
                            InputLen = strlen( InputBuf );
                        }
                    }

                    if ( ImGui::Button( "Cancel", ImVec2( 120, 0 ) ) )
                    {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SetItemDefaultFocus();
                    ImGui::SameLine();
                    if ( showNameInput && InputLen == 0 )
                    {
                        PushReadOnly( true );
                    }
                    if ( ImGui::Button( "YES!", ImVec2( 120, 0 ) ) )
                    {
                        confirmed = true;
                        ImGui::CloseCurrentPopup();
                    }
                    if ( showNameInput && InputLen == 0 )
                    {
                        PopReadOnly();
                    }

                    ImGui::EndPopup();
                }

                return confirmed;
            };

            static bool use_work_area = true;
            static ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

            // We demonstrate using the full viewport area or the work area (without menu-bars, task-bars etc.)
            // Based on your use case you may want one or the other.
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos( use_work_area ? viewport->WorkPos : viewport->Pos );
            ImGui::SetNextWindowSize( use_work_area ? viewport->WorkSize : viewport->Size );

            if ( ImGui::Begin( "Project Manager", &p_open, flags ) )
            {
                ImGui::Image( (ImTextureID)(intptr_t)divideLogo->_texture, logoSize );
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

                    const bool haveBuilds = haveMSVCBuilds || haveClangBuilds;
                    if ( launch_mode == 0 || !haveBuilds) { PushReadOnly(true); }
                    {
                        { // Toolset for Editor and Game launch modes only
                            ImGui::Text("Build Toolset:"); ImGui::SameLine();

                            if ( !haveMSVCBuilds ) { PushReadOnly( true ); }
                            if (ImGui::RadioButton( "MSVC", &build_toolset, 0 )) { OnSelectionChanged(); } ImGui::SameLine();
                            if ( !haveMSVCBuilds ) { PopReadOnly(); HelpMarker( "No prebuild MSVC executables detected! Build a MSVC configuration first!" ); }

                            if ( !haveClangBuilds ) { PushReadOnly(true); }
                            if (ImGui::RadioButton( "Clang", &build_toolset, 1 )) { OnSelectionChanged(); }
                            if ( !haveClangBuilds ) { PopReadOnly(); ImGui::SameLine(); HelpMarker( "No prebuild Clang executables detected! Build a Clang configuration first!" ); }

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
                ImGui::SameLine( ImGui::GetContentRegionAvail().x - iconImageSize - 5.f );
                ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.2f, 0.2f, 0.2f, 0.5f ) );
                ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( 0.3f, 0.3f, 0.3f, 0.5f ) );
                ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.4f, 0.4f, 0.4f, 0.5f ) );
                if ( ImGui::ImageButton( "Cancel", (ImTextureID)(intptr_t)closeIcon->_texture, iconSize ) )
                {
                    p_open = false;
                }
                ImGui::PopStyleColor( 3 );
                static const float scaleWidth = ImGui::CalcTextSize( "123" ).x;
                
                if (launch_mode == 0)
                {
                    PushReadOnly(true);
                }
                static float button_size = 120.f, button_distance = 15.f;
                ImGui::SetNextWindowPos( center, ImGuiCond_Always, ImVec2( 0.51f, 0.45f ) );
                if (ImGui::BeginChild( "Project List", ImVec2( ImGui::GetContentRegionAvail().x - scaleWidth * 1.5f, ImGui::GetContentRegionAvail().y * 0.9f ), ImGuiChildFlags_Border, ImGuiWindowFlags_HorizontalScrollbar ))
                {
                    float spacing_x = -button_size + ImGui::GetStyle().FramePadding.x;
                    const float& win_x = ImGui::GetWindowSize().x;

                    const auto CenteredText = []( const char* text )
                    {
                        ImVec2 text_size = ImGui::CalcTextSize( text );
                        ImGui::SetCursorPosX( ImGui::GetCursorPosX() + (button_size - text_size.x) * 0.5f );
                        ImGui::Text( text );
                    };

                    const auto EndGroup = [&]()
                    {
                        ImGui::EndGroup();
                        spacing_x += button_size + button_distance;
                        if ( spacing_x > win_x - (button_size) * 2 )
                        {
                            spacing_x = -button_size + ImGui::GetStyle().FramePadding.x;
                        }
                        else
                        {
                            ImGui::SameLine( spacing_x, button_size );
                        }
                    };

                    const auto SelectedImgButton = []( const char* str_id, ImTextureID user_texture_id, const ImVec2& image_size, const bool isSelected = false)
                    {
                        if ( isSelected )
                        {
                            static const ImVec4 selectedColour {0.25f, 0.75f, 0.25f, 1.0f};
                            ImGui::PushStyleColor( ImGuiCol_Button, selectedColour );
                            ImGui::PushStyleColor( ImGuiCol_ButtonActive, selectedColour );
                            ImGui::PushStyleColor( ImGuiCol_ButtonHovered, selectedColour );
                        }

                        const bool released = ImGui::ImageButton( str_id, user_texture_id, image_size );

                        if ( isSelected )
                        {
                            ImGui::PopStyleColor( 3 );
                        }

                        return released;
                    };

                    {
                        ImGui::BeginGroup();
                        if ( SelectedImgButton( "New Project", (ImTextureID)(intptr_t)newProjectIcon->_texture, { button_size, button_size } ) )
                        {
                            setSelected(nullptr);
                            ImGui::OpenPopup( CREATE_MODAL_NAME );
                        }
                        SetTooltip("Create a new project.\nThis will automatically add a default empty scene to the project!");
                        CenteredText( "New Project" );
                        EndGroup();

                        if ( ShowYesNoModal( CREATE_MODAL_NAME, CREATE_DESCRIPTION, true ) &&
                            !DuplicateProject(*defaultProject, InputBuf))
                        {
                            ImGui::OpenPopup( CREATE_MODAL_NAME );
                        }
                    }
                    for ( ProjectEntry& project : projects)
                    {
                        ImGui::BeginGroup();
                        const bool isSelected = &project == selectedProject;

                        if ( SelectedImgButton( project._name.c_str(), (ImTextureID)(intptr_t)existingProjectIcon->_texture, { button_size, button_size }, isSelected ))
                        {
                            g_globalMessage = fmt::format("Selected project ( {} )", project._name );
                            if ( selectedProject != nullptr)
                            {
                                selectedProject->_selected = false;
                            }
                            selectedProject = &project;
                            selectedProject->_selected = true;
                        }

                        SetTooltip( fmt::format("Open project [ {} ]", project._name ).c_str() );
                        CenteredText( project._name.c_str());
                        EndGroup();
                    }

                    ImGui::EndChild();
                }
                if ( launch_mode == 0 )
                {
                    PopReadOnly();
                }
                ImGui::SameLine();

                ImGui::VSliderFloat( "##Icon Size", ImVec2(scaleWidth, ImGui::GetContentRegionAvail().y - 55), &button_size, 100.f, 500.f );
                SetTooltip( "Change the size of the icons in the project list" );

                auto GetExecutablePath = [&](int launch_mode, int build_toolset, BuildTarget selectedTarget, std::string_view workingDir)
                {
                    const std::string toolset = build_toolset == 0 ? MSVCConfig._toolchainName : CLANGConfig._toolchainName;
                    const std::string editor_flag = launch_mode == 1 ? "-editor" : "";

                    std::string build_type = "";

                    switch(selectedTarget)
                    {
                        case BuildTarget::Debug:   build_type = "debug";   break;
                        case BuildTarget::Profile: build_type = "profile"; break;
                        case BuildTarget::Release: build_type = "release"; break;
                        default: break;
                    }
                        
                    const std::string name = fmt::format("{}\\Build\\{}-{}-{}{}\\bin\\Divide-Framework.exe", workingDir, OS_PREFIX, toolset, build_type, editor_flag);
                    return name;
                };

                ImGui::SetNextWindowPos( ImVec2(center.x, ImGui::GetCursorPosY() ), ImGuiCond_Always, ImVec2( 0.495f, 0.f ) );
                if ( ImGui::BeginChild( "Controls", ImVec2( ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y * 0.975f ), ImGuiChildFlags_None, ImGuiWindowFlags_None ) )
                {
                    const bool isReadOnly = selectedProject == nullptr || selectedProject->_isDefault;
                    if ( isReadOnly )
                    {
                        PushReadOnly( true );
                    }

                    if ( ImGui::ImageButton( "Delete Project", (ImTextureID)(intptr_t)deleteIcon->_texture, iconSize ) )
                    {
                        ImGui::OpenPopup( DELETE_MODAL_NAME );
                    }
                    SetTooltip( "Delete selected project" );

                    if( selectedProject != nullptr &&
                        ShowYesNoModal( DELETE_MODAL_NAME, fmt::format( DELETE_DESCRIPTION, selectedProject->_name.c_str() ).c_str(), false ))
                    {
                        const std::filesystem::path srcPath( selectedProject->_path );
                        const std::filesystem::path deletedProjectPath = srcPath / DELETED_FOLDER_NAME;

                        if (!std::filesystem::exists( deletedProjectPath ))
                        {
                            std::filesystem::create_directory( deletedProjectPath );
                        }

                        std::filesystem::rename( srcPath / selectedProject->_name, deletedProjectPath / selectedProject->_name );
                        OnProjectsUpdated();
                    }

                    ImGui::SameLine();

                    if ( ImGui::ImageButton( "Duplicate Project", (ImTextureID)(intptr_t)duplicateIcon->_texture, iconSize ) )
                    {
                        ImGui::OpenPopup( DUPLICATE_MODAL_NAME );
                    }

                    SetTooltip( "Duplicate selected project" );
                    if ( selectedProject != nullptr &&
                         ShowYesNoModal( DUPLICATE_MODAL_NAME, fmt::format( DUPLICATE_DESCRIPTION, selectedProject->_name.c_str() ).c_str(), true ) &&
                         !DuplicateProject(*selectedProject, InputBuf))
                    {
                        ImGui::OpenPopup( DUPLICATE_MODAL_NAME );
                    }

                    if ( isReadOnly )
                    {
                        PopReadOnly();
                    }

                    ImGui::SameLine();
                    
                    if ( ImGui::BeginChild( "Status", ImVec2( ImGui::GetContentRegionAvail().x * 0.95f, ImGui::GetContentRegionAvail().y * 0.8), ImGuiChildFlags_Border, ImGuiWindowFlags_None ) )
                    {
                        ImGui::Text( g_globalMessage.c_str() );
                        ImGui::EndChild();
                    }
                    ImGui::SameLine();

                    if ( ImGui::ImageButton( "Launch", (ImTextureID)(intptr_t)launchIcon->_texture, iconSize ) )
                    {
                        const auto workingDir = std::filesystem::current_path().string() + "\\..\\";
                        assert(selectedProject != nullptr || launch_mode == 0 );

                        const auto cmdLine = fmt::format( "--project={}", selectedProject->_name );
                        switch(launch_mode)
                        {
                            case 0: g_globalMessage = Startup("devenv.exe", workingDir.c_str() , workingDir.c_str()); break;
                            case 1: 
                            case 2: g_globalMessage = Startup( GetExecutablePath( launch_mode, build_toolset, selectedTarget, workingDir ).c_str(), cmdLine.c_str(), workingDir.c_str() ); break;
                            default: break;
                        }
                        p_open = false;
                    }
                    if ( launch_mode == 0 )
                    {
                        SetTooltip( "Open the Divide-Framework source code in Visual Studio.");
                    }
                    else if (launch_mode == 1)
                    {
                        SetTooltip("Launch the selected project in the editor.");
                    }
                    else
                    {
                        SetTooltip("Play the selected project.");
                    }
                    ImGui::EndChild();
                }

                ImGui::End();
            }
        }

        if ( !p_open )
        {
            done = true;
        }

        const ImVec4 clear_color( 0.45f, 0.55f, 0.60f, 1.00f );
        // Rendering
        ImGui::Render();
        SDL_RenderSetScale( renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y );
        SDL_SetRenderDrawColor( renderer, (Uint8)(clear_color.x * 255), (Uint8)(clear_color.y * 255), (Uint8)(clear_color.z * 255), (Uint8)(clear_color.w * 255) );
        SDL_RenderClear( renderer );
        ImGui_ImplSDLRenderer2_RenderDrawData( ImGui::GetDrawData() );
        SDL_RenderPresent( renderer );
    }

    // Cleanup
    closeIcon.reset();
    launchIcon.reset();
    duplicateIcon.reset();
    deleteIcon.reset();
    divideLogo.reset();
    newProjectIcon.reset();
    existingProjectIcon.reset();

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer( renderer );
    SDL_DestroyWindow( window );
    SDL_Quit();

	return 0;
}
