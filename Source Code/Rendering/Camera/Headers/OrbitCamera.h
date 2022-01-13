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
#ifndef _ORBIT_CAMERA_H_
#define _ORBIT_CAMERA_H_

#include "FreeFlyCamera.h"

namespace Divide {

class TransformComponent;

/// A camera that always looks at a given target and orbits around it.
/// It's position / direction can't be changed by user input
class OrbitCamera : public FreeFlyCamera {
  protected:
    friend class Camera;
    explicit OrbitCamera(const Str256& name,
                         const CameraType& type = Type(),
                         const vec3<F32>& eye = VECTOR3_ZERO);
  public:
    void setTarget(TransformComponent* tComp) noexcept;

    void setTarget(TransformComponent* tComp, const vec3<F32>& offsetDirection) noexcept;

    void curRadius(const F32 radius) noexcept {
        _curRadius = radius;
        CLAMP<F32>(_curRadius, _minRadius, _maxRadius);
    }

    void update(F32 deltaTimeMS) noexcept override;
    bool zoom(F32 zoomFactor) noexcept override;

    void fromCamera(const Camera& camera) override;

    static constexpr CameraType Type() noexcept { return CameraType::ORBIT; }

    virtual ~OrbitCamera() = default;

    void saveToXML(boost::property_tree::ptree& pt, string prefix = "") const override;
    void loadFromXML(const boost::property_tree::ptree& pt, string prefix = "") override;

    PROPERTY_RW(F32, maxRadius, 10.f);
    PROPERTY_RW(F32, minRadius, 0.1f);
    PROPERTY_R(F32,  curRadius, 8.f);

   protected:
    F32 _currentRotationX = 0.0f;
    F32 _currentRotationY = 0.0f;
    bool _rotationDirty = true;
    vec3<F32> _offsetDir = VECTOR3_ZERO;
    vec3<F32> _cameraRotation = VECTOR3_ZERO;
    TransformComponent* _targetTransform = nullptr;
};

};  // namespace Divide

#endif