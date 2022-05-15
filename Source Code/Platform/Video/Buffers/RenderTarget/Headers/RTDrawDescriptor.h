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

struct RTDrawMask {
    std::array<bool, MAX_RT_COLOUR_ATTACHMENTS> _disabledColours = {
        false,
        false,
        false,
        false
    };

    bool _disabledDepth = false;
};


struct RTClearColourDescriptor
{
    std::array<FColour4, MAX_RT_COLOUR_ATTACHMENTS> _customClearColour = {
        DefaultColours::BLACK,
        DefaultColours::BLACK,
        DefaultColours::BLACK,
        DefaultColours::BLACK
    };

    F32 _customClearDepth = 1.0f;
};

struct RTClearDescriptor {
    RTClearColourDescriptor* _customClearColour = nullptr;
    std::array<bool, MAX_RT_COLOUR_ATTACHMENTS> _clearColourAttachment = {
        true,
        true,
        true,
        true
    };
    bool _clearDepth = true;
    bool _clearColours = true;
    bool _clearExternalColour = false;
    bool _clearExternalDepth = false;
    bool _resetToDefault = true;
};

struct RTDrawDescriptor {
    bool _setViewport = true;
    bool _setDefaultState = true;
    bool _alphaToCoverage = false;
    RTDrawMask _drawMask{};
};

[[nodiscard]] bool IsEnabled(const RTDrawMask& mask, RTAttachmentType type) noexcept;
[[nodiscard]] bool IsEnabled(const RTDrawMask& mask, RTAttachmentType type, U8 index) noexcept;
void SetEnabled(RTDrawMask& mask, RTAttachmentType type, U8 index, bool state) noexcept;
void EnableAll(RTDrawMask& mask);
void DisableAll(RTDrawMask& mask);

bool operator==(const RTDrawMask& lhs, const RTDrawMask& rhs) noexcept;
bool operator!=(const RTDrawMask& lhs, const RTDrawMask& rhs) noexcept;
bool operator==(const RTDrawDescriptor& lhs, const RTDrawDescriptor& rhs) noexcept;
bool operator!=(const RTDrawDescriptor& lhs, const RTDrawDescriptor& rhs) noexcept;

}; //namespace Divide

#endif //_RENDER_TARGET_DRAW_DESCRIPTOR_H_


#include "RTDrawDescriptor.inl"