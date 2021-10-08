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
#ifndef _GUI_BUTTON_H_
#define _GUI_BUTTON_H_

#include "GUIElement.h"
#include "GUIText.h"

namespace CEGUI {
class Window;
class Font;
class EventArgs;
};

namespace Divide {

class AudioDescriptor;
TYPEDEF_SMART_POINTERS_FOR_TYPE(AudioDescriptor);

class GUIButton final : public GUIElementBase<GUIType::GUI_BUTTON> {
    using ButtonCallback = DELEGATE<void, I64>;
    using AudioCallback = DELEGATE<void, AudioDescriptor_ptr>;

    friend class GUIInterface;
    friend class SceneGUIElements;

    public:
    enum class Event : U8 {
        HoverEnter = 0,
        HoverLeave,
        MouseDown,
        MouseUp,
        MouseMove,
        MouseClick,
        MouseDoubleClick,
        MouseTripleClick,
        COUNT
    };

   public:
    void setTooltip(const string& tooltipText) override;
    void setText(const std::string_view& text) const;
    void setFont(const string& fontName, const string& fontFileName, U32 size) const;
    void active(const bool& active) noexcept override;
    void visible(const bool& visible) noexcept override;

    void setEventCallback(Event event, const ButtonCallback& callback);
    void setEventSound(Event event, const AudioDescriptor_ptr& sound);
    void setEventCallback(Event event, const ButtonCallback& callback, const AudioDescriptor_ptr& sound);

    // return false if we replace an existing callback
    static bool soundCallback(const AudioCallback& cbk);

   protected:
    GUIButton(const string& name,
              const string& text,
              const string& guiScheme, 
              const RelativePosition2D& offset,
              const RelativeScale2D& size,
              CEGUI::Window* parent);

    ~GUIButton();

    bool onEvent(Event event, const CEGUI::EventArgs& /*e*/);

   protected:;
    /// A pointer to a function to call if the button is pressed
    std::array<ButtonCallback, to_base(Event::COUNT)> _callbackFunction;
    std::array<AudioDescriptor_ptr, to_base(Event::COUNT)> _eventSound;
    std::array<CEGUI::Event::Connection, to_base(Event::COUNT)> _connections;

    CEGUI::Window* _btnWindow;

    static AudioCallback s_soundCallback;
};

};  // namespace Divide

#endif