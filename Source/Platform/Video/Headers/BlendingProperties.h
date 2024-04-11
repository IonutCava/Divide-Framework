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
#ifndef DVD_BLENDING_PROPERTIES_H_
#define DVD_BLENDING_PROPERTIES_H_

namespace Divide {

struct BlendingSettings
{

    PROPERTY_RW(BlendProperty,  blendSrc, BlendProperty::ONE);
    PROPERTY_RW(BlendProperty,  blendDest, BlendProperty::ZERO);
    PROPERTY_RW(BlendOperation, blendOp, BlendOperation::ADD);
    PROPERTY_RW(BlendProperty,  blendSrcAlpha, BlendProperty::ONE);
    PROPERTY_RW(BlendProperty,  blendDestAlpha, BlendProperty::ZERO);
    PROPERTY_RW(BlendOperation, blendOpAlpha, BlendOperation::COUNT);
    PROPERTY_RW(bool, enabled, false);

    bool operator==(const BlendingSettings&) const = default;
};

[[nodiscard]] size_t GetHash(const BlendingSettings& properties);

// Blend state 0 with no RT bound == Global blend
struct RTBlendStates
{
    UColour4 _blendColour = { 0u, 0u, 0u, 0u };
    std::array<BlendingSettings, to_base( RTColourAttachmentSlot::COUNT)> _settings;

    bool operator==(const RTBlendStates&) const = default;
};

[[nodiscard]] size_t GetHash(const RTBlendStates& blendStates);

}; //namespace Divide

#endif //DVD_BLENDING_PROPERTIES_H_
