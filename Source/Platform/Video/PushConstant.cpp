

#include "Headers/PushConstant.h"

namespace Divide {
namespace GFX {
    void PushConstant::clear() noexcept {
        efficient_clear( _buffer );
        dataSize(0u);
        type(PushConstantType::COUNT);
    }

} //namespace GFX
} //namespace Divide
