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
    , _shaderCompStage( other._shaderCompStage.load( std::memory_order_acquire ) )
    , _readyNotificationPending( other._readyNotificationPending.load( std::memory_order_relaxed ) )
    , _hasCommonMaterialInputs( other._hasCommonMaterialInputs.load( std::memory_order_relaxed ) )
{
    DIVIDE_ASSERT( !other._shaderQueued.load() );
    other._shaderKeyCache = I64_LOWEST;
    other._shaderRef = INVALID_HANDLE<ShaderProgram>;
    other._shaderCompStage.store( ShaderBuildStage::COUNT, std::memory_order_release );
    other._readyNotificationPending.store( false, std::memory_order_relaxed );
    other._hasCommonMaterialInputs.store( false, std::memory_order_relaxed );
}

ShaderProgramInfo& ShaderProgramInfo::operator=( ShaderProgramInfo&& other ) noexcept
{
    if ( this != &other )
    {
        DIVIDE_ASSERT( !_shaderQueued.load() && !other._shaderQueued.load() );
        DestroyResource( _shaderRef );
        _shaderKeyCache = other._shaderKeyCache;
        _shaderRef = other._shaderRef;
        _readyNotificationPending.store( other._readyNotificationPending.load( std::memory_order_relaxed ), std::memory_order_relaxed );
        _hasCommonMaterialInputs.store( other._hasCommonMaterialInputs.load( std::memory_order_relaxed ), std::memory_order_relaxed );
        _shaderCompStage.store( other._shaderCompStage.load( std::memory_order_acquire ), std::memory_order_release );
        other._shaderKeyCache = I64_LOWEST;
        other._shaderRef = INVALID_HANDLE<ShaderProgram>;
        other._shaderCompStage.store( ShaderBuildStage::COUNT, std::memory_order_release );
        other._readyNotificationPending.store( false, std::memory_order_relaxed );
        other._hasCommonMaterialInputs.store( false, std::memory_order_relaxed );
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
    const ShaderBuildStage stage = _shaderCompStage.load( std::memory_order_acquire );
    if ( stage == ShaderBuildStage::COUNT || stage == ShaderBuildStage::FAILED )
    {
        ret._shaderCompStage.store( stage, std::memory_order_relaxed );
        return ret;
    }

    if ( stage != ShaderBuildStage::READY ||
         _shaderQueued.load( std::memory_order_acquire ) ||
         ResourceCache::Inspect( _shaderRef )._state != ResourceState::RES_LOADED )
    {
        ret._shaderCompStage.store( ShaderBuildStage::REQUESTED, std::memory_order_relaxed );
        return ret;
    }

    ret._shaderKeyCache = _shaderKeyCache;
    if ( _shaderRef != INVALID_HANDLE<ShaderProgram> )
    {
        ret._shaderRef = GetResourceRef( _shaderRef );
    }
    ret._hasCommonMaterialInputs.store( _hasCommonMaterialInputs.load( std::memory_order_relaxed ), std::memory_order_relaxed );
    ret._readyNotificationPending.store( true, std::memory_order_relaxed );
    ret._shaderCompStage.store( ShaderBuildStage::READY, std::memory_order_release );

    return ret;
}

ShaderResourceStatus ShaderProgramInfo::updateReadiness( const ResourceState state, const I64 guid ) noexcept
{
    const ShaderResourceStatus status = ClassifyShaderResource( state );
    if ( status == ShaderResourceStatus::READY )
    {
        _shaderKeyCache = guid;
        _readyNotificationPending.store( true, std::memory_order_relaxed );
        _shaderCompStage.store( ShaderBuildStage::READY, std::memory_order_release );
    }
    else if ( status == ShaderResourceStatus::FAILED )
    {
        _shaderKeyCache = I64_LOWEST;
        _readyNotificationPending.store( false, std::memory_order_relaxed );
        _shaderCompStage.store( ShaderBuildStage::FAILED, std::memory_order_release );
    }
    return status;
}

bool ShaderProgramInfo::takeReadyNotification() noexcept
{
    // Avoid an exclusive cache-line RMW on every steady-state frame
    if ( !_readyNotificationPending.load( std::memory_order_relaxed ) )
    {
        return false;
    }
    return _readyNotificationPending.exchange( false, std::memory_order_acq_rel );
}

} //namespace Divide
