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
#include "Core/Headers/FrameListener.h"
#include "Core/Headers/KernelComponent.h"
#include "GUI/CEGUIAddons/Headers/CEGUIInput.h"
#include "Platform/Video/Headers/PushConstants.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Platform/Input/Headers/InputAggregatorInterface.h"
namespace CEGUI
{
    class CEGUIRenderer;
    class DVDTextureTarget;
};

struct FONScontext;

namespace Divide
{

namespace GFX
{
    class CommandBuffer;
    struct MemoryBarrierCommand;
};

class Scene;
class Pipeline;
class GUIConsole;
class GUIElement;
class ShaderProgram;
class ResourceCache;
class SceneGraphNode;
class PlatformContext;
class SceneGUIElements;

struct ImageView;
struct TextElementBatch;
struct SizeChangeParams;

FWD_DECLARE_MANAGED_STRUCT( DVDFONSContext );

/// Graphical User Interface
class GUI final : public GUIInterface,
                  public KernelComponent,
                  public FrameListener,
                  public Input::InputAggregatorInterface
{
    public:
        using GUIMapPerScene = hashMap<I64, SceneGUIElements*>;

    public:
        explicit GUI( Kernel& parent );
        ~GUI() override;

        /// Create the GUI
        [[nodiscard]] ErrorCode init( PlatformContext& context );
        void destroy();

        /// Render all elements that need their own internal render targets (e.g. CEGUI)
        void preDraw( GFXDevice& context, const Rect<I32>& viewport, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut );
        /// Go through all of the registered scene gui elements and gather all of the render commands
        void draw( GFXDevice& context, const Rect<I32>& viewport, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut );
        /// Text rendering is handled exclusively by Mikko Mononen's FontStash library (https://github.com/memononen/fontstash)
        void drawText( const TextElementBatch& batch, const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut, bool pushCamera = true);
        /// Mostly used by CEGUI to keep track of screen dimensions
        void onResolutionChange( const SizeChangeParams& params );
        /// When we change a scene, we want to toggle our current scene GUI elements off and toggle the new scene's elements on
        void onChangeScene( Scene* newScene );
        /// When we unload a scene, we unload all of its GUI elements. ToDo: Scene should own these and scene should submit to GUI for rendering. Current logic is backwards -Ionut
        void onUnloadScene( Scene* scene );
        /// Main update call. Used to tick gui elements (cursors, animations, etc)
        void update( U64 deltaTimeUS );
        /// Find a return a gui element by name
        template <typename T> requires std::is_base_of_v<GUIElement, T>
        [[nodiscard]] T* getSceneGUIElementImpl( const I64 sceneID, const U64 elementName ) { return static_cast<T*>(getSceneGUIElementImpl( sceneID, elementName, T::Type )); }
        /// Find a return a gui element by ID
        template <typename T> requires std::is_base_of_v<GUIElement, T>
        [[nodiscard]] T* getSceneGUIElementImpl( const I64 sceneID, const I64 elementID ) { return static_cast<T*>(getSceneGUIElementImpl( sceneID, elementID, T::Type )); }
        /// Get a reference to our console window
        [[nodiscard]] GUIConsole& getConsole() noexcept { return *_console; }
        /// Get a const reference to our console window
        [[nodiscard]] const GUIConsole& getConsole() const noexcept { return *_console; }
        /// Get a pointer to the root sheet that CEGUI renders into
        [[nodiscard]] CEGUI::Window* rootSheet() const noexcept { return _rootSheet; }
        /// Return a pointer to the default, general purpose message box
        [[nodiscard]] GUIMessageBox* getDefaultMessageBox() const noexcept { return _defaultMsgBox.get(); }
        /// Mouse cursor forced to a certain position
        void setCursorPosition( I32 x, I32 y );
        /// Provides direct access to the CEGUI context. Used by plugins  (e.g. GUIConsole, GUIInput, etc)
        [[nodiscard]] CEGUI::GUIContext* getCEGUIContext() noexcept;
        /// Toggle debug cursor rendering on or off.
        void showDebugCursor( bool state );
        /// Debug cursor state. The debug cursor is a the cursor as it's know internally to CEGUI (based on its internal position and state)
        PROPERTY_R( bool, showDebugCursor, false );
        /// The "skin" used by CEGUI
        PROPERTY_R( string, defaultGUIScheme, "GWEN" );
        /// We should avoid rendering text as fast as possible
        PROPERTY_RW( U64, textRenderInterval, Time::MillisecondsToMicroseconds( 10 ) );


    protected:
         bool frameStarted( const FrameEvent& evt ) override;

        /// Key pressed: return true if input was consumed
        [[nodiscard]] bool onKeyDownInternal( Input::KeyEvent& argInOut) override;
        /// Key released: return true if input was consumed
        [[nodiscard]] bool onKeyUpInternal( Input::KeyEvent& argInOut) override;
        /// Joystick axis change: return true if input was consumed
        [[nodiscard]] bool joystickAxisMovedInternal( Input::JoystickEvent& argInOut) override;
        /// Joystick direction change: return true if input was consumed
        [[nodiscard]] bool joystickPovMovedInternal( Input::JoystickEvent& argInOut) override;
        /// Joystick button pressed: return true if input was consumed
        [[nodiscard]] bool joystickButtonPressedInternal( Input::JoystickEvent& argInOut) override;
        /// Joystick button released: return true if input was consumed
        [[nodiscard]] bool joystickButtonReleasedInternal( Input::JoystickEvent& argInOut) override;
        [[nodiscard]] bool joystickBallMovedInternal( Input::JoystickEvent& argInOut) override;
        [[nodiscard]] bool joystickAddRemoveInternal( Input::JoystickEvent& argInOut) override;
        [[nodiscard]] bool joystickRemapInternal( Input::JoystickEvent& argInOut) override;
        /// Mouse moved: return true if input was consumed
        [[nodiscard]] bool mouseMovedInternal( Input::MouseMoveEvent& argInOut) override;
        /// Mouse button pressed: return true if input was consumed
        [[nodiscard]] bool mouseButtonPressedInternal( Input::MouseButtonEvent& argInOut) override;
        /// Mouse button released: return true if input was consumed
        [[nodiscard]] bool mouseButtonReleasedInternal( Input::MouseButtonEvent& argInOut) override;
        /// Called when text input was detected
        bool onTextInputInternal(Input::TextInputEvent& argInOut) override;
        /// Called when text edit was detected
        bool onTextEditInternal(Input::TextEditEvent& argInOut) override;

    private:
        //// Try to find the requested FontStash font in the font cache. Load on cache miss.
        [[nodiscard]] I32 getFont( const Str<64>& fontName );
        /// Internal lookup of a GUIElement by name
        [[nodiscard]] GUIElement* getSceneGUIElementImpl( I64 sceneID, U64 elementName, GUIType type ) const;
        /// Internal lookup of a GUIElement by ID
        [[nodiscard]] GUIElement* getSceneGUIElementImpl( I64 sceneID, I64 elementID, GUIType type ) const;
        /// Used to recreate and re-register the default message box if needed (usually on scene change)
        void recreateDefaultMessageBox();

    protected:
        friend class SceneGUIElements;
        /// The root window into which CEGUI anchors all of its elements
        CEGUI::Window* _rootSheet{nullptr};
        /// The CEGUI context as returned by the library upon creation
        CEGUI::GUIContext* _ceguiContext{nullptr};

    private:
        /// Set to true when the GUI has finished loading
        bool _init{false};
        /// Used to implement key repeat
        CEGUIInput _ceguiInput;  
        /// Used to render CEGUI to a texture; We want to port this to the Divide::GFX interface.
        CEGUI::CEGUIRenderer* _ceguiRenderer{ nullptr };
        /// Used to render CEGUI elements into. We blit this on top of our scene target.
        CEGUI::DVDTextureTarget* _renderTextureTarget{nullptr};
        /// Our custom in-game console (for logs and commands. A la Quake's ~-console)
        std::unique_ptr<GUIConsole> _console;
        /// Pointer to a default message box used for general purpose messages
        std::unique_ptr<GUIMessageBox> _defaultMsgBox;
        /// Each scene has its own gui elements! (0 = global). We keep a pointer to the scene but we really shouldn't. Scene should feed itself into GUI.
        Scene* _activeScene{nullptr};
        /// All the GUI elements created per scene
        GUIMapPerScene _guiStack{};
        /// A lock to protect access to _guiStack
        mutable SharedMutex _guiStackLock{};
        /// We use Font Stash (https://github.com/memononen/fontstash) for rendering basic text on the screen. This is our own Divide::GFX based context object used for rendering.
        DVDFONSContext_uptr _fonsContext;
        /// A cache of all font IDs used by Font Stash stored by name ID
        hashMap<U64, I32> _fonts;
        /// A cache of the last requested font by name to avoid a lookup in the fonts map
        hashAlg::pair<Str<64>, I32> _fontCache = { "", -1 };
        /// The text rendering pipeline we used to draw Font Stash text
        Pipeline* _textRenderPipeline{nullptr};
        /// The text rendering shaderProgram we used to draw Font Stash text
        Handle<ShaderProgram> _textRenderShader = INVALID_HANDLE<ShaderProgram>;
        Handle<ShaderProgram> _ceguiRenderShader = INVALID_HANDLE<ShaderProgram>;
    };

};  // namespace Divide
#endif
