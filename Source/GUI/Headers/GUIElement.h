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
#ifndef DVD_GUI_ELEMENT_H_
#define DVD_GUI_ELEMENT_H_

#include <CEGUI/Event.h>

namespace CEGUI {
    class Window;
};

namespace Divide {

namespace GFX {
    class CommandBuffer;
};

enum class GUIType : U8 {
    GUI_TEXT = 0,
    GUI_BUTTON = 1,
    GUI_FLASH = 2,
    GUI_CONSOLE = 3,
    GUI_MESSAGE_BOX = 4,
    GUI_CONFIRM_DIALOG = 5,
    COUNT
};

class GFXDevice;
struct SizeChangeParams;

template<GUIType TypeEnum>
struct TypeHelper {
    static const GUIType STATIC_GUI_TYPE_ID = TypeEnum;
};

class GUIElement : public GUIDWrapper {
    friend class GUI;
  public:
    GUIElement(string name, CEGUI::Window* parent) noexcept;
    
    virtual void setTooltip([[maybe_unused]] const string& tooltipText) {}

    PROPERTY_RW(string, name, "");

    VIRTUAL_PROPERTY_RW(bool, visible, true);
    VIRTUAL_PROPERTY_RW(bool, active, false);

    static constexpr GUIType Type = GUIType::COUNT;

    [[nodiscard]] virtual GUIType type() const noexcept { return Type; }

  protected:
    CEGUI::Window* _parent;
};

template<GUIType EnumVal>
struct GUIElementBase : GUIElement {

    GUIElementBase(const string& name, CEGUI::Window* const parent)
        : GUIElement(name, parent)
    {}

    static constexpr GUIType Type = EnumVal;

    [[nodiscard]] GUIType type() const noexcept override { return Type; }
};

};  // namespace Divide

#endif //DVD_GUI_ELEMENT_H_
