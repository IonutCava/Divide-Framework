--Vertex

#include "vbInputData.vert"
#include "lightingDefaults.vert"

void main() 
{
    const NodeTransformData data = fetchInputData();
    VAR._vertexW = Transform(dvd_Vertex, data._transform);
    VAR._vertexW.xyz += dvd_CameraPosition;

    VAR._vertexWV = dvd_ViewMatrix * VAR._vertexW;
    computeLightVectors(data);
    setClipPlanes();

    gl_Position = dvd_ProjectionMatrix * VAR._vertexWV;
    gl_Position.z = gl_Position.w - Z_TEST_SIGMA;
}

--Fragment

//ref: https://github.com/clayjohn/realtime_clouds
//ref: https://github.com/clayjohn/godot-volumetric-cloud-demo/tree/main

#if !defined(PRE_PASS)
layout(early_fragment_tests) in;
#endif //!PRE_PASS

DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform samplerCubeArray texSky;

#if !defined(NO_ATMOSPHERE)
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 2) uniform sampler3D perlworlnoise;
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 3) uniform sampler2DArray weathermap;
DESCRIPTOR_SET_RESOURCE(PER_DRAW, 4) uniform sampler3D worlnoise;
DESCRIPTOR_SET_RESOURCE( PER_DRAW, 9 ) uniform sampler2DArray curl;
#endif //NO_ATMOSPHERE

#define NO_POST_FX

#include "sceneData.cmn"
#include "utility.frag"
#include "output.frag"

#define UP_DIR WORLD_Y_AXIS

float TIME = MSToSeconds( dvd_GameTimeMS );
float sky_b_radius = dvd_PlanetRadius + dvd_CloudLayerMinMaxHeight.x;//bottom of cloud layer
float sky_t_radius = dvd_PlanetRadius + dvd_CloudLayerMinMaxHeight.y;//top of cloud layer
vec3 sunDirection = -GetSunFWDDirection();

// optical length at zenith for molecules
#define rayleigh_zenith_size 8.4e3
#define mie_zenith_size 1.25e3
#define _time_scale 1.f
#define _time_offset 0.f

