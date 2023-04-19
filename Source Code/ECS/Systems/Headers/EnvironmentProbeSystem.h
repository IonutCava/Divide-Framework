/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the Software
is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef _ENVIRONMENT_PROBE_SYSTEM_H_
#define _ENVIRONMENT_PROBE_SYSTEM_H_

#include "ECSSystem.h"
#include "Core/Headers/PlatformContextComponent.h"
#include "ECS/Components/Headers/EnvironmentProbeComponent.h"

namespace Divide {
    class EnvironmentProbeSystem final : public PlatformContextComponent,
                                         public ECSSystem<EnvironmentProbeSystem, EnvironmentProbeComponent>
    {
        using Parent = ECSSystem<EnvironmentProbeSystem, EnvironmentProbeComponent>;
    public:
        EnvironmentProbeSystem(ECS::ECSEngine& parentEngine, PlatformContext& context);

        void PreUpdate(F32 dt) override;
    };
}

#endif //_ENVIRONMENT_PROBE_SYSTEM_H_
