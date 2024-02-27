

#include "Headers/SceneShaderData.h"
#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide
{
    SceneShaderData::SceneShaderData( GFXDevice& context )
        : _context( context )
    {
        ShaderBufferDescriptor bufferDescriptor = {};
        bufferDescriptor._ringBufferLength = Config::MAX_FRAMES_IN_FLIGHT + 1u;
        bufferDescriptor._bufferParams._flags._usageType = BufferUsageType::CONSTANT_BUFFER;
        bufferDescriptor._bufferParams._flags._updateUsage = BufferUpdateUsage::CPU_TO_GPU;
        bufferDescriptor._bufferParams._flags._updateFrequency = BufferUpdateFrequency::OCASSIONAL;
        {
            bufferDescriptor._bufferParams._elementCount = 1u;
            bufferDescriptor._bufferParams._elementSize = sizeof( SceneShaderBufferData );
            bufferDescriptor._initialData = { (Byte*)&_sceneBufferData, bufferDescriptor._bufferParams._elementSize };
            bufferDescriptor._name = "SCENE_SHADER_DATA";
            _sceneShaderData = _context.newSB( bufferDescriptor );
        }
        {
            bufferDescriptor._bufferParams._elementCount = GLOBAL_PROBE_COUNT;
            bufferDescriptor._bufferParams._elementSize = sizeof( ProbeData );
            bufferDescriptor._initialData = { (Byte*)_probeData.data(), bufferDescriptor._bufferParams._elementSize };
            bufferDescriptor._name = "SCENE_PROBE_DATA";
            _probeShaderData = _context.newSB( bufferDescriptor );
        }
    }

    void SceneShaderData::updateSceneDescriptorSet( GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( _probeDataDirty )
        {
            _probeShaderData->incQueue();
            memCmdInOut._bufferLocks.push_back( _probeShaderData->writeData( _probeData.data() ) );
            _probeDataDirty = false;
        }

        if ( _sceneDataDirty )
        {
            _sceneShaderData->incQueue();
            memCmdInOut._bufferLocks.push_back( _sceneShaderData->writeData( &_sceneBufferData ) );
            _sceneDataDirty = false;
        }

        auto bindCmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
        bindCmd->_usage = DescriptorSetUsage::PER_FRAME;
        DescriptorSet& set = bindCmd->_set;
        {
            DescriptorSetBinding& binding = AddBinding( set, 7u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, _probeShaderData.get(), {0u, GLOBAL_PROBE_COUNT});
        }
        {
            DescriptorSetBinding& binding = AddBinding( set, 8u, ShaderStageVisibility::ALL_DRAW );
            Set( binding._data, _sceneShaderData.get(), {0u, 1u});
        }
    }

} //namespace Divide