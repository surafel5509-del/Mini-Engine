#pragma once
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace mini {

// Small, dependency-free decoded LDR image used by environment maps.
// Pixel layout is tightly packed RGB8, top-to-bottom rows.
struct ImageData {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb;  // canonical RGB8 pixels for environment maps
    std::vector<std::uint8_t> rgba; // present when source image carries alpha

    bool valid() const {
        if (width <= 0 || height <= 0) return false;
        const std::size_t w = static_cast<std::size_t>(width);
        const std::size_t h = static_cast<std::size_t>(height);
        if (w > std::numeric_limits<std::size_t>::max() / h / 4) return false;
        const std::size_t pixels = w * h;
        return rgb.size() == pixels * 3 && (rgba.empty() || rgba.size() == pixels * 4);
    }
};

// Loads binary PPM (P6), max value 1..255.
bool loadPpmImage(const std::string& path, ImageData& out);

// Loads P6 PPM, PNG, or JPEG into RGB8, preserving source alpha in RGBA8
// when present. This is used for LDR material textures; HDR formats are not
// accepted. Decoder implementation is vendored stb_image under its license.
bool loadImage(const std::string& path, ImageData& out);

} // namespace mini
