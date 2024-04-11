

#include "Headers/FileUpdateMonitor.h"
#include "Platform/File/Headers/FileManagement.h"

namespace Divide {

UpdateListener::UpdateListener(FileUpdateCbk&& cbk)
    : _cbk(MOV(cbk))
{
}

void UpdateListener::addIgnoredExtension(const char* extension) {
    _ignoredExtensions.emplace_back(extension);
}

void UpdateListener::addIgnoredEndCharacter(char character) {
    _ignoredEndingCharacters.emplace_back(character);
}

void UpdateListener::handleFileAction([[maybe_unused]] const FW::WatchID watchid, [[maybe_unused]] const FW::String& dir, const FW::String& filename, const FW::Action action)
{
    // We can ignore files that end in a specific character. Many text editors, for example, append a '~' at the end of temp files
    if (!_ignoredEndingCharacters.empty() &&
        eastl::any_of(cbegin(_ignoredEndingCharacters),
                      cend(_ignoredEndingCharacters),
                      [filename](const char character) noexcept {
                          return std::tolower(filename.back()) == std::tolower(character);
                      }))
    {
        return;
    }

    // We can specify a list of extensions to ignore for a specific listener to avoid, for example, parsing temporary OS files
    if (!_ignoredExtensions.empty() && 
        eastl::any_of(cbegin(_ignoredExtensions),
                      cend(_ignoredExtensions),
                      [filename](const Str<8>& extension) {
                          return hasExtension(filename, extension);
                      }))
    {
        return;
    }

    if (_cbk) {
        FileUpdateEvent evt = FileUpdateEvent::COUNT;
        switch (action)
        {
            case FW::Actions::Add: evt = FileUpdateEvent::ADD; break;
            case FW::Actions::Delete: evt = FileUpdateEvent::DELETE; break;
            case FW::Actions::Modified: evt = FileUpdateEvent::MODIFY; break;
            default: DIVIDE_UNEXPECTED_CALL();
        }

        _cbk(filename.c_str(), evt);
    }
}

}; //namespace Divide;
