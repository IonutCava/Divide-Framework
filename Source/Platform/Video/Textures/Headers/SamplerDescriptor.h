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

#include "Utility/Headers/Colours.h"

namespace Divide {

 /// This struct is used to define all of the sampler settings needed to use a texture
 /// We do not define copy constructors as we must define descriptors only with POD
struct SamplerDescriptor 
{
    static constexpr size_t INVALID_SAMPLER_HASH = std::numeric_limits<size_t>::max();

    /// Used with custom border colours
    UColour4 _customBorderColour { DefaultColours::BLACK_U8 };
    /// OpenGL eg: used by TEXTURE_MIN_LOD and TEXTURE_MAX_LOD
    F32 _minLOD { -1000.f };
    F32 _maxLOD {  1000.f };
    /// OpenGL eg: used by TEXTURE_LOD_BIAS
    F32 _biasLOD { 0 };
    /// Texture filtering mode
    TextureFilter _minFilter { TextureFilter::LINEAR };
    TextureFilter _magFilter { TextureFilter::LINEAR };
    TextureMipSampling _mipSampling { TextureMipSampling::LINEAR };
    /// Texture wrap mode (Or S-R-T)
    TextureWrap _wrapU { TextureWrap::REPEAT };
    TextureWrap _wrapV { TextureWrap::REPEAT };
    TextureWrap _wrapW { TextureWrap::REPEAT };
    /// The value must be in the range [0...255] and is automatically clamped by the max HW supported level
    U8 _anisotropyLevel { 255 };
    /// Used with CLAMP_TO_BORDER as the background colour outside of the texture border
    TextureBorderColour _borderColour { TextureBorderColour::TRANSPARENT_BLACK_INT };
    ///Used for depth comparison (COUNT = disabled)
    ComparisonFunction _depthCompareFunc { ComparisonFunction::COUNT };
};

bool operator==( const SamplerDescriptor& lhs, const SamplerDescriptor& rhs ) noexcept;
bool operator!=( const SamplerDescriptor& lhs, const SamplerDescriptor& rhs ) noexcept;

[[nodiscard]] size_t GetHash(SamplerDescriptor descriptor) noexcept;

namespace XMLParser
{
    void saveToXML(const SamplerDescriptor& sampler, const string& entryName, boost::property_tree::ptree& pt);
    [[nodiscard]] SamplerDescriptor loadFromXML(const string& entryName, const boost::property_tree::ptree& pt);
};

} //namespace Divide 

#endif //_SAMPLER_DESCRIPTOR_H_

#include "SamplerDescriptor.inl"
