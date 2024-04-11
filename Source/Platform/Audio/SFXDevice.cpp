

#include "Headers/SFXDevice.h"

#include "Platform/Audio/fmod/Headers/FmodWrapper.h"
#include "Platform/Audio/sdl_mixer/Headers/SDLWrapper.h"
#include "Platform/Audio/openAl/Headers/ALWrapper.h"

#include "Utility/Headers/Localization.h"

namespace Divide {

SFXDevice::SFXDevice(PlatformContext& context)
    : AudioAPIWrapper("SFXDevice", context),
      _state(true, true, true, true),
      _api(nullptr)
{
    _playNextInPlaylist = false;
}

SFXDevice::~SFXDevice()
{
    closeAudioAPI();
}

ErrorCode SFXDevice::initAudioAPI() {
    assert(_api == nullptr && "SFXDevice error: initAudioAPI called twice!");

    switch (_apiID)
    {
        case AudioAPI::FMOD: {
            _api = std::make_unique<FMOD_API>(_context );
        } break;
        case AudioAPI::OpenAL: {
            _api = std::make_unique<OpenAL_API>( _context );
        } break;
        case AudioAPI::SDL: {
            _api = std::make_unique<SDL_API>( _context );
        } break;
        default: {
            Console::errorfn(LOCALE_STR("ERROR_SFX_DEVICE_API"));
            return ErrorCode::SFX_NON_SPECIFIED;
        }
    }

    return _api->initAudioAPI();
}

void SFXDevice::closeAudioAPI() {
    _musicPlaylists.clear();
    _currentPlaylist.second.clear();

    if (_api != nullptr) {
        _api->closeAudioAPI();
        _api.reset();
    }
}

void SFXDevice::idle() {
    NOP();
}

bool SFXDevice::frameStarted( [[maybe_unused]] const FrameEvent& evt )
{
    PROFILE_SCOPE_AUTO( Divide::Profiler::Category::Sound );

    if (_playNextInPlaylist) {
        _api->musicFinished();

        if (!_currentPlaylist.second.empty()) {
            _currentPlaylist.first = ++_currentPlaylist.first % _currentPlaylist.second.size();
            _api->playMusic(_currentPlaylist.second[_currentPlaylist.first]);
        }
        _playNextInPlaylist = false;
    }

    return true;
}

void SFXDevice::playSound(const AudioDescriptor_ptr& sound) {
    PROFILE_SCOPE_AUTO( Divide::Profiler::Category::Sound );

    DIVIDE_ASSERT(_api != nullptr, "SFXDevice error: playSound called without init!");

    _api->playSound(sound);
}

void SFXDevice::addMusic(const U32 playlistEntry, const AudioDescriptor_ptr& music) {
    auto& [crtPlaylistIndex, songs] = _musicPlaylists[playlistEntry];
    songs.push_back(music);
    crtPlaylistIndex = 0;
}

bool SFXDevice::playMusic(const U32 playlistEntry) {
    const MusicPlaylists::iterator it = _musicPlaylists.find(playlistEntry);
    if (it != std::cend(_musicPlaylists)) {
        return playMusic(it->second);
    }

    return false;
}

bool SFXDevice::playMusic(const MusicPlaylist& playlist) {
    PROFILE_SCOPE_AUTO( Divide::Profiler::Category::Sound );

    if (!playlist.second.empty()) {
        _currentPlaylist = playlist;
        _api->playMusic(_currentPlaylist.second[_currentPlaylist.first]);
        return true;
    }

    return false;
}

void SFXDevice::playMusic(const AudioDescriptor_ptr& music) {
    PROFILE_SCOPE_AUTO( Divide::Profiler::Category::Sound );

    _api->playMusic(music);
}

void SFXDevice::pauseMusic() {
    DIVIDE_ASSERT(_api != nullptr, "SFXDevice error: pauseMusic called without init!");

    _api->pauseMusic();
}

void SFXDevice::stopMusic() {
    DIVIDE_ASSERT(_api != nullptr, "SFXDevice error: stopMusic called without init!");

    _api->stopMusic();
}

void SFXDevice::stopAllSounds() {
    DIVIDE_ASSERT(_api != nullptr, "SFXDevice error: stopAllSounds called without init!");

    _api->stopAllSounds();
}

void SFXDevice::setMusicVolume(const I8 value) {
    DIVIDE_ASSERT(_api != nullptr, "SFXDevice error: setMusicVolume called without init!");

    _api->setMusicVolume(value);
}

void SFXDevice::setSoundVolume(const I8 value) {
    DIVIDE_ASSERT(_api != nullptr, "SFXDevice error: setSoundVolume called without init!");

    _api->setSoundVolume(value);
}

void SFXDevice::musicFinished() noexcept {
    _playNextInPlaylist = true;
}

void SFXDevice::dumpPlaylists() {
    _currentPlaylist = MusicPlaylist();
    _musicPlaylists.clear();
}

}; //namespace Divide
