#include "stdafx.h"

#include "Headers/SceneShaderData.h"
#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide {
SceneShaderData::SceneShaderData(GFXDevice& context)
    : _context(context)
{
    ShaderBufferDescriptor bufferDescriptor = {};
    bufferDescriptor._usage = ShaderBuffer::Usage::CONSTANT_BUFFER;
    bufferDescriptor._ringBufferLength = 3;
    bufferDescriptor._bufferParams._updateUsage = BufferUpdateUsage::CPU_W_GPU_R;
    bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::OCASSIONAL;
    {
        bufferDescriptor._bufferParams._elementCount = 1u;
        bufferDescriptor._bufferParams._elementSize = sizeof(SceneShaderBufferData);
        bufferDescriptor._bufferParams._initialData = { (Byte*)&_sceneBufferData, bufferDescriptor._bufferParams._elementSize };
        bufferDescriptor._name = "SCENE_SHADER_DATA";
        _sceneShaderData = _context.newSB(bufferDescriptor);
    }
    {
        bufferDescriptor._bufferParams._elementCount = GLOBAL_PROBE_COUNT;
        bufferDescriptor._bufferParams._elementSize = sizeof(ProbeData);
        bufferDescriptor._bufferParams._initialData = { (Byte*)_probeData.data(), bufferDescriptor._bufferParams._elementSize };
        bufferDescriptor._name = "SCENE_PROBE_DATA";
        _probeShaderData = _context.newSB(bufferDescriptor);
    }
}

GFX::MemoryBarrierCommand SceneShaderData::bindSceneDescriptorSet(GFX::CommandBuffer& bufferInOut) {
    DescriptorSet& bindSet = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set;

    GFX::MemoryBarrierCommand memBarrier{};
    if (_sceneDataDirty) {
        _sceneShaderData->incQueue();
        memBarrier._bufferLocks.push_back(_sceneShaderData->writeData(&_sceneBufferData));
        _sceneDataDirty = false;
    }

    if (_probeDataDirty) {
        _probeShaderData->incQueue();
        memBarrier._bufferLocks.push_back(_probeShaderData->writeData(_probeData.data()));
        _sceneDataDirty = false;
    }

    ShaderBufferBinding sceneBufferBinding;
    sceneBufferBinding._binding = ShaderBufferLocation::SCENE_DATA;
    sceneBufferBinding._buffer = _sceneShaderData.get();
    sceneBufferBinding._elementRange = { 0, 1 };
    bindSet._buffers.add(sceneBufferBinding);

    ShaderBufferBinding probeBufferBinding;
    probeBufferBinding._binding = ShaderBufferLocation::PROBE_DATA;
    probeBufferBinding._buffer = _probeShaderData.get();
    probeBufferBinding._elementRange = { 0, GLOBAL_PROBE_COUNT };
    bindSet._buffers.add(probeBufferBinding);

    return memBarrier;
}

} //namespace Divide