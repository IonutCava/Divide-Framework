#ifndef _PBR_FRAG_
#define _PBR_FRAG_

// AMAZING RESOURCE : http://www.frostbite.com/wp-content/uploads/2014/11/course_notes_moving_frostbite_to_pbr.pdf
// Reference: https://github.com/urho3d/Urho3D/blob/master/bin/CoreData/Shaders/GLSL/PBR.glsl
// Following BRDF methods are based upon research Frostbite EA
// [Lagrade et al. 2014, "Moving Frostbite to Physically Based Rendering"]
vec3 SchlickFresnel(in vec3 specular, in float VdotH);

float SchlickFresnel(in float u) {
    const float m = 1.f - u;
    const float m2 = SQUARED(m);
    return SQUARED(m2) * m; // pow(m,5)
}

//-------------------- VISIBILITY ---------------------------------------------------
// Smith GGX corrected Visibility
//   roughness    = the roughness of the pixel
//   NdotV        = the dot product of the normal and the camera view direction
//   NdotL        = the dot product of the normal and direction to the light
float SmithGGXSchlickVisibility(in float roughness, in float NdotV, in float NdotL)
{
    const float rough2 = SQUARED(roughness);
    const float lambdaV = NdotL  * sqrt((-NdotV * rough2 + NdotV) * NdotV + rough2);
    const float lambdaL = NdotV  * sqrt((-NdotL * rough2 + NdotL) * NdotL + rough2);

    return 0.5f / (lambdaV + lambdaL);
}

// Neumann Visibility
//   NdotV        = the dot product of the normal and the camera view direction
//   NdotL        = the dot product of the normal and direction to the light
float NeumannVisibility(in float NdotV, in float NdotL)
{
    return NdotL * NdotV / max(1e-7, max(NdotL, NdotV));
}
//----------------------------------------------------------------------------------

//-------------------- DISTRIBUTION-------------------------------------------------
// Blinn Distribution
//   roughness    = the roughness of the pixel
//   NdotH        = the dot product of the normal and the half vector
float BlinnPhongDistribution(in float roughnessSq, in float NdotH)
{
    // Calculate specular power from roughness
    const float specPower = max((2.f / roughnessSq) - 2.f, 1e-4);
    return pow(saturate(NdotH), specPower);
}

// Beckmann Distribution
//   roughness    = the roughness of the pixel
//   NdotH        = the dot product of the normal and the half vector
float BeckmannDistribution(in float roughnessSq, in float NdotH)
{
    const float NdotH2 = SQUARED(NdotH);

    const float roughnessA = 1.f / (4.f * roughnessSq * pow(NdotH, 4.f));
    const float roughnessB = NdotH2 - 1.f;
    const float roughnessC = roughnessSq * NdotH2;
    return roughnessA * exp(roughnessB / roughnessC);
}

// GGX Distribution
//   roughness    = the roughness of the pixel
//   NdotH        = the dot product of the normal and the half vector
float GGXDistribution(in float roughnessSq, in float NdotH)
{
    const float tmp = (NdotH * roughnessSq - NdotH) * NdotH + 1.f;
    return roughnessSq / SQUARED(tmp);
}
//----------------------------------------------------------------------------------


//-------------------- DIFFUSE -----------------------------------------------------
// Lambertian Diffuse
//   NdotL        = the normal dot with the light direction
float LambertianDiffuse(in float NdotL)
{
    return INV_M_PI * NdotL;
}

// Custom Lambertian Diffuse
//   roughness    = the roughness of the pixel
//   NdotV        = the normal dot with the camera view direction
//   NdotL        = the normal dot with the light direction
float CustomLambertianDiffuse(in float roughness, in float NdotV, in float NdotL)
{
    return  INV_M_PI * pow(NdotV, 0.5f + 0.3f * roughness) * NdotL;
}

// Burley Diffuse
//   roughness    = the roughness of the pixel
//   NdotV        = the normal dot with the camera view direction
//   NdotL        = the normal dot with the light direction
//   VdotH        = the camera view direction dot with the half vector
//   LdotH        = the dot product of the light direction and the half vector 
float BurleyDiffuse(in float roughness, in float NdotV, in float NdotL, in float VdotH, in float LdotH)
{
#if 0
    const float energyBias = mix(roughness, 0.0f, 0.5f);
    const float energyFactor = mix(roughness, 1.f, 1.f / 1.51f);
    const float fd90 = energyBias + 2.f * SQUARED(VdotH) * roughness;
    const float f0 = 1.f;
    const float lightScatter = f0 + (fd90 - f0) * pow(1.f - NdotL, 5.f);
    const float viewScatter = f0 + (fd90 - f0) * pow(1.f - NdotV, 5.f);
    return lightScatter * viewScatter * energyFactor;
#else
    const float INV_FD90 = 2.f * SQUARED(LdotH) * roughness - 0.5f;
    const float FdV = 1.f + INV_FD90 * SchlickFresnel(NdotV);
    const float FdL = 1.f + INV_FD90 * SchlickFresnel(NdotL);
    return INV_M_PI * FdV * FdL * NdotL;
#endif
}
//----------------------------------------------------------------------------------