// the maximal dimness of a dot ( 0.0->1.0   0.0 = all dots bright,  1.0 = maximum variation )
float SimplexPolkaDot3D(in vec3 P, in float density)
{
    // simplex math constants
    const vec3 SKEWFACTOR = vec3(1.f / 3.f);
    const vec3 UNSKEWFACTOR = vec3(1.f / 6.f);

    const vec3 SIMPLEX_CORNER_POS = vec3(0.5f);
    const float SIMPLEX_PYRAMID_HEIGHT = 0.70710678118654752440084436210485;	// sqrt( 0.5 )	height of simplex pyramid.

    // calculate the simplex vector and index math (sqrt( 0.5 ) height of simplex pyramid.)
    const vec3 Pn = P * SIMPLEX_PYRAMID_HEIGHT; // scale space so we can have an approx feature size of 1.0  ( optional )

    // Find the vectors to the corners of our simplex pyramid
    const vec3 Pi = floor(Pn + vec3(dot(Pn, SKEWFACTOR)));
    const vec3 x0 = Pn - Pi + vec3(dot(Pi, UNSKEWFACTOR));
    const vec3 g = step(x0.yzx, x0.xyz);
    const vec3 l = vec3(1.f) - g;
    const vec3 Pi_1 = min(g.xyz, l.zxy);
    const vec3 Pi_2 = max(g.xyz, l.zxy);
    const vec3 x1 = x0 - Pi_1 + UNSKEWFACTOR;
    const vec3 x2 = x0 - Pi_2 + SKEWFACTOR;
    const vec3 x3 = x0 - SIMPLEX_CORNER_POS;

    // pack them into a parallel-friendly arrangement
    vec4 v1234_x = vec4(x0.x, x1.x, x2.x, x3.x);
    vec4 v1234_y = vec4(x0.y, x1.y, x2.y, x3.y);
    vec4 v1234_z = vec4(x0.z, x1.z, x2.z, x3.z);

    const vec3 v1_mask = Pi_1;
    const vec3 v2_mask = Pi_2;

    const vec2 OFFSET = vec2(50.f, 161.f);
    const float DOMAIN = 69.f;
    const float SOMELARGEFLOAT = 6351.29681f;
    const float ZINC = 487.500388f;

    // truncate the domain
    const vec3 gridcell = Pi - floor(Pi * (1.f / DOMAIN)) * DOMAIN;
    const vec3 gridcell_inc1 = step(gridcell, vec3(DOMAIN - 1.5)) * (gridcell + vec3(1.f));

    // compute x*x*y*y for the 4 corners
    vec4 Pp = vec4(gridcell.xy, gridcell_inc1.xy) + vec4(OFFSET, OFFSET);
    Pp *= Pp;

    const vec4 V1xy_V2xy = mix(vec4(Pp.xy, Pp.xy), vec4(Pp.zw, Pp.zw), vec4(v1_mask.xy, v2_mask.xy)); // apply mask for v1 and v2
    Pp = vec4(Pp.x, V1xy_V2xy.x, V1xy_V2xy.z, Pp.z) * vec4(Pp.y, V1xy_V2xy.y, V1xy_V2xy.w, Pp.w);

    vec2 V1z_V2z = vec2(gridcell_inc1.z);
    if (v1_mask.z < 0.5f) {
        V1z_V2z.x = gridcell.z;
    }
    if (v2_mask.z < 0.5f) {
        V1z_V2z.y = gridcell.z;
    }

    const vec4 temp = vec4(SOMELARGEFLOAT) + vec4(gridcell.z, V1z_V2z.x, V1z_V2z.y, gridcell_inc1.z) * ZINC;
    const vec4 mod_vals = vec4(1.f) / (temp);
    // compute the final hash
    const vec4 hash = fract(Pp * mod_vals);

    // apply user controls

    // scale to a 0.0->1.0 range.  2.0 / sqrt( 0.75 )
#   define INV_SIMPLEX_TRI_HALF_EDGELEN 2.3094010767585030580365951220078f
    const float radius = INV_SIMPLEX_TRI_HALF_EDGELEN;///(1.15-density);

    v1234_x *= radius;
    v1234_y *= radius;
    v1234_z *= radius;
    // return a smooth falloff from the closest point.  ( we use a f(x)=(1.0-x*x)^3 falloff )
    const vec4 point_distance = max(vec4(0.f), vec4(1.f) - (v1234_x * v1234_x + v1234_y * v1234_y + v1234_z * v1234_z));

    const vec4 b = pow(min(vec4(1.f), max(vec4(0.f), (vec4(density) - hash) * (1.f / density))), vec4(1.f / density));
    return dot(b, point_distance * point_distance * point_distance);
}

// From: https://www.shadertoy.com/view/4sfGzS credit to iq
float hash( in vec3 p )
{
    p = fract( p * 0.3183099 + 0.1 );
    p *= 17.0;
    return fract( p.x * p.y * p.z * (p.x + p.y + p.z) );
}

// Phase function
float henyey_greenstein( in float cos_theta, in float g )
{
    const float k = 0.0795774715459;
    const float g2 = Squared(g);
    return k * (1.f - g2) / (pow( 1.f + g2 - 2.f * g * cos_theta, 1.5f ));
}

