#include "stdafx.h"

#include "Headers/ContentExplorerWindow.h"

#include "Editor/Headers/Editor.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Geometry/Shapes/Headers/Mesh.h"

#include <filesystem>
#include <imgui_internal.h>

namespace Divide {
    namespace {
        constexpr char* const g_extensions[] = {
            "glsl", "cmn", "frag", "vert", "cmp", "geom", "tesc", "tese",  //Shaders
            "ogg", "wav", //Sounds
            "chai", //Scripts
            "layout", "looknfeel", "scheme", "xsd", "imageset", "xcf", "txt", "anims",  //CEGUI
            "ttf", "font", //Fonts
            "mtl", "md5anim", "bvh",  //Geometry support
            "xml" //General
        };

        constexpr char* const g_imageExtensions[] = {
            "png", "jpg", "jpeg", "tga", "raw", "dds"
        };

        constexpr char* const g_soundExtensions[] = {
           "wav", "ogg", "mp3", "mid"
        };

        constexpr char* const g_shaderExtensions[] = {
           "glsl", "vert", "frag", "geom", "comp", "cmn", "tesc", "tese"
        };

        bool IsValidFile(const char* name) {
            for (const char* extension : g_extensions) {
                if (hasExtension(name, extension)) {
                    return true;
                }
            }
            for (const char* extension : g_geometryExtensions) {
                if (hasExtension(name, extension)) {
                    return true;
                }
            }
            for (const char* extension : g_imageExtensions) {
                if (hasExtension(name, extension)) {
                    return true;
                }
            } 
            for (const char* extension : g_soundExtensions) {
                if (hasExtension(name, extension)) {
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

    void ContentExplorerWindow::init() {
        _currentDirectories.resize(2);

        getDirectoryStructureForPath(Paths::g_assetsLocation, _currentDirectories[0]);
        _currentDirectories[0]._name = "Assets";

        getDirectoryStructureForPath(Paths::g_xmlDataLocation, _currentDirectories[1]);
        _currentDirectories[1]._name = "XML";

        _fileIcon   = getTextureForPath(Paths::g_assetsLocation + "icons", ResourcePath("file_icon.png"));
        _soundIcon  = getTextureForPath(Paths::g_assetsLocation + "icons", ResourcePath("sound_icon.png"));
        _shaderIcon = getTextureForPath(Paths::g_assetsLocation + "icons", ResourcePath("shader_icon.png"));

        _geometryIcons[to_base(GeometryFormat::_3DS)]     = getTextureForPath(Paths::g_assetsLocation + "icons", ResourcePath("3ds_icon.png"));
        _geometryIcons[to_base(GeometryFormat::ASE)]      = getTextureForPath(Paths::g_assetsLocation + "icons", ResourcePath("ase_icon.png"));
        _geometryIcons[to_base(GeometryFormat::FBX)]      = getTextureForPath(Paths::g_assetsLocation + "icons", ResourcePath("fbx_icon.png"));
        _geometryIcons[to_base(GeometryFormat::MD2)]      = getTextureForPath(Paths::g_assetsLocation + "icons", ResourcePath("md2_icon.png"));
        _geometryIcons[to_base(GeometryFormat::MD5)]      = getTextureForPath(Paths::g_assetsLocation + "icons", ResourcePath("md5_icon.png"));
        _geometryIcons[to_base(GeometryFormat::OBJ)]      = getTextureForPath(Paths::g_assetsLocation + "icons", ResourcePath("obj_icon.png"));
        _geometryIcons[to_base(GeometryFormat::DAE)]      = getTextureForPath(Paths::g_assetsLocation + "icons", ResourcePath("collada.png"));
        _geometryIcons[to_base(GeometryFormat::GLTF)]     = getTextureForPath(Paths::g_assetsLocation + "icons", ResourcePath("gltf.png"));
        _geometryIcons[to_base(GeometryFormat::X)]        = getTextureForPath(Paths::g_assetsLocation + "icons", ResourcePath("x_icon.png"));
        _geometryIcons[to_base(GeometryFormat::DVD_ANIM)] = getTextureForPath(Paths::g_assetsLocation + "icons", ResourcePath("divide.png"));
        _geometryIcons[to_base(GeometryFormat::DVD_GEOM)] = getTextureForPath(Paths::g_assetsLocation + "icons", ResourcePath("divide.png"));
        _geometryIcons[to_base(GeometryFormat::COUNT)]    = getTextureForPath(Paths::g_assetsLocation + "icons", ResourcePath("file_icon.png"));
    }

    void ContentExplorerWindow::update([[maybe_unused]] const U64 deltaTimeUS) {

        while (!_textureLoadQueue.empty()) {
            if (!_textureLoadQueue.empty()) {
                const auto [path, name] = _textureLoadQueue.top();
                _textureLoadQueue.pop();
                _loadedTextures[_ID((path + "/" + name).c_str())] = getTextureForPath(ResourcePath(path), ResourcePath(name));
            }
        }

        _textureLoadQueueLocked = false;
    }

    void ContentExplorerWindow::getDirectoryStructureForPath(const ResourcePath& directoryPath, Directory& directoryOut) const {
        const std::filesystem::path p(directoryPath.c_str());
        if (is_directory(p)) {
            directoryOut._name = getTopLevelFolderName(directoryPath);
            directoryOut._path = directoryPath.str();

            for (auto&& x : std::filesystem::directory_iterator(p)) {
                if (is_regular_file(x.path())) {
                    if (IsValidFile(x.path().generic_string().c_str())) {
                        directoryOut._files.push_back({ directoryOut._path, x.path().filename().generic_string() });

                    }
                } else if (is_directory(x.path())) {
                    auto& childDirectory = directoryOut._children.emplace_back(eastl::make_unique<Directory>());
                    getDirectoryStructureForPath(ResourcePath(x.path().generic_string() + "/"), *childDirectory);
                }
            }
        }
    }

    void ContentExplorerWindow::printDirectoryStructure(const Directory& dir, const bool open) const {
        ImGuiTreeNodeFlags nodeFlags = (open ? ImGuiTreeNodeFlags_DefaultOpen : 0);

        if (dir._children.empty()) {
            nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        if (ImGui::TreeNodeEx(dir._name.c_str(), nodeFlags)) {
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

        const ImGuiContext& imguiContext = Attorney::EditorGeneralWidget::getImGuiContext(_parent, Editor::ImGuiContextType::Editor);

        static Texture_ptr previewTexture = nullptr;
        static Mesh_ptr spawnMesh = nullptr;

        const auto isSoundFile = [](const Str64& fileName) {
            const string extension = getExtension(fileName.c_str());
            for (const char* ext : g_soundExtensions) {
                if (Util::CompareIgnoreCase(extension.substr(1).c_str(), ext)) {
                    return true;
                }
            }
            return false;
        }; 
        
        const auto isShaderFile = [](const Str64& fileName) {
            const string extension = getExtension(fileName.c_str());
            for (const char* ext : g_shaderExtensions) {
                if (Util::CompareIgnoreCase(extension.substr(1).c_str(), ext)) {
                    return true;
                }
            }
            return false;
        };
        
        const auto openFileInEditor = [&](const std::pair<Str256, Str64>& file) {
            const string& textEditor = Attorney::EditorGeneralWidget::externalTextEditorPath(_parent);
            if (textEditor.empty()) {
                Attorney::EditorGeneralWidget::showStatusMessage(_parent, "ERROR: No text editor specified!", Time::SecondsToMilliseconds<F32>(3), true);
            } else {
                if (openFile(textEditor.c_str(), file.first.c_str(), file.second.c_str()) != FileError::NONE) {
                    Attorney::EditorGeneralWidget::showStatusMessage(_parent, "ERROR: Couldn't open specified source file!", Time::SecondsToMilliseconds<F32>(3), true);
                }
            }
        };
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);

        {
            ImGui::BeginChild("Folders", ImVec2(ImGui::GetWindowContentRegionWidth() * 0.3f, -1), true, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_HorizontalScrollbar);
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

            if (_selectedDir != nullptr) {
                ImGui::Columns(CLAMPED(to_I32(_selectedDir->_files.size()), 1, 4));
                bool lockTextureQueue = false;

                for (const auto& file : _selectedDir->_files) {
                    Texture_ptr tex = nullptr;
                    { // Textures
                        const string imageExtension = getExtension(file.second.c_str()).substr(1);
                        for (const char* extension : g_imageExtensions) {
                            if (Util::CompareIgnoreCase(imageExtension.c_str(), extension)) {
                                const auto it = _loadedTextures.find(_ID((file.first + "/" + file.second).c_str()));
                                if (it == std::cend(_loadedTextures) || it->second == nullptr) {
                                    if (!_textureLoadQueueLocked) {
                                        _textureLoadQueue.push(file);
                                        lockTextureQueue = true;
                                    }
                                } else if (it->second->getState() == ResourceState::RES_LOADED) {
                                    tex = it->second;
                                }
                                break;
                            }
                        }
                    }
                    ImGui::PushID(file.second.c_str());

                    const GeometryFormat format = tex != nullptr ? GeometryFormat::COUNT : GetGeometryFormatForExtension(getExtension(file.second.c_str()).c_str());

                    bool hasTooltip = false;
                    if (tex != nullptr) {
                        const U16 w = tex->width();
                        const U16 h = tex->height();
                        const F32 aspect = w / to_F32(h);

                        if (ImGui::ImageButton((void*)(intptr_t)tex->data()._textureHandle, ImVec2(64, 64 / aspect))) {
                            previewTexture = tex;
                        }
                    } else if (format != GeometryFormat::COUNT) {
                        const Texture_ptr& icon = _geometryIcons[to_base(format)];
                        const U16 w = icon->width();
                        const U16 h = icon->height();
                        const F32 aspect = w / to_F32(h);

                        const ImVec4 bgColour(imguiContext.IO.KeyShift ? 1.f : 0.f, 0.f, 0.f, imguiContext.IO.KeyShift ? 1.f : 0.f);
                        if (ImGui::ImageButton((void*)(intptr_t)icon->data()._textureHandle, ImVec2(64, 64 / aspect), ImVec2(0,0), ImVec2(1,1), 2, bgColour, ImVec4(1,1,1,1))) {
                            spawnMesh = getModelForPath(ResourcePath(file.first), ResourcePath(file.second));
                        }
                        hasTooltip = true;
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Hold down [Shift] to spawn directly at the camera position");
                        }
                    } else if (isSoundFile(file.second)) {
                        const U16 w = _soundIcon->width();
                        const U16 h = _soundIcon->height();
                         const F32 aspect = w / to_F32(h);

                        if (ImGui::ImageButton((void*)(intptr_t)_soundIcon->data()._textureHandle, ImVec2(64, 64 / aspect))) {
                            //ToDo: Play sound file -Ionut
                        }
                    } else if (isShaderFile(file.second)) {
                        const U16 w = _shaderIcon->width();
                        const U16 h = _shaderIcon->height();
                        const F32 aspect = w / to_F32(h);

                        if (ImGui::ImageButton((void*)(intptr_t)_shaderIcon->data()._textureHandle, ImVec2(64, 64 / aspect))) {
                            openFileInEditor(file);
                        }
                    } else {
                        const U16 w = _fileIcon->width();
                        const U16 h = _fileIcon->height();
                        const F32 aspect = w / to_F32(h);

                        if (ImGui::ImageButton((void*)(intptr_t)_fileIcon->data()._textureHandle, ImVec2(64, 64 / aspect))) {
                            openFileInEditor(file);
                        }
                    }
                    if (!hasTooltip && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(file.second.c_str());
                    }
                    ImGui::Text(file.second.c_str());

                    ImGui::PopID();
                    ImGui::NextColumn();
                    if (lockTextureQueue) {
                        _textureLoadQueueLocked = true;
                    }
                }
            }
            ImGui::EndChild();
        }

        if (Attorney::EditorGeneralWidget::modalTextureView(_parent, "Image Preview", previewTexture.get(), vec2<F32>(512, 512), true, true)) {
            previewTexture = nullptr;
        }
        if (Attorney::EditorGeneralWidget::modalModelSpawn(_parent, "Spawn Entity", spawnMesh)) {
            spawnMesh = nullptr;
        }

        ImGui::PopStyleVar();
    }

    Texture_ptr ContentExplorerWindow::getTextureForPath(const ResourcePath& texturePath, const ResourcePath& textureName) const {
        ImageTools::ImportOptions options{};
        options._useDDSCache = false;

        TextureDescriptor texturePreviewDescriptor(TextureType::TEXTURE_2D);
        texturePreviewDescriptor.textureOptions(options);

        ResourceDescriptor textureResource(textureName.str());
        textureResource.assetName(textureName);
        textureResource.assetLocation(texturePath);
        textureResource.propertyDescriptor(texturePreviewDescriptor);

        return CreateResource<Texture>(_parent.context().kernel().resourceCache(), textureResource);
    }

    Mesh_ptr ContentExplorerWindow::getModelForPath(const ResourcePath& modelPath, const ResourcePath& modelName) const {
        ResourceDescriptor model(modelName.str());
        model.assetLocation(modelPath);
        model.assetName(modelName);
        model.flag(true);

        return CreateResource<Mesh>(_parent.context().kernel().resourceCache(), model);
    }
} //namespace Divide
