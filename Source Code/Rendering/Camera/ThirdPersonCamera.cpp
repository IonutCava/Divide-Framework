#include "stdafx.h"

#include "Headers/ThirdPersonCamera.h"

namespace Divide {

ThirdPersonCamera::ThirdPersonCamera(const Str256& name, const vec3<F32>& eye)
    : OrbitCamera(name, CameraType::THIRD_PERSON, eye)
{
}

bool ThirdPersonCamera::rotateRelative(const vec3<F32>& relRotation) {
    constexpr F32 rotationLimitRollLower = M_PI_f * 0.30f - Angle::to_RADIANS(1);
    constexpr F32 rotationLimitRollUpper = M_PI_f * 0.175f - Angle::to_RADIANS(1);
    constexpr F32 rotationLimitPitch = M_PI_f - Angle::to_RADIANS(1);

    if (relRotation.lengthSquared() != 0) {
        vec3<F32> rotation(relRotation.pitch, relRotation.yaw, relRotation.roll);

        rotation.set(rotation / (getTurnSpeedFactor() * s_lastFrameTimeSec));

        if (!IS_ZERO(rotation.yaw)) {
            const F32 targetYaw = _cameraRotation.yaw - rotation.yaw;
            if (targetYaw > -rotationLimitRollLower &&
                targetYaw < rotationLimitRollUpper) {
                _cameraRotation.yaw -= rotation.yaw;
                _rotationDirty = true;
            }
        }

        if (!IS_ZERO(rotation.pitch)) {
            const F32 targetPitch = _cameraRotation.yaw - rotation.pitch;
            if (targetPitch > -rotationLimitPitch && targetPitch < rotationLimitPitch) {
                _cameraRotation.pitch -= rotation.pitch;
                _rotationDirty = true;
            }
        }

        if (_rotationDirty) {
            Util::Normalize(_cameraRotation, false, true, false, true);
        }
    }

    return OrbitCamera::rotateRelative(relRotation);
}

void ThirdPersonCamera::saveToXML(boost::property_tree::ptree& pt, const string prefix) const {
    OrbitCamera::saveToXML(pt, prefix);
}

void ThirdPersonCamera::loadFromXML(const boost::property_tree::ptree& pt, const string prefix) {
    OrbitCamera::loadFromXML(pt, prefix);
}
};