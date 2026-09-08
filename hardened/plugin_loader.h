#pragma once

#include "plugin_abi.h"

#include <dlfcn.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace loader {

/// Closes a dlopen handle.
struct DlCloser {
    void operator()(void* handle) const noexcept {
        if (handle != nullptr) ::dlclose(handle);
    }
};

using dl_handle = std::unique_ptr<void, DlCloser>;

/// Destroys a plugin instance through the entry point of the module that made
/// it, so the allocation and the deallocation stay inside one DSO.
class PluginDeleter {
public:
    using destroy_fn = void (*)(plugin_i*) noexcept;

    PluginDeleter() noexcept = default;
    explicit PluginDeleter(destroy_fn destroy) noexcept : destroy_{destroy} {}

    void operator()(plugin_i* instance) const noexcept {
        if (instance != nullptr && destroy_ != nullptr) destroy_(instance);
    }

private:
    destroy_fn destroy_{nullptr};
};

using plugin_ptr = std::unique_ptr<plugin_i, PluginDeleter>;

/// One dlopen'ed module together with the single instance it created.
///
/// Usage:
/// @code
///   if (auto module = PluginModule::load("./libheartbeat.so", api)) {
///       static_cast<void>(module->instance().start());
///   }
/// @endcode
class PluginModule {
public:
    /// Opens @p so_path, checks its ABI generation and creates one instance.
    /// @return the loaded module, or std::nullopt when the file cannot be
    ///         opened, lacks an entry point, reports a different ABI
    ///         generation, or fails to create an instance. The reason is
    ///         logged; in-tree this should return result_t<PluginModule>.
    [[nodiscard]] static std::optional<PluginModule> load(const std::filesystem::path& so_path, api_i& api);

    [[nodiscard]] plugin_i& instance() const noexcept { return *instance_; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    PluginModule(std::filesystem::path so_path, dl_handle handle, plugin_ptr instance) noexcept;

    std::filesystem::path path_;

    // Declaration order is the destruction contract: members die in reverse, so
    // instance_ is destroyed while the code that implements it is still mapped,
    // and handle_ runs dlclose only afterwards.
    dl_handle handle_;
    plugin_ptr instance_;
};

/// Loads every *.so in a directory and drives the instances as a group.
///
/// Usage:
/// @code
///   HostApi api{{}};                       // declared first, destroyed last
///   PluginRegistry registry{api};
///   static_cast<void>(registry.load_dir("./plugins"));
///   static_cast<void>(registry.start_all());
/// @endcode
class PluginRegistry {
public:
    explicit PluginRegistry(api_i& api) noexcept : api_{&api} {}
    ~PluginRegistry();

    PluginRegistry(const PluginRegistry&) = delete;
    PluginRegistry& operator=(const PluginRegistry&) = delete;

    /// @return E_Status::Ok when at least one module loaded,
    ///         E_Status::NotFound when @p dir holds no loadable module.
    [[nodiscard]] E_Status load_dir(const std::filesystem::path& dir);

    [[nodiscard]] E_Status load_one(const std::filesystem::path& so_path);

    /// Starts every loaded instance. A failing plugin is unloaded rather than
    /// left half-started.
    [[nodiscard]] E_Status start_all() noexcept;

    void stop_all() noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return modules_.size(); }

private:
    api_i* api_;
    std::vector<PluginModule> modules_;
};

}  // namespace loader
