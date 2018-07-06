/*
Copyright (c) 2016 DIVIDE-Studio
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

#ifndef _HARDWARE_VIDEO_GFX_SHADER_DATA_INL_
#define _HARDWARE_VIDEO_GFX_SHADER_DATA_INL_

namespace Divide {

inline F32 GFXShaderData::GPUData::aspectRatio() const {
    return _cameraPosition.w;
}

inline vec2<F32> GFXShaderData::GPUData::currentZPlanes() const {
    return _ZPlanesCombined.xy();
}

inline F32 GFXShaderData::GPUData::FoV() const {
    return _renderProperties.z;
}

inline F32 GFXShaderData::GPUData::tanHFoV() const {
    return _renderProperties.w;
}

}; //namespace Divide
#endif //_HARDWARE_VIDEO_GFX_SHADER_DATA_INL_