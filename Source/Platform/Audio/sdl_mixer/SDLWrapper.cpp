

#include "Headers/SDLWrapper.h"

#include "Core/Headers/PlatformContext.h"
#include "Utility/Headers/Localization.h"
#include "Platform/Audio/Headers/SFXDevice.h"

#include <SDL2/SDL_mixer.h>

namespace Divide {

namespace {
    SFXDevice* g_sfxDevice = nullptr;
};

void musicFinishedHook() noexcept {
    if (g_sfxDevice) {
        g_sfxDevice->musicFinished();
    }
}

SDL_API::SDL_API( PlatformContext& context )
    : AudioAPIWrapper("SDL", context)
{
}

ErrorCode SDL_API::initAudioAPI() {

    constexpr I32 flags = MIX_INIT_OGG | MIX_INIT_MP3 | MIX_INIT_FLAC/* | MIX_INIT_MOD*/;

    const I32 ret = Mix_Init(flags);
    if ((ret & flags) == flags) {
        // Try HiFi sound
        if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, MIX_DEFAULT_CHANNELS, 4096) == -1) {
            Console::errorfn("%s", Mix_GetError());
            // Try lower quality
            if (Mix_OpenAudio(MIX_DEFAULT_FREQUENCY, MIX_DEFAULT_FORMAT, MIX_DEFAULT_CHANNELS, 2048) == -1) {
                Console::errorfn("%s", Mix_GetError());
                return ErrorCode::SDL_AUDIO_MIX_INIT_ERROR;
            }
        }

        g_sfxDevice = &_context.sfx();
        Mix_HookMusicFinished(musicFinishedHook);
        return ErrorCode::NO_ERR;
    }
    Console::errorfn("%s", Mix_GetError());
    return ErrorCode::SDL_AUDIO_INIT_ERROR;
}

void SDL_API::closeAudioAPI() {
    Mix_HaltMusic();
    for (const MusicMap::value_type& it : _musicMap) {
        Mix_FreeMusic(it.second);
    }
    for (const SoundMap::value_type& it : _soundMap) {
        Mix_FreeChunk(it.second);
    }
    Mix_CloseAudio();
    Mix_Quit();
    g_sfxDevice = nullptr;
}

void SDL_API::stopMusic() noexcept { 
    Mix_HaltMusic(); 
}

void SDL_API::musicFinished() noexcept {
}


void SDL_API::playMusic(const AudioDescriptor_ptr& music) {
    if (music) {
        Mix_Music* musicPtr = nullptr;
        const MusicMap::iterator it = _musicMap.find(music->getGUID());
        if (it == std::cend(_musicMap)) {
            musicPtr = Mix_LoadMUS(music->assetPath().c_str());
            insert(_musicMap, music->getGUID(), musicPtr);
        } else {
            if (music->dirty()) {
                musicPtr = Mix_LoadMUS(music->assetPath().c_str());
                Mix_FreeMusic(it->second);
                it->second = musicPtr;
                music->clean();
            } else {
                musicPtr = it->second;
            }
        }
        
        if(musicPtr) {
            Mix_VolumeMusic(music->volume());
            if (Mix_PlayMusic(musicPtr, music->isLooping() ? -1 : 0) == -1) {
                Console::errorfn("%s", Mix_GetError());
            }
        } else {
            Console::errorfn(Locale::Get(_ID("ERROR_SDL_LOAD_SOUND")), music->resourceName().c_str());
        }
    }
}

void SDL_API::playSound(const AudioDescriptor_ptr& sound) {
    if (sound) {
        Mix_Chunk* soundPtr = nullptr;
        const SoundMap::iterator it = _soundMap.find(sound->getGUID());
        if (it == std::cend(_soundMap)) {
            soundPtr = Mix_LoadWAV(sound->assetPath().c_str());
            insert(_soundMap, sound->getGUID(), soundPtr);
        } else {
            if (sound->dirty()) {
                soundPtr = Mix_LoadWAV(sound->assetPath().c_str());
                Mix_FreeChunk(it->second);
                it->second = soundPtr;
                sound->clean();
            } else {
                soundPtr = it->second;
            }
        }

        if (soundPtr) {
            Mix_Volume(sound->channelID(), sound->volume());
            if (Mix_PlayChannel(sound->channelID(), soundPtr, sound->isLooping() ? -1 : 0) == -1) {
                Console::errorfn(Locale::Get(_ID("ERROR_SDL_CANT_PLAY")), sound->resourceName().c_str(), Mix_GetError());
            }
        } else {
            Console::errorfn(Locale::Get(_ID("ERROR_SDL_LOAD_SOUND")), sound->resourceName().c_str());
        }
    }   
}

};