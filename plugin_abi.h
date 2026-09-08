#pragma once

#include <cstdint>
#include <string_view>

/// Marks a symbol or type as part of the host<->plugin contract.
/// Both sides are built with -fvisibility=hidden, so anything the other side
/// must see - the entry points, and the interface types whose typeinfo has to
/// compare equal across the DSO boundary - needs this explicitly.
#define PLUGIN_API __attribute__((visibility("default")))

/// Generation of the ABI below. Bump on ANY change to api_i or plugin_i,
/// including *adding* a virtual function: that reorders the vtable, so an old
/// plugin would call a different slot than it was compiled against.
inline constexpr std::uint32_t kPluginAbiVersion = 1;

enum class E_Status {
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

enum class LogLevel {
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

/// Host facilities handed to every plugin.
///
/// Lifetime contract: the api instance outlives every plugin. The host destroys
/// all plugin instances and dlcloses their modules before tearing the api down.
/// A plugin may therefore cache the reference for its whole life, including on
/// background threads, provided it joins those threads in its destructor.
///
/// Thread safety: every method is callable concurrently from any thread.
class PLUGIN_API api_i {
public:
    /// Defined out-of-line in exactly one host translation unit, which pins the
    /// vtable and typeinfo to the host image instead of leaving weak copies in
    /// each DSO for the dynamic linker to merge.
    virtual ~api_i();

    virtual void log(LogLevel level, std::string_view msg) noexcept = 0;

    /// @return the configured value, or an empty view when @p key is unset.
    [[nodiscard]] virtual std::string_view config(std::string_view key) const noexcept = 0;

    [[nodiscard]] virtual std::uint64_t uptime_ms() const noexcept = 0;

protected:
    api_i() noexcept = default;
    api_i(const api_i&) = delete;
    api_i& operator=(const api_i&) = delete;
};

/// A loaded plugin instance. Created and destroyed only through the plugin's own
/// entry points, so allocation and deallocation stay inside one DSO.
class PLUGIN_API plugin_i {
public:
    virtual ~plugin_i() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual E_Status start() noexcept = 0;
    [[nodiscard]] virtual E_Status stop() noexcept = 0;
};

/// Entry points every plugin module exports. Declared noexcept because an
/// exception unwinding across the boundary would depend on both images sharing
/// one libstdc++ and one set of typeinfo symbols; status codes do not.
extern "C" {

/// Queried before anything else, so a stale plugin never runs its own code.
PLUGIN_API std::uint32_t plugin_abi_version() noexcept;

/// @return a new instance owned by the caller, or nullptr on failure.
PLUGIN_API plugin_i* plugin_create(api_i& api) noexcept;

PLUGIN_API void plugin_destroy(plugin_i* instance) noexcept;

}  // extern "C"
