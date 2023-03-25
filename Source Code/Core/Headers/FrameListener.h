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
#ifndef _FRAME_LISTENER_H_
#define _FRAME_LISTENER_H_

/// As you might of guessed it, it's the same system used in Ogre3D
/// (http://www.ogre3d.org/docs/api/html/OgreFrameListener_8h_source.html)
/// I decided to use something that people already know and are comfortable with
///-Ionut

namespace Divide {

enum class FrameEventType : U8 {
    FRAME_EVENT_ANY = 0,
    FRAME_EVENT_STARTED,
    FRAME_PRERENDER,
    FRAME_SCENERENDER_START,
    FRAME_SCENERENDER_END,
    FRAME_POSTRENDER,
    FRAME_EVENT_PROCESS,
    FRAME_EVENT_ENDED,
};

struct FrameEvent {
    struct Time {
        struct Impl {
            U64 _deltaTimeUS;
            U64 _currentTimeUS;
        } _app, _game;
    } _time; 

    FrameEventType _type;
};

class FrameListenerManager;
/// FrameListener class.
/// Has 3 events, associated with the start of rendering a frame,
/// the end of rendering and the end of buffer swapping after frames
/// All events have timers associated with them for update timing
class FrameListener : public GUIDWrapper {
   public:
    /// Either give it a name
    explicit FrameListener(const Str64& name, FrameListenerManager& parent, U32 callOrder);
    virtual ~FrameListener();

    bool operator<(FrameListener& that) const noexcept {
        return this->_callOrder < that._callOrder;
    }

   protected:
    friend class FrameListenerManager;
    void setCallOrder(const U32 order) noexcept { _callOrder = order; }
    /// Adapter patern instead of pure interface for the same reason as the Ogre
    /// boys pointed out:
    /// Implement what you need without filling classes with dummy functions
    /// frameStarted is calld at the beggining of a new frame before processing
    /// the logic aspect of a scene
    [[nodiscard]] virtual bool frameStarted([[maybe_unused]] const FrameEvent& evt) { return true; }
    /// framePreRenderStarted is called when we need to start processing the visual aspect of a scene
    [[nodiscard]] virtual bool framePreRender([[maybe_unused]] const FrameEvent& evt) { return true; }
    /// frameSceneRenderStarted is called right before rendering the scene for the current player starts
    [[nodiscard]] virtual bool frameSceneRenderStarted([[maybe_unused]] const FrameEvent& evt) { return true; }
    /// frameSceneRenderEnded is called immediately after scene rendering for the current player has ended but before any blitting operations
    [[nodiscard]] virtual bool frameSceneRenderEnded([[maybe_unused]] const FrameEvent& evt) { return true; }
    /// frameRendering Queued is called after all the frame setup/rendering but
    /// before the call to SwapBuffers
    [[nodiscard]] virtual bool frameRenderingQueued([[maybe_unused]] const FrameEvent& evt) { return true; }
    /// framePostRenderStarted is called after the main rendering calls are
    /// finished (e.g. use this for debug calls)
    [[nodiscard]] virtual bool framePostRender([[maybe_unused]] const FrameEvent& evt) { return true; }
    /// frameEnded is called after the buffers have been swapped
    [[nodiscard]] virtual bool frameEnded([[maybe_unused]] const FrameEvent& evt) { return true; }

    PROPERTY_R_IW(bool, enabled, false);
    PROPERTY_RW(Str64, name);

   private:
    FrameListenerManager& _mgr;
    /// If multiple frame listeners are handling the same event, this call ordervariable is used for sorting
    U32 _callOrder{0u};
};

}  // namespace Divide
#endif
