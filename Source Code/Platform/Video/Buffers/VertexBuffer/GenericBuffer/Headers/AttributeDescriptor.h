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
        AttributeDescriptor();

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

        void attribIndex(U32 index) noexcept;
        void strideInBytes(size_t strideInBytes) noexcept;
        void bufferIndex(U32 bufferIndex) noexcept;
        void componentsPerElement(U32 componentsPerElement) noexcept;
        void interleavedOffsetInBytes(U32 interleavedOffsetInBytes) noexcept;
        void normalized(bool normalized) noexcept;
        void dataType(GFXDataFormat type) noexcept;
        void wasSet(bool wasSet) noexcept;
        void clean() noexcept;

        [[nodiscard]] U32 attribIndex() const noexcept { return _index; }
        [[nodiscard]] size_t strideInBytes() const noexcept { return _strideInBytes; }
        [[nodiscard]] U32 bufferIndex() const noexcept { return _parentBuffer; }
        [[nodiscard]] U32 componentsPerElement() const noexcept { return _componentsPerElement; }
        [[nodiscard]] bool normalized() const noexcept { return _normalized; }
        [[nodiscard]] U32 interleavedOffsetInBytes() const noexcept { return _interleavedOffset; }
        [[nodiscard]] GFXDataFormat dataType() const noexcept { return _type; }
        [[nodiscard]] bool wasSet() const noexcept { return _wasSet; }
        [[nodiscard]] bool dirty() const noexcept { return _dirty; }
 
    protected:
        U32 _index;
        U32 _parentBuffer;
        U32 _componentsPerElement;
        U32 _interleavedOffset;
        bool _wasSet;
        bool _dirty;
        bool _normalized;
        size_t _strideInBytes;
        GFXDataFormat _type;
    };
}; //namespace Divide

#endif //_ATTRIBUTE_DESCRIPTOR_H_
