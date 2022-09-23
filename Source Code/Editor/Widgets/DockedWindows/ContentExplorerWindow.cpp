#include "stdafx.h"

#include "Headers/ContentExplorerWindow.h"

#include "Editor/Headers/Editor.h"
#include "Core/Headers/Kernel.h"
#include "Managers/Headers/SceneManager.h"
#include "Core/Headers/PlatformContext.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Geometry/Shapes/Headers/Mesh.h"

#include <filesystem>
#include <imgui_internal.h>

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

        // One per frame to avoid massive stutters.
        if (!_textureLoadQueue.empty()) {
            const auto [path, name] = _textureLoadQueue.top();
            _textureLoadQueue.pop();
            _loadedTextures[_ID((path + "/" + name._path).c_str())] = getTextureForPath(ResourcePath(path), ResourcePath(name._path));
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
                        const auto& filename = x.path().filename().generic_string();
                        directoryOut._files.push_back({ directoryOut._path, {filename, getExtension(filename.c_str()).substr(1)}});

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
        OPTICK_EVENT();

        const ImGuiContext& imguiContext = Attorney::EditorGeneralWidget::getImGuiContext(_parent, Editor::ImGuiContextType::Editor);

        static Texture_ptr previewTexture = nullptr;
        static Mesh_ptr spawnMesh = nullptr;

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
        
        const auto openFileInEditor = [&](const std::pair<Str256, File>& file) {
            const string& textEditor = Attorney::EditorGeneralWidget::externalTextEditorPath(_parent);
            if (textEditor.empty()) {
                Attorney::EditorGeneralWidget::showStatusMessage(_parent, "ERROR: No text editor specified!", Time::SecondsToMilliseconds<F32>(3), true);
            } else {
                if (openFile(textEditor.c_str(), file.first.c_str(), file.second._path.c_str()) != FileError::NONE) {
                    Attorney::EditorGeneralWidget::showStatusMessage(_parent, "ERROR: Couldn't open specified source file!", Time::SecondsToMilliseconds<F32>(3), true);
                }
            }
        };

        constexpr bool flipImages = true;
        constexpr U8 buttonSize = 64u;
        const ImVec2 uv0{ 0, flipImages ? 1 : 0 };
        const ImVec2 uv1{ 1, flipImages ? 0 : 1 };

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

            if (_selectedDir != nullptr) {
                ImGui::Columns(CLAMPED(to_I32(_selectedDir->_files.size()), 1, 4));
                bool lockTextureQueue = false;

                for (const auto& file : _selectedDir->_files) {
                    Texture_ptr tex = nullptr;
                    { // Textures
                        for (const char* extension : g_imageExtensions) {
                            if (Util::CompareIgnoreCase(file.second._extension.c_str(), extension)) {
                                const auto it = _loadedTextures.find(_ID((file.first + "/" + file.second._path).c_str()));
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
                    ImGui::PushID(file.second._path.c_str());

                    const GeometryFormat format = tex != nullptr ? GeometryFormat::COUNT : GetGeometryFormatForExtension(file.second._extension.c_str());

                    bool hasTooltip = false;
                    if (tex != nullptr) {
                        const U16 w = tex->width();
                        const U16 h = tex->height();
                        const F32 aspect = w / to_F32(h);
                        ;
                        if (ImGui::ImageButton(tex->resourceName().c_str(), (void*)tex.get(), ImVec2(buttonSize, buttonSize / aspect), uv0, uv1)) {
                            previewTexture = tex;
                        }
                    } else if (format != GeometryFormat::COUNT) {
                        const Texture_ptr& icon = _geometryIcons[to_base(format)];
                        const U16 w = icon->width();
                        const U16 h = icon->height();
                        const F32 aspect = w / to_F32(h);

                        const bool modifierPressed = imguiContext.IO.KeyShift;
                        const ImVec4 bgColour(modifierPressed ? 1.f : 0.f, 0.f, 0.f, modifierPressed ? 1.f : 0.f);
                        if (ImGui::ImageButton(icon->resourceName().c_str(), (void*)icon.get(), ImVec2(buttonSize, buttonSize / aspect), uv0, uv1, bgColour, ImVec4(1, 1, 1, 1))) {
                            spawnMesh = getModelForPath(ResourcePath(file.first), ResourcePath(file.second._path));
                            if (spawnMesh == nullptr) {
                                Attorney::EditorGeneralWidget::showStatusMessage(_parent, "ERROR: Couldn't load specified mesh!", Time::SecondsToMilliseconds<F32>(3), true);
                            }
                        }
                        hasTooltip = true;
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Hold down [Shift] to spawn directly at the camera position");
                        }
                    } else if (isSoundFile(file.second._extension.c_str())) {
                        const U16 w = _soundIcon->width();
                        const U16 h = _soundIcon->height();
                         const F32 aspect = w / to_F32(h);

                        if (ImGui::ImageButton(_soundIcon->resourceName().c_str(), (void*)_soundIcon.get(), ImVec2(buttonSize, buttonSize / aspect), uv0, uv1)) {
                            //ToDo: Play sound file -Ionut
                        }
                    } else if (isShaderFile(file.second._extension.c_str())) {
                        const U16 w = _shaderIcon->width();
                        const U16 h = _shaderIcon->height();
                        const F32 aspect = w / to_F32(h);

                        if (ImGui::ImageButton(_shaderIcon->resourceName().c_str(), (void*)_shaderIcon.get(), ImVec2(buttonSize, buttonSize / aspect), uv0, uv1)) {
                            openFileInEditor(file);
                        }
                    } else {
                        const U16 w = _fileIcon->width();
                        const U16 h = _fileIcon->height();
                        const F32 aspect = w / to_F32(h);

                        if (ImGui::ImageButton(_fileIcon->resourceName().c_str(), (void*)_fileIcon.get(), ImVec2(buttonSize, buttonSize / aspect), uv0, uv1)) {
                            openFileInEditor(file);
                        }
                    }
                    if (!hasTooltip && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(file.second._path.c_str());
                    }
                    ImGui::Text(file.second._path.c_str());

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

        const Camera* playerCam = Attorney::SceneManagerCameraAccessor::playerCamera(_parent.context().kernel().sceneManager());
        if (Attorney::EditorGeneralWidget::modalModelSpawn(_parent, spawnMesh, imguiContext.IO.KeyShift, VECTOR3_UNIT, playerCam->snapshot()._eye)) {
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
