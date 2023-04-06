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
#ifndef _EDITOR_DOCKED_WINDOW_H_
#define _EDITOR_DOCKED_WINDOW_H_

#ifndef IMGUI_API
#include <imgui/imgui.h>
#endif //IMGUI_API

namespace Divide {

class Editor;
class SceneGraphNode;
class EditorComponent;
class DockedWindow : NonCopyable, NonMovable {
    public:
        struct Descriptor {
            ImVec2 size;
            ImVec2 position;
            ImVec2 minSize = ImVec2(0, 0);
            ImVec2 maxSize = ImVec2(FLT_MAX, FLT_MAX);
            string name = "";
            ImGuiWindowFlags flags = 0;
            bool showCornerButton = false;
        };

    public:
        explicit DockedWindow(Editor& parent, Descriptor descriptor) noexcept;
        virtual ~DockedWindow() = default;

        // Called when the editor is visible
        void draw();
        // Always called, even if the editor is not visible
        void backgroundUpdate();

        [[nodiscard]] virtual const char* name() const { return _descriptor.name.c_str(); }


        [[nodiscard]] const Descriptor& descriptor() const noexcept { return _descriptor; }

        virtual void onRemoveComponent([[maybe_unused]] const EditorComponent& comp) {};

        PROPERTY_RW(ImGuiWindowFlags, windowFlags, 0);
        PROPERTY_RW(bool, enabled, true);
        PROPERTY_R_IW(bool, focused, false);
        PROPERTY_R_IW(bool, hovered, false);
        PROPERTY_R_IW(bool, visible, false);

    protected:
        virtual void drawInternal() = 0;
        virtual void backgroundUpdateInternal() {}

        const char* getIconForNode(const SceneGraphNode* sgn) noexcept;

    protected:
        Editor & _parent;
        Descriptor _descriptor;
};
} //namespace Divide

#endif //_EDITOR_DOCKED_WINDOW_H_