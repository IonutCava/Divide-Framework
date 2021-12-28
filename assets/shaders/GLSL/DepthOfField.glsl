/*
DoF with bokeh GLSL shader v2.4
by Martins Upitis (martinsh) (devlog-martinsh.blogspot.com)

----------------------
The shader is Blender Game Engine ready, but it should be quite simple to adapt for your engine.

This work is licensed under a Creative Commons Attribution 3.0 Unported License.
So you are free to share, modify and adapt it for your needs, and even use it for commercial use.
I would also love to hear about a project you are using it.

Have fun,
Martins
----------------------


changelog:

2.4:
- physically accurate DoF simulation calculated from "focalDepth" ,"focalLength", "f-stop" and "CoC" parameters.
- option for artist controlled DoF simulation calculated only from "focalDepth" and individual controls for near and far blur
- added "circe of confusion" (CoC) parameter in mm to accurately simulate DoF with different camera sensor or film sizes
- cleaned up the code
- some optimization

2.3:
- new and physically little more accurate DoF
- two extra input variables - focal length and aperture iris diameter
- added a debug visualization of focus point and focal range

2.1:
- added an option for pentagonal bokeh shape
- minor fixes

2.0:
- variable sample count to increase quality/performance
- option to blur depth buffer to reduce hard edges
- option to dither the samples with noise or pattern
- bokeh chromatic aberration/fringing
- bokeh bias to bring out bokeh edges
- image thresholding to bring out highlights when image is out of focus

*/

-- Fragment

layout(binding = TEXTURE_UNIT0) uniform sampler2D texScreen;
layout(binding = TEXTURE_DEPTH) uniform sampler2D texLinearDepth;

uniform vec2 size;
// autofocus point on screen (0.0,0.0 - left lower corner, 1.0,1.0 - upper right)
uniform vec2 focus;
uniform vec2 zPlanes;
//focal distance value in meters, but you may use autofocus option below
uniform float focalDepth;
//focal length in mm
uniform float focalLength;
uniform float fstop;
//near dof blur start
uniform float ndofstart;
//near dof blur falloff distance
uniform float ndofdist;
//far dof blur start
uniform float fdofstart;
//far dof blur falloff distance
uniform float fdofdist;
//vignetting outer border
uniform float vignout;
//vignetting inner border
uniform float vignin;
//show debug focus point and focal range (red = focal point, green = focal range)
uniform bool showFocus;
//manual dof calculation
uniform bool manualdof;
//use optical lens vignetting?
uniform bool vignetting;
//use autofocus in shader? disable if you use external focalDepth value
uniform bool autofocus;

out vec4 _colourOut;

//------------------------------------------
//user variables

int samples = FIRST_RING_SAMPLES; //samples on the first ring
int rings = RING_COUNT; //ring count

float CoC = 0.03f;//circle of confusion size in mm (35mm film = 0.03mm)

float vignfade = 22.0f; //f-stops till vignete fades

float maxblur = 1.0f; //clamp value of max blur (0.0 = no blur,1.0 default)

float threshold = 0.5f; //highlight threshold;
float gain = 2.0f; //highlight gain;

float bias = 0.5f; //bokeh edge bias
float fringe = 0.7f; //bokeh chromatic aberration/fringing

float namount = 0.0001f; //dither amount

#if defined(USE_DEPTH_BLUR)
float dbsize = 1.25f; //depthblursize
#endif

/*
next part is experimental
not looking good with small sample and ring count
looks okay starting from samples = 4, rings = 4
*/
#if defined(USE_PENTAGON)
float feather = 0.4; //pentagon shape feather
#endif

//------------------------------------------
float width = size.x; //texture width
float height = size.y; //texture height
vec2 texel = vec2(1.0f / width, 1.0f / height);

float znear = zPlanes.x;
float zfar = zPlanes.y;

