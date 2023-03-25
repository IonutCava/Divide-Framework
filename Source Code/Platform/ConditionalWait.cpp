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
        const U32 componentMask = g_ctx->componentMask();
        Attorney::PlatformContextKernel::setComponentMask( *g_ctx, 0u);
        // Mostly wait on threaded callbacks
        g_ctx->idle(true);
        Attorney::PlatformContextKernel::setComponentMask( *g_ctx, componentMask );
    }
}
} //namespace Divide