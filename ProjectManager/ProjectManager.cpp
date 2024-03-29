// ProjectManager.cpp : Defines the entry point for the application.

#include "ProjectManager.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include <regex>
#include <stack>

#include <exception>
#include <optional>
#include <stdio.h>
#include <cassert>
#include <memory>
#include <string.h>
#include <thread>

#include <SDL_image.h>
#include <SDL_surface.h>
#include <SDL.h>
#include <fmt/core.h>

#include <boost/property_tree/xml_parser.hpp>

#include <ImGuiMisc/imguistyleserializer/ImGuiStyleSerializer.cpp>
#include <imgui_stdlib.h>

#if defined(IS_WINDOWS_BUILD)
#pragma comment(linker, "/subsystem:\"windows\" /entry:\"mainCRTStartup\"")

constexpr const char* OS_PREFIX = "windows";
constexpr bool WINDOWS_BUILD = true;

#define _WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

//Returns the last Win32 error, in string format. Returns an empty string if there is no error.
static std::string GetLastErrorAsString()
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

[[nodiscard]] std::pair<std::string, bool> Startup( const char* lpApplicationName, const char* params, const std::filesystem::path& workingDir )
{
    const auto ret = ShellExecute( NULL, "open", lpApplicationName, params, workingDir.string().c_str() , SW_SHOWNORMAL );
    if (int(ret) <= 32)
    {
        return {fmt::format( ERRORS[0], lpApplicationName, GetLastErrorAsString() ), true };
    }
    
    return {fmt::format( LAUNCH_MSG, lpApplicationName ), false };
}

#else //IS_WINDOWS_BUILD
constexpr const char* OS_PREFIX = "unixlike";
constexpr bool WINDOWS_BUILD = false;
std::pair<std::string, bopol> Startup( [[maybe_unused]] const char* lpApplicationName, [[maybe_unused]] const char* params, [[maybe_unused]] const std::filesystem::path& workingDir )
{
    return {"Not implemented!", false};
}
#endif //IS_WINDOWS_BUILD

Image::Image( const std::filesystem::path& path, SDL_Renderer* renderer )
    : _path( path )
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

bool Image::operator==( const Image& other ) const noexcept
{
    return _path == other._path;
}

static std::string g_globalMessage = "";
static std::string g_ideName = EDITOR_NAME;
static std::string g_ideNamedOriginal = EDITOR_NAME;
static std::string g_ideLaunchCommand = EDITOR_LAUNCH_COMMAND;
static std::string g_ideLaunchCommandOriginal = EDITOR_LAUNCH_COMMAND;
static std::stack<bool> g_readOnlyFaded;
static std::filesystem::path g_projectPath;
static std::filesystem::path g_iconsPath;

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
        g_globalMessage = fmt::format( ERRORS[1], search_path.string().c_str() );
    }

    return std::nullopt;
}


