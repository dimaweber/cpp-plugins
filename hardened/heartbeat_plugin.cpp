#include "plugin_abi.h"

#include <atomic>
#include <charconv>
#include <chrono>
#include <new>
#include <string>
#include <thread>

namespace {

class HeartbeatPlugin final : public plugin_i {
public:
    explicit HeartbeatPlugin(api_i& api) noexcept : api_{&api} {}

    ~HeartbeatPlugin() override { shutdown(); }

    [[nodiscard]] abi_str name() const noexcept override { return to_abi("heartbeat"); }

    [[nodiscard]] E_Status start() noexcept override {
        if (worker_.joinable()) return E_Status::Ok;
        api_->log(LogLevel::Debug, to_abi("heartbeat starting"));
        running_.store(true, std::memory_order_relaxed);
        worker_ = std::thread{[this] { run(); }};
        return E_Status::Ok;
    }

    [[nodiscard]] E_Status stop() noexcept override {
        shutdown();
        return E_Status::Ok;
    }

private:
    void shutdown() noexcept {
        running_.store(false, std::memory_order_relaxed);
        if (worker_.joinable()) worker_.join();
    }

    void run() noexcept {
        const auto period = std::chrono::milliseconds{period_ms()};
        while (running_.load(std::memory_order_relaxed)) {
            const std::string msg = "tick at " + std::to_string(api_->uptime_ms()) + " ms";
            api_->log(LogLevel::Info, to_abi(msg));
            std::this_thread::sleep_for(period);
        }
        api_->log(LogLevel::Debug, to_abi("heartbeat thread left"));
    }

    [[nodiscard]] unsigned period_ms() const noexcept {
        const std::string_view raw = to_sv(api_->config(to_abi("heartbeat.period_ms")));
        unsigned value{500};
        std::from_chars(raw.data(), raw.data() + raw.size(), value);
        return value;
    }

    api_i* api_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace

extern "C" {

PLUGIN_API std::uint32_t plugin_abi_version() noexcept { return kPluginAbiVersion; }

PLUGIN_API plugin_i* plugin_create(api_i& api) noexcept { return new (std::nothrow) HeartbeatPlugin{api}; }

PLUGIN_API void plugin_destroy(plugin_i* instance) noexcept { delete instance; }

}  // extern "C"
