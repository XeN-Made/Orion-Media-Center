// ps5-homebrew-ui - ORION's library preferences: custom tabs, placements, the last tab.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/prefs.hpp"

#include "core/save_file.hpp"
#include "library/json.hpp"
#include "library/library.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace hui::orion
{

namespace
{

constexpr std::size_t kMaxName = 24; // bytes
constexpr std::size_t kMaxTabs = 32;

std::string file_of(const std::string &state_dir)
{
    return state_dir + "/library.json";
}

// Trimmed, single-line and short; never cut inside a UTF-8 character.
std::string clean_name(std::string_view name)
{
    std::string out;
    for (char c : name)
        out += c == '\n' || c == '\r' || c == '\t' ? ' ' : c;
    while (!out.empty() && out.front() == ' ')
        out.erase(out.begin());
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    if (out.size() > kMaxName)
    {
        std::size_t end = kMaxName;
        while (end > 0 && (static_cast<unsigned char>(out[end]) & 0xc0) == 0x80)
            --end;
        out.resize(end);
    }
    return out.empty() ? std::string("New tab") : out;
}

bool valid_id(std::string_view id)
{
    return !id.empty() && id.size() <= 16 &&
           std::all_of(id.begin(), id.end(),
                       [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; });
}

} // namespace

const CustomTab *LibraryPrefs::tab(const std::string &id) const
{
    for (const CustomTab &tab : tabs)
    {
        if (tab.id == id)
            return &tab;
    }
    return nullptr;
}

const CustomTab *LibraryPrefs::placement(const std::string &title) const
{
    for (const Placement &placement : placements)
    {
        if (placement.title == title)
            return tab(placement.tab);
    }
    return nullptr;
}

bool LibraryPrefs::place(const std::string &title, const std::string &tab_id)
{
    if (tab(tab_id) == nullptr || !library::is_item_id(title))
        return false;
    unplace(title);
    placements.push_back({title, tab_id});
    return true;
}

void LibraryPrefs::unplace(const std::string &title)
{
    std::erase_if(placements, [&](const Placement &p) { return p.title == title; });
}

const CustomTab &LibraryPrefs::add_tab(const std::string &name)
{
    int number = 1;
    std::string id;
    do
        id = "t" + std::to_string(number++);
    while (tab(id) != nullptr);
    tabs.push_back({id, clean_name(name)});
    return tabs.back();
}

bool LibraryPrefs::rename_tab(const std::string &id, const std::string &name)
{
    for (CustomTab &tab : tabs)
    {
        if (tab.id == id)
        {
            tab.name = clean_name(name);
            return true;
        }
    }
    return false;
}

bool LibraryPrefs::remove_tab(const std::string &id)
{
    const auto found =
        std::find_if(tabs.begin(), tabs.end(), [&](const CustomTab &t) { return t.id == id; });
    if (found == tabs.end())
        return false;
    tabs.erase(found);
    std::erase_if(placements, [&](const Placement &p) { return p.tab == id; });
    if (last_tab == "tab:" + id)
        last_tab = "all";
    return true;
}

bool LibraryPrefs::move_tab(const std::string &id, int direction)
{
    for (std::size_t i = 0; i < tabs.size(); ++i)
    {
        if (tabs[i].id != id)
            continue;
        const std::size_t other = i + static_cast<std::size_t>(direction);
        if ((direction < 0 && i == 0) || other >= tabs.size())
            return false;
        std::swap(tabs[i], tabs[other]);
        return true;
    }
    return false;
}

namespace
{

SystemPref &system_pref(std::vector<SystemPref> &systems, const std::string &system)
{
    for (SystemPref &pref : systems)
    {
        if (pref.system == system)
            return pref;
    }
    systems.push_back({system, true, ""});
    return systems.back();
}

} // namespace

bool LibraryPrefs::system_enabled(const std::string &system) const
{
    for (const SystemPref &pref : systems)
    {
        if (pref.system == system)
            return pref.enabled;
    }
    return true;
}

std::string LibraryPrefs::system_core(const std::string &system) const
{
    for (const SystemPref &pref : systems)
    {
        if (pref.system == system)
            return pref.core;
    }
    return "";
}

void LibraryPrefs::set_system_enabled(const std::string &system, bool enabled)
{
    system_pref(systems, system).enabled = enabled;
}

void LibraryPrefs::set_system_core(const std::string &system, const std::string &core)
{
    system_pref(systems, system).core = core;
}

const char *cover_shape_key(CoverShape shape)
{
    switch (shape)
    {
    case CoverShape::square:
        return "square";
    case CoverShape::portrait:
        return "portrait";
    case CoverShape::landscape:
        return "landscape";
    default:
        return "auto";
    }
}

CoverShape cover_shape_of(std::string_view key)
{
    if (key == "square")
        return CoverShape::square;
    if (key == "portrait")
        return CoverShape::portrait;
    if (key == "landscape")
        return CoverShape::landscape;
    return CoverShape::automatic;
}

namespace
{

const TabPrefs *find_tab_prefs(const std::vector<TabPrefs> &all, const std::string &tab)
{
    for (const TabPrefs &entry : all)
    {
        if (entry.tab == tab)
            return &entry;
    }
    return nullptr;
}

// Changes one tab's entry, adding it when needed and dropping it once it
// says nothing the defaults do not.
template <typename Edit>
void edit_tab_prefs(std::vector<TabPrefs> &all, const std::string &tab, Edit edit)
{
    if (tab.empty() || tab.size() > 96)
        return;
    auto at = std::find_if(all.begin(), all.end(), [&](const TabPrefs &e) { return e.tab == tab; });
    if (at == all.end())
    {
        if (all.size() >= 64)
            return;
        TabPrefs fresh;
        fresh.tab = tab;
        all.push_back(fresh);
        at = all.end() - 1;
    }
    edit(*at);
    if (at->theme.empty() && at->cover == CoverShape::automatic && at->sort.empty() && !at->hidden)
        all.erase(at);
}

} // namespace

std::string LibraryPrefs::tab_theme(const std::string &tab) const
{
    const TabPrefs *entry = find_tab_prefs(tab_prefs, tab);
    return entry != nullptr ? entry->theme : std::string();
}

void LibraryPrefs::set_tab_theme(const std::string &tab, const std::string &theme)
{
    edit_tab_prefs(tab_prefs, tab, [&](TabPrefs &e) { e.theme = theme.size() <= 40 ? theme : ""; });
}

CoverShape LibraryPrefs::tab_cover(const std::string &tab) const
{
    const TabPrefs *entry = find_tab_prefs(tab_prefs, tab);
    return entry != nullptr ? entry->cover : CoverShape::automatic;
}

void LibraryPrefs::set_tab_cover(const std::string &tab, CoverShape cover)
{
    edit_tab_prefs(tab_prefs, tab, [&](TabPrefs &e) { e.cover = cover; });
}

std::string LibraryPrefs::tab_sort(const std::string &tab) const
{
    const TabPrefs *entry = find_tab_prefs(tab_prefs, tab);
    return entry != nullptr ? entry->sort : std::string();
}

void LibraryPrefs::set_tab_sort(const std::string &tab, const std::string &sort)
{
    edit_tab_prefs(tab_prefs, tab, [&](TabPrefs &e) { e.sort = sort.size() <= 24 ? sort : ""; });
}

bool LibraryPrefs::tab_hidden(const std::string &tab) const
{
    const TabPrefs *entry = find_tab_prefs(tab_prefs, tab);
    return tab != "all" && entry != nullptr && entry->hidden;
}

void LibraryPrefs::set_tab_hidden(const std::string &tab, bool hidden)
{
    edit_tab_prefs(tab_prefs, tab, [&](TabPrefs &e) { e.hidden = hidden && tab != "all"; });
}

bool valid_art_dir(std::string_view path)
{
    if (path.size() < 7 || path.size() > 160 ||
        (path.rfind("/data/", 0) != 0 && path.rfind("/mnt/", 0) != 0) ||
        path.find("..") != std::string_view::npos || path.back() == '/')
        return false;
    return std::none_of(path.begin(), path.end(),
                        [](char c) { return static_cast<unsigned char>(c) < 0x20 || c == '\\'; });
}

std::string art_folder(const LibraryPrefs &prefs, const std::string &state_dir)
{
    return valid_art_dir(prefs.art_dir) ? prefs.art_dir : state_dir + "/art";
}

LibraryPrefs load_prefs(const std::string &state_dir)
{
    LibraryPrefs prefs;
    std::string text;
    json::Value root;
    if (state_dir.empty() || !save::read_file(file_of(state_dir), &text, 1u << 20) ||
        !json::parse(text, &root) || !root.is_object())
        return prefs;

    for (const json::Value &value : root["tabs"].elements())
    {
        const std::string id = value["id"].string_or("");
        if (valid_id(id) && prefs.tab(id) == nullptr && prefs.tabs.size() < kMaxTabs)
            prefs.tabs.push_back({id, clean_name(value["name"].string_or(""))});
    }
    for (const auto &member : root["placements"].members())
        prefs.place(member.first, member.second.string_or(""));

    for (const auto &member : root["systems"].members())
    {
        if (member.first.empty() || member.first.size() > 80 || prefs.systems.size() >= 64)
            continue;
        SystemPref pref;
        pref.system = member.first;
        pref.enabled = member.second["enabled"].bool_or(true);
        const std::string core = member.second["core"].string_or("");
        // A core is a file name in RetroArch's cores folder, nothing more.
        if (core.find('/') == std::string::npos && core.size() <= 64)
            pref.core = core;
        prefs.systems.push_back(std::move(pref));
    }
    for (const auto &member : root["tab_themes"].members())
        prefs.set_tab_theme(member.first, member.second.string_or(""));
    for (const auto &member : root["tab_settings"].members())
    {
        const json::Value &value = member.second;
        prefs.set_tab_theme(member.first, value["theme"].string_or(""));
        prefs.set_tab_cover(member.first, cover_shape_of(value["cover"].string_or("")));
        prefs.set_tab_sort(member.first, value["sort"].string_or(""));
        prefs.set_tab_hidden(member.first, value["hidden"].bool_or(false));
    }
    prefs.last_tab = root["last_tab"].string_or(prefs.last_tab);
    prefs.sort = root["sort"].string_or(prefs.sort);
    prefs.reopen_last_tab = root["reopen_last_tab"].bool_or(prefs.reopen_last_tab);
    prefs.moved_leave_shelf = root["moved_leave_shelf"].bool_or(prefs.moved_leave_shelf);
    prefs.auto_art = root["auto_art"].bool_or(prefs.auto_art);
    prefs.return_on_close = root["return_on_close"].bool_or(prefs.return_on_close);
    prefs.shortcut_test = root["shortcut_test"].bool_or(prefs.shortcut_test);
    prefs.game_backgrounds = root["game_backgrounds"].bool_or(prefs.game_backgrounds);
    prefs.blurred_covers = root["blurred_covers"].bool_or(prefs.blurred_covers);
    prefs.background_strength = std::clamp(
        static_cast<float>(root["background_strength"].number_or(prefs.background_strength)), 0.2f,
        1.0f);
    for (const json::Value &value : root["rom_folders"].elements())
    {
        RomFolder folder;
        folder.path = value["path"].string_or("");
        folder.system = value["system"].string_or("");
        if (valid_art_dir(folder.path) && folder.system.size() <= 80 &&
            prefs.rom_folders.size() < 16)
            prefs.rom_folders.push_back(std::move(folder));
    }
    const std::string art_dir = root["art_dir"].string_or("");
    if (valid_art_dir(art_dir))
        prefs.art_dir = art_dir;
    return prefs;
}

bool save_prefs(const std::string &state_dir, const LibraryPrefs &prefs)
{
    if (state_dir.empty() || !save::ensure_directory(state_dir))
        return false;
    std::string text = "{\n";
    text += "  \"last_tab\": " + json::quote(prefs.last_tab) + ",\n";
    text += "  \"sort\": " + json::quote(prefs.sort) + ",\n";
    text +=
        std::string("  \"reopen_last_tab\": ") + (prefs.reopen_last_tab ? "true" : "false") + ",\n";
    text += std::string("  \"moved_leave_shelf\": ") +
            (prefs.moved_leave_shelf ? "true" : "false") + ",\n";
    text += std::string("  \"auto_art\": ") + (prefs.auto_art ? "true" : "false") + ",\n";
    text +=
        std::string("  \"return_on_close\": ") + (prefs.return_on_close ? "true" : "false") + ",\n";
    text += std::string("  \"shortcut_test\": ") + (prefs.shortcut_test ? "true" : "false") + ",\n";
    text += std::string("  \"game_backgrounds\": ") + (prefs.game_backgrounds ? "true" : "false") +
            ",\n";
    text +=
        std::string("  \"blurred_covers\": ") + (prefs.blurred_covers ? "true" : "false") + ",\n";
    text += "  \"background_strength\": " +
            std::to_string(static_cast<int>(prefs.background_strength * 100.0f + 0.5f) / 100.0) +
            ",\n";
    text += "  \"art_dir\": " + json::quote(prefs.art_dir) + ",\n";
    text += "  \"rom_folders\": [";
    for (std::size_t i = 0; i < prefs.rom_folders.size(); ++i)
    {
        text += i == 0 ? "\n" : ",\n";
        text += "    {\"path\": " + json::quote(prefs.rom_folders[i].path) +
                ", \"system\": " + json::quote(prefs.rom_folders[i].system) + "}";
    }
    text += prefs.rom_folders.empty() ? "],\n" : "\n  ],\n";
    text += "  \"tabs\": [";
    for (std::size_t i = 0; i < prefs.tabs.size(); ++i)
    {
        text += i == 0 ? "\n" : ",\n";
        text += "    {\"id\": " + json::quote(prefs.tabs[i].id) +
                ", \"name\": " + json::quote(prefs.tabs[i].name) + "}";
    }
    text += prefs.tabs.empty() ? "],\n" : "\n  ],\n";
    text += "  \"placements\": {";
    for (std::size_t i = 0; i < prefs.placements.size(); ++i)
    {
        text += i == 0 ? "\n" : ",\n";
        text += "    " + json::quote(prefs.placements[i].title) + ": " +
                json::quote(prefs.placements[i].tab);
    }
    text += prefs.placements.empty() ? "},\n" : "\n  },\n";
    text += "  \"systems\": {";
    for (std::size_t i = 0; i < prefs.systems.size(); ++i)
    {
        const SystemPref &pref = prefs.systems[i];
        text += i == 0 ? "\n" : ",\n";
        text += "    " + json::quote(pref.system) +
                ": {\"enabled\": " + (pref.enabled ? "true" : "false") +
                ", \"core\": " + json::quote(pref.core) + "}";
    }
    text += prefs.systems.empty() ? "},\n" : "\n  },\n";
    text += "  \"tab_settings\": {";
    for (std::size_t i = 0; i < prefs.tab_prefs.size(); ++i)
    {
        const TabPrefs &entry = prefs.tab_prefs[i];
        text += i == 0 ? "\n" : ",\n";
        text += "    " + json::quote(entry.tab) + ": {\"theme\": " + json::quote(entry.theme) +
                ", \"cover\": " + json::quote(cover_shape_key(entry.cover)) +
                ", \"sort\": " + json::quote(entry.sort) +
                ", \"hidden\": " + (entry.hidden ? "true" : "false") + "}";
    }
    text += prefs.tab_prefs.empty() ? "}\n}\n" : "\n  }\n}\n";
    return save::write_atomic(file_of(state_dir), text).empty();
}

} // namespace hui::orion
