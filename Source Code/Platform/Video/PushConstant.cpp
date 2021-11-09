#include "stdafx.h"

#include "Headers/PushConstant.h"

namespace Divide {
namespace GFX {
    void PushConstant::clear() {
        //_buffer.resize(0);
        dataSize(0u);
        type(PushConstantType::COUNT);
    }

}; //namespace GFX
}; //namespace Divide