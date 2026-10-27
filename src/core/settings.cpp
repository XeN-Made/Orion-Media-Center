// ORION Media Center - Player settings and their save format.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/settings.hpp"

#include "core/bytes.hpp"

#include <algorithm>

namespace hui
{

namespace
{
// 2 added title_volume at the end; a version 1 file reads with its default.
constexpr std::uint8_t kVersion = 2;
} // namespace

std::string encode_settings(const Settings &settings)
{
    bytes::Writer w;
    w.put(kVersion);
    w.put(static_cast<std::uint8_t>(settings.music_volume));
    w.put(static_cast<std::uint8_t>(settings.sfx_volume));
    w.put(static_cast<std::uint8_t>(settings.ui_volume));
    w.put_bool(settings.reduced_motion);
    w.put_bool(settings.swap_confirm);
    w.put_bool(settings.show_fps);
    w.put_bool(settings.haptics);
    w.put_bool(settings.light_bar);
    w.put(static_cast<std::uint8_t>(settings.resolution));
    w.put(static_cast<std::uint8_t>(settings.hint_size));
    w.put(static_cast<std::uint8_t>(settings.concept_index));
    w.put(static_cast<std::uint8_t>(settings.title_volume));
    return w.data();
}

bool decode_settings(std::string_view data, Settings *settings)
{
    bytes::Reader r(data);
    const std::uint8_t version = r.get<std::uint8_t>();
    if (version != 1 && version != kVersion)
        return false;
    Settings s;
    s.music_volume = std::clamp<int>(r.get<std::uint8_t>(), 0, 10);
    s.sfx_volume = std::clamp<int>(r.get<std::uint8_t>(), 0, 10);
    s.ui_volume = std::clamp<int>(r.get<std::uint8_t>(), 0, 10);
    s.reduced_motion = r.get_bool();
    s.swap_confirm = r.get_bool();
    s.show_fps = r.get_bool();
    s.haptics = r.get_bool();
    s.light_bar = r.get_bool();
    s.resolution = std::clamp<int>(r.get<std::uint8_t>(), 0, Settings::kResolutionCount - 1);
    s.hint_size = std::clamp<int>(r.get<std::uint8_t>(), 0, 2);
    s.concept_index = r.get<std::uint8_t>();
    if (version >= 2)
        s.title_volume = std::clamp<int>(r.get<std::uint8_t>(), 0, 10);
    if (!r.finished())
        return false;
    *settings = s;
    return true;
}

} // namespace hui
