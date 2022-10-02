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
        auto bindCmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        bindCmd->_usage = DescriptorSetUsage::PER_FRAME;
        auto& set = bindCmd->_bindings;

        if (_probeDataDirty) {
            _probeShaderData->incQueue();
            memBarrier._bufferLocks.push_back(_probeShaderData->writeData(_probeData.data()));

            auto& binding = set.emplace_back(ShaderStageVisibility::FRAGMENT);
            binding._slot = 7;
            binding._data.As<ShaderBufferEntry>() = { *_probeShaderData, { 0u, GLOBAL_PROBE_COUNT }};

            _probeDataDirty = false;
        }

        if ( _sceneDataDirty )
        {
            _sceneShaderData->incQueue();
            memBarrier._bufferLocks.push_back( _sceneShaderData->writeData( &_sceneBufferData ) );

            auto& binding = set.emplace_back( ShaderStageVisibility::ALL_DRAW );
            binding._slot = 8;
            binding._data.As<ShaderBufferEntry>() = { *_sceneShaderData, { 0u, 1u } };

            _sceneDataDirty = false;
        }
    }
    return memBarrier;
}

} //namespace Divide