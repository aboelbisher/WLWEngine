#include <iostream>
#include "wlw/core/logger.h"
#include "image_loader.h"
#include "wlw/platform/platform.h"
#include "stb_image.h"
#include "wlw/core/vector2.h"
#include "wlw/rendering/texture.h"

namespace wlw::utils {

  std::unique_ptr<RawImage> ImageLoader::LoadImage(const std::string &file_path) {
    int width, height, channels_count;

    // Read through the platform layer so this also works for assets packed inside an APK.
    const auto file = platform::ReadFile(file_path);
    if (FAIL_IF(!file, "Failed to read image file: " << file_path)) {
      return nullptr;
    }
    unsigned char* data = stbi_load_from_memory(file->data(), static_cast<int>(file->size()), &width, &height,
                                                &channels_count, 0);
    if (FAIL_IF(data == nullptr, "Failed to decode image: " << file_path)) {
      return nullptr;
    }

    size_t total_size = (size_t)width * height * channels_count;
    std::vector<unsigned char> data_vector(data, data + total_size);
    stbi_image_free(data);

    return std::make_unique<RawImage>(core::Vector2{.x = static_cast<float>(width), .y = static_cast<float>(height)},
                                      channels_count, data_vector);
  }

} // namespace wlw::utils