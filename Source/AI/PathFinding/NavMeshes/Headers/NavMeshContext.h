/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */
#pragma once
#ifndef DVD_NAV_MESH_CTX_H_
#define DVD_NAV_MESH_CTX_H_

#include "NavMeshDefines.h"

namespace Divide {
namespace AI {
namespace Navigation {

class rcContextDivide final : public rcContext {
   public:
    explicit rcContextDivide(const bool state) : rcContext(state)
    {
        _startTime.fill(0.0);
        _accTime.fill(0);
        resetTimers();
    }

    ~rcContextDivide() = default;

   private:
    /// Logs a message.
    ///  @param[in]        category    The category of the message.
    ///  @param[in]        msg            The formatted message.
    ///  @param[in]        len            The length of the formatted message.
    void doLog(rcLogCategory category, const char* msg, I32 len) override;
    void doResetTimers() noexcept override;
    void doStartTimer(rcTimerLabel /*label*/) noexcept override;
    void doStopTimer(rcTimerLabel /*label*/) override;
    [[nodiscard]] I32 doGetAccumulatedTime(rcTimerLabel /*label*/) const noexcept override;

   private:
    std::array<D64, RC_MAX_TIMERS> _startTime;
    std::array<I32, RC_MAX_TIMERS> _accTime;
};
}  // namespace Navigation
}  // namespace AI
}  // namespace Divide

#endif //DVD_NAV_MESH_CTX_H_
