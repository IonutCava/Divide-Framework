/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef DVD_SFX_DEVICE_H
#define DVD_SFX_DEVICE_H

#include "AudioAPIWrapper.h"
#include "Core/Headers/KernelComponent.h"

namespace Divide {

class SFXDevice final : public AudioAPIWrapper {
public:
    enum class AudioAPI : U8 {
        FMOD,
        OpenAL,
        SDL,
        COUNT
    };

    explicit SFXDevice( PlatformContext& context );

    ~SFXDevice() override;

    [[nodiscard]] ErrorCode initAudioAPI() override;
    void closeAudioAPI() override;

    void setAudioState(const AudioState& state) noexcept { _state = state; }
    [[nodiscard]] AudioState& getActiveAudioState() noexcept { return _state; }

    void idle();
    void playSound( Handle<AudioDescriptor> sound ) override;
    void playMusic( Handle<AudioDescriptor> music ) override;

    void pauseMusic() override;
    void resumeMusic() override;
    void stopMusic() override;
    void stopAllSounds() override;
    void setMusicVolume(I8 value) override;
    void setSoundVolume(I8 value) override;

    void addMusic(U32 playlistEntry, Handle<AudioDescriptor> music);
    [[nodiscard]] bool playMusic(U32 playlistEntry);
    [[nodiscard]] bool playMusic(const MusicPlaylist& playlist);

    void dumpPlaylists();

    PROPERTY_RW(AudioAPI, apiID, AudioAPI::COUNT);

protected:
    friend class SDL_API;
    void trackFinished(const TrackDetails& details) noexcept override;

    [[nodiscard]] bool frameStarted( const FrameEvent& evt ) override;

protected:
    AudioState _state;

    MusicPlaylists _musicPlaylists;
    MusicPlaylist _currentPlaylist;

private:
    AudioAPIWrapper_uptr _api;
    std::atomic_bool _playNextInPlaylist;
};

};  // namespace Divide

#endif //DVD_SFX_DEVICE_H
