#ifndef COLORMAP_SH_HEADER_GUARD
#define COLORMAP_SH_HEADER_GUARD

// Copyright 2019 Google LLC.
// SPDX-License-Identifier: Apache-2.0

// Polynomial approximation in GLSL for the Turbo colormap
// Original LUT: https://gist.github.com/mikhailov-work/ee72ba4191942acecc03fe6da94fc73f

// Authors:
//   Colormap Design: Anton Mikhailov (mikhailov@google.com)
//   GLSL Approximation: Ruofei Du (ruofei@google.com)

// See also: https://ai.googleblog.com/2019/08/turbo-improved-rainbow-colormap-for.html

vec3 turboColormap(in float x) {
    // show clipping
    if (x < 0.f)
        return vec3(0.f);
    else if (x > 1.f)
        return vec3(1.f);

    const vec4 kRedVec4 = vec4(0.13572138f, 4.61539260f, -42.66032258f, 132.13108234f);
    const vec4 kGreenVec4 = vec4(0.09140261f, 2.19418839f, 4.84296658f, -14.18503333f);
    const vec4 kBlueVec4 = vec4(0.10667330f, 12.64194608f, -60.58204836f, 110.36276771f);
    const vec2 kRedVec2 = vec2(-152.94239396f, 59.28637943f);
    const vec2 kGreenVec2 = vec2(4.27729857f, 2.82956604f);
    const vec2 kBlueVec2 = vec2(-89.90310912f, 27.34824973f);

    x = Saturate(x);
    const vec4 v4 = vec4(1.0, x, x * x, x * x * x);
    const vec2 v2 = v4.zw * v4.z;
    return vec3(
        dot(v4, kRedVec4) + dot(v2, kRedVec2),
        dot(v4, kGreenVec4) + dot(v2, kGreenVec2),
        dot(v4, kBlueVec4) + dot(v2, kBlueVec2)
    );
}

#endif // COLORMAP_SH_HEADER_GUARD
