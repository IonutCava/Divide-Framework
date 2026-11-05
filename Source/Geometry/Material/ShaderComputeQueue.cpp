// Copyright (c) 2026 DIVIDE-Studio

#include "Headers/ShaderComputeQueue.h"

#include "Core/Time/Headers/ProfileTimer.h"
#include "Core/Resources/Headers/ResourceCache.h"

namespace Divide
{

    ShaderComputeQueue::ShaderComputeQueue()
        : _queueComputeTimer( Time::ADD_TIMER( "Shader Queue Timer" ) )
    {
    }

    void ShaderComputeQueue::idle()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Streaming );

        {
            SharedLock<SharedMutex> r_lock( _queueLock );
            if ( _shaderComputeQueue.empty() )
            {
                return;
            }
        }

        Time::ScopedTimer timer( _queueComputeTimer );
        if ( !stepQueue() )
        {
            NOP();
        }
    }

    // Processes a queue element on the spot
    void ShaderComputeQueue::process( ShaderQueueElement& element )
    {
        LockGuard<SharedMutex> lock( _queueLock );
        if ( element._shaderInfo->_shaderQueued.load( std::memory_order_relaxed ) )
        {
            cancelLocked( *element._shaderInfo );
        }
        processLocked( element );
    }

    void ShaderComputeQueue::processLocked( ShaderQueueElement& element )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Streaming );
        ResourceDescriptor<ShaderProgram> resDescriptor( element._shaderDescriptor._name );
        resDescriptor._propertyDescriptor = element._shaderDescriptor;
        resDescriptor.waitForReady( false );
        const Handle<ShaderProgram> shader = CreateResource( resDescriptor, _maxShaderLoadsInFlight );
        DestroyResource( element._shaderInfo->_shaderRef );
        element._shaderInfo->_shaderRef = shader;
        element._shaderInfo->_shaderQueued.store( false, std::memory_order_release );
    }

    bool ShaderComputeQueue::stepQueue()
    {
        LockGuard<SharedMutex> lock( _queueLock );
        return stepQueueLocked();
    }

    bool ShaderComputeQueue::stepQueueLocked()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Streaming );

        constexpr U8 MAX_SHADER_LOADS_IN_FLIGHT = 15u;
        bool processed = false;
        while ( !_shaderComputeQueue.empty() &&
                _maxShaderLoadsInFlight.load( std::memory_order_relaxed ) < MAX_SHADER_LOADS_IN_FLIGHT )
        {
            processLocked( _shaderComputeQueue.front() );
            _shaderComputeQueue.pop_front();
            processed = true;
        }
        return processed;
    }

    void ShaderComputeQueue::addToQueueFront( const ShaderQueueElement& element )
    {
        LockGuard<SharedMutex> w_lock( _queueLock );
        if ( element._shaderInfo->_shaderQueued.load( std::memory_order_relaxed ) )
        {
            cancelLocked( *element._shaderInfo );
        }
        _shaderComputeQueue.push_front( element );
        element._shaderInfo->_shaderQueued.store( true, std::memory_order_release );
    }

    void ShaderComputeQueue::addToQueueBack( const ShaderQueueElement& element )
    {
        LockGuard<SharedMutex> w_lock( _queueLock );
        if ( element._shaderInfo->_shaderQueued.load( std::memory_order_relaxed ) )
        {
            cancelLocked( *element._shaderInfo );
        }
        _shaderComputeQueue.push_back( element );
        element._shaderInfo->_shaderQueued.store( true, std::memory_order_release );
    }

    void ShaderComputeQueue::cancel( ShaderProgramInfo& shaderInfo )
    {
        LockGuard<SharedMutex> w_lock( _queueLock );
        cancelLocked( shaderInfo );
    }

    void ShaderComputeQueue::cancelLocked( ShaderProgramInfo& shaderInfo )
    {
        std::erase_if( _shaderComputeQueue,
                       [&shaderInfo]( const ShaderQueueElement& element ) noexcept
                       {
                           return element._shaderInfo == &shaderInfo;
                       } );
        shaderInfo._shaderQueued.store( false, std::memory_order_release );
    }

    void ShaderComputeQueue::waitForPendingLoads() const
    {
        WAIT_FOR_CONDITION( _maxShaderLoadsInFlight.load() == 0u );
    }

} //namespace Divide
