// ORION Media Center - ORION's library preferences: custom tabs, placements, the last tab.
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
constexpr std::size_t kMaxHidden = 4000; // titles and games hidden

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

namespace
{

bool is_shelf_key(const std::string &key)
{
    return key == "games" || key == "homebrew" || key == "media";
}

} // namespace

std::string LibraryPrefs::placed_shelf(const std::string &title) const
{
    for (const Placement &placement : placements)
    {
        if (placement.title == title)
            return is_shelf_key(placement.tab) ? placement.tab : std::string();
    }
    return {};
}

bool LibraryPrefs::place(const std::string &title, const std::string &tab_id)
{
    if ((tab(tab_id) == nullptr && !is_shelf_key(tab_id)) || !library::is_item_id(title))
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

std::vector<std::string> LibraryPrefs::ordered_tabs(const std::vector<std::string> &tabs) const
{
    std::vector<std::string> out;
    for (const std::string &key : tab_order)
    {
        if (std::find(tabs.begin(), tabs.end(), key) != tabs.end() &&
            std::find(out.begin(), out.end(), key) == out.end())
            out.push_back(key);
    }
    // A tab the order does not name (new since) goes after its neighbour.
    for (std::size_t i = 0; i < tabs.size(); ++i)
    {
        if (std::find(out.begin(), out.end(), tabs[i]) != out.end())
            continue;
        auto at = out.begin();
        for (std::size_t before = i; before > 0; --before)
        {
            const auto found = std::find(out.begin(), out.end(), tabs[before - 1]);
            if (found != out.end())
            {
                at = found + 1;
                break;
            }
        }
        out.insert(at, tabs[i]);
    }
    return out;
}

bool LibraryPrefs::move_tab_key(const std::string &key, int direction,
                                const std::vector<std::string> &tabs)
{
    std::vector<std::string> order = ordered_tabs(tabs);
    const auto found = std::find(order.begin(), order.end(), key);
    if (found == order.end())
        return false;
    const std::size_t i = static_cast<std::size_t>(found - order.begin());
    if ((direction < 0 && i == 0) || (direction > 0 && i + 1 >= order.size()))
        return false;
    std::swap(order[i], order[direction < 0 ? i - 1 : i + 1]);
    tab_order = std::move(order);
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

bool known_view(const std::string &view)
{
    for (const char *key : kViewKeys)
    {
        if (view == key)
            return true;
    }
    return false;
}

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
    if (at->theme.empty() && at->cover == CoverShape::automatic && at->sort.empty() &&
        at->view.empty() && !at->hidden && at->name.empty())
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

std::string LibraryPrefs::tab_view(const std::string &tab) const
{
    const TabPrefs *entry = find_tab_prefs(tab_prefs, tab);
    return entry != nullptr ? entry->view : std::string();
}

void LibraryPrefs::set_tab_view(const std::string &tab, const std::string &view)
{
    edit_tab_prefs(tab_prefs, tab, [&](TabPrefs &e) { e.view = known_view(view) ? view : ""; });
}

bool LibraryPrefs::tab_hidden(const std::string &tab) const
{
    if (tab == kHiddenTabKey)
        return !hidden_tab_shown;
    const TabPrefs *entry = find_tab_prefs(tab_prefs, tab);
    return entry != nullptr && entry->hidden;
}

void LibraryPrefs::set_tab_hidden(const std::string &tab, bool hidden)
{
    if (tab == kHiddenTabKey)
    {
        hidden_tab_shown = !hidden;
        return;
    }
    edit_tab_prefs(tab_prefs, tab, [&](TabPrefs &e) { e.hidden = hidden; });
}

std::string LibraryPrefs::tab_name(const std::string &tab) const
{
    const TabPrefs *entry = find_tab_prefs(tab_prefs, tab);
    return entry != nullptr ? entry->name : std::string();
}

void LibraryPrefs::set_tab_name(const std::string &tab, const std::string &name)
{
    std::string clean;
    for (const char c : name)
        clean += c == '\n' || c == '\r' || c == '\t' ? ' ' : c;
    while (!clean.empty() && clean.back() == ' ')
        clean.pop_back();
    while (!clean.empty() && clean.front() == ' ')
        clean.erase(clean.begin());
    if (clean.size() > kMaxTitleName)
        clean.clear();
    edit_tab_prefs(tab_prefs, tab, [&](TabPrefs &e) { e.name = clean; });
}

std::string LibraryPrefs::game_player(const std::string &id) const
{
    for (const auto &[game, key] : game_players)
    {
        if (game == id)
            return key;
    }
    return "";
}

std::string LibraryPrefs::title_name(const std::string &id) const
{
    for (const auto &[title, name] : title_names)
    {
        if (title == id)
            return name;
    }
    return "";
}

void LibraryPrefs::set_title_name(const std::string &id, const std::string &name)
{
    std::erase_if(title_names, [&](const auto &entry) { return entry.first == id; });
    if (!name.empty() && name.size() <= kMaxTitleName && library::is_item_id(id) &&
        title_names.size() < kMaxHidden)
        title_names.emplace_back(id, name);
}

void LibraryPrefs::set_game_player(const std::string &id, const std::string &key)
{
    std::erase_if(game_players, [&](const auto &entry) { return entry.first == id; });
    if (!key.empty() && key.size() <= 96 && library::is_item_id(id) &&
        game_players.size() < kMaxHidden)
        game_players.emplace_back(id, key);
}

bool LibraryPrefs::title_hidden(const std::string &id) const
{
    return std::find(hidden_titles.begin(), hidden_titles.end(), id) != hidden_titles.end();
}

bool LibraryPrefs::set_title_hidden(const std::string &id, bool hidden)
{
    if (!library::is_item_id(id))
        return false;
    std::erase(hidden_titles, id);
    if (!hidden)
        return true;
    if (hidden_titles.size() >= kMaxHidden)
        return false;
    hidden_titles.push_back(id);
    return true;
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

int art_place(const std::string &art_dir)
{
    for (int i = 0; i < kArtPlaceCount; ++i)
    {
        if (art_dir == kArtPlacePaths[i])
            return i;
    }
    return kArtPlaceCount;
}

std::vector<library::GameFolder> game_folders(const LibraryPrefs &prefs)
{
    std::vector<library::GameFolder> folders;
    for (const RomFolder &folder : prefs.rom_folders)
        folders.push_back({folder.path, folder.system, folder.emulator});
    return folders;
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
    for (const json::Value &value : root["tab_order"].elements())
    {
        const std::string key = value.string_or("");
        if (!key.empty() && key.size() <= 96 && prefs.tab_order.size() < 128)
            prefs.tab_order.push_back(key);
    }
    for (const json::Value &value : root["hidden_titles"].elements())
        prefs.set_title_hidden(value.string_or(""), true);
    for (const auto &member : root["game_players"].members())
        prefs.set_game_player(member.first, member.second.string_or(""));
    for (const auto &member : root["title_names"].members())
        prefs.set_title_name(member.first, member.second.string_or(""));

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
        prefs.set_tab_view(member.first, value["view"].string_or(""));
        prefs.set_tab_hidden(member.first, value["hidden"].bool_or(false));
        prefs.set_tab_name(member.first, value["name"].string_or(""));
    }
    prefs.last_tab = root["last_tab"].string_or(prefs.last_tab);
    prefs.sort = root["sort"].string_or(prefs.sort);
    if (const std::string view = root["view"].string_or(""); known_view(view))
        prefs.view = view;
    prefs.reopen_last_tab = root["reopen_last_tab"].bool_or(prefs.reopen_last_tab);
    prefs.group_systems = root["group_systems"].bool_or(prefs.group_systems);
    prefs.continue_row = root["continue_row"].bool_or(prefs.continue_row);
    prefs.ask_player = root["ask_player"].bool_or(prefs.ask_player);
    prefs.setup_done = root["setup_done"].bool_or(prefs.setup_done);
    prefs.autostart = root["autostart"].bool_or(prefs.autostart);
    prefs.reopen_after_rest = root["reopen_after_rest"].bool_or(prefs.reopen_after_rest);
    prefs.hidden_tab_shown = root["hidden_tab_shown"].bool_or(prefs.hidden_tab_shown);
    prefs.title_music = root["title_music"].bool_or(prefs.title_music);
    prefs.every_version = root["every_version"].bool_or(prefs.every_version);
    if (const std::string language = root["language"].string_or("");
        language.size() <= 16 &&
        std::all_of(language.begin(), language.end(),
                    [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-'; }))
        prefs.language = language;
    prefs.moved_leave_shelf = root["moved_leave_shelf"].bool_or(prefs.moved_leave_shelf);
    prefs.auto_art = root["auto_art"].bool_or(prefs.auto_art);
    prefs.return_on_close = root["return_on_close"].bool_or(prefs.return_on_close);
    prefs.check_updates = root["check_updates"].bool_or(prefs.check_updates);
    prefs.auto_update = root["auto_update"].bool_or(prefs.auto_update);
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
        folder.emulator = value["emulator"].string_or("");
        if (valid_art_dir(folder.path) && folder.system.size() <= 80 &&
            folder.emulator.size() <= 16 && prefs.rom_folders.size() < 16)
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
    text += "  \"view\": " + json::quote(prefs.view) + ",\n";
    text +=
        std::string("  \"reopen_last_tab\": ") + (prefs.reopen_last_tab ? "true" : "false") + ",\n";
    text += std::string("  \"group_systems\": ") + (prefs.group_systems ? "true" : "false") + ",\n";
    text += std::string("  \"continue_row\": ") + (prefs.continue_row ? "true" : "false") + ",\n";
    text += std::string("  \"ask_player\": ") + (prefs.ask_player ? "true" : "false") + ",\n";
    text += std::string("  \"setup_done\": ") + (prefs.setup_done ? "true" : "false") + ",\n";
    text += std::string("  \"autostart\": ") + (prefs.autostart ? "true" : "false") + ",\n";
    text += std::string("  \"title_music\": ") + (prefs.title_music ? "true" : "false") + ",\n";
    text += std::string("  \"reopen_after_rest\": ") +
            (prefs.reopen_after_rest ? "true" : "false") + ",\n";
    text += std::string("  \"hidden_tab_shown\": ") + (prefs.hidden_tab_shown ? "true" : "false") +
            ",\n";
    text += std::string("  \"every_version\": ") + (prefs.every_version ? "true" : "false") + ",\n";
    text += "  \"language\": " + json::quote(prefs.language) + ",\n";
    text += std::string("  \"moved_leave_shelf\": ") +
            (prefs.moved_leave_shelf ? "true" : "false") + ",\n";
    text += std::string("  \"auto_art\": ") + (prefs.auto_art ? "true" : "false") + ",\n";
    text +=
        std::string("  \"return_on_close\": ") + (prefs.return_on_close ? "true" : "false") + ",\n";
    text += std::string("  \"check_updates\": ") + (prefs.check_updates ? "true" : "false") + ",\n";
    text += std::string("  \"auto_update\": ") + (prefs.auto_update ? "true" : "false") + ",\n";
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
                ", \"system\": " + json::quote(prefs.rom_folders[i].system) +
                ", \"emulator\": " + json::quote(prefs.rom_folders[i].emulator) + "}";
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
    text += "  \"tab_order\": [";
    for (std::size_t i = 0; i < prefs.tab_order.size(); ++i)
        text += (i == 0 ? "" : ", ") + json::quote(prefs.tab_order[i]);
    text += "],\n";
    text += "  \"hidden_titles\": [";
    for (std::size_t i = 0; i < prefs.hidden_titles.size(); ++i)
        text += (i == 0 ? "" : ", ") + json::quote(prefs.hidden_titles[i]);
    text += "],\n";
    text += "  \"game_players\": {";
    for (std::size_t i = 0; i < prefs.game_players.size(); ++i)
        text += std::string(i == 0 ? "\n" : ",\n") + "    " +
                json::quote(prefs.game_players[i].first) + ": " +
                json::quote(prefs.game_players[i].second);
    text += prefs.game_players.empty() ? "},\n" : "\n  },\n";
    text += "  \"title_names\": {";
    for (std::size_t i = 0; i < prefs.title_names.size(); ++i)
        text += std::string(i == 0 ? "\n" : ",\n") + "    " +
                json::quote(prefs.title_names[i].first) + ": " +
                json::quote(prefs.title_names[i].second);
    text += prefs.title_names.empty() ? "},\n" : "\n  },\n";
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
                ", \"view\": " + json::quote(entry.view) +
                ", \"hidden\": " + (entry.hidden ? "true" : "false") +
                ", \"name\": " + json::quote(entry.name) + "}";
    }
    text += prefs.tab_prefs.empty() ? "}\n}\n" : "\n  }\n}\n";
    return save::write_atomic(file_of(state_dir), text).empty();
}

} // namespace hui::orion
