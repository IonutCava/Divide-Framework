

#include "Headers/Localization.h"

#include "Core/Headers/ErrorCodes.h"

#include "Platform/File/Headers/FileManagement.h"
#include "Platform/File/Headers/FileUpdateMonitor.h"

#include <SimpleIni.h>

namespace FW
{
    FWD_DECLARE_MANAGED_CLASS(FileWatcher);
};

namespace Divide::Locale
{

namespace detail
{
    /// Default language can be set at compile time
    static Str<64> g_localeFile = {};

    static bool g_init = false;
    static bool g_fileWatcher = false;

    static LanguageData_uptr g_data = nullptr;

    /// External modification monitoring system
    static FW::FileWatcher_uptr g_LanguageFileWatcher = nullptr;

    /// Callback for external file changes. 
    static UpdateListener g_fileWatcherListener([](const std::string_view languageFile, const FileUpdateEvent evt)
    {
        if (evt == FileUpdateEvent::DELETED)
        {
            return;
        }

        // If we modify our currently active language, re-init the Locale system
        if ((g_localeFile + g_languageFileExtension).c_str() == languageFile)
        {
            ChangeLanguage(g_localeFile.c_str());
        }
    });

    constexpr U32 g_fileWatcherUpdateFrameInterval = 120u;

} //detail

void LanguageData::setChangeLanguageCallback(const DELEGATE<void, std::string_view /*new language*/>& cbk)
{
    _languageChangeCallback = cbk;
}

ErrorCode LanguageData::changeLanguage(const std::string_view newLanguage)
{
    // Use SimpleIni library for cross-platform INI parsing
    CSimpleIni languageFile(true, false, true);

    detail::g_localeFile = newLanguage.data();
    assert(!detail::g_localeFile.empty());

    const ResourcePath file = Paths::g_localisationPath / ( detail::g_localeFile + g_languageFileExtension );

    if (languageFile.LoadFile(file.string().c_str() ) != SI_OK)
    {
        return ErrorCode::NO_LANGUAGE_INI;
    }

    _languageTable.clear();

    CSimpleIni::TNamesDepend sections{};
    languageFile.GetAllSections(sections);

    for (const auto& section: sections)
    {
        // Load all key-value pairs for the current section
        const CSimpleIni::TKeyVal* keyValue = languageFile.GetSection(section.pItem);
    
        // And add all pairs to the language table
        CSimpleIni::TKeyVal::const_iterator keyValuePairIt = keyValue->begin();
        for (; keyValuePairIt != keyValue->end(); ++keyValuePairIt)
        {
            emplace(_languageTable,
                    _ID(keyValuePairIt->first.pItem),
                    LanguageEntry
                    {
                        ._value = keyValuePairIt->second, 
                        ._sectionAndValue = Util::StringFormat( "[ {} ] {}", section.pItem, keyValuePairIt->second )
                    }
                 );
        }
    }

    if (_languageChangeCallback)
    {
        _languageChangeCallback(newLanguage);
    }

    return ErrorCode::NO_ERR;
}

const char* LanguageData::get(const U64 key, const bool appendSection, const char* defaultValue )
{
    // When we ask for a string for the given key, we check our language cache first
    const auto& entry = _languageTable.find(key);
    if (entry != std::cend(_languageTable))
    {
        // Usually, the entire language table is loaded.
        return appendSection ? entry->second._sectionAndValue.c_str() : entry->second._value.c_str();
    }

    DIVIDE_UNEXPECTED_CALL_MSG("Locale error: INVALID STRING KEY!");

    return defaultValue == nullptr ? "MISSING_ENTRY_NO_DEFAULT" : defaultValue;
}

ErrorCode Init(const char* newLanguage)
{
    if constexpr (!Config::Build::IS_SHIPPING_BUILD && Config::ENABLE_LOCALE_FILE_WATCHER)
    {
        detail::g_fileWatcherListener.addIgnoredEndCharacter('~');
        detail::g_fileWatcherListener.addIgnoredExtension("tmp");

        detail::g_LanguageFileWatcher.reset(new FW::FileWatcher());
        detail::g_LanguageFileWatcher->addWatch(FW::String(Paths::g_localisationPath.string()), &detail::g_fileWatcherListener);
        detail::g_fileWatcher = true;
    }
    else
    {
        detail::g_fileWatcher = false;
    }


    detail::g_data = std::make_unique<LanguageData>();
    detail::g_init = true;

    return ChangeLanguage(newLanguage);
}

void Clear() noexcept
{
    detail::g_data.reset();
    detail::g_init = false;

    detail::g_LanguageFileWatcher.reset();
    detail::g_fileWatcher = false;
}

void Idle()
{
    static U32 updateCounter = detail::g_fileWatcherUpdateFrameInterval;
    if (detail::g_fileWatcher && --updateCounter == 0u)
    {
        detail::g_LanguageFileWatcher->update();
        updateCounter = detail::g_fileWatcherUpdateFrameInterval;
    }
}

// Although the language can be set at compile time, in-game options may support language changes
ErrorCode ChangeLanguage(const char* newLanguage)
{
    assert(detail::g_data != nullptr);

    // Set the new language code (override old data)
    return detail::g_data->changeLanguage(newLanguage);
}

const char* Get(const U64 key, bool appendSection, const char* defaultValue )
{
    assert( detail::g_data != nullptr );

    return detail::g_data->get(key, appendSection, defaultValue);
}

void SetChangeLanguageCallback(const DELEGATE<void, std::string_view /*new language*/>& cbk)
{
    assert(detail::g_data != nullptr);

    detail::g_data->setChangeLanguageCallback(cbk);
}

const Str<64>& CurrentLanguage() noexcept
{
    return detail::g_localeFile;
}

}  // namespace Divide::Locale
