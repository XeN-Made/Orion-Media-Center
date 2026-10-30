// ORION Media Center - PS4 and PS5 packages (.pkg): what one is, and whether it is installed.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/packages.hpp"

#include "core/save_file.hpp"
#include "orion/registered_titles.hpp"

namespace hui::orion
{

namespace
{

bool upper(char c)
{
    return c >= 'A' && c <= 'Z';
}

bool digit(char c)
{
    return c >= '0' && c <= '9';
}

// "UP0000-PPSA08560_00-FULLGAMEUNLOCK00": two capitals and four digits, the
// title ID (four capitals, five digits), "_" and two digits, then a label
// of sixteen capitals, digits or underscores.
bool content_id_at(std::string_view text, std::size_t at)
{
    constexpr std::size_t kLength = 36;
    if (at + kLength > text.size())
        return false;
    const std::string_view id = text.substr(at, kLength);
    for (std::size_t i = 0; i < kLength; ++i)
    {
        const char c = id[i];
        bool ok = false;
        if (i < 2 || (i >= 7 && i < 11))
            ok = upper(c);
        else if (i < 6 || (i >= 11 && i < 16) || i == 17 || i == 18)
            ok = digit(c);
        else if (i == 6 || i == 19)
            ok = c == '-';
        else if (i == 16)
            ok = c == '_';
        else
            ok = upper(c) || digit(c) || c == '_';
        if (!ok)
            return false;
    }
    return true;
}

} // namespace

std::string find_content_id(std::string_view bytes)
{
    for (std::size_t at = 0; at + 36 <= bytes.size(); ++at)
    {
        if (upper(bytes[at]) && content_id_at(bytes, at))
            return std::string(bytes.substr(at, 36));
    }
    return {};
}

bool is_ps5_package(std::string_view head)
{
    return head.substr(0, 4) == std::string_view("\x7f"
                                                 "FIH",
                                                 4);
}

bool read_ps5_header(std::string_view header, PackageId *id)
{
    if (!read_package(header, id) || id->kind != PackageKind::ps4)
    {
        *id = PackageId();
        return false;
    }
    id->kind = PackageKind::ps5;
    return true;
}

bool read_package(std::string_view head, PackageId *id)
{
    *id = PackageId();
    if (head.substr(0, 4) == std::string_view("\x7f"
                                              "CNT",
                                              4))
        id->kind = PackageKind::ps4;
    else if (head.substr(0, 4) == std::string_view("\x7f"
                                                   "FIH",
                                                   4))
        id->kind = PackageKind::ps5;
    else if (head.substr(0, 8) == "PS5MPKG1")
        id->kind = PackageKind::parts;
    else
        return false;
    // A PS4 header has it at 0x40; elsewhere it is looked for.
    id->content_id = id->kind == PackageKind::ps4 && content_id_at(head, 0x40)
                         ? std::string(head.substr(0x40, 36))
                         : find_content_id(head);
    if (id->content_id.empty())
    {
        id->kind = PackageKind::none;
        return false;
    }
    id->title_id = id->content_id.substr(7, 9);
    id->label = id->content_id.substr(20);
    return true;
}

Installed read_installed(const std::string &state_dir)
{
    Installed installed;
    const std::string apps = state_dir + "/" + kSystemApps;
    std::string text;
    if (save::read_file(apps + "/index.txt", &text, 64u << 10))
    {
        for (std::size_t start = 0; start < text.size();)
        {
            std::size_t end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();
            const std::string title = text.substr(start, end - start);
            start = end + 1;
            if (title.size() != 9)
                continue;
            std::string param;
            const bool read =
                save::read_file(apps + "/" + title + "/param.json", &param, 1u << 20) ||
                save::read_file(apps + "/" + title + "/param.sfo", &param, 1u << 20);
            installed.apps[title] = read ? find_content_id(param) : std::string();
        }
    }
    text.clear();
    if (save::read_file(apps + "/addcont.txt", &text, 256u << 10))
    {
        for (std::size_t start = 0; start < text.size();)
        {
            std::size_t end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();
            const std::string line = text.substr(start, end - start);
            start = end + 1;
            const std::size_t tab = line.find('\t');
            if (tab != std::string::npos)
                installed.addcont.insert(line.substr(0, tab) + "/" + line.substr(tab + 1));
        }
    }
    return installed;
}

PackageState package_state(const PackageId &id, const Installed &installed)
{
    if (id.kind == PackageKind::none)
        return PackageState::unknown;
    if (installed.addcont.count(id.title_id + "/" + id.label) != 0)
        return PackageState::installed;
    const auto app = installed.apps.find(id.title_id);
    if (app == installed.apps.end())
        return PackageState::no_game;
    // The game's own package (or an update of it, which shares its ID).
    if (app->second == id.content_id)
        return PackageState::installed;
    return PackageState::not_installed;
}

void mark_installed(const PackageId &id, Installed *installed)
{
    if (id.kind == PackageKind::none)
        return;
    const auto app = installed->apps.find(id.title_id);
    if (app == installed->apps.end())
        installed->apps[id.title_id] = id.content_id;
    else if (app->second != id.content_id)
        installed->addcont.insert(id.title_id + "/" + id.label);
}

} // namespace hui::orion
