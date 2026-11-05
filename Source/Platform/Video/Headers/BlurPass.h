// Copyright (c) 2026 DIVIDE-Studio
// Copyright (c) 2009-2026 Ionut Cava

#pragma once

#include "PushConstants.h"

namespace Divide::GFX
{
    class CommandBuffer;

    void RecordBlurDrawCommands( CommandBuffer& buffer, PushConstantsStruct pushData, bool gaussian, U8 layerCount );
}
