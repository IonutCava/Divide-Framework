#include "stdafx.h"

#include "Headers/ShaderComputeQueue.h"

#include "Core/Time/Headers/ProfileTimer.h"
#include "Core/Resources/Headers/ResourceCache.h"

namespace Divide {

ShaderComputeQueue::ShaderComputeQueue(ResourceCache* cache)
    : _cache(cache),
      _queueComputeTimer(Time::ADD_TIMER("Shader Queue Timer"))
{
    std::atomic_init(&_maxShaderLoadsInFlight, 0u);
}

void ShaderComputeQueue::idle() {
    PROFILE_SCOPE();

    {
        SharedLock<SharedMutex> r_lock(_queueLock);
        if (_shaderComputeQueue.empty()) {
            return;
        }
    }

    Time::ScopedTimer timer(_queueComputeTimer);
    if (!stepQueue()) {
        NOP();
    }
}

// Processes a queue element on the spot
void ShaderComputeQueue::process(ShaderQueueElement& element) {
    ResourceDescriptor resDescriptor(element._shaderDescriptor._name);
    resDescriptor.propertyDescriptor(element._shaderDescriptor);
    resDescriptor.waitForReady(false);
    element._shaderRef = CreateResource<ShaderProgram>(_cache, resDescriptor, _maxShaderLoadsInFlight);
}

bool ShaderComputeQueue::stepQueue() {
    ScopedLock<SharedMutex> lock(_queueLock);
    return stepQueueLocked();
}

bool ShaderComputeQueue::stepQueueLocked() {
    constexpr U8 MAX_STEP_PER_FRAME = 15u;

    if (_shaderComputeQueue.empty()) {
        return false;
    }

    if (_maxShaderLoadsInFlight.load() >= MAX_STEP_PER_FRAME) {
        return false;
    }

    while (!_shaderComputeQueue.empty()) {
        process(_shaderComputeQueue.front());
        _shaderComputeQueue.pop_front();
    }
    return true;
}

void ShaderComputeQueue::addToQueueFront(const ShaderQueueElement& element) {
    ScopedLock<SharedMutex> w_lock(_queueLock);
    _shaderComputeQueue.push_front(element);
}

void ShaderComputeQueue::addToQueueBack(const ShaderQueueElement& element) {
    ScopedLock<SharedMutex> w_lock(_queueLock);
    _shaderComputeQueue.push_back(element);
}

}; //namespace Divide