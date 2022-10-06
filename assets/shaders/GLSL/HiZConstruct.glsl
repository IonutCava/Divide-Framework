-- Compute

//ref: https://github.com/nvpro-samples/gl_occlusion_culling

DESCRIPTOR_SET_RESOURCE_LAYOUT(PER_DRAW, 0, r32f) uniform ACCESS_W image2D outImage;
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 1) uniform sampler2D inImage;

layout(local_size_x = LOCAL_SIZE, local_size_y = LOCAL_SIZE, local_size_z = 1) in;
void main() {
    uvec2 pos = gl_GlobalInvocationID.xy;
    const ivec2 uvOut = ivec2(pos);
    const ivec2 uvIn = ivec2(((pos + 0.5f) / imageSizeOut) * imageSizeIn);

    float depth = 0;
    if (wasEven)
    {
        ivec2 offsets[] = ivec2[](ivec2(0, 0),
                                  ivec2(0, 1),
                                  ivec2(1, 1),
                                  ivec2(1, 0));

        for (int i = 0; i < 4; i++)
        {
            const ivec2 coords = clamp(uvIn + offsets[i], ivec2(0), ivec2(imageSizeIn - vec2(1)));
            depth = max(depth, texelFetch(inImage, coords, 0).r);
        }
    }
    else
    {
        // need this to handle non-power of two very conservative
        ivec2 offsets[] = ivec2[](ivec2(-1, -1),
                                  ivec2( 0, -1),
                                  ivec2( 1, -1),
                                  ivec2(-1,  0),
                                  ivec2( 0,  0),
                                  ivec2( 1,  0),
                                  ivec2(-1,  1),
                                  ivec2( 0,  1),
                                  ivec2( 1,  1));

        for (int i = 0; i < 9; i++)
        {
            const ivec2 coords = clamp(uvIn + offsets[i], ivec2(0), ivec2(imageSizeIn - vec2(1)));
            depth = max(depth, texelFetch(inImage, coords, 0).r);
        }
    }


    imageStore(outImage, uvOut, vec4(depth));
}