// Copyright (c) 2026 DIVIDE-Studio

#include "Headers/ShaderProgramInfo.h"
#include "Core/Resources/Headers/ResourceCache.h"

namespace Divide
{

ShaderProgramInfo::ShaderProgramInfo( ShaderProgramInfo&& other ) noexcept
    : _shaderKeyCache( other._shaderKeyCache )
    , _shaderRef( other._shaderRef )
    , _shaderCompStage( other._shaderCompStage )
{
    DIVIDE_ASSERT( !other._shaderQueued.load() );
    other._shaderKeyCache = I64_LOWEST;
    other._shaderRef = INVALID_HANDLE<ShaderProgram>;
    other._shaderCompStage = ShaderBuildStage::COUNT;
}

ShaderProgramInfo& ShaderProgramInfo::operator=( ShaderProgramInfo&& other ) noexcept
{
    if ( this != &other )
    {
        DIVIDE_ASSERT( !_shaderQueued.load() && !other._shaderQueued.load() );
        DestroyResource( _shaderRef );
        _shaderKeyCache = other._shaderKeyCache;
        _shaderRef = other._shaderRef;
        _shaderCompStage = other._shaderCompStage;
        other._shaderKeyCache = I64_LOWEST;
        other._shaderRef = INVALID_HANDLE<ShaderProgram>;
        other._shaderCompStage = ShaderBuildStage::COUNT;
    }

    return *this;
}

ShaderProgramInfo::~ShaderProgramInfo()
{
    DestroyResource( _shaderRef );
}

ShaderProgramInfo ShaderProgramInfo::clone() const
{
    ShaderProgramInfo ret = {};
    if ( _shaderCompStage == ShaderBuildStage::QUEUED || _shaderQueued.load( std::memory_order_acquire ) )
    {
        ret._shaderCompStage = ShaderBuildStage::REQUESTED;
        return ret;
    }

    ret._shaderKeyCache = _shaderKeyCache;
    ret._shaderCompStage = _shaderCompStage;
    if ( _shaderRef != INVALID_HANDLE<ShaderProgram> )
    {
        ret._shaderRef = GetResourceRef( _shaderRef );
    }

    return ret;
}

} //namespace Divide
