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
#ifndef _THIRD_PERSON_CAMERA_H_
#define _THIRD_PERSON_CAMERA_H_

#include "OrbitCamera.h"

namespace Divide {

/// A camera that always looks at a given target and orbits around it.
/// It's position / direction can be changed by user input
class ThirdPersonCamera final : public OrbitCamera {
  protected:
    friend class Camera;
    explicit ThirdPersonCamera(const Str256& name, const vec3<F32>& eye = VECTOR3_ZERO);

    static constexpr CameraType Type() noexcept { return CameraType::THIRD_PERSON; }

    void saveToXML(boost::property_tree::ptree& pt, string prefix = "") const override;
    void loadFromXML(const boost::property_tree::ptree& pt, string prefix = "") override;

  public:
    bool rotateRelative(const vec3<I32>& relRotation) override;
};

}  // namespace Divide

#endif