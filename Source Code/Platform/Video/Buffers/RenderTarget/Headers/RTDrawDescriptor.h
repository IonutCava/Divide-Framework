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
#ifndef _RENDER_TARGET_DRAW_DESCRIPTOR_H_
#define _RENDER_TARGET_DRAW_DESCRIPTOR_H_

#include "RTAttachment.h"
#include "Platform/Video/Headers/BlendingProperties.h"

namespace Divide {
    constexpr U16 INVALID_LAYER_INDEX = std::numeric_limits<U16>::max();

struct BlitIndex {
    U16 _index{ INVALID_LAYER_INDEX };
    U16 _layer{ 0u };
};

struct ColourBlitEntry {
    BlitIndex _input;
    BlitIndex _output;
};

struct DepthBlitEntry {
    U16 _inputLayer{ INVALID_LAYER_INDEX };
    U16 _outputLayer{ INVALID_LAYER_INDEX };
};

struct RTBlitParams {
    using ColourArray = std::array<ColourBlitEntry, RT_MAX_COLOUR_ATTACHMENTS>;
    DepthBlitEntry _blitDepth{};
    ColourArray _blitColours{};
};

struct RTDrawMask {
    std::array<bool, MAX_RT_COLOUR_ATTACHMENTS> _disabledColours = {
        false,
        false,
        false,
        false
    };

    bool _disabledDepth = false;
};

struct ClearColourEntry
{
    FColour4 _colour{ VECTOR4_ZERO };
    U8 _index{ MAX_RT_COLOUR_ATTACHMENTS };
};

struct RTDrawLayerParams {
    RTAttachmentType _type{ RTAttachmentType::COUNT };
    U8 _index{ 0u };
    U16 _layer{ 0 };
    U16 _mipLevel{ U16_MAX };
};

struct RTClearDescriptor {
    std::array<ClearColourEntry, MAX_RT_COLOUR_ATTACHMENTS> _clearColourDescriptors;
    bool _clearDepth = false;
    F32 _clearDepthValue = 1.f;
};

struct RTDrawLayerDescriptor {
    std::array<U16, MAX_RT_COLOUR_ATTACHMENTS> _colourLayers = create_array<MAX_RT_COLOUR_ATTACHMENTS>(INVALID_LAYER_INDEX);
    U16 _depthLayer = INVALID_LAYER_INDEX;
};

struct RTDrawDescriptor {
    bool _setViewport = true;
    bool _alphaToCoverage = false;
    RTDrawMask _drawMask{};
    U16 _mipWriteLevel{ U16_MAX };
    RTDrawLayerDescriptor _writeLayers{};
};

[[nodiscard]] bool IsEnabled(const RTDrawMask& mask, RTAttachmentType type) noexcept;
[[nodiscard]] bool IsEnabled(const RTDrawMask& mask, RTAttachmentType type, U8 index) noexcept;
void SetEnabled(RTDrawMask& mask, RTAttachmentType type, U8 index, bool state) noexcept;
void EnableAll(RTDrawMask& mask);
void DisableAll(RTDrawMask& mask);

bool operator==(const RTDrawMask& lhs, const RTDrawMask& rhs);
bool operator!=(const RTDrawMask& lhs, const RTDrawMask& rhs);
bool operator==(const RTDrawDescriptor& lhs, const RTDrawDescriptor& rhs);
bool operator!=(const RTDrawDescriptor& lhs, const RTDrawDescriptor& rhs);

}; //namespace Divide

#endif //_RENDER_TARGET_DRAW_DESCRIPTOR_H_


#include "RTDrawDescriptor.inl"