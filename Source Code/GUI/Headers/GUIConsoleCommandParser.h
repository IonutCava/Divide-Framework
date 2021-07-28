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
#ifndef _GUI_CONSOLE_COMMAND_PARSER_H_
#define _GUI_CONSOLE_COMMAND_PARSER_H_

#include "Core/Headers/PlatformContextComponent.h"
#include "Utility/Headers/CommandParser.h"
/// Handle console commands that start with a forward slash

namespace Divide {

class AudioDescriptor;
TYPEDEF_SMART_POINTERS_FOR_TYPE(AudioDescriptor);

class ResourceCache;
class PlatformContext;
class GUIConsoleCommandParser final : public CommandParser, public PlatformContextComponent {
   public:
    GUIConsoleCommandParser(PlatformContext& context, ResourceCache* cache);

    [[nodiscard]] bool processCommand(const stringImpl& commandString) override;

   private:
    using CommandMap = hashMap<U64 /*command name*/, DELEGATE_STD<void, stringImpl /*args*/> >;

    void handleSayCommand(const stringImpl& args);
    void handleQuitCommand(const stringImpl& args);
    void handleHelpCommand(const stringImpl& args);
    void handleEditParamCommand(const stringImpl& args);
    void handlePlaySoundCommand(const stringImpl& args);
    void handleNavMeshCommand(const stringImpl& args);
    void handleShaderRecompileCommand(const stringImpl& args);
    void handleFOVCommand(const stringImpl& args);
    void handleInvalidCommand(const stringImpl& args);

   private:
    ResourceCache* _resCache = nullptr;
    /// Help text for every command
    hashMap<U64, const char*> _commandHelp;
    /// used for sound playback
    AudioDescriptor_ptr _sound;
};

};  // namespace Divide

#endif
