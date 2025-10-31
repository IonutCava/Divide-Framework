

#include "Headers/ContentExplorerWindow.h"

#include "Editor/Headers/Editor.h"
#include "Core/Headers/Kernel.h"
#include "Managers/Headers/ProjectManager.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Rendering/Camera/Headers/Camera.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Geometry/Shapes/Headers/Mesh.h"

#include <filesystem>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

namespace Divide {
    namespace {
        constexpr const char* g_extensions[] = {
            "glsl", "cmn", "frag", "vert", "cmp", "geom", "tesc", "tese",  //Shaders
            "ogg", "wav", //Sounds
            "chai", //Scripts
            "layout", "looknfeel", "scheme", "xsd", "imageset", "xcf", "txt", "anims",  //CEGUI
            "ttf", "font", //Fonts
            "mtl", "md5anim", "bvh",  //Geometry support
            "xml" //General
        };

        constexpr const char* g_imageExtensions[] = {
            "png", "jpg", "jpeg", "tga", "raw", "dds"
        };

        constexpr const char* g_soundExtensions[] = {
           "wav", "ogg", "mp3", "mid"
        };

        constexpr const char* g_shaderExtensions[] = {
           "glsl", "vert", "frag", "geom", "comp", "cmn", "tesc", "tese"
        };

        bool IsValidFile(const ResourcePath& name)
        {
            for (const char* extension : g_extensions)
            {
                if (hasExtension(name, extension))
                {
                    return true;
                }
            }
            for (const char* extension : g_geometryExtensions)
            {
                if (hasExtension(name, extension))
                {
                    return true;
                }
            }
            for (const char* extension : g_imageExtensions)
            {
                if (hasExtension(name, extension))
                {
                    return true;
                }
            } 
            for (const char* extension : g_soundExtensions)
            {
                if (hasExtension(name, extension))
                {
                    return true;
                }
            }

            return false;
        }
    }

    ContentExplorerWindow::ContentExplorerWindow(Editor& parent, const Descriptor& descriptor)
        : DockedWindow(parent, descriptor)
    {
    }

    ContentExplorerWindow::~ContentExplorerWindow()
    {
        DestroyResource( _fileIcon );
        DestroyResource( _soundIcon );
        DestroyResource( _shaderIcon );
        for ( Handle<Texture>& icon : _geometryIcons )
        {
            DestroyResource( icon );
        }
        for ( auto& icon : _loadedTextures)
        {
            DestroyResource( icon.second );
        }
        DestroyResource(_previewTexture);
    }

    void ContentExplorerWindow::init()
    {
        _currentDirectories.resize(2);

        getDirectoryStructureForPath(Paths::g_assetsLocation, _currentDirectories[0]);
        _currentDirectories[0]._name = Paths::g_assetsLocation;

        getDirectoryStructureForPath(Paths::g_xmlDataLocation, _currentDirectories[1]);
        _currentDirectories[1]._name = Paths::g_xmlDataLocation;

        _fileIcon   = getTextureForPath(Paths::g_iconsPath, "file_icon.png");
        _soundIcon  = getTextureForPath( Paths::g_iconsPath, "sound_icon.png");
        _shaderIcon = getTextureForPath( Paths::g_iconsPath, "shader_icon.png");

        _geometryIconNames[to_base(GeometryFormat::_3DS)]     = "3ds_icon.png";
        _geometryIconNames[to_base(GeometryFormat::ASE)]      = "ase_icon.png";
        _geometryIconNames[to_base(GeometryFormat::FBX)]      = "fbx_icon.png";
        _geometryIconNames[to_base(GeometryFormat::MD2)]      = "md2_icon.png";
        _geometryIconNames[to_base(GeometryFormat::MD5)]      = "md5_icon.png";
        _geometryIconNames[to_base(GeometryFormat::OBJ)]      = "obj_icon.png";
        _geometryIconNames[to_base(GeometryFormat::DAE)]      = "collada.png";
        _geometryIconNames[to_base(GeometryFormat::GLTF)]     = "gltf.png";
        _geometryIconNames[to_base(GeometryFormat::X)]        = "x_icon.png";
        _geometryIconNames[to_base(GeometryFormat::DVD_ANIM)] = "divide.png";
        _geometryIconNames[to_base(GeometryFormat::DVD_GEOM)] = "divide.png";
        _geometryIconNames[to_base(GeometryFormat::COUNT)]    = "file_icon.png";

        for (U8 i = 0u; i < to_U8(GeometryFormat::COUNT) + 1u; ++i)
        {
            _geometryIcons[i] = getTextureForPath(Paths::g_iconsPath, _geometryIconNames[i]);
        }
    }

