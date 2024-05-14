

#include "Headers/TextureDescriptor.h"

namespace Divide {
    
    [[nodiscard]] bool IsCompressed(const GFXImageFormat format) noexcept {
        return format == GFXImageFormat::BC1  ||
               format == GFXImageFormat::BC1a ||
               format == GFXImageFormat::BC2  ||
               format == GFXImageFormat::BC3  ||
               format == GFXImageFormat::BC3n ||
               format == GFXImageFormat::BC4s ||
               format == GFXImageFormat::BC4u ||
               format == GFXImageFormat::BC5s ||
               format == GFXImageFormat::BC5u ||
               format == GFXImageFormat::BC6s ||
               format == GFXImageFormat::BC6u ||
               format == GFXImageFormat::BC7;
    }

    [[nodiscard]] bool HasAlphaChannel(const GFXImageFormat format) noexcept {
        return format == GFXImageFormat::BC1a ||
               format == GFXImageFormat::BC2  ||
               format == GFXImageFormat::BC3  ||
               format == GFXImageFormat::BC3n ||
               format == GFXImageFormat::BC7  ||
               format == GFXImageFormat::BGRA ||
               format == GFXImageFormat::RGBA;
    }

} //namespace Divide
