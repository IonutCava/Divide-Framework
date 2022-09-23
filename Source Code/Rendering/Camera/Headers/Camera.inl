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
#ifndef _CAMERA_INL_
#define _CAMERA_INL_

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

    inline void Camera::setGlobalRotation( const vec3<Angle::DEGREES<F32>>& euler ) noexcept
    {
        setGlobalRotation( euler.yaw, euler.pitch, euler.roll );
    }

    /// Sets the camera to point at the specified target point
    inline const mat4<F32>& Camera::lookAt( const vec3<F32>& target )
    {
        return lookAt( _data._eye, target );
    }

    inline const mat4<F32>& Camera::lookAt( const vec3<F32>& eye, const vec3<F32>& target )
    {
        return lookAt( eye, target, viewMatrix().getUpVec() );
    }

    inline void Camera::setYaw( const Angle::DEGREES<F32> angle ) noexcept
    {
        setRotation( angle, _euler.pitch, _euler.roll );
    }

    /// Sets the camera's Pitch angle. Yaw and Roll are previous extracted values
    inline void Camera::setPitch( const Angle::DEGREES<F32> angle ) noexcept
    {
        setRotation( _euler.yaw, angle, _euler.roll );
    }

    /// Sets the camera's Roll angle. Yaw and Pitch are previous extracted values
    inline void Camera::setRoll( const Angle::DEGREES<F32> angle ) noexcept
    {
        setRotation( _euler.yaw, _euler.pitch, angle );
    }

    /// Sets the camera's Yaw angle.
    /// This creates a new orientation quaternion for the camera and extracts the Euler angles
    inline void Camera::setGlobalYaw( const Angle::DEGREES<F32> angle ) noexcept
    {
        setGlobalRotation( angle, _euler.pitch, _euler.roll );
    }

    /// Sets the camera's Pitch angle. Yaw and Roll are previous extracted values
    inline void Camera::setGlobalPitch( const Angle::DEGREES<F32> angle ) noexcept
    {
        setGlobalRotation( _euler.yaw, angle, _euler.roll );
    }

    /// Sets the camera's Roll angle. Yaw and Pitch are previous extracted values
    inline void Camera::setGlobalRoll( const Angle::DEGREES<F32> angle ) noexcept
    {
        setGlobalRotation( _euler.yaw, _euler.pitch, angle );
    }

    inline void Camera::setEye( const F32 x, const F32 y, const F32 z ) noexcept
    {
        _data._eye.set( x, y, z );
        _viewMatrixDirty = true;
    }

    inline void Camera::setEye( const vec3<F32>& position ) noexcept
    {
        setEye( position.x, position.y, position.z );
    }

    inline void Camera::setRotation( const Quaternion<F32>& q ) noexcept
    {
        _data._orientation = q;
        _viewMatrixDirty = true;
    }

    /// Creates a quaternion based on the specified axis-angle and calls "rotate" to change the orientation
    inline void Camera::rotate( const vec3<F32>& axis, const Angle::DEGREES<F32> angle )
    {
        rotate( Quaternion<F32>( axis, angle * speedFactor().turn * s_lastFrameTimeSec ) );
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
    inline void Camera::setFixedYawAxis( const bool useFixed, const vec3<F32>& fixedAxis ) noexcept
    {
        _yawFixed = useFixed;
        _fixedYawAxis = fixedAxis;
    }

    inline void Camera::setEuler( const vec3<Angle::DEGREES<F32>>& euler ) noexcept
    {
        setRotation( euler.yaw, euler.pitch, euler.roll );
    }

    inline void Camera::setEuler( const Angle::DEGREES<F32>& pitch, const Angle::DEGREES<F32>& yaw, const Angle::DEGREES<F32>& roll ) noexcept
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

    inline  vec3<F32> Camera::unProject( const vec3<F32>& winCoords, const Rect<I32>& viewport ) const noexcept
    {
        return unProject( winCoords.x, winCoords.y, viewport );
    }
}; //namespace Divide
#endif //_CAMERA_INL_