// Simple Analytic sky. In a real project you should use a texture
vec3 atmosphere( in vec3 eye_dir )
{
    const vec3 ground_color = dvd_GroundColour;

    float zenith_angle = clamp( dot( UP_DIR, sunDirection ), -1.f, 1.f );

    float sun_energy = max( 0.f, 1.f - exp( -((M_PI * 0.5f) - acos( zenith_angle )) ) ) * SUN_ENERGY;
    float sun_fade = 1.f - Saturate( 1.f - exp( sunDirection.y ) );

    // Rayleigh coefficients.
    float rayleigh_coefficient = dvd_Rayleigh - (1.f * (1.f - sun_fade));
    vec3 rayleigh_beta = rayleigh_coefficient * dvd_RayleighColour * 0.0001f;
    // mie coefficients from Preetham
    vec3 mie_beta = dvd_Turbidity * dvd_Mie * dvd_MieColour * 0.000434f;

    // optical length
    float zenith = acos( max( 0.0, dot( UP_DIR, eye_dir ) ) );
    float optical_mass = 1.0 / (cos( zenith ) + 0.15 * pow( 93.885 - degrees( zenith ), -1.253 ));
    float rayleigh_scatter = rayleigh_zenith_size * optical_mass;
    float mie_scatter = mie_zenith_size * optical_mass;

    // light extinction based on thickness of atmosphere
    vec3 extinction = exp( -(rayleigh_beta * rayleigh_scatter + mie_beta * mie_scatter) );

    // in scattering
    float cos_theta = dot( eye_dir, sunDirection );

    float rayleigh_phase = (3.0 / (16.0 * M_PI)) * (1.0 + pow( cos_theta * 0.5 + 0.5, 2.0 ));
    vec3 betaRTheta = rayleigh_beta * rayleigh_phase;

    float mie_phase = henyey_greenstein( cos_theta, dvd_MieEccentricity );
    vec3 betaMTheta = mie_beta * mie_phase;

    vec3 Lin = pow( sun_energy * ((betaRTheta + betaMTheta) / (rayleigh_beta + mie_beta)) * (1.0 - extinction), vec3( 1.5 ) );
    
    // Hack from https://github.com/mrdoob/three.js/blob/master/examples/jsm/objects/Sky.js
    Lin *= mix( vec3( 1.0 ), pow( sun_energy * ((betaRTheta + betaMTheta) / (rayleigh_beta + mie_beta)) * extinction, vec3( 0.5 ) ), clamp( pow( 1.0 - zenith_angle, 5.0 ), 0.0, 1.0 ) );

    // Hack in the ground color
    Lin *= mix( ground_color.rgb, vec3( 1.0 ), smoothstep( -0.1, 0.1, dot( UP_DIR, eye_dir ) ) );

    // Solar disk and out-scattering
    float sunAngularDiameterCos = cos( SUN_SIZE * dvd_SunDiskSize );
    float sunAngularDiameterCos2 = cos( SUN_SIZE * dvd_SunDiskSize * 0.5 );
    float sundisk = smoothstep( sunAngularDiameterCos, sunAngularDiameterCos2, cos_theta );
    vec3 L0 = (sun_energy * 1900.0 * extinction) * sundisk * dvd_sunColour.xyz;
    // Note: Add nightime here: L0 += night_sky * extinction

    vec3 color = (Lin + L0) * 0.04;
    color = pow( color, vec3( 1.f / (1.2 + (1.2 * sun_fade)) ) );
    color *= dvd_Exposure;
    return color;
}

#if !defined(NO_ATMOSPHERE)
float GetHeightFractionForPoint( float inPosition )
{
    return Saturate( (inPosition - sky_b_radius) / (sky_t_radius - sky_b_radius));
}

vec4 mixGradients( float cloudType )
{
    const vec4 STRATUS_GRADIENT = vec4( 0.02f, 0.05f, 0.09f, 0.11f );
    const vec4 STRATOCUMULUS_GRADIENT = vec4( 0.02f, 0.2f, 0.48f, 0.625f );
    const vec4 CUMULUS_GRADIENT = vec4( 0.01f, 0.0625f, 0.78f, 1.0f );
    float stratus = 1.0f - clamp( cloudType * 2.0f, 0.0, 1.0 );
    float stratocumulus = 1.0f - abs( cloudType - 0.5f ) * 2.0f;
    float cumulus = clamp( cloudType - 0.5f, 0.0, 1.0 ) * 2.0f;
    return STRATUS_GRADIENT * stratus + STRATOCUMULUS_GRADIENT * stratocumulus + CUMULUS_GRADIENT * cumulus;
}

float densityHeightGradient( float heightFrac, float cloudType )
{
    vec4 cloudGradient = mixGradients( cloudType );
    return smoothstep( cloudGradient.x, cloudGradient.y, heightFrac ) - smoothstep( cloudGradient.z, cloudGradient.w, heightFrac );
}

float intersectSphere( vec3 pos, vec3 dir, float r )
{
    float a = dot( dir, dir );
    float b = 2.0 * dot( dir, pos );
    float c = dot( pos, pos ) - (r * r);
    float d = sqrt( (b * b) - 4.0 * a * c );
    float p = -b - d;
    float p2 = -b + d;
    return max( p, p2 ) / (2.0 * a);
}

