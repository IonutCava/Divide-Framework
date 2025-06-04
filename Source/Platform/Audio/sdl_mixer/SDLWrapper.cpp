

#include "Headers/SDLWrapper.h"

#include "Core/Headers/PlatformContext.h"
#include "Utility/Headers/Localization.h"
#include "Platform/Audio/Headers/SFXDevice.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include <SDL3_mixer/SDL_mixer.h>

namespace Divide {

namespace
{
    SFXDevice* g_sfxDevice = nullptr;
};

void musicFinishedHook() noexcept
{
    if (g_sfxDevice)
    {
        g_sfxDevice->musicFinished();
    }
}

SDL_API::SDL_API( PlatformContext& context )
    : AudioAPIWrapper("SDL", context)
{
}

ErrorCode SDL_API::initAudioAPI()
{
    constexpr I32 flags = MIX_INIT_WAVPACK | MIX_INIT_OGG | MIX_INIT_MP3;// | MIX_INIT_FLAC | MIX_INIT_MOD;

    const I32 ret = Mix_Init(flags);
    if ((ret & flags) == flags)
    {
        SDL_AudioSpec spec
        {
            .format = MIX_DEFAULT_FORMAT,
            .channels = MIX_DEFAULT_CHANNELS,
            .freq = MIX_DEFAULT_FREQUENCY
        };

        if ( !Mix_OpenAudio(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec) )
        {
            Console::errorfn("{}", SDL_GetError());
            return ErrorCode::SDL_AUDIO_MIX_INIT_ERROR;
        }

        g_sfxDevice = &_context.sfx();
        Mix_HookMusicFinished(musicFinishedHook);
        return ErrorCode::NO_ERR;
    }

    Console::errorfn("{}", SDL_GetError());
    return ErrorCode::SDL_AUDIO_INIT_ERROR;
}

void SDL_API::closeAudioAPI()
{
    Mix_HaltMusic();
    for (const MusicMap::value_type& it : _musicMap)
    {
        Mix_FreeMusic(it.second);
    }
    for (const SoundMap::value_type& it : _soundMap)
    {
        Mix_FreeChunk(it.second);
    }
    Mix_CloseAudio();
    Mix_Quit();
    g_sfxDevice = nullptr;
}

void SDL_API::stopMusic() noexcept
{
    Mix_HaltMusic(); 
}

void SDL_API::musicFinished() noexcept
{
}

void SDL_API::playMusic(const Handle<AudioDescriptor> music)
{
    if (music != INVALID_HANDLE<AudioDescriptor>)
    {
        ResourcePtr<AudioDescriptor> musicPtr = Get(music);

        Mix_Music* mixMusicPtr = nullptr;
        const MusicMap::iterator it = _musicMap.find(music._data);
        if (it == std::cend(_musicMap))
        {
            mixMusicPtr = Mix_LoadMUS((musicPtr->assetLocation() / musicPtr->assetName()).string().c_str() );
            insert(_musicMap, music._data, mixMusicPtr);
        }
        else
        {

            if ( musicPtr->dirty())
            {
                mixMusicPtr = Mix_LoadMUS( (musicPtr->assetLocation() / musicPtr->assetName()).string().c_str() );
                Mix_FreeMusic(it->second);
                it->second = mixMusicPtr;
                musicPtr->clean();
            }
            else
            {
                mixMusicPtr = it->second;
            }
        }
        
        if( mixMusicPtr )
        {
            Mix_VolumeMusic( musicPtr->volume());
            if ( !Mix_PlayMusic( mixMusicPtr, musicPtr->isLooping() ? -1 : 0) )
            {
                Console::errorfn("{}", SDL_GetError());
            }
        }
        else
        {
            Console::errorfn(LOCALE_STR("ERROR_SDL_LOAD_SOUND"), Get( music )->resourceName().c_str());
        }
    }
}

void SDL_API::playSound(const Handle<AudioDescriptor> sound)
{
    if (sound != INVALID_HANDLE<AudioDescriptor> )
    {
        ResourcePtr<AudioDescriptor> soundPtr = Get( sound );

        Mix_Chunk* mixSoundPtr = nullptr;
        const SoundMap::iterator it = _soundMap.find(sound._data);
        if (it == std::cend(_soundMap))
        {
            mixSoundPtr = Mix_LoadWAV( (soundPtr->assetLocation() / soundPtr->assetName()).string().c_str() );
            insert(_soundMap, sound._data, mixSoundPtr );
        }
        else
        {

            if ( soundPtr->dirty())
            {
                mixSoundPtr = Mix_LoadWAV( (soundPtr->assetLocation() / soundPtr->assetName()).string().c_str() );
                Mix_FreeChunk(it->second);
                it->second = mixSoundPtr;
                soundPtr->clean();
            }
            else 
            {
                mixSoundPtr = it->second;
            }
        }

        if ( mixSoundPtr )
        {
            Mix_Volume( soundPtr->channelID(), soundPtr->volume());
            if (!Mix_PlayChannel( soundPtr->channelID(), mixSoundPtr, soundPtr->isLooping() ? -1 : 0))
            {
                Console::errorfn(LOCALE_STR("ERROR_SDL_CANT_PLAY"), soundPtr->resourceName().c_str(), SDL_GetError());
            }
        }
        else
        {
            Console::errorfn(LOCALE_STR("ERROR_SDL_LOAD_SOUND"), soundPtr->resourceName().c_str());
        }
    }   
}

}; //namespace Divide
