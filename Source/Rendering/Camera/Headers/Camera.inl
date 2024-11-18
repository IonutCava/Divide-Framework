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
#ifndef DVD_CAMERA_INL_
#define DVD_CAMERA_INL_

namespace Divide
{
    namespace
    {
        F32 s_lastFrameTimeSec = 0.f;
    }

    inline const CameraSnapshot& Camera::snapshot() const noexcept
    {
        return _data;
    }

    inline void Camera::setGlobalRotation( const vec3<Angle::DEGREES_F>& euler ) noexcept
    {
        setGlobalRotation( euler.yaw, euler.pitch, euler.roll );
    }

    /// Sets the camera to point at the specified target point
    inline const mat4<F32>& Camera::lookAt( const float3& target )
    {
        return lookAt( _data._eye, target );
    }

    inline const mat4<F32>& Camera::lookAt( const float3& eye, const float3& target )
    {
        return lookAt( eye, target, viewMatrix().getUpVec() );
    }

    inline void Camera::setYaw( const Angle::DEGREES_F angle ) noexcept
    {
        setRotation( angle, _euler.pitch, _euler.roll );
    }

    /// Sets the camera's Pitch angle. Yaw and Roll are previous extracted values
    inline void Camera::setPitch( const Angle::DEGREES_F angle ) noexcept
    {
        setRotation( _euler.yaw, angle, _euler.roll );
    }

    /// Sets the camera's Roll angle. Yaw and Pitch are previous extracted values
    inline void Camera::setRoll( const Angle::DEGREES_F angle ) noexcept
    {
        setRotation( _euler.yaw, _euler.pitch, angle );
    }

    /// Sets the camera's Yaw angle.
    /// This creates a new orientation quaternion for the camera and extracts the Euler angles
    inline void Camera::setGlobalYaw( const Angle::DEGREES_F angle ) noexcept
    {
        setGlobalRotation( angle, _euler.pitch, _euler.roll );
    }

    /// Sets the camera's Pitch angle. Yaw and Roll are previous extracted values
    inline void Camera::setGlobalPitch( const Angle::DEGREES_F angle ) noexcept
    {
        setGlobalRotation( _euler.yaw, angle, _euler.roll );
    }

    /// Sets the camera's Roll angle. Yaw and Pitch are previous extracted values
    inline void Camera::setGlobalRoll( const Angle::DEGREES_F angle ) noexcept
    {
        setGlobalRotation( _euler.yaw, _euler.pitch, angle );
    }

    inline void Camera::setEye( const F32 x, const F32 y, const F32 z ) noexcept
    {
        _data._eye.set( x, y, z );
        _viewMatrixDirty = true;
    }

    inline void Camera::setEye( const float3& position ) noexcept
    {
        setEye( position.x, position.y, position.z );
    }

    inline void Camera::setRotation( const Quaternion<F32>& q ) noexcept
    {
        _data._orientation = q;
        _viewMatrixDirty = true;
    }

    /// Creates a quaternion based on the specified axis-angle and calls "rotate" to change the orientation
    inline void Camera::rotate( const float3& axis, const Angle::DEGREES_F angle )
    {
        rotate( Quaternion<F32>( axis, Angle::to_RADIANS(angle * speedFactor().turn * s_lastFrameTimeSec )) );
    }

    inline void Camera::moveForward( const F32 factor ) noexcept
    {
        move( 0.0f, 0.0f, factor );
    }

    /// Moves the camera left or right
    inline void Camera::moveStrafe( const F32 factor ) noexcept
    {
        move( factor, 0.0f, 0.0f );
    }

    /// Moves the camera up or down
    inline void Camera::moveUp( const F32 factor ) noexcept
    {
        move( 0.0f, factor, 0.0f );
    }

    /// Exactly as in Ogre3D: locks the yaw movement to the specified axis
    inline void Camera::setFixedYawAxis( const bool useFixed, const float3& fixedAxis ) noexcept
    {
        _yawFixed = useFixed;
        _fixedYawAxis = fixedAxis;
    }

    inline void Camera::setEuler( const vec3<Angle::DEGREES_F>& euler ) noexcept
    {
        setRotation( euler.yaw, euler.pitch, euler.roll );
    }

    inline void Camera::setEuler( const Angle::DEGREES_F& pitch, const Angle::DEGREES_F& yaw, const Angle::DEGREES_F& roll ) noexcept
    {
        setRotation( yaw, pitch, roll );
    }

    inline const mat4<F32>& Camera::viewMatrix() const noexcept
    {
        return _data._viewMatrix;
    }

