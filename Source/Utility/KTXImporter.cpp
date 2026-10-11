#include "Headers/KTXImporter.h"

#include <png.h>
#include <turbojpeg.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_surface.h>

#include <algorithm>
#include <csetjmp>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace Divide::ImageTools
{
    namespace
    {
        constexpr size_t BOOKKEEPING_BYTES = 1024u * 1024u;
        constexpr size_t CODEC_BYTES = 64u * 1024u * 1024u;
        constexpr int JPEG_INTERMEDIATE_MIB = 32;

        bool Fail(std::string* error, const char* message)
        {
            if (error != nullptr) { *error = message; }
            return false;
        }

        bool ValidateLayout(const size_t width, const size_t height, const int components,
                            const int precision, const bool srgb,
                            const KTXAlphaInterpretation interpretation,
                            std::string* error)
        {
            if (width == 0u || height == 0u || width > std::numeric_limits<U16>::max() ||
                height > std::numeric_limits<U16>::max())
            {
                return Fail(error, "Source dimensions exceed the typed KTX limits");
            }
            if (components != 1 && components != 2 && components != 4)
            {
                return Fail(error, "Unsupported source channel layout");
            }
            if (srgb && (precision != 8 || components != 4))
            {
                return Fail(error, "sRGB requires 8-bit RGB/RGBA; high precision is not quantized");
            }
            if (components < 4 && interpretation != KTXAlphaInterpretation::DATA)
            {
                return Fail(error, "Grayscale R/RG channels require DATA alpha interpretation");
            }
            return true;
        }

        struct alignas(std::max_align_t) PNGAllocation
        {
            size_t _bytes;
        };

        // Mutated state lives outside the function containing setjmp. That
        // function and all libpng callbacks have only trivial automatic objects.
        struct PNGState
        {
            png_structp _png = nullptr;
            png_infop _info = nullptr;
            const unsigned char* _input = nullptr;
            size_t _inputBytes = 0u;
            size_t _offset = 0u;
            size_t _allocated = 0u;
            size_t _limit = 0u;
            size_t _workingLimit = 0u;
            unsigned char* _pixels = nullptr;
            png_bytepp _rows = nullptr;
            size_t _pixelBytes = 0u;
            size_t _decodedByteLimit = KTX_IMAGE_BYTE_LIMIT;
            png_uint_32 _width = 0u;
            png_uint_32 _height = 0u;
            int _precision = 0;
            int _components = 0;
            bool _synthetic = false;
            bool _data = false;
            bool _srgb = false;
            KTXAlphaInterpretation _interpretation = KTXAlphaInterpretation::UNKNOWN;
            char _error[256] = {};
        };

        void* PNGAllocate(PNGState* state, const size_t bytes)
        {
            if (bytes > state->_limit || sizeof(PNGAllocation) > state->_limit - bytes ||
                state->_allocated > state->_limit - bytes - sizeof(PNGAllocation))
            {
                return nullptr;
            }
            auto* allocation = static_cast<PNGAllocation*>(std::malloc(sizeof(PNGAllocation) + bytes));
            if (allocation == nullptr) { return nullptr; }
            allocation->_bytes = sizeof(PNGAllocation) + bytes;
            state->_allocated += allocation->_bytes;
            return allocation + 1;
        }

        void PNGFree(PNGState* state, void* memory)
        {
            if (memory == nullptr) { return; }
            auto* allocation = static_cast<PNGAllocation*>(memory) - 1;
            state->_allocated -= allocation->_bytes;
            std::free(allocation);
        }

        png_voidp PNGMalloc(png_structp png, const png_alloc_size_t bytes)
        {
            return PNGAllocate(static_cast<PNGState*>(png_get_mem_ptr(png)), bytes);
        }

        void PNGRelease(png_structp png, png_voidp memory)
        {
            PNGFree(static_cast<PNGState*>(png_get_mem_ptr(png)), memory);
        }

        void PNGError(png_structp png, png_const_charp message)
        {
            auto* state = static_cast<PNGState*>(png_get_error_ptr(png));
            std::snprintf(state->_error, sizeof(state->_error), "%s", message);
            png_longjmp(png, 1);
        }

        void PNGWarning(png_structp, png_const_charp)
        {
        }

        void PNGRead(png_structp png, png_bytep destination, const png_size_t bytes)
        {
            auto* state = static_cast<PNGState*>(png_get_io_ptr(png));
            if (bytes > state->_inputBytes - state->_offset)
            {
                png_error(png, "Truncated PNG source");
            }
            std::memcpy(destination, state->_input + state->_offset, bytes);
            state->_offset += bytes;
        }

        bool ReadPNG(PNGState* state)
        {
            state->_png = png_create_read_struct_2(PNG_LIBPNG_VER_STRING, state, PNGError,
                                                   PNGWarning, state, PNGMalloc, PNGRelease);
            if (state->_png == nullptr)
            {
                std::snprintf(state->_error, sizeof(state->_error), "Unable to allocate PNG decoder");
                return false;
            }
            if (setjmp(png_jmpbuf(state->_png)) != 0) { return false; }
            state->_info = png_create_info_struct(state->_png);
            if (state->_info == nullptr) { png_error(state->_png, "Unable to allocate PNG metadata"); }
            png_set_read_fn(state->_png, state, PNGRead);
            png_set_user_limits(state->_png, std::numeric_limits<U16>::max(),
                                std::numeric_limits<U16>::max());
            png_set_chunk_cache_max(state->_png, 32u);
            png_set_chunk_malloc_max(state->_png, BOOKKEEPING_BYTES);
            // Only structural/image chunks are needed. In particular, do not
            // decompress ICC/text metadata or apply colour-profile transforms.
            png_set_keep_unknown_chunks(state->_png, PNG_HANDLE_CHUNK_NEVER, nullptr, -1);
            png_set_crc_action(state->_png, PNG_CRC_ERROR_QUIT, PNG_CRC_ERROR_QUIT);
            png_read_info(state->_png, state->_info);
            state->_width = png_get_image_width(state->_png, state->_info);
            state->_height = png_get_image_height(state->_png, state->_info);
            state->_precision = png_get_bit_depth(state->_png, state->_info);
            const int color = png_get_color_type(state->_png, state->_info);
            const bool gray = color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA;
            const bool alpha = (color & PNG_COLOR_MASK_ALPHA) != 0 ||
                               png_get_valid(state->_png, state->_info, PNG_INFO_tRNS) != 0;
            const bool expandGrayAlpha = gray && alpha &&
                                         state->_interpretation == KTXAlphaInterpretation::OPACITY;
            state->_synthetic = !gray && !alpha;
            state->_data = gray && !expandGrayAlpha;
            state->_components = gray && !expandGrayAlpha ? (alpha ? 2 : 1) : 4;
            if (state->_precision < 8) { state->_precision = 8; }
            if (state->_srgb && (state->_precision != 8 || state->_components != 4))
            {
                png_error(state->_png, "sRGB requires 8-bit RGB/RGBA; high precision is not quantized");
            }
            if (alpha && state->_interpretation != KTXAlphaInterpretation::OPACITY &&
                state->_interpretation != KTXAlphaInterpretation::DATA)
            {
                png_error(state->_png, "Actual PNG alpha requires OPACITY or DATA interpretation");
            }
            if (color == PNG_COLOR_TYPE_PALETTE) { png_set_palette_to_rgb(state->_png); }
            if (expandGrayAlpha) { png_set_gray_to_rgb(state->_png); }
            if (gray && png_get_bit_depth(state->_png, state->_info) < 8)
            {
                png_set_expand_gray_1_2_4_to_8(state->_png);
            }
            if (png_get_valid(state->_png, state->_info, PNG_INFO_tRNS) != 0)
            {
                png_set_tRNS_to_alpha(state->_png);
            }
            if (state->_synthetic)
            {
                png_set_add_alpha(state->_png, state->_precision == 16 ? 65535u : 255u, PNG_FILLER_AFTER);
            }
            const size_t rowBytes = size_t(state->_width) * size_t(state->_components) *
                                    (state->_precision == 16 ? 2u : 1u);
            if (rowBytes == 0u || state->_height > state->_workingLimit / rowBytes)
            {
                png_error(state->_png, "Oversized PNG decoded payload");
            }
            state->_pixelBytes = rowBytes * state->_height;
            if (state->_pixelBytes > state->_decodedByteLimit)
            {
                png_error(state->_png, "PNG decoded payload exceeds the caller limit");
            }
            // Reserve typed output as well as the raw decode while bounding all
            // libpng allocations (including zlib and interlace scratch buffers).
            if (state->_pixelBytes >= state->_workingLimit / 2u)
            {
                png_error(state->_png, "PNG working memory exceeds the import limit");
            }
            state->_limit = std::min(state->_workingLimit - state->_pixelBytes,
                                    CODEC_BYTES + state->_pixelBytes);
            // Keep codec/bookkeeping within 64 MiB in addition to the raw
            // PNG plane; typed output has a separate reservation above.
            png_set_interlace_handling(state->_png);
            png_read_update_info(state->_png, state->_info);
            if (png_get_channels(state->_png, state->_info) != state->_components ||
                png_get_bit_depth(state->_png, state->_info) != state->_precision ||
                png_get_rowbytes(state->_png, state->_info) != rowBytes)
            {
                png_error(state->_png, "Unexpected PNG decoder layout");
            }
            state->_pixels = static_cast<unsigned char*>(PNGAllocate(state, state->_pixelBytes));
            state->_rows = static_cast<png_bytepp>(PNGAllocate(state, size_t(state->_height) * sizeof(png_bytep)));
            if (state->_pixels == nullptr || state->_rows == nullptr)
            {
                png_error(state->_png, "PNG working memory exceeds the import limit");
            }
            for (png_uint_32 y = 0u; y < state->_height; ++y)
            {
                state->_rows[y] = state->_pixels + size_t(state->_height - 1u - y) * rowBytes;
            }
            png_read_image(state->_png, state->_rows);
            png_read_end(state->_png, state->_info);
            return true;
        }

        struct PNGOwner
        {
            PNGState _state;
            ~PNGOwner()
            {
                if (_state._png != nullptr)
                {
                    png_destroy_read_struct(&_state._png, &_state._info, nullptr);
                }
                PNGFree(&_state, _state._rows);
                PNGFree(&_state, _state._pixels);
            }
        };

        struct JPEGDeleter
        {
            void operator()(void* handle) const noexcept { tj3Destroy(handle); }
        };
        using JPEGOwner = std::unique_ptr<void, JPEGDeleter>;

        struct SurfaceDeleter
        {
            void operator()(SDL_Surface* surface) const noexcept { SDL_DestroySurface(surface); }
        };
        using SurfaceOwner = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

        void SetLayout(KTXImage& image, const U16 width, const U16 height, const U8 components,
                       const bool sixteenBit, const bool srgb)
        {
            image._width = width;
            image._height = height;
            image._components = components;
            image._srgb = srgb;
            image._format = components == 1u ? GFXImageFormat::RED :
                            components == 2u ? GFXImageFormat::RG : GFXImageFormat::RGBA;
            image._componentType = sixteenBit ? KTXComponentType::UNORM_16 : KTXComponentType::UNORM_8;
            image._dataType = sixteenBit ? GFXDataFormat::UNSIGNED_SHORT : GFXDataFormat::UNSIGNED_BYTE;
            image._subresources.emplace_back();
            image._subresources.back()._width = width;
            image._subresources.back()._height = height;
        }

        bool DecodeBMP(const std::span<const Byte> input, const bool srgb,
                       const size_t decodedByteLimit, KTXImage& image, std::string* error)
        {
            if (input.size() < 54u || input[0] != Byte{'B'} || input[1] != Byte{'M'})
            {
                return Fail(error, "Invalid or truncated BMP header");
            }
            const auto readLE = [&](const size_t offset, const size_t count)
            {
                U32 value = 0u;
                for (size_t i = 0u; i < count; ++i)
                {
                    value |= U32(std::to_integer<U8>(input[offset + i])) << (8u * i);
                }
                return value;
            };
            // Accept only BITMAPINFOHEADER RGB24. BI_RGB 32-bit alpha is
            // ambiguous and bitfield/V4/V5 alpha needs a separate contract.
            if (readLE(14u, 4u) != 40u || readLE(26u, 2u) != 1u ||
                readLE(28u, 2u) != 24u || readLE(30u, 4u) != 0u)
            {
                return Fail(error, "Only uncompressed 24-bit RGB BMP sources are supported");
            }
            const U32 width = readLE(18u, 4u);
            const U32 rawHeight = readLE(22u, 4u);
            const U32 height = (rawHeight & 0x80000000u) != 0u ? 0u - rawHeight : rawHeight;
            if (!ValidateLayout(width, height, 4, 8, srgb, KTXAlphaInterpretation::SYNTHETIC, error))
            {
                return false;
            }
            const size_t rowBytes = (size_t(width) * 3u + 3u) & ~size_t(3u);
            const size_t sourceBytes = rowBytes * height;
            const size_t decodedBytes = size_t(width) * height * 4u;
            const size_t pixelOffset = readLE(10u, 4u);
            if (pixelOffset < 54u || pixelOffset > input.size() ||
                sourceBytes > input.size() - pixelOffset)
            {
                return Fail(error, "Truncated or overlapping BMP pixels");
            }
            if (decodedBytes > decodedByteLimit ||
                input.size() + sourceBytes + decodedBytes * 2u + BOOKKEEPING_BYTES >= KTX_IMAGE_BYTE_LIMIT)
            {
                return Fail(error, "BMP working memory exceeds the import limit");
            }
            SDL_IOStream* stream = SDL_IOFromConstMem(input.data(), input.size());
            if (stream == nullptr) { return Fail(error, SDL_GetError()); }
            SurfaceOwner source(SDL_LoadBMP_IO(stream, true));
            if (!source) { return Fail(error, SDL_GetError()); }
            if (source->w != static_cast<int>(width) || source->h != static_cast<int>(height))
            {
                return Fail(error, "Unexpected BMP decoder dimensions");
            }
            SurfaceOwner rgba(SDL_ConvertSurface(source.get(), SDL_PIXELFORMAT_RGBA32));
            if (!rgba) { return Fail(error, SDL_GetError()); }
            source.reset();
            if (rgba->pitch < static_cast<int>(size_t(width) * 4u) || rgba->pixels == nullptr ||
                SDL_MUSTLOCK(rgba.get()))
            {
                return Fail(error, "Unsupported BMP surface storage");
            }
            SetLayout(image, static_cast<U16>(width), static_cast<U16>(height), 4u, false, srgb);
            auto& pixels = image._subresources.front()._pixels.emplace<std::vector<U8>>(decodedBytes);
            const auto* rows = static_cast<const U8*>(rgba->pixels);
            for (size_t y = 0u; y < height; ++y)
            {
                std::memcpy(pixels.data() + y * size_t(width) * 4u,
                            rows + (size_t(height) - 1u - y) * size_t(rgba->pitch), size_t(width) * 4u);
            }
            for (size_t i = 3u; i < pixels.size(); i += 4u) { pixels[i] = 255u; }
            return ClassifyKTXImageAlpha(image, KTXAlphaInterpretation::SYNTHETIC) ||
                   Fail(error, "Invalid BMP synthetic alpha");
        }

        bool DecodePNG(const std::span<const Byte> input, const bool srgb,
                       const KTXAlphaInterpretation interpretation, KTXImage& image,
                       std::string* error, const size_t decodedByteLimit)
        {
            PNGOwner owner;
            PNGState& state = owner._state;
            state._input = reinterpret_cast<const unsigned char*>(input.data());
            state._inputBytes = input.size();
            state._workingLimit = KTX_IMAGE_BYTE_LIMIT - input.size() - BOOKKEEPING_BYTES;
            state._limit = std::min(state._workingLimit, CODEC_BYTES);
            state._srgb = srgb;
            state._interpretation = interpretation;
            state._decodedByteLimit = decodedByteLimit;
            if (!ReadPNG(&state)) { return Fail(error, state._error); }
            png_destroy_read_struct(&state._png, &state._info, nullptr);
            PNGFree(&state, state._rows);
            state._rows = nullptr;
            SetLayout(image, static_cast<U16>(state._width), static_cast<U16>(state._height),
                      static_cast<U8>(state._components), state._precision == 16, srgb);
            if (state._precision == 16)
            {
                std::vector<U16> pixels(state._pixelBytes / 2u);
                // libpng's default 16-bit output is big-endian, regardless of
                // host endianness. Assemble numeric samples rather than swap.
                for (size_t i = 0u; i < pixels.size(); ++i)
                {
                    pixels[i] = static_cast<U16>((U16(state._pixels[2u * i]) << 8u) |
                                                 state._pixels[2u * i + 1u]);
                }
                image._subresources.front()._pixels = std::move(pixels);
            }
            else
            {
                image._subresources.front()._pixels =
                    std::vector<U8>(state._pixels, state._pixels + state._pixelBytes);
            }
            return ClassifyKTXImageAlpha(image, state._synthetic ? KTXAlphaInterpretation::SYNTHETIC :
                                               state._data ? KTXAlphaInterpretation::DATA : interpretation) ||
                   Fail(error, "Invalid PNG alpha interpretation");
        }

        bool DecodeJPEG(const std::span<const Byte> input, const bool srgb,
                        const KTXAlphaInterpretation interpretation, KTXImage& image,
                        std::string* error, const size_t decodedByteLimit)
        {
            JPEGOwner decoder(tj3Init(TJINIT_DECOMPRESS));
            if (!decoder) { return Fail(error, "Unable to allocate JPEG decoder"); }
            void* handle = decoder.get();
            // MAXMEMORY bounds intermediate coefficients; reserve another
            // 32 MiB for codec structures, scanlines, and entropy workspaces.
            if (tj3Set(handle, TJPARAM_MAXMEMORY, JPEG_INTERMEDIATE_MIB) != 0 ||
                tj3Set(handle, TJPARAM_MAXPIXELS, 16 * 1024 * 1024) != 0 ||
                tj3Set(handle, TJPARAM_SCANLIMIT, 100) != 0 ||
                tj3Set(handle, TJPARAM_SAVEMARKERS, 0) != 0 ||
                tj3Set(handle, TJPARAM_STOPONWARNING, 1) != 0 ||
                tj3Set(handle, TJPARAM_BOTTOMUP, 1) != 0)
            {
                return Fail(error, tj3GetErrorStr(handle));
            }
            const auto* source = reinterpret_cast<const unsigned char*>(input.data());
            if (tj3DecompressHeader(handle, source, input.size()) != 0)
            {
                return Fail(error, tj3GetErrorStr(handle));
            }
            const int width = tj3Get(handle, TJPARAM_JPEGWIDTH);
            const int height = tj3Get(handle, TJPARAM_JPEGHEIGHT);
            const int precision = tj3Get(handle, TJPARAM_PRECISION);
            const int colorspace = tj3Get(handle, TJPARAM_COLORSPACE);
            if (precision != 8 && precision != 12 && precision != 16)
            {
                return Fail(error, "Only 8-, 12-, and 16-bit JPEG sources are supported");
            }
            if (colorspace != TJCS_GRAY && colorspace != TJCS_RGB && colorspace != TJCS_YCbCr)
            {
                return Fail(error, "JPEG CMYK/YCCK and unknown colour spaces are not supported");
            }
            const int components = colorspace == TJCS_GRAY ? 1 : 4;
            if (width <= 0 || height <= 0)
            {
                return Fail(error, "Invalid JPEG dimensions");
            }
            if (!ValidateLayout(static_cast<size_t>(width), static_cast<size_t>(height),
                                components, precision, srgb,
                                components == 1 ? KTXAlphaInterpretation::DATA : interpretation, error))
            {
                return false;
            }
            const size_t samples = size_t(width) * size_t(height) * size_t(components);
            const size_t bytes = samples * (precision == 8 ? 1u : 2u);
            if (bytes > decodedByteLimit)
            {
                return Fail(error, "JPEG decoded payload exceeds the caller limit");
            }
            if (input.size() + bytes + CODEC_BYTES + BOOKKEEPING_BYTES >= KTX_IMAGE_BYTE_LIMIT)
            {
                return Fail(error, "JPEG working memory exceeds the import limit");
            }
            SetLayout(image, static_cast<U16>(width), static_cast<U16>(height),
                      static_cast<U8>(components), precision != 8, srgb);
            const int pixelFormat = components == 1 ? TJPF_GRAY : TJPF_RGBA;
            int status = 0;
            if (precision == 8)
            {
                auto& pixels = image._subresources.front()._pixels.emplace<std::vector<U8>>(samples);
                status = tj3Decompress8(handle, source, input.size(), pixels.data(), 0, pixelFormat);
            }
            else
            {
                auto& pixels = image._subresources.front()._pixels.emplace<std::vector<U16>>(samples);
                if (precision == 12)
                {
                    static_assert(sizeof(short) == sizeof(U16));
                    status = tj3Decompress12(handle, source, input.size(),
                                            reinterpret_cast<short*>(pixels.data()), 0, pixelFormat);
                    if (status == 0)
                    {
                        for (U16& sample : pixels)
                        {
                            sample = static_cast<U16>((U32(sample) * 65535u + 2047u) / 4095u);
                        }
                    }
                }
                else
                {
                    static_assert(sizeof(unsigned short) == sizeof(U16));
                    status = tj3Decompress16(handle, source, input.size(), pixels.data(), 0, pixelFormat);
                }
                if (components == 4)
                {
                    // Ensure the generated alpha is full-scale for typed UNORM_16.
                    for (size_t i = 3u; i < samples; i += 4u) { pixels[i] = 65535u; }
                }
            }
            if (status != 0) { return Fail(error, tj3GetErrorStr(handle)); }
            return ClassifyKTXImageAlpha(image, components == 4 ? KTXAlphaInterpretation::SYNTHETIC :
                                                                 KTXAlphaInterpretation::DATA) ||
                   Fail(error, "Invalid JPEG alpha interpretation");
        }
    }

    bool DecodeKTXSource(const std::span<const Byte> input, const KTXSourceFormat format,
                         const bool srgb, const KTXAlphaInterpretation alphaInterpretation,
                         KTXImage& output, std::string* error, const size_t decodedByteLimit)
    {
        if (error != nullptr) { error->clear(); }
        if (input.empty() || input.size() >= KTX_IMAGE_BYTE_LIMIT - CODEC_BYTES - BOOKKEEPING_BYTES)
        {
            return Fail(error, "Empty or oversized source image");
        }
        if (alphaInterpretation != KTXAlphaInterpretation::UNKNOWN &&
            alphaInterpretation != KTXAlphaInterpretation::OPACITY &&
            alphaInterpretation != KTXAlphaInterpretation::DATA &&
            alphaInterpretation != KTXAlphaInterpretation::SYNTHETIC)
        {
            return Fail(error, "Invalid source alpha interpretation");
        }
        try
        {
            KTXImage image;
            bool decoded = false;
            switch (format)
            {
                case KTXSourceFormat::PNG:
                    decoded = DecodePNG(input, srgb, alphaInterpretation, image, error, decodedByteLimit);
                    break;
                case KTXSourceFormat::JPEG:
                    decoded = DecodeJPEG(input, srgb, alphaInterpretation, image, error, decodedByteLimit);
                    break;
                case KTXSourceFormat::BMP:
                    decoded = DecodeBMP(input, srgb, decodedByteLimit, image, error);
                    break;
                default: return Fail(error, "Unknown source image format");
            }
            if (!decoded) { return false; }
            output = std::move(image);
            return true;
        }
        catch (const std::bad_alloc&)
        {
            return Fail(error, "Source image allocation failed");
        }
        catch (const std::exception&)
        {
            return Fail(error, "Source image decoding failed");
        }
    }
}
