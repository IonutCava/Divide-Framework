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
#ifndef DVD_EDITOR_CONTENT_EXPLORER_WINDOW_H_
#define DVD_EDITOR_CONTENT_EXPLORER_WINDOW_H_

#include "Editor/Widgets/Headers/DockedWindow.h"
#include "Geometry/Importer/Headers/MeshImporter.h"

namespace Divide {
    class Mesh;
    class Texture;

    struct File
    {
        ResourcePath _path;
        Str<32> _extension;
    };

    struct EditorFileEntry
    {
        ResourcePath _path{};
        File _file{};
    };

    FWD_DECLARE_MANAGED_STRUCT(Directory);

    struct Directory
    {
        Directory() = default;
        Directory(const ResourcePath& path, const ResourcePath& name)
            : _path(path), _name(name)
        {
        }


        ResourcePath _path;
        ResourcePath _name;
        vector<File> _files;
        vector<Directory_uptr> _children;
    };

    class ContentExplorerWindow final : public DockedWindow {
    public:
        ContentExplorerWindow(Editor& parent, const Descriptor& descriptor);
        ~ContentExplorerWindow();

        void drawInternal() override;
        void init();
        void update(U64 deltaTimeUS);

    private:
        void getDirectoryStructureForPath(const ResourcePath& directoryPath, Directory& directoryOut) const;
        void printDirectoryStructure(const Directory& dir, bool open) const;

        Handle<Texture> getTextureForPath(const ResourcePath& texturePath, std::string_view textureName) const;
        Handle<Mesh> getModelForPath(const ResourcePath& modelPath, std::string_view modelName) const;
        
    private:
        Handle<Texture> _fileIcon = INVALID_HANDLE<Texture>;
        Handle<Texture> _shaderIcon = INVALID_HANDLE<Texture>;
        Handle<Texture> _soundIcon = INVALID_HANDLE<Texture>;
        std::array<Handle<Texture>, to_base(GeometryFormat::COUNT) + 1> _geometryIcons = {};
        std::array<string, to_base(GeometryFormat::COUNT) + 1> _geometryIconNames = {};
        mutable const Directory* _selectedDir = nullptr;
        vector<Directory> _currentDirectories;

        hashMap<size_t, Handle<Texture>> _loadedTextures;

        Handle<Texture> _previewTexture = INVALID_HANDLE<Texture>;

        bool _textureLoadQueueLocked = false;
        eastl::stack<EditorFileEntry> _textureLoadQueue;
    };
} //namespace Divide

#endif //DVD_EDITOR_CONTENT_EXPLORER_WINDOW_H_