#if defined(USE_PENTAGON)
float penta(in vec2 coords) //pentagonal shape
{
    float scale = float(rings) - 1.3;
    vec4  HS0 = vec4( 1.0,         0.0,         0.0,  1.0);
    vec4  HS1 = vec4( 0.309016994, 0.951056516, 0.0,  1.0);
    vec4  HS2 = vec4(-0.809016994, 0.587785252, 0.0,  1.0);
    vec4  HS3 = vec4(-0.809016994,-0.587785252, 0.0,  1.0);
    vec4  HS4 = vec4( 0.309016994,-0.951056516, 0.0,  1.0);
    vec4  HS5 = vec4( 0.0        ,0.0         , 1.0,  1.0);
    
    vec4  one = vec4( 1.0 );
    
    vec4 P = vec4((coords),vec2(scale, scale)); 
    
    vec4 dist = vec4(0.0);
    float inorout = -4.0;
    
    dist.x = dot( P, HS0 );
    dist.y = dot( P, HS1 );
    dist.z = dot( P, HS2 );
    dist.w = dot( P, HS3 );
    
    dist = smoothstep( -feather, feather, dist );
    
    inorout += dot( dist, one );
    
    dist.x = dot( P, HS4 );
    dist.y = HS5.w - abs( P.z );
    
    dist = smoothstep( -feather, feather, dist );
    inorout += dist.x;
    
    return clamp( inorout, 0.0, 1.0 );
}
#endif

#if defined(USE_DEPTH_BLUR)
float bdepth(in vec2 coords) //blurring depth
{
    float d = 0.0f;
    float kernel[9];
    vec2 offset[9];
    
    const vec2 wh = texel * dbsize;
    
    offset[0] = vec2(-wh.x,-wh.y);
    offset[1] = vec2( 0.0, -wh.y);
    offset[2] = vec2( wh.x -wh.y);
    
    offset[3] = vec2(-wh.x,  0.0);
    offset[4] = vec2( 0.0,   0.0);
    offset[5] = vec2( wh.x,  0.0);
    
    offset[6] = vec2(-wh.x, wh.y);
    offset[7] = vec2( 0.0,  wh.y);
    offset[8] = vec2( wh.x, wh.y);
    
    kernel[0] = 1.0/16.0;   kernel[1] = 2.0/16.0;   kernel[2] = 1.0/16.0;
    kernel[3] = 2.0/16.0;   kernel[4] = 4.0/16.0;   kernel[5] = 2.0/16.0;
    kernel[6] = 1.0/16.0;   kernel[7] = 2.0/16.0;   kernel[8] = 1.0/16.0;
    
    
    for( int i=0; i<9; i++ )
    {
        const float tmp = texture(texLinearDepth, coords + offset[i]).r;
        d += tmp * kernel[i];
    }
    
    return d;
}
#endif

vec3 colourProc(in vec2 coords, in float blur) //processing the sample
{
    vec3 col = vec3(0.0f);
    
    col.r = texture(texScreen,coords + vec2( 0.0f,   1.0f)*texel*fringe*blur).r;
    col.g = texture(texScreen,coords + vec2(-0.866f,-0.5f)*texel*fringe*blur).g;
    col.b = texture(texScreen,coords + vec2( 0.866f,-0.5f)*texel*fringe*blur).b;
    
    const vec3 lumcoeff = vec3(0.299f,0.587f,0.114f);
    const float lum = dot(col.rgb, lumcoeff);
    const float thresh = max((lum - threshold) * gain, 0.0f);

    return col + mix(vec3(0.0f), col, thresh * blur);
}

