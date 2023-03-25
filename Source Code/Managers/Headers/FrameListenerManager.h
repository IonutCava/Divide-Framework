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
#ifndef _FRAME_LISTENER_MANAGER_H_
#define _FRAME_LISTENER_MANAGER_H_

/// Add this include here so that any FrameListener derived class only needs to include the manager

#include "Core/Headers/FrameListener.h"

namespace Divide {

class FrameListenerManager {

    using EventTimeMap = eastl::fixed_vector<U64, 16, false>;

  public:

    void registerFrameListener(FrameListener* listener, U32 callOrder);
    void removeFrameListener(FrameListener* listener);

    [[nodiscard]] bool frameEvent(const FrameEvent& evt);

    /// Calls createEvent and frameEvent
    [[nodiscard]] bool createAndProcessEvent(FrameEventType type, FrameEvent& evt);

  private:

    bool frameStarted(const FrameEvent& evt);
    bool framePreRender(const FrameEvent& evt);
    bool frameSceneRenderStarted(const FrameEvent& evt);
    bool frameSceneRenderEnded(const FrameEvent& evt);
    bool frameRenderingQueued(const FrameEvent& evt);
    bool framePostRender(const FrameEvent& evt);
    bool frameEnded(const FrameEvent& evt);

   private:
    vector<FrameListener*> _listeners;

};

};  // namespace Divide

#endif //_FRAME_LISTENER_MANAGER_H_
