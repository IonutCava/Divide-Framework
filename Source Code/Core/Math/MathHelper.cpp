#include "stdafx.h"

#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>
#include <glm/gtc/type_ptr.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

namespace Divide::Util {

bool decomposeMatrix(const mat4<F32>& transform,
                     vec3<F32>& translationOut,
                     vec3<F32>& scaleOut,
                     vec3<Angle::RADIANS<F32>>& rotationOut,
                     bool& isUniformScaleOut)
{
    using T = F32;

    glm::mat4 LocalMatrix = glm::make_mat4(transform.mat);

    // Normalize the matrix.
    if (glm::epsilonEqual(LocalMatrix[3][3], static_cast<T>(0), glm::epsilon<T>()))
        return false;

    // First, isolate perspective.  This is the messiest.
    if (glm::epsilonNotEqual(LocalMatrix[0][3], static_cast<T>(0), glm::epsilon<T>()) ||
        glm::epsilonNotEqual(LocalMatrix[1][3], static_cast<T>(0), glm::epsilon<T>()) ||
        glm::epsilonNotEqual(LocalMatrix[2][3], static_cast<T>(0), glm::epsilon<T>()))
    {
        LocalMatrix[0][3] = LocalMatrix[1][3] = LocalMatrix[2][3] = static_cast<T>(0);
        LocalMatrix[3][3] = static_cast<T>(1);
    }

    // Next take care of translation (easy).
    glm::vec3 translation(LocalMatrix[3]);
    translationOut.set(translation.x, translation.y, translation.z);
    LocalMatrix[3] = glm::vec4(0, 0, 0, LocalMatrix[3].w);

    glm::vec3 Row[3];

    // Now get scale and shear.
    for (glm::length_t i = 0; i < 3; ++i)
        for (glm::length_t j = 0; j < 3; ++j)
            Row[i][j] = LocalMatrix[i][j];

    // Compute X scale factor and normalize first row.
    scaleOut.x = length(Row[0]);// v3Length(Row[0]);
    Row[0] = glm::detail::scale(Row[0], static_cast<T>(1));
    // Now, compute Y scale and normalize 2nd row.
    scaleOut.y = length(Row[1]);
    Row[1] = glm::detail::scale(Row[1], static_cast<T>(1));
    scaleOut.z = length(Row[2]);
    Row[2] = glm::detail::scale(Row[2], static_cast<T>(1));

      
    rotationOut.y = asin(-Row[0][2]);
    if (cos(rotationOut.y) != 0) {
        rotationOut.x = atan2(Row[1][2], Row[2][2]);
        rotationOut.z = atan2(Row[0][1], Row[0][0]);
    } else {
        rotationOut.x = atan2(-Row[2][0], Row[1][1]);
        rotationOut.z = 0;
    }

    isUniformScaleOut = scaleOut.isUniform();
    return true;
}

bool IntersectCircles(const Circle& cA, const Circle& cB, vec2<F32>* pointsOut) noexcept {
    assert(pointsOut != nullptr);

    const F32 x0 = cA.center[0];
    const F32 y0 = cA.center[1];
    const F32 r0 = cA.radius;

    const F32 x1 = cB.center[0];
    const F32 y1 = cB.center[1];
    const F32 r1 = cB.radius;
    
    /* dx and dy are the vertical and horizontal distances between
     * the circle centers.
     */
    const F32 dx = x1 - x0;
    const F32 dy = y1 - y0;

    /* Determine the straight-line distance between the centers. */
    //d = sqrt((dy*dy) + (dx*dx));
    const F32 d = hypot(dx, dy); // Suggested by Keith Briggs

    /* Check for solvability. */
    if (d > r0 + r1)
    {
        /* no solution. circles do not intersect. */
        return false;
    }
    if (d < fabs(r0 - r1))
    {
        /* no solution. one circle is contained in the other */
        return false;
    }

    /* 'point 2' is the point where the line through the circle
     * intersection points crosses the line between the circle
     * centers.
     */

     /* Determine the distance from point 0 to point 2. */
    const F32 a = (r0*r0 - r1*r1 + d*d) / (2.0f * d);

    /* Determine the coordinates of point 2. */
    const F32 x2 = x0 + dx * a / d;
    const F32 y2 = y0 + dy * a / d;

    /* Determine the distance from point 2 to either of the
     * intersection points.
     */
    const F32 h = sqrt(r0*r0 - a*a);

    /* Now determine the offsets of the intersection points from
     * point 2.
     */
    const F32 rx = -dy * (h / d);
    const F32 ry = dx * (h / d);

    /* Determine the absolute intersection points. */
    
    pointsOut[0].x = x2 + rx;
    pointsOut[1].x = x2 - rx;

    pointsOut[0].y = y2 + ry;
    pointsOut[1].y = y2 - ry;

    return true;
}

void ToByteColour(const FColour4& floatColour, UColour4& colourOut) noexcept {
    colourOut.set(FLOAT_TO_CHAR_UNORM(floatColour.r),
                  FLOAT_TO_CHAR_UNORM(floatColour.g),
                  FLOAT_TO_CHAR_UNORM(floatColour.b),
                  FLOAT_TO_CHAR_UNORM(floatColour.a));
}

void ToByteColour(const FColour3& floatColour, UColour3& colourOut) noexcept {
    colourOut.set(FLOAT_TO_CHAR_UNORM(floatColour.r),
                  FLOAT_TO_CHAR_UNORM(floatColour.g),
                  FLOAT_TO_CHAR_UNORM(floatColour.b));
}

void ToIntColour(const FColour4& floatColour, vec4<I32>& colourOut) noexcept {
    colourOut.set(FLOAT_TO_CHAR_SNORM(floatColour.r),
                  FLOAT_TO_CHAR_SNORM(floatColour.g),
                  FLOAT_TO_CHAR_SNORM(floatColour.b),
                  FLOAT_TO_CHAR_SNORM(floatColour.a));
}

void ToIntColour(const FColour3& floatColour, vec3<I32>& colourOut) noexcept {
    colourOut.set(FLOAT_TO_CHAR_SNORM(floatColour.r),
                  FLOAT_TO_CHAR_SNORM(floatColour.g),
                  FLOAT_TO_CHAR_SNORM(floatColour.b));
}

void ToUIntColour(const FColour4& floatColour, vec4<U32>& colourOut) noexcept {
    colourOut.set(FLOAT_TO_CHAR_UNORM(floatColour.r),
                  FLOAT_TO_CHAR_UNORM(floatColour.g),
                  FLOAT_TO_CHAR_UNORM(floatColour.b),
                  FLOAT_TO_CHAR_UNORM(floatColour.a));
}

void ToUIntColour(const FColour3& floatColour, vec3<U32>& colourOut) noexcept {
    colourOut.set(FLOAT_TO_CHAR_UNORM(floatColour.r),
                  FLOAT_TO_CHAR_UNORM(floatColour.g),
                  FLOAT_TO_CHAR_UNORM(floatColour.b));
}

void ToFloatColour(const UColour4& byteColour, FColour4& colourOut) noexcept {
    colourOut.set(UNORM_CHAR_TO_FLOAT(byteColour.r),
                  UNORM_CHAR_TO_FLOAT(byteColour.g),
                  UNORM_CHAR_TO_FLOAT(byteColour.b),
                  UNORM_CHAR_TO_FLOAT(byteColour.a));
}

void ToFloatColour(const UColour3& byteColour, FColour3& colourOut) noexcept {
    colourOut.set(UNORM_CHAR_TO_FLOAT(byteColour.r),
                  UNORM_CHAR_TO_FLOAT(byteColour.g),
                  UNORM_CHAR_TO_FLOAT(byteColour.b));
}

void ToFloatColour(const vec4<U32>& uintColour, FColour4& colourOut) noexcept {
    colourOut.set(uintColour.r / 255.0f,
                  uintColour.g / 255.0f,
                  uintColour.b / 255.0f,
                  uintColour.a / 255.0f);
}

void ToFloatColour(const vec3<U32>& uintColour, FColour3& colourOut) noexcept {
    colourOut.set(uintColour.r / 255.0f,
                  uintColour.g / 255.0f,
                  uintColour.b / 255.0f);
}

UColour4 ToByteColour(const FColour4& floatColour) noexcept {
    UColour4 tempColour;
    ToByteColour(floatColour, tempColour);
    return tempColour;
}

UColour3 ToByteColour(const FColour3& floatColour) noexcept {
    UColour3 tempColour;
    ToByteColour(floatColour, tempColour);
    return tempColour;
}

vec4<I32> ToIntColour(const FColour4& floatColour) noexcept {
    vec4<I32> tempColour;
    ToIntColour(floatColour, tempColour);
    return tempColour;
}

vec3<I32> ToIntColour(const FColour3& floatColour) noexcept {
    vec3<I32> tempColour;
    ToIntColour(floatColour, tempColour);
    return tempColour;
}

vec4<U32> ToUIntColour(const FColour4& floatColour) noexcept {
    vec4<U32> tempColour;
    ToUIntColour(floatColour, tempColour);
    return tempColour;
}

vec3<U32> ToUIntColour(const FColour3& floatColour) noexcept {
    vec3<U32> tempColour;
    ToUIntColour(floatColour, tempColour);
    return tempColour;
}

FColour4 ToFloatColour(const UColour4& byteColour) noexcept {
    FColour4 tempColour;
    ToFloatColour(byteColour, tempColour);
    return tempColour;
}

FColour3 ToFloatColour(const UColour3& byteColour) noexcept {
    FColour3 tempColour;
    ToFloatColour(byteColour, tempColour);
    return tempColour;
}

FColour4 ToFloatColour(const vec4<U32>& uintColour) noexcept {
    FColour4 tempColour;
    ToFloatColour(uintColour, tempColour);
    return tempColour;
}

FColour3 ToFloatColour(const vec3<U32>& uintColour) noexcept {
    FColour3 tempColour;
    ToFloatColour(uintColour, tempColour);
    return tempColour;
}

F32 PACK_VEC3(const vec3<F32_SNORM>& value) noexcept {
    return PACK_VEC3(value.x, value.y, value.z);
}

void UNPACK_VEC3(const F32 src, vec3<F32_SNORM>& res)noexcept {
    UNPACK_VEC3(src, res.x, res.y, res.z);
}

vec3<F32_SNORM> UNPACK_VEC3(const F32 src) noexcept {
    vec3<F32_SNORM> res;
    UNPACK_VEC3(src, res);
    return res;
}

[[nodiscard]] vec3<F32_NORM> UNPACK_11_11_10(const U32 src) {
    vec3<F32_NORM> res;
    UNPACK_11_11_10(src, res);
    return res;
}

U32 PACK_HALF2x16(const vec2<F32>& value) {
    return to_U32(glm::packHalf2x16(glm::mediump_vec2(value.x, value.y)));
}

void UNPACK_HALF2x16(const U32 src, vec2<F32>& value) {
    const glm::vec2 ret = glm::unpackHalf2x16(src);
    value.set(ret.x, ret.y);
}

vec2<F32> UNPACK_HALF2x16(const U32 src) {
    vec2<F32> ret;
    UNPACK_HALF2x16(src, ret);
    return ret;
}

U16 PACK_HALF1x16(const F32 value) {
    return to_U16(glm::packHalf1x16(value));
}

void UNPACK_HALF1x16(const U16 src, F32& value) {
    value = glm::unpackHalf1x16(src);
}

F32 UNPACK_HALF1x16(const U16 src) {
    F32 ret = 0.f;
    UNPACK_HALF1x16(src, ret);
    return ret;
}

F32 UINT_TO_FLOAT(const U32 src) {
    return glm::uintBitsToFloat(src);
}

U32 FLOAT_TO_UINT(const F32 src) {
    return glm::floatBitsToUint(src);
}

F32 UINT_TO_FLOAT(const I32 src) {
    return glm::intBitsToFloat(src);
}

I32 FLOAT_TO_INT(const F32 src) {
    return glm::floatBitsToInt(src);
}
U32 PACK_HALF2x16(const F32 x, const F32 y) {
    return to_U32(glm::packHalf2x16(glm::mediump_vec2(x, y)));
}

void UNPACK_HALF2x16(const U32 src, F32& x, F32& y) {

    const glm::vec2 ret = glm::unpackHalf2x16(src);
    x = ret.x;
    y = ret.y;
}

// Converts each component of the normalized floating  point value "value" into 8 bit integer values.
U32 PACK_UNORM4x8(const vec4<F32_NORM>& value) {
    return PACK_UNORM4x8(value.x, value.y, value.z, value.w);
}

U32 PACK_UNORM4x8(const vec4<U8>& value) {
    return PACK_UNORM4x8(value.x, value.y, value.z, value.w);
}

void UNPACK_UNORM4x8(const U32 src, vec4<F32_NORM>& value) {
    UNPACK_UNORM4x8(src, value.x, value.y, value.z, value.w);
}

U32 PACK_UNORM4x8(const U8 x, const U8 y, const U8 z, const U8 w) {
    return to_U32(glm::packUnorm4x8({ UNORM_CHAR_TO_FLOAT(x),
                                      UNORM_CHAR_TO_FLOAT(y),
                                      UNORM_CHAR_TO_FLOAT(z),
                                      UNORM_CHAR_TO_FLOAT(w)}));
}

U32 PACK_UNORM4x8(const F32_NORM x, const F32_NORM y, const F32_NORM z, const F32_NORM w) {
    assert(IS_IN_RANGE_INCLUSIVE(x, 0.f, 1.f));
    assert(IS_IN_RANGE_INCLUSIVE(y, 0.f, 1.f));
    assert(IS_IN_RANGE_INCLUSIVE(z, 0.f, 1.f));
    assert(IS_IN_RANGE_INCLUSIVE(w, 0.f, 1.f));

    return to_U32(glm::packUnorm4x8({ x, y, z, w }));
}

void UNPACK_UNORM4x8(const U32 src, U8& x, U8& y, U8& z, U8& w) {
    const glm::vec4 ret = glm::unpackUnorm4x8(src);
    x = FLOAT_TO_CHAR_UNORM(ret.x);
    y = FLOAT_TO_CHAR_UNORM(ret.y);
    z = FLOAT_TO_CHAR_UNORM(ret.z);
    w = FLOAT_TO_CHAR_UNORM(ret.w);
}

void UNPACK_UNORM4x8(const U32 src, F32_NORM& x, F32_NORM& y, F32_NORM& z, F32_NORM& w) {
    const glm::vec4 ret = glm::unpackUnorm4x8(src);
    x = ret.x; y = ret.y; z = ret.z; w = ret.w;
    assert(IS_IN_RANGE_INCLUSIVE(x, 0.f, 1.f));
    assert(IS_IN_RANGE_INCLUSIVE(y, 0.f, 1.f));
    assert(IS_IN_RANGE_INCLUSIVE(z, 0.f, 1.f));
    assert(IS_IN_RANGE_INCLUSIVE(w, 0.f, 1.f));
}

vec4<U8> UNPACK_UNORM4x8_U8(const U32 src) {
    vec4<U8> ret;
    UNPACK_UNORM4x8(src, ret.x, ret.y, ret.z, ret.w);
    return ret;
}

vec4<F32_NORM> UNPACK_UNORM4x8_F32(const U32 src) {
    vec4<F32_NORM> ret;
    UNPACK_UNORM4x8(src, ret.x, ret.y, ret.z, ret.w);
    return ret;
}

U32 PACK_11_11_10(const vec3<F32_NORM>& value) {
    return PACK_11_11_10(value.x, value.y, value.z);
}

void UNPACK_11_11_10(const U32 src, vec3<F32_NORM>& res) {
    UNPACK_11_11_10(src, res.x, res.y, res.z);
}

U32 PACK_11_11_10(const F32_NORM x, const F32_NORM y, const F32_NORM z) {
    assert(x >= 0.f && x <= 1.0f);
    assert(y >= 0.f && y <= 1.0f);
    assert(z >= 0.f && z <= 1.0f);
    
    return glm::packF2x11_1x10(glm::vec3(x, y, z));
}

void UNPACK_11_11_10(const U32 src, F32_NORM& x, F32_NORM& y, F32_NORM& z) {
    const glm::vec3 ret = glm::unpackF2x11_1x10(src);
    x = ret.x;
    y = ret.y;
    z = ret.z;
}

void Normalize(vec3<F32>& inputRotation, const bool degrees, const bool normYaw, const bool normPitch, const bool normRoll) noexcept {
    if (normYaw) {
        F32 yaw = degrees ? Angle::to_RADIANS(inputRotation.yaw)
                          : inputRotation.yaw;
        if (yaw < -M_PI_f) {
            yaw = fmod(yaw, M_PI_f * 2.0f);
            if (yaw < -M_PI_f) {
                yaw += M_PI_f * 2.0f;
            }
            inputRotation.yaw = Angle::to_DEGREES(yaw);
        } else if (yaw > M_PI_f) {
            yaw = fmod(yaw, M_PI_f * 2.0f);
            if (yaw > M_PI_f) {
                yaw -= M_PI_f * 2.0f;
            }
            inputRotation.yaw = degrees ? Angle::to_DEGREES(yaw) : yaw;
        }
    }
    if (normPitch) {
        F32 pitch = degrees ? Angle::to_RADIANS(inputRotation.pitch)
                            : inputRotation.pitch;
        if (pitch < -M_PI_f) {
            pitch = fmod(pitch, M_PI_f * 2.0f);
            if (pitch < -M_PI_f) {
                pitch += M_PI_f * 2.0f;
            }
            inputRotation.pitch = Angle::to_DEGREES(pitch);
        } else if (pitch > M_PI_f) {
            pitch = fmod(pitch, M_PI_f * 2.0f);
            if (pitch > M_PI_f) {
                pitch -= M_PI_f * 2.0f;
            }
            inputRotation.pitch =
                degrees ? Angle::to_DEGREES(pitch) : pitch;
        }
    }
    if (normRoll) {
        F32 roll = degrees ? Angle::to_RADIANS(inputRotation.roll)
                           : inputRotation.roll;
        if (roll < -M_PI_f) {
            roll = fmod(roll, M_PI_f * 2.0f);
            if (roll < -M_PI_f) {
                roll += M_PI_f * 2.0f;
            }
            inputRotation.roll = Angle::to_DEGREES(roll);
        } else if (roll > M_PI_f) {
            roll = fmod(roll, M_PI_f * 2.0f);
            if (roll > M_PI_f) {
                roll -= M_PI_f * 2.0f;
            }
            inputRotation.roll = degrees ? Angle::to_DEGREES(roll) : roll;
        }
    }
}
}  // namespace Divide::Util
