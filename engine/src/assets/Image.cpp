#include "mini/assets/Image.h"
#include <cctype>
#include <fstream>
#include <limits>
#include <string>

namespace mini {
namespace {

bool readToken(std::istream& input, std::string& token, bool consumeLineEnding = false) {
    token.clear();
    char c = 0;
    while (input.get(c)) {
        if (c == '#') {
            input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            continue;
        }
        if (!std::isspace(static_cast<unsigned char>(c))) {
            token.push_back(c);
            break;
        }
    }
    if (token.empty()) return false;
    while (input.get(c)) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (consumeLineEnding && c == '\r' && input.peek() == '\n') input.get();
            return true;
        }
        token.push_back(c);
    }
    return true;
}

} // namespace

bool loadPpmImage(const std::string& path, ImageData& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    std::string token;
    if (!readToken(file, token) || token != "P6") return false;
    int width = 0, height = 0, maximum = 0;
    try {
        if (!readToken(file, token)) return false;
        width = std::stoi(token);
        if (!readToken(file, token)) return false;
        height = std::stoi(token);
        if (!readToken(file, token, true)) return false;
        maximum = std::stoi(token);
    } catch (...) {
        return false;
    }
    if (width <= 0 || height <= 0 || maximum <= 0 || maximum > 255) return false;

    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t h = static_cast<std::size_t>(height);
    if (w > std::numeric_limits<std::size_t>::max() / h / 3) return false;
    const std::size_t byteCount = w * h * 3;
    if (byteCount > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
        return false;

    ImageData image;
    image.width = width;
    image.height = height;
    image.rgb.resize(byteCount);
    if (!file.read(reinterpret_cast<char*>(image.rgb.data()),
                   static_cast<std::streamsize>(byteCount))) return false;

    if (maximum != 255) {
        for (std::uint8_t& channel : image.rgb) {
            channel = static_cast<std::uint8_t>(
                (static_cast<unsigned>(channel) * 255u) / static_cast<unsigned>(maximum));
        }
    }
    out = std::move(image);
    return true;
}

} // namespace mini

#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#define STBI_MAX_DIMENSIONS 16384
#include "../../third_party/stb_image.h"

namespace mini {

bool loadImage(const std::string& path, ImageData& out) {
    std::ifstream probe(path, std::ios::binary);
    if (!probe) return false;
    char magic[2] = {};
    probe.read(magic, 2);
    probe.close();
    if (magic[0] == 'P' && magic[1] == '6') return loadPpmImage(path, out);

    int width = 0, height = 0, sourceChannels = 0;
    stbi_uc* decoded = stbi_load(path.c_str(), &width, &height, &sourceChannels, 0);
    if (!decoded || width <= 0 || height <= 0 || sourceChannels < 1 || sourceChannels > 4) {
        if (decoded) stbi_image_free(decoded);
        return false;
    }
    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t h = static_cast<std::size_t>(height);
    if (w > std::numeric_limits<std::size_t>::max() / h / 4) {
        stbi_image_free(decoded);
        return false;
    }
    const std::size_t pixels = w * h;
    ImageData image;
    image.width = width;
    image.height = height;
    image.rgb.resize(pixels * 3);
    const bool hasAlpha = sourceChannels == 2 || sourceChannels == 4;
    if (hasAlpha) image.rgba.resize(pixels * 4);
    for (std::size_t i = 0; i < pixels; ++i) {
        std::uint8_t r = 0, g = 0, b = 0, a = 255;
        if (sourceChannels == 1 || sourceChannels == 2) {
            r = g = b = decoded[i * static_cast<std::size_t>(sourceChannels)];
            if (sourceChannels == 2) a = decoded[i * 2 + 1];
        } else {
            const std::size_t source = i * static_cast<std::size_t>(sourceChannels);
            r = decoded[source]; g = decoded[source + 1]; b = decoded[source + 2];
            if (sourceChannels == 4) a = decoded[source + 3];
        }
        image.rgb[i * 3] = r;
        image.rgb[i * 3 + 1] = g;
        image.rgb[i * 3 + 2] = b;
        if (hasAlpha) {
            image.rgba[i * 4] = r;
            image.rgba[i * 4 + 1] = g;
            image.rgba[i * 4 + 2] = b;
            image.rgba[i * 4 + 3] = a;
        }
    }
    stbi_image_free(decoded);
    out = std::move(image);
    return out.valid();
}

} // namespace mini
