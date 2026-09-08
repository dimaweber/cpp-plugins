#include "example_log.h"
#include "host_api.h"
#include "plugin_loader.h"

#include <chrono>
#include <filesystem>
#include <thread>

int main(int argc, char** argv) {
    using namespace std::chrono_literals;

    const std::filesystem::path plugin_dir{argc > 1 ? argv[1] : "./plugins"};

    // Declaration order is the lifetime contract: the api is constructed first
    // and destroyed last, so it outlives every plugin the registry holds.
    HostApi api{{{"heartbeat.period_ms", "500"}}};
    loader::PluginRegistry registry{api};

    if (registry.load_dir(plugin_dir) != E_Status::Ok) {
        LOG_ERR("nothing to run");
        return 1;
    }

    static_cast<void>(registry.start_all());
    LOG_INFO("{} plugin(s) running", registry.size());

    std::this_thread::sleep_for(1200ms);

    LOG_INFO("shutting down after {} ms", api.uptime_ms());
    return 0;
}
