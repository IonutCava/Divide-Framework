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

bool PreRenderOperator::execute(const Camera* camera, const RenderTargetHandle& input, const RenderTargetHandle& output, GFX::CommandBuffer& bufferInOut) {
    ACKNOWLEDGE_UNUSED(camera);
    ACKNOWLEDGE_UNUSED(input);
    ACKNOWLEDGE_UNUSED(output);
    ACKNOWLEDGE_UNUSED(bufferInOut);

    return false;
}

void PreRenderOperator::reshape(U16 width, U16 height) {
    ACKNOWLEDGE_UNUSED(width);
    ACKNOWLEDGE_UNUSED(height);
}

void PreRenderOperator::onToggle(const bool state) {
    _enabled = state;
}
} //namespace Divide