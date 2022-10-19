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
#ifndef _EDITOR_MENU_BAR_H_
#define _EDITOR_MENU_BAR_H_

#include "Core/Headers/PlatformContextComponent.h"
#include "Geometry/Shapes/Headers/Object3D.h"

#include <ImGuiMisc/imguifilesystem/imguifilesystem.h>

namespace Divide {
    FWD_DECLARE_MANAGED_CLASS(Texture);

    class MenuBar final : public PlatformContextComponent, NonMovable {
        enum class DebugObject : U8 {
            SPONZA = 0,
            COUNT
        };

    public:
        explicit MenuBar(PlatformContext& context, bool mainMenu);
        void draw();

    protected:
        void drawFileMenu(bool modifierPressed);
        void drawEditMenu(bool modifierPressed) const;
        void drawProjectMenu(bool modifierPressed) const;
        void drawObjectMenu(bool modifierPressed);
        void drawToolsMenu(bool modifierPressed);
        void drawWindowsMenu(bool modifierPressed) const;
        void drawPostFXMenu(bool modifierPressed) const;
        void drawDebugMenu(bool modifierPressed);
        void drawHelpMenu(bool modifierPressed) const;
        void spawnDebugObject(DebugObject object, bool modifierPressed) const;

    protected:
        bool _isMainMenu = true;
        bool _quitPopup = false;
        bool _restartPopup = false;
        bool _newScenePopup = false;
        bool _closePopup = false;
        bool _savePopup = false;
        SceneNodeType _newPrimitiveType = SceneNodeType::COUNT;
        DebugObject _debugObject = DebugObject::COUNT;
        
        string _errorMsg = "";
        vector<Texture_ptr> _previewTextures;
        
        ImGuiFs::Dialog _sceneOpenDialog;
        ImGuiFs::Dialog _sceneSaveDialog;
    };

    FWD_DECLARE_MANAGED_CLASS(MenuBar);
} //namespace Divide

#endif //_EDITOR_MENU_BAR_H_