#include "stdafx.h"

#include "Headers/ConditionalWait.h"

#include "Core/Headers/PlatformContext.h"
#include "Headers/PlatformRuntime.h"

namespace Divide {
namespace {
    PlatformContext* g_ctx = nullptr;
}

void InitConditionalWait(PlatformContext& context) noexcept {
    g_ctx = &context;
}

void PlatformContextIdleCall() {
    if (Runtime::isMainThread() && g_ctx != nullptr) {
        g_ctx->idle(true, 0u);
    }
}
} //namespace Divide