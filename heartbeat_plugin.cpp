#include "plugin_abi.h"

#include <atomic>
#include <charconv>
#include <chrono>
#include <new>
#include <string>
#include <thread>

namespace {

/// Logs through the host api from its own thread until stopped.
class HeartbeatPlugin final : public plugin_i {
public:
    explicit HeartbeatPlugin(api_i& api) noexcept : api_{&api} {}

    ~HeartbeatPlugin() override { shutdown(); }

    [[nodiscard]] std::string_view name() const noexcept override { return "heartbeat"; }

    [[nodiscard]] E_Status start() noexcept override {
        if (worker_.joinable()) return E_Status::Ok;
        api_->log(LogLevel::Debug, "heartbeat starting");
        running_.store(true, std::memory_order_relaxed);
        worker_ = std::thread{[this] { run(); }};
        return E_Status::Ok;
    }

    [[nodiscard]] E_Status stop() noexcept override {
        shutdown();
        return E_Status::Ok;
    }

private:
    /// Joins the worker. Called from stop() and again from the destructor: the
    /// thread body lives in this DSO, so it must be finished before the host
    /// dlcloses the module.
    void shutdown() noexcept {
        running_.store(false, std::memory_order_relaxed);
        if (worker_.joinable()) worker_.join();
    }

    void run() noexcept {
        const auto period = std::chrono::milliseconds{period_ms()};
        while (running_.load(std::memory_order_relaxed)) {
            api_->log(LogLevel::Info, "tick at " + std::to_string(api_->uptime_ms()) + " ms");
            std::this_thread::sleep_for(period);
        }
        api_->log(LogLevel::Debug, "heartbeat thread left");
    }

    [[nodiscard]] unsigned period_ms() const noexcept {
        const std::string_view raw = api_->config("heartbeat.period_ms");
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

PLUGIN_API plugin_i* plugin_create(api_i& api) noexcept {
    // The boundary is noexcept, so a bad_alloc must not escape it.
    return new (std::nothrow) HeartbeatPlugin{api};
}

PLUGIN_API void plugin_destroy(plugin_i* instance) noexcept { delete instance; }

}  // extern "C"
