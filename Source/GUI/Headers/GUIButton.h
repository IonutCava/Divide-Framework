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
#ifndef DVD_GUI_BUTTON_H_
#define DVD_GUI_BUTTON_H_

#include "GUIElement.h"
#include "GUIText.h"

namespace CEGUI {
    class Window;
    class Font;
    class EventArgs;
};

namespace Divide
{

class AudioDescriptor;

class GUIButton final : public GUIElementBase<GUIType::GUI_BUTTON>
{
    using ButtonCallback = DELEGATE<void, I64>;
    using AudioCallback = DELEGATE<void, Handle<AudioDescriptor>>;

    friend class GUIInterface;
    friend class SceneGUIElements;

    public:
    enum class Event : U8
    {
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
    void setTooltip(std::string_view tooltipText) override;
    void setText(std::string_view text) const;
    void setFont(std::string_view fontName, std::string_view fontFileName, U32 size) const;
    void active(bool active) noexcept override;
    void visible(bool visible) noexcept override;

    void setEventCallback(Event event, const ButtonCallback& callback);
    void setEventSound(Event event, Handle<AudioDescriptor> sound);
    void setEventCallback(Event event, const ButtonCallback& callback, Handle<AudioDescriptor> sound);

    // return false if we replace an existing callback
    static bool soundCallback(const AudioCallback& cbk);

   protected:
    GUIButton(std::string_view name,
              std::string_view text,
              std::string_view guiScheme, 
              const RelativePosition2D& offset,
              const RelativeScale2D& size,
              CEGUI::Window* parent);

    ~GUIButton() override;

    bool onEvent(Event event, const CEGUI::EventArgs& /*e*/);

   protected:
    /// A pointer to a function to call if the button is pressed
    std::array<ButtonCallback, to_base(Event::COUNT)> _callbackFunction;
    std::array<Handle<AudioDescriptor>, to_base(Event::COUNT)> _eventSound;
    std::array<CEGUI::Event::Connection, to_base(Event::COUNT)> _connections;

    CEGUI::Window* _btnWindow;

    static AudioCallback s_soundCallback;
};

};  // namespace Divide

#endif //DVD_GUI_BUTTON_H_
