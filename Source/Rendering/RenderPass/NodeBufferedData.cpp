

#include "Headers/NodeBufferedData.h"


namespace Divide {
    size_t HashMaterialData(const NodeMaterialData& dataIn) {
        size_t tempHash = 9999991;
        
        Util::Hash_combine(tempHash, dataIn._albedo.x * 255,
                                     dataIn._albedo.y * 255,
                                     dataIn._albedo.z * 255,
                                     dataIn._albedo.w * 255);

        Util::Hash_combine(tempHash, dataIn._emissiveAndParallax.x * 255,
                                     dataIn._emissiveAndParallax.y * 255,
                                     dataIn._emissiveAndParallax.z * 255,
                                     dataIn._emissiveAndParallax.w * 255);

        Util::Hash_combine(tempHash, dataIn._colourData.x * 255,
                                     dataIn._colourData.y * 255,
                                     dataIn._colourData.z * 255,
                                     dataIn._colourData.w * 255);

        Util::Hash_combine(tempHash, dataIn._data.x * 255,
                                     dataIn._data.y * 255,
                                     dataIn._data.z * 255,
                                     dataIn._data.w * 255); 
        
        Util::Hash_combine(tempHash, dataIn._textureOperations.x * 255,
                                     dataIn._textureOperations.y * 255,
                                     dataIn._textureOperations.z * 255,
                                     dataIn._textureOperations.w * 255);
        
        return tempHash;
    }

}; //namespace Divide