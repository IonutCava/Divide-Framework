#include "stdafx.h"

#include "Headers/NodeBufferedData.h"


namespace Divide {
    size_t HashMaterialData(const NodeMaterialData& dataIn) {
        size_t tempHash = 9999991;
        
        Util::Hash_combine(tempHash, dataIn._albedo.x * 255);
        Util::Hash_combine(tempHash, dataIn._albedo.y * 255);
        Util::Hash_combine(tempHash, dataIn._albedo.z * 255);
        Util::Hash_combine(tempHash, dataIn._albedo.w * 255);

        Util::Hash_combine(tempHash, dataIn._emissiveAndParallax.x * 255);
        Util::Hash_combine(tempHash, dataIn._emissiveAndParallax.y * 255);
        Util::Hash_combine(tempHash, dataIn._emissiveAndParallax.z * 255);
        Util::Hash_combine(tempHash, dataIn._emissiveAndParallax.w * 255);

        Util::Hash_combine(tempHash, dataIn._colourData.x * 255);
        Util::Hash_combine(tempHash, dataIn._colourData.y * 255);
        Util::Hash_combine(tempHash, dataIn._colourData.z * 255);
        Util::Hash_combine(tempHash, dataIn._colourData.w * 255);

        Util::Hash_combine(tempHash, dataIn._data.x * 255);
        Util::Hash_combine(tempHash, dataIn._data.y * 255);
        Util::Hash_combine(tempHash, dataIn._data.z * 255);
        Util::Hash_combine(tempHash, dataIn._data.w * 255); 
        
        Util::Hash_combine(tempHash, dataIn._textureOperations.x * 255);
        Util::Hash_combine(tempHash, dataIn._textureOperations.y * 255);
        Util::Hash_combine(tempHash, dataIn._textureOperations.z * 255);
        Util::Hash_combine(tempHash, dataIn._textureOperations.w * 255);
        
        return tempHash;
    }

    size_t HashTexturesData(const NodeMaterialTextures& dataIn) {
        size_t tempHash = 9999991;
        for (U8 i = 0u; i < MATERIAL_TEXTURE_COUNT; ++i) {
            Util::Hash_combine(tempHash, Uvec2ToTexture(dataIn[i]));
        }

        return tempHash;
    }

}; //namespace Divide