// ProjectManager.cpp : Defines the entry point for the application.

#include "ProjectManager.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <regex>
#include <stack>

#include <exception>
#include <optional>
#include <cassert>
#include <memory>
#include <string.h>
#include <thread>

#include <filesystem>

#include <SDL3_image/SDL_image.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_main.h>
#include <fmt/core.h>

#include <boost/property_tree/xml_parser.hpp>

#include <ImGuiMisc/imguistyleserializer/imguistyleserializer.h>
#include <imgui_stdlib.h>

#if defined(IS_WINDOWS_BUILD)
constexpr bool WINDOWS_BUILD = true;

#define _WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

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
    const HINSTANCE ret = ShellExecute( NULL, "open", lpApplicationName, params, workingDir.string().c_str() , SW_SHOWNORMAL );
    if (Divide::to_I32(reinterpret_cast<uintptr_t>(ret)) <= 32)
    {
        return {fmt::format( ERRORS[0], lpApplicationName, GetLastErrorAsString() ), true };
    }
    
    return {fmt::format( LAUNCH_MSG, lpApplicationName ), false };
}

#else //IS_WINDOWS_BUILD
constexpr const char* OS_PREFIX = "Linux";
constexpr bool WINDOWS_BUILD = false;
std::pair<std::string, bool> Startup( [[maybe_unused]] const char* lpApplicationName, [[maybe_unused]] const char* params, [[maybe_unused]] const std::filesystem::path& workingDir )
{
    return {"Not implemented!", false};
}
#endif //IS_WINDOWS_BUILD

struct Image
{
    explicit Image( const std::filesystem::path& path, SDL_Renderer* renderer );
    ~Image();

    const std::filesystem::path _path;
    SDL_Surface* _surface = nullptr;
    SDL_Texture* _texture = nullptr;
    int _width = 1, _height = 1;
    Sint64 _access = 0;
    SDL_PixelFormat _format;
    float _aspectRatio = 1.f;

    bool operator==( const Image& other ) const noexcept;
};

using Image_ptr = std::shared_ptr<Image>;
using ImageDB = std::vector<Image_ptr>;

struct ProjectEntry
{
    std::string _name;
    std::string _path;
    bool _selected{ false };
    bool _isDefault{ false };
    std::weak_ptr<Image> _logo;
    std::string _logoName{ DEFAULT_ICON_NAME };
    std::string _logoPath;
    std::vector<std::string> _sceneList;
};

using ProjectDB = std::vector<ProjectEntry>;

template<typename T>
static T* SDL_ASSERT( T* object)
{
    if (object == nullptr)
    {
        printf("%s", fmt::format(ERRORS[9], SDL_GetError()).c_str());
        assert(false);
    }

    return object;
}

static void SDL_ASSERT( const bool condition )
{
    if ( !condition )
    {
        printf("%s", fmt::format(ERRORS[9], SDL_GetError()).c_str());
        assert(false);
    }
}

Image::Image( const std::filesystem::path& path, SDL_Renderer* renderer )
    : _path( path )
{
    _surface = SDL_ASSERT(IMG_Load( path.string().c_str() ));  
    _texture = SDL_ASSERT(SDL_CreateTextureFromSurface( renderer, _surface ));

    float w, h;
    SDL_ASSERT(SDL_GetTextureSize(_texture, &w, &h));
    _width = static_cast<int>(std::floor(w));
    _height = static_cast<int>(std::floor(h));
    SDL_PropertiesID properties = SDL_GetTextureProperties(_texture);
    _aspectRatio = _width / float( _height );
    _format = (SDL_PixelFormat)SDL_GetNumberProperty(properties, SDL_PROP_TEXTURE_FORMAT_NUMBER, 0);
    _access = SDL_GetNumberProperty(properties, SDL_PROP_TEXTURE_ACCESS_NUMBER, 0);
    
}

Image::~Image()
{
    SDL_DestroyTexture( _texture );
    SDL_DestroySurface( _surface );
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
        g_globalMessage = fmt::format( ERRORS[1], search_path.string() );
    }

    return std::nullopt;
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

    return imageDB.emplace_back( std::make_shared<Image>( targetPath, renderer ) );
}

