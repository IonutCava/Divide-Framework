

#include "Headers/SDLWrapper.h"

#include "Core/Headers/PlatformContext.h"
#include "Utility/Headers/Localization.h"
#include "Platform/Audio/Headers/SFXDevice.h"
#include "Core/Resources/Headers/ResourceCache.h"

namespace Divide {

// Equal-power mapping: preserves perceived loudness at center
inline std::pair<F32, F32> panToStereoEqualPower(F32 pan) noexcept
{
    pan = std::max(-1.f, std::min(1.f, pan));
    const F32 angle = (pan + 1.f) * M_PI_DIV_4; // 0..pi/2
    F32 left = std::cos(angle);  // pan=-1 -> cos(0)=1, pan=0 -> cos(pi/4)=0.707, pan=1 -> cos(pi/2)=0
    F32 right = std::sin(angle); // pan=-1 -> 0, pan=0 -> 0.707, pan=1 -> 1
    return {CLAMPED_01(left), CLAMPED_01(right)};
}

template<typename Func, typename... Args>
FORCE_INLINE void SDL_CHECKED(Func&& func, Args&&... args)
{
    if (!func(std::forward<Args>(args)...))
    {
        Console::errorfn("{}", SDL_GetError());
    }
}

MIX_Audio* load_audio(MIX_Mixer* mixer, const char* path, const bool predecode)
{
    MIX_Audio* m = MIX_LoadAudio(mixer, path, predecode);
    if (m == nullptr)
    {
        Console::errorfn(LOCALE_STR("ERROR_SDL_LOAD_SOUND"), path, SDL_GetError());
    }

    return m;
}

SDL_API::SDL_API( PlatformContext& context )
    : AudioAPIWrapper("SDL", context)
{
    _activeSoundChannels.fill(nullptr);
}

ErrorCode SDL_API::initAudioAPI()
{
    if (MIX_Init())
    {
        SDL_AudioSpec spec
        {
            .format = SDL_AUDIO_S16,
            .channels = 2,
            .freq = 44100
        };

        _musicMixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
        _soundMixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
        if (_musicMixer == nullptr || _soundMixer == nullptr )
        {
            Console::errorfn("{}", SDL_GetError());
            return ErrorCode::SDL_AUDIO_MIX_INIT_ERROR;
        }
        _activeSongDetails._sfx = &_context.sfx();
        _activeSongDetails._isMusic = true;
        _activeSongProperties = SDL_CreateProperties();
        if (_activeSongProperties == 0)
        {
            Console::errorfn("{}", SDL_GetError());
            return ErrorCode::SDL_AUDIO_MIX_INIT_ERROR;
        }

        return ErrorCode::NO_ERR;
    }

    Console::errorfn("{}", SDL_GetError());
    return ErrorCode::SDL_AUDIO_INIT_ERROR;
}

void SDL_API::closeAudioAPI()
{
    stopMusic();
    stopAllSounds();

    if (_activeSong != nullptr && MIX_TrackPlaying(_activeSong))
    {
        MIX_DestroyTrack(_activeSong);
        if (_activeSongProperties != 0)
        {
            SDL_DestroyProperties(_activeSongProperties);
        }
        _activeSong = nullptr;
    }

    _activeSongDetails._sfx = nullptr;
    _activeSongDetails._trackID = 0u;
    for (SoundEntry* entry : _activeSoundChannels)
    {
        if (entry!= nullptr)
        {
            if (entry->_track != nullptr)
            {
                MIX_DestroyTrack(entry->_track);
            }
            if (entry->_audio != nullptr)
            {
                MIX_DestroyAudio(entry->_audio);
            }
            if (entry->_properties != 0)
            {
                SDL_DestroyProperties(entry->_properties);
            }
        }
    }
    _activeSoundChannels.fill(nullptr);
    _soundMap.clear();

    if (_soundMixer)
    {
        MIX_DestroyMixer(_soundMixer);
        _soundMixer = nullptr;
    }

    if (_musicMixer)
    {
        MIX_DestroyMixer(_musicMixer);
        _musicMixer = nullptr;
    }

    MIX_Quit();
}


void SDL_API::playMusic(const Handle<AudioDescriptor> music)
{
    if (music == INVALID_HANDLE<AudioDescriptor>)
    {
        DIVIDE_UNEXPECTED_CALL();
    }

    ResourcePtr<AudioDescriptor> musicPtr = Get(music);
    const char* musicPath = (musicPtr->assetLocation() / musicPtr->assetName()).string().c_str();

    MIX_Audio* mixMusicPtr = nullptr;
    const MusicMap::iterator it = _musicMap.find(music._data);
    if (it == std::cend(_musicMap))
    {
        mixMusicPtr = load_audio(_musicMixer, musicPath, false);
        insert(_musicMap, music._data, mixMusicPtr);
    }
    else
    {
        if ( musicPtr->dirty())
        {
            MIX_DestroyAudio(it->second);
            it->second = load_audio(_musicMixer, musicPath, false);
            musicPtr->clean();
        }
        else
        {
            mixMusicPtr = it->second;
        }
    }

    if( mixMusicPtr )
    {
        stopMusic();
        const F32 pan = CLAMPED(musicPtr->stereoGain(), -1.f, 1.f);
        auto [left, right] = panToStereoEqualPower(pan);
        _activeSongDetails._trackID = music._data;

        AudioPlaybackProperties playbackProps
        {
            ._details = &_activeSongDetails,
            ._mixer = _musicMixer,
            ._audio = mixMusicPtr,
            ._track = _activeSong,
            ._position = {
                musicPtr->position().x,
                musicPtr->position().y,
                musicPtr->position().z
            },
            ._stereoGains = {
                .left = left,
                .right = right
            },
            ._properties = _activeSongProperties,
            ._volume = musicPtr->volume() / 100.f,
            ._loop = musicPtr->isLooping(),
            ._is3D = !musicPtr->position().compare(VECTOR3_ZERO)
        };

        playAudio(playbackProps);
    }
    else
    {
        Console::errorfn(LOCALE_STR("ERROR_SDL_LOAD_SOUND"), Get( music )->resourceName().c_str());
    }
}

void SDL_API::playSound(const Handle<AudioDescriptor> sound)
{
    if (sound == INVALID_HANDLE<AudioDescriptor> )
    {
        DIVIDE_UNEXPECTED_CALL();
    }

    SoundEntry* targetSoundEntry = nullptr;
    for ( SoundEntry* entry : _activeSoundChannels)
    {
        if (entry == nullptr || !MIX_TrackPlaying(entry->_track))
        {
            targetSoundEntry = entry;
            break;
        }
    }

    if ( targetSoundEntry == nullptr)
    {
        Console::warnfn(LOCALE_STR("WARNING_SDL_MAX_SOUND_CHANNELS"));
        return;
    }

    ResourcePtr<AudioDescriptor> soundPtr = Get( sound );
    const char* soundPath = (soundPtr->assetLocation() / soundPtr->assetName()).string().c_str();

    SoundEntry soundEntry;
    const SoundMap::iterator it = _soundMap.find(sound._data);
    if (it == std::cend(_soundMap))
    {
        soundEntry._audio = load_audio(_soundMixer, soundPath, true);
        soundEntry._details._sfx = &_context.sfx();
        soundEntry._details._trackID = sound._data;
        soundEntry._properties = SDL_CreateProperties();
        if (soundEntry._properties == 0)
        {
            Console::errorfn("{}", SDL_GetError());
        }

        insert(_soundMap, sound._data, soundEntry);
    }
    else
    {
        if ( soundPtr->dirty())
        {
            MIX_DestroyAudio(it->second._audio);
            soundEntry._audio = load_audio(_soundMixer, soundPath, true);
            soundPtr->clean();
        }
        else 
        {
            soundEntry = it->second;
        }
    }

    if (soundEntry._audio)
    {
        const F32 pan = CLAMPED(soundPtr->stereoGain(), -1.f, 1.f);
        auto [left, right] = panToStereoEqualPower(pan);

        AudioPlaybackProperties playbackProps
        {
            ._details = &soundEntry._details,
            ._mixer = _soundMixer,
            ._audio = soundEntry._audio,
            ._track = soundEntry._track,
            ._position = {
                soundPtr->position().x,
                soundPtr->position().y,
                soundPtr->position().z
            },
            ._stereoGains = {
                .left = left,
                .right = right
            },
            ._properties = soundEntry._properties,
            ._volume = soundPtr->volume() / 100.f,
            ._loop = soundPtr->isLooping(),
            ._is3D = !soundPtr->position().compare(VECTOR3_ZERO)
        };
        if (playAudio(playbackProps) )
        {
            targetSoundEntry = &soundEntry;
        }
    }
    else
    {
        Console::errorfn(LOCALE_STR("ERROR_SDL_LOAD_SOUND"), soundPtr->resourceName().c_str());
    }
}

void SDL_API::pauseMusic() noexcept
{
    _paused = true;
    if (_activeSong != nullptr && MIX_TrackPlaying(_activeSong))
    {
        MIX_PauseTrack(_activeSong);
    }
}

void SDL_API::resumeMusic() noexcept
{
    _paused = false;
    if (_activeSong != nullptr && MIX_TrackPaused(_activeSong))
    {
        MIX_ResumeTrack(_activeSong);
    }
}

void SDL_API::stopMusic() noexcept
{
    if (_activeSong != nullptr && MIX_TrackPlaying(_activeSong))
    {
        MIX_StopTrack(_activeSong, 0);
    }
}

void SDL_API::stopAllSounds() noexcept
{
    for (SoundEntry* entry : _activeSoundChannels)
    {
        if (entry != nullptr && MIX_TrackPlaying(entry->_track))
        {
            MIX_StopTrack(entry->_track, 0);
        }
        entry = nullptr;
    }
}

void SDL_API::setMusicVolume(const I8 gain) noexcept
{
    _musicGain = CLAMPED<F32>(gain / 100.f, 0.f, 2.f);
    MIX_SetMasterGain(_musicMixer, _musicGain);
}

void SDL_API::setSoundVolume(const I8 gain) noexcept
{
    _soundGain = CLAMPED<F32>(gain / 100.f, 0.f, 2.f);
    MIX_SetMasterGain(_soundMixer, _soundGain);
}

void SDL_API::trackFinished( [[maybe_unused]] const TrackDetails& details) noexcept
{
}

bool SDL_API::playAudio(const AudioPlaybackProperties& playback) const
{
    SDL_CHECKED(MIX_SetTrackAudio, playback._track, playback._audio);

    SDL_CHECKED(MIX_SetTrackStoppedCallback,
                playback._track,
                [](void* userData, [[maybe_unused]] MIX_Track* track)
                {
                    auto details = static_cast<AudioAPIWrapper::TrackDetails*>(userData);
                    details->_sfx->trackFinished(*details);

                },
                playback._details);

    SDL_CHECKED(SDL_SetNumberProperty, playback._properties, MIX_PROP_PLAY_LOOPS_NUMBER, playback._loop ? -1 : 0);
    SDL_CHECKED(MIX_SetTrackGain, playback._track, playback._volume);
    SDL_CHECKED(MIX_SetTrack3DPosition, playback._track, playback._is3D ? &playback._position : nullptr);
    SDL_CHECKED(MIX_SetTrackGain, playback._track, playback._volume);
    if (!playback._is3D)
    {
        SDL_CHECKED(MIX_SetTrackStereo, playback._track, &playback._stereoGains);
    }

    if (!MIX_PlayTrack(playback._track, playback._properties))
    {
        Console::errorfn("{}", SDL_GetError());
        return false;
    }

    return true;
}
}; //namespace Divide
