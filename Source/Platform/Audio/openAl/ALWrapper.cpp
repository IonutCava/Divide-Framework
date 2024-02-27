

#include "Headers/ALWrapper.h"

#include <AL/al.h>
#include <AL/alc.h>

namespace Divide {

OpenAL_API::OpenAL_API( PlatformContext& context )
    : AudioAPIWrapper( "OpenAL", context )
{
}

ErrorCode OpenAL_API::initAudioAPI() noexcept {
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

void OpenAL_API::closeAudioAPI() noexcept {}

void OpenAL_API::playSound([[maybe_unused]] const AudioDescriptor_ptr& sound) noexcept {}

void OpenAL_API::playMusic([[maybe_unused]] const AudioDescriptor_ptr& music) noexcept {}

void OpenAL_API::pauseMusic() noexcept {}

void OpenAL_API::stopMusic() noexcept {}

void OpenAL_API::stopAllSounds() noexcept {}

void OpenAL_API::setMusicVolume([[maybe_unused]] I8 value) noexcept {}

void OpenAL_API::setSoundVolume([[maybe_unused]] I8 value) noexcept {}

void OpenAL_API::musicFinished() noexcept {}
};