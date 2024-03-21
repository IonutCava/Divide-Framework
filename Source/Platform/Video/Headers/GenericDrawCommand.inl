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

#ifndef DVD_GENERIC_DRAW_COMMAND_INL_
#define DVD_GENERIC_DRAW_COMMAND_INL_

namespace Divide {

    inline bool isEnabledOption(const GenericDrawCommand& cmd, const CmdRenderOptions option) noexcept {
        return cmd._renderOptions & to_base(option);
    }

    inline void enableOption(GenericDrawCommand& cmd, const CmdRenderOptions option) noexcept {
        cmd._renderOptions |= to_base(option);
    }

    inline void disableOption(GenericDrawCommand& cmd, const CmdRenderOptions option) noexcept {
        cmd._renderOptions &= ~to_base(option);
    }

    inline void toggleOption(GenericDrawCommand& cmd, const CmdRenderOptions option) noexcept {
        setOption(cmd, option, !isEnabledOption(cmd, option));
    }

    inline void setOption(GenericDrawCommand& cmd, const CmdRenderOptions option, const bool state) noexcept {
        if (state) {
            enableOption(cmd, option);
        } else {
            disableOption(cmd, option);
        }
    }

    inline void enableOptions(GenericDrawCommand& cmd, const BaseType<CmdRenderOptions> optionsMask) noexcept {
        cmd._renderOptions |= to_base(optionsMask);
    }

    inline void disableOptions(GenericDrawCommand& cmd, const BaseType<CmdRenderOptions> optionsMask) noexcept {
        cmd._renderOptions &= ~to_base(optionsMask);
    }

    inline void setOptions(GenericDrawCommand& cmd, const BaseType<CmdRenderOptions> optionsMask, const bool state) noexcept {
        if (state) {
            enableOptions(cmd, optionsMask);
        } else {
            disableOptions(cmd, optionsMask);
        }
    }

    inline void resetOptions(GenericDrawCommand& cmd) noexcept {
        cmd._renderOptions = to_base(CmdRenderOptions::RENDER_GEOMETRY);
    }
}; //namespace Divide

#endif //DVD_GENERIC_DRAW_COMMAND_INL_
