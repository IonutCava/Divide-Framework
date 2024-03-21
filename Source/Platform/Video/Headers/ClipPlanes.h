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
#ifndef DVD_CLIP_PLANES_H_
#define DVD_CLIP_PLANES_H_

namespace Divide {
    enum class ClipPlaneIndex : U8 {
        CLIP_PLANE_0 = 0,
        CLIP_PLANE_1,
        CLIP_PLANE_2,
        CLIP_PLANE_3,
        CLIP_PLANE_4,
        CLIP_PLANE_5,
        COUNT
    };

    template<size_t N>
    struct ClipPlaneList {
        void resetAll() {
            _planeState.fill(false);
        }

        void set(U32 index, const Plane<F32>& plane) noexcept {
            assert(index < N);

            _planes[index] = plane;
            _planeState[index] = true;
        }

        void reset(U32 index) {
            assert(index < N);
            _planeState[index] = false;
        }

        bool operator==(const ClipPlaneList& rhs) const noexcept {
            return _planeState == rhs._planeState &&
                   _planes == rhs._planes;
        }

        bool operator!=(const ClipPlaneList& rhs) const {
            return _planeState != rhs._planeState ||
                   _planes != rhs._planes;
        }

        [[nodiscard]] const PlaneList<N>& planes() const noexcept { return _planes; }
        [[nodiscard]] const std::array<bool, N>& planeState() const noexcept { return _planeState; }

    private:
        PlaneList<N> _planes;
        std::array<bool, N> _planeState = create_array<N, bool>(false);
    };
    
    using FrustumClipPlanes = ClipPlaneList<to_base(ClipPlaneIndex::COUNT)>;
}; //namespace Divide

#endif //DVD_CLIP_PLANES_H_
