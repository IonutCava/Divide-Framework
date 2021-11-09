#include "stdafx.h"

#include "Headers/ShaderComputeQueue.h"

#include "Core/Time/Headers/ProfileTimer.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

namespace Divide {

ShaderComputeQueue::ShaderComputeQueue(ResourceCache* cache)
    : _cache(cache),
      _queueComputeTimer(Time::ADD_TIMER("Shader Queue Timer"))
{
}

void ShaderComputeQueue::idle() {
    OPTICK_EVENT();

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
void ShaderComputeQueue::process(ShaderQueueElement& element) const {
    element._shaderDescriptor.waitForReady(false);
    element._shaderRef = CreateResource<ShaderProgram>(_cache, element._shaderDescriptor);
}

bool ShaderComputeQueue::stepQueue() {
    ScopedLock<SharedMutex> lock(_queueLock);
    return stepQueueLocked();
}

bool ShaderComputeQueue::stepQueueLocked() {
    constexpr U8 MAX_STEP_PER_FRAME = 50u;

    if (_shaderComputeQueue.empty()) {
        return false;
    }

    U8 count = 0u;
    while (!_shaderComputeQueue.empty()) {
        process(_shaderComputeQueue.front());
        _shaderComputeQueue.pop_front();
        if (++count == MAX_STEP_PER_FRAME) {
            break;
        }
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