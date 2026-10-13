#include "wlw/wlw.h"

#include "wlw/rendering/rendering_driver.h"
#include "wlw/rendering/render_device.h"
#include "wlw/platform/platform.h"

#include <unordered_map>

namespace wlw {

  class WLWEngineImpl : public WLWEngine {
  public:
    WLWEngineImpl() {
        render_device_ = rendering::RenderDevice::Create();
        rendering_driver_ = rendering::RenderingDriver::Create(render_device_.get());
    }

    // Shutdown order matters: GPU resources must be freed while a GL context exists,
    // and platform::Shutdown() (in the rendering driver) must come last.
    ~WLWEngineImpl() override {
      if (platform::IsAlive()) {
        for (auto& [_, window] : windows_) {
          window->MakeContextCurrent();
          window->ReleaseScenes();     // frees meshes/textures/viewports nobody else holds
        }
      }
      rendering_driver_.reset();       // frees shaders, then shuts the platform down
      windows_.clear();
      render_device_.reset();
    }

    bool Start(std::shared_ptr<wlw::scene::Window> window) override {
      windows_.insert({ last_window_id, window });
      last_window_id++;
      return rendering_driver_->Initialize(window);
    }

    int AttachWindow(std::shared_ptr<scene::Window > window) override {
      rendering_driver_->AttachWindow(window);
      windows_.insert({ last_window_id, window });
      return last_window_id++;
    }

    // One frame: draw + present every window, then poll input for the next frame.
    void Iterate() override {
      for (auto& [_, window] : windows_) {
        if (window->HasSurface()) rendering_driver_->DrawWindow(window);
      }
      // Clear last frame's input events on every window first, then poll the OS once:
      // on desktop one poll delivers events for all windows.
      for (auto& [_, window] : windows_) {
        window->GetInput().BeginFrame();
      }
      for (auto& [_, window] : windows_) {
        window->ProcessEvents();
      }
    }

    rendering::RenderingDriver* GetRenderingDriver() override {
        return rendering_driver_.get();
    }

    rendering::RenderDevice* GetRenderDevice() override {
        return render_device_.get();
    }


  private:
    std::unordered_map<int, std::shared_ptr<scene::Window>> windows_;
    int last_window_id = 0;
  
    std::unique_ptr<rendering::RenderDevice> render_device_;
    std::unique_ptr<rendering::RenderingDriver> rendering_driver_;

  };

  std::unique_ptr<WLWEngine> WLWEngine::Create() {
    return std::make_unique<WLWEngineImpl>();
	}
  
} // wlw