#pragma once
#ifndef DVD_PROJECTMANAGER_H
#define DVD_PROJECTMANAGER_H

#include <cstdint>
#include <array>
#include <string>
#include <filesystem>

struct SDL_Texture;
struct SDL_Surface;
struct SDL_Renderer;

constexpr const char* CLANG_PREFIX = "clang";
constexpr const char* MSVC_PREFIX = "msvc";
constexpr const char* BUILD_FOLDER_NAME = "Build";
constexpr const char* DELETED_FOLDER_NAME = "Deleted";
constexpr const char* PROJECTS_FOLDER_NAME = "Projects";
constexpr const char* DEFAULT_PROJECT_NAME = "Default";
constexpr const char* DELETE_MODAL_NAME = "Delete selected project?";
constexpr const char* DUPLICATE_MODAL_NAME = "Duplicate existing project?";
constexpr const char* CREATE_MODAL_NAME = "Create new project?";

constexpr const char* CREATE_DESCRIPTION = "Are you sure you want to create a new project?";
constexpr const char* DELETE_DESCRIPTION = "Are you sure you want to delete the following project : [{}] ?";
constexpr const char* DUPLICATE_DESCRIPTION = "Are you sure you want to duplicate the following project: [ {} ]?\nAll scenes and assets will be copied across!";

constexpr const char* MISSING_DIRECTORY_ERROR = "Error finding directory: {}";
constexpr const char* DUPLICATE_ENTRY_ERROR = "Failed to create new project! [ {} ] already exists!";

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

    SDL_Surface* _surface = nullptr;
    SDL_Texture* _texture = nullptr;
    int _width = 1, _height = 1, _access = 0;
    uint32_t _format = 0u;
    float _aspectRatio = 1.f;
};

struct ProjectEntry
{
    std::string _name;
    std::string _path;
    bool _selected{ false };
    bool _isDefault{ false };
};

#endif //DVD_PROJECTMANAGER_H
