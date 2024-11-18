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
#ifndef DVD_LIGHT_COMPONENT_INL_
#define DVD_LIGHT_COMPONENT_INL_

namespace Divide
{
    inline void Light::getDiffuseColour( FColour3& colourOut ) const noexcept { Util::ToFloatColour( _colour.rgb, colourOut ); }

    inline FColour3 Light::getDiffuseColour() const noexcept { return Util::ToFloatColour( _colour.rgb ); }

    inline void Light::setDiffuseColour( const FColour3& newDiffuseColour ) noexcept { setDiffuseColour( Util::ToByteColour( newDiffuseColour ) ); }

    inline void Light::toggleEnabled() noexcept { enabled( !enabled() ); }

    inline const LightType& Light::getLightType() const noexcept { return _type; }

    inline F32 Light::distanceSquared( const float3& pos ) const noexcept { return positionCache().distanceSquared( pos ); }

    inline const Light::ShadowProperties& Light::getShadowProperties() const noexcept { return _shadowProperties; }

    inline const mat4<F32>& Light::getShadowVPMatrix( const U8 index ) const noexcept { assert( index < 6 ); return _shadowProperties._lightVP[index]; }

    inline F32 Light::getShadowFloatValues( const U8 index ) const noexcept { assert( index < 6 ); return _shadowProperties._lightPosition[index].w; }

    inline const float4& Light::getShadowLightPos( const U8 index ) const noexcept { assert( index < 6 ); return _shadowProperties._lightPosition[index]; }

    inline void Light::setShadowVPMatrix( const U8 index, const mat4<F32>& newValue ) noexcept { assert( index < 6 ); _shadowProperties._lightVP[index].set( newValue ); _shadowProperties._dirty = true; }

    inline void Light::setShadowFloatValue( const U8 index, const F32 newValue ) noexcept { assert( index < 6 ); _shadowProperties._lightPosition[index].w = newValue; _shadowProperties._dirty = true; }

    inline void Light::setShadowLightPos( const U8 index, const float3& newValue ) noexcept { _shadowProperties._lightPosition[index].xyz = newValue; _shadowProperties._dirty = true; }

    inline void Light::setShadowArrayOffset( const U16 offset ) noexcept {
        if ( getShadowArrayOffset() != offset )
        {
            _shadowProperties._lightDetails.y = offset == U16_MAX ? -1.f : to_F32( offset );
            _shadowProperties._dirty = true;
        }
    }

    inline void Light::cleanShadowProperties() noexcept
    {
        _shadowProperties._dirty = false;
        staticShadowsDirty( false );
        dynamicShadowsDirty( false );
    }

    inline U16 Light::getShadowArrayOffset() const noexcept
    {
        if ( _shadowProperties._lightDetails.y < 0.f )
        {
            return U16_MAX;
        }

        return to_U16( _shadowProperties._lightDetails.y );
    }

} //namespace Divide

#endif //DVD_LIGHT_COMPONENT_INL_
