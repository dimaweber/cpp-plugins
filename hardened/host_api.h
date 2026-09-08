#pragma once

#include "plugin_abi.h"

#include <chrono>
#include <functional>
#include <map>
#include <string>

class HostApi final : public api_i {
public:
    explicit HostApi(std::map<std::string, std::string, std::less<>> config) noexcept;

    void log(LogLevel level, abi_str msg) noexcept override;
    [[nodiscard]] abi_str config(abi_str key) const noexcept override;
    [[nodiscard]] std::uint64_t uptime_ms() const noexcept override;

private:
    std::map<std::string, std::string, std::less<>> config_;
    std::chrono::steady_clock::time_point started_{std::chrono::steady_clock::now()};
};
