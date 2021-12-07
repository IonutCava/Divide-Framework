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

namespace Divide {
    namespace {
        const char* const g_extensions[] = {
            "glsl", "cmn", "frag", "vert", "cmp", "geom", "tesc", "tese",  //Shaders
            "ogg", "wav", //Sounds
            "chai", //Scripts
            "layout", "looknfeel", "scheme", "xsd", "imageset", "xcf", "txt", "anims",  //CEGUI
            "ttf", "font", //Fonts
            "mtl", "md5anim", "bvh",  //Geometry support
            "xml" //General
        };

        const char* const g_imageExtensions[] = {
            "png", "jpg", "jpeg", "tga", "raw", "dds"
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
        _currentDirectories[0]._path = "Assets";

        getDirectoryStructureForPath(Paths::g_xmlDataLocation, _currentDirectories[1]);
        _currentDirectories[1]._path = "XML";

        _fileIcon = getTextureForPath(ResourcePath("icons"), ResourcePath("file_icon.png"));

        _geometryIcons[to_base(GeometryFormat::_3DS)]     = getTextureForPath(ResourcePath("icons"), ResourcePath("3ds_icon.png"));
        _geometryIcons[to_base(GeometryFormat::ASE)]      = getTextureForPath(ResourcePath("icons"), ResourcePath("ase_icon.png"));
        _geometryIcons[to_base(GeometryFormat::FBX)]      = getTextureForPath(ResourcePath("icons"), ResourcePath("fbx_icon.png"));
        _geometryIcons[to_base(GeometryFormat::MD2)]      = getTextureForPath(ResourcePath("icons"), ResourcePath("md2_icon.png"));
        _geometryIcons[to_base(GeometryFormat::MD5)]      = getTextureForPath(ResourcePath("icons"), ResourcePath("md5_icon.png"));
        _geometryIcons[to_base(GeometryFormat::OBJ)]      = getTextureForPath(ResourcePath("icons"), ResourcePath("obj_icon.png"));
        _geometryIcons[to_base(GeometryFormat::DAE)]      = getTextureForPath(ResourcePath("icons"), ResourcePath("collada.png"));
        _geometryIcons[to_base(GeometryFormat::GLTF)]     = getTextureForPath(ResourcePath("icons"), ResourcePath("gltf.png"));
        _geometryIcons[to_base(GeometryFormat::X)]        = getTextureForPath(ResourcePath("icons"), ResourcePath("x_icon.png"));
        _geometryIcons[to_base(GeometryFormat::DVD_ANIM)] = getTextureForPath(ResourcePath("icons"), ResourcePath("divide.png"));
        _geometryIcons[to_base(GeometryFormat::DVD_GEOM)] = getTextureForPath(ResourcePath("icons"), ResourcePath("divide.png"));
        _geometryIcons[to_base(GeometryFormat::COUNT)]    = getTextureForPath(ResourcePath("icons"), ResourcePath("file_icon.png"));
    }

    void ContentExplorerWindow::update([[maybe_unused]] const U64 deltaTimeUS) {

        while (!_textureLoadQueue.empty() || !_modelLoadQueue.empty()) {
            if (!_textureLoadQueue.empty()) {
                const auto [path, name] = _textureLoadQueue.top();
                _textureLoadQueue.pop();
                _loadedTextures[_ID((path + "/" + name).c_str())] = getTextureForPath(ResourcePath(path), ResourcePath(name));
            }


            if (!_modelLoadQueue.empty()) {
                const auto [path, name] = _modelLoadQueue.top();
                _modelLoadQueue.pop();
                _loadedModels[_ID((path + "/" + name).c_str())] = getModelForPath(ResourcePath(path), ResourcePath(name));
            }
        }

        _textureLoadQueueLocked = false;
        _modelLoadQueueLocked = false;
    }

    void ContentExplorerWindow::getDirectoryStructureForPath(const ResourcePath& directoryPath, Directory& directoryOut) const {
        const std::filesystem::path p(directoryPath.c_str());
        if (is_directory(p)) {
            directoryOut._path = p.filename().generic_string();
            for (auto&& x : std::filesystem::directory_iterator(p)) {
                if (is_regular_file(x.path())) {
                    if (IsValidFile(x.path().generic_string().c_str())) {
                        directoryOut._files.push_back({ directoryOut._path, x.path().filename().generic_string() });

                    }
                } else if (is_directory(x.path())) {
                    directoryOut._children.push_back(std::make_shared<Directory>());
                    getDirectoryStructureForPath(ResourcePath(x.path().generic_string()), *directoryOut._children.back());
                }
            }
        }
    }

