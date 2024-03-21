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

typedef struct _Mix_Music Mix_Music;
struct Mix_Chunk;

namespace Divide {

class SDL_API final : public AudioAPIWrapper {
public:
    explicit SDL_API( PlatformContext& context );

    ErrorCode initAudioAPI() override;
    void closeAudioAPI() override;

    void playSound(const AudioDescriptor_ptr& sound) override;
    void playMusic(const AudioDescriptor_ptr& music) override;

    void stopMusic() noexcept override;
    void stopAllSounds() noexcept override {}
    void pauseMusic() noexcept override {}

    void setMusicVolume(I8) noexcept override {}
    void setSoundVolume(I8) noexcept override {}

protected:
    void musicFinished() noexcept override;

private:
    using MusicMap = hashMap<I64, Mix_Music*>;
    using SoundMap = hashMap<I64, Mix_Chunk*>;

    MusicMap _musicMap;
    SoundMap _soundMap;
};

};  // namespace Divide

#endif //DVD_WRAPPER_SDL_H_
