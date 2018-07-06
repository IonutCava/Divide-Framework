-- Vertex

#include "vbInputData.vert"
#include "lightingDefaults.vert"

#if defined(ADD_FOLIAGE)
#include "foliage.vert"
#endif
void main(void){

    computeData();

#if defined(ADD_FOLIAGE) && defined(IS_TREE)
    computeFoliageMovementTree(dvd_Vertex);
#endif
    
    computeLightVectors();

    //Compute the final vert position
    gl_Position = dvd_ViewProjectionMatrix * VAR._vertexW;
}

-- Fragment
#if !defined(HAS_TRANSPARENCY)
layout(early_fragment_tests) in;
#endif

#include "BRDF.frag"

#if defined(COMPUTE_TBN)
    #include "bumpMapping.frag"
#endif

layout(location = 0) out vec4 _colourOut;
layout(location = 1) out vec3 _normalOut;

//subroutine vec4 MappingRoutineType();

//layout(location = 0) subroutine uniform MappingRoutineType MappingRoutine;

//subroutine(MappingRoutineType)
vec4 mappingFlat(){
    return getPixelColour(VAR._texCoord, VAR._normalWV);
}

#if defined(COMPUTE_TBN)
//subroutine(MappingRoutineType)
vec4 mappingNormal(){
    vec3 bump = getBump(VAR._texCoord);
    return mix(getPixelColour(VAR._texCoord, getTBNNormal(bump)),
               mappingFlat(),
               vec4(dvd_lodLevel > 1));
}

//subroutine(MappingRoutineType)
vec4 mappingRelief(){
    return mix(ReliefMapping(bumpMapLightID, VAR._texCoord),
               mappingFlat(),
               vec4(dvd_lodLevel > 1));
}

//subroutine(MappingRoutineType)
vec4 mappingParallax(){
    return mix(ParallaxMapping(bumpMapLightID, VAR._texCoord),
               mappingFlat(),
               vec4(dvd_lodLevel > 1));
}
#endif

void main (void){
    //_colourOut = ToSRGB(applyFog(MappingRoutine()));
#if defined(COMPUTE_TBN)
#    if defined(USE_PARALLAX_MAPPING)
    _colourOut = ToSRGB(applyFog(ParallaxMapping()));
#    elif defined(USE_RELIEF_MAPPING)
    _colourOut = ToSRGB(applyFog(ReliefMapping()));
#    else
    _colourOut = ToSRGB(applyFog(mappingNormal()));
#    endif
#else
    _colourOut = ToSRGB(applyFog(mappingFlat()));
#endif

    _normalOut = processedNormal;
}   