// Returns density at a given point
// Heavily based on method from Schneider
float density( vec3 pip, vec3 weather, float mip, in bool fast )
{
    const float time = TIME;
    vec3 p = pip;
    p.x += time * 10.0 * _time_scale + _time_offset;
    float height_fraction = GetHeightFractionForPoint( length( p ) );
    vec4 n = textureLod( perlworlnoise, p.xyz * 0.00008, mip - 2.0 );
    float fbm = n.g * 0.625 + n.b * 0.25 + n.a * 0.125;
    float g = densityHeightGradient( height_fraction, weather.r );
    float base_cloud = ReMap( n.r, -(1.0 - fbm), 1.0, 0.0, 1.0 );
    float weather_coverage = dvd_CloudCoverage * weather.b;
    base_cloud = ReMap( base_cloud * g, 1.0 - (weather_coverage), 1.0, 0.0, 1.0 );
    base_cloud *= weather_coverage;

    if (!fast )
    {
        const vec2 whisp = texture(curl, vec3( p.xy * 0.0003f, 0 )).xy;

        p.xy += whisp * 400.f * (1.f - height_fraction);
        vec3 hn = textureLod( worlnoise, p * 0.001, mip ).rgb;
        float hfbm = hn.r * 0.625 + hn.g * 0.25 + hn.b * 0.125;
        hfbm = mix( hfbm, 1.0 - hfbm, clamp( height_fraction * 4.0, 0.0, 1.0 ) );
        base_cloud = ReMap( base_cloud, hfbm * 0.4 * height_fraction, 1.0, 0.0, 1.0 );
    }

    return pow( clamp( base_cloud, 0.0, 1.0 ), (1.0 - height_fraction) * 0.8 + 0.5 );
}

const vec3 RANDOM_VECTORS[6] = 
{
    vec3( 0.38051305f,  0.92453449f, -0.02111345f ),
    vec3( -0.50625799f, -0.03590792f, -0.86163418f ),
    vec3( -0.32509218f, -0.94557439f,  0.01428793f ),
    vec3( 0.09026238f, -0.27376545f,  0.95755165f ),
    vec3( 0.28128598f,  0.42443639f, -0.86065785f ),
    vec3( -0.16852403f,  0.14748697f,  0.97460106f )
};

vec4 march(vec3 pos, vec3 end, vec3 dir, int depth )
{
    float T = 1.0;
    float alpha = 0.0;
    float ss = length( dir );
    dir = normalize( dir );
    vec3 p = pos + hash( pos * 10.0 ) * ss;
    const float t_dist = sky_t_radius - sky_b_radius;
    float lss = (t_dist / 36.0);
    vec3 ldir = sunDirection;
    vec3 L = vec3( 0.0 );
    int count = 0;
    float t = 1.0;
    float costheta = dot( ldir, dir );
    // Stack multiple phase functions to emulate some backscattering
    float phase = max( max( henyey_greenstein( costheta, 0.6 ), henyey_greenstein( costheta, (0.4 - 1.4 * ldir.y) ) ), henyey_greenstein( costheta, -0.2 ) );

    // Precalculate sun and ambient colors
    // This should really come from a uniform or texture for performance reasons
    vec3 atmosphere_ground = atmosphere( normalize( vec3( 1.0, -1.0, 0.0 ) ) );
    vec3 atmosphere_sun = atmosphere( sunDirection ) * ss * 0.1;
    vec3 atmosphere_ambient = atmosphere( normalize( vec3( 1.0, 1.0, 0.0 ) ) );

    const float weather_scale = 0.00006;
    float time = TIME * 0.003 * _time_scale + 0.005 * _time_offset;
    vec2 weather_pos = vec2( time * 0.9, time );

    for ( int i = 0; i < depth; i++ )
    {
        p += dir * ss;
        vec3 weather_sample = texture(weathermap, vec3(p.xz * weather_scale + 0.5f + weather_pos, 0)).xyz;
        float height_fraction = GetHeightFractionForPoint( length( p ) );

        t = density( p, weather_sample, 0.0, false );

        if ( t > 0.0 )
        { //calculate lighting, but only when we are in the cloud
            float dt = exp( -dvd_CloudDensity * t * ss );
            T *= dt;
            vec3 lp = p;
            float lt = 1.0;
            float cd = 0.0;

            float lheight_fraction = 0.0;
            for ( int j = 0; j < 6; j++ )
            {
                lp += (ldir + RANDOM_VECTORS[j] * float( j )) * lss;
                lheight_fraction = GetHeightFractionForPoint( length( lp ) );
                vec3 lweather = texture(weathermap, vec3(lp.xz * weather_scale + 0.5 + weather_pos, 0)).xyz;
                lt = density( lp, lweather, float( j ), true );
                cd += lt;
            }

            // Take a single distant sample
            lp = p + ldir * 18.0 * lss;
            lheight_fraction = GetHeightFractionForPoint( length( lp ) );
            vec3 lweather = texture(weathermap, vec3(lp.xz * weather_scale + 0.5, 0)).xyz;
            lt = pow( density( lp, lweather, 5.0, true ), (1.0 - lheight_fraction) * 0.8 + 0.5 );
            cd += lt;

            // captures the direct lighting from the sun
            float beers = exp( -dvd_CloudDensity * cd * lss );
            float beers2 = exp( -dvd_CloudDensity * cd * lss * 0.25 ) * 0.7;
            float beers_total = max( beers, beers2 );

            vec3 ambient = mix( atmosphere_ground, vec3( 1.0 ), smoothstep( 0.0, 1.0, height_fraction ) ) * dvd_CloudDensity * mix( atmosphere_ambient, vec3( 1.0 ), 0.4 ) * (sunDirection.y);
            alpha += (1.0 - dt) * (1.0 - alpha);
            L += (ambient + beers_total * atmosphere_sun * phase * alpha) * T * t;
        }
        if ( alpha >= 1.f )
        {
            break;
        }
    }
    return clamp( vec4( L, alpha ), 0.0, 1.0 );
}
#endif //!NO_ATMOSPHERE

