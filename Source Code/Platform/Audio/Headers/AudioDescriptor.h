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
#ifndef _AUDIO_DESCRIPTOR_H_
#define _AUDIO_DESCRIPTOR_H_

#include "Core/Resources/Headers/Resource.h"
#include "Platform/File/Headers/FileManagement.h"

namespace Divide {

class AudioDescriptor final : public CachedResource {
   public:
    AudioDescriptor(const size_t descriptorHash, const Str256& name, const ResourcePath& audioFileName, const ResourcePath& audioFilePath)
        : CachedResource(ResourceType::DEFAULT, descriptorHash, name, audioFileName, audioFilePath)
    {
    }

    virtual ~AudioDescriptor() = default;

    bool unload() override {
        return true;
    }

    void setAudioFile(const string& filePath) {
        const auto[name, path] = splitPathToNameAndLocation(filePath.c_str());
        assetName(name);
        assetLocation(path);
        _dirty = true;
    }

    void clean() noexcept { dirty(false); }

    [[nodiscard]] const char* getResourceTypeName() const noexcept  override { return "AudioDescriptor"; }

    PROPERTY_RW(F32, frequency, 44.2f);
    PROPERTY_RW(I8, bitDepth, 16);
    PROPERTY_RW(I8, volume, 100);
    PROPERTY_RW(I8, channelID, -1);
    PROPERTY_R_IW(bool, dirty, true);
    PROPERTY_RW(bool, isLooping, false);
    PROPERTY_RW(bool, is3D, false);
};

TYPEDEF_SMART_POINTERS_FOR_TYPE(AudioDescriptor);

};  // namespace Divide

#endif
