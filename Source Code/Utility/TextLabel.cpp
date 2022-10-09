#include "stdafx.h"

#include "Headers/TextLabel.h"
#include "Core/Headers/StringHelper.h"

namespace Divide {

TextLabelStyle::TextLabelStyleMap TextLabelStyle::s_textLabelStyle;
SharedMutex TextLabelStyle::s_textLableStyleMutex = {};
size_t TextLabelStyle::s_defaultHashValue = 0;

TextLabelStyle::FontNameHashMap TextLabelStyle::s_fontName;

TextLabelStyle::TextLabelStyle(const char* font,
                               const UColour4& colour,
                               const U8 fontSize)
  : _fontSize(fontSize),
    _font(_ID(font)),
    _colour(colour)
{
    // First one found
    if (s_defaultHashValue == 0) {
        s_defaultHashValue = getHash();

        s_fontName[_ID(Font::DIVIDE_DEFAULT)] = Font::DIVIDE_DEFAULT;
        s_fontName[_ID(Font::BATANG)] = Font::BATANG;
        s_fontName[_ID(Font::DEJA_VU)] = Font::DEJA_VU;
        s_fontName[_ID(Font::DROID_SERIF)] = Font::DROID_SERIF;
        s_fontName[_ID(Font::DROID_SERIF_ITALIC)] = Font::DROID_SERIF_ITALIC;
        s_fontName[_ID(Font::DROID_SERIF_BOLD)] = Font::DROID_SERIF_BOLD;
    }
}

size_t TextLabelStyle::getHash() const {
    const size_t previousHash = _hash;
    _hash = 17;
    Util::Hash_combine(_hash, _font,
                              _fontSize,
                              _width,
                              _blurAmount,
                              _spacing,
                              _alignFlag,
                              _colour.r,
                              _colour.g,
                              _colour.b,
                              _colour.a,
                              _bold,
                              _italic);

    if (previousHash != _hash) {
        ScopedLock<SharedMutex> w_lock(s_textLableStyleMutex);
        insert(s_textLabelStyle, _hash, *this);
    }

    return Hashable::getHash();
}

const TextLabelStyle& TextLabelStyle::get(const size_t textLabelStyleHash) {
    bool styleFound = false;
    const TextLabelStyle& style = get(textLabelStyleHash, styleFound);
    // Return the state block's descriptor
    return style;
}

const TextLabelStyle& TextLabelStyle::get(const size_t textLabelStyleHash, bool& styleFound) {
    styleFound = false;

    SharedLock<SharedMutex> r_lock(s_textLableStyleMutex);
    // Find the render state block associated with the received hash value
    const TextLabelStyleMap::const_iterator it = s_textLabelStyle.find(textLabelStyleHash);
    if (it != std::cend(s_textLabelStyle)) {
        styleFound = true;
        return it->second;
    }

    return s_textLabelStyle.find(s_defaultHashValue)->second;
}

const Str64& TextLabelStyle::fontName(const size_t fontNameHash) {
    return s_fontName[fontNameHash];
}

TextElement::TextElement(const TextLabelStyle& textLabelStyle, const RelativePosition2D& position)
    : TextElement(textLabelStyle.getHash(), position)
{
}

TextElement::TextElement(const size_t textLabelStyleHash, const RelativePosition2D& position)
    : _textLabelStyleHash(textLabelStyleHash),
      _position(position)
{
}

void TextElement::text(const char* text, const bool multiLine) {
    if (multiLine) {
        Util::Split<TextType, eastl::string>(text, '\n', _text);
        return;
    }

    _text = { text };
}

TextElementBatch::TextElementBatch(const TextElement& element)
{
    _data.push_back(element);
}
} //namespace Divide