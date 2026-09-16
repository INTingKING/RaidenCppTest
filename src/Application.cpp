#include "Application.hpp"

#include "core/Log.hpp"
#include "core/Paths.hpp"
#include "platform/Window.hpp"
#include "renderer/VulkanRenderer.hpp"

namespace raiden {

Application::Application(int argc, char** argv) {
    argv0_ = (argc > 0) ? argv[0] : nullptr;
}

void Application::run() {
    Window window(WindowDesc{});
    const auto assets = findAssetRoot(executableDir(argv0_));
    RAIDEN_LOG("Assets: %s", assets.string().c_str());

    VulkanRenderer renderer(window.handle(), assets);
    window.onResize([&](int, int) { renderer.notifyResize(); });

    while (!window.shouldClose()) {
        window.poll();
        renderer.drawFrame(window.time());
    }
    renderer.waitIdle();
}

}  // namespace raiden
