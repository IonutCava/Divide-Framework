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

const DescriptorSet& SceneShaderData::getDescriptorSet() {
    static DescriptorSet bindSet{};

    if (_sceneDataDirty) {
        _sceneShaderData->incQueue();
        _sceneShaderData->writeData(&_sceneBufferData);
    }

    if (_probeDataDirty) {
        _probeShaderData->incQueue();
        _probeShaderData->writeData(_probeData.data());
    }

    ShaderBufferBinding sceneBufferBinding;
    sceneBufferBinding._binding = ShaderBufferLocation::SCENE_DATA;
    sceneBufferBinding._buffer = _sceneShaderData.get();
    sceneBufferBinding._elementRange = { 0, 1 };
    sceneBufferBinding._lockType = _sceneDataDirty ? ShaderBufferLockType::AFTER_DRAW_COMMANDS : ShaderBufferLockType::COUNT;
    bindSet._buffers.add(sceneBufferBinding);

    ShaderBufferBinding probeBufferBinding;
    probeBufferBinding._binding = ShaderBufferLocation::PROBE_DATA;
    probeBufferBinding._buffer = _probeShaderData.get();
    probeBufferBinding._elementRange = { 0, GLOBAL_PROBE_COUNT };
    probeBufferBinding._lockType = _probeDataDirty ? ShaderBufferLockType::AFTER_DRAW_COMMANDS : ShaderBufferLockType::COUNT;
    bindSet._buffers.add(probeBufferBinding);

    _sceneDataDirty = false;
    _probeDataDirty = false;
    return bindSet;
}

} //namespace Divide