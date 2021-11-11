#include "stdafx.h"

#include "Headers/AnimationUtils.h"

namespace Divide::AnimUtils {

/// there is some type of alignment issue with my mat4 and the aimatrix4x4
/// class, so the copy must be done manually
void TransformMatrix(const aiMatrix4x4& in, mat4<F32>& out, const bool rowMajor) noexcept {
    if (rowMajor) {
        out.set({ in.a1, in.a2, in.a3, in.a4,
                  in.b1, in.b2, in.b3, in.b4,
                  in.c1, in.c2, in.c3, in.c4,
                  in.d1, in.d2, in.d3, in.d4 });
    } else {
        out.set({ in.a1, in.b1, in.c1, in.d1,
                  in.a2, in.b2, in.c2, in.d2,
                  in.a3, in.b3, in.c3, in.d3,
                  in.a4, in.b4, in.c4, in.d4 });
    }
}

void TransformMatrix(const mat4<F32>& in, aiMatrix4x4& out, const bool rowMajor) noexcept {
    if (rowMajor) {
        out.a1 = in._11; out.a2 = in._12; out.a3 = in._13; out.a4 = in._14;
        out.b1 = in._21; out.b2 = in._22; out.b3 = in._23; out.b4 = in._24;
        out.c1 = in._31; out.c2 = in._32; out.c3 = in._33; out.c4 = in._34;
        out.d1 = in._41; out.d2 = in._42; out.d3 = in._43; out.d4 = in._44;
    } else {
        out.a1 = in._11; out.b1 = in._12; out.c1 = in._13; out.d1 = in._14;
        out.a2 = in._21; out.b2 = in._22; out.c2 = in._23; out.d2 = in._24;
        out.a3 = in._31; out.b3 = in._32; out.c3 = in._33; out.d3 = in._34;
        out.a4 = in._41; out.b4 = in._42; out.c4 = in._43; out.d4 = in._44;
    }
}

}