

#include "Headers/OBB.h"

#include "Headers/BoundingBox.h"
#include "Headers/BoundingSphere.h"

namespace Divide {
    OBB::OBB(float3 pos, float3 hExtents, OBBAxis axis)  noexcept
        : _position(MOV(pos)),
          _halfExtents(MOV(hExtents)),
          _axis(MOV(axis))
    {
    }

    OBB::OBB(const BoundingBox &aabb)  noexcept
    {
        fromBoundingBox(aabb);
    }

    OBB::OBB(const BoundingSphere &bSphere)  noexcept
    {
        fromBoundingSphere(bSphere);
    }

    float3 OBB::cornerPoint(const U8 cornerIndex) const noexcept
    {
        switch (cornerIndex)
        {
            case 0: return _position - _halfExtents.x * _axis[0] - _halfExtents.y * _axis[1] - _halfExtents.z * _axis[2];
            case 1: return _position - _halfExtents.x * _axis[0] - _halfExtents.y * _axis[1] + _halfExtents.z * _axis[2];
            case 2: return _position - _halfExtents.x * _axis[0] + _halfExtents.y * _axis[1] - _halfExtents.z * _axis[2];
            case 3: return _position - _halfExtents.x * _axis[0] + _halfExtents.y * _axis[1] + _halfExtents.z * _axis[2];
            case 4: return _position + _halfExtents.x * _axis[0] - _halfExtents.y * _axis[1] - _halfExtents.z * _axis[2];
            case 5: return _position + _halfExtents.x * _axis[0] - _halfExtents.y * _axis[1] + _halfExtents.z * _axis[2];
            case 6: return _position + _halfExtents.x * _axis[0] + _halfExtents.y * _axis[1] - _halfExtents.z * _axis[2];
            case 7: return _position + _halfExtents.x * _axis[0] + _halfExtents.y * _axis[1] + _halfExtents.z * _axis[2];
            default: DIVIDE_UNEXPECTED_CALL(); break;
        }

        return VECTOR3_ZERO;
    }

    void OBB::fromBoundingBox(const BoundingBox& aabb) noexcept
    {
        _position.set(aabb.getCenter());
        _halfExtents.set(aabb.getHalfExtent());
        _axis = { WORLD_X_AXIS, WORLD_Y_AXIS, WORLD_Z_AXIS };
    }

    void OBB::fromBoundingBox(const BoundingBox& aabb, const mat4<F32>& worldMatrix)
    {
        DIVIDE_ASSERT(worldMatrix.isColOrthogonal()); // We cannot convert transform an AABB to OBB if it gets sheared in the process.

        fromBoundingBox(aabb);
        //transform(worldMatrix);
        _position = worldMatrix.transformCoord(_position);
        _axis[0]  = Normalized(worldMatrix * float4(_axis[0], 0.f));
        _axis[1]  = Normalized(worldMatrix * float4(_axis[1], 0.f));
        _axis[2]  = Normalized(worldMatrix * float4(_axis[2], 0.f));

        float3 position = VECTOR3_ZERO, scale = VECTOR3_UNIT;
        Util::DecomposeMatrix(worldMatrix, position, scale);
        _halfExtents *= scale;

        OrthoNormalize(_axis[0], _axis[1], _axis[2]);
    }

    void OBB::fromBoundingBox(const BoundingBox& aabb, const quatf& orientation)
    {
        fromBoundingBox(aabb, GetMat4(orientation));
    }

    void OBB::fromBoundingBox(const BoundingBox& aabb, const float3& position, const quatf& rotation, const float3& scale)
    {
        fromBoundingBox(aabb, mat4<F32>(position, scale, rotation));
    }

    void OBB::fromBoundingSphere(const BoundingSphere& sphere) noexcept
    {
        _position.set(sphere._sphere.center);
        _halfExtents.set(sphere._sphere.radius);
        _axis = { WORLD_X_AXIS, WORLD_Y_AXIS, WORLD_Z_AXIS };
    }

    void OBB::translate(const float3& offset)
    {
        _position += offset;
    }

    void OBB::scale(const float3& centerPoint, const F32 scaleFactor)
    {
        scale(centerPoint, float3(scaleFactor));
    }

    void OBB::scale(const float3& centerPoint, const float3& scaleFactor)
    {
        transform(mat4<F32>(centerPoint, scaleFactor, mat3<F32>()));
    }

    void OBB::transform(const mat3<F32>& transformIn)
    {
        transform(mat4<F32>(transformIn, false));
    }

    void OBB::transform(const mat4<F32>& transform)
    {
        DIVIDE_ASSERT(transform.isColOrthogonal());

        _position = transform * _position;
        for (U8 i = 0u; i < 3u; ++i)
        {
            _axis[i] = transform * (_halfExtents[i] * _axis[i]);
            _halfExtents[i] = _axis[i].length();
            _axis[i].normalize();
        }
    }

    void OBB::transform(const quatf& rotation)
    {
        transform(GetMat3(rotation));
    }

    BoundingBox OBB::toBoundingBox() const noexcept
    {
        return BoundingBox{ *this };
    }

    BoundingSphere OBB::toEnclosingSphere() const noexcept
    {
        return BoundingSphere{ position(), halfDiagonal().length() };
    }

