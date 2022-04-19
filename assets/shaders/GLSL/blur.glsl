--Geometry.GaussBlur

#include "nodeDataInput.cmn"

#if !defined(GS_MAX_INVOCATIONS)
#define GS_MAX_INVOCATIONS 1
#endif //!GS_MAX_INVOCATIONS

layout(points, invocations = GS_MAX_INVOCATIONS) in;
layout(triangle_strip, max_vertices = 4) out;

uniform vec2 blurSizes[GS_MAX_INVOCATIONS];

uniform int layerCount;
uniform int layerOffsetRead;
uniform int layerOffsetWrite;
uniform uint verticalBlur;

layout(location = ATTRIB_FREE_START) out flat int _blurred;
#if GS_MAX_INVOCATIONS > 1
layout(location = ATTRIB_FREE_START + 1) out vec3 _blurCoords[7];
#else
layout(location = ATTRIB_FREE_START + 1) out vec2 _blurCoords[7];
#endif

void computeCoordsH(in float texCoordX, in float texCoordY, in int layer){
    vec2 blurSize = blurSizes[layer];
#if GS_MAX_INVOCATIONS > 1
    _blurCoords[0] = vec3(texCoordX, texCoordY - 3.0 * blurSize.y, layer);
    _blurCoords[1] = vec3(texCoordX, texCoordY - 2.0 * blurSize.y, layer);
    _blurCoords[2] = vec3(texCoordX, texCoordY - 1.0 * blurSize.y, layer);
    _blurCoords[3] = vec3(texCoordX, texCoordY, layer);
    _blurCoords[4] = vec3(texCoordX, texCoordY + 1.0 * blurSize.y, layer);
    _blurCoords[5] = vec3(texCoordX, texCoordY + 2.0 * blurSize.y, layer);
    _blurCoords[6] = vec3(texCoordX, texCoordY + 3.0 * blurSize.y, layer);
#else
    _blurCoords[0] = vec2(texCoordX, texCoordY - 3.0 * blurSize.y);
    _blurCoords[1] = vec2(texCoordX, texCoordY - 2.0 * blurSize.y);
    _blurCoords[2] = vec2(texCoordX, texCoordY - 1.0 * blurSize.y);
    _blurCoords[3] = vec2(texCoordX, texCoordY);
    _blurCoords[4] = vec2(texCoordX, texCoordY + 1.0 * blurSize.y);
    _blurCoords[5] = vec2(texCoordX, texCoordY + 2.0 * blurSize.y);
    _blurCoords[6] = vec2(texCoordX, texCoordY + 3.0 * blurSize.y);
#endif
    _blurred = 1;
}

void computeCoordsV(in float texCoordX, in float texCoordY, in int layer){
    vec2 blurSize = blurSizes[layer];
#if GS_MAX_INVOCATIONS > 1
    _blurCoords[0] = vec3(texCoordX - 3.0 * blurSize.x, texCoordY, layer);
    _blurCoords[1] = vec3(texCoordX - 2.0 * blurSize.x, texCoordY, layer);
    _blurCoords[2] = vec3(texCoordX - 1.0 * blurSize.x, texCoordY, layer);
    _blurCoords[3] = vec3(texCoordX, texCoordY, layer);
    _blurCoords[4] = vec3(texCoordX + 1.0 * blurSize.x, texCoordY, layer);
    _blurCoords[5] = vec3(texCoordX + 2.0 * blurSize.x, texCoordY, layer);
    _blurCoords[6] = vec3(texCoordX + 3.0 * blurSize.x, texCoordY, layer);
#else
    _blurCoords[0] = vec2(texCoordX - 3.0 * blurSize.x, texCoordY);
    _blurCoords[1] = vec2(texCoordX - 2.0 * blurSize.x, texCoordY);
    _blurCoords[2] = vec2(texCoordX - 1.0 * blurSize.x, texCoordY);
    _blurCoords[3] = vec2(texCoordX, texCoordY);
    _blurCoords[4] = vec2(texCoordX + 1.0 * blurSize.x, texCoordY);
    _blurCoords[5] = vec2(texCoordX + 2.0 * blurSize.x, texCoordY);
    _blurCoords[6] = vec2(texCoordX + 3.0 * blurSize.x, texCoordY);
#endif

    _blurred = 1;
}

