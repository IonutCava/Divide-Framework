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
#ifndef DVD_WRAPPER_AL_H_
#define DVD_WRAPPER_AL_H_

#include "Platform/Audio/Headers/AudioAPIWrapper.h"

namespace Divide {

class OpenAL_API final : public AudioAPIWrapper {
public:
    explicit OpenAL_API(PlatformContext& context);

    ErrorCode initAudioAPI() noexcept override;
    void closeAudioAPI() noexcept override;

    void playSound(const Handle<AudioDescriptor> sound) noexcept override;
    void playMusic(const Handle<AudioDescriptor> music) noexcept override;

    void pauseMusic() noexcept override;
    void resumeMusic() noexcept override;
    void stopMusic() noexcept override;
    void stopAllSounds() noexcept override;

    void setMusicVolume(I8 value) noexcept override;
    void setSoundVolume(I8 value) noexcept override;

protected:
    void trackFinished(const TrackDetails& details) noexcept override;

private:
    U32 buffers[MAX_SOUND_BUFFERS] = {};
};

};  // namespace Divide

#endif //DVD_WRAPPER_AL_H_
