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
#ifndef DVD_WRAPPER_SDL_H_
#define DVD_WRAPPER_SDL_H_

#include "Platform/Audio/Headers/AudioAPIWrapper.h"
#include <SDL3_mixer/SDL_mixer.h>

namespace Divide {

class SDL_API final : public AudioAPIWrapper {
public:
    explicit SDL_API( PlatformContext& context );

    ErrorCode initAudioAPI() override;
    void closeAudioAPI() override;

    void playSound(const Handle<AudioDescriptor> sound) override;
    void playMusic(const Handle<AudioDescriptor> music) override;

    void stopMusic() noexcept override;
    void stopAllSounds() noexcept override;
    void pauseMusic() noexcept override;
    void resumeMusic() noexcept override;

    void setMusicVolume(I8 gain) noexcept override;
    void setSoundVolume(I8 gain) noexcept override;

    PROPERTY_R(bool, paused, false);

protected:
    struct AudioPlaybackProperties
    {
        TrackDetails* _details;
        MIX_Mixer* _mixer{nullptr};
        MIX_Audio* _audio{ nullptr };
        MIX_Track* _track{ nullptr };
        MIX_Point3D _position{};
        MIX_StereoGains _stereoGains{};
        SDL_PropertiesID _properties{0};
        F32 _volume{ 1.f };
        bool _loop{ false };
        bool _is3D{ false };
    };

    void trackFinished(const TrackDetails& details) noexcept override;

    bool playAudio(const AudioPlaybackProperties& playback) const;
private:

    struct SoundEntry
    {
        MIX_Audio* _audio = nullptr;
        MIX_Track* _track = nullptr;
        TrackDetails _details{};
        SDL_PropertiesID _properties{ 0 };
        MIX_Point3D _position{};
        MIX_StereoGains _stereoGains{};
    };

    using MusicMap = hashMap<U32, MIX_Audio*>;
    using SoundMap = hashMap<U32, SoundEntry>;

    F32 _musicGain = 1.f;
    F32 _soundGain = 1.f;
    MusicMap _musicMap;
    SoundMap _soundMap;
    MIX_Mixer* _musicMixer = nullptr;
    MIX_Mixer* _soundMixer = nullptr;
    MIX_Track* _activeSong = nullptr; 
    std::array<SoundEntry*, MAX_SOUND_BUFFERS> _activeSoundChannels{};
    SDL_PropertiesID _activeSongProperties{0};
    TrackDetails _activeSongDetails{};
};

};  // namespace Divide

#endif //DVD_WRAPPER_SDL_H_
