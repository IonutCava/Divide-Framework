#include "stdafx.h"

#include "Headers/Pipeline.h"

#include "Scenes/Headers/SceneShaderData.h"
#include "Geometry/Material/Headers/ShaderComputeQueue.h"

namespace Divide {
size_t GetHash( const PipelineDescriptor& descriptor )
{
    size_t hash = GetHash(descriptor._stateBlock);
    Util::Hash_combine( hash, descriptor._multiSampleCount,
                        descriptor._shaderProgramHandle._generation,
                        descriptor._shaderProgramHandle._id,
                        descriptor._primitiveTopology,
                        descriptor._alphaToCoverage );

    for ( U8 i = 0u; i < to_base( ShaderType::COUNT ); ++i )
    {
        Util::Hash_combine( hash, i );
    }

    Util::Hash_combine( hash, GetHash( descriptor._blendStates ) );
    Util::Hash_combine( hash, GetHash( descriptor._vertexFormat ) );

    return hash;
}

bool operator==(const PipelineDescriptor& lhs, const PipelineDescriptor& rhs) {
    return lhs._primitiveTopology == rhs._primitiveTopology &&
           lhs._multiSampleCount == rhs._multiSampleCount &&
           lhs._shaderProgramHandle == rhs._shaderProgramHandle &&
           lhs._blendStates == rhs._blendStates &&
           lhs._vertexFormat == rhs._vertexFormat &&
           lhs._stateBlock == rhs._stateBlock;
}

bool operator!=(const PipelineDescriptor& lhs, const PipelineDescriptor& rhs) {
    return lhs._primitiveTopology != rhs._primitiveTopology ||
           lhs._multiSampleCount != rhs._multiSampleCount ||
           lhs._shaderProgramHandle != rhs._shaderProgramHandle ||
           lhs._blendStates != rhs._blendStates ||
           lhs._vertexFormat != rhs._vertexFormat ||
           lhs._stateBlock != rhs._stateBlock;
}

namespace
{
    size_t GetFullStateHash( const Pipeline& pipeline, const size_t compiledStateHash, const size_t blendStateHash )
    {
        size_t hash = compiledStateHash;

        Util::Hash_combine( hash, blendStateHash ); //VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT + VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT + VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT

        const RenderStateBlock& block = pipeline.descriptor()._stateBlock;
        Util::Hash_combine( hash,
                            to_U32( std::floor( block._zBias * 1000.0f + 0.5f ) ),   //VK_DYNAMIC_STATE_DEPTH_BIAS
                            to_U32( std::floor( block._zUnits * 1000.0f + 0.5f ) ),  //VK_DYNAMIC_STATE_DEPTH_BIAS
                            to_U32( block._cullMode ),                               //VK_DYNAMIC_STATE_CULL_MODE
                            block._scissorTestEnabled,                               //VK_DYNAMIC_STATE_SCISSOR
                            block._frontFaceCCW,                                     //VK_DYNAMIC_STATE_FRONT_FACE 
                            block._stencilRef,                                       //VK_DYNAMIC_STATE_STENCIL_REFERENCE
                            block._stencilMask,                                      //VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK
                            block._stencilWriteMask,                                 //VK_DYNAMIC_STATE_STENCIL_WRITE_MASK
                            block._stencilEnabled,                                   //VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE
                            to_U32( block._stencilFailOp ),                          //VK_DYNAMIC_STATE_STENCIL_OP 
                            to_U32( block._stencilZFailOp ),                         //VK_DYNAMIC_STATE_STENCIL_OP 
                            to_U32( block._stencilPassOp ),                          //VK_DYNAMIC_STATE_STENCIL_OP 
                            to_U32( block._stencilFunc ),                            //VK_DYNAMIC_STATE_STENCIL_OP 
                            to_U32( block._zFunc ),                                  //VK_DYNAMIC_STATE_DEPTH_COMPARE_OP
                            block._rasterizationEnabled,                             //VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE
                            block._primitiveRestartEnabled,                          //VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE
                            block._depthWriteEnabled,                                //VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE
                            block._depthTestEnabled);                                //VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE 
        return hash;
    }

    size_t GetVulkanPipelineHash( const Pipeline& pipeline, const size_t vertexFormatHash )
    {
        const auto& descriptor = pipeline.descriptor();

        size_t hash = vertexFormatHash;
        for ( U8 i = 0u; i < to_base( ShaderType::COUNT ); ++i )
        {
            Util::Hash_combine( hash, i );
        }

        Util::Hash_combine( hash,
                            descriptor._multiSampleCount,
                            descriptor._shaderProgramHandle._generation,
                            descriptor._shaderProgramHandle._id,
                            descriptor._primitiveTopology,
                            descriptor._alphaToCoverage);

        const RenderStateBlock& block = descriptor._stateBlock;
        Util::Hash_combine( hash,
                            //to_U32( std::floor( block._zBias * 1000.0f + 0.5f ) ),   //VK_DYNAMIC_STATE_DEPTH_BIAS
                            //to_U32( std::floor( block._zUnits * 1000.0f + 0.5f ) ),  //VK_DYNAMIC_STATE_DEPTH_BIAS
                            //to_U32( block._cullMode ),                               //VK_DYNAMIC_STATE_CULL_MODE
                            //block._frontFaceCCW,                                     //VK_DYNAMIC_STATE_FRONT_FACE 
                            //block._depthTestEnabled,                                 //VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE 
                            //block._depthWriteEnabled,                                //VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE
                            //to_U32( block._zFunc ),                                  //VK_DYNAMIC_STATE_DEPTH_COMPARE_OP
                            //block._scissorTestEnabled,                               //VK_DYNAMIC_STATE_SCISSOR
                            //block._stencilEnabled,                                   //VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE
                            //block._stencilRef,                                       //VK_DYNAMIC_STATE_STENCIL_REFERENCE
                            //block._stencilMask,                                      //VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK
                            //block._stencilWriteMask,                                 //VK_DYNAMIC_STATE_STENCIL_WRITE_MASK
                            //to_U32( block._stencilFailOp ),                          //VK_DYNAMIC_STATE_STENCIL_OP 
                            //to_U32( block._stencilZFailOp ),                         //VK_DYNAMIC_STATE_STENCIL_OP 
                            //to_U32( block._stencilPassOp ),                          //VK_DYNAMIC_STATE_STENCIL_OP 
                            //to_U32( block._stencilFunc ),                            //VK_DYNAMIC_STATE_STENCIL_OP 
                            //block._rasterizationEnabled,                             //VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE
                            //block._primitiveRestartEnabled,                          //VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE
                            block._colourWrite.i,
                            to_U32( block._fillMode ),
                            block._tessControlPoints );

        return hash;
    }
};

Pipeline::Pipeline(const PipelineDescriptor& descriptor )
    : _descriptor(descriptor)
    , _blendStateHash( GetHash( descriptor._blendStates ) )
{
    _vertexFormatHash = GetHash( descriptor._vertexFormat );
    _compiledPipelineHash = GetVulkanPipelineHash(*this, _vertexFormatHash );
    _stateHash = GetFullStateHash(*this, _compiledPipelineHash, _blendStateHash );
}

bool operator==(const Pipeline& lhs, const Pipeline& rhs) noexcept {
    return lhs.stateHash() == rhs.stateHash();
}

bool operator!=(const Pipeline& lhs, const Pipeline& rhs) noexcept {
    return lhs.stateHash() != rhs.stateHash();
}
}; //namespace Divide