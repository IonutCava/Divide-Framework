#include "stdafx.h"

#include "Headers/glIMPrimitive.h"
#include "Headers/glResources.h"
#include "Headers/GLWrapper.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/GLIM/glim.h"

namespace Divide {

glIMPrimitive::glIMPrimitive(GFXDevice& context)
    : IMPrimitive(context)
{
    _imInterface = MemoryManager_NEW NS_GLIM::GLIM_BATCH();
    _imInterface->SetVertexAttribLocation(to_base(AttribLocation::POSITION));
}

glIMPrimitive::~glIMPrimitive()
{
    MemoryManager::DELETE(_imInterface);
}

void glIMPrimitive::beginBatch(const bool reserveBuffers, const U32 vertexCount, const U32 attributeCount) {
    _imInterface->BeginBatch(reserveBuffers, vertexCount, attributeCount);
}

void glIMPrimitive::clearBatch() {
    _imInterface->Clear(true, 64 * 3, 1);
}

bool glIMPrimitive::hasBatch() const noexcept {
    return !_imInterface->isCleared();
}

void glIMPrimitive::begin(const PrimitiveTopology type) {
    _imInterface->Begin(GLUtil::glimPrimitiveType[to_U32(type)]);
}

void glIMPrimitive::vertex(const F32 x, const  F32 y, const F32 z) {
    _imInterface->Vertex(x, y, z);
}

void glIMPrimitive::attribute1f(const U32 attribLocation, const F32 value) {
    _imInterface->Attribute1f(attribLocation, value);
}

void glIMPrimitive::attribute2f(const U32 attribLocation, const vec2<F32> value) {
    _imInterface->Attribute2f(attribLocation, value.x, value.y);
}

void glIMPrimitive::attribute3f(const U32 attribLocation, const vec3<F32> value) {
    _imInterface->Attribute3f(attribLocation, value.x, value.y, value.z);
}

void glIMPrimitive::attribute4ub(const U32 attribLocation, const U8 x, const U8 y, const U8 z, const U8 w) {
    _imInterface->Attribute4ub(attribLocation, x, y, z, w);
}

void glIMPrimitive::attribute4f(const U32 attribLocation, const F32 x, const F32 y, const F32 z, const F32 w) {
    _imInterface->Attribute4f(attribLocation, x, y, z, w);
}

void glIMPrimitive::attribute1i(const U32 attribLocation, const I32 value) {
    _imInterface->Attribute1i(attribLocation, value);
}

void glIMPrimitive::end() {
    _imInterface->End();
}

void glIMPrimitive::endBatch() noexcept {
    _imInterface->EndBatch();
}

void glIMPrimitive::pipeline(const Pipeline& pipeline) noexcept {

    IMPrimitive::pipeline(pipeline);
    _imInterface->SetShaderProgramHandle(pipeline.descriptor()._shaderProgramHandle);
}

void glIMPrimitive::draw(const GenericDrawCommand& cmd) {
    _imInterface->RenderBatchInstanced(cmd._cmd.primCount, _forceWireframe || isEnabledOption(cmd, CmdRenderOptions::RENDER_WIREFRAME));
}
};