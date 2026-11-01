// SPDX-License-Identifier: GPL-3.0-only
// Wii input profiles for the handheld: a text file of the same `bind.<scheme>.<target>=...` and
// `tune.<scheme>.<setting>=...` lines as runtime_settings.ini (input_bindings.h apply_controls_setting), applied over the
// controls the runtime holds. A profile is a game's way of using the buttons; the runtime's defaults (and Windows) are
// not touched. `name=` and `#` lines are ignored. Header-only so the host and the test share it.
#pragma once

#include "galaxy/input_bindings.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace galaxy::host {

struct ProfileResult {
    std::size_t applied = 0;  // bind./tune. lines the model took
    std::size_t ignored = 0;  // lines that were neither comments nor bind./tune. (or had no value)
    std::string name;         // the profile's `name=`, if any
};

[[nodiscard]] inline std::string_view profile_trim(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

// Applies every line of `text` to `config`.
inline ProfileResult apply_wii_profile_text(galaxy::input::ControlsConfig& config, std::string_view text) {
    ProfileResult result;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = profile_trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1u);
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            ++result.ignored;
            continue;
        }
        const std::string key(profile_trim(line.substr(0, equals)));
        const std::string value(profile_trim(line.substr(equals + 1u)));
        if (key == "name") {
            result.name = value;
        } else if ((key.starts_with("bind.") || key.starts_with("tune.")) &&
                   galaxy::input::apply_controls_setting(config, key, value)) {
            ++result.applied;
        } else {
            ++result.ignored;
        }
    }
    return result;
}

}  // namespace galaxy::host