//-------------------- FRESNEL -----------------------------------------------------
// Schlick Fresnel
//   specular  = the rgb specular color value of the pixel
//   VdotH     = the dot product of the camera view direction and the half vector 
vec3 SchlickFresnel(in vec3 specular, in float VdotH) {
    //return mix(specular, vec3(1.f), pow(1.f - VdotH, 5.f));
    return specular + (vec3(1.f) - specular) * pow(1.f - VdotH, 5.f);
}

// Schlick Gaussian Fresnel
//   specular  = the rgb specular color value of the pixel
//   VdotH     = the dot product of the camera view direction and the half vector 
vec3 SchlickGaussianFresnel(in vec3 specular, in float VdotH) {
    const float sphericalGaussian = pow(2.f, (-5.55473f * VdotH - 6.98316f) * VdotH);
    return specular + (vec3(1.f) - specular) * sphericalGaussian;
}

// Schlick Gaussian Fresnel
//   specular  = the rgb specular color value of the pixel
//   LdotH     = the dot product of the light direction and the half vector 
vec3 SchlickFresnelCustom(in vec3 specular, in float LdotH) {
    const float ior = 0.25f;
    const float airIor = 1.000277f;
    float f0 = (ior - airIor) / (ior + airIor);
    const float max_ior = 2.5f;
    f0 = clamp(SQUARED(f0), 0.f, (max_ior - airIor) / (max_ior + airIor));
    return specular * (f0 + (1 - f0) * pow(2, (-5.55473 * LdotH - 6.98316) * LdotH));
}

//----------------------------------------------------------------------------------

//Fresnel
//  specular  = the rgb specular color value of the pixel
//  VdotH     = the dot product of the camera view direction and the half vector 

//Distribution
//   roughnessSq  = the roughnessSq of the pixel squared
//   NdotH        = the dot product of the normal and the half vector

//Diffuse
//   diffuseColor = the rgb color value of the pixel
//   roughness    = the roughness of the pixel
//   NdotV        = the normal dot with the camera view direction
//   NdotL        = the normal dot with the light direction
//   VdotH        = the camera view direction dot with the half vector

//Visibility
//   roughness    = the roughness of the pixel
//   NdotL        = the dot product of the normal and direction to the light
//   NdotV        = the dot product of the normal and the camera view direction

//Return the PBR BRDF value
//   L  = normalised lightDir
//   V  = view direction
//   N  = surface normal of the pixel
//   lightColour = the colour of the light we're computing the BRDF factors for
//   lightAttenuation = attenuation factor to multiply the light's colour by (includes shadow, distance fade, etc)
//   ndl       = dot(normal,lightVec) [M_EPSILON,1.0f]
//   material = material value for the target pixel (base colour, OMR, spec value, etc)
vec3 GetBRDF(in vec3 L,
             in vec3 V,
             in vec3 N,
             in vec3 lightColour,
             in float lightAttenuation,
             in float NdotL,
             in float NdotV,
             in PBRMaterial material)
{
    const vec3 H = normalize(V + L);

    const float VdotH = clamp((dot(V, H)), M_EPSILON, 1.f);
    const float NdotH = clamp((dot(N, H)), M_EPSILON, 1.f);

#if defined(SHADING_MODE_PBR_MR)
    const float LdotH = clamp((dot(L, H)), M_EPSILON, 1.f);

    const vec3 diffuseFactor = material._diffuseColour * BurleyDiffuse(material._roughness, NdotV, NdotL, VdotH, LdotH);
    const vec3 fresnelTerm = SchlickGaussianFresnel(material._specular.rgb, VdotH);
    const float distTerm = GGXDistribution(material._a, NdotH);
    const float visTerm = NeumannVisibility(NdotV, NdotL);
#else //SHADING_MODE_PBR_SG
    //WRONG / TEMP / WHATEVER!
    const vec3 diffuseFactor = material._diffuseColour * LambertianDiffuse(NdotL);
    //diffuseFactor = material._diffuseColour * CustomLambertianDiffuse(material._roughness, NdotV, NdotL);
    const vec3 fresnelTerm = SchlickFresnel(material._specular.rgb, VdotH);
    const float distTerm = BlinnPhongDistribution(material._a, NdotH);
    const float visTerm = SmithGGXSchlickVisibility(material._roughness, NdotV, NdotL);
#endif //SHADING_MODE_PBR_MR

    const vec3 specularFactor = distTerm * visTerm * fresnelTerm * NdotL * INV_M_PI;

    const vec3 brdf = (diffuseFactor * material._occlusion) + specularFactor;

    return brdf * lightColour * lightAttenuation;
}

#endif //_PBR_FRAG_