vec3 U2Tone( const vec3 x )
{
    const float A = 0.15f;
    const float B = 0.50f;
    const float C = 0.10f;
    const float D = 0.20f;
    const float E = 0.02f;
    const float F = 0.30f;

    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 nightColour(in vec3 rayDirection, in float lerpValue)
{
    vec3 skyColour = dvd_NightSkyColour;
    if ( dvd_UseNightSkybox && lerpValue > 0.25f)
    {
        const vec3 sky = texture(texSky, vec4(rayDirection, 1.f)).rgb;
        skyColour = (skyColour + sky) - (skyColour * sky);
    }

    const float star = SimplexPolkaDot3D(rayDirection * 100.f, 0.15f) + SimplexPolkaDot3D(rayDirection * 150.f, 0.25f) * 0.7f;

    const vec3 ret = skyColour + 
                     max(0.f, (star - smoothstep(0.2f, 0.95f, 0.5f - 0.5f * rayDirection.y))) +
                     skyColour * (1.f - smoothstep(-0.1f, 0.45f, rayDirection.y));


    //Need to add a little bit of atmospheric effects, both for when the moon is high and for the end when color comes into the sky
    //For moon halo
    const vec3 moonpos = -sunDirection;
    const vec3 moonposNorm = normalize(moonpos);
    const float d = length(rayDirection - moonposNorm);

    const vec3 moonColour = vec3(smoothstep(1.0f - (dvd_MoonScale), 1.f, dot(rayDirection, moonposNorm))) +
                            0.4f * exp(-4.f * d) * dvd_MoonColour +
                            0.2f * exp(-2.f * d);

    return U2Tone(ret + moonColour);
}

vec3 dayColour(in vec3 rayDirection, in float lerpValue)
{
    const vec3 colour = atmosphere(rayDirection);

    if ( dvd_UseDaySkybox && lerpValue < 0.2f )
    {
        const vec3 sky = texture(texSky, vec4(rayDirection, 0.f)).rgb;

        return mix((colour + sky) - (colour * sky),
                   colour,
                   Luminance(colour.rgb));
    }

    return colour;
}

vec3 getSkyColour(in vec3 rayDirection, in float lerpValue)
{
    return mix(dayColour(rayDirection, lerpValue), nightColour(rayDirection, lerpValue), lerpValue);
}

vec3 computeClouds(in vec3 rayDirection, in float lerpValue)
{
    vec3 background = getSkyColour( rayDirection, lerpValue );
#if !defined(NO_ATMOSPHERE)
    if (rayDirection.y > 0.f)
    {
        const vec3 camPos = vec3(0.f, dvd_PlanetRadius, 0.f);
        const vec3 start = camPos + rayDirection * intersectSphere(camPos, rayDirection, sky_b_radius);
        const vec3 end = camPos + rayDirection * intersectSphere(camPos, rayDirection, sky_t_radius);

        // Take fewer steps towards horizon
#if 0
        const float steps = mix( dvd_RaySteps, dvd_RaySteps * 0.4f, 1.f - Saturate( dot( rayDirection, UP_DIR ) + 0.35f) );
#else
        const float steps = dvd_RaySteps * 0.75f;
#endif
        const float shelldist = (length( end - start ));
        vec3 raystep = rayDirection * shelldist / steps;

        vec4 volume = march(start, end, raystep, int(steps));

        // Draw cloud shape
        vec4 col = vec4( background * (1.f - volume.a) + volume.xyz, 1.f );

        // Blend distant clouds into the sky
        return mix( col.xyz, clamp( background, vec3( 0.0 ), vec3( 1.0 ) ), smoothstep( 0.6f, 1.f, 1.f - rayDirection.y ) );
    }

#endif //NO_ATMOSPHERE

    return background;
}

vec3 atmosphereColour(in vec3 rayDirection, in float lerpValue)
{
    if ( dvd_EnableClouds )
    {
        return computeClouds( rayDirection, lerpValue );
    }
    
    return getSkyColour( rayDirection, lerpValue );
}

#if defined(MAIN_DISPLAY_PASS)
vec3 getRawAlbedo(in vec3 rayDirection, in float lerpValue)
{
    return lerpValue <= 0.5f ? (dvd_UseDaySkybox ? texture(texSky, vec4(rayDirection, 0.f)).rgb : vec3(0.4f))
                             : (dvd_UseNightSkybox ? texture(texSky, vec4(rayDirection, 1.f)).rgb : vec3(0.2f));
}
#endif //MAIN_DISPLAY_PASS

void main()
{
    //TIME = MSToSeconds( dvd_GameTimeMS );
    //sky_b_radius = dvd_PlanetRadius + dvd_CloudLayerMinMaxHeight.x;//bottom of cloud layer
    //sky_t_radius = dvd_PlanetRadius + dvd_CloudLayerMinMaxHeight.y;//top of cloud layer
    //sunDirection = -GetSunFWDDirection();

    // Guess work based on what "look right"
    const float lerpValue = Saturate(2.95f * (-sunDirection.y + 0.15f));
    //const float lerpValue = dvd_sunAltitude * 0.5f + 0.5f; //Saturate(2.95f * (-sunDirection.y + 0.15f));
    const vec3 rayDirection = normalize(VAR._vertexW.xyz - dvd_CameraPosition);

#if defined(MAIN_DISPLAY_PASS)
    vec3 ret = vec3(0.f);
    switch (dvd_MaterialDebugFlag) {
        case DEBUG_ALBEDO:        ret = getRawAlbedo(rayDirection, lerpValue); break;
        case DEBUG_LIGHTING:      ret = getSkyColour(rayDirection, lerpValue); break;
        case DEBUG_SPECULAR:      
        case DEBUG_VELOCITY:
        case DEBUG_SSAO:
        case DEBUG_IBL:
        case DEBUG_KS:            ret = vec3(0.f); break;
        case DEBUG_UV:            ret = vec3(fract(rayDirection)); break;
        case DEBUG_EMISSIVE:      ret = getSkyColour(rayDirection, lerpValue); break;
        case DEBUG_ROUGHNESS:     ret = vec3(1.f); break;
        case DEBUG_METALNESS:     ret = vec3(0.f); break;
        case DEBUG_NORMALS:       ret = normalize(mat3(dvd_InverseViewMatrix) * VAR._normalWV); break;
        case DEBUG_TANGENTS:
        case DEBUG_BITANGENTS:    ret = vec3(0.0f); break;
        case DEBUG_SHADOW_MAPS:
        case DEBUG_CSM_SPLITS:    ret = vec3(1.0f); break;
        case DEBUG_LIGHT_HEATMAP:
        case DEBUG_DEPTH_CLUSTERS:
        case DEBUG_DEPTH_CLUSTER_AABBS:
        case DEBUG_REFRACTIONS:
        case DEBUG_REFLECTIONS:
        case DEBUG_MATERIAL_IDS:
        case DEBUG_SHADING_MODE:  ret = vec3(0.0f); break;
        default:                  ret = atmosphereColour(rayDirection, lerpValue); break;
    }

#else //MAIN_DISPLAY_PASS
    const vec3 ret = atmosphereColour(rayDirection, lerpValue);
#endif //MAIN_DISPLAY_PASS

    writeScreenColour(vec4(ret, 1.f), vec3( 0.f ), VAR._normalWV);
}

-- Fragment.PrePass

#include "prePass.frag"

void main() {

    writeGBuffer(VAR._normalWV, 1.f);
}