#pragma once

#include <memory>
#include <string>
#include "texture.h"

namespace wlw::utils {
		class ImageLoader {
		public:
				static std::unique_ptr<Texture> LoadImage(const std::string& file_path);
		};
} // namespace wlw::utils