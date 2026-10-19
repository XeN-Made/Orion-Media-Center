// ps5-homebrew-ui - Design "Library": the titles installed on this console.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORION's home: every title ShadowMountPlus can mount (library/library.hpp),
// as a grid of covers on five shelves (All, Games, Homebrew, Media,
// Favourites), sorted four ways and filtered live from the on-screen
// keyboard. Favourites are written to <state_dir>/favourites.json the moment
// they change. Titles without artwork get a monogram on a colour derived from
// their ID, so the same title always looks the same.

#include "concepts/concepts.hpp"

#include "core/tween.hpp"
#include "library/favourites.hpp"
#include "ui/components/grid.hpp"
#include "ui/components/keyboard.hpp"
#include "ui/components/tabs.hpp"
#include "ui/glyphs.hpp"
#include "ui/motion.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace hui::concepts
{

namespace
{

using gfx::Color;
using gfx::Rect;
using library::Title;

constexpr float kMargin = 96.0f;
constexpr float kTabsTop = 204.0f;
constexpr float kGridTop = 280.0f;
constexpr float kGridBottom = 972.0f;
// Seven columns keep two whole rows in view, so scrolling never leaves a
// single row alone on the screen.
constexpr int kColumns = 7;
constexpr std::size_t kMaxQuery = 40; // bytes
// The search panel sits under the first row of covers, so the results of
// what is typed stay in view above it.
constexpr Rect kSearchPanel{544.0f, 590.0f, 832.0f, 378.0f};
constexpr Rect kSearchField{560.0f, 606.0f, 800.0f, 56.0f};
constexpr Rect kKeysRect{560.0f, 670.0f, 800.0f, 290.0f};

enum Tab : int
{
    kAll,
    kGames,
    kHomebrew,
    kMedia,
    kFavourites,
    kTabCount,
};
constexpr const char *kTabNames[kTabCount] = {"All", "Games", "Homebrew", "Media", "Favourites"};

enum class Sort : int
{
    name,
    name_reversed,
    recent,
    platform,
    count,
};
constexpr const char *kSortNames[] = {"Name A\u2013Z", "Name Z\u2013A", "Recently added",
                                      "Platform"};

constexpr const char *kTechniques[] = {
    "Reads the console's library the way ShadowMountPlus does: folders, images, icon cache",
    "Shelves from the metadata itself: DRM-free titles are homebrew, the Media area is media",
    "Live search from the on-screen keyboard; the grid filters as you type",
    "Favourites kept in a JSON file that survives reinstalling the app",
    "Monogram covers in a colour hashed from the title ID for titles without art",
};

constexpr app::TourStep kTour[] = {
    {0.5f, 0, Direction::right},
    {0.4f, action_bit(Action::jump_next)},
    {0.6f, action_bit(Action::jump_next), Direction::none, "games"},
    {0.6f, action_bit(Action::west), Direction::none, "homebrew"},
    {0.5f, action_bit(Action::jump_prev)},
    {0.3f, action_bit(Action::jump_prev)},
    {0.6f, action_bit(Action::menu), Direction::none, "favourite"},
    {0.6f, action_bit(Action::north), Direction::none, "sorted"},
    {0.7f, 0, Direction::none, "search"},
    // Type "re": down to the letters, along to r, then back to e.
    {0.15f, 0, Direction::down},
    {0.15f, 0, Direction::right},
    {0.15f, 0, Direction::right},
    {0.15f, 0, Direction::right},
    {0.2f, action_bit(Action::confirm)},
    {0.15f, 0, Direction::left},
    {0.2f, action_bit(Action::confirm)},
    {0.6f, action_bit(Action::menu), Direction::none, "typed"},
    {0.6f, 0, Direction::none, "filtered"},
};

const ui::Theme &library_theme()
{
    for (const ui::Theme &theme : ui::themes())
    {
        if (std::strcmp(theme.id, "orion") == 0)
            return theme;
    }
    return ui::themes()[0];
}

// FNV-1a of the ID, then a finaliser: IDs differ only in their last digits,
// and without the mixing neighbouring IDs land on neighbouring hues.
Color hashed_color(const std::string &id, float saturation, float value)
{
    std::uint32_t hash = 2166136261u;
    for (char c : id)
        hash = (hash ^ static_cast<unsigned char>(c)) * 16777619u;
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    hash *= 0x846ca68bu;
    hash ^= hash >> 16;
    const float hue = static_cast<float>(hash % 360u) / 60.0f;
    const float chroma = value * saturation;
    const float x = chroma * (1.0f - std::fabs(std::fmod(hue, 2.0f) - 1.0f));
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    switch (static_cast<int>(hue))
    {
    case 0:
        r = chroma, g = x;
        break;
    case 1:
        r = x, g = chroma;
        break;
    case 2:
        g = chroma, b = x;
        break;
    case 3:
        g = x, b = chroma;
        break;
    case 4:
        r = x, b = chroma;
        break;
    default:
        r = chroma, b = x;
        break;
    }
    const float m = value - chroma;
    return Color{r + m, g + m, b + m, 1.0f};
}

// Up to two letters: the initials of the first two words.
std::string monogram(const std::string &name)
{
    std::string letters;
    bool start = true;
    for (char c : name)
    {
        const bool letter = std::isalnum(static_cast<unsigned char>(c)) != 0;
        if (letter && start)
        {
            letters += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (letters.size() == 2)
                break;
        }
        start = !letter;
    }
    return letters;
}

std::string describe(const Title &title)
{
    std::string text = library::platform_name(title.platform);
    if (!text.empty())
        text += "  \xc2\xb7  ";
    text += title.source == library::Source::image ? "Image" : "Folder";
    return text;
}

const char *shelf_name(library::Shelf shelf)
{
    switch (shelf)
    {
    case library::Shelf::games:
        return "Game";
    case library::Shelf::homebrew:
        return "Homebrew";
    case library::Shelf::media:
        return "Media";
    }
    return "";
}

std::string folded(std::string_view text)
{
    std::string out(text);
    for (char &c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

class Library final : public app::Concept
{
  public:
    explicit Library(app::Context &context)
        : context_(context), theme_(library_theme()),
          favourites_(library::load_favourites(context.library.state_dir))
    {
        tabs_.style.theme = theme_;
        tabs_.style.kind = ui::TabKind::pill;
        tabs_.style.height = 52.0f;
        tabs_.style.text_size = 21.0f;
        tabs_.style.badge_size = 16.0f;
        tabs_.style.padding = 20.0f;
        tabs_.style.on_page = true;
        tabs_.style.focus_ring = false;
        tabs_.set_bounds({kMargin, kTabsTop, 1080.0f, 56.0f});

        grid_.style.theme = theme_;
        grid_.style.columns = kColumns;
        grid_.style.gap_x = 28.0f;
        grid_.style.gap_y = 30.0f;
        // Every edge refuses, as the home screen's own grids do.
        grid_.style.wrap = ui::GridWrap::none;
        grid_.style.card.title_size = 22.0f;
        grid_.style.card.subtitle_size = 17.0f;
        grid_.style.card.badge_corner = ui::CardCorner::top_right;
        grid_.style.card.glow = true;
        grid_.style.card.radius = 22.0f; // soft covers, not tiles
        grid_.art = [this](ui::Canvas &canvas, const Rect &art, float radius,
                           const ui::CardItem &item, float /*focus*/)
        { draw_art(canvas, art, radius, item); };
        grid_.set_bounds(
            {kMargin, kGridTop, gfx::kVirtualWidth - 2.0f * kMargin, kGridBottom - kGridTop});

        keys_.style.theme = theme_;
        keys_.style.bindings = ui::KeyboardBindings::standard();
        keys_.style.bindings.done = Action::menu;
        keys_.style.max_length = static_cast<int>(kMaxQuery);
        keys_.set_bounds(kKeysRect);
        keys_.on_text = [this](std::string_view utf8)
        {
            if (query_.size() + utf8.size() <= kMaxQuery)
            {
                query_ += utf8;
                rebuild();
            }
        };
        keys_.on_backspace = [this]
        {
            // Drop one whole UTF-8 character: continuation bytes, then its lead.
            while (!query_.empty() && (static_cast<unsigned char>(query_.back()) & 0xc0) == 0x80)
                query_.pop_back();
            if (!query_.empty())
                query_.pop_back();
            rebuild();
        };

        rebuild();
    }

    const app::ConceptInfo &info() const override
    {
        static const app::ConceptInfo kInfo{
            "library",
            "Library",
            "Every title on this console, as ShadowMountPlus sees it",
            "src/concepts/library.cpp",
            audio::SoundSet::glass,
            library_theme().accent,
            kTechniques,
        };
        return kInfo;
    }

    void enter() override
    {
        age_ = 0.0f;
        grid_.enter();
    }

    void update(const InputFrame &input, float dt, app::Feedback &feedback) override
    {
        age_ += dt;
        clock_ += dt;
        const bool reduced = context_.settings.reduced_motion;
        grid_.style.reduced_motion = tabs_.style.reduced_motion = keys_.style.reduced_motion =
            reduced;

        if (searching_)
            update_search(input, feedback);
        else
            update_browse(input, feedback);

        tabs_.update(dt);
        grid_.update(dt);
        keys_.update(dt);
        keyboard_shown_.target = searching_ ? 1.0f : 0.0f;
        keyboard_shown_.update(dt, reduced ? 60.0f : 16.0f);
    }

    void draw(app::Frame &frame) const override
    {
        const bool still = context_.settings.reduced_motion;
        const float drift = still ? 0.0f : clock_;
        frame.backdrop = theme_.backdrop;
        frame.backdrop.time = drift;
        // The star layers slide at different fractions of the camera: a slow
        // pan is enough to make the sky feel deep.
        frame.backdrop.params[0] = drift * 6.0f;
        frame.backdrop.params[1] = std::sin(drift * 0.05f) * 40.0f;
        if (theme_.style == ui::SurfaceStyle::glass)
        {
            // Nebula clouds: large, faint and slow. They are what the frosted
            // glass picks its colour up from.
            struct Cloud
            {
                float x, y, size, speed;
                std::uint32_t colour;
                float alpha;
            };
            constexpr Cloud kClouds[] = {
                {300.0f, 300.0f, 900.0f, 0.05f, 0x6a3cff, 0.42f},
                {1520.0f, 240.0f, 820.0f, 0.04f, 0x1aa6d9, 0.34f},
                {1100.0f, 840.0f, 960.0f, 0.03f, 0xc04bd6, 0.3f},
                {380.0f, 920.0f, 700.0f, 0.06f, 0x3b4bff, 0.3f},
            };
            for (int i = 0; i < 4; ++i)
            {
                const Cloud &c = kClouds[i];
                const float phase = drift * c.speed + static_cast<float>(i) * 1.9f;
                const float cx = c.x + std::sin(phase) * 120.0f;
                const float cy = c.y + std::cos(phase * 1.2f) * 80.0f;
                frame.scene.shadow({cx - c.size * 0.5f, cy - c.size * 0.5f, c.size, c.size},
                                   c.size * 0.5f, c.size * 0.55f, Color::rgb(c.colour, c.alpha));
            }
            frame.glass = true;
        }

        gfx::DrawList &list = frame.overlay;
        const std::uint32_t glass = frame.glass ? frame.glass_texture : 0;
        ui::Canvas canvas{list, context_.fonts, glass, clock_};
        ui::Painter paint(list, context_.fonts, theme_, glass);
        list.push_opacity(tween::cubic_out(age_ / 0.35f));

        draw_header(list, paint);
        tabs_.draw(canvas);
        draw_tab_sheen(list);
        draw_query(list, paint);
        if (grid_.count() > 0)
        {
            grid_.draw(canvas);
            draw_details(list, paint);
        }
        else
        {
            draw_empty(list, paint);
        }
        if (keyboard_shown_.value > 0.01f)
            draw_keyboard(canvas, paint);
        else
            draw_hints(list, paint);
        list.pop_opacity();
    }

    std::span<const app::TourStep> tour() const override
    {
        return kTour;
    }

  private:
    // ---- input ----

    void update_browse(const InputFrame &input, app::Feedback &feedback)
    {
        // L1 / R1 change shelf; L2 / R2 do too, which is also what the tour
        // uses, since L1 / R1 belong to the shell wherever it holds several
        // designs (the PC tools).
        const bool next =
            input.is_pressed(Action::page_next) || input.is_pressed(Action::jump_next);
        const bool previous =
            input.is_pressed(Action::page_prev) || input.is_pressed(Action::jump_prev);
        if (next || previous)
        {
            const int direction = next ? 1 : -1;
            if (tabs_.step(direction, input, feedback) == ui::Event::changed)
                rebuild();
            return;
        }
        if (input.is_pressed(Action::north))
        {
            searching_ = true;
            keys_.set_length(static_cast<int>(query_.size()));
            feedback.play(audio::Cue::modal_open);
            return;
        }
        if (input.is_pressed(Action::menu))
        {
            sort_ =
                static_cast<Sort>((static_cast<int>(sort_) + 1) % static_cast<int>(Sort::count));
            feedback.play(audio::Cue::tab);
            rebuild();
            return;
        }
        if (input.is_pressed(Action::back) && !query_.empty())
        {
            query_.clear();
            feedback.play(audio::Cue::erase);
            rebuild();
            return;
        }
        if (input.is_pressed(Action::west) && grid_.count() > 0)
        {
            toggle_favourite(feedback);
            return;
        }
        if (grid_.count() > 0)
            grid_.handle(input, feedback);
    }

    void update_search(const InputFrame &input, app::Feedback &feedback)
    {
        keys_.set_length(static_cast<int>(query_.size()));
        const ui::Event event = keys_.handle(input, feedback);
        if (event == ui::Event::activated || event == ui::Event::cancelled)
        {
            searching_ = false;
            feedback.play(audio::Cue::modal_close);
        }
    }

    void toggle_favourite(app::Feedback &feedback)
    {
        const Title &title = titles()[static_cast<std::size_t>(view_[focus_index()])];
        const auto found = std::find(favourites_.begin(), favourites_.end(), title.id);
        if (found == favourites_.end())
        {
            favourites_.push_back(title.id);
            feedback.play(audio::Cue::favorite_on);
        }
        else
        {
            favourites_.erase(found);
            feedback.play(audio::Cue::favorite_off);
        }
        library::save_favourites(context_.library.state_dir, favourites_);
        // On the Favourites shelf an unstarred title leaves the view.
        rebuild();
    }

    // ---- the view ----

    const std::vector<Title> &titles() const
    {
        return context_.library.titles;
    }

    bool favourite(const std::string &id) const
    {
        return std::find(favourites_.begin(), favourites_.end(), id) != favourites_.end();
    }

    bool on_tab(const Title &title, int tab) const
    {
        switch (tab)
        {
        case kGames:
            return library::shelf_of(title) == library::Shelf::games;
        case kHomebrew:
            return library::shelf_of(title) == library::Shelf::homebrew;
        case kMedia:
            return library::shelf_of(title) == library::Shelf::media;
        case kFavourites:
            return favourite(title.id);
        default:
            return true;
        }
    }

    int focus_index() const
    {
        return std::clamp(grid_.focus(), 0, std::max(0, static_cast<int>(view_.size()) - 1));
    }

    // Recomputes what the grid shows from the tab, the query and the sort,
    // keeping the focus on the same title when it is still there.
    void rebuild()
    {
        std::string focused;
        if (!view_.empty())
            focused = titles()[static_cast<std::size_t>(view_[focus_index()])].id;

        std::vector<ui::TabItem> tabs;
        for (int tab = 0; tab < kTabCount; ++tab)
        {
            ui::TabItem item;
            item.label = kTabNames[tab];
            item.badge = static_cast<int>(std::count_if(
                titles().begin(), titles().end(), [&](const Title &t) { return on_tab(t, tab); }));
            tabs.push_back(std::move(item));
        }
        const int active = tabs_.tabs().empty() ? 0 : tabs_.active();
        tabs_.set_tabs(std::move(tabs));
        tabs_.set_active(active, true);

        const std::string needle = folded(query_);
        view_.clear();
        for (std::size_t i = 0; i < titles().size(); ++i)
        {
            const Title &title = titles()[i];
            if (!on_tab(title, active))
                continue;
            if (!needle.empty() && folded(title.name).find(needle) == std::string::npos &&
                folded(title.id).find(needle) == std::string::npos)
                continue;
            view_.push_back(static_cast<int>(i));
        }
        // The scan already sorted by name; every other order is a stable sort
        // on top of it, so ties stay alphabetical.
        switch (sort_)
        {
        case Sort::name_reversed:
            std::reverse(view_.begin(), view_.end());
            break;
        case Sort::recent:
            std::stable_sort(view_.begin(), view_.end(),
                             [&](int a, int b) { return titles()[a].added > titles()[b].added; });
            break;
        case Sort::platform:
            std::stable_sort(view_.begin(), view_.end(), [&](int a, int b)
                             { return titles()[a].platform < titles()[b].platform; });
            break;
        default:
            break;
        }

        std::vector<ui::CardItem> items;
        int focus = 0;
        for (std::size_t i = 0; i < view_.size(); ++i)
        {
            const Title &title = titles()[static_cast<std::size_t>(view_[i])];
            if (title.id == focused)
                focus = static_cast<int>(i);
            ui::CardItem item;
            item.title = title.name;
            item.subtitle = describe(title);
            item.badge = title.category == library::Category::media ? "MEDIA" : "";
            item.texture = title.texture;
            item.top = hashed_color(title.id, 0.55f, 0.42f);
            item.bottom = hashed_color(title.id, 0.7f, 0.2f);
            item.accent = hashed_color(title.id, 0.6f, 1.0f);
            item.tag = view_[i];
            items.push_back(std::move(item));
        }
        grid_.set_items(std::move(items));
        if (!view_.empty())
            grid_.set_focus(focus, true);
    }

    // ---- drawing ----

    void draw_header(gfx::DrawList &list, const ui::Painter &paint) const
    {
        const ui::Fonts &fonts = context_.fonts;
        const float orbit = context_.settings.reduced_motion ? 0.8f : clock_ * 0.9f;
        list.glow({kMargin, 88.0f, 22.0f, 22.0f}, 11.0f, 14.0f, theme_.accent.with_alpha(0.45f));
        list.ring(kMargin + 11.0f, 99.0f, 11.0f, 2.0f, theme_.accent.with_alpha(0.85f));
        list.circle(kMargin + 11.0f + std::cos(orbit) * 11.0f, 99.0f + std::sin(orbit) * 11.0f,
                    3.5f, Color::rgb(0xffffff));
        ui::text(list, fonts.semibold, "ORION MEDIA CENTER", kMargin + 36.0f, 105, 17,
                 theme_.accent, gfx::Align::left, 4.0f);
        ui::text(list, fonts.display, "Library", kMargin, 172, 62, paint.page_text());
        const std::size_t count = view_.size();
        const std::string summary = std::to_string(count) + (count == 1 ? " title" : " titles") +
                                    "  \xc2\xb7  " + kSortNames[static_cast<int>(sort_)];
        ui::text(list, fonts.regular, summary.c_str(), gfx::kVirtualWidth - kMargin, 172, 24,
                 paint.page_text_muted(), gfx::Align::right);
    }

    // The active search, at the right of the tab row, with how to clear it.
    void draw_query(gfx::DrawList &list, const ui::Painter &paint) const
    {
        if (query_.empty())
            return;
        const ui::Fonts &fonts = context_.fonts;
        const float right = gfx::kVirtualWidth - kMargin;
        const float y = kTabsTop + 37.0f;
        float x = right;
        if (!searching_)
        {
            const float w = ui::button_width(ui::Button::circle, 30);
            ui::draw_button(list, fonts, ui::GlyphStyle::dark(), ui::Button::circle, right - w,
                            y - 9, 30);
            x -= w + 12.0f;
        }
        const std::string label = "\xe2\x80\x9c" + query_ + "\xe2\x80\x9d";
        ui::text(list, fonts.semibold, label.c_str(), x, y, 22, paint.page_text(),
                 gfx::Align::right);
    }

    void draw_art(ui::Canvas &canvas, const Rect &art, float radius, const ui::CardItem &item) const
    {
        if (item.texture != 0)
        {
            canvas.list.image(item.texture, art, item.uv, Color::rgb(0xffffff), radius);
        }
        else
        {
            canvas.list.gradient_rect(art, radius, item.top, item.bottom);
            const std::string letters = monogram(item.title);
            ui::text(canvas.list, canvas.fonts.display, letters.c_str(), art.cx(),
                     art.y + art.h * 0.5f + art.h * 0.13f, art.h * 0.36f,
                     Color::rgb(0xffffff, 0.9f), gfx::Align::center);
        }
        canvas.list.gradient_rect(art, radius, Color::rgb(0xffffff, 0.14f),
                                  Color::rgb(0xffffff, 0.0f));
        canvas.list.bordered_rect(art, radius, Color::rgb(0x000000, 0.0f), 1.0f,
                                  Color::rgb(0xffffff, 0.16f));
        if (favourite(titles()[static_cast<std::size_t>(item.tag)].id))
        {
            // A gold star on a dark disc in the corner the badge leaves free.
            const float r = std::max(14.0f, art.w * 0.075f);
            const float cx = art.x + r + 8.0f;
            const float cy = art.y + r + 8.0f;
            canvas.list.circle(cx, cy, r, Color::rgb(0x0b0e20, 0.72f));
            canvas.list.star(cx, cy, r * 0.72f, Color::rgb(0xffc94a));
        }
    }

    void draw_details(gfx::DrawList &list, ui::Painter &paint) const
    {
        const Title &title = titles()[static_cast<std::size_t>(view_[focus_index()])];
        std::string line = title.id;
        if (!title.version.empty())
            line += "   v" + title.version;
        line += "   " + std::string(shelf_name(library::shelf_of(title)));
        if (favourite(title.id))
            line += "   \xc2\xb7   Favourite";
        const float width = context_.fonts.regular.font->measure(line, 21.0f);
        glass_panel(list, paint, {kMargin - 24.0f, 976.0f, width + 48.0f, 68.0f}, 34.0f);
        ui::text(list, context_.fonts.regular, line.c_str(), kMargin, 1018, 21,
                 paint.page_text_muted());
    }

    void draw_empty(gfx::DrawList &list, const ui::Painter &paint) const
    {
        const ui::Fonts &fonts = context_.fonts;
        const float cx = gfx::kVirtualWidth * 0.5f;
        const char *title = "No titles yet";
        const char *first = "ORION shows what ShadowMountPlus can mount from /data/homebrew,";
        const char *second = "/mnt/ext0/homebrew and USB drives.";
        std::string searched;
        if (!titles().empty() && !query_.empty())
        {
            searched = "Nothing matches \xe2\x80\x9c" + query_ + "\xe2\x80\x9d";
            title = searched.c_str();
            first = "Try fewer letters, or press Circle to clear the search.";
            second = "";
        }
        else if (!titles().empty() && tabs_.active() == kFavourites)
        {
            title = "No favourites yet";
            first = "Press Square on any title to star it.";
            second = "Starred titles collect here and keep their star.";
        }
        else if (!titles().empty())
        {
            title = "Nothing on this shelf";
            first = "Press L1 or R1 to look at another one.";
            second = "";
        }
        ui::text(list, fonts.display, title, cx, 560, 46, paint.page_text(), gfx::Align::center);
        ui::text(list, fonts.regular, first, cx, 616, 24, paint.page_text_muted(),
                 gfx::Align::center);
        ui::text(list, fonts.regular, second, cx, 650, 24, paint.page_text_muted(),
                 gfx::Align::center);
    }

    void draw_hints(gfx::DrawList &list, ui::Painter &paint) const
    {
        const ui::Hint hints[] = {
            {ui::Button::l1, "Shelf", ui::Button::r1},
            {ui::Button::square, "Favourite"},
            {ui::Button::triangle, "Search"},
            {ui::Button::options, "Sort"},
        };
        const float right = gfx::kVirtualWidth - kMargin;
        const float width = ui::measure_hints(context_.fonts, hints, 4);
        glass_panel(list, paint, {right - width - 28.0f, 976.0f, width + 56.0f, 68.0f}, 34.0f);
        ui::draw_hints(list, context_.fonts, ui::GlyphStyle::dark(), hints, 4, right, true);
    }

    // A frosted panel with a glossy sheen and a polished rim: the glass ORION's
    // menus are made of.
    void glass_panel(gfx::DrawList &list, ui::Painter &paint, const Rect &r, float radius) const
    {
        list.shadow({r.x, r.y + 12.0f, r.w, r.h}, radius, 34.0f, Color::rgb(0x000000, 0.38f));
        paint.surface(r, radius, theme_.surface, theme_.light, 1.0f);
        sheen(list, r, radius);
    }

    // Light falling on glass from above: brightest at the top edge, gone by
    // the bottom, with a thin specular line just inside the rim.
    static void sheen(gfx::DrawList &list, const Rect &r, float radius, bool rim = true)
    {
        list.gradient_rect(r, radius, Color::rgb(0xffffff, 0.16f), Color::rgb(0xffffff, 0.0f));
        const float inset = std::min(radius, r.w * 0.25f) * 0.75f + 6.0f;
        list.rounded_rect({r.x + inset, r.y + 2.0f, r.w - 2.0f * inset, 2.0f}, 1.0f,
                          Color::rgb(0xffffff, 0.38f));
        if (rim)
            list.bordered_rect(r, radius, Color::rgb(0x000000, 0.0f), 1.0f,
                               Color::rgb(0xffffff, 0.2f));
    }

    // The tab bar draws its own frosted track; this adds the same gloss.
    void draw_tab_sheen(gfx::DrawList &list) const
    {
        const int count = static_cast<int>(tabs_.tabs().size());
        if (count == 0)
            return;
        const Rect first = tabs_.tab_rect(context_.fonts, 0);
        const Rect last = tabs_.tab_rect(context_.fonts, count - 1);
        const float inset = tabs_.style.track_inset;
        const Rect track{first.x - inset, first.y - inset, last.x + last.w - first.x + 2.0f * inset,
                         first.h + 2.0f * inset};
        sheen(list, track, track.h * 0.5f, false); // the bar has a rim of its own
    }

    // The search overlay: a veil, the query in a field, the keyboard under it.
    void draw_keyboard(ui::Canvas &canvas, ui::Painter &paint) const
    {
        gfx::DrawList &list = canvas.list;
        const float shown = tween::cubic_out(keyboard_shown_.value);
        // A light veil: the filtered covers above the panel are the point.
        list.rounded_rect({0, 0, gfx::kVirtualWidth, gfx::kVirtualHeight}, 0,
                          theme_.page.with_alpha(0.3f * shown));
        list.push_opacity(shown);
        list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, 40.0f * (1.0f - shown));
        // Frosted glass from the sky behind; it is opaque to the covers drawn
        // in the same layer, so they do not show through the keys.
        glass_panel(list, paint, kSearchPanel, theme_.radius_card);
        const Rect field = kSearchField;
        list.rounded_rect(field, field.h * 0.5f, Color::rgb(0x000000, 0.28f));
        list.bordered_rect(field, field.h * 0.5f, Color::rgb(0x000000, 0.0f), 1.0f,
                           Color::rgb(0xffffff, 0.18f));
        const ui::Fonts &fonts = context_.fonts;
        const bool empty = query_.empty();
        ui::text(list, fonts.regular, empty ? "Search titles or IDs" : query_.c_str(), field.x + 28,
                 field.y + 37, 25, empty ? paint.page_text_muted() : theme_.text);
        if (!empty || std::fmod(clock_, 1.0f) < 0.5f)
        {
            const float caret =
                empty ? field.x + 26 : field.x + 30 + fonts.regular.font->measure(query_, 25.0f);
            list.rounded_rect({caret, field.y + 13, 2.5f, 30}, 1.0f, theme_.accent);
        }
        keys_.draw(canvas);
        list.pop_transform();
        list.pop_opacity();
    }

    app::Context &context_;
    const ui::Theme &theme_;
    std::vector<std::string> favourites_;
    std::vector<int> view_; // indices into the library, in display order
    std::string query_;
    Sort sort_ = Sort::name;
    bool searching_ = false;
    ui::TabBar tabs_;
    ui::GridView grid_;
    ui::Keyboard keys_;
    tween::Spring keyboard_shown_;
    float age_ = 0.0f;
    float clock_ = 0.0f;
};

} // namespace

std::unique_ptr<app::Concept> make_library(app::Context &context)
{
    return std::make_unique<Library>(context);
}

} // namespace hui::concepts
