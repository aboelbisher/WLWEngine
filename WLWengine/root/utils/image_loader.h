#pragma once

#include <memory>
#include <string>
#include "image.h"

namespace wlw::utils {
		class ImageLoader {
		public:
				static std::unique_ptr<Image> LoadImage(const std::string& file_path);
		};
} // namespace wlw::utils