

#include "Headers/Transform.h"

namespace Divide {

Transform::Transform(const Quaternion<F32>& orientation, const float3& translation, const float3& scale) noexcept
{
    _scale.set(scale);
    _translation.set(translation);
    _orientation.set(orientation);
}

void Transform::setTransforms(const mat4<F32>& transform)
{
    vec3<Angle::RADIANS_F> tempEuler = VECTOR3_ZERO;
    if (Util::decomposeMatrix(transform, _translation, _scale, tempEuler))
    {
        _orientation.fromEuler(-tempEuler);
    }
}

void Transform::identity() noexcept {
    _scale.set(VECTOR3_UNIT);
    _translation.set(VECTOR3_ZERO);
    _orientation.identity();
}

}  // namespace Divide
