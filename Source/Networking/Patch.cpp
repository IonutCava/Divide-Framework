

#include "Headers/Patch.h"

namespace Divide {

namespace
{
    NO_DESTROY vector<FileData> s_ModelData;
};

namespace Patch
{
    void addGeometry(const FileData& data)
    {
        s_ModelData.push_back(data);
    }

    bool compareData(const PatchData& /*data*/) 
    {
        const bool updated = true;
        /*XML::loadScene(data.sceneName);
        for (vector<FileData>::iterator _iter = std::begin(ModelData);
             _iter != std::end(ModelData); _iter++) {
            for (U32 i = 0; i < data.size; i++) {
                // for each item in the scene
                if (data.name[i] ==  (*_iter).ItemName) {
                    // if the version differs
                    if ((*_iter).version != data.version[i])
                    {
                        if ((*_iter).ModelName == data.name[i]) {
                            // Don't update modelNames
                            (*_iter).ModelName == "nullptr";
                        }

                        updated = false;
                        continue;
                    } else {
                        ModelData.erase(_iter);
                    }
                }
            }
        }*/
        // After the 2 for's ModelData and VegetationData contain all the geometry
        // that needs patching;
        return updated;
    }

    const vector<FileData>& modelData() noexcept
    {
        return s_ModelData;
    }

    void clearModelData() noexcept
    {
        efficient_clear( s_ModelData );
    }
};

};  // namespace Divide
