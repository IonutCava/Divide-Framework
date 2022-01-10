#ifndef _TEXTURING_FRAG_
#define _TEXTURING_FRAG_

#include "nodeBufferedInput.cmn"

// The MIT License
// Copyright (C) 2015 Inigo Quilez
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), 
// to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, 
// and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions: 
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software. 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// One way to avoid texture tile repetition one using one small texture to cover a huge area.
// Based on Voronoise (https://www.shadertoy.com/view/Xd23Dh), a random offset is applied to
// the texture UVs per Voronoi cell. Distance to the cell is used to smooth the transitions
// between cells.

// More info here: http://www.iquilezles.org/www/articles/texturerepetition/texturerepetition.htm


vec4 hash4(in vec2 p) {
    return fract(sin(vec4(1.0f + dot(p, vec2(37.0f, 17.0f)),
        2.0f + dot(p, vec2(11.0f, 47.0f)),
        3.0f + dot(p, vec2(41.0f, 29.0f)),
        4.0f + dot(p, vec2(23.0f, 31.0f)))) * 103.0f);
}

float sum(vec3 v) { return v.x + v.y + v.z; }
float sum(vec4 v) { return v.x + v.y + v.z + v.w; }

vec4 textureNoTile(sampler2D samp, in vec2 uv) {
    vec2 ddx = dFdx(uv);
    vec2 ddy = dFdy(uv);

    vec2 iuv = floor(uv);
    vec2 fuv = fract(uv);

    // generate per-tile transform
    vec4 ofa = hash4(iuv + vec2(0.0, 0.0));
    vec4 ofb = hash4(iuv + vec2(1.0, 0.0));
    vec4 ofc = hash4(iuv + vec2(0.0, 1.0));
    vec4 ofd = hash4(iuv + vec2(1.0f, 1.0f));

    // transform per-tile uvs
    ofa.zw = sign(ofa.zw - 0.5f);
    ofb.zw = sign(ofb.zw - 0.5f);
    ofc.zw = sign(ofc.zw - 0.5f);
    ofd.zw = sign(ofd.zw - 0.5f);

    // uv's, and derivarives (for correct mipmapping)
    vec2 uva = uv * ofa.zw + ofa.xy; vec2 ddxa = ddx * ofa.zw; vec2 ddya = ddy * ofa.zw;
    vec2 uvb = uv * ofb.zw + ofb.xy; vec2 ddxb = ddx * ofb.zw; vec2 ddyb = ddy * ofb.zw;
    vec2 uvc = uv * ofc.zw + ofc.xy; vec2 ddxc = ddx * ofc.zw; vec2 ddyc = ddy * ofc.zw;
    vec2 uvd = uv * ofd.zw + ofd.xy; vec2 ddxd = ddx * ofd.zw; vec2 ddyd = ddy * ofd.zw;

    // fetch and blend
    vec2 b = smoothstep(0.25f, 0.75f, fuv);

    return mix(mix(textureGrad(samp, uva, ddxa, ddya),
                   textureGrad(samp, uvb, ddxb, ddyb),
                   b.x),
               mix(textureGrad(samp, uvc, ddxc, ddyc),
                   textureGrad(samp, uvd, ddxd, ddyd),
                   b.x),
               b.y);
}

vec4 textureNoTile(sampler2DArray samp, in vec3 uvIn) {
    const vec2 uv = uvIn.xy;

    vec2 ddx = dFdx(uv);
    vec2 ddy = dFdy(uv);

    vec2 iuv = floor(uv);
    vec2 fuv = fract(uv);

    // generate per-tile transform
    vec4 ofa = hash4(iuv + vec2(0.0f, 0.0f));
    vec4 ofb = hash4(iuv + vec2(1.0f, 0.0f));
    vec4 ofc = hash4(iuv + vec2(0.0f, 1.0f));
    vec4 ofd = hash4(iuv + vec2(1.0f, 1.0f));

    // transform per-tile uvs
    ofa.zw = sign(ofa.zw - 0.5f);
    ofb.zw = sign(ofb.zw - 0.5f);
    ofc.zw = sign(ofc.zw - 0.5f);
    ofd.zw = sign(ofd.zw - 0.5f);

    // uv's, and derivarives (for correct mipmapping)
    vec2 uva = uv * ofa.zw + ofa.xy; vec2 ddxa = ddx * ofa.zw; vec2 ddya = ddy * ofa.zw;
    vec2 uvb = uv * ofb.zw + ofb.xy; vec2 ddxb = ddx * ofb.zw; vec2 ddyb = ddy * ofb.zw;
    vec2 uvc = uv * ofc.zw + ofc.xy; vec2 ddxc = ddx * ofc.zw; vec2 ddyc = ddy * ofc.zw;
    vec2 uvd = uv * ofd.zw + ofd.xy; vec2 ddxd = ddx * ofd.zw; vec2 ddyd = ddy * ofd.zw;

    // fetch and blend
    vec2 b = smoothstep(0.25f, 0.75f, fuv);

    return mix(mix(textureGrad(samp, vec3(uva, uvIn.z), ddxa, ddya),
                   textureGrad(samp, vec3(uvb, uvIn.z), ddxb, ddyb),
                   b.x),
               mix(textureGrad(samp, vec3(uvc, uvIn.z), ddxc, ddyc),
                   textureGrad(samp, vec3(uvd, uvIn.z), ddxd, ddyd),
                   b.x),
               b.y);
}

