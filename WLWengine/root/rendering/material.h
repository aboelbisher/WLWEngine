#pragma once

#include <memory>

#include "utils/image.h"

namespace wlw::rendering {

		class Material {
		public:
			enum class MaterialType {
				BASIC,
				//PHONG,
				//BLINN_PHONG,
				//PBR
			};


			virtual ~Material() = default;

			virtual void BindTexture() const = 0;
			virtual void SetTexture(std::unique_ptr<utils::Image> image) = 0;
			virtual void GenerateTexture() = 0;
			virtual void UnbindTexture() const = 0;


			static std::unique_ptr<Material> Create();
		};
} // namespace wlw::rendering