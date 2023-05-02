-- Vertex

#define COMPUTE_TBN
#include "vbInputData.vert"
#include "lightingDefaults.vert"

layout(location = ATTRIB_FREE_START + 0) out flat int _underwater;
layout(location = ATTRIB_FREE_START + 1) out vec4 _vertexWVP;

void main(void)
{
    const NodeTransformData data = fetchInputData();
    _vertexWVP = computeData(data);
    setClipPlanes();
    computeLightVectors(data);

    _underwater = dvd_CameraPosition.y < VAR._vertexW.y ? 1 : 0;
    gl_Position = _vertexWVP;
}

-- Fragment

layout(location = ATTRIB_FREE_START + 0) in flat int _underwater;
layout(location = ATTRIB_FREE_START + 1) in vec4 _vertexWVP;

#define NO_OMR_TEX
#define NO_ENV_MAPPING
#define NO_SSAO

#if defined(PRE_PASS)
#include "prePass.frag"
#else //PRE_PASS
#include "BRDF.frag"
#include "output.frag"
#endif //PRE_PASS

#include "bumpMapping.frag"

const float Eta = 0.15f; //water

float Fresnel(in vec3 viewDir, in vec3 normal) {
    const float fresnel = Eta + (1.f - Eta) * pow(max(0.f, 1.f - dot(viewDir, normal)), 5.f);
    return Saturate(fresnel);
}

void main()
{

#if defined(MAIN_DISPLAY_PASS) && !defined(PRE_PASS)
    const vec3 normalWV = normalize(unpackNormal(sampleTexSceneNormals().rg));
#else //MAIN_DISPLAY_PASS && !PRE_PASS
    const float time2 = MSToSeconds(dvd_GameTimeMS) * 0.05f;
    const vec2 uvNormal0 = (VAR._texCoord * _noiseTile) + vec2( time2, time2);
    const vec2 uvNormal1 = (VAR._texCoord * _noiseTile) + vec2(-time2, time2);

    const vec3 normal0 = getNormalMap(texNormalMap, vec3(uvNormal0, 0));
    const vec3 normal1 = getNormalMap(texNormalMap, vec3(uvNormal1, 0));

    vec3 normal = normalPartialDerivativesBlend(normal0, normal1);
    const vec3 normalWV = normalize(getTBNWV() * normal);
#endif

    NodeMaterialData data = dvd_Materials[MATERIAL_IDX];

#if !defined(PRE_PASS)
    const vec3 normalW = normalize(mat3(dvd_InverseViewMatrix) * normalWV);
    const vec3 incident = normalize(dvd_CameraPosition - VAR._vertexW.xyz);

    const vec2 waterUV = clamp(0.5f * Homogenize(_vertexWVP).xy + 0.5f, vec2(0.001f), vec2(0.999f));
#if defined(SKIP_REFLECT_REFRACT)
    const vec3 refractColour = vec3(0.f);
#else //SKIP_REFLECT_REFRACT
    const vec3 refractColour = texture(texRefractPlanar, waterUV).rgb;
#endif //SKIP_REFLECT_REFRACT
    const vec3 refractionColour = overlayVec( refractColour, _refractionTint);

#if defined(MAIN_DISPLAY_PASS)
    switch (dvd_MaterialDebugFlag) {
        case DEBUG_REFRACTIONS:
            writeScreenColour(vec4(refractionColour, 1.f), vec3( 0.f ), normalWV );
            return;
        case DEBUG_REFLECTIONS:
#if defined(SKIP_REFLECT_REFRACT)
            writeScreenColour(vec4(0.f, 0.f, 0.f ,1.f), vec3( 0.f ), vec3( 0.f ), );
#else //SKIP_REFLECT_REFRACT
            writeScreenColour(texture(texReflectPlanar, waterUV), vec3( 0.f ), normalWV );
#endif //SKIP_REFLECT_REFRACT
            return;
    }
#endif //MAIN_DISPLAY_PASS

    // Get a modified viewing direction of the camera that only takes into account height.
    // ref: https://www.rastertek.com/terdx10tut16.html
    const vec3 texColour = (_underwater == 1 ? refractionColour : mix(refractionColour,
#if defined(SKIP_REFLECT_REFRACT)
                                                                      vec4(0.f, 0.f, 0.f, 1.f)
#else
                                                                      texture(texReflectPlanar, waterUV).rgb,
#endif
                                                                      Fresnel(incident, normalW)));
    
    vec4 outColour = getPixelColour(vec4(texColour, 1.f), data, normalWV);

    // Add some distance based fog to the water to hide reflection/refraction artifacts where no geometry is rendered
    const float fogDensity = smoothstep(_fogStartEndDistances.x, _fogStartEndDistances.y, length(VAR._vertexWV));
    //const float fogDensity = smoothstep(1.1f, 2.1f, fragDepth);
    outColour.rgb = mix(outColour.rgb, _waterDistanceFogColour, fogDensity);

    // Guess work based on what "look right"
    const vec3 sunDirection = GetSunDirection();
    const float lerpValue = Saturate(2.95f * (sunDirection.y + 0.15f));
    const vec3 lightDirection = mix(-sunDirection.xyz, sunDirection.xyz, lerpValue);

    // Calculate the reflection vector using the normal and the direction of the light.
    const vec3 reflection = -reflect(lightDirection, normalW);
    // Calculate the specular light based on the reflection and the camera position.
    const float specular = dot(normalize(reflection), incident);
    // Increase the specular light by the shininess value and add the specular to the final color.
    // Check to make sure the specular was positive so we aren't adding black spots to the water.

   
    writeScreenColour(outColour + (specular > 0.f ? pow(specular, _specularShininess) : 0.f), vec3( 0.f ), normalWV );

#else //!PRE_PASS
    const float roughness = getRoughness(data, VAR._texCoord, 0.f);
    writeGBuffer(normalWV, roughness);
#endif //PRE_PASS
}
