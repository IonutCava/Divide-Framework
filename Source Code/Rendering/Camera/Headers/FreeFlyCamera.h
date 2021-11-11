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
#ifndef _FREE_FLY_CAMERA_H_
#define _FREE_FLY_CAMERA_H_

#include "Camera.h"

namespace Divide {

// A "god-mode" camera. It can move freely around the world
class FreeFlyCamera : public Camera {
   protected:
    friend class Camera;
    explicit FreeFlyCamera(const Str256& name, CameraType type = Type(), const vec3<F32>& eye = VECTOR3_ZERO);

    void update(F32 deltaTimeMS) noexcept override;

  public:
    void fromCamera(const Camera& camera, bool flag = false) override;

    void setGlobalRotation(F32 yaw, F32 pitch, F32 roll = 0.0f) noexcept override;
    /// Sets the camera's orientation to match the specified yaw, pitch and roll
    /// values;
    /// Creates a quaternion based on the specified Euler angles and calls
    /// "rotate" to change
    /// the orientation
    virtual void rotate(Angle::DEGREES<F32> yaw, Angle::DEGREES<F32> pitch, Angle::DEGREES<F32> roll) noexcept;
    /// Creates a quaternion based on the specified axis-angle and calls
    /// "rotate"
    /// to change
    /// the orientation
    void rotate(const vec3<F32>& axis, const Angle::DEGREES<F32> angle) {
        rotate(Quaternion<F32>(axis, angle * _speed.turn));
    }
    /// Yaw, Pitch and Roll call "rotate" with a appropriate quaternion for  each
    /// rotation.
    /// Because the camera is facing the -Z axis, a positive angle will create a
    /// positive Yaw
    /// behind the camera and a negative one in front of the camera
    /// (so we invert the angle - left will turn left when facing -Z)
    void rotateYaw(Angle::DEGREES<F32> angle);
    /// Change camera's roll.
    void rotateRoll(Angle::DEGREES<F32> angle);
    /// Change camera's pitch
    void rotatePitch(Angle::DEGREES<F32> angle);

    /// Moves the camera by the specified offsets in each direction
    virtual void move(F32 dx, F32 dy, F32 dz) noexcept;
    /// Rotates the camera (changes its orientation) by the specified quaternion
    /// (_orientation *= q)
    void rotate(const Quaternion<F32>& q);

    /// Moves the camera forward or backwards
    void moveForward(const F32 factor) noexcept {
        move(0.0f, 0.0f, factor);
    }
    /// Moves the camera left or right
    void moveStrafe(const F32 factor) noexcept {
        move(factor, 0.0f, 0.0f);
    }
    /// Moves the camera up or down
    void moveUp(const F32 factor) noexcept {
        move(0.0f, factor, 0.0f);
    }
    /// Mouse sensitivity: amount of pixels per radian (this should be moved out
    /// of the camera class)
    void setMouseSensitivity(const F32 sensitivity) noexcept {
        _mouseSensitivity = sensitivity;
    }

    void setMoveSpeedFactor(const F32 moveSpeedFactor) noexcept {
        _speedFactor.move = moveSpeedFactor;
    }

    void setTurnSpeedFactor(const F32 turnSpeedFactor) noexcept {
        _speedFactor.turn = turnSpeedFactor;
    }

    void setZoomSpeedFactor(const F32 zoomSpeedFactor) noexcept {
        _speedFactor.zoom = zoomSpeedFactor;
    }

    /// Exactly as in Ogre3D: locks the yaw movement to the specified axis
    void setFixedYawAxis(const bool useFixed, const vec3<F32>& fixedAxis = WORLD_Y_AXIS) noexcept {
        _yawFixed = useFixed;
        _fixedYawAxis = fixedAxis;
    }


    F32 getTurnSpeedFactor() const noexcept { return _speedFactor.turn; }
    F32 getMoveSpeedFactor() const noexcept { return _speedFactor.move; }
    F32 getZoomSpeedFactor() const noexcept { return _speedFactor.zoom; }


    void lockMovement(const bool state) noexcept { _movementLocked = state; }

    void lockRotation(const bool state) noexcept { _rotationLocked = state; }

    virtual bool moveRelative(const vec3<I32>& relMovement);
    virtual bool rotateRelative(const vec3<I32>& relRotation);
    virtual bool zoom(I32 zoomFactor) noexcept;

    static constexpr CameraType Type() noexcept { return CameraType::FREE_FLY; }

    virtual ~FreeFlyCamera() = default;

   protected:
     vec4<F32> _speedFactor = VECTOR4_UNIT;
     vec4<F32> _speed = VECTOR4_ZERO;
     vec3<F32> _targetPosition = VECTOR3_ZERO;
     vec3<F32> _currentVelocity = VECTOR3_ZERO;
     vec3<F32> _fixedYawAxis = WORLD_Y_AXIS;
     F32 _mouseSensitivity = 1.0f;
     bool _rotationLocked = false;
     bool _movementLocked = false;
     bool _yawFixed = false;
};

};  // namespace Divide

#endif