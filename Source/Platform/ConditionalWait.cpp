

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

void PlatformContextIdleCall()
{
    if (Runtime::isMainThread() && g_ctx != nullptr)
    {
        const U32 componentMask = g_ctx->componentMask();
        g_ctx->componentMask( to_base(PlatformContext::SystemComponentType::NONE) );
        // Mostly wait on threaded callbacks
        g_ctx->idle(true, 0u, 0u);
        g_ctx->componentMask( componentMask );
    }
}
} //namespace Divide
