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
#ifndef GUI_H_
#define GUI_H_

#include "GUIInterface.h"
#include "Core/Headers/KernelComponent.h"
#include "GUI/CEGUIAddons/Headers/CEGUIInput.h"
#include "Platform/Video/Headers/TextureData.h"
#include "Platform/Input/Headers/InputAggregatorInterface.h"

namespace CEGUI {
class Renderer;
};

namespace Divide {

namespace GFX {
    class CommandBuffer;
};

class Pipeline;
class GUIConsole;
class GUIElement;
class ResourceCache;
class SceneGraphNode;
class PlatformContext;
class RenderStateBlock;

FWD_DECLARE_MANAGED_CLASS(ShaderProgram);

class Scene;
struct SizeChangeParams;
/// Graphical User Interface

class SceneGUIElements;
class GUI final : public GUIInterface,
                  public KernelComponent,
                  public Input::InputAggregatorInterface {
public:
    using GUIMapPerScene = hashMap<I64, SceneGUIElements*>;

public:
    explicit GUI(Kernel& parent);
    ~GUI();

    /// Create the GUI
    [[nodiscard]] bool init(PlatformContext& context, ResourceCache* cache);
    void destroy();

    void draw(GFXDevice& context, const Rect<I32>& viewport, GFX::CommandBuffer& bufferInOut);

    void onSizeChange(const SizeChangeParams& params);
    void onChangeScene(Scene* newScene);
    void onUnloadScene(Scene* scene);

    void idle();
    /// Main update call
    void update(U64 deltaTimeUS);

    template <typename T>
    [[nodiscard]] typename std::enable_if<std::is_base_of<GUIElement, T>::value, T*>::type
    getGUIElement(const I64 sceneID, const U64 elementName) {
        return static_cast<T*>(getGUIElementImpl(sceneID, elementName, T::Type));
    }

    template <typename T>
    [[nodiscard]] typename std::enable_if<std::is_base_of<GUIElement, T>::value, T*>::type
    getGUIElement(const I64 sceneID, const I64 elementID) {
        static_assert(std::is_base_of<GUIElement, T>::value,
            "getGuiElement error: Target is not a valid GUI item");

        return static_cast<T*>(getGUIElementImpl(sceneID, elementID, T::Type));
    }

    /// Get a pointer to our console window
    [[nodiscard]] GUIConsole& getConsole() noexcept { return *_console; }
    [[nodiscard]] const GUIConsole& getConsole() const noexcept { return *_console; }

    [[nodiscard]] CEGUI::Window* rootSheet() const noexcept { return _rootSheet; }

    /// Return a pointer to the default, general purpose message box
    [[nodiscard]] GUIMessageBox* getDefaultMessageBox() const noexcept { return _defaultMsgBox; }
    /// Used to prevent text updating every frame
    void setTextRenderTimer(const U64 renderIntervalUs) noexcept {
        _textRenderInterval = renderIntervalUs;
    }
    /// Mouse cursor forced to a certain position
    void setCursorPosition(I32 x, I32 y);
    /// Key pressed: return true if input was consumed
    [[nodiscard]] bool onKeyDown(const Input::KeyEvent& key) override;
    /// Key released: return true if input was consumed
    [[nodiscard]] bool onKeyUp(const Input::KeyEvent& key) override;
    /// Joystick axis change: return true if input was consumed
    [[nodiscard]] bool joystickAxisMoved(const Input::JoystickEvent& arg) override;
    /// Joystick direction change: return true if input was consumed
    [[nodiscard]] bool joystickPovMoved(const Input::JoystickEvent& arg) override;
    /// Joystick button pressed: return true if input was consumed
    [[nodiscard]] bool joystickButtonPressed(const Input::JoystickEvent& arg) override;
    /// Joystick button released: return true if input was consumed
    [[nodiscard]] bool joystickButtonReleased(const Input::JoystickEvent& arg) override;
    [[nodiscard]] bool joystickBallMoved(const Input::JoystickEvent& arg) override;
    [[nodiscard]] bool joystickAddRemove(const Input::JoystickEvent& arg) override;
    [[nodiscard]] bool joystickRemap(const Input::JoystickEvent &arg) override;
    /// Mouse moved: return true if input was consumed
    [[nodiscard]] bool mouseMoved(const Input::MouseMoveEvent& arg) override;
    /// Mouse button pressed: return true if input was consumed
    [[nodiscard]] bool mouseButtonPressed(const Input::MouseButtonEvent& arg) override;
    /// Mouse button released: return true if input was consumed
    [[nodiscard]] bool mouseButtonReleased(const Input::MouseButtonEvent& arg) override;

    [[nodiscard]] bool onUTF8(const Input::UTF8Event& arg) noexcept override;

    [[nodiscard]] Scene* activeScene() noexcept { return _activeScene; }

    [[nodiscard]] const Scene* activeScene() const noexcept { return _activeScene; }

    [[nodiscard]] CEGUI::GUIContext& getCEGUIContext() noexcept;
    [[nodiscard]] const CEGUI::GUIContext& getCEGUIContext() const noexcept;

    void setRenderer(CEGUI::Renderer& renderer);

    PROPERTY_R(bool, showDebugCursor, false);
    PROPERTY_R(string, defaultGUIScheme, "GWEN");
    void showDebugCursor(bool state);

protected:
    void CEGUIDrawInternal();
    [[nodiscard]] GUIElement* getGUIElementImpl(I64 sceneID, U64 elementName, GUIType type) const;
    [[nodiscard]] GUIElement* getGUIElementImpl(I64 sceneID, I64 elementID, GUIType type) const;
    [[nodiscard]] TextureData getCEGUIRenderTextureData() const;

    void recreateDefaultMessageBox();

protected:
    friend class SceneGUIElements;
    CEGUI::Window* _rootSheet = nullptr;  ///< gui root Window
    CEGUI::GUIContext* _ceguiContext = nullptr;
    CEGUI::TextureTarget* _ceguiRenderTextureTarget = nullptr;

private:
    bool _init = false;              ///< Set to true when the GUI has finished loading
                             /// Toggle CEGUI rendering on/off (e.g. to check raw application rendering
                             /// performance)
    CEGUIInput _ceguiInput;  ///< Used to implement key repeat
    CEGUI::Renderer* _ceguiRenderer = nullptr; ///<Used to render CEGUI to a texture;
    GUIConsole* _console = nullptr;    ///< Pointer to the GUIConsole object
    GUIMessageBox* _defaultMsgBox = nullptr;  ///< Pointer to a default message box used for general purpose messages
    U64 _textRenderInterval{};  ///< We should avoid rendering text as fast as possible
                              ///for performance reasons

    /// Each scene has its own gui elements! (0 = global)
    Scene* _activeScene = nullptr;
    Pipeline* _postCEGUIPipeline = nullptr;
    U32 _debugVarCacheCount = 0;
    // GROUP, VAR
    vector<std::pair<I64, I64>> _debugDisplayEntries{};

    /// All the GUI elements created per scene
    GUIMapPerScene _guiStack{};
    mutable SharedMutex _guiStackLock{};
};

};  // namespace Divide
#endif
