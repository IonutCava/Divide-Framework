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
#ifndef DVD_PRE_RENDER_BATCH_INL_
#define DVD_PRE_RENDER_BATCH_INL_

namespace Divide {

    [[nodiscard]] inline RenderTargetHandle PreRenderBatch::getLinearDepthRT() const noexcept {
        return _linearDepthRT;
    }

    [[nodiscard]] inline RenderTargetHandle PreRenderBatch::getTarget(const bool hdr, const bool swapped) const noexcept {
        if (hdr) {
            return swapped ? _screenRTs._hdr._screenCopy : _screenRTs._hdr._screenRef;
        }

        return _screenRTs._ldr._temp[swapped ? 0 : 1];
    }

    [[nodiscard]] inline RenderTargetHandle PreRenderBatch::getInput(const bool hdr) const {
        return getTarget(hdr, hdr ? _screenRTs._swappedHDR : _screenRTs._swappedLDR);
    }

    [[nodiscard]] inline RenderTargetHandle PreRenderBatch::getOutput(const bool hdr) const {
        return getTarget(hdr, hdr ? !_screenRTs._swappedHDR : !_screenRTs._swappedLDR);
    }

    [[nodiscard]] inline RenderTargetHandle PreRenderBatch::screenRT() const noexcept {
        return _screenRTs._hdr._screenRef;
    }

    [[nodiscard]] inline RenderTargetHandle PreRenderBatch::edgesRT() const noexcept {
        return _sceneEdges;
    }

    [[nodiscard]] inline Handle<Texture> PreRenderBatch::luminanceTex() const noexcept {
        return _currentLuminance;
    }

}; //namespace Divide

#endif //DVD_PRE_RENDER_BATCH_INL_
