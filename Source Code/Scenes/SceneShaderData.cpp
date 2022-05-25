#include "stdafx.h"

#include "Headers/SceneShaderData.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/CommandBuffer.h"

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
        bufferDescriptor._initialData = { (Byte*)&_sceneBufferData, bufferDescriptor._bufferParams._elementSize };
        bufferDescriptor._name = "SCENE_SHADER_DATA";
        _sceneShaderData = _context.newSB(bufferDescriptor);
    }
    {
        bufferDescriptor._bufferParams._elementCount = GLOBAL_PROBE_COUNT;
        bufferDescriptor._bufferParams._elementSize = sizeof(ProbeData);
        bufferDescriptor._initialData = { (Byte*)_probeData.data(), bufferDescriptor._bufferParams._elementSize };
        bufferDescriptor._name = "SCENE_PROBE_DATA";
        _probeShaderData = _context.newSB(bufferDescriptor);
    }
}

GFX::MemoryBarrierCommand SceneShaderData::updateSceneDescriptorSet(GFX::CommandBuffer& bufferInOut) {
    GFX::MemoryBarrierCommand memBarrier{};
    if (_sceneDataDirty || _probeDataDirty) {
        DescriptorSet& set = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set;
        set._usage = DescriptorSetUsage::PER_FRAME_SET;

        if (_sceneDataDirty) {
            _sceneShaderData->incQueue();
            memBarrier._bufferLocks.push_back(_sceneShaderData->writeData(&_sceneBufferData));

            auto& binding = set._bindings.emplace_back();
            binding._resourceSlot = to_base(ShaderBufferLocation::SCENE_DATA);
            binding._shaderStageVisibility = DescriptorSetBinding::ShaderStageVisibility::ALL_DRAW;
            binding._type = DescriptorSetBindingType::UNIFORM_BUFFER;
            binding._data.As<ShaderBufferEntry>() = { _sceneShaderData.get(), { 0u, 1u } };

            _sceneDataDirty = false;
        }

        if (_probeDataDirty) {
            _probeShaderData->incQueue();
            memBarrier._bufferLocks.push_back(_probeShaderData->writeData(_probeData.data()));

            auto& binding = set._bindings.emplace_back();
            binding._resourceSlot = to_base(ShaderBufferLocation::PROBE_DATA);
            binding._shaderStageVisibility = DescriptorSetBinding::ShaderStageVisibility::COMPUTE_AND_DRAW;
            binding._type = DescriptorSetBindingType::UNIFORM_BUFFER;
            binding._data.As<ShaderBufferEntry>() = { _probeShaderData.get(), { 0u, GLOBAL_PROBE_COUNT } };

            _probeDataDirty = false;
        }
    }
    return memBarrier;
}

} //namespace Divide