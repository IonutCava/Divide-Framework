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
#ifndef DVD_AUDIO_API_H
#define DVD_AUDIO_API_H

#include "AudioDescriptor.h"
#include "Core/Headers/FrameListener.h"
#include "Core/Headers/PlatformContextComponent.h"

namespace Divide {

class PlatformContext;

class AudioState {
   public:
    AudioState([[maybe_unused]] bool enableA, [[maybe_unused]] bool enableB, [[maybe_unused]] bool enableC, [[maybe_unused]] bool enableD) noexcept
    {
    }
};

constexpr U32 MAX_SOUND_BUFFERS = 64;

/// Audio Programming Interface
class NOINITVTABLE AudioAPIWrapper : public PlatformContextComponent, public FrameListener
{
   public:
     using MusicPlaylist = std::pair<U32, vector< Handle<AudioDescriptor>>>;
     using MusicPlaylists = hashMap<U32, MusicPlaylist>;

   public:
     explicit AudioAPIWrapper( const Str<64>& name, PlatformContext& context );

   protected:
    [[nodiscard]] virtual bool frameStarted( const FrameEvent& evt ) override { DIVIDE_UNUSED(evt); return true; }
    [[nodiscard]] virtual bool frameEnded( const FrameEvent& evt ) noexcept override { DIVIDE_UNUSED(evt); return true; }

   protected:
    friend class SFXDevice;
    virtual ErrorCode initAudioAPI() = 0;
    virtual void closeAudioAPI() = 0;

    virtual void playSound( Handle<AudioDescriptor> sound ) = 0;

    // this stops the current track, if any, and plays the specified song
    virtual void playMusic( Handle<AudioDescriptor> music ) = 0;

    virtual void pauseMusic() = 0;
    virtual void stopMusic() = 0;
    virtual void stopAllSounds() = 0;

    virtual void setMusicVolume(I8 value) = 0;
    virtual void setSoundVolume(I8 value) = 0;

    virtual void musicFinished() = 0;
};

FWD_DECLARE_MANAGED_CLASS(AudioAPIWrapper);

};  // namespace Divide

#endif //DVD_AUDIO_API_H
