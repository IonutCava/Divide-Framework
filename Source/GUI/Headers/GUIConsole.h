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
#ifndef DVD_GUI_CONSOLE_H_
#define DVD_GUI_CONSOLE_H_

constexpr size_t CEGUI_MAX_INPUT_HISTORY = 5;

#include "Core/Headers/PlatformContextComponent.h"

namespace CEGUI {
    class FormattedListboxTextItem;
    class EventArgs;
};

namespace Divide {

class GUI;
class ResourceCache;
class PlatformContext;
class GUIConsoleCommandParser;
/// GUIConsole implementation, CEGUI based, as in the practical tutorial series
class GUIConsole final : public PlatformContextComponent {
   public:
    explicit GUIConsole(GUI& parent, PlatformContext& context);
    ~GUIConsole() override;

    /// Hide or show the console
    void setVisible(bool visible);
    /// Return true if console is visible, false if is hidden
    bool isVisible() const;

    void update(U64 deltaTimeUS);

    /// Add text to the console Window. Uses a text buffer if the console isn't ready for display yet
    void printText(const Console::OutputEntry& entry);

   protected:
    void RegisterHandlers();  ///< Register our handler functions
    bool Handle_TextSubmitted(const CEGUI::EventArgs& e);  ///< Handle when we press Enter after typing
    bool Handle_TextInput(const CEGUI::EventArgs& e);  ///< A key is pressed in the console input editbox

   protected:
    friend class GUI;
    void createCEGUIWindow();  ///< The function which will load in the CEGUI Window and register event handlers
    // Post the message to the ChatHistory listbox with a white colour default
    void OutputText(const Console::OutputEntry& text) const;

   protected:
    GUI& _parent;
    /// used to check if the console is ready
    bool _init;
    bool _closing;
    Console::EntryType _lastMsgType;
    CEGUI::String _lastMsg;
    /// pointer to the editBox to reduce typing and casting
    CEGUI::Editbox* _editBox;
    /// pointer to the listbox that will contain all of the text we output to
    /// the console
    CEGUI::Listbox* _outputWindow;
    /// pointer to the command parser instance used
    std::unique_ptr<GUIConsoleCommandParser> _cmdParser;
    /// This will be a pointer to the ConsoleRoot Window.
    CEGUI::Window* _consoleWindow;
    /// Used to check the text we are typing so that we don't close the console
    /// in the middle of a sentence/command
    CEGUI::String _inputBuffer;
    /// Used to manage the input history
    std::deque<CEGUI::String> _inputHistory;
    /// Used to cycle through history
    I16 _inputHistoryIndex;
    
    moodycamel::ConcurrentQueue<Console::OutputEntry> _outputBuffer;

    size_t _consoleCallbackIndex;
};

};  // namespace Divide

#endif //DVD_GUI_CONSOLE_H_
