#include "stdafx.h"

#include "Headers/GUIConsole.h"
#include "CEGUIAddons/Headers/CEGUIFormattedListBox.h"
#include "Headers/GUI.h"
#include "Headers/GUIConsoleCommandParser.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Utility/Headers/Localization.h"

#include "Platform/Headers/PlatformRuntime.h"

#include <CEGUI/CEGUI.h>

namespace Divide {

namespace {
    /// Maximum number of lines to display in the console Window
    constexpr U32 _CEGUI_MAX_CONSOLE_ENTRIES = Config::Build::IS_DEBUG_BUILD ? 128 : 512;
};

GUIConsole::GUIConsole(GUI& parent, PlatformContext& context, ResourceCache* cache)
    : PlatformContextComponent(context),
      _parent(parent),
      _init(false),
      _closing(false),
      _lastMsgType(Console::EntryType::INFO),
      _editBox(nullptr),
      _outputWindow(nullptr),
      _consoleWindow(nullptr),
      _inputHistoryIndex(0),
      _consoleCallbackIndex(0)
{
    // we need a default command parser, so just create it here
    _cmdParser = MemoryManager_NEW GUIConsoleCommandParser(_context, cache);

    _consoleCallbackIndex = Console::bindConsoleOutput([this](const Console::OutputEntry& entry) {
        printText(entry);
    });
}

GUIConsole::~GUIConsole()
{
    Console::unbindConsoleOutput(_consoleCallbackIndex);
    _closing = true;

    if (_consoleWindow) {
        setVisible(false);
        _init = false;
        _parent.getCEGUIContext().getRootWindow()->removeChild(_consoleWindow);
        CEGUI::WindowManager::getSingletonPtr()->destroyWindow(_consoleWindow);
    }

    MemoryManager::DELETE(_cmdParser);
}

void GUIConsole::createCEGUIWindow() {
    if (_init) {
        Console::errorfn(Locale::Get(_ID("ERROR_CONSOLE_DOUBLE_INIT")));
    }
    // load the console Window from the layout file
    const string layoutFile(_context.config().gui.consoleLayoutFile);
    _consoleWindow = CEGUI::WindowManager::getSingletonPtr()->loadLayoutFromFile(layoutFile.c_str());

    if (_consoleWindow) {
        // Add the Window to the GUI Root Sheet
        _parent.getCEGUIContext().getRootWindow()->addChild(_consoleWindow);
        _outputWindow = static_cast<CEGUI::Listbox*>(_consoleWindow->getChild("ChatBox"));
        _editBox = static_cast<CEGUI::Editbox*>(_consoleWindow->getChild("EditBox"));
        // Now register the handlers for the events (Clicking, typing, etc)
        RegisterHandlers();
    } else {
        // Loading layout from file, failed, so output an error Message.
        CEGUI::Logger::getSingleton().logEvent(
            "Error: Unable to load the ConsoleWindow from .layout");
        Console::errorfn(Locale::Get(_ID("ERROR_CONSOLE_LAYOUT_FILE")),
                         layoutFile.c_str());
    }

    _consoleWindow->setVisible(false);
    _init = true;
    Console::printfn(Locale::Get(_ID("GUI_CONSOLE_CREATED")));
}

void GUIConsole::RegisterHandlers() {
    assert(_editBox != nullptr);
    // We need to monitor text/command submission from the editBox
    _editBox->subscribeEvent(
        CEGUI::Editbox::EventTextAccepted,
        CEGUI::Event::Subscriber(&GUIConsole::Handle_TextSubmitted, this));
    // we also monitor any key presses to make sure we do not accidentally hide
    // the console in the middle of a sentence
    _editBox->subscribeEvent(
        CEGUI::Editbox::EventKeyUp,
        CEGUI::Event::Subscriber(&GUIConsole::Handle_TextInput, this));
}

bool GUIConsole::Handle_TextInput(const CEGUI::EventArgs& e) {
    assert(_editBox != nullptr);
    const CEGUI::KeyEventArgs* keyEvent =
        static_cast<const CEGUI::KeyEventArgs*>(&e);
    // Just get the current text string from the editbox at each keypress.
    // Performance isn't a issue for console commands
    if (!_inputHistory.empty()) {
        if (keyEvent->scancode == CEGUI::Key::ArrowUp) {
            _inputHistoryIndex--;
            if (_inputHistoryIndex < 0)
                _inputHistoryIndex = (I16)_inputHistory.size() - 1;
            _editBox->setText(_inputHistory[_inputHistoryIndex]);
        }
        if (keyEvent->scancode == CEGUI::Key::ArrowDown) {
            _inputHistoryIndex++;
            if (_inputHistoryIndex >= (I32)_inputHistory.size())
                _inputHistoryIndex = 0;
            _editBox->setText(_inputHistory[_inputHistoryIndex]);
        }
    }

    _inputBuffer = _editBox->getText();
    return true;
}

bool GUIConsole::Handle_TextSubmitted(const CEGUI::EventArgs& /*e*/) {
    assert(_editBox != nullptr);
    // Since we have that string, lets send it to the TextParser which will
    // handle it from here
    if (!_cmdParser->processCommand(_inputBuffer.c_str())) {
        printText({"Failed to parse command!", Console::EntryType::ERR});
    }
    // Now that we've finished with the text, we need to ensure that we clear
    // out the EditBox.
    // This is what we would expect to happen after we press enter
    _editBox->setText("");
    _inputHistory.push_back(_inputBuffer);
    // Keep command history low
    if (_inputHistory.size() > _CEGUI_MAX_CONSOLE_ENTRIES) {
        _inputHistory.pop_front();
    }
    _inputHistoryIndex = (I16)_inputHistory.size() - 1;
    // reset the inputbuffer so we can handle console closing properly
    _inputBuffer.clear();
    return true;
}

void GUIConsole::setVisible(const bool visible) {
    if (_init) {
        // if it's not the first key (e.g., if the toggle key is "~", then
        // "lorem~ipsum" should not close the Window)
        if (!_inputBuffer.empty()) {
            return;
        }
        assert(_editBox != nullptr);
        _consoleWindow->setVisible(visible);

        if (visible) {
            _editBox->activate();
        } else {
            _editBox->deactivate();
            _editBox->setText("");
        }

        printText(
            {
                visible ? "Toggling console display: ON"
                        : "Toggling console display: OFF",
                Console::EntryType::INFO
            }
        );
        const size_t count = _outputWindow->getItemCount();
        if (count > 0 && visible) {
            _outputWindow->ensureItemIsVisible(count - 1);
        }
    }
}

bool GUIConsole::isVisible() const {
    if (!_init) {
        return false;
    }
    return _consoleWindow->isVisible();
}

void GUIConsole::printText(const Console::OutputEntry& entry) {
    _outputBuffer.enqueue(entry);
    if (_outputBuffer.size_approx() > _CEGUI_MAX_CONSOLE_ENTRIES) {
        for (U8 i = 0; i < _CEGUI_MAX_CONSOLE_ENTRIES; ++i) {
            Console::OutputEntry message;
            if (!_outputBuffer.try_dequeue(message)) {
                break;
            }
        }
    }
}

void GUIConsole::OutputText(const Console::OutputEntry& text) const {
    if (_outputWindow->getItemCount() == _CEGUI_MAX_CONSOLE_ENTRIES - 1) {
        _outputWindow->removeItem(_outputWindow->getListboxItemFromIndex(0));
    }

    CEGUI::FormattedListboxTextItem* crtItem =
        new CEGUI::FormattedListboxTextItem(
            CEGUI::String(text._text.c_str()),
            text._type == Console::EntryType::ERR 
                ? CEGUI::Colour(1.0f, 0.0f, 0.0f)
                : text._type == Console::EntryType::WARNING 
                ? CEGUI::Colour(1.0f, 1.0f, 0.0f)
                : CEGUI::Colour(0.4f, 0.4f, 0.3f),
            CEGUI::HTF_WORDWRAP_LEFT_ALIGNED);

    crtItem->setTextParsingEnabled(false);
    _outputWindow->addItem(crtItem);
    _outputWindow->ensureItemIsVisible(crtItem);
}

void GUIConsole::update(const U64 /*deltaTimeUS*/) {
    if (!_init || !Runtime::isMainThread() || _closing) {
        return;
    }
    if (!isVisible()) {
        return;
    }

    {
        Console::OutputEntry message;
        while(_outputBuffer.try_dequeue(message)) {
            const Console::EntryType type = message._type;
            if (_lastMsgType != type) {
                _lastMsgType = type;
                OutputText(message);
                _lastMsg.clear();
            }
         
            _lastMsg.append(message._text.c_str());
            _lastMsg.append("\n");
        }
    }

    if (!_lastMsg.empty()) {
        OutputText(
            {
                _lastMsg.c_str(),
                _lastMsgType
            }
        );
        _lastMsg.clear();
    }
}
};