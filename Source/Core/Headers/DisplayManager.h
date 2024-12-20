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
#ifndef DVD_CORE_DISPLAY_MANAGER_H_
#define DVD_CORE_DISPLAY_MANAGER_H_

namespace Divide
{
    class GL_API;
    class VK_API;
    class WindowManager;

    namespace Attorney
    {
        class DisplayManagerApplication;
        class DisplayManagerRenderingAPI;
        class DisplayManagerWindowManager;
    } //namespace Attorney


    struct DisplayManager
    {
        struct OutputDisplayProperties
        {
            string _formatName{};
            vec2<U16> _resolution{ 1u };
            U8 _bitsPerPixel{ 8u };
            U8 _maxRefreshRate{ 24u }; ///< As returned by SDL_GetPixelFormatName
        };

        friend class Attorney::DisplayManagerWindowManager;
        friend class Attorney::DisplayManagerRenderingAPI;
        friend class Attorney::DisplayManagerApplication;

        static constexpr U8 g_maxDisplayOutputs = 4u;

        using OutputDisplayPropertiesContainer = vector<OutputDisplayProperties>;

        [[nodiscard]] static const OutputDisplayPropertiesContainer& GetDisplayModes(const size_t displayIndex) noexcept;
        [[nodiscard]] static U8 ActiveDisplayCount() noexcept;
        [[nodiscard]] static U8 MaxMSAASamples() noexcept;

    private:
        static void MaxMSAASamples(const U8 maxSampleCount) noexcept;
        static void SetActiveDisplayCount(const U8 displayCount);
        static void RegisterDisplayMode(const U8 displayIndex, const OutputDisplayProperties& mode);

        static void Reset() noexcept;

    private:
        static U8 s_activeDisplayCount;
        static U8 s_maxMSAASAmples;
        static std::array<OutputDisplayPropertiesContainer, g_maxDisplayOutputs> s_supportedDisplayModes;
    };

    bool operator==(const DisplayManager::OutputDisplayProperties& lhs, const DisplayManager::OutputDisplayProperties& rhs) noexcept;

    namespace Attorney
    {
        class DisplayManagerWindowManager
        {
            static void SetActiveDisplayCount(const U8 displayCount)
            {
                DisplayManager::SetActiveDisplayCount(displayCount);
            }

            static void RegisterDisplayMode(const U8 displayIndex, const DisplayManager::OutputDisplayProperties& mode)
            {
                DisplayManager::RegisterDisplayMode(displayIndex, mode);
            }

            friend class Divide::WindowManager;
        };

        class DisplayManagerRenderingAPI
        {
            static void MaxMSAASamples(const U8 maxSampleCount) noexcept
            {
                DisplayManager::MaxMSAASamples(maxSampleCount);
            }

            friend class Divide::GL_API;
            friend class Divide::VK_API;
        };

        class DisplayManagerApplication
        {
            static void Reset() noexcept
            {
                DisplayManager::Reset();
            }

            friend class Divide::Application;
        };
    } //namespace Attorney
} //namespace Divide

#endif //DVD_CORE_DISPLAY_MANAGER_H_

#include "DisplayManager.inl"