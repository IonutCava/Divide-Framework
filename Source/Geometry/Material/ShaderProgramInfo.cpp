// Copyright (c) 2026 DIVIDE-Studio

#include "Headers/ShaderProgramInfo.h"
#include "Core/Resources/Headers/ResourceCache.h"

namespace Divide
{

ShaderResourceStatus ClassifyShaderResource( const ResourceState state ) noexcept
{
    switch ( state )
    {
        case ResourceState::RES_CREATED:
        case ResourceState::RES_LOADING:
        case ResourceState::RES_THREAD_LOADED:
            return ShaderResourceStatus::PENDING;
        case ResourceState::RES_LOADED:
            return ShaderResourceStatus::READY;
        default:
            return ShaderResourceStatus::FAILED;
    }
}

ShaderProgramInfo::ShaderProgramInfo( ShaderProgramInfo&& other ) noexcept
    : _shaderKeyCache( other._shaderKeyCache )
    , _shaderRef( other._shaderRef )
    , _shaderCompStage( other._shaderCompStage )
    , _readyNotificationPending( other._readyNotificationPending )
{
    DIVIDE_ASSERT( !other._shaderQueued.load() );
    other._shaderKeyCache = I64_LOWEST;
    other._shaderRef = INVALID_HANDLE<ShaderProgram>;
    other._shaderCompStage = ShaderBuildStage::COUNT;
    other._readyNotificationPending = false;
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
        _readyNotificationPending = other._readyNotificationPending;
        other._shaderKeyCache = I64_LOWEST;
        other._shaderRef = INVALID_HANDLE<ShaderProgram>;
        other._shaderCompStage = ShaderBuildStage::COUNT;
        other._readyNotificationPending = false;
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
    if ( _shaderCompStage == ShaderBuildStage::COUNT || _shaderCompStage == ShaderBuildStage::FAILED )
    {
        ret._shaderCompStage = _shaderCompStage;
        return ret;
    }

    if ( _shaderCompStage != ShaderBuildStage::READY ||
         _shaderQueued.load( std::memory_order_acquire ) ||
         ResourceCache::Inspect( _shaderRef )._state != ResourceState::RES_LOADED )
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
    ret._readyNotificationPending = true;

    return ret;
}

ShaderResourceStatus ShaderProgramInfo::updateReadiness( const ResourceState state, const I64 guid ) noexcept
{
    const ShaderResourceStatus status = ClassifyShaderResource( state );
    if ( status == ShaderResourceStatus::READY )
    {
        _shaderCompStage = ShaderBuildStage::READY;
        _shaderKeyCache = guid;
        _readyNotificationPending = true;
    }
    else if ( status == ShaderResourceStatus::FAILED )
    {
        _shaderCompStage = ShaderBuildStage::FAILED;
        _shaderKeyCache = I64_LOWEST;
        _readyNotificationPending = false;
    }
    return status;
}

bool ShaderProgramInfo::takeReadyNotification() noexcept
{
    return std::exchange( _readyNotificationPending, false );
}

} //namespace Divide
