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
#ifndef DVD_CORE_STRING_HELPER_H_
#define DVD_CORE_STRING_HELPER_H_

namespace Divide {
    namespace Util {
        template<typename T>
        concept valid_replace_string = is_string<T> || std::is_same_v<T, ResourcePath>;

        bool FindCommandLineArgument(int argc, char** argv, const char* target_arg, const char* arg_prefix = "--");
        bool ExtractStartupProject(int argc, char** argv, string& projectOut, const char* arg_prefix = "--");

        template<size_t N, typename T_str = string>  requires valid_replace_string<T_str>
        bool ReplaceStringInPlace(T_str& subject, const std::array<std::string_view, N>& search, std::string_view replace, bool recursive = false);

        template<size_t N, typename T_str = string>  requires valid_replace_string<T_str>
        T_str ReplaceString(std::string_view subject, const std::array<std::string_view, N>& search, std::string_view replace, bool recursive = false);

        template<typename T_str = string> requires valid_replace_string<T_str>
        bool ReplaceStringInPlace(T_str& subject, std::string_view search, std::string_view replace, bool recursive = false);
        
        template<typename T_str = string> requires valid_replace_string<T_str>
        T_str ReplaceString(std::string_view subject, std::string_view search, std::string_view replace, bool recursive = false);
        
        template<typename T_str = string> requires valid_replace_string<T_str>
        T_str MakeXMLSafe(std::string_view subject); 
        
        ResourcePath MakeXMLSafe(const ResourcePath& subject);

        template<typename T_str = string> requires valid_replace_string<T_str>
        void GetPermutations(std::string_view subject, vector<T_str>& permutationContainer);

        template<typename T_str = string> requires is_string<T_str>
        bool IsNumber(const T_str& s);

        bool IsNumber(const char* s);

        bool BeginsWith(std::string_view input, std::string_view compare, bool ignoreWhitespace);

        template<typename T_str = string> requires is_string<T_str>
        T_str GetTrailingCharacters(const T_str& input, size_t count);

        template<typename T_str = string> requires is_string<T_str>
        T_str GetStartingCharacters(const T_str& input, size_t count);

        template<typename T_strA = string, typename T_strB = string> requires valid_replace_string<T_strA> && valid_replace_string<T_strB>
        bool CompareIgnoreCase(const T_strA& a, const T_strB& b) noexcept;

        bool CompareIgnoreCase(const char* a, const char* b) noexcept;

        /// http://stackoverflow.com/questions/236129/split-a-string-in-c
        template<typename T_vec, typename T_str> requires is_vector<T_vec, T_str> && is_string<T_str>
        [[nodiscard]] T_vec Split(const char* input, char delimiter);

        template<typename T_vec, typename T_str> requires is_vector<T_vec, T_str> && is_string<T_str>
        void Split(const char* input, char delimiter, T_vec& elems);

        /// http://stackoverflow.com/questions/216823/whats-the-best-way-to-trim-stdstring
        template<typename T_str = string> requires is_string<T_str>
        T_str& Ltrim(T_str& s);

        template<typename T_str = string>  requires is_string<T_str>
        T_str Ltrim(const T_str& s);

        template<typename T_str = string>  requires is_string<T_str>
        T_str& Rtrim(T_str& s);

        template<typename T_str = string>  requires is_string<T_str>
        T_str Rtrim(const T_str& s);

        template<typename T_str = string>  requires is_string<T_str>
        T_str& Trim(T_str& s);

        template<typename T_str = string>  requires is_string<T_str>
        T_str  Trim(const T_str& s);

        ALIAS_TEMPLATE_FUNCTION(StringFormat, fmt::sprintf)

        template<typename T>
        string to_string(T value);

        template<typename T_str = string> requires is_string<T_str>
        U32 LineCount(const T_str& str);

        void CStringRemoveChar(char* str, char charToRemove) noexcept;

        bool IsEmptyOrNull(const char* str) noexcept;

        [[nodiscard]] char *commaprint(U64 number) noexcept;
    } //namespace Util
} //namespace Divide

#endif //DVD_CORE_STRING_HELPER_H_

#include "StringHelper.inl"