void passThrough(in float texCoordX, in float texCoordY, in int layer) {
#if GS_MAX_INVOCATIONS > 1
    _blurCoords[0] = vec3(texCoordX, texCoordY, layer);
    _blurCoords[1] = vec3(1.0, 1.0, layer);
    _blurCoords[2] = vec3(1.0, 1.0, layer);
    _blurCoords[3] = vec3(1.0, 1.0, layer);
    _blurCoords[4] = vec3(1.0, 1.0, layer);
    _blurCoords[5] = vec3(1.0, 1.0, layer);
    _blurCoords[6] = vec3(1.0, 1.0, layer);
#else
    _blurCoords[0] = vec2(texCoordX, texCoordY);
    _blurCoords[1] = vec2(1.0, 1.0);
    _blurCoords[2] = vec2(1.0, 1.0);
    _blurCoords[3] = vec2(1.0, 1.0);
    _blurCoords[4] = vec2(1.0, 1.0);
    _blurCoords[5] = vec2(1.0, 1.0);
    _blurCoords[6] = vec2(1.0, 1.0);
#endif
    _blurred = 0;
}

void BlurRoutine(in float texCoordX, in float texCoordY, in int layer) {
    if (verticalBlur != 0u) {
        computeCoordsV(texCoordX, texCoordY, layer);
    } else {
        computeCoordsH(texCoordX, texCoordY, layer);
    }
}

void main() {
    if (gl_InvocationID < layerCount) {
        gl_Layer = gl_InvocationID + layerOffsetWrite;
        gl_Position = vec4(1.0, 1.0, 0.0, 1.0);
        BlurRoutine(1.0, 1.0, gl_InvocationID + layerOffsetRead);
        EmitVertex();

        gl_Layer = gl_InvocationID + layerOffsetWrite;
        gl_Position = vec4(-1.0, 1.0, 0.0, 1.0);
        BlurRoutine(0.0, 1.0, gl_InvocationID + layerOffsetRead);
        EmitVertex();

        gl_Layer = gl_InvocationID + layerOffsetWrite;
        gl_Position = vec4(1.0, -1.0, 0.0, 1.0);
        BlurRoutine(1.0, 0.0, gl_InvocationID + layerOffsetRead);
        EmitVertex();

        gl_Layer = gl_InvocationID + layerOffsetWrite;
        gl_Position = vec4(-1.0, -1.0, 0.0, 1.0);
        BlurRoutine(0.0, 0.0, gl_InvocationID + layerOffsetRead);
        EmitVertex();

        EndPrimitive();
    } else {
        gl_Layer = gl_InvocationID + layerOffsetWrite;
        gl_Position = vec4(1.0, 1.0, 0.0, 1.0);
        passThrough(1.0, 1.0, gl_InvocationID + layerOffsetRead);
        EmitVertex();

        gl_Layer = gl_InvocationID + layerOffsetWrite;
        gl_Position = vec4(-1.0, 1.0, 0.0, 1.0);
        passThrough(0.0, 1.0, gl_InvocationID + layerOffsetRead);
        EmitVertex();

        gl_Layer = gl_InvocationID + layerOffsetWrite;
        gl_Position = vec4(1.0, -1.0, 0.0, 1.0);
        passThrough(1.0, 0.0, gl_InvocationID + layerOffsetRead);
        EmitVertex();

        gl_Layer = gl_InvocationID + layerOffsetWrite;
        gl_Position = vec4(-1.0, -1.0, 0.0, 1.0);
        passThrough(0.0, 0.0, gl_InvocationID + layerOffsetRead);
        EmitVertex();

        EndPrimitive();
    }
}

--Fragment.GaussBlur

#include "nodeDataInput.cmn"

layout(location = ATTRIB_FREE_START) in flat int _blurred;

#if defined(LAYERED)
layout(location = ATTRIB_FREE_START + 1) in vec3 _blurCoords[7];
DESCRIPTOR_SET_RESOURCE(0, TEXTURE_UNIT0) uniform sampler2DArray texScreen;
#else
layout(location = ATTRIB_FREE_START + 1) in vec2 _blurCoords[7];
DESCRIPTOR_SET_RESOURCE(0, TEXTURE_UNIT0) uniform sampler2D texScreen;
#endif

layout(location = 0) out vec4 _outColour;

