#pragma once
#include <memory>
#include <vector>
#include "core/vector2.h"

namespace wlw::utils {

class Texture {
public:

	Texture(const core::Vector2& size, int channels, const std::vector<unsigned char>& data)
		: size_(size), channels_(channels), data_(data) { }
	core::Vector2 GetSize() const { return size_; }
	int GetChannels() const { return channels_; }
	const unsigned char* GetData() const { return data_.data(); }

private:
	core::Vector2 size_;
	int channels_;
	std::vector<unsigned char> data_;

};
} // namespace wlw::utils