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
#ifndef _SAMPLER_DESCRIPTOR_INL_
#define _SAMPLER_DESCRIPTOR_INL_

namespace Divide
{
    inline bool operator==( const SamplerDescriptor& lhs, const SamplerDescriptor& rhs ) noexcept
    {
        return lhs._customBorderColour == rhs._customBorderColour &&
               lhs._minLOD == rhs._minLOD &&
               lhs._maxLOD == rhs._maxLOD &&
               lhs._biasLOD == rhs._biasLOD &&
               lhs._minFilter == rhs._minFilter &&
               lhs._magFilter == rhs._magFilter &&
               lhs._mipSampling == rhs._mipSampling &&
               lhs._wrapU == rhs._wrapU &&
               lhs._wrapV == rhs._wrapV &&
               lhs._wrapW == rhs._wrapW &&
               lhs._anisotropyLevel == rhs._anisotropyLevel &&
               lhs._borderColour == rhs._borderColour &&
               lhs._depthCompareFunc == rhs._depthCompareFunc;
    }

    inline bool operator!=( const SamplerDescriptor& lhs, const SamplerDescriptor& rhs ) noexcept
    {
        return lhs._customBorderColour != rhs._customBorderColour ||
               lhs._minLOD != rhs._minLOD ||
               lhs._maxLOD != rhs._maxLOD ||
               lhs._biasLOD != rhs._biasLOD ||
               lhs._minFilter != rhs._minFilter ||
               lhs._magFilter != rhs._magFilter ||
               lhs._mipSampling != rhs._mipSampling ||
               lhs._wrapU != rhs._wrapU ||
               lhs._wrapV != rhs._wrapV ||
               lhs._wrapW != rhs._wrapW ||
               lhs._anisotropyLevel != rhs._anisotropyLevel ||
               lhs._borderColour != rhs._borderColour ||
               lhs._depthCompareFunc != rhs._depthCompareFunc;
    }

    inline size_t GetHash( const SamplerDescriptor descriptor ) noexcept
    {
        size_t hash = 59;
        Util::Hash_combine( hash,
                            descriptor._customBorderColour.r,
                            descriptor._customBorderColour.g,
                            descriptor._customBorderColour.b,
                            descriptor._customBorderColour.a,
                            descriptor._minLOD,
                            descriptor._maxLOD,
                            descriptor._biasLOD,
                            to_base( descriptor._minFilter ),
                            to_base( descriptor._magFilter ),
                            to_base( descriptor._mipSampling ),
                            to_base( descriptor._wrapU ),
                            to_base( descriptor._wrapV ),
                            to_base( descriptor._wrapW ),
                            descriptor._anisotropyLevel,
                            to_base( descriptor._borderColour ),
                            to_base( descriptor._depthCompareFunc ));
        return hash;
    }

} //namespace Divide 

#endif //_SAMPLER_DESCRIPTOR_INL_
