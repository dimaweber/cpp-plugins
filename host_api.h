#pragma once

#include "plugin_abi.h"

#include <chrono>
#include <functional>
#include <map>
#include <string>

/// The host's implementation of the plugin-facing api. Owned by main() and
/// outlives the plugin registry.
///
/// Usage:
/// @code
///   HostApi api{{{"peer.address", "10.0.0.2"}}};
///   PluginRegistry registry{api};
/// @endcode
class HostApi final : public api_i {
public:
    explicit HostApi(std::map<std::string, std::string, std::less<>> config) noexcept;

    void log(LogLevel level, std::string_view msg) noexcept override;
    [[nodiscard]] std::string_view config(std::string_view key) const noexcept override;
    [[nodiscard]] std::uint64_t uptime_ms() const noexcept override;

private:
    std::map<std::string, std::string, std::less<>> config_;
    std::chrono::steady_clock::time_point started_{std::chrono::steady_clock::now()};
};
