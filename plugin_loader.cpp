#include "plugin_loader.h"

#include "example_log.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace loader {
namespace {

/// Resolves one symbol. dlsym returns nullptr both for "absent" and for a
/// symbol whose value is nullptr, so the error is read from dlerror() after
/// clearing any stale one.
template <typename Fn>
[[nodiscard]] Fn dl_symbol(void* handle, const char* name) noexcept {
    static_cast<void>(::dlerror());
    void* symbol = ::dlsym(handle, name);
    if (const char* error = ::dlerror(); error != nullptr) {
        LOG_ERR("dlsym({}) failed: {}", name, error);
        return nullptr;
    }
    // Conditionally supported by the standard, guaranteed by POSIX dlsym.
    return reinterpret_cast<Fn>(symbol);
}

}  // namespace

PluginModule::PluginModule(std::filesystem::path so_path, dl_handle handle, plugin_ptr instance) noexcept
    : path_{std::move(so_path)}, handle_{std::move(handle)}, instance_{std::move(instance)} {}

std::optional<PluginModule> PluginModule::load(const std::filesystem::path& so_path, api_i& api) {
    LOG_DEBUG("opening plugin {}", so_path.string());

    // RTLD_NOW so a missing symbol fails here instead of at the first call;
    // RTLD_LOCAL so the plugin's own symbols cannot shadow anyone else's.
    dl_handle handle{::dlopen(so_path.c_str(), RTLD_NOW | RTLD_LOCAL)};
    if (!handle) {
        LOG_ERR("dlopen({}) failed: {}", so_path.string(), ::dlerror());
        return std::nullopt;
    }

    const auto abi_version = dl_symbol<std::uint32_t (*)() noexcept>(handle.get(), "plugin_abi_version");
    const auto create = dl_symbol<plugin_i* (*)(api_i&) noexcept>(handle.get(), "plugin_create");
    const auto destroy = dl_symbol<PluginDeleter::destroy_fn>(handle.get(), "plugin_destroy");
    if (abi_version == nullptr || create == nullptr || destroy == nullptr) {
        LOG_ERR("{} is not a plugin module: entry points missing", so_path.string());
        return std::nullopt;
    }

    if (const std::uint32_t module_abi = abi_version(); module_abi != kPluginAbiVersion) {
        LOG_ERR("{} was built for ABI {}, host speaks {}: {}", so_path.string(), module_abi, kPluginAbiVersion,
                E_Status::AbiMismatch);
        return std::nullopt;
    }

    plugin_ptr instance{create(api), PluginDeleter{destroy}};
    if (!instance) {
        LOG_ERR("plugin_create({}) returned no instance", so_path.string());
        return std::nullopt;
    }

    LOG_INFO("loaded plugin '{}' from {}", instance->name(), so_path.string());
    return PluginModule{so_path, std::move(handle), std::move(instance)};
}

PluginRegistry::~PluginRegistry() {
    stop_all();
    LOG_DEBUG("unloading {} module(s)", modules_.size());
    while (!modules_.empty()) modules_.pop_back();
}

E_Status PluginRegistry::load_one(const std::filesystem::path& so_path) {
    auto module = PluginModule::load(so_path, *api_);
    if (!module) return E_Status::Error;
    modules_.push_back(std::move(*module));
    return E_Status::Ok;
}

E_Status PluginRegistry::load_dir(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        LOG_ERR("plugin directory {} is unusable: {}", dir.string(), ec.message());
        return E_Status::NotFound;
    }

    // Sorted so the load order is reproducible across filesystems.
    std::vector<std::filesystem::path> candidates;
    for (const auto& entry : std::filesystem::directory_iterator{dir, ec}) {
        if (entry.is_regular_file() && entry.path().extension() == ".so") candidates.push_back(entry.path());
    }
    std::ranges::sort(candidates);

    const std::size_t before = modules_.size();
    for (const auto& candidate : candidates) static_cast<void>(load_one(candidate));

    if (modules_.size() == before) {
        LOG_WARN("no loadable plugin in {}", dir.string());
        return E_Status::NotFound;
    }
    return E_Status::Ok;
}

E_Status PluginRegistry::start_all() noexcept {
    E_Status result = E_Status::Ok;
    for (auto it = modules_.begin(); it != modules_.end();) {
        const E_Status status = it->instance().start();
        if (status == E_Status::Ok) {
            ++it;
            continue;
        }
        LOG_ERR("plugin '{}' failed to start: {}, unloading it", it->instance().name(), status);
        it = modules_.erase(it);
        result = E_Status::Error;
    }
    return result;
}

void PluginRegistry::stop_all() noexcept {
    for (auto it = modules_.rbegin(); it != modules_.rend(); ++it) {
        if (it->instance().stop() != E_Status::Ok) {
            LOG_WARN("plugin '{}' did not stop cleanly", it->instance().name());
        }
    }
}

}  // namespace loader
