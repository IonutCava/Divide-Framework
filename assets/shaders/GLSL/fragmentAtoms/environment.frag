#ifndef _ENVIRONMENT_FRAG_
#define _ENVIRONMENT_FRAG_

#include "sceneData.cmn"
#include "utility.frag"

uniform uint dvd_raySteps;

#define iSteps dvd_raySteps
#define jSteps int(dvd_raySteps * 0.5f)

//ref: https://github.com/wwwtyro/glsl-atmosphere
vec2 rsi(in vec3 r0, in vec3 rd, in float sr) {
    // ray-sphere intersection that assumes
    // the sphere is centered at the origin.
    // No intersection when result.x > result.y
    const float a = dot(rd, rd);
    const float b = 2.0f * dot(rd, r0);
    const float c = dot(r0, r0) - (sr * sr);
    const float d = (b * b) - 4.0f * a * c;
    return (d >= 0.0f) ? vec2((-b - sqrt(d)) / (2.0f * a),
        (-b + sqrt(d)) / (2.0f * a))
        : vec2(1e5, -1e5);
}

vec3 atmosphere(vec3 r, vec3 r0, vec3 pSun, float iSun, float rPlanet, float rAtmos, vec3 kRlh, float kMie, float shRlh, float shMie, float g) {
    // Normalize the sun and view directions.
    pSun = normalize(pSun);
    r = normalize(r);

    // Calculate the step size of the primary ray.
    vec2 p = rsi(r0, r, rAtmos);
    if (p.x > p.y) {
        return vec3(0.0f);
    }

    p.y = min(p.y, rsi(r0, r, rPlanet).x);
    const float iStepSize = (p.y - p.x) / float(iSteps);

    // Initialize the primary ray time.
    float iTime = 0.0;

    // Initialize accumulators for Rayleigh and Mie scattering.
    vec3 totalRlh = vec3(0, 0, 0);
    vec3 totalMie = vec3(0, 0, 0);

    // Initialize optical depth accumulators for the primary ray.
    float iOdRlh = 0.0f;
    float iOdMie = 0.0f;

    // Calculate the Rayleigh and Mie phases.
    float mu = dot(r, pSun);
    float mumu = mu * mu;
    float gg = g * g;
    float pRlh = 3.0f / (16.0f * M_PI) * (1.0f + mumu);
    float pMie = 3.0f / (8.0f * M_PI) * ((1.0f - gg) * (mumu + 1.0f)) / (pow(1.0f + gg - 2.0f * mu * g, 1.5f) * (2.0f + gg));

    // Sample the primary ray.
    for (int i = 0; i < iSteps; i++) {

        // Calculate the primary ray sample position.
        vec3 iPos = r0 + r * (iTime + iStepSize * 0.5);

        // Calculate the height of the sample.
        float iHeight = length(iPos) - rPlanet;

        // Calculate the optical depth of the Rayleigh and Mie scattering for this step.
        float odStepRlh = exp(-iHeight / shRlh) * iStepSize;
        float odStepMie = exp(-iHeight / shMie) * iStepSize;

        // Accumulate optical depth.
        iOdRlh += odStepRlh;
        iOdMie += odStepMie;

        // Calculate the step size of the secondary ray.
        float jStepSize = rsi(iPos, pSun, rAtmos).y / float(jSteps);

        // Initialize the secondary ray time.
        float jTime = 0.0;

        // Initialize optical depth accumulators for the secondary ray.
        float jOdRlh = 0.0;
        float jOdMie = 0.0;

        // Sample the secondary ray.
        for (int j = 0; j < jSteps; j++) {

            // Calculate the secondary ray sample position.
            vec3 jPos = iPos + pSun * (jTime + jStepSize * 0.5);

            // Calculate the height of the sample.
            float jHeight = length(jPos) - rPlanet;

            // Accumulate the optical depth.
            jOdRlh += exp(-jHeight / shRlh) * jStepSize;
            jOdMie += exp(-jHeight / shMie) * jStepSize;

            // Increment the secondary ray time.
            jTime += jStepSize;
        }

        // Calculate attenuation.
        vec3 attn = exp(-(kMie * (iOdMie + jOdMie) + kRlh * (iOdRlh + jOdRlh)));

        // Accumulate scattering.
        totalRlh += odStepRlh * attn;
        totalMie += odStepMie * attn;

        // Increment the primary ray time.
        iTime += iStepSize;

    }

    const float spot = smoothstep(0.0f, 1000.0f, pMie) * iSun;

    // Calculate and return the final color.
    return iSun * (spot * totalMie + pRlh * kRlh * totalRlh + pMie * kMie * totalMie);
}

//ref: https://github.com/jwagner/terrain/blob/master/shaders/atmosphere.glsl
vec3 atmosphereColor(vec3 rayDirection) {
    float a = max(0.f, dot(rayDirection, vec3(0.0, 1.0, 0.0)));
    vec3 skyColor = mix(dvd_horizonColour, dvd_zenithColour, a).rgb;
    const vec3 sunDirection = GetSunDirection();
    float sunTheta = max(dot(rayDirection, sunDirection), 0.0);
    return skyColor + dvd_sunColour.rgb * pow(sunTheta, 256.0) * 0.5;
}

vec3 aerialPerspective(vec3 albedo, float dist, vec3 rayOrigin, vec3 rayDirection) {
    float atmosphereDensity = 0.000025;
    vec3 atmosphere = atmosphereColor(rayDirection) + vec3(0.0, 0.02, 0.04);
    vec3 color = mix(albedo, atmosphere, clamp(1.0 - exp(-dist * atmosphereDensity), 0.0, 1.0));
    
    float fogDensity = dvd_fogDetails._colourAndDensity.a;
    float vFalloff = 20.0;
    vec3 fogColor = dvd_fogDetails._colourAndDensity.rgb;
    float fog = exp((-rayOrigin.y * vFalloff) * fogDensity) * (1.0 - exp(-dist * rayDirection.y * vFalloff * fogDensity)) / (rayDirection.y * vFalloff);
    return mix(color, fogColor, Saturate(fog));
}

#endif //_ENVIRONMENT_FRAG_
