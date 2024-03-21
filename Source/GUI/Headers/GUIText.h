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
#ifndef DVD_GUI_TEXT_H_
#define DVD_GUI_TEXT_H_

#include "GUIElement.h"
#include "Utility/Headers/TextLabel.h"

namespace Divide {

class GUIText final : public GUIElementBase<GUIType::GUI_TEXT>, public TextElement {
    friend class GUIInterface;

   public:
    GUIText(const string& name,
            const string& text,
            bool  multiLine,
            const RelativePosition2D& relativePosition,
            const string& font,
            const UColour4& colour,
            CEGUI::Window* parent,
            U8 fontSize = 16u);

    [[nodiscard]] const RelativePosition2D& getPosition() const noexcept;
};

};  // namespace Divide

#endif //DVD_GUI_TEXT_H_
