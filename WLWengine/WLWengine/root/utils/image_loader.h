#pragma once

#include <memory>
#include <string>
#include <vector>
#include <array>
#include "core/vector2.h"

namespace wlw::utils {

    struct RawImage {
        core::Vector2 size;
        int channels;
        std::vector<unsigned char> pixels;
    };

    class ImageLoader {
    public:
        static std::unique_ptr<RawImage> LoadImage(const std::string& file_path);
    };
} // namespace wlw::utils