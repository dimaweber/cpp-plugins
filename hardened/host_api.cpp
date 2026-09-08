#include "host_api.h"

#include "example_log.h"

#include <utility>

api_i::~api_i() = default;

HostApi::HostApi(std::map<std::string, std::string, std::less<>> config) noexcept
    : config_{std::move(config)} {}

void HostApi::log(LogLevel level, abi_str msg) noexcept {
    LOG_INFO("plugin[{}] {}", level, to_sv(msg));
}

abi_str HostApi::config(abi_str key) const noexcept {
    const auto it = config_.find(to_sv(key));
    if (it == config_.end()) return {};
    return to_abi(it->second);
}

std::uint64_t HostApi::uptime_ms() const noexcept {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(duration_cast<milliseconds>(steady_clock::now() - started_).count());
}
