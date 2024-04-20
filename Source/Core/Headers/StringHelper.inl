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
#ifndef DVD_CORE_STRING_HELPER_INL_
#define DVD_CORE_STRING_HELPER_INL_

#ifndef FMT_EXCEPTIONS
#define FMT_EXCEPTIONS 0
#endif

#include "Platform/File/Headers/ResourcePath.h"

namespace Divide
{
    namespace Util
    {
        template<typename T_vec, typename T_str> requires is_vector<T_vec, T_str> && is_string<T_str>
        void Split( const char* input, const char delimiter, T_vec& elems )
        {
            assert( input != nullptr );

            const T_str original( input );
            if ( !original.empty() )
            {
                {
                    size_t i = 0u;
                    const char* o = input;
                    for ( i = 0u; input[i]; (input[i] == delimiter) ? i++ : *(input++) )
                    {
                        NOP();
                    }
                    elems.resize( i + 1 );
                    input = o;
                }

                size_t idx = 0;
                typename T_str::const_iterator start = eastl::begin( original );
                typename T_str::const_iterator end = eastl::end( original );
                typename T_str::const_iterator next = eastl::find( start, end, delimiter );
                while ( next != end )
                {
                    elems[idx++] = { start, next };
                    start = next + 1;
                    next = eastl::find( start, end, delimiter );
                }
                elems[idx] = { start, next };
            }
            else
            {
                elems.clear();
            }
        }

        template<typename T_vec, typename T_str> requires is_vector<T_vec, T_str> && is_string<T_str>
        T_vec Split( const char* input, const char delimiter )
        {
            T_vec elems;
            Split<T_vec, T_str>( input, delimiter, elems );
            return elems;
        }

        template<typename T_str> requires is_string<T_str>
        bool IsNumber( const T_str& s )
        {
            return IsNumber( s.c_str() );
        }

        template<typename T_str> requires valid_replace_string<T_str>
        void GetPermutations( std::string_view subject, vector<T_str>& permutationContainer )
        {
            permutationContainer.clear();
            T_str tempCpy( subject );
            std::sort( std::begin( tempCpy ), std::end( tempCpy ) );
            do
            {
                permutationContainer.push_back( tempCpy );
            }
            while ( std::next_permutation( std::begin( tempCpy ), std::end( tempCpy ) ) );
        }

        template<typename T_str> requires valid_replace_string<T_str>
        bool ReplaceStringInPlace( T_str& subject, const std::span<const std::string_view> search, std::string_view replace, bool recursive )
        {
            bool ret = true;
            bool changed = true;
            while ( changed )
            {
                changed = false;
                for ( const std::string_view s : search )
                {
                    changed = ReplaceStringInPlace( subject, s, replace, recursive );
                    ret = changed || ret;

                }
            }

            return ret;
        }

        template<typename T_str> requires valid_replace_string<T_str>
        T_str ReplaceString( std::string_view subject, const std::span<const std::string_view> search, std::string_view replace, bool recursive )
        {
            T_str ret{ subject };
            bool changed = true;
            while ( changed )
            {
                changed = false;
                for ( const std::string_view s : search )
                {
                    changed = ReplaceStringInPlace( ret, s, replace, recursive ) || changed;
                }
            }

            return ret;
        }

        template<typename T_str> requires valid_replace_string<T_str>
        bool ReplaceStringInPlace( T_str& subject,
                                   const std::string_view search,
                                   const std::string_view replace,
                                   const bool recursive )
        {
            bool ret = false;
            bool changed = true;
            while ( changed )
            {
                changed = false;

                size_t pos = 0;
                while ( (pos = subject.find( search.data(), pos )) != T_str::npos )
                {
                    subject.replace( pos, search.length(), replace.data() );
                    pos += replace.length();
                    changed = true;
                    ret = true;
                }

                if ( !recursive )
                {
                    break;
                }
            }

            return ret;
        }

        template<>
        inline bool ReplaceStringInPlace( ResourcePath& subject,
                                          const std::string_view search,
                                          const std::string_view replace,
                                          const bool recursive )
        {
            string temp = subject.string();
            const bool ret = ReplaceStringInPlace( temp, search, replace, recursive );
            subject = ResourcePath{ temp };
            return ret;
        }

