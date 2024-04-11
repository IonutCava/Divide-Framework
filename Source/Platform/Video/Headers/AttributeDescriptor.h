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
#ifndef DVD_ATTRIBUTE_DESCRIPTOR_H_
#define DVD_ATTRIBUTE_DESCRIPTOR_H_

namespace Divide {

    struct AttributeDescriptor
    {
        size_t _strideInBytes{ 0u };
        U16 _vertexBindingIndex{ 0u };
        U8 _componentsPerElement{ 0u };
        GFXDataFormat _dataType{ GFXDataFormat::COUNT };
        bool _normalized{ false };


        bool operator==(const AttributeDescriptor&) const = default;
    };

    struct VertexBinding
    {
        size_t _strideInBytes{ 0u };
        U16 _bufferBindIndex{0u};
        bool _perVertexInputRate{ true };


        bool operator==(const VertexBinding&) const = default;
    };

    struct AttributeMap
    {
        using Attributes = std::array<AttributeDescriptor, to_base( AttribLocation::COUNT )>;
        using VertexBindings = vector<VertexBinding>;
        Attributes _attributes;
        VertexBindings _vertexBindings;

        bool operator==(const AttributeMap&) const = default;
    };

    size_t GetHash(const AttributeDescriptor& descriptor);
    size_t GetHash(const VertexBinding& vertexBinding);
    size_t GetHash(const AttributeMap& attributes);

}; //namespace Divide

#endif //DVD_ATTRIBUTE_DESCRIPTOR_H_
