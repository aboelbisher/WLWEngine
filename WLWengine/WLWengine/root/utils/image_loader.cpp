#include <iostream>
#include "core/logger.h"
#include "image_loader.h"
#include "stb_image.h" // Include the header without the macro
#include "core/vector2.h"

namespace wlw::utils {

  std::unique_ptr<Texture> ImageLoader::LoadImage(const std::string& file_path) {
    int width, height, channels_count;

    unsigned char* data = stbi_load(file_path.c_str(), &width, &height, &channels_count, 0);
    if (FAIL_IF(data == nullptr, "Failed to load image: " + file_path)) {
      return nullptr;
    }

    size_t total_size = (size_t)width * height * channels_count;
    std::vector<unsigned char> data_vector(data,data + total_size);
    stbi_image_free(data);

    return std::make_unique<Texture>( core::Vector2{ .x = static_cast<float>(width), .y = static_cast<float>(height) }, channels_count, data_vector);
  }
} // namespace wlw::utils