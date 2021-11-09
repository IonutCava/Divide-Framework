#include "stdafx.h"

#include "Headers/ALWrapper.h"

#include <al.h>
#include <alc.h>

namespace Divide {

ErrorCode OpenAL_API::initAudioAPI([[maybe_unused]] PlatformContext& context) {
    // Initialization
    ALCdevice* device = alcOpenDevice(nullptr);  // select the "preferred device"
    if (device) {
        ALCcontext* alContext = alcCreateContext(device, nullptr);
        alcMakeContextCurrent(alContext);
    }
    // Check for EAX 2.0 support
    // ALboolean g_bEAX = alIsExtensionPresent("EAX2.0");
    // Generate Buffers
    alGetError();  // clear error code
    alGenBuffers(MAX_SOUND_BUFFERS, buffers);
    const ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        return ErrorCode::OAL_INIT_ERROR;
    }
    // Clear Error Code (so we can catch any new errors)
    alGetError();
    return ErrorCode::OAL_INIT_ERROR;
}

void OpenAL_API::beginFrame() {}

void OpenAL_API::endFrame() {}

void OpenAL_API::closeAudioAPI() {}

void OpenAL_API::playSound([[maybe_unused]] const AudioDescriptor_ptr& sound) {}

void OpenAL_API::playMusic([[maybe_unused]] const AudioDescriptor_ptr& music) {}

void OpenAL_API::pauseMusic() {}

void OpenAL_API::stopMusic() {}

void OpenAL_API::stopAllSounds() {}

void OpenAL_API::setMusicVolume([[maybe_unused]] I8 value) {}

void OpenAL_API::setSoundVolume([[maybe_unused]] I8 value) {}

void OpenAL_API::musicFinished() {}
};