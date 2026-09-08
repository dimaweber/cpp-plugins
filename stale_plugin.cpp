#include "plugin_abi.h"

// A module built against an older contract. The host rejects it before calling
// plugin_create, so none of its code below ever runs.

extern "C" {

PLUGIN_API std::uint32_t plugin_abi_version() noexcept { return kPluginAbiVersion - 1; }

PLUGIN_API plugin_i* plugin_create(api_i&) noexcept { return nullptr; }

PLUGIN_API void plugin_destroy(plugin_i* instance) noexcept { delete instance; }

}  // extern "C"