    void ContentExplorerWindow::printDirectoryStructure(const Directory& dir, const bool open) const {
        ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | (open ? ImGuiTreeNodeFlags_DefaultOpen : 0);

        if (dir._children.empty()) {
            nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        if (ImGui::TreeNodeEx(dir._path.c_str(), nodeFlags)) {
            if (ImGui::IsItemClicked()) {
                _selectedDir = &dir;
            }

            for (const std::shared_ptr<Directory>& childDirectory : dir._children) {
                printDirectoryStructure(*childDirectory, false);
            }
       
            if (!dir._children.empty()) {
                ImGui::TreePop();
            }
        }
    }

    void ContentExplorerWindow::drawInternal() {

        static Texture_ptr previewTexture = nullptr;
        static Mesh_ptr spawnMesh = nullptr;

        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);

        {
            ImGui::BeginChild("Folders", ImVec2(ImGui::GetWindowContentRegionWidth() * 0.3f, -1), true, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_HorizontalScrollbar);
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("Menu")) {
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
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
            ImGui::Columns(4);
            if (_selectedDir != nullptr) {
                bool lockTextureQueue = false;
                bool lockModelQueue = false;

                for (const auto& file : _selectedDir->_files) {
                    Texture_ptr tex = nullptr;
                    Mesh_ptr mesh = nullptr;
                    GeometryFormat format = GeometryFormat::COUNT;
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
                    { //Geometry
                        format = GetGeometryFormatForExtension(getExtension(file.second.c_str()).c_str());
                        if (format != GeometryFormat::COUNT) {
                            const auto it = _loadedModels.find(_ID((file.first + "/" + file.second).c_str()));
                            if (it == std::cend(_loadedModels) || it->second == nullptr) {
                                if (!_modelLoadQueueLocked) {
                                    _modelLoadQueue.push(file);
                                    lockModelQueue = true;
                                }
                            } else if (it->second->getState() == ResourceState::RES_LOADED) {
                                mesh = it->second;
                            }
                        }
                    }

                    ImGui::PushID(file.second.c_str());

                    if (tex != nullptr) {
                        const U16 w = tex->width();
                        const U16 h = tex->height();
                        const F32 aspect = w / to_F32(h);

                        if (ImGui::ImageButton((void*)(intptr_t)tex->data()._textureHandle, ImVec2(64, 64 / aspect))) {
                            previewTexture = tex;
                        }
                    } else if (mesh != nullptr) {
                        const Texture_ptr& icon = _geometryIcons[to_base(format)];
                        const U16 w = icon->width();
                        const U16 h = icon->height();
                        const F32 aspect = w / to_F32(h);

                        if (ImGui::ImageButton((void*)(intptr_t)icon->data()._textureHandle, ImVec2(64, 64 / aspect))) {
                            spawnMesh = mesh;
                        }
                    } else {
                        const U16 w = _fileIcon->width();
                        const U16 h = _fileIcon->height();
                        const F32 aspect = w / to_F32(h);

                        if (ImGui::ImageButton((void*)(intptr_t)_fileIcon->data()._textureHandle, ImVec2(64, 64 / aspect))) {
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(file.second.c_str());
                    }
                    ImGui::Text(file.second.c_str());

                    ImGui::PopID();
                    ImGui::NextColumn();
                    if (lockTextureQueue) {
                        _textureLoadQueueLocked = true;
                    }
                    if (lockModelQueue) {
                        _modelLoadQueueLocked = true;
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
        const TextureDescriptor texturePreviewDescriptor(TextureType::TEXTURE_2D);

        ResourceDescriptor textureResource(textureName.str());
        textureResource.assetName(textureName);
        textureResource.assetLocation(Paths::g_assetsLocation + texturePath);
        textureResource.propertyDescriptor(texturePreviewDescriptor);

        return CreateResource<Texture>(_parent.context().kernel().resourceCache(), textureResource);
    }

    Mesh_ptr ContentExplorerWindow::getModelForPath(const ResourcePath& modelPath, const ResourcePath& modelName) const {
        ResourceDescriptor model(modelName.str());
        model.assetLocation(Paths::g_assetsLocation + modelPath);
        model.assetName(modelName);
        model.flag(true);

        return CreateResource<Mesh>(_parent.context().kernel().resourceCache(), model);
    }
} //namespace Divide
