

#include "Headers/BlendingProperties.h"

namespace Divide {

    [[nodiscard]] size_t GetHash(const BlendingSettings& properties) {
        size_t hash = 1337;
        Util::Hash_combine(hash, properties.enabled(),
                                 properties.blendSrc(),
                                 properties.blendDest(),
                                 properties.blendOp(),
                                 properties.blendSrcAlpha(),
                                 properties.blendDestAlpha(),
                                 properties.blendOpAlpha());

        return hash;
    }

    [[nodiscard]] size_t GetHash(const RTBlendStates& blendStates) {
        size_t hash = 1333377;
        Util::Hash_combine(hash, blendStates._blendColour.r,
                                 blendStates._blendColour.g,
                                 blendStates._blendColour.b,
                                 blendStates._blendColour.a);

        for (const BlendingSettings& state : blendStates._settings) {
            Util::Hash_combine(hash, GetHash(state));
        }
        return hash;
    }

}; //namespace Divide
