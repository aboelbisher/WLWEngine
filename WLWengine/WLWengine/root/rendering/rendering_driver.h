#pragma once

#include <memory>
#include <vector>
#include <array>
#include "rendering/index_buffer.h"
#include "rendering/vertex_buffer.h"
#include "core/vertex_2d.h"
#include "core/vertex_3d.h"
#include "utils/image_loader.h"
#include "rendering/texture.h"

#include "rendering/render_device.h"

#include "scene/window.h"

namespace wlw::rendering {
class RenderingDriver {
public:
  virtual ~RenderingDriver() = default;

	virtual bool Initialize(std::shared_ptr<scene::Window> window) = 0;
	virtual void AttachWindow(std::shared_ptr<scene::Window> window) = 0;
  virtual void DrawWindow(std::shared_ptr<scene::Window> window) = 0;

  virtual void SetViewport(int x, int y, int width, int height) = 0;
  virtual void Clear() = 0;

  virtual RenderDevice* GetDevice() = 0;

	static std::unique_ptr<RenderingDriver> Create(RenderDevice* device);
};
} // wlw::rendering