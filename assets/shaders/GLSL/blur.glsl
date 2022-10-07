--Geometry.GaussBlur

#include "nodeDataInput.cmn"

layout(points, invocations = GS_MAX_INVOCATIONS) in;
layout(triangle_strip, max_vertices = 4) out;

layout(location = ATTRIB_FREE_START) out flat int _blurred;
layout(location = ATTRIB_FREE_START + 1) out vec3 _blurCoords[7];

void computeCoordsH(in float texCoordX, in float texCoordY, in int layer, in vec2 blurSizes[12]){
    vec2 blurSize = blurSizes[layer];

    _blurCoords[0] = vec3(texCoordX, texCoordY - 3.0 * blurSize.y, layer);
    _blurCoords[1] = vec3(texCoordX, texCoordY - 2.0 * blurSize.y, layer);
    _blurCoords[2] = vec3(texCoordX, texCoordY - 1.0 * blurSize.y, layer);
    _blurCoords[3] = vec3(texCoordX, texCoordY, layer);
    _blurCoords[4] = vec3(texCoordX, texCoordY + 1.0 * blurSize.y, layer);
    _blurCoords[5] = vec3(texCoordX, texCoordY + 2.0 * blurSize.y, layer);
    _blurCoords[6] = vec3(texCoordX, texCoordY + 3.0 * blurSize.y, layer);

    _blurred = 1;
}

void computeCoordsV(in float texCoordX, in float texCoordY, in int layer, in vec2 blurSizes[12] ){
    vec2 blurSize = blurSizes[layer];

    _blurCoords[0] = vec3(texCoordX - 3.0 * blurSize.x, texCoordY, layer);
    _blurCoords[1] = vec3(texCoordX - 2.0 * blurSize.x, texCoordY, layer);
    _blurCoords[2] = vec3(texCoordX - 1.0 * blurSize.x, texCoordY, layer);
    _blurCoords[3] = vec3(texCoordX, texCoordY, layer);
    _blurCoords[4] = vec3(texCoordX + 1.0 * blurSize.x, texCoordY, layer);
    _blurCoords[5] = vec3(texCoordX + 2.0 * blurSize.x, texCoordY, layer);
    _blurCoords[6] = vec3(texCoordX + 3.0 * blurSize.x, texCoordY, layer);

    _blurred = 1;
}

void passThrough(in float texCoordX, in float texCoordY, in int layer) {
    _blurCoords[0] = vec3(texCoordX, texCoordY, layer);
    _blurCoords[1] = vec3(1.0, 1.0, layer);
    _blurCoords[2] = vec3(1.0, 1.0, layer);
    _blurCoords[3] = vec3(1.0, 1.0, layer);
    _blurCoords[4] = vec3(1.0, 1.0, layer);
    _blurCoords[5] = vec3(1.0, 1.0, layer);
    _blurCoords[6] = vec3(1.0, 1.0, layer);

    _blurred = 0;
}

void BlurRoutine(in float texCoordX, in float texCoordY, in int layer) {
    const vec2 blurSizes[12] = vec2[12]( PushData0[2].xy,
                                         PushData0[2].zw,
                                         PushData0[3].xy,
                                         PushData0[3].zw,
                                         PushData1[0].xy,
                                         PushData1[0].zw,
                                         PushData1[1].xy,
                                         PushData1[1].zw,
                                         PushData1[2].xy,
                                         PushData1[2].zw,
                                         PushData1[3].xy,
                                         PushData1[3].zw );

    if (verticalBlur != 0u) {
        computeCoordsV(texCoordX, texCoordY, layer, blurSizes );
    } else {
        computeCoordsH(texCoordX, texCoordY, layer, blurSizes );
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

layout(location = ATTRIB_FREE_START + 1) in vec3 _blurCoords[7];

#if defined(LAYERED)
#define COORDS(X) _blurCoords[X]
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2DArray texScreen;
#else
#define COORDS(X) _blurCoords[X].xy
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2D texScreen;
#endif

layout(location = 0) out vec4 _outColour;

void main(void)
{
    if (_blurred == 1) {
        _outColour  = texture(texScreen, COORDS(0)) * 0.015625f; //(1.0  / 64.0);
        _outColour += texture(texScreen, COORDS(1)) * 0.09375f;  //(6.0  / 64.0);
        _outColour += texture(texScreen, COORDS(2)) * 0.234375f; //(15.0 / 64.0);
        _outColour += texture(texScreen, COORDS(3)) * 0.3125f;   //(20.0 / 64.0);
        _outColour += texture(texScreen, COORDS(4)) * 0.234375f; //(15.0 / 64.0);
        _outColour += texture(texScreen, COORDS(5)) * 0.09375f;  //(6.0  / 64.0);
        _outColour += texture(texScreen, COORDS(6)) * 0.015625f; //(1.0  / 64.0);
    } else {
        _outColour = texture(texScreen, COORDS(0));
    }
}

--Fragment.Generic

#include "nodeDataInput.cmn"

layout(location = 0) out vec4 _colourOut;

#if defined(LAYERED)
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2DArray texScreen;
#else
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2D texScreen;
#endif

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
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2D texScreen;
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 1) uniform sampler2D texVelocity;

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