[[nodiscard]] static std::pair<bool, bool> InitBuildConfigs( BuildConfig& config )
{
    auto buildFolderExists = []( const BuildTarget target, const std::string_view toolchain, const bool editor )
        {
            const std::string buildName = editor ? fmt::format( "{}-editor", BuildTargetNames[uint8_t( target )] ) : BuildTargetNames[uint8_t( target )];

            return find_directory( g_projectPath / BUILD_FOLDER_NAME, std::regex( fmt::format( "\\{}-{}-{}", OS_PREFIX, toolchain, buildName ) ) ).has_value();
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

static Image* getImage( const std::string& imagePath, const std::string& imageName, const ImageDB& imageDB )
{
    const std::filesystem::path targetPath = std::filesystem::path( imagePath ) / imageName;
    assert( std::filesystem::is_regular_file( targetPath ) );
    for ( const Image_ptr& image : imageDB )
    {
        if ( image->_path == targetPath )
        {
            return image.get();
        }
    }
    return nullptr;
}

static std::weak_ptr<Image> loadImage( const std::string& imagePath, const std::string& imageName, SDL_Renderer* renderer,ImageDB& imageDB)
{
    const std::filesystem::path targetPath = std::filesystem::path( imagePath ) / imageName;
    assert( std::filesystem::is_regular_file( targetPath ) );
    for ( const Image_ptr& image : imageDB )
    {
        if ( image->_path == targetPath )
        {
            return image;
        }
    }

    return imageDB.emplace_back( std::make_unique<Image>( targetPath, renderer ) );
}

static boost::property_tree::iptree GetXmlTree(std::filesystem::path filePath)
{
    boost::property_tree::iptree XmlTree;
    try
    {
        boost::property_tree::read_xml( (g_projectPath / PROJECT_MANAGER_FOLDER_NAME / MANAGER_CONFIG_FILE_NAME).string(), XmlTree );
    }
    catch ( boost::property_tree::xml_parser_error& e )
    {
        g_globalMessage = fmt::format( ERRORS[7], (g_projectPath / PROJECT_MANAGER_FOLDER_NAME / MANAGER_CONFIG_FILE_NAME).string().c_str(), e.what() );
    }

    return XmlTree;
}

static void loadConfig()
{
    const std::filesystem::path configPath = g_projectPath / PROJECT_MANAGER_FOLDER_NAME / MANAGER_CONFIG_FILE_NAME;
    boost::property_tree::iptree XmlTree = GetXmlTree(configPath);

    if ( !XmlTree.empty() )
    {
        g_ideName = XmlTree.get<std::string>( CONFIG_IDE_NAME_TAG, EDITOR_NAME );
        g_ideLaunchCommand = XmlTree.get<std::string>( CONFIG_IDE_CMD_TAG, EDITOR_LAUNCH_COMMAND );

        g_ideNamedOriginal = g_ideName;
        g_ideLaunchCommandOriginal = g_ideLaunchCommand;
    }
}

static void saveConfig()
{
    const std::filesystem::path configPath = g_projectPath / PROJECT_MANAGER_FOLDER_NAME / MANAGER_CONFIG_FILE_NAME;
    boost::property_tree::iptree XmlTree = GetXmlTree( configPath );

    XmlTree.put<std::string>( CONFIG_IDE_NAME_TAG, g_ideName );
    XmlTree.put<std::string>( CONFIG_IDE_CMD_TAG, g_ideLaunchCommand);

    static boost::property_tree::xml_writer_settings<std::string> settings( ' ', 4 );
    try
    {
        boost::property_tree::write_xml( configPath.string(), XmlTree, std::locale(), settings );
    }
    catch ( boost::property_tree::xml_parser_error& e )
    {
        g_globalMessage = fmt::format( ERRORS[8], (g_projectPath / PROJECT_MANAGER_FOLDER_NAME / MANAGER_CONFIG_FILE_NAME).string().c_str(), e.what() );
    }
}

static void populateProjects( ProjectDB& projects, SDL_Renderer* renderer, ImageDB& imageDB, bool retry = false )
{
    projects.resize(0);

    try
    {
        boost::property_tree::iptree XmlTree;

        const std::filesystem::directory_iterator end;
        for ( std::filesystem::directory_iterator iter{ g_projectPath / PROJECTS_FOLDER_NAME }; iter != end; ++iter )
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
            entry._logoPath = g_iconsPath.string();

            if ( std::filesystem::is_regular_file( iter->path() / PROJECT_CONFIG_FILE_NAME ) )
            {
                XmlTree.clear();
                boost::property_tree::read_xml( (iter->path() / PROJECT_CONFIG_FILE_NAME).string(), XmlTree, boost::property_tree::xml_parser::trim_whitespace);
                if (!XmlTree.empty() )
                {
                    const std::string logoName = XmlTree.get<std::string>( CONFIG_LOGO_TAG, "" );
                    if (!logoName.empty())
                    {
                        entry._logoName = logoName;
                        entry._logoPath = iter->path().string();
                    }
                }
            }
            entry._logo = loadImage( entry._logoPath, entry._logoName, renderer, imageDB );

            for ( std::filesystem::directory_iterator sceneIter{ iter->path() / SCENES_FOLDER_NAME }; sceneIter != end; ++sceneIter )
            {
                if (std::filesystem::is_directory(*sceneIter))
                {
                    entry._sceneList.emplace_back( sceneIter->path().filename().string() );
                }
            }

        }
    }
    catch ( std::exception& e)
    {
        g_globalMessage = fmt::format( ERRORS[2], (g_projectPath / PROJECTS_FOLDER_NAME).string().c_str(), e.what() );
        if (!retry)
        {
            std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
            populateProjects( projects, renderer, imageDB, true );
        }
    }
}

static ProjectEntry* getProjectByName( ProjectDB& projects, const char* name)
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

static ProjectEntry* getDefaultProject( ProjectDB& projects)
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
    g_projectPath = std::filesystem::current_path();
    while ( !std::filesystem::exists( g_projectPath / PROJECTS_FOLDER_NAME ))
    {
        g_projectPath = g_projectPath.parent_path();
    }
    g_iconsPath = g_projectPath / ASSESTS_FOLDER_NAME / ICONS_FOLDER_NAME;

    loadConfig();

    // Setup SDL
    if ( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER ) != 0 )
    {
        printf( ERRORS[3], SDL_GetError() );
        return -1;
    }
    // From 2.0.18: Enable native IME.
