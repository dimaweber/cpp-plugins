#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#define PLUGIN_API __attribute__((visibility("default")))

/// Generation of the ABI below. Bump on ANY change to api_i or plugin_i,
/// including *adding* a virtual function: that reorders the vtable.
inline constexpr std::uint32_t kPluginAbiVersion = 2;

/// A borrowed character range, laid out by this header rather than by whichever
/// standard library each side happens to use. std::string_view has the same two
/// members in the opposite order in libstdc++ and libc++, so passing it across
/// the boundary reinterprets the size as the pointer.
///
/// Trivially copyable and 16 bytes, so the psABI passes it in two registers on
/// every toolchain that targets this platform.
struct abi_str {
    const char* data{nullptr};
    std::uint64_t size{0};
};

/// Converts to and from the local standard library's view type. Both are inline,
/// so each image compiles them against its own headers - which is exactly the
/// point.
[[nodiscard]] inline abi_str to_abi(std::string_view text) noexcept {
    return abi_str{text.data(), static_cast<std::uint64_t>(text.size())};
}

[[nodiscard]] inline std::string_view to_sv(abi_str text) noexcept {
    return std::string_view{text.data, static_cast<std::size_t>(text.size)};
}

/// Underlying types are fixed, so the width does not depend on how a compiler
/// chooses to size an unfixed enumeration.
enum class E_Status : std::uint32_t {
    Ok,
    Error,
    NotFound,
    AbiMismatch,
};

[[nodiscard]] constexpr const char* to_string(E_Status value, const char* defValue = nullptr) noexcept {
    switch (value) {
        case E_Status::Ok: return "Ok";
        case E_Status::Error: return "Error";
        case E_Status::NotFound: return "NotFound";
        case E_Status::AbiMismatch: return "AbiMismatch";
    }
    return defValue;
}

enum class LogLevel : std::uint32_t {
    Debug,
    Info,
    Warn,
    Err,
};

[[nodiscard]] constexpr const char* to_string(LogLevel value, const char* defValue = nullptr) noexcept {
    switch (value) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Err: return "ERR";
    }
    return defValue;
}

/// Host facilities handed to every plugin. Every type in every signature is a
/// built-in, a pointer, or a POD declared above, so the layout comes from the
/// psABI and the Itanium C++ ABI instead of from a standard library.
class PLUGIN_API api_i {
public:
    virtual ~api_i();

    virtual void log(LogLevel level, abi_str msg) noexcept = 0;
    [[nodiscard]] virtual abi_str config(abi_str key) const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t uptime_ms() const noexcept = 0;

protected:
    api_i() noexcept = default;
    api_i(const api_i&) = delete;
    api_i& operator=(const api_i&) = delete;
};

class PLUGIN_API plugin_i {
public:
    virtual ~plugin_i() = default;

    [[nodiscard]] virtual abi_str name() const noexcept = 0;
    [[nodiscard]] virtual E_Status start() noexcept = 0;
    [[nodiscard]] virtual E_Status stop() noexcept = 0;
};

extern "C" {

PLUGIN_API std::uint32_t plugin_abi_version() noexcept;
PLUGIN_API plugin_i* plugin_create(api_i& api) noexcept;
PLUGIN_API void plugin_destroy(plugin_i* instance) noexcept;

}  // extern "C"
