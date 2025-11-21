#pragma once
#include <memory>
#include "core/vector2.h"

namespace wlw::utils {
class Texture {
public:
	Texture(const core::Vector2& size, int channels, unsigned char* data)
		: size_(size), channels_(channels), data_(data) { }
	~Texture() {
		if (data_) {
			free(data_);
		}
	}
	core::Vector2 GetSize() const { return size_; }
	int GetChannels() const { return channels_; }
	const unsigned char* GetData() const { return data_; }
private:
	core::Vector2 size_;
	int channels_;
	unsigned char* data_;
};
} // namespace wlw::utils