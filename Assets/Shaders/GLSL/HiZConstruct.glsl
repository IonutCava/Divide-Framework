-- Compute

// Copyright (c) 2026 DIVIDE-Studio

//ref: https://github.com/nvpro-samples/gl_occlusion_culling

DESCRIPTOR_SET_RESOURCE_LAYOUT(PER_DRAW, 12, r32f) uniform ACCESS_W image2D outImage;
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 1) uniform sampler2D inImage;

// Push data: source and destination sizes; isBaseLevel enables conservative Hi-Z dilation.
layout(local_size_x = LOCAL_SIZE, local_size_y = LOCAL_SIZE, local_size_z = 1) in;
void main() {
    const ivec2 sizeIn = ivec2(imageSizeIn);
    const ivec2 sizeOut = ivec2(imageSizeOut);
    const ivec2 uvOut = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(uvOut, sizeOut)))
    {
        return;
    }

    // Every source texel overlapping this destination texel: [floor(p * in / out), ceil((p + 1) * in / out) - 1].
    // Exact halving reads the 2x2 block starting at 2p; odd source sizes widen the footprint to 3 texels on that axis.
    ivec2 first = (uvOut * sizeIn) / sizeOut;
    ivec2 last = ((uvOut + 1) * sizeIn + sizeOut - 1) / sizeOut - 1;
    if (isBaseLevel)
    {
        // Deliberately conservative: dilate the depth buffer copy by one texel to absorb projection rounding in HiZCull.
        first -= 1;
        last += 1;
    }
    first = max(first, ivec2(0));
    last = min(last, sizeIn - 1);

    float depth = 0.f;
    for (int y = first.y; y <= last.y; ++y)
    {
        for (int x = first.x; x <= last.x; ++x)
        {
            depth = max(depth, texelFetch(inImage, ivec2(x, y), 0).r);
        }
    }

    imageStore(outImage, uvOut, vec4(depth));
}
