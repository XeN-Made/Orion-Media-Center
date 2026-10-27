// ORION Media Center - The RomM Games screen: the server's games by system, picked and downloaded.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/romm_browser.hpp"

#include "core/i18n.hpp"
#include "library/art_match.hpp"
#include "orion/drives.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <map>

#include <sys/stat.h>

namespace hui::orion
{

namespace
{

using i18n::tr;
using i18n::trf;

constexpr float kFilterTop = 6.0f; // the filter's line, from the sheet's content top
constexpr float kTabsTop = 40.0f;  // the systems' tabs
constexpr float kTabsHeight = 56.0f;
constexpr float kListTop = 112.0f; // the games
constexpr float kHintsHeight = 44.0f;
constexpr float kCheckWidth = 52.0f;

bool is_file(const std::string &path)
{
    struct stat info = {};
    return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

} // namespace

std::vector<RommEntry> romm_entries(const std::vector<RommGame> &games,
                                    const std::vector<RommSave> &saves,
                                    const library::Emulator &emulator,
                                    const library::OwnedGames &owned, const std::string &base,
                                    const std::string &folder)
{
    std::map<long long, std::pair<int, int>> kept; // game: saves, states
    for (const RommSave &save : saves)
    {
        auto &count = kept[save.rom_id];
        (save.state ? count.second : count.first) += 1;
    }
    std::vector<RommEntry> entries;
    entries.reserve(games.size());
    for (const RommGame &game : games)
    {
        RommEntry entry;
        entry.system = library::system_of_folder(game.fs_slug, emulator);
        if (entry.system.empty())
            continue; // a system ORION does not know: nothing would play it
        entry.game = game;
        entry.short_name = library::short_system_name(entry.system);
        entry.path = romm_game_path(folder, game);
        entry.url = romm_content_url(base, game);
        entry.downloaded = is_file(entry.path);
        entry.owned = !entry.downloaded && owned.has(entry.system, entry.path, game.name);
        if (const auto found = kept.find(game.id); found != kept.end())
        {
            entry.saves = found->second.first;
            entry.states = found->second.second;
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<std::string> romm_entry_systems(const std::vector<RommEntry> &entries)
{
    std::vector<std::string> systems;
    for (const RommEntry &entry : entries)
    {
        if (std::find(systems.begin(), systems.end(), entry.short_name) == systems.end())
            systems.push_back(entry.short_name);
    }
    std::sort(systems.begin(), systems.end());
    return systems;
}

std::vector<int> romm_view(const std::vector<RommEntry> &entries, const std::string &system,
                           RommFilter filter, const std::string &query)
{
    std::vector<int> view;
    const std::string needle = library::art_key(query);
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const RommEntry &entry = entries[i];
        if (!system.empty() && entry.short_name != system)
            continue;
        if (!needle.empty() && library::art_key(entry.game.name).find(needle) == std::string::npos)
            continue;
        if (filter == RommFilter::missing && (entry.owned || entry.downloaded))
            continue;
        if (filter == RommFilter::saves && entry.saves + entry.states == 0)
            continue;
        view.push_back(static_cast<int>(i));
    }
    std::stable_sort(view.begin(), view.end(),
                     [&](int a, int b)
                     {
                         return library::art_key(entries[static_cast<std::size_t>(a)].game.name) <
                                library::art_key(entries[static_cast<std::size_t>(b)].game.name);
                     });
    return view;
}

RommBrowser::RommBrowser()
{
    sheet_.style.edge = ui::SheetEdge::right;
    sheet_.style.size = 1320.0f;
    sheet_.style.title_size = 34.0f;
    sheet_.set_title("RomM");
    sheet_.content = [this](ui::Canvas &canvas, const gfx::Rect &area, float opacity)
    { draw_content(canvas, area, opacity); };
    list_.style.leading_width = kCheckWidth;
    list_.style.row_height = 70.0f;
    list_.style.title_size = 26.0f;
    list_.style.value_size = 22.0f;
    list_.style.entrance_step = 0.0f; // a system of thousands: no stagger
    list_.leading =
        [this](ui::Canvas &canvas, const gfx::Rect &row, const ui::ListItem &item, int, float)
    {
        const auto at = static_cast<std::size_t>(item.tag);
        if (at >= entries_.size() || entries_[at].owned || entries_[at].downloaded)
            return;
        ui::Painter paint(canvas.list, canvas.fonts, list_.style.theme);
        const float size = 30.0f;
        paint.checkbox({row.x + 14.0f, row.y + (row.h - size) * 0.5f, size, size},
                       picked_.count(entries_[at].game.id) != 0 ? 1.0f : 0.0f, ui::Look());
    };
}

void RommBrowser::set_theme(const ui::Theme &theme, bool reduced_motion)
{
    sheet_.style.theme = tabs_.style.theme = list_.style.theme = theme;
    sheet_.style.reduced_motion = tabs_.style.reduced_motion = list_.style.reduced_motion =
        reduced_motion;
}

void RommBrowser::open(std::vector<RommEntry> entries, bool saves_read, ui::Feedback &feedback)
{
    saves_read_ = saves_read;
    entries_ = std::move(entries);
    systems_ = romm_entry_systems(entries_);
    picked_.clear();
    query_.clear();
    std::vector<ui::TabItem> tabs;
    // Saves first, always: with none, it says why.
    saves_tab_ = !entries_.empty();
    if (saves_tab_)
    {
        ui::TabItem tab;
        tab.label = tr("Saves");
        tabs.push_back(std::move(tab));
    }
    for (const std::string &system : systems_)
    {
        ui::TabItem tab;
        tab.label = system;
        tabs.push_back(std::move(tab));
    }
    tabs_.set_tabs(std::move(tabs));
    show_system(0);
    list_.set_active(true);
    sheet_.open(feedback);
}

void RommBrowser::close(ui::Feedback &feedback)
{
    sheet_.close(feedback);
}

void RommBrowser::show_system(int index)
{
    if (systems_.empty())
    {
        view_.clear();
        list_.set_items({});
        return;
    }
    tabs_.set_active(std::clamp(index, 0, static_cast<int>(systems_.size()) - (saves_tab_ ? 0 : 1)),
                     true);
    rebuild_list();
    list_.set_focus(0, true);
}

void RommBrowser::rebuild_list()
{
    const int active = std::max(tabs_.active(), 0);
    // Saves: every system's games with one, whatever the filter.
    if (saves_tab_ && active == 0)
        view_ = romm_view(entries_, {}, RommFilter::saves, query_);
    else
    {
        const int system = active - (saves_tab_ ? 1 : 0);
        view_ =
            system < 0 || system >= static_cast<int>(systems_.size())
                ? std::vector<int>()
                : romm_view(entries_, systems_[static_cast<std::size_t>(system)], filter_, query_);
    }
    std::vector<ui::ListItem> items;
    items.reserve(view_.size());
    for (const int index : view_)
    {
        const RommEntry &entry = entries_[static_cast<std::size_t>(index)];
        ui::ListItem item;
        item.title = entry.game.name;
        item.subtitle = saves_tab_ && active == 0 ? entry.short_name + "  ·  " + entry.game.fs_name
                                                  : entry.game.fs_name;
        item.tag = index;
        item.value = entry.game.size > 0 ? format_bytes(entry.game.size) : std::string();
        if (entry.downloaded)
            item.badge = tr("Downloaded");
        else if (entry.owned)
            item.badge = tr("On the console");
        else if (entry.saves + entry.states > 0)
            item.badge = i18n::plural(entry.saves + entry.states, "{0} save", "{0} saves");
        item.disabled = entry.owned || entry.downloaded;
        items.push_back(std::move(item));
    }
    const int focus = list_.focus();
    list_.set_items(std::move(items));
    if (!view_.empty())
        list_.set_focus(std::clamp(focus, 0, static_cast<int>(view_.size()) - 1), true);
}

std::string RommBrowser::filter_name() const
{
    switch (filter_)
    {
    case RommFilter::missing:
        return tr("Not on the console");
    case RommFilter::saves:
        return tr("With saves on RomM");
    case RommFilter::all:
        break;
    }
    return tr("All of them");
}

RommBrowser::Choice RommBrowser::handle(const InputFrame &input, ui::Feedback &feedback)
{
    if (!sheet_.is_open())
        return Choice::none;
    if (sheet_.handle(input, feedback) == ui::Event::cancelled)
        return Choice::closed;
    if (input.is_pressed(Action::jump_prev) || input.is_pressed(Action::jump_next))
    {
        if (tabs_.step(input.is_pressed(Action::jump_next) ? 1 : -1, input, feedback) ==
            ui::Event::changed)
        {
            rebuild_list();
            list_.set_focus(0, true);
        }
        return Choice::none;
    }
    if (input.is_pressed(Action::north))
    {
        filter_ = filter_ == RommFilter::missing ? RommFilter::saves
                  : filter_ == RommFilter::saves ? RommFilter::all
                                                 : RommFilter::missing;
        rebuild_list();
        ui::play_cue(feedback, list_.style, list_.style.sounds.change, 960.0f);
        return Choice::none;
    }
    if (input.is_pressed(Action::menu))
        return Choice::search;
    if (input.is_pressed(Action::west))
    {
        if (picked_.empty())
        {
            ui::play_cue(feedback, list_.style, list_.style.sounds.refuse, 960.0f);
            return Choice::none;
        }
        return Choice::download;
    }
    if (list_.handle(input, feedback) == ui::Event::activated && list_.focus() >= 0 &&
        list_.focus() < static_cast<int>(view_.size()))
    {
        const RommEntry &entry =
            entries_[static_cast<std::size_t>(view_[static_cast<std::size_t>(list_.focus())])];
        if (!entry.owned && !entry.downloaded && !picked_.erase(entry.game.id))
            picked_.insert(entry.game.id);
    }
    return Choice::none;
}

void RommBrowser::set_query(std::string query)
{
    query_ = std::move(query);
    rebuild_list();
    list_.set_focus(0, true);
}

std::vector<RommEntry> RommBrowser::take_picked()
{
    std::vector<RommEntry> picked;
    for (const RommEntry &entry : entries_)
    {
        if (picked_.count(entry.game.id) != 0)
            picked.push_back(entry);
    }
    picked_.clear();
    return picked;
}

void RommBrowser::update(float dt)
{
    area_ = sheet_.content_rect();
    tabs_.set_bounds({area_.x, area_.y + kTabsTop, area_.w, kTabsHeight});
    list_.set_bounds(
        {area_.x, area_.y + kListTop, area_.w, std::max(area_.h - kListTop - kHintsHeight, 0.0f)});
    sheet_.update(dt);
    tabs_.update(dt);
    list_.update(dt);
}

void RommBrowser::draw(ui::Canvas &canvas) const
{
    sheet_.draw(canvas);
}

void RommBrowser::draw_content(ui::Canvas &canvas, const gfx::Rect &area, float) const
{
    const ui::Theme &theme = sheet_.style.theme;
    ui::Painter paint(canvas.list, canvas.fonts, theme);
    const int shown = static_cast<int>(view_.size());
    const bool saves = saves_tab_ && tabs_.active() == 0;
    std::string line = trf("{0}  ·  {1}", {saves ? tr("With saves on RomM") : filter_name(),
                                           i18n::plural(shown, "{0} game", "{0} games")});
    if (!query_.empty())
        line += "  ·  " + trf("Looking for “{0}”", {query_});
    paint.body(line, area.x, area.y + kFilterTop + 20.0f, 22.0f, theme.text_muted);
    tabs_.draw(canvas);
    if (view_.empty())
        paint.body(systems_.empty() ? tr("Refresh RomM's game list in Settings, RomM first")
                   : !query_.empty()
                       ? tr("No game here has that in its name: Options searches again")
                   : saves && !saves_read_
                       ? tr("ORION has not read your saves from RomM yet: Settings, RomM shows why")
                   : saves ? tr("No saves on RomM for these games yet")
                           : tr("No games here with this filter: Triangle changes it"),
                   area.x, area.y + kListTop + 60.0f, 24.0f, theme.text_muted);
    else
        list_.draw(canvas);
    const std::string hints =
        trf("Cross tick  ·  Square download {0}  ·  Triangle filter  ·  Options search  ·  L2 R2 "
            "system  ·  Circle back",
            {std::to_string(picked_.size())});
    paint.body(hints, area.x, area.y + area.h - 12.0f, 20.0f, theme.text_muted);
}

} // namespace hui::orion
