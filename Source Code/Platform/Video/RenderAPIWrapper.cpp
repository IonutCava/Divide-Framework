#include "stdafx.h"

#include "Headers/RenderAPIWrapper.h"

namespace Divide {
    namespace TypeUtil {
        const char* RenderTargetUsageToString(const RenderTargetUsage usage) noexcept {
            return Names::renderTargetUsage[to_base(usage)];
        }

        RenderTargetUsage StringToRenderTargetUsage(const char* name) noexcept {
            for (U8 i = 0u; i < to_U8(RenderTargetUsage::COUNT); ++i) {
                if (strcmp(name, Names::renderTargetUsage[i]) == 0) {
                    return static_cast<RenderTargetUsage>(i);
                }
            }

            return RenderTargetUsage::COUNT;
        }
    }
}; //namespace Divide