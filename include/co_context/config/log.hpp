#pragma once

#include <cstdint>

namespace co_context::config {

enum class level : uint8_t { verbose, debug, info, warning, error, no_log };

inline level log_level = level::warning;

inline void set_log_level(level new_level) noexcept {
    log_level = new_level;
}

inline bool is_log_v = log_level <= level::verbose;
inline bool is_log_d = log_level <= level::debug;
inline bool is_log_i = log_level <= level::info;
inline bool is_log_w = log_level <= level::warning;
inline bool is_log_e = log_level <= level::error;

} // namespace co_context::config
