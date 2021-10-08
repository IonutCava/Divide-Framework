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
#ifndef _UTILITY_TEXT_LABEL_H_
#define _UTILITY_TEXT_LABEL_H_

#include "Colours.h"
#include "Core/Math/Headers/Dimension.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Headers/Hashable.h"

namespace Divide {

namespace Font {
    const static char* DIVIDE_DEFAULT = "DroidSerif-Regular.ttf"; /*"Test.ttf"*/
    const static char* BATANG = "Batang.ttf";
    const static char* DEJA_VU = "DejaVuSans.ttf";
    const static char* DROID_SERIF = "DroidSerif-Regular.ttf";
    const static char* DROID_SERIF_ITALIC = "DroidSerif-Italic.ttf";
    const static char* DROID_SERIF_BOLD = "DroidSerif-Bold.ttf";
}

class TextLabelStyle final : public Hashable {
  protected:
    using TextLabelStyleMap = hashMap<size_t, TextLabelStyle>;
    static TextLabelStyleMap s_textLabelStyle;
    static SharedMutex s_textLableStyleMutex;
    static size_t s_defaultHashValue;

    using FontNameHashMap = hashMap<size_t, Str64>;
    static FontNameHashMap s_fontName;

  public:
    static const TextLabelStyle& get(size_t textLabelStyleHash);
    static const TextLabelStyle& get(size_t textLabelStyleHash, bool& styleFound);
    static const Str64& fontName(size_t fontNameHash);

   TextLabelStyle(const char* font, const UColour4& colour, U8 fontSize);

    [[nodiscard]] size_t getHash() const noexcept override;

    PROPERTY_RW(U8, fontSize, 1u);
    PROPERTY_RW(U8, width, 1u);
    PROPERTY_RW(F32, blurAmount, 0.f);
    PROPERTY_RW(F32, spacing, 0.f);
    PROPERTY_RW(U32, alignFlag, 0u); ///< Check font-stash alignment for details
    PROPERTY_RW(size_t, font, 0u);
    PROPERTY_RW(UColour4, colour, DefaultColours::BLACK_U8);
    PROPERTY_RW(bool, bold, false);
    PROPERTY_RW(bool, italic, false);
};

struct TextElement {
    using TextType = vector<eastl::string>;

    TextElement() = default;
    explicit TextElement(const TextLabelStyle& textLabelStyle, const RelativePosition2D& position)
        : TextElement(textLabelStyle.getHash(), position)
    {
    }

    explicit TextElement(const size_t textLabelStyleHash, const RelativePosition2D& position)
        : _textLabelStyleHash(textLabelStyleHash),
          _position(position)
    {
    }

    void text(const char* text, const bool multiLine) {
        if (multiLine) {
            Util::Split(text, '\n', _text);
            return;
        }

        _text = { text };
    }

    PROPERTY_R(size_t, textLabelStyleHash, 0);
    PROPERTY_R(TextType, text, {});
    PROPERTY_RW(RelativePosition2D, position);
};

struct TextElementBatch {
    using BatchType = vector_fast<TextElement>;

    TextElementBatch() = default;
    explicit TextElementBatch(const TextElement& element) {
        _data.push_back(element);
    }

    PROPERTY_RW(BatchType, data, {});
};

}  // namespace Divide

#endif //_UTILITY_TEXT_LABEL_H_