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
#ifndef _UTILITY_IMAGETOOLS_FWD_H
#define _UTILITY_IMAGETOOLS_FWD_H

namespace Divide {
    namespace ImageTools {

        void OnStartup(bool upperLeftOrigin);
        void OnShutdown();

        [[nodiscard]] bool UseUpperLeftOrigin() noexcept;

        enum class MipMapFilter : U8 {
            BOX,
            TRIANGLE,
            KAISER,
            COUNT
        };

        enum class ImageOutputFormat : U8 {
            BC1, //Will be BC1n for normal maps
            BC1a,
            BC2,
            BC3,//Will be BC3n for normal maps
            BC4,
            BC5,
            BC6,
            BC7,
            //BC3_RGBM, //Not supported
            AUTO,  // BC7 for textures, BC5 for normal maps, BC4 single channel images
            COUNT
        };

        struct ImportOptions {
            bool _useDDSCache = true;
            bool _waitForDDSConversion = false; //<If false, we will load the src image and convert to DDS in the background. If true, we will wait for the conversion first and load that instead
            bool _skipMipMaps = false;
            bool _isNormalMap = false;
            bool _fastCompression = false;
            bool _outputSRGB = false;
            bool _alphaChannelTransparency = true; //< If false, the alpha channel represents arbitrary data (e.g. in splatmaps)
            MipMapFilter _mipFilter = MipMapFilter::KAISER;
            ImageOutputFormat _outputFormat = ImageOutputFormat::AUTO;
        };

        FWD_DECLARE_MANAGED_STRUCT(LayerData);


        enum class SaveImageFormat : U8 {
            PNG,
            BMP,
            TGA,
            //HDR, /// Use special function for this!
            JPG,
            COUNT
        };

        /// Save an image to file of the desired format. Only R/RG/RGB/RGBA 8 bits per pixel data is supported as input data.
        bool SaveImage(const ResourcePath& filename, U16 width, U16 height, U8 numberOfComponents, U8 bytesPerPixel, const bool sourceIsBGR, const Byte* imageData, SaveImageFormat format);
        /// Save an HDR image to file of the desired format.
        bool SaveImageHDR(const ResourcePath& filename, U16 width, U16 height, U8 numberOfComponents, U8 bytesPerPixel, const bool sourceIsBGR, const F32* imageData);

    } //namespace ImageTools
} //namespace Divide

#endif //_UTILITY_IMAGETOOLS_FWD_H