vec2 rand(in vec2 coord) //generating noise/pattern texture for dithering
{
    float noiseX = ((fract(1.0f - coord.s * (width * 0.5f)) * 0.25f) + (fract(coord.t * (height * 0.5f)) * 0.75f)) * 2.0f - 1.0f;
    float noiseY = ((fract(1.0f - coord.s * (width * 0.5f)) * 0.75f) + (fract(coord.t * (height * 0.5f)) * 0.25f)) * 2.0f - 1.0f;

#if defined(USE_NOISE)
        noiseX = saturate(fract(sin(dot(coord ,vec2(12.9898f,78.233f)))        * 43758.5453f)) * 2.0f - 1.0f;
        noiseY = saturate(fract(sin(dot(coord ,vec2(12.9898f,78.233f) * 2.0f)) * 43758.5453f)) * 2.0f - 1.0f;
#endif

    return vec2(noiseX,noiseY);
}

vec3 debugFocus(in vec3 col, in float blur, in float depth) {
    const float edge = 0.002f * depth; //distance based edge smoothing
    const float m = saturate(smoothstep(0.0f,        edge, blur));
    const float e = saturate(smoothstep(1.0f - edge, 1.0f, blur));

    col = mix(col, vec3(1.0f, 0.5f, 0.0f) , (1.0f - m) * 0.6f);
    col = mix(col, vec3(0.0f, 0.5f, 1.0f), ((1.0f - e) - (1.0f - m)) * 0.2f);

    return col;
}

float vignette(in vec2 coord) {
    float dist = distance(coord, vec2(0.5f, 0.5f));

    dist = smoothstep(vignout + (fstop / vignfade), vignin + (fstop / vignfade), dist);

    return saturate(dist);
}

void main() 
{
    //scene depth calculation
#if defined(USE_DEPTH_BLUR)
    const float depth = bdepth(VAR._texCoord);
#else
    const float depth = texture(texLinearDepth, VAR._texCoord).r;
#endif

    //focal plane calculation
    const float fDepth = autofocus ? texture(texLinearDepth, focus).r : focalDepth;

    //dof blur factor calculation
    float blur = 0.0;

    if (manualdof) {
        float a = depth - fDepth; //focal plane
        float b = (a - fdofstart) / fdofdist; //far DoF
        float c = (-a - ndofstart) / ndofdist; //near Dof
        blur = (a > 0.0f) ? b : c;
    } else {
        float f = focalLength; //focal length in mm
        float d = fDepth * 1000.0f; //focal plane in mm
        float o = depth * 1000.0f; //depth in mm

        float a = (o * f) / (o - f);
        float b = (d * f) / (d - f);
        float c = (d - f) / (d * fstop * CoC);

        blur = abs(a - b) * c;
    }
    blur = saturate(blur);

    // calculation of pattern for ditering
    const vec2 noise = rand(VAR._texCoord) * namount * blur;

    // getting blur x and y step factor
    const float w = texel.x * blur * maxblur + noise.x;
    const float h = texel.y * blur * maxblur + noise.y;

    // calculation of final color

    vec3 col = texture2D(texScreen, VAR._texCoord).rgb;

    if (blur >= 0.05f)  { // Hmm ... there used to be a comment here :-"
        float s = 1.0f;
        for (int i = 1; i <= rings; ++i)
        {
            int ringsamples = i * samples;

            for (int j = 0 ; j < ringsamples; ++j)
            {
                const float step = M_PI * 2.0f / float(ringsamples);
                const float pw = (cos(float(j) * step) * float(i));
                const float ph = (sin(float(j) * step) * float(i));
    #           if defined(USE_PENTAGON)
                const float p = penta(vec2(pw,ph));
    #           else
                const float p = 1.0f;
    #           endif

                col += colourProc(VAR._texCoord + vec2(pw*w, ph*h), blur) * mix(1.0f, (float(i)) / (float(rings)), bias) * p;
                s += 1.0f * mix(1.0f, (float(i)) / (float(rings)), bias) * p;
            }
        }
    
        col /= s;  //divide by sample count
    }

    if (showFocus) {
        col = debugFocus(col, blur, depth);
    }

    if (vignetting) {
        col *= vignette(VAR._texCoord);
    }

    _colourOut = vec4(col, 1.0);
}