void main(void)
{
    if (_blurred == 1) {
        _outColour  = texture(texScreen, _blurCoords[0]) * 0.015625f; //(1.0  / 64.0);
        _outColour += texture(texScreen, _blurCoords[1]) * 0.09375f;  //(6.0  / 64.0);
        _outColour += texture(texScreen, _blurCoords[2]) * 0.234375f; //(15.0 / 64.0);
        _outColour += texture(texScreen, _blurCoords[3]) * 0.3125f;   //(20.0 / 64.0);
        _outColour += texture(texScreen, _blurCoords[4]) * 0.234375f; //(15.0 / 64.0);
        _outColour += texture(texScreen, _blurCoords[5]) * 0.09375f;  //(6.0  / 64.0);
        _outColour += texture(texScreen, _blurCoords[6]) * 0.015625f; //(1.0  / 64.0);
    } else {
        _outColour = texture(texScreen, _blurCoords[0]);
    }
}

--Fragment.Generic

#include "nodeDataInput.cmn"

layout(location = 0) out vec4 _colourOut;

#if defined(LAYERED)
uniform int layer;
DESCRIPTOR_SET_RESOURCE(0, TEXTURE_UNIT0) uniform sampler2DArray texScreen;
#else
DESCRIPTOR_SET_RESOURCE(0, TEXTURE_UNIT0) uniform sampler2D texScreen;
#endif

uniform vec2 size;
uniform int kernelSize;
uniform uint verticalBlur;

#if defined(LAYERED)
vec3 blurHorizontal() {
    vec2 pass = 1.0 / size;
    vec3 colour = vec3(0.0);
    vec3 value;
    int sum = 0;
    int factor = 0;
    for (int i = -kernelSize; i <= kernelSize; i++) {
        value = texture(texScreen, vec3(VAR._texCoord + vec2(pass.x * i, 0.f), layer)).rgb;
        factor = kernelSize + 1 - abs(i);
        colour += value * factor;
        sum += factor;
    }
    return colour / sum;
}

vec3 blurVertical() {
    vec2 pass = 1.0 / size;
    vec3 colour = vec3(0.0);
    vec3 value;
    int sum = 0;
    int factor = 0;
    for (int i = -kernelSize; i <= kernelSize; i++) {
        value = texture(texScreen, vec3(VAR._texCoord + vec2(0.f, pass.y * i), layer)).rgb;
        factor = kernelSize + 1 - abs(i);
        colour += value * factor;
        sum += factor;
    }
    return colour / sum;
}

#else
vec3 blurHorizontal() {
    vec2 pass = 1.0 / size;
    vec3 colour = vec3(0.0);
    vec3 value;
    int sum = 0;
    int factor = 0;
    for (int i = -kernelSize; i <= kernelSize; i++) {
        value = texture(texScreen, VAR._texCoord + vec2(pass.x * i, 0.0)).rgb;
        factor = kernelSize + 1 - abs(i);
        colour += value * factor;
        sum += factor;
    }
    return colour / sum;
}

vec3 blurVertical() {
    vec2 pass = 1.0 / size;
    vec3 colour = vec3(0.0);
    vec3 value;
    int sum = 0;
    int factor = 0;
    for (int i = -kernelSize; i <= kernelSize; i++) {
        value = texture(texScreen, VAR._texCoord + vec2(0.0, pass.y * i)).rgb;
        factor = kernelSize + 1 - abs(i);
        colour += value * factor;
        sum += factor;
    }
    return colour / sum;
}
#endif

void main() {
    if (verticalBlur != 0u) {
        _colourOut = vec4(blurVertical(), 1.0);
    } else {
        _colourOut = vec4(blurHorizontal(), 1.0);
    }
}

--Fragment.ObjectMotionBlur

#include "utility.frag"

//ref: http://john-chapman-graphics.blogspot.com/2013/01/per-object-motion-blur.html
DESCRIPTOR_SET_RESOURCE(0, TEXTURE_UNIT0) uniform sampler2D texScreen;
DESCRIPTOR_SET_RESOURCE(0, TEXTURE_UNIT1) uniform sampler2D texVelocity;

uniform float dvd_velocityScale;
uniform int dvd_maxSamples;

layout(location = 0) out vec4 _outColour;

void main(void) {
    const vec2 texelSize = 1.f / vec2(textureSize(texScreen, 0));
    const vec2 screenTexCoords = gl_FragCoord.xy * texelSize;
    const vec2 velocity = texture(texVelocity, screenTexCoords).rg * dvd_velocityScale;

    const float speed = length(velocity / texelSize);
    const int nSamples = clamp(int(speed), 1, dvd_maxSamples);

    _outColour = texture(texScreen, screenTexCoords);

    for (int i = 1; i < nSamples; ++i) {
        const vec2 offset = velocity * (float(i) / float(nSamples - 1) - 0.5f);
        _outColour += texture(texScreen, screenTexCoords + offset);
    }

    _outColour /= float(nSamples);
}