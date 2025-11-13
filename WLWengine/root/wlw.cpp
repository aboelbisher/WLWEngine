#include "wlw.h"

#include "rendering_driver/rendering_driver.h"

namespace wlw {

  class WLWEngineImpl : public WLWEngine {
  public:
    WLWEngineImpl() {}

    void Start(std::shared_ptr<wlw::core::Window> window) override {

      //window->Initialize();
      windows_.insert({ last_window_id, window });
      last_window_id++;
			rendering_driver_->Initialize(window);
      AttachWindow(window);
    }

    int AttachWindow(std::shared_ptr<core::Window > window) override {
      window->Initialize();
			rendering_driver_->AttachWindow(window);
      windows_.insert({ last_window_id, window });
      return last_window_id++;
    }


    void Iterate() override {
      for (auto& [_, window] : windows_) {
				rendering_driver_->DrawWindow(window);
      }
    }


  private:
    std::unordered_map<int, std::shared_ptr<core::Window>> windows_;
		std::unique_ptr<rendering::RenderingDriver> rendering_driver_ = rendering::RenderingDriver::Create();
    int last_window_id = 0;
  };

  std::unique_ptr<WLWEngine> WLWEngine::Create() {
    return std::make_unique<WLWEngineImpl>();
	}
  
} // wlw