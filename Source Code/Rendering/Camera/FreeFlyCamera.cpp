#include "stdafx.h"

#include "Headers/FreeFlyCamera.h"

namespace Divide {

FreeFlyCamera::FreeFlyCamera(const Str256& name, const CameraType type, const vec3<F32>& eye)
    : Camera(name, type, eye)
{
    _speedFactor.set(35.0f);
    _speed.set(35.0f);
}

void FreeFlyCamera::fromCamera(const Camera& camera) {
    if (camera.type() == Type()) {
        const FreeFlyCamera& cam = static_cast<const FreeFlyCamera&>(camera);
        _speedFactor = cam._speedFactor;
        _speed = cam._speed;
        setFixedYawAxis(cam._yawFixed, cam._fixedYawAxis);
        _mouseSensitivity = cam._mouseSensitivity;
        lockRotation(cam._rotationLocked);
        lockMovement(cam._movementLocked);
    }

    Camera::fromCamera(camera);
}

void FreeFlyCamera::update(const F32 deltaTimeMS) noexcept {
    _speed = _speedFactor * Time::MillisecondsToSeconds(deltaTimeMS);
}

void FreeFlyCamera::setGlobalRotation(const F32 yaw, const F32 pitch, const F32 roll) noexcept {
    if (_rotationLocked) {
        return;
    }
    Camera::setGlobalRotation(yaw, pitch, roll);
}

void FreeFlyCamera::rotate(const Quaternion<F32>& q) {
    if (_rotationLocked) {
        return;
    }

    if (_type == CameraType::FIRST_PERSON) {
        const vec3<Angle::DEGREES<F32>> euler = Angle::to_DEGREES(q.getEuler());
        rotate(euler.yaw, euler.pitch, euler.roll);
    } else {
        _data._orientation = q * _data._orientation;
        _data._orientation.normalize();
    }

    _viewMatrixDirty = true;
}

void FreeFlyCamera::rotate(Angle::DEGREES<F32> yaw, Angle::DEGREES<F32> pitch, Angle::DEGREES<F32> roll) noexcept {
    if (_rotationLocked) {
        return;
    }

    const F32 turnSpeed = _speed.turn;
    yaw = -yaw * turnSpeed;
    pitch = -pitch * turnSpeed;
    roll = -roll * turnSpeed;

    Quaternion<F32> tempOrientation;
    if (_type == CameraType::FIRST_PERSON) {
        _accumPitchDegrees += pitch;

        if (_accumPitchDegrees > 90.0f) {
            pitch = 90.0f - (_accumPitchDegrees - pitch);
            _accumPitchDegrees = 90.0f;
        }

        if (_accumPitchDegrees < -90.0f) {
            pitch = -90.0f - (_accumPitchDegrees - pitch);
            _accumPitchDegrees = -90.0f;
        }

        // Rotate camera about the world y axis.
        // Note the order the quaternions are multiplied. That is important!
        if (!IS_ZERO(yaw)) {
            tempOrientation.fromAxisAngle(WORLD_Y_AXIS, yaw);
            _data._orientation = tempOrientation * _data._orientation;
        }

        // Rotate camera about its local x axis.
        // Note the order the quaternions are multiplied. That is important!
        if (!IS_ZERO(pitch)) {
            tempOrientation.fromAxisAngle(WORLD_X_AXIS, pitch);
            _data._orientation = _data._orientation * tempOrientation;
        }
    }
    else {
        tempOrientation.fromEuler(pitch, yaw, roll);
        _data._orientation *= tempOrientation;
    }

    _viewMatrixDirty = true;
}

void FreeFlyCamera::rotateYaw(const Angle::DEGREES<F32> angle) {
    rotate(Quaternion<F32>(_yawFixed ? _fixedYawAxis : _data._orientation * WORLD_Y_AXIS, -angle * _speed.turn));
}

void FreeFlyCamera::rotateRoll(const Angle::DEGREES<F32> angle) {
    rotate(Quaternion<F32>(_data._orientation * WORLD_Z_AXIS, -angle * _speed.turn));
}

void FreeFlyCamera::rotatePitch(const Angle::DEGREES<F32> angle) {
    rotate(Quaternion<F32>(_data._orientation * WORLD_X_AXIS, -angle * _speed.turn));
}

void FreeFlyCamera::move(F32 dx, F32 dy, F32 dz) noexcept {
    if (_movementLocked) {
        return;
    }

    const F32 moveSpeed = _speed.move;
    dx *= moveSpeed;
    dy *= moveSpeed;
    dz *= moveSpeed;

    _data._eye += getRightDir() * dx;
    _data._eye += WORLD_Y_AXIS * dy;

    if (_type == CameraType::FIRST_PERSON) {
        // Calculate the forward direction. Can't just use the camera's local
        // z axis as doing so will cause the camera to move more slowly as the
        // camera's view approaches 90 degrees straight up and down.
        const vec3<F32> forward = Normalized(Cross(WORLD_Y_AXIS, getRightDir()));
        _data._eye += forward * dz;
    }
    else {
        _data._eye += getForwardDir() * dz;
    }

    _viewMatrixDirty = true;
}

bool FreeFlyCamera::moveRelative(const vec3<F32>& relMovement) {
    if (relMovement.lengthSquared() > 0) {
        move(relMovement.y, relMovement.z, relMovement.x);
        return true;
    }

    return false;
}

bool FreeFlyCamera::rotateRelative(const vec3<F32>& relRotation) {
    if (relRotation.lengthSquared() > 0) {
        const F32 turnSpeed = _speed.turn;
        rotate(Quaternion<F32>(_yawFixed ? _fixedYawAxis : _data._orientation * WORLD_Y_AXIS, -relRotation.yaw * turnSpeed) *
               Quaternion<F32>(_data._orientation * WORLD_X_AXIS, -relRotation.pitch * turnSpeed) *
               Quaternion<F32>(_data._orientation * WORLD_Z_AXIS, -relRotation.roll * turnSpeed));
        return true;
    }
    return false;
}

bool FreeFlyCamera::zoom(const F32 zoomFactor) noexcept {
    return !IS_ZERO(zoomFactor);
}

void FreeFlyCamera::saveToXML(boost::property_tree::ptree& pt, const string prefix) const {
    Camera::saveToXML(pt, prefix);

    const string savePath = xmlSavePath(prefix);

    pt.put(savePath + ".speedFactor.<xmlattr>.turn", _speedFactor.turn);
    pt.put(savePath + ".speedFactor.<xmlattr>.move", _speedFactor.move);
    pt.put(savePath + ".speedFactor.<xmlattr>.zoom", _speedFactor.zoom); 
    pt.put(savePath + ".fixedYawAxis.<xmlattr>.x", _fixedYawAxis.x);
    pt.put(savePath + ".fixedYawAxis.<xmlattr>.y", _fixedYawAxis.y);
    pt.put(savePath + ".fixedYawAxis.<xmlattr>.z", _fixedYawAxis.z);
    pt.put(savePath + ".yawFixed", _yawFixed);
    pt.put(savePath + ".rotationLocked", _rotationLocked);
    pt.put(savePath + ".movementLocked", _movementLocked);
}

void FreeFlyCamera::loadFromXML(const boost::property_tree::ptree& pt, const string prefix) {
    Camera::loadFromXML(pt, prefix);

    const string savePath = xmlSavePath(prefix);

    _speedFactor.turn = pt.get(savePath + ".speedFactor.<xmlattr>.turn", _speedFactor.turn);
    _speedFactor.move = pt.get(savePath + ".speedFactor.<xmlattr>.move", _speedFactor.move);
    _speedFactor.zoom = pt.get(savePath + ".speedFactor.<xmlattr>.zoom", _speedFactor.zoom);
    _fixedYawAxis.set(
        pt.get(savePath + ".fixedYawAxis.<xmlattr>.x", _fixedYawAxis.x),
        pt.get(savePath + ".fixedYawAxis.<xmlattr>.y", _fixedYawAxis.y),
        pt.get(savePath + ".fixedYawAxis.<xmlattr>.z", _fixedYawAxis.z)
    );
    _yawFixed = pt.get(savePath + ".yawFixed", _yawFixed);
    _rotationLocked = pt.get(savePath + ".rotationLocked", _rotationLocked);
    _movementLocked = pt.get(savePath + ".movementLocked", _movementLocked);
  
}

};