    void ContentExplorerWindow::update([[maybe_unused]] const U64 deltaTimeUS) {

        // One per frame to avoid massive stutters.
        if (!_textureLoadQueue.empty())
        {
            const EditorFileEntry entry = _textureLoadQueue.top();
            _textureLoadQueue.pop();
            _loadedTextures[_ID((entry._path / entry._file._path).string())] = getTextureForPath( entry._path, entry._file._path.string());
        }

        _textureLoadQueueLocked = false;
    }

    void ContentExplorerWindow::getDirectoryStructureForPath(const ResourcePath& directoryPath, Directory& directoryOut) const
    {
        const std::filesystem::path p(directoryPath.string());
        if ( std::filesystem::is_directory(p))
        {
            directoryOut._name = getTopLevelFolderName(directoryPath);
            directoryOut._path = directoryPath;

            for (auto&& x : std::filesystem::directory_iterator(p))
            {
                if (std::filesystem::is_regular_file(x.path()))
                {
                    if (IsValidFile(ResourcePath { x.path().string() }))
                    {
                        const ResourcePath filename{ x.path().filename().string() };
                        directoryOut._files.emplace_back(filename, getExtension(filename).substr(1).c_str());

                    }
                }
                else if (std::filesystem::is_directory(x.path()))
                {
                    auto& childDirectory = directoryOut._children.emplace_back(std::make_unique<Directory>());
                    getDirectoryStructureForPath(ResourcePath(x.path().string()), *childDirectory);
                }
            }
        }
    }

    void ContentExplorerWindow::printDirectoryStructure(const Directory& dir, const bool open) const
    {
        ImGuiTreeNodeFlags nodeFlags = (open ? ImGuiTreeNodeFlags_DefaultOpen : 0);

        if (dir._children.empty()) {
            nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        if (ImGui::TreeNodeEx(dir._name.string().c_str(), nodeFlags))
        {
            if (ImGui::IsItemClicked() || ImGui::IsItemToggledOpen()) {
                _selectedDir = &dir;
            }

            for (const auto& childDirectory : dir._children) {
                printDirectoryStructure(*childDirectory, false);
            }
       
            if (!dir._children.empty()) {
                ImGui::TreePop();
            }
        }
    }

    void ContentExplorerWindow::drawInternal() {
        PROFILE_SCOPE_AUTO(Profiler::Category::GUI);

        static bool previewTexture = false;

        const ImGuiContext& imguiContext = Attorney::EditorGeneralWidget::getImGuiContext(_parent, Editor::ImGuiContextType::Editor);

        const auto isSoundFile = [](const char* extension) {
            for (const char* ext : g_soundExtensions) {
                if (Util::CompareIgnoreCase(extension, ext)) {
                    return true;
                }
            }

            return false;
        }; 
        
        const auto isShaderFile = [](const char* extension) {
            for (const char* ext : g_shaderExtensions) {
                if (Util::CompareIgnoreCase(extension, ext)) {
                    return true;
                }
            }

            return false;
        };
        
        const auto openFileInEditor = [&](const ResourcePath& path, const File& file) {
            const ResourcePath& textEditor = Attorney::EditorGeneralWidget::externalTextEditorPath(_parent);

            if (textEditor.empty())
            {
                Attorney::EditorGeneralWidget::showStatusMessage(_parent, "ERROR: No text editor specified!", Time::SecondsToMilliseconds<F32>(3), true);
            }
            else
            {
                if (openFile(textEditor.string(), path, file._path.string()) != FileError::NONE)
                {
                    Attorney::EditorGeneralWidget::showStatusMessage(_parent, "ERROR: Couldn't open specified source file!", Time::SecondsToMilliseconds<F32>(3), true);
                }
            }
        };

        constexpr U8 buttonSize = 64u;

        const bool flipImages = !ImageTools::UseUpperLeftOrigin();
        const ImVec2 uv0{ 0.f, flipImages ? 1.f : 0.f };
        const ImVec2 uv1{ 1.f, flipImages ? 0.f : 1.f };

        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);

        {
            ImGui::BeginChild("Folders", ImVec2(ImGui::GetContentRegionAvail().x * 0.3f, -1), true, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_HorizontalScrollbar);
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("Menu")) {
                    ImGui::MenuItem("Refresh", nullptr, nullptr, false);
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }

            for (const Directory& dir : _currentDirectories) {
                printDirectoryStructure(dir, true);
            }

            ImGui::EndChild();
        }
        ImGui::SameLine();
        {
            ImGui::BeginChild("Contents", ImVec2(0, -1), true, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_HorizontalScrollbar);
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("Menu")) {
                    ImGui::MenuItem("Refresh", nullptr, nullptr, false);
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }

