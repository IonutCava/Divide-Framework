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
#ifndef _BLENDING_PROPERTIES_H_
#define _BLENDING_PROPERTIES_H_

#include "RenderAPIEnums.h"

namespace Divide {

struct BlendingProperties {

    PROPERTY_RW(BlendProperty,  blendSrc, BlendProperty::ONE);
    PROPERTY_RW(BlendProperty,  blendDest, BlendProperty::ZERO);
    PROPERTY_RW(BlendOperation, blendOp, BlendOperation::ADD);
    PROPERTY_RW(BlendProperty,  blendSrcAlpha, BlendProperty::ONE);
    PROPERTY_RW(BlendProperty,  blendDestAlpha, BlendProperty::ZERO);
    PROPERTY_RW(BlendOperation, blendOpAlpha, BlendOperation::COUNT);
    PROPERTY_RW(bool, enabled, false);
};

[[nodiscard]] size_t GetHash(const BlendingProperties& properties);
bool operator==(const BlendingProperties& lhs, const BlendingProperties& rhs) noexcept;
bool operator!=(const BlendingProperties& lhsc, const BlendingProperties& rhs) noexcept;

// 4 should be more than enough even for batching multiple render targets together
constexpr U8 MAX_RT_COLOUR_ATTACHMENTS = 4;

struct RTBlendState {
    UColour4 _blendColour = { 0u, 0u, 0u, 0u };
    BlendingProperties _blendProperties;
};

bool operator==(const RTBlendState& lhs, const RTBlendState& rhs) noexcept;
bool operator!=(const RTBlendState& lhs, const RTBlendState& rhs) noexcept;

// Blend state 0 with no RT bound == Global blend
using RTBlendStates = std::array<RTBlendState, MAX_RT_COLOUR_ATTACHMENTS>;

[[nodiscard]] size_t GetHash(const RTBlendStates& blendStates);

}; //namespace Divide

#endif //_BLENDING_PROPERTIES_H_