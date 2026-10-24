// ORION Media Center - A title's details page: its cover, its facts, Play and Favourite.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/title_details.hpp"

#include "core/i18n.hpp"
#include "orion/drives.hpp"

#include <algorithm>
#include <thread>

#include <dirent.h>
#include <sys/stat.h>

namespace hui::orion
{

using i18n::tr;

namespace
{

// A game's folder is thousands of files at most; past this many the size
// shown is what was counted, marked as more.
constexpr int kMostFiles = 60000;
constexpr float kCoverHeight = 300.0f;
constexpr float kCoverGap = 24.0f;

ui::DetailItem row(std::string label, std::string value, int tag = 0)
{
    ui::DetailItem item;
    item.label = std::move(label);
    item.value = std::move(value);
    item.tag = tag;
    return item;
}

ui::DetailItem header(std::string label)
{
    ui::DetailItem item;
    item.label = std::move(label);
    item.header = true;
    return item;
}

} // namespace

std::vector<ui::DetailItem> title_detail_rows(const TitleFacts &facts, const std::string &size)
{
    std::vector<ui::DetailItem> items;
    ui::DetailItem play = row(tr("Play"), "", kDetailsPlay);
    play.focusable = true;
    items.push_back(play);
    ui::DetailItem star =
        row(facts.favourite ? tr("Remove from Favourites") : tr("Add to Favourites"), "",
            kDetailsFavourite);
    star.focusable = true;
    items.push_back(star);
    const auto add = [&](const char *label, const std::string &value, int tag = 0)
    {
        if (!value.empty())
            items.push_back(row(tr(label), value, tag));
    };
    items.push_back(header(tr("About")));
    add(i18n::marked("Title ID"), facts.id);
    add(i18n::marked("File"), facts.file);
    add(i18n::marked("Version"), facts.version);
    add(i18n::marked("Kind"), facts.kind);
    add(i18n::marked("Plays with"), facts.plays_with);
    items.push_back(header(tr("Where it is")));
    add(i18n::marked("Kept as"), facts.kept_as);
    add(i18n::marked("Drive"), facts.drive);
    add(i18n::marked("Size"), size, kDetailsSize);
    add(i18n::marked("Path"), facts.path);
    items.push_back(header(tr("Play history")));
    add(i18n::marked("Played for"),
        facts.play_time.empty() ? tr("Not played yet") : facts.play_time);
    add(i18n::marked("Last played"), facts.last_played);
    add(i18n::marked("Added"), facts.added);
    return items;
}

std::uint64_t folder_bytes(const std::string &path, int max_entries, bool *complete)
{
    std::uint64_t bytes = 0;
    int seen = 0;
    *complete = true;
    std::vector<std::string> todo = {path};
    while (!todo.empty())
    {
        const std::string folder = std::move(todo.back());
        todo.pop_back();
        DIR *dir = ::opendir(folder.c_str());
        if (dir == nullptr)
            continue;
        while (const dirent *entry = ::readdir(dir))
        {
            const std::string name = entry->d_name;
            if (name == "." || name == "..")
                continue;
            if (++seen > max_entries)
            {
                *complete = false;
                ::closedir(dir);
                return bytes;
            }
            const std::string full = folder + "/" + name;
            struct stat info = {};
            if (::stat(full.c_str(), &info) != 0)
                continue;
            if (S_ISDIR(info.st_mode))
                todo.push_back(full);
            else if (S_ISREG(info.st_mode))
                bytes += static_cast<std::uint64_t>(info.st_size);
        }
        ::closedir(dir);
    }
    return bytes;
}

TitleDetails::TitleDetails()
{
    sheet_.style.edge = ui::SheetEdge::right;
    sheet_.style.size = 680.0f;
    sheet_.style.title_size = 34.0f;
    sheet_.content = [this](ui::Canvas &canvas, const gfx::Rect &area, float opacity)
    { draw_content(canvas, area, opacity); };
    rows_.style.panel = false;
    rows_.style.copy_mark = false; // Play and Favourite act; nothing is copied
    rows_.style.label_share = 0.36f;
    rows_.style.max_lines = 2;
}

void TitleDetails::set_theme(const ui::Theme &theme, bool reduced_motion)
{
    sheet_.style.theme = rows_.style.theme = theme;
    sheet_.style.reduced_motion = rows_.style.reduced_motion = reduced_motion;
}

void TitleDetails::open(TitleFacts facts, ui::Feedback &feedback)
{
    facts_ = std::move(facts);
    sheet_.set_title(facts_.name);
    std::string size;
    count_.reset();
    size_shown_ = true;
    if (facts_.folder && !facts_.path.empty())
    {
        // Added up beside the screen; "…" until then.
        count_ = std::make_shared<Count>();
        size = "…";
        size_shown_ = false;
        std::thread(
            [count = count_, path = facts_.path]()
            {
                bool complete = true;
                count->bytes = folder_bytes(path, kMostFiles, &complete);
                count->complete = complete;
                count->done = true;
            })
            .detach();
    }
    else if (!facts_.path.empty())
    {
        struct stat info = {};
        if (::stat(facts_.path.c_str(), &info) == 0 && S_ISREG(info.st_mode))
            size = format_bytes(static_cast<std::uint64_t>(info.st_size));
    }
    rows_.set_items(title_detail_rows(facts_, size));
    rows_.set_focus(0);
    rows_.set_active(true);
    layout();
    rows_.enter();
    sheet_.open(feedback);
}

void TitleDetails::close(ui::Feedback &feedback)
{
    sheet_.close(feedback);
}

void TitleDetails::set_favourite(bool favourite)
{
    facts_.favourite = favourite;
    for (int i = 0; i < static_cast<int>(rows_.items().size()); ++i)
    {
        if (rows_.items()[static_cast<std::size_t>(i)].tag == kDetailsFavourite)
            rows_.item(i).label =
                favourite ? tr("Remove from Favourites") : tr("Add to Favourites");
    }
}

void TitleDetails::layout()
{
    const gfx::Rect area = sheet_.content_rect();
    const float top = facts_.texture != 0 ? kCoverHeight + kCoverGap : 0.0f;
    rows_.set_bounds({area.x, area.y + top, area.w, std::max(area.h - top, 0.0f)});
}

TitleDetails::Choice TitleDetails::handle(const InputFrame &input, ui::Feedback &feedback)
{
    if (!sheet_.is_open())
        return Choice::none;
    if (sheet_.handle(input, feedback) == ui::Event::cancelled)
        return Choice::closed;
    if (rows_.handle(input, feedback) != ui::Event::activated)
        return Choice::none;
    const int focus = rows_.focus();
    if (focus < 0)
        return Choice::none;
    const int tag = rows_.items()[static_cast<std::size_t>(focus)].tag;
    return tag == kDetailsPlay        ? Choice::play
           : tag == kDetailsFavourite ? Choice::favourite
                                      : Choice::none;
}

void TitleDetails::update(float dt)
{
    if (!size_shown_ && count_ != nullptr && count_->done.load())
    {
        size_shown_ = true;
        const std::string size =
            format_bytes(count_->bytes.load()) + (count_->complete.load() ? "" : "+");
        for (int i = 0; i < static_cast<int>(rows_.items().size()); ++i)
        {
            if (rows_.items()[static_cast<std::size_t>(i)].tag == kDetailsSize)
                rows_.item(i).value = size;
        }
    }
    sheet_.update(dt);
    rows_.update(dt);
}

void TitleDetails::draw(ui::Canvas &canvas) const
{
    sheet_.draw(canvas);
}

void TitleDetails::draw_content(ui::Canvas &canvas, const gfx::Rect &area, float opacity) const
{
    if (facts_.texture != 0)
    {
        // The cover at its own shape, as tall as the room allows.
        const float aspect = std::clamp(facts_.aspect, 0.4f, 2.5f);
        float h = kCoverHeight;
        float w = h * aspect;
        if (w > area.w)
        {
            w = area.w;
            h = w / aspect;
        }
        const gfx::Rect cover{area.x + (area.w - w) * 0.5f, area.y + (kCoverHeight - h) * 0.5f, w,
                              h};
        canvas.list.image(facts_.texture, cover, gfx::kFullUv,
                          gfx::Color{1.0f, 1.0f, 1.0f, opacity},
                          std::min(sheet_.style.theme.radius_card, 16.0f));
    }
    rows_.draw(canvas);
}

} // namespace hui::orion
