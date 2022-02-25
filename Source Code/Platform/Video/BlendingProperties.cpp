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
        Util::Hash_combine(hash, properties.enabled());
        Util::Hash_combine(hash, properties.blendSrc());
        Util::Hash_combine(hash, properties.blendDest());
        Util::Hash_combine(hash, properties.blendOp());
        Util::Hash_combine(hash, properties.blendSrcAlpha());
        Util::Hash_combine(hash, properties.blendDestAlpha());
        Util::Hash_combine(hash, properties.blendOpAlpha());

        return hash;
    }

    [[nodiscard]] size_t GetHash(const RTBlendStates& blendStates) {
        size_t hash = 1333377;
        Util::Hash_combine(hash, blendStates._blendColour.r);
        Util::Hash_combine(hash, blendStates._blendColour.g);
        Util::Hash_combine(hash, blendStates._blendColour.b);
        Util::Hash_combine(hash, blendStates._blendColour.a);
        for (const BlendingSettings& state : blendStates._settings) {
            Util::Hash_combine(hash, GetHash(state));
        }
        return hash;
    }

    bool operator==(const RTBlendStates& lhs, const RTBlendStates& rhs) noexcept {
        if (lhs._blendColour != rhs._blendColour) {
            return false;
        }
        for (U8 i = 0u; i < MAX_RT_COLOUR_ATTACHMENTS; ++i) {
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

        for (U8 i = 0u; i < MAX_RT_COLOUR_ATTACHMENTS; ++i) {
            if (lhs._settings[i] != rhs._settings[i]) {
                return true;
            }
        }

        return false;
    }

}; //namespace Divide