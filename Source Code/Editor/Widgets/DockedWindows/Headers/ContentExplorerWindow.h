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
#ifndef _EDITOR_CONTENT_EXPLORER_WINDOW_H_
#define _EDITOR_CONTENT_EXPLORER_WINDOW_H_

#include "Editor/Widgets/Headers/DockedWindow.h"
#include "Geometry/Importer/Headers/MeshImporter.h"

namespace Divide {
    FWD_DECLARE_MANAGED_CLASS(Texture);
    FWD_DECLARE_MANAGED_CLASS(Mesh);

    struct Directory {
        string _path;
        string _name;
        vector_fast<std::pair<Str256, Str64>> _files;
        vector_fast<eastl::unique_ptr<Directory>> _children;
    };

    class ContentExplorerWindow final : public DockedWindow {
    public:
        ContentExplorerWindow(Editor& parent, const Descriptor& descriptor);

        void drawInternal() override;
        void init();
        void update(U64 deltaTimeUS);

    private:
        void getDirectoryStructureForPath(const ResourcePath& directoryPath, Directory& directoryOut) const;
        void printDirectoryStructure(const Directory& dir, bool open) const;

        Texture_ptr getTextureForPath(const ResourcePath& texturePath, const ResourcePath& textureName) const;
        Mesh_ptr getModelForPath(const ResourcePath& modelPath, const ResourcePath& modelName) const;
        
    private:
        Texture_ptr _fileIcon = nullptr;
        Texture_ptr _soundIcon = nullptr;
        std::array<Texture_ptr, to_base(GeometryFormat::COUNT) + 1> _geometryIcons = {};
        mutable const Directory* _selectedDir = nullptr;
        vector_fast<Directory> _currentDirectories;

        hashMap<size_t, Texture_ptr> _loadedTextures;

        bool _textureLoadQueueLocked = false;
        std::stack<std::pair<Str256, Str64>> _textureLoadQueue;
    };
} //namespace Divide

#endif //_EDITOR_CONTENT_EXPLORER_WINDOW_H_