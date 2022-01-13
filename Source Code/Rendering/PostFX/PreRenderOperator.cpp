#include "stdafx.h"

#include "Headers/PreRenderOperator.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Rendering/Camera/Headers/Camera.h"

namespace Divide {

PreRenderOperator::PreRenderOperator(GFXDevice& context, PreRenderBatch& parent, const FilterType operatorType)
    : _context(context),
      _parent(parent),
      _operatorType(operatorType)
{
    DisableAll(_screenOnlyDraw._drawMask);
    SetEnabled(_screenOnlyDraw._drawMask, RTAttachmentType::Colour, 0, true);

    GenericDrawCommand pointsCmd = {};
    pointsCmd._primitiveType = PrimitiveType::API_POINTS;
    pointsCmd._drawCount = 1;

    _pointDrawCmd = { pointsCmd };

    GenericDrawCommand triangleCmd = {};
    triangleCmd._primitiveType = PrimitiveType::TRIANGLES;
    triangleCmd._drawCount = 1;

    _triangleDrawCmd = { triangleCmd };
}

bool PreRenderOperator::execute([[maybe_unused]] const PlayerIndex idx, [[maybe_unused]] const CameraSnapshot& cameraSnapshot, [[maybe_unused]] const RenderTargetHandle& input, [[maybe_unused]] const RenderTargetHandle& output, [[maybe_unused]] GFX::CommandBuffer& bufferInOut) {
    return false;
}

void PreRenderOperator::prepare([[maybe_unused]] const PlayerIndex idx, [[maybe_unused]] GFX::CommandBuffer& bufferInOut) {

}

void PreRenderOperator::reshape([[maybe_unused]] U16 width, [[maybe_unused]] U16 height) {
}

void PreRenderOperator::onToggle(const bool state) {
    _enabled = state;
}
} //namespace Divide