    BoundingSphere OBB::toEnclosedSphere() const noexcept
    {
        return BoundingSphere{ position(), halfExtents().minComponent() };
    }

    float3 OBB::size() const noexcept
    {
        return _halfExtents * 2;
    }

    float3 OBB::diagonal() const noexcept
    {
        return halfDiagonal() * 2;
    }

    float3 OBB::halfDiagonal() const noexcept
    {
        return _axis[0] * _halfExtents[0] + 
               _axis[1] * _halfExtents[1] +
               _axis[2] * _halfExtents[2];
    }

    LineSegment OBB::edge(const U8 edgeIndex) const noexcept
    {
        switch (edgeIndex)
        {
            default: DIVIDE_UNEXPECTED_CALL(); break;

            case 0:  return {cornerPoint(0), cornerPoint(1) };
            case 1:  return {cornerPoint(0), cornerPoint(2) };
            case 2:  return {cornerPoint(0), cornerPoint(4) };
            case 3:  return {cornerPoint(1), cornerPoint(3) };
            case 4:  return {cornerPoint(1), cornerPoint(5) };
            case 5:  return {cornerPoint(2), cornerPoint(3) };
            case 6:  return {cornerPoint(2), cornerPoint(6) };
            case 7:  return {cornerPoint(3), cornerPoint(7) };
            case 8:  return {cornerPoint(4), cornerPoint(5) };
            case 9:  return {cornerPoint(4), cornerPoint(6) };
            case 10: return {cornerPoint(5), cornerPoint(7) };
            case 11: return {cornerPoint(6), cornerPoint(7) };
        }

        return {};
    }

    OBB::OOBBEdgeList OBB::edgeList() const noexcept
    {
        return
        {
            edge(0), edge(1),  edge(2),
            edge(3), edge(4),  edge(5),
            edge(6), edge(7),  edge(8),
            edge(9),edge(10), edge(11),
        };
    }

    float3 OBB::closestPoint(const float3& point) const noexcept
    {
        const float3 d = point - _position;
        float3 closestPoint = _position;
        for (U8 i = 0; i < 3; ++i)
        {
            closestPoint += CLAMPED(Dot(d, _axis[i]), -_halfExtents[i], _halfExtents[i]) * _axis[i];
        }

        return closestPoint;
    }

    F32 OBB::distance(const float3& point) const noexcept
    {
        return closestPoint(point).distance(point);
    }

    bool OBB::containsPoint(const float3& point) const noexcept
    {
        const float3 pt = point - position();
        return std::abs(Dot(pt, _axis[0])) <= _halfExtents[0] &&
               std::abs(Dot(pt, _axis[1])) <= _halfExtents[1] &&
               std::abs(Dot(pt, _axis[2])) <= _halfExtents[2];
    }

    bool OBB::containsBox(const OBB& OBB) const noexcept
    {
        for (U8 i = 0; i < 8; ++i)
        {
            if (!containsPoint(OBB.cornerPoint(i)))
            {
                return false;
            }
        }

        return true;
    }

    bool OBB::containsBox(const BoundingBox& AABB) const noexcept
    {
        for (U8 i = 0; i < 8; ++i)
        {
            if (!containsPoint(AABB.cornerPoint(i)))
            {
                return false;
            }
        }

        return true;
    }

    bool OBB::containsSphere(const BoundingSphere& bSphere) const noexcept
    {
        return distance(bSphere._sphere.center) - bSphere._sphere.radius < 0.f;
    }

    RayResult OBB::intersect(const IntersectionRay& ray, const F32 t0In, const F32 t1In) const noexcept
    {
        F32 tNear = -F32_MAX;
        F32 tFar  =  F32_MAX;

        const float3& rayDir = ray._direction.xyz;
        const float3& rayOrg = ray._origin.xyz;

        for (U8 i = 0; i < 3; ++i)
        {
            if (std::abs(Dot(rayDir, _axis[i])) < EPSILON_F32)
            {
                // Ray parallel to planes
                const F32 r = Dot(_axis[i], _position - rayOrg);
                if (-r - _halfExtents[i] > 0.f ||
                    -r + _halfExtents[i] > 0.f)
                {
                    return {};
                }
            }

            const F32 r = Dot(_axis[i], _position - rayOrg);
            const F32 s = Dot(_axis[i], rayDir);
            F32 t0 = (r + _halfExtents[i]) / s;
            F32 t1 = (r - _halfExtents[i]) / s;
            if (t0 > t1)
            {
                std::swap(t0, t1);
            }

            if (t0 > tNear)
            {
                tNear = t0;
            }

            if (t1 < tFar)
            {
                tFar = t1;
            }

            if (tFar < 0.f)
            {
                return {};
            }
        }

        RayResult ret;
        ret.inside = tNear < 0.f;
        ret.dist = tNear > 0.f ? tNear : tFar;
        // Ray started inside the box
        if (ret.dist < 0.0f)
        {
            ret.dist = 0.0f;
            ret.hit = true;
        }
        else
        {
            ret.hit = IS_IN_RANGE_INCLUSIVE(ret.dist, t0In, t1In);
        }

        return ret;
    }

}  // namespace Divide
