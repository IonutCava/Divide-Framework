

#include "Headers/Transform.h"

namespace Divide
{

Transform::Transform(const quatf& orientation, const float3& translation, const float3& scale) noexcept
{
    _scale.set(scale);
    _translation.set(translation);
    _orientation.set(orientation);
}

void Transform::setTransforms(const mat4<F32>& transform)
{
    const TransformValues previous = *this;
    if (!Util::DecomposeMatrix(transform, _translation, _scale, _orientation))
    {
        DebugBreak();
        setValues(previous);
    }
}

void Transform::identity() noexcept
{
    _scale.set(VECTOR3_UNIT);
    _translation.set(VECTOR3_ZERO);
    _orientation.identity();
}

}  // namespace Divide
