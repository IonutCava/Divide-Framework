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
#ifndef _AUDIO_API_H
#define _AUDIO_API_H

#include "AudioDescriptor.h"

namespace Divide {

class PlatformContext;

class AudioState {
   public:
    AudioState(bool enableA, bool enableB, bool enableC, bool enableD) noexcept
    {
        ACKNOWLEDGE_UNUSED(enableA);
        ACKNOWLEDGE_UNUSED(enableB);
        ACKNOWLEDGE_UNUSED(enableC);
        ACKNOWLEDGE_UNUSED(enableD);
    }
};

constexpr U32 MAX_SOUND_BUFFERS = 64;

/// Audio Programming Interface
class NOINITVTABLE AudioAPIWrapper {
   public:
     using MusicPlaylist = std::pair<U32, vector<AudioDescriptor_ptr>>;
     using MusicPlaylists = hashMap<U32, MusicPlaylist>;

   public:
     virtual ~AudioAPIWrapper() = default;

   protected:
    friend class SFXDevice;
    virtual ErrorCode initAudioAPI(PlatformContext& context) = 0;
    virtual void closeAudioAPI() = 0;

    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;

    virtual void playSound(const AudioDescriptor_ptr& sound) = 0;

    // this stops the current track, if any, and plays the specified song
    virtual void playMusic(const AudioDescriptor_ptr& music) = 0;

    virtual void pauseMusic() = 0;
    virtual void stopMusic() = 0;
    virtual void stopAllSounds() = 0;

    virtual void setMusicVolume(I8 value) = 0;
    virtual void setSoundVolume(I8 value) = 0;

    virtual void musicFinished() = 0;
};

};  // namespace Divide

#endif