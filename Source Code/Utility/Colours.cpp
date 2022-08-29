#include "stdafx.h"

#include "Headers/Colours.h"

namespace Divide {
    namespace DefaultColours {
        /// Random stuff added for convenience
        FColour4 WHITE = { 1.0f, 1.0f, 1.0f, 1.0f };
        FColour4 BLACK = { 0.0f, 0.0f, 0.0f, 1.0f };
        FColour4 RED = { 1.0f, 0.0f, 0.0f, 1.0f };
        FColour4 GREEN = { 0.0f, 1.0f, 0.0f, 1.0f };
        FColour4 BLUE = { 0.0f, 0.0f, 1.0f, 1.0f };

        UColour4 WHITE_U8 = { 255, 255, 255, 255 };
        UColour4 BLACK_U8 = { 0,   0,   0,   255 };
        UColour4 RED_U8 = { 255, 0,   0,   255 };
        UColour4 GREEN_U8 = { 0,   255, 0,   255 };
        UColour4 BLUE_U8 = { 0,   0,   255, 255 };

        FColour4 DIVIDE_BLUE = { 0.1f, 0.1f, 0.8f, 1.0f };
        UColour4 DIVIDE_BLUE_U8 = { 26,   26,   204,  255 };

        vec4<U8> RANDOM() {
            return vec4<U8>(Random<U8>(255),
                            Random<U8>(255),
                            Random<U8>(255),
                            to_U8(255));
        }

        vec4<F32> RANDOM_NORMALIZED() {
            return Util::ToFloatColour(RANDOM());
        }

    }  // namespace DefaultColours
} //namespace Divide
