#pragma once

#include <memory>
#include <optional>

#include "utils/image.h"
#include "core/color.h"
#include "core/Vector3.h"

namespace wlw::rendering {

	struct Lighting {
		core::Vector3 position;
		core::Color color;

		float ambient_strength;
		float shininess;
	};


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


		void SetLighting(const Lighting& lighting) {
			lighting_ = lighting;
		}

		std::optional<Lighting> GetLighting() const {
			return lighting_;
		}

		static std::unique_ptr<Material> Create();


	private:
		std::optional<Lighting> lighting_;


	};
} // namespace wlw::rendering