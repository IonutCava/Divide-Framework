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
        _sceneShaderData->bind(ShaderBufferLocation::SCENE_DATA);
    }
    {
        bufferDescriptor._bufferParams._elementCount = GLOBAL_PROBE_COUNT;
        bufferDescriptor._bufferParams._elementSize = sizeof(ProbeData);
        bufferDescriptor._bufferParams._initialData = { (Byte*)_probeData.data(), bufferDescriptor._bufferParams._elementSize };
        bufferDescriptor._name = "SCENE_PROBE_DATA";
        _probeShaderData = _context.newSB(bufferDescriptor);
        _probeShaderData->bind(ShaderBufferLocation::PROBE_DATA);
    }
}

void SceneShaderData::uploadToGPU() {
    if (_sceneDataDirty) {
        _sceneShaderData->lockRange(ShaderBufferLockType::IMMEDIATE);
        _sceneShaderData->incQueue();
        _sceneShaderData->writeData(&_sceneBufferData);
        _sceneShaderData->bind(ShaderBufferLocation::SCENE_DATA);
        _sceneDataDirty = false;
    }

    if (_probeDataDirty) {
        _probeShaderData->lockRange(ShaderBufferLockType::IMMEDIATE);
        _probeShaderData->incQueue();
        _probeShaderData->writeData(_probeData.data());
        _probeShaderData->bind(ShaderBufferLocation::PROBE_DATA);
        _probeDataDirty = false;
    }
}

} //namespace Divide