static boost::property_tree::iptree GetXmlTree(std::filesystem::path filePath)
{
    boost::property_tree::iptree XmlTree;
    try
    {
        boost::property_tree::read_xml( filePath.string(), XmlTree );
    }
    catch ( boost::property_tree::xml_parser_error& e )
    {
        g_globalMessage = fmt::format( ERRORS[7], filePath.string(), e.what() );
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
        g_globalMessage = fmt::format( ERRORS[8], (g_projectPath / PROJECT_MANAGER_FOLDER_NAME / MANAGER_CONFIG_FILE_NAME).string(), e.what() );
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
                 iter->path().filename().string().compare( Divide::Config::DELETED_FOLDER_NAME ) == 0 )
            {
                continue;
            }

            ProjectEntry& entry = projects.emplace_back();
            entry._name = iter->path().filename().string();
            entry._path = iter->path().parent_path().string();
            entry._isDefault = entry._name.compare( Divide::Config::DEFAULT_PROJECT_NAME ) == 0;
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
        g_globalMessage = fmt::format( ERRORS[2], (g_projectPath / PROJECTS_FOLDER_NAME).string(), e.what() );
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
    SDL_ASSERT(SDL_Init( SDL_INIT_VIDEO | SDL_INIT_GAMEPAD) );

    SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "1" );

    // Create window with SDL_Renderer graphics context
    SDL_Window* window = SDL_ASSERT(SDL_CreateWindow( WINDOW_TITLE, 1280, 720, SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIGH_PIXEL_DENSITY));
    SDL_Renderer* renderer = SDL_ASSERT(SDL_CreateRenderer( window, nullptr));

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    ImGui::ResetStyle( ImGuiStyle_Dracula );
    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForSDLRenderer( window, renderer );
    ImGui_ImplSDLRenderer3_Init( renderer );

    constexpr uint8_t iconImageSize = 32u;
    constexpr uint8_t logoImageSize = 96u;
    //constexpr uint8_t projectImageSize = 128u;

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

    LaunchMode launch_mode = LaunchMode::IDE;

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
            ImGui_ImplSDL3_ProcessEvent( &event );
            switch (event.type)
            {
                case SDL_EVENT_QUIT: done = true; break;
                case SDL_EVENT_KEY_UP:
                {
                    if ( event.key.key == SDLK_ESCAPE)
                    {
                        done = true;
                    }
                } break;
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                {
                    if (event.window.windowID == SDL_GetWindowID( window ) )
                    {
                        done = true;
                    }
                } break;
                default: break;
            }
        }

        // Start the Dear ImGui frame
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        bool p_open = true;
        {
            static char InputBuf[256];
            static size_t InputLen = 0u;

            const auto DuplicateProject = [&projects, setSelected, OnProjectsUpdated]( const ProjectEntry srcProject, const char* targetProjectName )
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
                        ImGui::BeginDisabled();
                    }
                    if ( ImGui::Button( MODEL_YES_BUTTON_LABEL, ImVec2( 120, 0 ) ) )
                    {
                        confirmed = true;
                        ImGui::CloseCurrentPopup();
                    }
                    if ( showNameInput && InputLen == 0 )
                    {
                        ImGui::EndDisabled();
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

                if ( ImGui::BeginChild( "##options", ImVec2( ImGui::GetContentRegionAvail().x * 0.45f, ImGui::GetContentRegionAvail().y * 0.125f ), ImGuiChildFlags_Borders, ImGuiWindowFlags_None ) )
                {
                    int mode = static_cast<int>(launch_mode);

                    ImGui::Text( LAUNCH_CONFIG ); ImGui::SameLine();

                    { // Launch mode: VS, Editor, Game
                        if constexpr ( !WINDOWS_BUILD ) { ImGui::BeginDisabled(); }
                        ImGui::RadioButton( g_ideName.c_str(), &mode, 0 );
                        if constexpr ( !WINDOWS_BUILD ) { ImGui::EndDisabled(); }

                        ImGui::SameLine();

                        ImGui::RadioButton( OPERATION_MODES[1], &mode, 1 );

                        ImGui::SameLine();

                        ImGui::RadioButton( OPERATION_MODES[2], &mode, 2 );
                    }

                    launch_mode = static_cast<LaunchMode>(mode);

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
                        ImGui::BeginDisabled();
                    }
                    if ( ImGui::Button( MODEL_SAVE_BUTTON_LABEL, ImVec2( 120, 0 ) ) )
                    {
                        saveConfig();
                        ImGui::CloseCurrentPopup();
                    }
                    if ( g_ideLaunchCommand.empty() || g_ideName.empty() )
                    {
                        ImGui::EndDisabled();
                    }

                    ImGui::EndPopup();
                }

                static const float scaleWidth = ImGui::CalcTextSize( "###" ).x;
                
                if (launch_mode == 0)
                {
                    ImGui::BeginDisabled();
                }
                static float button_size = 120.f, button_distance = 15.f;
                ImGui::SetNextWindowPos( center, ImGuiCond_Always, ImVec2( 0.51f, 0.45f ) );
                if (ImGui::BeginChild( "##projectList", ImVec2( ImGui::GetContentRegionAvail().x - scaleWidth * 1.5f, ImGui::GetContentRegionAvail().y * 0.9f ), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar ))
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
                            ImGui::BeginDisabled();
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
                            ImGui::EndDisabled();
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
                    ImGui::EndDisabled();
                }
                ImGui::SameLine();

                ImGui::VSliderFloat( "##iconSize", ImVec2(scaleWidth, ImGui::GetContentRegionAvail().y - 55), &button_size, 100.f, 500.f );
                SetTooltip( TOOLTIPS[8] );

                ImGui::SetNextWindowPos( ImVec2(center.x, ImGui::GetCursorPosY() ), ImGuiCond_Always, ImVec2( 0.495f, 0.f ) );
                if ( ImGui::BeginChild( "##controls", ImVec2( ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y * 0.975f ), ImGuiChildFlags_None, ImGuiWindowFlags_None ) )
                {
                    const bool isReadOnly = selectedProject == nullptr || selectedProject->_isDefault;
                    if ( isReadOnly )
                    {
                        ImGui::BeginDisabled();
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
                        const std::filesystem::path deletedProjectPath = srcPath / Divide::Config::DELETED_FOLDER_NAME;

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
                        ImGui::EndDisabled();
                    }

                    ImGui::SameLine();
                    
                    if ( ImGui::BeginChild( "##status", ImVec2( ImGui::GetContentRegionAvail().x * 0.95f, ImGui::GetContentRegionAvail().y * 0.8), ImGuiChildFlags_Borders, ImGuiWindowFlags_None ) )
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
                            case LaunchMode::IDE:
                            {
                                 const auto[msg, error] = Startup( g_ideLaunchCommand.c_str(), g_projectPath.string().c_str(), g_projectPath ); 
                                 g_globalMessage = msg;
                                 if (!error)
                                 {
                                     p_open = false;
                                 }
                            } break;
                            case LaunchMode::GAME:
                            case LaunchMode::EDITOR:
                            {
                                auto cmdLine = fmt::format( "--project={} {}", selectedProject->_name, launch_mode == LaunchMode::EDITOR ? "" : "--disableEditor" );
                                const auto [msg, error] = Startup( "Divide-Framework.exe", cmdLine.c_str(), g_projectPath );
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
        SDL_SetRenderScale( renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y );
        SDL_SetRenderDrawColor( renderer, (Uint8)(clear_color.x * 255), (Uint8)(clear_color.y * 255), (Uint8)(clear_color.z * 255), (Uint8)(clear_color.w * 255) );
        SDL_RenderClear( renderer );
        ImGui_ImplSDLRenderer3_RenderDrawData( ImGui::GetDrawData(), renderer );
        SDL_ASSERT(SDL_RenderPresent( renderer ));
    }

    saveConfig();

    // Cleanup
    imageDB.clear();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer( renderer );
    SDL_DestroyWindow( window );
    SDL_Quit();

	return 0;
}
