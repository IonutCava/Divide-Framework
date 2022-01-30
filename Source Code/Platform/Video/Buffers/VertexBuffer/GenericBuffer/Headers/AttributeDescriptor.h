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
#ifndef _ATTRIBUTE_DESCRIPTOR_H_
#define _ATTRIBUTE_DESCRIPTOR_H_

#include "Platform/Video/Headers/RenderAPIEnums.h"

namespace Divide {

    struct AttributeDescriptor {
        void set(U32 bufferIndex,
                 U32 componentsPerElement,
                 GFXDataFormat dataType) noexcept;

        void set(U32 bufferIndex,
                 U32 componentsPerElement,
                 GFXDataFormat dataType,
                 bool normalized) noexcept;

        void set(U32 bufferIndex,
                 U32 componentsPerElement,
                 GFXDataFormat dataType,
                 bool normalized,
                 size_t strideInBytes) noexcept;

        void index(U32 index) noexcept;
        void parentBuffer(U32 bufferIndex) noexcept;
        void componentsPerElement(U32 componentsPerElement) noexcept;
        void wasSet(bool wasSet) noexcept;
        void clean() noexcept;
        void normalized(bool normalized) noexcept;
        void enabled(bool state) noexcept;
        void strideInBytes(size_t strideInBytes) noexcept;
        void dataType(GFXDataFormat type) noexcept;

        PROPERTY_R(U32, index, 0u);
        PROPERTY_R(U32, parentBuffer, 0u);
        PROPERTY_R(U32, componentsPerElement, 0u);
        PROPERTY_R(bool, wasSet, false);
        PROPERTY_R(bool, dirty, false);
        PROPERTY_R(bool, normalized, false);
        PROPERTY_R(bool, enabled, true);
        PROPERTY_R(size_t, strideInBytes, 0u);
        PROPERTY_R(GFXDataFormat, dataType, GFXDataFormat::UNSIGNED_INT);
    };
}; //namespace Divide

#endif //_ATTRIBUTE_DESCRIPTOR_H_
