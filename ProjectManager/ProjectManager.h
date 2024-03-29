/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef DVD_PROJECTMANAGER_H
#define DVD_PROJECTMANAGER_H

#include <cstdint>
#include <array>
#include <string>
#include <vector>
#include <filesystem>

struct SDL_Texture;
struct SDL_Surface;
struct SDL_Renderer;

constexpr const char* COPYRIGHT_NOTICE = "Copyright (c) 2018 DIVIDE-Studio\nCopyright (c) 2009 Ionut Cava";

constexpr const char* WINDOW_TITLE = "Divide-Framework project manager";
constexpr const char* CLANG_PREFIX = "clang";
constexpr const char* CLANG_NAME = "Clang";
constexpr const char* MSVC_PREFIX = "msvc";
constexpr const char* MSVC_NAME = "MSVC";

//@ToDo(Ionut): Save these to a config file
#if defined(IS_WINDOWS_BUILD)
constexpr const char* EDITOR_NAME = "Visual Studio";
constexpr const char* EDITOR_LAUNCH_COMMAND = "devenv.exe";
#else //IS_WINDOWS_BUILD
constexpr const char* EDITOR_NAME = "Visual Studio Code";
constexpr const char* EDITOR_LAUNCH_COMMAND = "code";
#endif //IS_WINDOWS_BUILD

constexpr const char* IDE_FIELD_NAME = "IDE Launch command:";

constexpr const char* LAUNCH_MSG = "Launching application: {}";
constexpr const char* CURRENT_DIR_MSG = "Current working dir: {}";
constexpr const char* SELECTED_PROJECT_MSG = "Selected project [ {} ]";

constexpr const char* ICONS_INV_FOLDER_NAME = "Inverse";
constexpr const char* ASSESTS_FOLDER_NAME = "Assets";
constexpr const char* ICONS_FOLDER_NAME = "Icons";
constexpr const char* BUILD_FOLDER_NAME = "Build";
constexpr const char* DELETED_FOLDER_NAME = "Deleted";
constexpr const char* PROJECTS_FOLDER_NAME = "Projects";
constexpr const char* SCENES_FOLDER_NAME = "Scenes";
constexpr const char* DEFAULT_PROJECT_NAME = "Default";
constexpr const char* DELETE_MODAL_NAME = "Delete selected project?";
constexpr const char* DUPLICATE_MODAL_NAME = "Duplicate existing project?";
constexpr const char* CREATE_MODAL_NAME = "Create new project?";
constexpr const char* SETTINGS_MODAL_NAME = "Settings";
constexpr const char* MODEL_NO_BUTTON_LABEL = "Cancel";
constexpr const char* MODEL_YES_BUTTON_LABEL = "YES!";
constexpr const char* MODEL_CLOSE_BUTTON_LABEL = "Close";
constexpr const char* MODEL_SAVE_BUTTON_LABEL = "Save";
constexpr const char* PROJECT_CONFIG_FILE_NAME = "config.xml";

constexpr const char* CONFIG_LOGO_TAG = "logo";

//Source: game-icons.net
constexpr const char* ICONS[] =
{
    /*0*/ "cardboard-box-closed.png",
    /*1*/ "divide.png",
    /*2*/ "trash-can.png",
    /*3*/ "checkbox-tree.png",
    /*4*/ "play-button.png",
    /*5*/ "cancel.png",
    /*6*/ "auto-repair.png"
};

constexpr const char* OPERATION_MODES[] = 
{
    /*0*/ EDITOR_NAME,
    /*1*/ "Editor Mode",
    /*2*/ "Game Mode"
};

constexpr const char* LAUNCH_CONFIG = "Launch Config:";
constexpr const char* BUILD_TOOLSET = "Build Toolset:";
constexpr const char* BUILD_TYPE = "Build Type:";

constexpr const char* TARGET_PROJECT_NAME = "Target project name:";

constexpr const char* CREATE_DESCRIPTION = "Are you sure you want to create a new project?";
constexpr const char* DELETE_DESCRIPTION = "Are you sure you want to delete the following project : [{}] ?";
constexpr const char* DUPLICATE_DESCRIPTION = "Are you sure you want to duplicate the following project: [ {} ]?\nAll scenes and assets will be copied across!";

constexpr const char* DUPLICATE_ENTRY_ERROR = "Failed to create new project! [ {} ] already exists!";

constexpr const char* DEFAULT_ICON_NAME = "box-unpacking.png";
constexpr const char* NEW_PROJECT_LABEL = "New Project";

constexpr const char* TOOLTIPS[] = 
{
    /*00*/ "No prebuild editor executables detected! Build a configuration first!",
    /*01*/ "No prebuild game executables detected! Build a configuration first!",
    /*02*/ "No prebuild MSVC executables detected! Build a MSVC configuration first!",
    /*03*/ "No prebuild Clang executables detected! Build a Clang configuration first!",
    /*04*/ "Some build types are not available for this toolset!",
    /*05*/ "Create a new project.\nThis will automatically add a default empty scene to the project!",
    /*06*/ "Open project [ {} ]\n{}",
    /*07*/ "This project [ {} ] is invalid as it doesn't contain any scenes!",
    /*08*/ "Change the size of the icons in the project list",
    /*09*/ "Delete selected project",
    /*10*/ "Duplicate selected project",
    /*11*/ "Open the Divide-Framework source code in the chosen IDE.",
    /*12*/ "Launch the selected project in the editor.",
    /*13*/ "Play the selected project.",
    /*14*/ "\nScene list: \n"
};

constexpr const char* ERRORS[] = 
{
    /*0*/ "Error: ShellExecute( {} ) failed:\n{}",
    /*1*/ "Error finding directory: {}",
    /*2*/ "Error listing project directory: {}.\n[ {} ]",
    /*3*/ "Error: SDL Init failed. %s\n",
    /*4*/ "Error: SDL_CreateWindow(): %s\n",
    /*5*/ "Error creating SDL_Renderer: %s\n",
    /*6*/ "Failed to duplicate project [ {} ]\n{}",
};

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
    using Builds = std::array<bool, uint8_t( BuildType::COUNT )>;
    using Targets = std::array<Builds, uint8_t( BuildTarget::COUNT )>;

    std::string _toolchainName;
    Targets _targets;
};

struct Image
{
    explicit Image( const std::filesystem::path& path, SDL_Renderer* renderer );
    ~Image();

    const std::filesystem::path _path;
    SDL_Surface* _surface = nullptr;
    SDL_Texture* _texture = nullptr;
    int _width = 1, _height = 1, _access = 0;
    uint32_t _format = 0u;
    float _aspectRatio = 1.f;

    bool operator==(const Image& other) const noexcept;
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


#endif //DVD_PROJECTMANAGER_H
