#pragma once

#include <memory>
#include <optional>
#include <string>

#include "utils/texture.h"
#include "core/color.h"
#include "core/vector3.h"

namespace wlw::rendering {

	struct Lighting {
		core::Vector3 position;

		core::Color color = { 1.0f, 1.0f, 1.0f, 1.0f };
		float ambient_strength;
		float shininess;
	};


	class Material {
	public:

		virtual ~Material() = default;

		virtual void BindTexture() const = 0;
		virtual void GenerateTexture() = 0;
		virtual void UnbindTexture() const = 0;

		void SetTexture(std::shared_ptr<utils::Texture> texture) {
			texture_ = texture;
		}

		bool HasTexture() const {
			return texture_ != nullptr;
		}

		void SetLighting(const Lighting& lighting) {
			lighting_ = lighting;
		}

		std::optional<Lighting> GetLighting() const {
			return lighting_;
		}

		static std::unique_ptr<Material> Create();

		float metallic = 0.0f;
		float roughness = 1.0f;
		std::string name;

	protected:
		std::optional<Lighting> lighting_;
		std::shared_ptr<utils::Texture> texture_;

	};
} // namespace wlw::rendering