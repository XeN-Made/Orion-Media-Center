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
    if (tab(tab_id) == nullptr || !library::is_title_id(title))
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

    prefs.last_tab = root["last_tab"].string_or(prefs.last_tab);
    prefs.sort = root["sort"].string_or(prefs.sort);
    prefs.reopen_last_tab = root["reopen_last_tab"].bool_or(prefs.reopen_last_tab);
    prefs.moved_leave_shelf = root["moved_leave_shelf"].bool_or(prefs.moved_leave_shelf);
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
    text += prefs.placements.empty() ? "}\n}\n" : "\n  }\n}\n";
    return save::write_atomic(file_of(state_dir), text).empty();
}

} // namespace hui::orion
