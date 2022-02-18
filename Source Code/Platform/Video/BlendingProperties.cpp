#include "stdafx.h"

#include "Headers/BlendingProperties.h"

namespace Divide {
    bool operator==(const BlendingProperties& lhs, const BlendingProperties& rhs) noexcept {
        return lhs.enabled()        == rhs.enabled() &&
               lhs.blendSrc()       == rhs.blendSrc() &&
               lhs.blendDest()      == rhs.blendDest() &&
               lhs.blendOp()        == rhs.blendOp() &&
               lhs.blendSrcAlpha()  == rhs.blendSrcAlpha() &&
               lhs.blendDestAlpha() == rhs.blendDestAlpha() &&
               lhs.blendOpAlpha()   == rhs.blendOpAlpha();
    }

    bool operator!=(const BlendingProperties& lhs, const BlendingProperties& rhs) noexcept {
        return lhs.enabled()        != rhs.enabled() ||
               lhs.blendSrc()       != rhs.blendSrc() ||
               lhs.blendDest()      != rhs.blendDest() ||
               lhs.blendOp()        != rhs.blendOp() ||
               lhs.blendSrcAlpha()  != rhs.blendSrcAlpha() ||
               lhs.blendDestAlpha() != rhs.blendDestAlpha() ||
               lhs.blendOpAlpha()   != rhs.blendOpAlpha();
    }

    [[nodiscard]] size_t GetHash(const BlendingProperties& properties) {
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
        for (const RTBlendState& state : blendStates) {
            Util::Hash_combine(hash, state._blendColour.r);
            Util::Hash_combine(hash, state._blendColour.g);
            Util::Hash_combine(hash, state._blendColour.b);
            Util::Hash_combine(hash, state._blendColour.a);
            Util::Hash_combine(hash, GetHash(state._blendProperties));
        }
        return hash;
    }

    bool operator==(const RTBlendState& lhs, const RTBlendState& rhs) noexcept {
        return lhs._blendColour == rhs._blendColour &&
               lhs._blendProperties == rhs._blendProperties;
    }

    bool operator!=(const RTBlendState& lhs, const RTBlendState& rhs) noexcept {
        return lhs._blendColour != rhs._blendColour ||
               lhs._blendProperties != rhs._blendProperties;
    }
}; //namespace Divide