    inline const mat4<F32>& Camera::viewMatrix()       noexcept
    {
        updateViewMatrix();
        return _data._viewMatrix;
    }

    inline const mat4<F32>& Camera::projectionMatrix() const noexcept
    {
        return _data._projectionMatrix;
    }

    inline const mat4<F32>& Camera::projectionMatrix()       noexcept
    {
        updateProjection();
        return _data._projectionMatrix;
    }

    inline const mat4<F32>& Camera::viewProjectionMatrix()       noexcept
    {
        updateViewMatrix();
        updateProjection();
        return _viewProjectionMatrix;
    }

    inline const mat4<F32>& Camera::viewProjectionMatrix() const noexcept
    {
        return _viewProjectionMatrix;
    }

    inline const mat4<F32>& Camera::worldMatrix()            noexcept
    {
        updateViewMatrix();
        return _data._invViewMatrix;
    }

    inline const mat4<F32>& Camera::worldMatrix()      const noexcept
    {
        return _data._invViewMatrix;
    }

    inline const Frustum& Camera::getFrustum() const noexcept
    {
        return _frustum;
    }

    inline  Frustum& Camera::getFrustum() noexcept
    {
        updateFrustum();
        return _frustum;
    }

    inline  float3 Camera::unProject( const float3& winCoords, const Rect<I32>& viewport ) const noexcept
    {
        return unProject( winCoords.x, winCoords.y, viewport );
    }

    template<bool zeroToOneDepth>
    mat4<F32> Camera::Ortho( const F32 left, const F32 right, const F32 bottom, const F32 top, const F32 zNear, const F32 zFar )  noexcept
    {
        mat4<F32> ret{ MAT4_ZERO };

        ret.m[0][0] = 2.f / (right - left);
        ret.m[1][1] = 2.f / (top - bottom);
        ret.m[3][3] = 1;

        ret.m[3][0] = -( right + left ) / (right - left);
        ret.m[3][1] = -( top + bottom ) / (top - bottom);

        if constexpr ( zeroToOneDepth )
        {
            ret.m[2][2] = -1.f / (zFar - zNear);
            ret.m[3][2] = -zNear / (zFar - zNear);
        }
        else
        {
            ret.m[2][2] = -2.f / (zFar - zNear);
            ret.m[3][2] = -( zFar + zNear ) / (zFar - zNear);
        }

        return ret;
    }

    template<bool zeroToOneDepth>
    mat4<F32> Camera::Perspective( const Angle::DEGREES_F fovy, const F32 aspect, const F32 zNear, const F32 zFar) noexcept
    {
        mat4<F32> ret{ MAT4_ZERO };

        assert( !IS_ZERO( aspect ) );
        assert( zFar > zNear );

        Angle::RADIANS_F tanHalfFovy = std::tan( Angle::to_RADIANS( fovy ) * 0.5f );

        ret.m[0][0] = 1.f / (aspect * tanHalfFovy);
        ret.m[1][1] = 1.f / tanHalfFovy;
        ret.m[2][3] = -1.f;

        if constexpr ( zeroToOneDepth )
        {
            ret.m[2][2] = zFar / (zNear - zFar);
            ret.m[3][2] = -(zFar * zNear) / (zFar - zNear);
        }
        else
        {
            ret.m[2][2] = -( zFar + zNear ) / (zFar - zNear);
            ret.m[3][2] = -2.f * zFar * zNear / (zFar - zNear);
        }

        return ret;
    }

    template<bool zeroToOneDepth>
    mat4<F32> Camera::FrustumMatrix( const F32 left, const F32 right, const F32 bottom, const F32 top, const F32 nearVal, const F32 farVal) noexcept
    {
        mat4<F32> ret{MAT4_ZERO};

        ret.m[0][0] = 2.f * nearVal / (right - left);
        ret.m[1][1] = 2.f * nearVal / (top - bottom);
        ret.m[2][0] = ( right + left ) / (right - left);
        ret.m[2][1] = ( top + bottom ) / (top - bottom);
        ret.m[2][3] = -1.f;

        if constexpr ( zeroToOneDepth )
        {
            ret.m[2][2] = farVal / (nearVal - farVal);
            ret.m[3][2] = -(farVal * nearVal) / (farVal - nearVal);
        }
        else
        {
            ret.m[2][2] = -( farVal + nearVal ) / (farVal - nearVal);
            ret.m[3][2] = -2.f * farVal * nearVal / (farVal - nearVal);
        }

        return ret;
    }

}; //namespace Divide

#endif //DVD_CAMERA_INL_
