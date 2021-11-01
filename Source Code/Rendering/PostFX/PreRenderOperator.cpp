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
    _screenOnlyDraw.drawMask().disableAll();
    _screenOnlyDraw.drawMask().setEnabled(RTAttachmentType::Colour, 0, true);

    GenericDrawCommand pointsCmd = {};
    pointsCmd._primitiveType = PrimitiveType::API_POINTS;
    pointsCmd._drawCount = 1;

    _pointDrawCmd = { pointsCmd };

    GenericDrawCommand triangleCmd = {};
    triangleCmd._primitiveType = PrimitiveType::TRIANGLES;
    triangleCmd._drawCount = 1;

    _triangleDrawCmd = { triangleCmd };
}

bool PreRenderOperator::execute([[maybe_unused]] const Camera* camera, [[maybe_unused]] const RenderTargetHandle& input, [[maybe_unused]] const RenderTargetHandle& output, [[maybe_unused]] GFX::CommandBuffer& bufferInOut) {
    return false;
}

void PreRenderOperator::reshape([[maybe_unused]] U16 width, [[maybe_unused]] U16 height) {
}

void PreRenderOperator::onToggle(const bool state) {
    _enabled = state;
}
} //namespace Divide