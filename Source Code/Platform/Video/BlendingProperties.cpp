#include "stdafx.h"

#include "Headers/BlendingProperties.h"

namespace Divide {
    bool operator==(const BlendingSettings& lhs, const BlendingSettings& rhs) noexcept {
        return lhs.enabled()        == rhs.enabled() &&
               lhs.blendSrc()       == rhs.blendSrc() &&
               lhs.blendDest()      == rhs.blendDest() &&
               lhs.blendOp()        == rhs.blendOp() &&
               lhs.blendSrcAlpha()  == rhs.blendSrcAlpha() &&
               lhs.blendDestAlpha() == rhs.blendDestAlpha() &&
               lhs.blendOpAlpha()   == rhs.blendOpAlpha();
    }

    bool operator!=(const BlendingSettings& lhs, const BlendingSettings& rhs) noexcept {
        return lhs.enabled()        != rhs.enabled() ||
               lhs.blendSrc()       != rhs.blendSrc() ||
               lhs.blendDest()      != rhs.blendDest() ||
               lhs.blendOp()        != rhs.blendOp() ||
               lhs.blendSrcAlpha()  != rhs.blendSrcAlpha() ||
               lhs.blendDestAlpha() != rhs.blendDestAlpha() ||
               lhs.blendOpAlpha()   != rhs.blendOpAlpha();
    }

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

    bool operator==(const RTBlendStates& lhs, const RTBlendStates& rhs) noexcept {
        if (lhs._blendColour != rhs._blendColour) {
            return false;
        }
        for (size_t i = 0u; i < lhs._settings.size(); ++i ) {
            if (lhs._settings[i] != rhs._settings[i]) {
                return false;
            }
        }

        return true;
    }

    bool operator!=(const RTBlendStates& lhs, const RTBlendStates& rhs) noexcept {
        if (lhs._blendColour != rhs._blendColour) {
            return true;
        }

        for (size_t i = 0u; i < lhs._settings.size(); ++i ) {
            if (lhs._settings[i] != rhs._settings[i]) {
                return true;
            }
        }

        return false;
    }

}; //namespace Divide