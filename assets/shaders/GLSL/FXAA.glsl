-- Geometry

layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

void main() {

    gl_Position = vec4(1.0, 1.0, 0.0, 1.0);
    _out._texCoord = vec2(1.0, 1.0);
    EmitVertex();

    gl_Position = vec4(-1.0, 1.0, 0.0, 1.0);
    _out._texCoord = vec2(0.0, 1.0);
    EmitVertex();

    gl_Position = vec4(1.0, -1.0, 0.0, 1.0);
    _out._texCoord = vec2(1.0, 0.0);
    EmitVertex();

    gl_Position = vec4(-1.0, -1.0, 0.0, 1.0);
    _out._texCoord = vec2(0.0, 0.0);
    EmitVertex();

    EndPrimitive();
}

-- Fragment

#include "utility.frag"

#define FXAA_PC 1
#define FXAA_GLSL_130 1
#define FXAA_QUALITY__PRESET 23
#define FXAA_GREEN_AS_LUMA 0
#include "Fxaa3_11.frag"

out vec4 _colourOut;
layout(binding = TEXTURE_UNIT0) uniform sampler2D texScreen;

uniform int dvd_qualityMultiplier;

void main(void)
{

    vec2 rcpFrame = dvd_InvScreenDimensions;
    vec2 pos = gl_FragCoord.xy * rcpFrame;

    vec4 ConsolePosPos = vec4(0.0, 0.0, 0.0, 0.0);
    vec4 ConsoleRcpFrameOpt = vec4(0.0, 0.0, 0.0, 0.0);
    vec4 ConsoleRcpFrameOpt2 = vec4(0.0, 0.0, 0.0, 0.0);
    vec4 Console360RcpFrameOpt2 = vec4(0.0, 0.0, 0.0, 0.0);

    int qualityFactor = min(dvd_qualityMultiplier, 4);
    // Only used on FXAA Quality.
    // Choose the amount of sub-pixel aliasing removal.
    // This can effect sharpness.
    //   1.00 - upper limit (softer)
    //   0.75 - default amount of filtering
    //   0.50 - lower limit (sharper, less sub-pixel aliasing removal)
    //   0.25 - almost off
    //   0.00 - completely off
    float QualitySubpix = qualityFactor * 0.25;

    // The minimum amount of local contrast required to apply algorithm.
    //   0.333 - too little (faster)
    //   0.250 - low quality
    //   0.166 - default
    //   0.125 - high quality 
    //   0.033 - very high quality (slower)
    const float thresholdArray[5] = {0.333, 0.250, 0.166, 0.125, 0.033};

    float QualityEdgeThreshold = thresholdArray[qualityFactor];

    vec4  Console360ConstDir = vec4(1.0, -1.0, 0.25, -0.25);
    _colourOut = FxaaPixelShader(pos, ConsolePosPos, texScreen, texScreen, texScreen, rcpFrame, ConsoleRcpFrameOpt, ConsoleRcpFrameOpt2, Console360RcpFrameOpt2, QualitySubpix, QualityEdgeThreshold, 0.0, 8.0, 0.125, 0.05, Console360ConstDir);
    _colourOut.rgb = _colourOut.rgb;
}