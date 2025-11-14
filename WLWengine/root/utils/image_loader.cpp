#include <iostream>
#include "core/logger.h"
#include "image_loader.h"
#include "stb_image.h" // Include the header without the macro

namespace wlw::utils {

  std::shared_ptr<Image> ImageLoader::LoadImage(const std::string& file_path) {
    int width, height, channels_count;

    // Replace "path/to/your/image.png" with the actual path
    unsigned char* data = stbi_load(file_path.c_str(), &width, &height, &channels_count, 0);
    if (FAIL_IF(data == nullptr, "Failed to load image: " + file_path)) {
      return nullptr;
    }
    return std::make_shared<Image>(width, height, channels_count, data);
  }
} // namespace wlw::utils