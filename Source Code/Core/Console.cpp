#include "stdafx.h"

#include "Headers/Console.h"

#include "Core/Time/Headers/ApplicationTimer.h"

namespace Divide {

bool Console::_timestamps = false;
bool Console::_threadID = false;
bool Console::_enabled = true;
bool Console::_immediateMode = true;
bool Console::_errorStreamEnabled = true;

std::atomic_bool Console::_running = false;
vector<Console::ConsolePrintCallback> Console::_guiConsoleCallbacks;

//Use moodycamel's implementation of a concurrent queue due to its "Knock-your-socks-off blazing fast performance."
//https://github.com/cameron314/concurrentqueue
namespace {
    thread_local char textBuffer[CONSOLE_OUTPUT_BUFFER_SIZE + 1];

    std::array<Console::OutputEntry, 16> g_outputCache;

    moodycamel::BlockingConcurrentQueue<Console::OutputEntry>& OutBuffer() {
        static moodycamel::BlockingConcurrentQueue<Console::OutputEntry> s_OutputBuffer;
        return s_OutputBuffer;
    }
}

//! Do not remove the following license without express permission granted by DIVIDE-Studio
void Console::printCopyrightNotice() {
    std::cout << "------------------------------------------------------------------------------\n"
              << "Copyright (c) 2018 DIVIDE-Studio\n"
              << "Copyright (c) 2009 Ionut Cava\n\n"
              << "This file is part of DIVIDE Framework.\n\n"
              << "Permission is hereby granted, free of charge, to any person obtaining a copy of this software\n"
              << "and associated documentation files (the 'Software'), to deal in the Software without restriction,\n"
              << "including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,\n"
              << "and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so,\n"
              << "subject to the following conditions:\n\n"
              << "The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.\n\n"
              << "THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,\n"
              << "INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.\n"
              << "IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,\n"
              << "WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE\n"
              << "OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.\n\n"
              << "For any problems or licensing issues I may have overlooked, please contact: \n"
              << "E-mail: ionut.cava@divide-studio.com | Website: \n http://wwww.divide-studio.com\n"
              << "-------------------------------------------------------------------------------\n\n";
}

const char* Console::formatText(const char* format, ...) noexcept {
    va_list args;
    va_start(args, format);
    assert(_vscprintf(format, args) + 1 < CONSOLE_OUTPUT_BUFFER_SIZE);
    vsprintf(textBuffer, format, args);
    va_end(args);
    return textBuffer;
}

void Console::decorate(std::ostream& outStream, const char* text, const bool newline, const EntryType type) {
    if (_timestamps) {
        outStream << "[ " << std::internal
                          << std::setw(9)
                          << std::setprecision(3)
                          << std::setfill('0')
                          << std::fixed
                          << Time::App::ElapsedSeconds()
                  << " ] ";
    }
    if (_threadID) {
        outStream << "[ " << std::this_thread::get_id() << " ] ";
    }

    if (type == EntryType::WARNING || type == EntryType::ERR) {
        outStream << (type == EntryType::ERR ? " Error: " : " Warning: ");
    }

    outStream << text;

    if (newline) {
        outStream << "\n";
    }
}

void Console::output(std::ostream& outStream, const char* text, const bool newline, const EntryType type) {
    if (_enabled) {
        decorate(outStream, text, newline, type);
    }
}

void Console::output(const char* text, const bool newline, const EntryType type) {
    if (_enabled) {
        stringstream_fast outStream;
        decorate(outStream, text, newline, type);

        OutputEntry entry;
        entry._text = outStream.str();
        entry._type = type;
        if (_immediateMode) {
            printToFile(entry);
        } else {
            if (!OutBuffer().enqueue(entry)) {
                // ToDo: Something happened. Handle it, maybe? -Ionut
                printToFile(entry);
            }
        }
    }
}

void Console::printToFile(const OutputEntry& entry) {
    (entry._type == EntryType::ERR && _errorStreamEnabled ? std::cerr : std::cout) << entry._text.c_str();

    for (const ConsolePrintCallback& cbk : _guiConsoleCallbacks) {
        if (!_running) {
            break;
        }
        cbk(entry);
    }
}

void Console::flush() {
    if (!_enabled) {
        return;
    }

    size_t count;
    do {
        count = OutBuffer().try_dequeue_bulk(std::begin(g_outputCache), g_outputCache.size());

        for (size_t i = 0; i < count; ++i) {
            printToFile(g_outputCache[i]);
        }
    } while (count > 0);
}

void Console::start() noexcept {
    if (!_running) {
        _running = true;
    }
}

void Console::stop() {
    if (_running) {
        flush();
        _immediateMode = true;
        _enabled = false;
        _running = false;
        std::cout << "------------------------------------------" << std::endl
                  << std::endl << std::endl << std::endl;

        std::cerr << std::flush;
        std::cout << std::flush;
    }
}
}
