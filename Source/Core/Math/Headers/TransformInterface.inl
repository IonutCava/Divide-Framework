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
#ifndef _TRANSFORM_INTERFACE_INL_
#define _TRANSFORM_INTERFACE_INL_

namespace Divide
{
    inline bool operator==( const TransformValues& lhs, const TransformValues& rhs )
    {
        return lhs._scale.compare( rhs._scale ) &&
               lhs._orientation.compare( rhs._orientation ) &&
               lhs._translation.compare( rhs._translation );
    }

    inline bool operator!=( const TransformValues& lhs, const TransformValues& rhs )
    {
        return !lhs._scale.compare( rhs._scale ) ||
               !lhs._orientation.compare( rhs._orientation ) ||
               !lhs._translation.compare( rhs._translation );
    }

    inline TransformValues Lerp( const TransformValues& a, const TransformValues& b, const F32 t )
    {
        return TransformValues 
        {
            ._orientation = Slerp( a._orientation, b._orientation, t ),
            ._translation = Lerp( a._translation, b._translation, t ),
            ._scale = Lerp( a._scale, b._scale, t )
        };
    }

    inline mat4<F32> GetMatrix( const TransformValues& values )
    {
        return mat4<F32>
        {
            values._translation,
            values._scale,
            GetMatrix( values._orientation )
        };
    }

    inline void ITransform::setScale( const F32 ammout )
    {
        setScale( vec3<F32>( ammout ) );
    }

    inline void ITransform::setScale( const F32 x, const F32 y, const F32 z )
    {
        setScale( vec3<F32>( x, y, z ) );
    }

    inline void ITransform::setRotationEuler( const vec3<F32>& euler )
    {
        setRotation( euler.pitch, euler.yaw, euler.roll );
    }

    inline void ITransform::translate( const F32 x, const F32 y, const F32 z )
    {
        translate( vec3<F32>( x, y, z ) );
    }

    inline void ITransform::translateX( const F32 positionX )
    {
        translate( vec3<F32>( positionX, 0.0f, 0.0f ) );
    }

    inline void ITransform::translateY( const F32 positionY )
    {
        translate( vec3<F32>( 0.0f, positionY, 0.0f ) );
    }

    inline void ITransform::translateZ( const F32 positionZ )
    {
        translate( vec3<F32>( 0.0f, 0.0f, positionZ ) );
    }

    inline void ITransform::scale( const F32 amount )
    {
        scale( vec3<F32>( amount ) );
    }

    inline void ITransform::scale( const F32 x, const F32 y, const F32 z )
    {
        scale( vec3<F32>( x, y, z ) );
    }

    inline void ITransform::rotate( const F32 xAxis, const F32 yAxis, const F32 zAxis, const Angle::DEGREES<F32> degrees )
    {
        rotate( vec3<F32>( xAxis, yAxis, zAxis ), degrees );
    }

    inline void ITransform::rotate( const vec3<F32>& euler )
    {
        rotate( euler.pitch, euler.yaw, euler.roll );
    }
} //namespace Divide

#endif //_TRANSFORM_INTERFACE_INL_
