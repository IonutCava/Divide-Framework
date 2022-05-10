--Compute.Create

#define EPSILON 0.005f
// Taken from RTR vol 4 pg. 278
#define RGB_TO_LUM vec3(0.2125f, 0.7154f, 0.0721f)

// Uniforms:
uniform vec4 u_params;
// u_params.x = minimum log_2 luminance
// u_params.y = inverse of the log_2 luminance range

// Our two inputs, the read-only HDR color image, and the histogramBuffer
DESCRIPTOR_SET_RESOURCE_LAYOUT(PER_DRAW_SET, TEXTURE_UNIT0, rgba16f) uniform ACCESS_R image2D s_texColor;
DESCRIPTOR_SET_RESOURCE_LAYOUT(PER_DRAW_SET, BUFFER_LUMINANCE_HISTOGRAM, std430) coherent ACCESS_W buffer histogramBuffer
{
    uint histogram[];
};

// Shared histogram buffer used for storing intermediate sums for each work group
shared  uint histogramShared[GROUP_SIZE];

// For a given color and luminance range, return the histogram bin index
uint colorToBin(vec3 hdrColor, float minLogLum, float inverseLogLumRange) {
    // Convert our RGB value to Luminance, see note for RGB_TO_LUM macro above
    float lum = dot(hdrColor, RGB_TO_LUM);

    // Avoid taking the log of zero
    if (lum < EPSILON) {
        return 0;
    }

    // Calculate the log_2 luminance and express it as a value in [0.0, 1.0]
    // where 0.0 represents the minimum luminance, and 1.0 represents the max.
    float logLum = clamp((log2(lum) - minLogLum) * inverseLogLumRange, 0.0, 1.0);

    // Map [0, 1] to [1, 255]. The zeroth bin is handled by the epsilon check above.
    return uint(logLum * 254.0f + 1.0f);
}

layout(local_size_x = THREADS_X, local_size_y = THREADS_Y, local_size_z = 1) in;
void main() {
    // Initialize the bin for this thread to 0
    histogramShared[gl_LocalInvocationIndex] = 0;
    groupMemoryBarrier();

    uvec2 dim = uvec2(u_params.zw);
    // Ignore threads that map to areas beyond the bounds of our HDR image
    if (gl_GlobalInvocationID.x < dim.x && gl_GlobalInvocationID.y < dim.y) {
        vec3 hdrColor = imageLoad(s_texColor, ivec2(gl_GlobalInvocationID.xy)).rgb;
        uint binIndex = colorToBin(hdrColor, u_params.x, u_params.y);

        // We use an atomic add to ensure we don't write to the same bin in our
        // histogram from two different threads at the same time.
        atomicAdd(histogramShared[binIndex], 1);
    }
    // Wait for all threads in the work group to reach this point before adding our
    // local histogram to the global one
    groupMemoryBarrier();

    // Technically there's no chance that two threads write to the same bin here,
    // but different work groups might! So we still need the atomic add.
    atomicAdd(histogram[gl_LocalInvocationIndex], histogramShared[gl_LocalInvocationIndex]);
}

//ToneMap ref: https://bruop.github.io/exposure/

--Compute.Average

// Uniforms:
uniform vec4 u_params;

#define minLogLum u_params.x
#define logLumRange u_params.y
#define timeCoeff u_params.z
#define numPixels u_params.w

// We'll be writing our average to s_target
DESCRIPTOR_SET_RESOURCE_LAYOUT(PER_DRAW_SET, TEXTURE_UNIT0, r16f) uniform ACCESS_RW image2D s_target;

DESCRIPTOR_SET_RESOURCE_LAYOUT(PER_DRAW_SET, BUFFER_LUMINANCE_HISTOGRAM, std430) coherent ACCESS_RW buffer histogramBuffer
{
    uint histogram[];
};

shared uint histogramShared[GROUP_SIZE];

layout(local_size_x = GROUP_SIZE, local_size_y = 1, local_size_z = 1) in;
void main() {
    // Get the count from the histogram buffer
    uint countForThisBin = histogram[gl_LocalInvocationIndex];
    histogramShared[gl_LocalInvocationIndex] = countForThisBin * gl_LocalInvocationIndex;

    groupMemoryBarrier();

    // Reset the count stored in the buffer in anticipation of the next pass
    histogram[gl_LocalInvocationIndex] = 0;

    // This loop will perform a weighted count of the luminance range
    for (uint binIndex = (GROUP_SIZE >> 1); binIndex > 0; binIndex >>= 1) {
        if (uint(gl_LocalInvocationIndex) < binIndex) {
            histogramShared[gl_LocalInvocationIndex] += histogramShared[gl_LocalInvocationIndex + binIndex];
        }

        groupMemoryBarrier();
    }

    // We only need to calculate this once, so only a single thread is needed.
    if (gl_LocalInvocationIndex == 0) {
        // Here we take our weighted sum and divide it by the number of pixels
        // that had luminance greater than zero (since the index == 0, we can
        // use countForThisBin to find the number of black pixels)
        float weightedLogAverage = (histogramShared[0] / max(numPixels - float(countForThisBin), 1.0f)) - 1.0f;

        // Map from our histogram space to actual luminance
        float weightedAvgLum = exp2(((weightedLogAverage / 254.0f) * logLumRange) + minLogLum);


        // The new stored value will be interpolated using the last frames value
        // to prevent sudden shifts in the exposure.
        float lumLastFrame = imageLoad(s_target, ivec2(0, 0)).x;

        float adaptedLum = lumLastFrame + (weightedAvgLum - lumLastFrame) * timeCoeff;

        imageStore(s_target, ivec2(0, 0), vec4(adaptedLum, 0.0, 0.0, 0.0));
    }
}