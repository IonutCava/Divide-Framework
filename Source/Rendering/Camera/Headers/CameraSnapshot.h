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
#ifndef DVD_CAMERA_SNAPSHOT_H_
#define DVD_CAMERA_SNAPSHOT_H_

namespace Divide {
    struct CameraSnapshot {
        mat4<F32> _viewMatrix;
        mat4<F32> _invViewMatrix;
        mat4<F32> _projectionMatrix;
        mat4<F32> _invProjectionMatrix;
        Quaternion<F32> _orientation;
        std::array<Plane<F32>, 6> _frustumPlanes;
        vec3<F32> _eye;
        vec2<F32> _zPlanes;
        Angle::DEGREES<F32> _fov{ 0.f};
        F32 _aspectRatio{ 0.f };
        bool _isOrthoCamera{false};
    };

    inline bool operator==(const CameraSnapshot& lhs, const CameraSnapshot& rhs) {
        return lhs._isOrthoCamera == rhs._isOrthoCamera &&
               COMPARE(lhs._fov, rhs._fov) &&
               COMPARE(lhs._aspectRatio,rhs._aspectRatio) &&
               lhs._zPlanes == rhs._zPlanes &&
               lhs._eye == rhs._eye &&
               lhs._viewMatrix == rhs._viewMatrix &&
               lhs._projectionMatrix == rhs._projectionMatrix &&
               lhs._orientation == rhs._orientation &&
               lhs._frustumPlanes == rhs._frustumPlanes;
    }

    inline bool operator!=(const CameraSnapshot& lhs, const CameraSnapshot& rhs) {
        return lhs._isOrthoCamera != rhs._isOrthoCamera ||
               !COMPARE(lhs._fov, rhs._fov) ||
               !COMPARE(lhs._aspectRatio, rhs._aspectRatio) ||
               lhs._zPlanes != rhs._zPlanes ||
               lhs._eye != rhs._eye ||
               lhs._viewMatrix != rhs._viewMatrix ||
               lhs._projectionMatrix != rhs._projectionMatrix ||
               lhs._orientation != rhs._orientation ||
               lhs._frustumPlanes != rhs._frustumPlanes;
    }
};

#endif //DVD_CAMERA_SNAPSHOT_H_
