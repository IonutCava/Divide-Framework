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
#ifndef _SAMPLER_DESCRIPTOR_H_
#define _SAMPLER_DESCRIPTOR_H_

#include "Core/Headers/Hashable.h"
#include "Utility/Headers/Colours.h"
#include "Platform/Video/Headers/RenderAPIEnums.h"

namespace Divide {

 /// This struct is used to define all of the sampler settings needed to use a texture
 /// We do not define copy constructors as we must define descriptors only with POD
struct SamplerDescriptor final : Hashable {

    SamplerDescriptor();

    [[nodiscard]] size_t getHash() const override;

    void wrapUVW(const TextureWrap wrap) noexcept;

    /// Texture filtering mode
    PROPERTY_RW(TextureFilter, minFilter, TextureFilter::LINEAR);
    PROPERTY_RW(TextureFilter, magFilter, TextureFilter::LINEAR);
    PROPERTY_RW(TextureMipSampling, mipSampling, TextureMipSampling::LINEAR);

    /// Texture wrap mode (Or S-R-T)
    PROPERTY_RW(TextureWrap, wrapU, TextureWrap::REPEAT);
    PROPERTY_RW(TextureWrap, wrapV, TextureWrap::REPEAT);
    PROPERTY_RW(TextureWrap, wrapW, TextureWrap::REPEAT);
    ///use red channel as comparison (e.g. for shadows)
    PROPERTY_RW(bool, useRefCompare, false);
    ///Used by RefCompare
    PROPERTY_RW(ComparisonFunction, cmpFunc, ComparisonFunction::LEQUAL);
    /// The value must be in the range [0...255] and is automatically clamped by the max HW supported level
    PROPERTY_RW(U8, anisotropyLevel, 255);
    /// OpenGL eg: used by TEXTURE_MIN_LOD and TEXTURE_MAX_LOD
    PROPERTY_RW(F32, minLOD, -1000.f);
    PROPERTY_RW(F32, maxLOD, 1000.f);
    /// OpenGL eg: used by TEXTURE_LOD_BIAS
    PROPERTY_RW(F32, biasLOD, 0);
    /// Used with CLAMP_TO_BORDER as the background colour outside of the texture border
    PROPERTY_RW(TextureBorderColour, borderColour, TextureBorderColour::TRANSPARENT_BLACK_INT);
    /// Used with custom border colours
    PROPERTY_RW(UColour4, customBorderColour, DefaultColours::BLACK_U8);

public:
    static void Clear();
    /// Retrieve a sampler descriptor by hash value.
    /// If the hash value does not exist in the descriptor map, return the default descriptor
    static const SamplerDescriptor& Get(size_t samplerDescriptorHash);
    /// Returns false if the specified hash is not found in the map
    static const SamplerDescriptor& Get(size_t samplerDescriptorHash, bool& descriptorFound);

protected:
    using SamplerDescriptorMap = hashMap<size_t, SamplerDescriptor, NoHash<size_t>>;
    static SamplerDescriptorMap s_samplerDescriptorMap;
    static SharedMutex s_samplerDescriptorMapMutex;

public:
    static size_t s_defaultHashValue;
};

namespace XMLParser {
    void saveToXML(const SamplerDescriptor& sampler, const string& entryName, boost::property_tree::ptree& pt);
    [[nodiscard]] size_t loadFromXML(const string& entryName, const boost::property_tree::ptree& pt);
};

} //namespace Divide 

#endif //_SAMPLER_DESCRIPTOR_H_

#include "SamplerDescriptor.inl"
