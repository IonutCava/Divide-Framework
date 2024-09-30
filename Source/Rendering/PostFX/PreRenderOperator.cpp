

#include "Headers/PreRenderOperator.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Rendering/Camera/Headers/Camera.h"

namespace Divide {

PreRenderOperator::PreRenderOperator(GFXDevice& context, PreRenderBatch& parent, const FilterType operatorType, [[maybe_unused]] std::atomic_uint& taskCounter)
    : _context(context),
      _parent(parent),
      _operatorType(operatorType)
{
    std::ranges::fill(_screenOnlyDraw._drawMask, false);
    _screenOnlyDraw._drawMask[to_base(RTColourAttachmentSlot::SLOT_0)] = true;
}

bool PreRenderOperator::execute([[maybe_unused]] const PlayerIndex idx, [[maybe_unused]] const CameraSnapshot& cameraSnapshot, [[maybe_unused]] const RenderTargetHandle& input, [[maybe_unused]] const RenderTargetHandle& output, [[maybe_unused]] GFX::CommandBuffer& bufferInOut)
{
    return false;
}

void PreRenderOperator::prepare([[maybe_unused]] const PlayerIndex idx, [[maybe_unused]] GFX::CommandBuffer& bufferInOut)
{
}

void PreRenderOperator::reshape([[maybe_unused]] U16 width, [[maybe_unused]] U16 height)
{
}

void PreRenderOperator::onToggle(const bool state)
{
    _enabled = state;
}
} //namespace Divide
