#pragma once

// Stand-in for the in-tree sk_log.h / siklu_fmt.h so this example builds on its
// own. In the repository use fLOG_EX_ERR(true, ...) and friends instead, and
// drop the formatter below - the project one already routes enums to to_string.

#include <fmt/format.h>

#include <type_traits>

/// Formats any enum that has a to_string(...) helper by its name.
template <typename E>
struct fmt::formatter<E, char, std::enable_if_t<std::is_enum_v<E>>> : fmt::formatter<fmt::string_view> {
    template <typename Ctx>
    auto format(E value, Ctx& ctx) const {
        return fmt::formatter<fmt::string_view>::format(to_string(value, "<unknown>"), ctx);
    }
};

#define LOG_DEBUG(...) ::fmt::print("[DEBUG] {}\n", ::fmt::format(__VA_ARGS__))
#define LOG_INFO(...)  ::fmt::print("[INFO ] {}\n", ::fmt::format(__VA_ARGS__))
#define LOG_WARN(...)  ::fmt::print("[WARN ] {}\n", ::fmt::format(__VA_ARGS__))
#define LOG_ERR(...)   ::fmt::print("[ERR  ] {}\n", ::fmt::format(__VA_ARGS__))
