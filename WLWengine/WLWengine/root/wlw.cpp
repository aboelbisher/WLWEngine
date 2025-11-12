#include "wlw.h"

#include "rendering/rendering_driver.h"
#include "os/input_engine.h"

namespace wlw {

  class WLWEngineImpl : public WLWEngine {
  public:
    WLWEngineImpl() {}

    void Start(std::shared_ptr<wlw::scene::Window> window) override {
      windows_.insert({ last_window_id, window });
      last_window_id++;
			rendering_driver_->Initialize(window);
			input_engine_->AttachWindow(window);
    }

    int AttachWindow(std::shared_ptr<scene::Window > window) override {
			rendering_driver_->AttachWindow(window);
      input_engine_->AttachWindow(window);
      windows_.insert({ last_window_id, window });
      return last_window_id++;
    }

    void Iterate() override {
      for (auto& [_, window] : windows_) {
				rendering_driver_->DrawWindow(window);
      }
    }


  private:
    std::unordered_map<int, std::shared_ptr<scene::Window>> windows_;
    int last_window_id = 0;
  
    std::unique_ptr<rendering::RenderingDriver> rendering_driver_ = rendering::RenderingDriver::Create();
		std::unique_ptr<os::InputEngine> input_engine_ = os::InputEngine::Create();

  };

  std::unique_ptr<WLWEngine> WLWEngine::Create() {
    return std::make_unique<WLWEngineImpl>();
	}
  
} // wlw