        template<typename T_str> requires valid_replace_string<T_str>
        T_str ReplaceString( std::string_view subject,
                             std::string_view search,
                             std::string_view replace,
                             bool recursive )
        {
            T_str ret{ subject };
            ReplaceStringInPlace( ret, search, replace, recursive );
            return ret;
        }

        inline string MakeXMLSafe( const std::string_view subject )
        {
            constexpr std::string_view InvalidXMLStrings[10] =
            {
                " ", "[", "]", "...", "..", ".", "/", "'\'", "<", ">"
            };

            return ReplaceString( subject, InvalidXMLStrings, "__" );
        }

        FORCE_INLINE ResourcePath MakeXMLSafe( const ResourcePath& subject )
        {
            return ResourcePath{ MakeXMLSafe( subject.string() ) };
        }

        inline bool BeginsWith( const std::string_view input, const std::string_view compare, bool ignoreWhitespace )
        {
            if ( ignoreWhitespace ) [[likely]]
            {
                const auto itBegin = input.find_first_not_of( " \t\n\r\f\v" );
                if ( itBegin != std::string_view::npos )
                {
                    return input.substr( itBegin ).starts_with( compare );
                }
            }

            return input.starts_with( compare );
        }


        template<typename T_str>  requires is_string<T_str>
        T_str GetTrailingCharacters( const T_str& input, size_t count )
        {
            const size_t inputLength = input.length();
            count = std::min( inputLength, count );
            assert( count > 0 );
            return input.substr( inputLength - count, inputLength ).data();
        }

        template<typename T_str>  requires is_string<T_str>
        T_str GetStartingCharacters( const T_str& input, size_t count )
        {
            const size_t inputLength = input.length();
            count = std::min( inputLength, count );
            assert( count > 0 );
            return input.substr( 0, inputLength - count );
        }

        inline bool CompareIgnoreCase( const char* a, const char* b ) noexcept
        {
            if (a == nullptr || b == nullptr)
            {
                return false;
            }

            return strcasecmp( a, b ) == 0;
        }

        inline bool CompareIgnoreCase( const std::string_view a, const std::string_view b ) noexcept
        {
            if (a.size() != b.size())
            {
                return false;
            }

            for ( size_t i = 0; i < a.size(); ++i )
            {
                if ( tolower( a[i] ) != tolower( b[i] ) )
                {
                    return false;
                }
            }

            return true;
        }

        template<typename T_str> requires is_string<T_str>
        U32 LineCount( const T_str& str )
        {
            return to_U32( std::count( std::cbegin( str ), std::cend( str ), '\n' ) ) + 1;
        }

        /// http://stackoverflow.com/questions/216823/whats-the-best-way-to-trim-stdstring
        template<typename T_str> requires is_string<T_str>
        T_str Ltrim( const T_str& s )
        {
            T_str temp( s );
            return Ltrim( temp );
        }

        template<typename T_str> requires is_string<T_str>
        T_str& Ltrim( T_str& s )
        {
            s.erase( s.begin(), std::find_if( s.begin(), s.end(), []( const T_str::value_type c ) noexcept
                                              {
                                                  return !std::isspace( c );
                                              } ) );
            return s;
        }

        template<typename T_str> requires is_string<T_str>
        T_str Rtrim( const T_str& s )
        {
            T_str temp( s );
            return Rtrim( temp );
        }

        template<typename T_str> requires is_string<T_str>
        T_str& Rtrim( T_str& s )
        {
            s.erase( std::find_if( s.rbegin(), s.rend(), []( const T_str::value_type c ) noexcept
                                   {
                                       return !std::isspace( c );
                                   } ).base(), s.end() );
            return s;
        }

        template<typename T_str> requires is_string<T_str>
        T_str& Trim( T_str& s )
        {
            return Ltrim( Rtrim( s ) );
        }

        template<typename T_str> requires is_string<T_str>
        T_str Trim( const T_str& s )
        {
            T_str temp( s );
            return Trim( temp );
        }
        
        template<typename T_str> requires is_string<T_str>
        bool GetLine( istringstream& input, T_str& line, const char delimiter )
        {
            if (std::getline(input, line, delimiter))
            {
                std::erase(line, '\r');
                std::erase(line, '\n');
                return true;
            }

            return false;
        }

        template<typename T>
        string to_string( T value )
        {
            return fmt::format( "{}", value );
        }

    } //namespace Util
} //namespace Divide

#endif //DVD_CORE_STRING_HELPER_INL_
