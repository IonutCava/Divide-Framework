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
#ifndef _WRAPPER_FMOD_H_
#define _WRAPPER_FMOD_H_

#include "Platform/Audio/Headers/AudioAPIWrapper.h"

namespace Divide {

/*****************************************************************************************/
/*                ATTENTION! FMOD is not free for commercial use!!! */
/*    Do not include it in commercial products unless you own a license for it.
 */
/*    Visit: http://www.fmod.org/index.php/sales  for more details -Ionut */
/*****************************************************************************************/

class FMOD_API final : public AudioAPIWrapper {
public:
    ErrorCode initAudioAPI([[maybe_unused]] PlatformContext& context) override {
        return ErrorCode::FMOD_AUDIO_INIT_ERROR;
    }

    void closeAudioAPI() override {}

    void playSound(const AudioDescriptor_ptr& sound) override {}

    void playMusic(const AudioDescriptor_ptr& music) override {}

    void beginFrame() override {}
    void endFrame() override {}

    void pauseMusic() override {}
    void stopMusic() override {}
    void stopAllSounds() override {}

    void setMusicVolume(I8 value) override {}
    void setSoundVolume(I8 value) override {}

protected:
    void musicFinished() override {}
};

};  // namespace Divide

#endif