#ifdef SDL_HINT_IME_SHOW_UI
    SDL_SetHint( SDL_HINT_IME_SHOW_UI, "1" );
#endif

    // Create window with SDL_Renderer graphics context
    SDL_Window* window = SDL_CreateWindow( WINDOW_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALLOW_HIGHDPI );
    if ( window == nullptr )
    {
        printf( ERRORS[4], SDL_GetError() );
        return -1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer( window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED );
    if ( renderer == nullptr )
    {
        printf( ERRORS[5], SDL_GetError() );
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
    ImGui::ResetStyle( ImGuiStyle_Dracula );
    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForSDLRenderer( window, renderer );
    ImGui_ImplSDLRenderer2_Init( renderer );

    constexpr uint8_t iconImageSize = 32u;
    constexpr uint8_t logoImageSize = 96u;
    constexpr uint8_t projectImageSize = 128u;

    ImageDB imageDB = {
        std::make_shared<Image>( (g_iconsPath / ICONS[0]).c_str(), renderer ),
        std::make_shared<Image>( (g_iconsPath / ICONS[1]).c_str(), renderer ),
        std::make_shared<Image>( (g_iconsPath / ICONS[2]).c_str(), renderer ),
        std::make_shared<Image>( (g_iconsPath / ICONS[3]).c_str(), renderer ),
        std::make_shared<Image>( (g_iconsPath / ICONS[4]).c_str(), renderer ),
        std::make_shared<Image>( (g_iconsPath / ICONS[5]).c_str(), renderer ),
        std::make_shared<Image>( (g_iconsPath / ICONS_INV_FOLDER_NAME / ICONS[5]).c_str(), renderer ),
        std::make_shared<Image>( (g_iconsPath / ICONS[6]).c_str(), renderer ),
        std::make_shared<Image>( (g_iconsPath / ICONS_INV_FOLDER_NAME / ICONS[6]).c_str(), renderer )
    };

    Image* newProjectIcon  = imageDB[0].get();
    Image* divideLogo      = imageDB[1].get();
    Image* deleteIcon      = imageDB[2].get();
    Image* duplicateIcon   = imageDB[3].get();
    Image* launchIcon      = imageDB[4].get();
    Image* closeIcon       = imageDB[5].get();
    Image* closeInvIcon    = imageDB[6].get();
    Image* settingsIcon    = imageDB[7].get();
    Image* settingsInvIcon = imageDB[8].get();

    ProjectDB projects;
    populateProjects( projects, renderer, imageDB);
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
        g_globalMessage = fmt::format( CURRENT_DIR_MSG, g_projectPath.string().c_str() );

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

    const auto OnProjectsUpdated = [&projects, &defaultProject, &imageDB, renderer, setSelected]()
    {
        populateProjects( projects, renderer, imageDB);
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

                bool ret = false;
                try
                {
                    if (!std::filesystem::exists( targetPath ))
                    {
                        std::filesystem::create_directories( targetPath );
                        std::filesystem::copy( std::filesystem::path(srcProject._path) / srcProject._name, targetPath, std::filesystem::copy_options::recursive );
                        OnProjectsUpdated();
                        setSelected( getProjectByName(projects, targetProjectName) );
                        ret = true;
                    }
                    else
                    {
                        g_globalMessage = fmt::format( DUPLICATE_ENTRY_ERROR, InputBuf );
                    }
                }
                catch (const std::exception& e)
                {
                    g_globalMessage = fmt::format( ERRORS[6],srcProject._name.c_str(), e.what() );
                }

                memset( InputBuf, 0, sizeof( InputBuf ) );
                InputLen = 0u;
                return ret;
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
                        ImGui::Text( TARGET_PROJECT_NAME ); ImGui::SameLine();
                        if ( ImGui::InputText( "##targetName", InputBuf, IM_ARRAYSIZE( InputBuf ) ) )
                        {
                            InputLen = strlen( InputBuf );
                        }
                    }

                    ImGui::Separator();

                    if ( ImGui::Button( MODEL_NO_BUTTON_LABEL, ImVec2( 120, 0 ) ) )
                    {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SetItemDefaultFocus();
                    ImGui::SameLine();
                    if ( showNameInput && InputLen == 0 )
                    {
                        PushReadOnly( true );
                    }
                    if ( ImGui::Button( MODEL_YES_BUTTON_LABEL, ImVec2( 120, 0 ) ) )
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

            if ( ImGui::Begin( "##projectManager", &p_open, flags ) )
            {
                const ImVec2 logoSize = ImVec2( logoImageSize, logoImageSize / divideLogo->_aspectRatio );
                ImGui::Image( (ImTextureID)(intptr_t)divideLogo->_texture, logoSize );
                SetTooltip( COPYRIGHT_NOTICE );
                ImGui::SameLine();

                ImVec2 center = ImGui::GetMainViewport()->GetCenter();
                ImGui::SetNextWindowPos( ImVec2(center.x, 0.f), ImGuiCond_Always, ImVec2( 0.5f, -0.2f ) );
                if ( ImGui::BeginChild( "##options", ImVec2( ImGui::GetContentRegionAvail().x * 0.45f, ImGui::GetContentRegionAvail().y * 0.125f ), ImGuiChildFlags_Border, ImGuiWindowFlags_None ) )
                {
                    ImGui::Text( LAUNCH_CONFIG ); ImGui::SameLine();

                    { // Launch mode: VS, Editor, Game
                        if constexpr ( !WINDOWS_BUILD ) { PushReadOnly( true ); }
                        if (ImGui::RadioButton( g_ideName.c_str(), &launch_mode, 0 )) { OnSelectionChanged(); }
                        if constexpr ( !WINDOWS_BUILD ) { PopReadOnly( ); }

                        ImGui::SameLine();

                        if ( !haveMSVCEditor && !haveClangEditor) { PushReadOnly(true); }
                        if (ImGui::RadioButton( OPERATION_MODES[1], &launch_mode, 1 )) { OnSelectionChanged(); }
                        if ( !haveMSVCEditor && !haveClangEditor) { PopReadOnly(); ImGui::SameLine(); HelpMarker( TOOLTIPS[0] ); }

                        ImGui::SameLine();

                        if ( !haveMSVCGame && !haveClangGame) { PushReadOnly(true); }
                        if (ImGui::RadioButton( OPERATION_MODES[2], &launch_mode, 2 )) { OnSelectionChanged(); }
                        if ( !haveMSVCGame && !haveClangGame) { PopReadOnly(); ImGui::SameLine(); HelpMarker( TOOLTIPS[1] ); }

                    }

                    const bool haveBuilds = haveMSVCBuilds || haveClangBuilds;
                    if ( launch_mode == 0 || !haveBuilds) { PushReadOnly(true); }
                    {
                        { // Toolset for Editor and Game launch modes only
                            ImGui::Text( BUILD_TOOLSET ); ImGui::SameLine();

                            if ( !haveMSVCBuilds ) { PushReadOnly( true ); }
                            if (ImGui::RadioButton( MSVC_NAME, &build_toolset, 0 )) { OnSelectionChanged(); } ImGui::SameLine();
                            if ( !haveMSVCBuilds ) { PopReadOnly(); HelpMarker( TOOLTIPS[2] ); }

                            if ( !haveClangBuilds ) { PushReadOnly(true); }
                            if (ImGui::RadioButton( CLANG_NAME, &build_toolset, 1 )) { OnSelectionChanged(); }
                            if ( !haveClangBuilds ) { PopReadOnly(); ImGui::SameLine(); HelpMarker( TOOLTIPS[3] ); }

                        }

                        { // Build preset for Editor and Game launch modes only
                            ImGui::Text( BUILD_TYPE ); ImGui::SameLine();
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
                                HelpMarker( TOOLTIPS[4] );
                            }
                        }
                    }
                    if ( launch_mode == 0 || !haveBuilds ) { PopReadOnly( ); }

                    ImGui::EndChild();
                }

                const ImVec2 iconSize = ImVec2( iconImageSize, iconImageSize / deleteIcon->_aspectRatio );

                const auto drawActiveButton = [iconSize](const char* id, SDL_Texture* icon, SDL_Texture* iconInv, const bool isHovered, const bool isActive)
                {
                    bool ret = false;
                    
                    auto activeIcon = icon;
                    float sizeFactor = 1.f;

                    if ( isHovered )
                    {
                        if ( isActive )
                        {
                            sizeFactor = 0.9f;
                            const ImVec2 pos = ImGui::GetCursorPos();
                            const ImVec2 diff ={ iconSize.x - iconSize.x * sizeFactor, iconSize.y - iconSize.y * sizeFactor};
                            const ImVec2 spacing = { diff.x * 0.5f, diff.y * 0.5f };
                            ImGui::SetCursorPos( {pos.x + spacing.x, pos.y + spacing.y });
                        }
                        activeIcon = iconInv;
                    }
                    ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.0f, 0.0f, 0.0f, 0.0f ) );
                    ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( 0.0f, 0.0f, 0.0f, 0.0f ) );
                    ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.0f, 0.0f, 0.0f, 0.0f ) );

                    ret = ImGui::ImageButton( id, (ImTextureID)(intptr_t)activeIcon, { iconSize.x * sizeFactor, iconSize.y * sizeFactor } );
                    
                    ImGui::PopStyleColor( 3 );

                    return ret;
                };

                float btnOffset = ImGui::GetContentRegionAvail().x - (iconImageSize * 2.f) - 15.f;
                ImGui::SameLine( btnOffset );

                static bool isSettingsButtonActive = false, isSettingsButtonHovered = false;
                if ( drawActiveButton( "##settings", settingsIcon->_texture, settingsInvIcon->_texture, isSettingsButtonHovered, isSettingsButtonActive ) )
                {
                    g_ideLaunchCommandOriginal = g_ideLaunchCommand;
                    g_ideNamedOriginal = g_ideName;
                    ImGui::OpenPopup( SETTINGS_MODAL_NAME );
                }
                isSettingsButtonActive = ImGui::IsItemActive();
                isSettingsButtonHovered = ImGui::IsItemHovered();

                btnOffset += iconImageSize + 15.f;
                ImGui::SameLine( btnOffset );

                static bool isCloseButtonActive = false, isCloseButtonHovered = false;
                if ( drawActiveButton( "##cancel", closeIcon->_texture, closeInvIcon->_texture, isCloseButtonHovered, isCloseButtonActive ) )
                {
                    p_open = false;
                }
                isCloseButtonActive = ImGui::IsItemActive();
                isCloseButtonHovered = ImGui::IsItemHovered();


                if ( ImGui::BeginPopupModal( SETTINGS_MODAL_NAME, NULL, ImGuiWindowFlags_AlwaysAutoResize ) )
                {
                    ImGui::Text( IDE_FIELD_TITLE_NAME ); ImGui::SameLine();

                    if ( ImGui::InputText( "##targetName", &g_ideName ) )
                    {
                        // TODO: Validate command
                    }

                    ImGui::Text( IDE_FIELD_COMMAND_NAME ); ImGui::SameLine();

                    if ( ImGui::InputText( "##targetCommnad", &g_ideLaunchCommand ) )
                    {
                        // TODO: Validate command
                    }

                    ImGui::Separator();

                    if ( ImGui::Button( MODEL_CLOSE_BUTTON_LABEL, ImVec2( 120, 0 ) ) )
                    {
                        g_ideLaunchCommand = g_ideLaunchCommandOriginal;
                        g_ideName = g_ideNamedOriginal;
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::SetItemDefaultFocus();
                    ImGui::SameLine();
                    if ( g_ideLaunchCommand.empty() || g_ideName.empty() )
                    {
                        PushReadOnly( true );
                    }
                    if ( ImGui::Button( MODEL_SAVE_BUTTON_LABEL, ImVec2( 120, 0 ) ) )
                    {
                        saveConfig();
                        ImGui::CloseCurrentPopup();
                    }
                    if ( g_ideLaunchCommand.empty() || g_ideName.empty() )
                    {
                        PopReadOnly();
                    }

                    ImGui::EndPopup();
                }

                static const float scaleWidth = ImGui::CalcTextSize( "###" ).x;
                
                if (launch_mode == 0)
                {
                    PushReadOnly(true);
                }
                static float button_size = 120.f, button_distance = 15.f;
                ImGui::SetNextWindowPos( center, ImGuiCond_Always, ImVec2( 0.51f, 0.45f ) );
                if (ImGui::BeginChild( "##projectList", ImVec2( ImGui::GetContentRegionAvail().x - scaleWidth * 1.5f, ImGui::GetContentRegionAvail().y * 0.9f ), ImGuiChildFlags_Border, ImGuiWindowFlags_HorizontalScrollbar ))
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
                        if ( SelectedImgButton( NEW_PROJECT_LABEL, (ImTextureID)(intptr_t)newProjectIcon->_texture, { button_size, button_size } ) )
                        {
                            setSelected(nullptr);
                            ImGui::OpenPopup( CREATE_MODAL_NAME );
                        }
                        SetTooltip( TOOLTIPS[5] );
                        CenteredText( NEW_PROJECT_LABEL );
                        EndGroup();

                        if ( ShowYesNoModal( CREATE_MODAL_NAME, CREATE_DESCRIPTION, true ) &&
                            !DuplicateProject(*defaultProject, InputBuf))
                        {
                            ImGui::OpenPopup( CREATE_MODAL_NAME );
                        }
                    }
                    
                    for ( ProjectEntry& project : projects)
                    {
                        const bool sceneListEmpty = project._sceneList.empty();
                        ImGui::BeginGroup();
                        if ( sceneListEmpty )
                        {
                            PushReadOnly(true);
                        }

                        const bool isSelected = &project == selectedProject;
                        if ( SelectedImgButton( project._name.c_str(), (ImTextureID)(intptr_t)project._logo.lock()->_texture, { button_size, button_size }, isSelected ))
                        {
                            g_globalMessage = fmt::format( SELECTED_PROJECT_MSG, project._name );
                            if ( selectedProject != nullptr)
                            {
                                selectedProject->_selected = false;
                            }
                            selectedProject = &project;
                            selectedProject->_selected = true;
                        }

                        if ( sceneListEmpty ) 
                        {
                            SetTooltip( fmt::format( TOOLTIPS[7], project._name ).c_str() );
                        }
                        else
                        {
                            if ( ImGui::IsItemHovered() && ImGui::BeginItemTooltip() )
                            {
                                ImGui::Text(fmt::format( TOOLTIPS[6], project._name, TOOLTIPS[14] ).c_str());
                                for ( const std::string& scene : project._sceneList )
                                {
                                    ImGui::Bullet(); ImGui::Text( scene.c_str() );
                                }
                                ImGui::EndTooltip();
                            }
                        }
                        
                        CenteredText( project._name.c_str());
                        if ( sceneListEmpty )
                        {
                            PopReadOnly();
                        }
                        if ( sceneListEmpty )
                        {
                            ImGui::SameLine();
                            HelpMarker( TOOLTIPS[7] );
                        }
                        EndGroup();
                    }

                    ImGui::EndChild();
                }
                if ( launch_mode == 0 )
                {
                    PopReadOnly();
                }
                ImGui::SameLine();

                ImGui::VSliderFloat( "##iconSize", ImVec2(scaleWidth, ImGui::GetContentRegionAvail().y - 55), &button_size, 100.f, 500.f );
                SetTooltip( TOOLTIPS[8] );

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
                if ( ImGui::BeginChild( "##controls", ImVec2( ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y * 0.975f ), ImGuiChildFlags_None, ImGuiWindowFlags_None ) )
                {
                    const bool isReadOnly = selectedProject == nullptr || selectedProject->_isDefault;
                    if ( isReadOnly )
                    {
                        PushReadOnly( true );
                    }

                    if ( ImGui::ImageButton( "##deleteProject", (ImTextureID)(intptr_t)deleteIcon->_texture, iconSize ) )
                    {
                        ImGui::OpenPopup( DELETE_MODAL_NAME );
                    }
                    SetTooltip( TOOLTIPS[9] );

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

                    if ( ImGui::ImageButton( "##duplicateProject", (ImTextureID)(intptr_t)duplicateIcon->_texture, iconSize ) )
                    {
                        ImGui::OpenPopup( DUPLICATE_MODAL_NAME );
                    }

                    SetTooltip( TOOLTIPS[10] );
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
                    
                    if ( ImGui::BeginChild( "##status", ImVec2( ImGui::GetContentRegionAvail().x * 0.95f, ImGui::GetContentRegionAvail().y * 0.8), ImGuiChildFlags_Border, ImGuiWindowFlags_None ) )
                    {
                        ImGui::Text( g_globalMessage.c_str() );
                        ImGui::EndChild();
                    }
                    SetTooltip(g_globalMessage.c_str());

                    ImGui::SameLine();

                    if ( ImGui::ImageButton( "##launch", (ImTextureID)(intptr_t)launchIcon->_texture, iconSize ) )
                    {
                        assert(selectedProject != nullptr || launch_mode == 0 );

                        switch(launch_mode)
                        {
                            case 0:
                            {
                                 const auto[msg, error] = Startup( g_ideLaunchCommand.c_str(), g_projectPath.string().c_str(), g_projectPath ); 
                                 g_globalMessage = msg;
                                 if (!error)
                                 {
                                     p_open = false;
                                 }
                            } break;
                            case 1: 
                            case 2: 
                            {
                                const auto cmdLine = fmt::format( "--project={}", selectedProject->_name );
                                const auto [msg, error] = Startup( GetExecutablePath( launch_mode, build_toolset, selectedTarget, g_projectPath.string() ).c_str(), cmdLine.c_str(), g_projectPath );
                                g_globalMessage = msg;
                                if ( !error )
                                {
                                    p_open = false;
                                }
                            } break;
                            default: break;
                        }
                    }
                    if ( launch_mode == 0 )
                    {
                        SetTooltip( TOOLTIPS[11] );
                    }
                    else if (launch_mode == 1)
                    {
                        SetTooltip( TOOLTIPS[12] );
                    }
                    else
                    {
                        SetTooltip( TOOLTIPS[13] );
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

    saveConfig();

    // Cleanup
    imageDB.clear();
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer( renderer );
    SDL_DestroyWindow( window );
    SDL_Quit();

	return 0;
}
