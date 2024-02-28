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
    const vec2 ddx = dFdx(uv);
    const vec2 ddy = dFdy(uv);

    const vec2 iuv = floor(uv);
    const vec2 fuv = fract(uv);

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
    const vec2 uva = uv * ofa.zw + ofa.xy; const vec2 ddxa = ddx * ofa.zw; const vec2 ddya = ddy * ofa.zw;
    const vec2 uvb = uv * ofb.zw + ofb.xy; const vec2 ddxb = ddx * ofb.zw; const vec2 ddyb = ddy * ofb.zw;
    const vec2 uvc = uv * ofc.zw + ofc.xy; const vec2 ddxc = ddx * ofc.zw; const vec2 ddyc = ddy * ofc.zw;
    const vec2 uvd = uv * ofd.zw + ofd.xy; const vec2 ddxd = ddx * ofd.zw; const vec2 ddyd = ddy * ofd.zw;

    // fetch and blend
    const vec2 b = smoothstep(0.25f, 0.75f, fuv);

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

vec4 textureNoTile(sampler2D samp, sampler2DArray noiseSampler, in vec2 uv) {
    // sample variation pattern    
    float k = texture(noiseSampler, vec3(0.005f * uv, 0)).x; // cheap (cache friendly) lookup    

    float index = k * 8.f;
    float i = floor(index);
    float f = fract(index);

    vec2 offa = sin(vec2(3.f, 7.f) * (i + 0.f)); // can replace with any other hash
    vec2 offb = sin(vec2(3.f, 7.f) * (i + 1.f)); // can replace with any other hash

    const vec2 dx = dFdx(uv), dy = dFdy(uv);

    // sample the two closest virtual patterns    
    vec4 cola = textureGrad(samp, uv + offa, dx, dy);
    vec4 colb = textureGrad(samp, uv + offb, dx, dy);

    // interpolate between the two virtual patterns    
    return mix(cola, colb, smoothstep(0.2f, 0.8f, f - 0.1f * sum(cola - colb)));
}

vec4 textureNoTile(sampler2DArray samp, sampler2DArray noiseSampler, in vec3 uvIn) {
    const vec2 uv = uvIn.xy;

    // sample variation pattern
    float k = texture(noiseSampler, vec3(0.005f * uv, 0)).x; // cheap (cache friendly) lookup 

    float index = k * 8.f;
    float i = floor(index);
    float f = fract(index);

    const vec2 offa = sin(vec2(3.f, 7.f) * (i + 0.f)); // can replace with any other hash
    const vec2 offb = sin(vec2(3.f, 7.f) * (i + 1.f)); // can replace with any other hash

    const vec2 dx = dFdx(uv), dy = dFdy(uv);

    // sample the two closest virtual patterns    
    vec4 cola = textureGrad(samp, vec3(uv + offa, uvIn.z), dx, dy);
    vec4 colb = textureGrad(samp, vec3(uv + offb, uvIn.z), dx, dy);

    // interpolate between the two virtual patterns    
    return mix(cola, colb, smoothstep(0.2f, 0.8f, f - 0.1f * sum(cola - colb)));
}

#endif //_TEXTURING_FRAG_
