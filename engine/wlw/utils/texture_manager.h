#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include "image_loader.h"
#include "wlw/rendering/texture.h"

namespace wlw::utils {

class TextureManager {
public:
    static TextureManager& GetInstance() {
        static TextureManager instance;
        return instance;
    }

    std::shared_ptr<rendering::WTexture2D> LoadTexture(const std::string& file_path) {
        auto it = texture_cache_.find(file_path);
        if (it != texture_cache_.end()) {
            return std::static_pointer_cast<rendering::WTexture2D>(it->second);
        }

        auto raw_image = ImageLoader::LoadImage(file_path);
        if (raw_image) {
            auto texture = rendering::WTexture2D::FromImage(*raw_image);
            texture_cache_[file_path] = texture;
            return texture;
        }

        return nullptr;
    }

    void ClearCache() {
        texture_cache_.clear();
    }

private:
    TextureManager() = default;
    std::unordered_map<std::string, std::shared_ptr<rendering::WTexture>> texture_cache_;
};

} // namespace wlw::utils