            if (_selectedDir != nullptr)
            {
                ImGui::Columns(CLAMPED(to_I32(_selectedDir->_files.size()), 1, 4));
                bool lockTextureQueue = false;

                size_t imageButtonIndex = 0u;
                for (const auto& file : _selectedDir->_files)
                {
                    Handle<Texture> tex = INVALID_HANDLE<Texture>;
                    { // Textures
                        for (const char* extension : g_imageExtensions)
                        {
                            if (Util::CompareIgnoreCase(file._extension.c_str(), extension))
                            {
                                const string path = (_selectedDir->_path / file._path).string();

                                const auto it = _loadedTextures.find(_ID(path));
                                if (it == std::cend(_loadedTextures) || it->second == INVALID_HANDLE<Texture>)
                                {
                                    if (!_textureLoadQueueLocked)
                                    {
                                        _textureLoadQueue.push(EditorFileEntry(_selectedDir->_path, file));
                                        lockTextureQueue = true;
                                    }
                                }
                                else if (Get(it->second)->getState() == ResourceState::RES_LOADED)
                                {
                                    tex = it->second;
                                }
                                break;
                            }
                        }
                    }

                    ImGui::PushID( _selectedDir->_path.fileSystemPath().c_str() );

                    const GeometryFormat format = tex != INVALID_HANDLE<Texture> ? GeometryFormat::COUNT : GetGeometryFormatForExtension(file._extension.c_str());

                    bool hasTooltip = false;
                    if (tex != INVALID_HANDLE<Texture>)
                    {
                        const U16 w = Get(tex)->width();
                        const U16 h = Get(tex)->height();
                        const F32 aspect = w / to_F32(h);
                    
                        if (ImGui::ImageButton(Util::StringFormat("{}_{}", Get(tex)->resourceName(), imageButtonIndex++).c_str(), to_TexID(tex), ImVec2(buttonSize, buttonSize / aspect), uv0, uv1))
                        {
                            DestroyResource( _previewTexture );
                            _previewTexture = tex;
                            previewTexture = true;
                        }
                    }
                    else if (format != GeometryFormat::COUNT)
                    {
                        const Handle<Texture> icon = _geometryIcons[to_base(format)];
                        const U16 w = Get(icon)->width();
                        const U16 h = Get(icon)->height();
                        const F32 aspect = w / to_F32(h);

                        const bool modifierPressed = imguiContext.IO.KeyShift;
                        const ImVec4 bgColour(modifierPressed ? 1.f : 0.f, 0.f, 0.f, modifierPressed ? 1.f : 0.f);
                        if (ImGui::ImageButton(Util::StringFormat("{}_{}", _geometryIconNames[to_base(format)], imageButtonIndex++).c_str(), to_TexID(icon), ImVec2(buttonSize, buttonSize / aspect), uv0, uv1, bgColour, ImVec4(1, 1, 1, 1)))
                        {
                            const Handle<Mesh> spawnMesh = getModelForPath(_selectedDir->_path, file._path.string());
                            if ( spawnMesh == INVALID_HANDLE<Mesh>)
                            {
                                Attorney::EditorGeneralWidget::showStatusMessage(_parent, "ERROR: Couldn't load specified mesh!", Time::SecondsToMilliseconds<F32>(3), true);
                            }
                            else
                            {
                                if (!Attorney::EditorGeneralWidget::modalModelSpawn(_parent, spawnMesh, !imguiContext.IO.KeyShift, VECTOR3_UNIT, VECTOR3_ZERO))
                                {
                                    Attorney::EditorGeneralWidget::showStatusMessage(_parent, "ERROR: Couldn't spoawn specified mesh!", Time::SecondsToMilliseconds<F32>(3), true);
                                }
                            }
                        }
                        hasTooltip = true;
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("Hold down [Shift] to spawn directly at the camera position");
                        }
                    }
                    else if (isSoundFile(file._extension.c_str()))
                    {
                        const U16 w = Get(_soundIcon)->width();
                        const U16 h = Get(_soundIcon)->height();
                        const F32 aspect = w / to_F32(h);

                        if (ImGui::ImageButton(Util::StringFormat("{}_{}", Get(_soundIcon)->resourceName(), imageButtonIndex++).c_str(), to_TexID(_soundIcon), ImVec2(buttonSize, buttonSize / aspect), uv0, uv1))
                        {
                            //ToDo: Play sound file -Ionut
                        }
                    } else if (isShaderFile(file._extension.c_str())) {
                        const U16 w = Get(_shaderIcon)->width();
                        const U16 h = Get(_shaderIcon)->height();
                        const F32 aspect = w / to_F32(h);

                        if (ImGui::ImageButton(Util::StringFormat("{}_{}", Get(_shaderIcon)->resourceName(), imageButtonIndex++).c_str(), to_TexID(_shaderIcon), ImVec2(buttonSize, buttonSize / aspect), uv0, uv1))
                        {
                            openFileInEditor(_selectedDir->_path, file);
                        }
                    }
                    else
                    {
                        const U16 w = Get(_fileIcon)->width();
                        const U16 h = Get(_fileIcon)->height();
                        const F32 aspect = w / to_F32(h);

                        if (ImGui::ImageButton(Util::StringFormat("{}_{}", Get(_fileIcon)->resourceName(), imageButtonIndex++).c_str(), to_TexID(_fileIcon), ImVec2(buttonSize, buttonSize / aspect), uv0, uv1))
                        {
                            openFileInEditor( _selectedDir->_path, file);
                        }
                    }

                    static string pathString = "";

                    pathString = file._path.string();
                    if (!hasTooltip && ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip( pathString.c_str() );
                    }

                    ImGui::Text( pathString.c_str() );

                    ImGui::PopID();
                    ImGui::NextColumn();
                    if (lockTextureQueue)
                    {
                        _textureLoadQueueLocked = true;
                    }
                }
            }
            ImGui::EndChild();
        }

        if ( previewTexture && Attorney::EditorGeneralWidget::modalTextureView(_parent, "Image Preview", _previewTexture, float2(512, 512), true, true))
        {
            previewTexture = false;
        }

        ImGui::PopStyleVar();
    }

    Handle<Texture> ContentExplorerWindow::getTextureForPath(const ResourcePath& texturePath, const std::string_view textureName) const
    {
        ResourceDescriptor<Texture> textureResource(textureName);
        textureResource.assetName( textureName );
        textureResource.assetLocation(texturePath);

        TextureDescriptor& descriptor = textureResource._propertyDescriptor;
        descriptor._textureOptions._useDDSCache = false;

        return CreateResource(textureResource);
    }

    Handle<Mesh> ContentExplorerWindow::getModelForPath(const ResourcePath& modelPath, const std::string_view modelName) const
    {
        ResourceDescriptor<Mesh> model(modelName);
        model.assetLocation(modelPath);
        model.assetName( modelName );
        model.flag(true);

        return CreateResource(model);
    }
} //namespace Divide
