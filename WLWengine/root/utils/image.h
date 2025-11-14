#pragma once
#include <corecrt_malloc.h>

namespace wlw::utils {
		class Image {
		public:
				Image(int width, int height, int channels, unsigned char* data)
					: width_(width), height_(height), channels_(channels), data_(data) {}
				~Image() {
					if (data_) {
						free(data_);
					}
				}
				int GetWidth() const { return width_; }
				int GetHeight() const { return height_; }
				int GetChannels() const { return channels_; }
				const unsigned char* GetData() const { return data_; }
		private:
				int width_;
				int height_;
				int channels_;
				unsigned char* data_;
		};
} // namespace wlw::utils