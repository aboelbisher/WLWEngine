#pragma once

#include <memory>
#include <optional>
#include <string>

#include "wlw/rendering/texture.h"
#include "wlw/core/color.h"
#include "wlw/core/vector3.h"

namespace wlw::rendering {

	enum class LightType {
		Directional = 0,
		Point = 1,
		Spot = 2
	};

	struct Lighting {
		LightType type = LightType::Point;
		core::Vector3 position;
		core::Vector3 direction = { 0.0f, -1.0f, 0.0f };

		core::Color color = { 1.0f, 1.0f, 1.0f, 1.0f };
		float ambient_strength;
		float shininess;

		float constant = 1.0f;
		float linear = 0.09f;
		float quadratic = 0.032f;
		
		float cutOff = 0.976f;      // cos(12.5)
		float outerCutOff = 0.953f; // cos(17.5)
	};


	class ShaderMaterial;

	// The engine's own look: a texture, and (the older way) a light. For a game's own shader use
	// ShaderMaterial (shader_material.h), which nodes and meshes take the same way.
	class Material {
	public:

		virtual ~Material() = default;

		virtual void BindTexture() const = 0;
		virtual void UnbindTexture() const = 0;

		void SetTexture(std::shared_ptr<rendering::WTexture> texture) {
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

		// Non-null for a ShaderMaterial: the renderers draw it with its shader instead.
		virtual const ShaderMaterial* AsShaderMaterial() const { return nullptr; }

		static std::unique_ptr<Material> Create();

		float metallic = 0.0f;
		float shininess = 32.0f;  // specular highlight sharpness under scene lights (LightNode)
		float roughness = 1.0f;
		std::string name;

	protected:
		std::optional<Lighting> lighting_;
		std::shared_ptr<WTexture> texture_;

	};
} // namespace wlw::rendering