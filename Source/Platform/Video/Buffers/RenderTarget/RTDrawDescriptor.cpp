

#include "Headers/RTDrawDescriptor.h"

namespace Divide
{

BlitEntry INVALID_BLIT_ENTRY = {};

RTClearEntry DEFAULT_CLEAR_ENTRY =
{
    ._colour = DefaultColours::WHITE,
    ._enabled = true
};

/// Depth-clear entry for reversed-Z rendering (perspective cameras).
/// Clears the depth buffer to 0.0 (the "infinite far" value in reversed-Z).
RTClearEntry REVERSED_Z_DEPTH_CLEAR_ENTRY =
{
    ._colour = VECTOR4_ZERO,
    ._enabled = true
};

} //namespace Divide