vec4 textureNoTile(sampler2D samp, sampler2DArray noiseSampler, in int noiseSamplerIdx, in vec2 uv, in float v) {
    // sample variation pattern    
    float k = texture(noiseSampler, vec3(0.005 * uv, noiseSamplerIdx)).x; // cheap (cache friendly) lookup    

    vec2 duvdx = dFdx(uv);
    vec2 duvdy = dFdy(uv);

    float l = k * 8.0;
    float f = fract(l);
    float ia = floor(l);
    float ib = ia + 1.0;

    vec2 offa = sin(vec2(3.0, 7.0) * ia); // can replace with any other hash
    vec2 offb = sin(vec2(3.0, 7.0) * ib); // can replace with any other hash

    vec4 cola = textureGrad(samp, uv + v * offa, duvdx, duvdy);
    vec4 colb = textureGrad(samp, uv + v * offb, duvdx, duvdy);

    // interpolate between the two virtual patterns    
    return mix(cola, colb, smoothstep(0.2, 0.8, f - 0.1 * sum(cola - colb)));
}

vec4 textureNoTile(sampler2DArray samp, sampler2DArray noiseSampler, in int noiseSamplerIdx, in vec3 uvIn, in float v) {
    const vec2 uv = uvIn.xy;

    // sample variation pattern
    float k = texture(noiseSampler, vec3(0.005f * uv, noiseSamplerIdx)).x; // cheap (cache friendly) lookup 

    vec2 duvdx = dFdx(uv);
    vec2 duvdy = dFdy(uv);

    float l = k * 8.0;
    float f = fract(l);
    float ia = floor(l);
    float ib = ia + 1.0;

    vec2 offa = sin(vec2(3.0, 7.0) * ia); // can replace with any other hash
    vec2 offb = sin(vec2(3.0, 7.0) * ib); // can replace with any other hash

    // sample the two closest virtual patterns    
    vec4 cola = textureGrad(samp, vec3(uv + v * offa, uvIn.z), duvdx, duvdy);
    vec4 colb = textureGrad(samp, vec3(uv + v * offb, uvIn.z), duvdx, duvdy);

    // interpolate between the two virtual patterns    
    return mix(cola, colb, smoothstep(0.2, 0.8, f - 0.1 * sum(cola - colb)));
}

#ifndef MipScale
#define MipScale 0.25f
#endif //MipScale

//ref: https://bgolus.medium.com/anti-aliased-alpha-test-the-esoteric-alpha-to-coverage-8b177335ae4f
float calcMipLevel(in vec2 texture_coord) {
    const vec2 dx = dFdx(texture_coord);
    const vec2 dy = dFdy(texture_coord);
    const float delta_max_sqr = max(dot(dx, dx), dot(dy, dy));

    return max(0.f, 0.5f * log2(delta_max_sqr));
}

float getScaledAlpha(in float refAlpha, in vec2 uv, in ivec3 texSize) {
#if 1
    return refAlpha;
#else
    refAlpha *= 1.f + max(0.f, calcMipLevel(uv * texSize.xy)) * MipScale;
    // rescale alpha by partial derivative
    refAlpha = (refAlpha - Z_TEST_SIGMA) / max(fwidth(refAlpha), 0.0001f) + 0.5f;
    return refAlpha;
#endif
}

float getAlpha(in sampler2DArray tex, in vec3 uv) {
    const float refAlpha = texture(tex, uv).a;
    return getScaledAlpha(refAlpha, uv.xy, textureSize(tex, 0));
}
#endif //_TEXTURING_FRAG_
