// With huge thanks to:
//    Mikko Mononen http://groups.google.com/group/recastnavigation
// And thanks to  Daniel Buckmaster, 2011 for his
// T3D 1.1 implementation of Recast's DebugUtils.

#include "stdafx.h"

#include "Headers/NavMeshDebugDraw.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/IMPrimitive.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

namespace Divide::AI::Navigation {

NavMeshDebugDraw::NavMeshDebugDraw(GFXDevice& context) noexcept
    : _context(context)
{
}

NavMeshDebugDraw::~NavMeshDebugDraw()
{
    if (_primitive) {
        _context.destroyIMP(_primitive);
    }
}

void NavMeshDebugDraw::paused(const bool state) noexcept {
    _paused = state;
}

void NavMeshDebugDraw::depthMask(const bool state) noexcept {
    _depthMask = state;
}

void NavMeshDebugDraw::beginBatch() {
    if (!_primitive) {
        _dirty = true;
        _primitive = _context.newIMP("NavMesh Debug Draw");

        // Generate a render state
        RenderStateBlock navigationDebugStateBlock;
        navigationDebugStateBlock.setCullMode(CullMode::NONE);
        navigationDebugStateBlock.depthTestEnabled(_depthMask);
        navigationDebugStateBlock.depthWriteEnabled(false);

        PipelineDescriptor pipeDesc;
        pipeDesc._stateHash = navigationDebugStateBlock.getHash();
        pipeDesc._shaderProgramHandle = _context.imShaders()->imShaderNoTexture()->handle();
        _primitive->setPipelineDescriptor(pipeDesc);
    }

    assert(_primitive != nullptr);

    if (_dirty) {
        _primitive->beginBatch(true, 1024, 1);
    }
}

void NavMeshDebugDraw::endBatch() {
    if (!_dirty) {
        return;
    }

    if (_primitive) {
        _primitive->endBatch();
    }

    _dirty = false;
}

void NavMeshDebugDraw::begin(const duDebugDrawPrimitives prim, F32 size) {
    if (!_dirty || !_primitive) {
        return;
    }

    switch (prim) {
        case DU_DRAW_TRIS:
            _primType = PrimitiveTopology::TRIANGLES;
            break;
        case DU_DRAW_POINTS:
            _primType = PrimitiveTopology::POINTS;
            break;
        case DU_DRAW_LINES:
            _primType = PrimitiveTopology::LINES;
            break;
    }

    _primitive->attribute4f(to_base(AttribLocation::COLOR), FColour4(1.0f, 1.0f, 1.0f, 0.5f));
    _primitive->begin(_primType);
}

void NavMeshDebugDraw::vertex(const F32 x, const F32 y, const F32 z,
                              U32 colour) {
    if (!_dirty || !_primitive) {
        return;
    }
    if (_overrideColour) {
        colour = _colour;
    }

    UColour4 colourVec;
    rcCol(colour, colourVec.r, colourVec.g, colourVec.b, colourVec.a);
    colourVec.a = 64;

    _primitive->attribute4f(to_base(AttribLocation::COLOR), Util::ToFloatColour(colourVec));
    _primitive->vertex(x, y, z);
}

void NavMeshDebugDraw::end() {
    if (_dirty && _primitive) {
        _primitive->end();
    }
}

void NavMeshDebugDraw::toCommandBuffer( GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut ) const
{
    _primitive->getCommandBuffer(bufferInOut, memCmdInOut);
}

void NavMeshDebugDraw::overrideColour(const U32 col) noexcept {
    _overrideColour = true;
    _colour = col;
}

}  // namespace Divide::AI::Navigation
