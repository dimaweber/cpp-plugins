#include "host_api.h"

#include "example_log.h"

#include <utility>

// api_i's key function. Defining it here keeps a single vtable and typeinfo for
// the interface in the host image.
api_i::~api_i() = default;

HostApi::HostApi(std::map<std::string, std::string, std::less<>> config) noexcept
    : config_{std::move(config)} {}

void HostApi::log(LogLevel level, std::string_view msg) noexcept {
    LOG_INFO("plugin[{}] {}", level, msg);
}

std::string_view HostApi::config(std::string_view key) const noexcept {
    const auto it = config_.find(key);
    if (it == config_.end()) return {};
    return it->second;
}

std::uint64_t HostApi::uptime_ms() const noexcept {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(duration_cast<milliseconds>(steady_clock::now() - started_).count());
}
