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
#ifndef DVD_LOCALE_H_
#define DVD_LOCALE_H_

#include "Platform/Headers/PlatformDefines.h"

namespace Divide {

enum class ErrorCode : I8;
enum class FileUpdateEvent : U8;

namespace Locale {
constexpr const char* DEFAULT_LANG = "enGB";
constexpr const char* const g_languageFileExtension = ".ini";

struct LanguageEntry
{
    string _value;
    string _sectionAndValue;
};

class LanguageData {
public:
    using LangCallback = DELEGATE<void, std::string_view/*new language*/>;

    ErrorCode changeLanguage(std::string_view newLanguage);

    const char* get(U64 key, bool appendSection, const char* defaultValue );
    void setChangeLanguageCallback(const DELEGATE<void, std::string_view /*new language*/>& cbk);

private:
    /// Each string key in the map matches a key in the language ini file
    /// each string value in the map matches the value in the ini file for the given key
    /// Basically, the hashMap is a direct copy of the [language] section of the give ini file
    hashMap<U64, LanguageEntry> _languageTable{};
    SharedMutex _languageTableMutex{};
    LangCallback _languageChangeCallback{};
};

FWD_DECLARE_MANAGED_CLASS(LanguageData);

/// Reset everything and load the specified language file.
ErrorCode Init(const char* newLanguage = DEFAULT_LANG);
/// clear the language table
void Clear() noexcept;
/// perform maintenance tasks
void Idle();
/// Although the language can be set at compile time, in-game options may support
/// language changes
ErrorCode ChangeLanguage(const char* newLanguage);
/// Set a function to be called on each language change
void SetChangeLanguageCallback(const DELEGATE<void, std::string_view /*new language*/>& cbk);
/// Query the current language code to detect changes
const Str<64>& CurrentLanguage() noexcept;
/// usage: Locale::Get("A_B_C")) or Locale::Get("A_B_C"), true/false, "X") where "A_B_C" is the language key we want
/// and "X" is a default string in case the key does not exist in the INI file
/// appendSection will add [ sectionName ] in front of the returned string.
const char* Get(U64 key, bool appendSection = true, const char* defaultValue = nullptr);

#ifndef LOCALE_STR
#define LOCALE_STR(X) Locale::Get(_ID(X))
#endif //LOCALE_STR

}  // namespace Locale
}  // namespace Divide

#endif //DVD_LOCALE_H_
