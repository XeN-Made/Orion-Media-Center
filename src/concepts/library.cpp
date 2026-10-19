// ps5-homebrew-ui - Design "Library": the titles installed on this console.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORION's home: every title ShadowMountPlus can mount (library/library.hpp),
// as a grid of covers on shelves: All, Games, Homebrew, Media, Favourites,
// then the player's own tabs. Cross launches a title; Options opens its menu
// (star it, move it to a tab, Settings); R3 opens Settings directly (orion/settings_panel.hpp),
// where the look and the library's behaviour are edited live. Everything the player chooses is
// written to <state_dir> the moment it changes: favourites.json, library.json (tabs, placements,
// last tab, sort) and theme.json (the look). Titles without artwork get a monogram on a colour
// derived from their ID, so the same title always looks the same.

#include "concepts/concepts.hpp"

#include "core/save_file.hpp"
#include "core/tween.hpp"
#include "library/favourites.hpp"
#include "orion/look.hpp"
#include "orion/prefs.hpp"
#include "orion/settings_panel.hpp"
#include "orion/theme_file.hpp"
#include "platform/ps5/system.hpp"
#include "ui/components/grid.hpp"
#include "ui/components/keyboard.hpp"
#include "ui/components/menu.hpp"
#include "ui/components/tabs.hpp"
#include "ui/glyphs.hpp"
#include "ui/motion.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
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
constexpr std::size_t kMaxName = 24;  // bytes, as orion::LibraryPrefs keeps them
// The launch helper shipped in the app folder, and the console's ELF loader.
constexpr const char *kLauncherPath = "/app0/assets/payloads/orion-launcher.elf";
constexpr int kLoaderPort = 9021;
// The search panel sits under the first row of covers, so the results of
// what is typed stay in view above it.
constexpr Rect kSearchPanel{544.0f, 590.0f, 832.0f, 378.0f};
constexpr Rect kSearchField{560.0f, 606.0f, 800.0f, 56.0f};
constexpr Rect kKeysRect{560.0f, 670.0f, 800.0f, 290.0f};

// The built-in shelves, by the key library.json remembers them with.
struct Shelf
{
    const char *key;
    const char *name;
};
constexpr Shelf kShelves[] = {
    {"all", "All"},     {"games", "Games"},           {"homebrew", "Homebrew"},
    {"media", "Media"}, {"favourites", "Favourites"},
};

enum class Sort : int
{
    name,
    name_reversed,
    recent,
    platform,
};
constexpr const char *kSortKeys[] = {"name", "name_reversed", "recent", "platform"};
constexpr const char *kSortNames[] = {"Name A–Z", "Name Z–A", "Recently added", "Platform"};

// What the on-screen keyboard is typing into.
enum class Typing : std::uint8_t
{
    none,
    search,
    new_tab,           // from Settings > Tabs > New tab
    new_tab_for_title, // from a title's menu: the title moves into it
    rename_tab,
    theme_name, // from Settings > Save as new theme
};

// Who the open menu is about.
enum class MenuFor : std::uint8_t
{
    title,
    tab,
};

// Tags of the menu items.
enum : int
{
    kItemFavourite = 1,
    kItemNewTab,
    kItemUnplace,
    kItemLaunch,
    kItemRename,
    kItemLeft,
    kItemRight,
    kItemDelete,
    kItemSettings,
    kItemMove = 100, // + the tab's index
};

constexpr const char *kTechniques[] = {
    "Reads the console's library the way ShadowMountPlus does: folders, images, icon cache",
    "Shelves from the metadata itself: DRM-free titles are homebrew, the Media area is media",
    "Custom tabs and placements, the last tab and the sort kept in library.json",
    "A look edited live in Settings and kept in theme.json, over any base theme",
    "Live search from the on-screen keyboard; the grid filters as you type",
    "Launches through a helper payload, since a title cannot start while ORION is in front",
    "Logs every launch step, ORION's and the helper's, to launch.txt",
};

constexpr app::TourStep kTour[] = {
    {0.5f, 0, Direction::right},
    {0.4f, action_bit(Action::jump_next)},
    {0.6f, action_bit(Action::jump_next), Direction::none, "games"},
    {0.6f, action_bit(Action::west), Direction::none, "homebrew"},
    {0.5f, action_bit(Action::jump_prev)},
    {0.3f, action_bit(Action::jump_prev)},
    {0.6f, action_bit(Action::menu), Direction::none, "favourite"},
    {0.6f, action_bit(Action::back), Direction::none, "title-menu"},
    // Cross launches: on a PC the card says it needs the console.
    {0.4f, action_bit(Action::confirm)},
    {0.7f, 0, Direction::none, "launch"},
    {0.4f, action_bit(Action::r3)},
    {0.9f, 0, Direction::down, "settings"},
    {0.5f, action_bit(Action::back)},
    {0.6f, action_bit(Action::north)},
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

// iOS-like clear glass for the search keyboard: keys of frosted white at low
// opacity with white lettering, the focused one lit almost solid white, and
// no tint from the page, so it floats over the sky instead of sitting on it.
ui::Theme clear_glass(const ui::Theme &base)
{
    ui::Theme t = base;
    t.surface = Color::rgb(0xffffff, 0.1f);
    t.surface_high = Color::rgb(0xffffff, 0.16f);
    t.secondary = Color::rgb(0xffffff, 0.12f);
    t.on_secondary = Color::rgb(0xffffff);
    t.text = Color::rgb(0xffffff);
    t.text_muted = Color::rgb(0xffffff, 0.72f);
    t.primary = Color::rgb(0xffffff, 0.92f);
    t.on_primary = Color::rgb(0x0a0c16);
    t.accent = Color::rgb(0xffffff);
    t.outline = Color::rgb(0xffffff, 0.16f);
    t.focus = Color::rgb(0xffffff);
    t.light = Color::rgb(0xffffff, 0.34f);
    t.shadow = Color::rgb(0x000000, 0.25f);
    return t;
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

Sort sort_of(const std::string &key)
{
    for (int i = 0; i < 4; ++i)
    {
        if (key == kSortKeys[i])
            return static_cast<Sort>(i);
    }
    return Sort::name;
}

// A colour brighter by a factor, for clouds drawn from the nebula's colours.
Color lifted(Color colour, float factor)
{
    return Color{std::min(1.0f, colour.r * factor), std::min(1.0f, colour.g * factor),
                 std::min(1.0f, colour.b * factor), 1.0f};
}

// A menu row with just a label (MenuItem holds strings, so it is filled field
// by field rather than brace-initialised in part).
ui::MenuItem menu_item(std::string label)
{
    ui::MenuItem item;
    item.label = std::move(label);
    return item;
}

class Library final : public app::Concept
{
  public:
    explicit Library(app::Context &context)
        : context_(context), prefs_(orion::load_prefs(context.library.state_dir)),
          favourites_(library::load_favourites(context.library.state_dir))
    {
        tabs_.style.kind = ui::TabKind::pill;
        tabs_.style.height = 52.0f;
        tabs_.style.text_size = 21.0f;
        tabs_.style.badge_size = 16.0f;
        tabs_.style.padding = 20.0f;
        tabs_.style.on_page = true;
        tabs_.style.focus_ring = false;
        tabs_.set_bounds({kMargin, kTabsTop, 1300.0f, 56.0f});

        grid_.style.columns = kColumns;
        grid_.style.gap_x = 28.0f;
        grid_.style.gap_y = 30.0f;
        // Every edge refuses, as the home screen's own grids do.
        grid_.style.wrap = ui::GridWrap::none;
        grid_.style.card.title_size = 22.0f;
        grid_.style.card.subtitle_size = 17.0f;
        grid_.style.card.badge_corner = ui::CardCorner::top_right;
        grid_.style.card.glow = true;
        grid_.art = [this](ui::Canvas &canvas, const Rect &art, float radius,
                           const ui::CardItem &item, float /*focus*/)
        { draw_art(canvas, art, radius, item); };
        grid_.set_bounds(
            {kMargin, kGridTop, gfx::kVirtualWidth - 2.0f * kMargin, kGridBottom - kGridTop});

        keys_.style.bindings = ui::KeyboardBindings::standard();
        keys_.style.bindings.done = Action::menu;
        keys_.set_bounds(kKeysRect);
        keys_.on_text = [this](std::string_view utf8) { type(utf8); };
        keys_.on_backspace = [this] { erase(); };

        menu_.set_bounds({kMargin, 120.0f, gfx::kVirtualWidth - 2.0f * kMargin, 900.0f});
        menu_.style.backing = 0.86f; // readable over bright covers
        menu_.style.scrim = 0.25f;

        // Every theme ORION can wear: the app's files, then the player's.
        const std::string &state = context_.library.state_dir;
        themes_.load(context_.library.themes_dir, state.empty() ? "" : state + "/themes");
        if (!state.empty() && !themes_.problems().empty())
        {
            std::string report;
            for (const std::string &problem : themes_.problems())
                report += problem + "\n";
            save::write_atomic(state + "/themes.txt", report);
        }
        look_ = orion::load_look(state, themes_);
        settings_.set_themes(themes_);
        restyle();
        // ORION reopens where it was left, when asked to.
        std::string start = prefs_.reopen_last_tab ? prefs_.last_tab : std::string("all");
        rebuild_tabs(start);
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
            Color::rgb(0x8fe3ff), // ORION's accent
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
        if (reduced != reduced_)
        {
            reduced_ = reduced;
            restyle();
        }

        // One layer has the input: the keyboard, then a menu, then Settings,
        // then the library itself.
        if (typing_ != Typing::none)
            update_typing(input, feedback);
        else if (menu_.is_open())
            update_menu(input, feedback);
        else if (settings_.is_open())
            update_settings(input, feedback);
        else
            update_browse(input, feedback);

        tabs_.update(dt);
        grid_.update(dt);
        keys_.update(dt);
        menu_.update(dt);
        settings_.update(dt);
        if (launch_exit_ > 0.0f)
        {
            launch_exit_ -= dt;
            if (launch_exit_ <= 0.0f)
                context_.exit_requested = true;
        }
        launch_shown_ = std::max(0.0f, launch_shown_ - dt);
        launch_pending_ = std::max(0.0f, launch_pending_ - dt);
        launch_card_.target = launch_shown_ > 0.0f ? 1.0f : 0.0f;
        launch_card_.update(dt, reduced ? 60.0f : 14.0f);
        keyboard_shown_.target = typing_ != Typing::none ? 1.0f : 0.0f;
        keyboard_shown_.update(dt, reduced ? 60.0f : 16.0f);
    }

    void draw(app::Frame &frame) const override
    {
        const bool still = context_.settings.reduced_motion || !look_.motion;
        const float drift = still ? 0.0f : clock_;
        frame.backdrop = theme_.backdrop;
        frame.backdrop.time = drift;
        if (frame.backdrop.mode == gfx::BackdropMode::stars)
        {
            // The star layers slide at different fractions of the camera: a
            // slow pan is enough to make the sky feel deep.
            frame.backdrop.params[0] = drift * 6.0f;
            frame.backdrop.params[1] = std::sin(drift * 0.05f) * 40.0f;
        }
        if (theme_.style == ui::SurfaceStyle::glass)
        {
            draw_clouds(frame.scene, drift);
            frame.glass = true;
        }

        gfx::DrawList &list = frame.overlay;
        const std::uint32_t glass = frame.glass ? frame.glass_texture : 0;
        ui::Canvas canvas{list, context_.fonts, glass, clock_};
        ui::Painter paint(list, context_.fonts, theme_, glass);
        const float intro = tween::cubic_out(age_ / 0.35f);
        // The covers go in the scene, under the glass capture: every frosted
        // panel drawn over them (the keyboard, the plates) shows them blurred,
        // which is what makes the glass read as glass.
        if (grid_.count() > 0)
        {
            ui::Canvas covers{frame.scene, context_.fonts, 0, clock_};
            frame.scene.push_opacity(intro);
            grid_.draw(covers);
            frame.scene.pop_opacity();
        }
        list.push_opacity(intro);

        draw_header(list, paint);
        tabs_.draw(canvas);
        draw_tab_sheen(list);
        draw_query(list, paint);
        if (grid_.count() > 0)
            draw_details(list, paint);
        else
            draw_empty(list, paint);
        if (keyboard_shown_.value > 0.01f)
            draw_keyboard(canvas, paint);
        else
            draw_hints(list, paint);
        draw_launch_card(list, paint);
        settings_.draw(canvas);
        menu_.draw(canvas); // last: it belongs to whatever it was opened over
        list.pop_opacity();
    }

    std::span<const app::TourStep> tour() const override
    {
        return kTour;
    }

  private:
    // ---- styling ----

    // The look's theme on every component; called when Settings changes it.
    void restyle()
    {
        theme_ = orion::themed(look_, themes_);
        tabs_.style.theme = grid_.style.theme = menu_.style.theme = theme_;
        keys_.style.theme = clear_glass(theme_);
        grid_.style.card.radius = look_.corners;
        tabs_.style.reduced_motion = grid_.style.reduced_motion = keys_.style.reduced_motion =
            menu_.style.reduced_motion = reduced_;
        settings_.set_theme(theme_, reduced_);
    }

    void save_prefs() const
    {
        orion::save_prefs(context_.library.state_dir, prefs_);
    }

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
            if (tabs_.step(next ? 1 : -1, input, feedback) == ui::Event::changed)
            {
                prefs_.last_tab = active_key();
                save_prefs();
                rebuild();
            }
            return;
        }
        if (input.is_pressed(Action::north))
        {
            start_typing(Typing::search, query_);
            feedback.play(audio::Cue::modal_open);
            return;
        }
        if (input.is_pressed(Action::r3) || (input.is_pressed(Action::menu) && grid_.count() == 0))
        {
            settings_.open(look_, prefs_, feedback);
            return;
        }
        if (input.is_pressed(Action::back) && !query_.empty())
        {
            query_.clear();
            feedback.play(audio::Cue::erase);
            rebuild();
            return;
        }
        if (grid_.count() == 0)
            return;
        if (input.is_pressed(Action::west))
        {
            toggle_favourite(focused().id, feedback);
            return;
        }
        if (input.is_pressed(Action::confirm))
        {
            launch(focused().id, feedback);
            return;
        }
        if (input.is_pressed(Action::menu))
        {
            open_title_menu(feedback);
            return;
        }
        grid_.handle(input, feedback);
    }

    void update_settings(const InputFrame &input, app::Feedback &feedback)
    {
        switch (settings_.handle(input, feedback, &look_, &prefs_))
        {
        case orion::SettingsPanel::Change::look:
            restyle();
            orion::save_look(context_.library.state_dir, look_);
            break;
        case orion::SettingsPanel::Change::prefs:
            save_prefs();
            rebuild();
            break;
        case orion::SettingsPanel::Change::new_tab:
            start_typing(Typing::new_tab, "");
            feedback.play(audio::Cue::modal_open);
            break;
        case orion::SettingsPanel::Change::save_theme:
            start_typing(Typing::theme_name, "");
            feedback.play(audio::Cue::modal_open);
            break;
        case orion::SettingsPanel::Change::edit_tab:
            open_tab_menu(settings_.tab_id(), feedback);
            break;
        case orion::SettingsPanel::Change::none:
            break;
        }
    }

    // ---- the keyboard ----

    void start_typing(Typing typing, std::string text)
    {
        typing_ = typing;
        if (typing == Typing::search)
            query_ = std::move(text);
        else
            name_ = std::move(text);
        keys_.style.max_length = static_cast<int>(typing == Typing::search ? kMaxQuery : kMaxName);
        keys_.set_length(static_cast<int>(buffer().size()));
    }

    std::string &buffer()
    {
        return typing_ == Typing::search ? query_ : name_;
    }

    void type(std::string_view utf8)
    {
        const std::size_t limit = typing_ == Typing::search ? kMaxQuery : kMaxName;
        if (buffer().size() + utf8.size() > limit)
            return;
        buffer() += utf8;
        if (typing_ == Typing::search)
            rebuild();
    }

    void erase()
    {
        // Drop one whole UTF-8 character: continuation bytes, then its lead.
        std::string &text = buffer();
        while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xc0) == 0x80)
            text.pop_back();
        if (!text.empty())
            text.pop_back();
        if (typing_ == Typing::search)
            rebuild();
    }

    void update_typing(const InputFrame &input, app::Feedback &feedback)
    {
        keys_.set_length(static_cast<int>(buffer().size()));
        const ui::Event event = keys_.handle(input, feedback);
        if (event != ui::Event::activated && event != ui::Event::cancelled)
            return;
        const Typing typing = typing_;
        typing_ = Typing::none;
        feedback.play(audio::Cue::modal_close);
        // Back keeps a search but abandons a name.
        if (event == ui::Event::cancelled || typing == Typing::search)
            return;

        if (typing == Typing::theme_name)
        {
            save_theme(feedback);
            return;
        }
        if (typing == Typing::rename_tab)
        {
            prefs_.rename_tab(menu_tab_, name_);
        }
        else
        {
            const std::string id = prefs_.add_tab(name_).id;
            if (typing == Typing::new_tab_for_title)
                prefs_.place(menu_title_, id);
        }
        save_prefs();
        rebuild_tabs(active_key());
        rebuild();
        if (settings_.is_open())
            settings_.refresh(look_, prefs_);
        feedback.play(audio::Cue::saved);
    }

    // ---- menus ----

    void open_title_menu(app::Feedback &feedback)
    {
        const Title &title = focused();
        menu_for_ = MenuFor::title;
        menu_title_ = title.id;
        const orion::CustomTab *placed = prefs_.placement(title.id);

        std::vector<ui::MenuItem> items;
        ui::MenuItem launch = menu_item("Launch");
        launch.tag = kItemLaunch;
        launch.disabled = launch_pending_ > 0.0f;
        items.push_back(launch);
        ui::MenuItem gap;
        gap.separator = true;
        items.push_back(gap);
        ui::MenuItem favourite =
            menu_item(favourite_of(title.id) ? "Remove from Favourites" : "Add to Favourites");
        favourite.shortcut = "Square";
        favourite.tag = kItemFavourite;
        items.push_back(favourite);
        ui::MenuItem line;
        line.separator = true;
        items.push_back(line);
        for (std::size_t i = 0; i < prefs_.tabs.size(); ++i)
        {
            const bool here = placed != nullptr && placed->id == prefs_.tabs[i].id;
            ui::MenuItem move = menu_item("Move to " + prefs_.tabs[i].name);
            move.tag = kItemMove + static_cast<int>(i);
            move.disabled = here;
            move.shortcut = here ? "Here" : "";
            items.push_back(move);
        }
        ui::MenuItem fresh = menu_item("Move to a new tab…");
        fresh.tag = kItemNewTab;
        items.push_back(fresh);
        if (placed != nullptr)
        {
            ui::MenuItem unplace = menu_item("Remove from " + placed->name);
            unplace.tag = kItemUnplace;
            unplace.danger = true;
            items.push_back(unplace);
        }
        ui::MenuItem rule;
        rule.separator = true;
        items.push_back(rule);
        ui::MenuItem settings = menu_item("Settings");
        settings.shortcut = "R3";
        settings.tag = kItemSettings;
        items.push_back(settings);
        menu_.set_items(std::move(items));
        menu_.open(grid_.cell_rect(grid_.focus()), feedback);
    }

    void open_tab_menu(const std::string &tab_id, app::Feedback &feedback)
    {
        menu_for_ = MenuFor::tab;
        menu_tab_ = tab_id;
        std::size_t index = 0;
        while (index < prefs_.tabs.size() && prefs_.tabs[index].id != tab_id)
            ++index;
        ui::MenuItem rename = menu_item("Rename");
        rename.tag = kItemRename;
        ui::MenuItem left = menu_item("Move left");
        left.tag = kItemLeft;
        left.disabled = index == 0;
        ui::MenuItem right = menu_item("Move right");
        right.tag = kItemRight;
        right.disabled = index + 1 >= prefs_.tabs.size();
        ui::MenuItem line;
        line.separator = true;
        ui::MenuItem remove = menu_item("Delete tab");
        remove.tag = kItemDelete;
        remove.danger = true;
        menu_.set_items({rename, left, right, line, remove});
        menu_.open(settings_.focused_row(), feedback);
    }

    void update_menu(const InputFrame &input, app::Feedback &feedback)
    {
        if (menu_.handle(input, feedback) != ui::Event::activated)
            return;
        const int tag = menu_.items()[static_cast<std::size_t>(menu_.focus())].tag;
        if (menu_.is_open())
            menu_.close(feedback);

        if (menu_for_ == MenuFor::title)
        {
            if (tag == kItemLaunch)
            {
                launch(menu_title_, feedback);
                return;
            }
            if (tag == kItemSettings)
            {
                settings_.open(look_, prefs_, feedback);
                return;
            }
            if (tag == kItemFavourite)
            {
                toggle_favourite(menu_title_, feedback);
                return;
            }
            if (tag == kItemNewTab)
            {
                start_typing(Typing::new_tab_for_title, "");
                feedback.play(audio::Cue::modal_open);
                return;
            }
            if (tag == kItemUnplace)
                prefs_.unplace(menu_title_);
            else if (tag >= kItemMove && tag - kItemMove < static_cast<int>(prefs_.tabs.size()))
                prefs_.place(menu_title_,
                             prefs_.tabs[static_cast<std::size_t>(tag - kItemMove)].id);
            save_prefs();
            rebuild_tabs(active_key());
            rebuild();
            feedback.play(audio::Cue::place);
            return;
        }

        // A tab, from Settings.
        if (tag == kItemRename)
        {
            const orion::CustomTab *tab = prefs_.tab(menu_tab_);
            start_typing(Typing::rename_tab, tab != nullptr ? tab->name : "");
            feedback.play(audio::Cue::modal_open);
            return;
        }
        if (tag == kItemLeft || tag == kItemRight)
            prefs_.move_tab(menu_tab_, tag == kItemLeft ? -1 : 1);
        else if (tag == kItemDelete)
            prefs_.remove_tab(menu_tab_);
        save_prefs();
        rebuild_tabs(active_key());
        rebuild();
        settings_.refresh(look_, prefs_);
    }

    // A title cannot start while ORION is in front (the system refuses with
    // 0x8094000f), so the launch is handed over: the title goes into
    // <state_dir>/launch-request.txt, the launch helper (payload/launcher)
    // goes to the console's ELF loader, and ORION closes itself a moment
    // later. The helper waits for that and starts the title. Every step is
    // appended to <state_dir>/launch.txt, by ORION and by the helper.
    void launch(const std::string &id, app::Feedback &feedback)
    {
        const Title *title = nullptr;
        for (const Title &t : titles())
        {
            if (t.id == id)
                title = &t;
        }
        if (title == nullptr || launch_pending_ > 0.0f)
            return;
        const std::string &dir = context_.library.state_dir;
        launch_ok_ = false;
        std::string elf;
        std::string why;
        if (!sys::on_console())
            why = "not on a console";
        else if (dir.empty() ||
                 !save::write_atomic(dir + "/launch-request.txt", title->id + "\n").empty())
            why = "could not write the launch request";
        else if (!save::read_file(kLauncherPath, &elf, 4u << 20) || elf.empty())
            why = std::string("the launch helper is missing (") + kLauncherPath + ")";
        else
        {
            const int error = sys::send_payload(elf, kLoaderPort);
            if (error == 0)
                launch_ok_ = true;
            else if (error == sys::kLaunchUnsupported)
                why = "not on a console";
            else
                why = "the ELF loader did not answer (errno " + std::to_string(error) + ")";
        }

        if (launch_ok_)
        {
            log_launch(*title, "sent the helper (" + std::to_string(elf.size()) +
                                   " bytes) to 127.0.0.1:" + std::to_string(kLoaderPort) +
                                   "; closing ORION");
            launch_message_ = "Starting " + title->name + "\u2026";
            // Long enough to read the card, then ORION gets out of the way.
            launch_exit_ = 0.9f;
            launch_pending_ = 30.0f;
        }
        else
        {
            log_launch(*title, "not launched: " + why);
            std::remove((dir + "/launch-request.txt").c_str());
            launch_message_ = why == "not on a console"
                                  ? std::string("Launching works on the console only")
                                  : "Could not start " + title->name + ": " + why;
        }
        launch_shown_ = 6.0f;
        feedback.play(launch_ok_ ? audio::Cue::launch : audio::Cue::error);
    }

    void log_launch(const Title &title, const std::string &what) const
    {
        const std::string &dir = context_.library.state_dir;
        if (dir.empty() || !save::ensure_directory(dir))
            return;
        std::string text;
        save::read_file(dir + "/launch.txt", &text, 64u << 10);
        char stamp[32];
        std::snprintf(stamp, sizeof(stamp), "%lld", static_cast<long long>(std::time(nullptr)));
        text +=
            std::string(stamp) + "  orion   " + title.id + "  " + title.name + "  " + what + "\n";
        // The newest lines are the ones that matter; keep the file small.
        if (text.size() > 32u << 10)
            text.erase(0, text.find('\n', text.size() - (24u << 10)) + 1);
        save::write_atomic(dir + "/launch.txt", text);
    }

    // The look as it is now becomes a theme file of the player's, which the
    // look then wears, so further tweaks start from it.
    void save_theme(app::Feedback &feedback)
    {
        orion::ThemeFile file = orion::theme_of(look_, themes_);
        const std::string name = name_.empty() ? std::string("My theme") : name_;
        std::string id = orion::theme_id_from_name(name);
        if (id.empty())
            id = "my-theme";
        // Never take a built-in theme's id; number the player's own instead.
        const std::string stem = id;
        for (int n = 2; themes_.find(id) != nullptr && !themes_.find(id)->user; ++n)
            id = stem + "-" + std::to_string(n);
        file.id = id;
        file.name = name;
        file.author = "";
        file.description =
            "Made in ORION from " + std::string(file.family.empty() ? "a theme" : file.family);
        std::string error;
        if (themes_.save_user_theme(std::move(file), &error))
        {
            look_.base = id;
            orion::save_look(context_.library.state_dir, look_);
            restyle();
            launch_ok_ = true;
            launch_message_ = "Saved the theme \u201c" + name + "\u201d";
            feedback.play(audio::Cue::saved);
        }
        else
        {
            launch_ok_ = false;
            launch_message_ = "Could not save the theme: " + error;
            feedback.play(audio::Cue::error);
        }
        launch_shown_ = 4.0f;
        if (settings_.is_open())
            settings_.refresh(look_, prefs_);
    }

    void toggle_favourite(const std::string &id, app::Feedback &feedback)
    {
        const auto found = std::find(favourites_.begin(), favourites_.end(), id);
        if (found == favourites_.end())
        {
            favourites_.push_back(id);
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

    const Title &focused() const
    {
        return titles()[static_cast<std::size_t>(view_[focus_index()])];
    }

    bool favourite_of(const std::string &id) const
    {
        return std::find(favourites_.begin(), favourites_.end(), id) != favourites_.end();
    }

    std::string active_key() const
    {
        const int active = tabs_.active();
        return active >= 0 && active < static_cast<int>(tab_keys_.size())
                   ? tab_keys_[static_cast<std::size_t>(active)]
                   : std::string("all");
    }

    bool on_tab(const Title &title, const std::string &key) const
    {
        if (key == "all")
            return true;
        if (key == "favourites")
            return favourite_of(title.id);
        const orion::CustomTab *placed = prefs_.placement(title.id);
        if (key.rfind("tab:", 0) == 0)
            return placed != nullptr && key.compare(4, std::string::npos, placed->id) == 0;
        // A built-in shelf; a moved title may have left it.
        if (placed != nullptr && prefs_.moved_leave_shelf)
            return false;
        const library::Shelf shelf = library::shelf_of(title);
        return (key == "games" && shelf == library::Shelf::games) ||
               (key == "homebrew" && shelf == library::Shelf::homebrew) ||
               (key == "media" && shelf == library::Shelf::media);
    }

    int focus_index() const
    {
        return std::clamp(grid_.focus(), 0, std::max(0, static_cast<int>(view_.size()) - 1));
    }

    // The tab row: the built-in shelves, then the player's tabs. The active
    // tab is chosen by key, so it survives tabs being added, moved or removed.
    void rebuild_tabs(const std::string &active)
    {
        tab_keys_.clear();
        std::vector<ui::TabItem> tabs;
        const auto add = [&](std::string key, std::string name)
        {
            ui::TabItem item;
            item.label = std::move(name);
            item.badge = static_cast<int>(std::count_if(
                titles().begin(), titles().end(), [&](const Title &t) { return on_tab(t, key); }));
            tabs.push_back(std::move(item));
            tab_keys_.push_back(std::move(key));
        };
        for (const Shelf &shelf : kShelves)
            add(shelf.key, shelf.name);
        for (const orion::CustomTab &tab : prefs_.tabs)
            add("tab:" + tab.id, tab.name);
        tabs_.set_tabs(std::move(tabs));
        int index = 0;
        for (std::size_t i = 0; i < tab_keys_.size(); ++i)
        {
            if (tab_keys_[i] == active)
                index = static_cast<int>(i);
        }
        tabs_.set_active(index, true);
    }

    // Recomputes what the grid shows from the tab, the query and the sort,
    // keeping the focus on the same title when it is still there.
    void rebuild()
    {
        std::string keep;
        if (!view_.empty())
            keep = focused().id;
        // The counts follow favourites and placements.
        for (std::size_t i = 0; i < tab_keys_.size(); ++i)
            tabs_.tab(static_cast<int>(i)).badge =
                static_cast<int>(std::count_if(titles().begin(), titles().end(), [&](const Title &t)
                                               { return on_tab(t, tab_keys_[i]); }));

        const std::string key = active_key();
        const std::string needle = folded(query_);
        view_.clear();
        for (std::size_t i = 0; i < titles().size(); ++i)
        {
            const Title &title = titles()[i];
            if (!on_tab(title, key))
                continue;
            if (!needle.empty() && folded(title.name).find(needle) == std::string::npos &&
                folded(title.id).find(needle) == std::string::npos)
                continue;
            view_.push_back(static_cast<int>(i));
        }
        // The scan already sorted by name; every other order is a stable sort
        // on top of it, so ties stay alphabetical.
        switch (sort_of(prefs_.sort))
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
            if (title.id == keep)
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

    // Nebula clouds: large, faint and slow. They are what the frosted glass
    // picks its colour up from. With ORION's own nebula they are the hues it
    // was designed with; any other nebula lends its colours to them.
    void draw_clouds(gfx::DrawList &scene, float drift) const
    {
        struct Cloud
        {
            float x, y, size, speed;
            std::uint32_t designed;
            bool far;
            float alpha;
        };
        constexpr Cloud kClouds[] = {
            {300.0f, 300.0f, 900.0f, 0.05f, 0x2448c8, false, 0.16f},
            {1520.0f, 240.0f, 820.0f, 0.04f, 0x1a9ad0, true, 0.15f},
            {1100.0f, 840.0f, 960.0f, 0.03f, 0x1f5f9a, true, 0.1f},
            {380.0f, 920.0f, 700.0f, 0.06f, 0x1c3a9c, false, 0.12f},
        };
        const orion::Look designed = orion::default_look(themes_);
        const bool own = orion::same_rgb(look_.nebula, designed.nebula) &&
                         orion::same_rgb(look_.nebula_far, designed.nebula_far);
        for (int i = 0; i < 4; ++i)
        {
            const Cloud &c = kClouds[i];
            const Color hue = own ? Color::rgb(c.designed)
                                  : lifted(c.far ? look_.nebula_far : look_.nebula, 2.0f);
            const float alpha = c.alpha * look_.clouds;
            if (alpha <= 0.001f)
                continue;
            const float phase = drift * c.speed + static_cast<float>(i) * 1.9f;
            const float cx = c.x + std::sin(phase) * 120.0f;
            const float cy = c.y + std::cos(phase * 1.2f) * 80.0f;
            scene.shadow({cx - c.size * 0.5f, cy - c.size * 0.5f, c.size, c.size}, c.size * 0.5f,
                         c.size * 0.55f, hue.with_alpha(std::min(1.0f, alpha)));
        }
    }

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
                                    "  \xc2\xb7  " +
                                    kSortNames[static_cast<int>(sort_of(prefs_.sort))];
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
        if (typing_ != Typing::search)
        {
            const float w = ui::button_width(ui::Button::circle, 30);
            ui::draw_button(list, fonts, glyphs(), ui::Button::circle, right - w, y - 9, 30);
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
        if (favourite_of(titles()[static_cast<std::size_t>(item.tag)].id))
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
        const Title &title = focused();
        std::string line = title.id;
        if (!title.version.empty())
            line += "   v" + title.version;
        line += "   " + std::string(shelf_name(library::shelf_of(title)));
        if (const orion::CustomTab *placed = prefs_.placement(title.id))
            line += "   \xc2\xb7   " + placed->name;
        if (favourite_of(title.id))
            line += "   \xc2\xb7   Favourite";
        const float width = context_.fonts.regular.font->measure(line, 21.0f);
        glass_panel(list, paint, {kMargin - 24.0f, 972.0f, width + 48.0f, 76.0f}, 38.0f);
        ui::text(list, context_.fonts.regular, line.c_str(), kMargin, 1018, 21,
                 paint.page_text_muted());
    }

    void draw_empty(gfx::DrawList &list, const ui::Painter &paint) const
    {
        const ui::Fonts &fonts = context_.fonts;
        const float cx = gfx::kVirtualWidth * 0.5f;
        const std::string key = active_key();
        std::string title = "No titles yet";
        std::string first = "ORION shows what ShadowMountPlus can mount from /data/homebrew,";
        std::string second = "/mnt/ext0/homebrew and USB drives.";
        if (!titles().empty() && !query_.empty())
        {
            title = "Nothing matches \xe2\x80\x9c" + query_ + "\xe2\x80\x9d";
            first = "Try fewer letters, or press Circle to clear the search.";
            second.clear();
        }
        else if (!titles().empty() && key == "favourites")
        {
            title = "No favourites yet";
            first = "Press Square on any title to star it.";
            second = "Starred titles collect here and keep their star.";
        }
        else if (!titles().empty() && key.rfind("tab:", 0) == 0)
        {
            title = "Nothing here yet";
            first = "Press Cross on any title and choose where to move it.";
            second = "Titles you move here stay here.";
        }
        else if (!titles().empty())
        {
            title = "Nothing on this shelf";
            first = "Press L1 or R1 to look at another one.";
            second.clear();
        }
        ui::text(list, fonts.display, title.c_str(), cx, 560, 46, paint.page_text(),
                 gfx::Align::center);
        ui::text(list, fonts.regular, first.c_str(), cx, 616, 24, paint.page_text_muted(),
                 gfx::Align::center);
        ui::text(list, fonts.regular, second.c_str(), cx, 650, 24, paint.page_text_muted(),
                 gfx::Align::center);
    }

    void draw_hints(gfx::DrawList &list, ui::Painter &paint) const
    {
        const ui::Hint hints[] = {
            {ui::Button::l1, "Shelf", ui::Button::r1},
            {ui::Button::cross, "Launch"},
            {ui::Button::square, "Favourite"},
            {ui::Button::triangle, "Search"},
            {ui::Button::options, "More"},
        };
        const float right = gfx::kVirtualWidth - kMargin;
        ui::HintLayout layout;
        layout.size = 46.0f;
        layout.text_size = 26.0f;
        layout.cy = 1010.0f;
        const float width = ui::measure_hints(context_.fonts, hints, 5, layout);
        glass_panel(list, paint, {right - width - 30.0f, 972.0f, width + 60.0f, 76.0f}, 38.0f);
        ui::draw_hints(list, context_.fonts, glyphs(), hints, 5, right, true, layout);
    }

    // A frosted panel with a glossy sheen and a polished rim: the glass ORION's
    // menus are made of.
    void glass_panel(gfx::DrawList &list, ui::Painter &paint, const Rect &r, float radius) const
    {
        glass_panel(list, paint, r, radius, theme_.surface, 0.38f);
    }
    void glass_panel(gfx::DrawList &list, ui::Painter &paint, const Rect &r, float radius,
                     Color tint, float shadow) const
    {
        list.shadow({r.x, r.y + 12.0f, r.w, r.h}, radius, 34.0f, Color::rgb(0x000000, shadow));
        paint.surface(r, radius, tint, theme_.light, 1.0f);
        sheen(list, r, radius);
    }

    // Light falling on glass from above: brightest at the top edge and gone
    // by the bottom, inside a faint rim.
    static void sheen(gfx::DrawList &list, const Rect &r, float radius, bool rim = true)
    {
        list.gradient_rect(r, radius, Color::rgb(0xffffff, 0.14f), Color::rgb(0xffffff, 0.0f));
        if (rim)
            list.bordered_rect(r, radius, Color::rgb(0x000000, 0.0f), 1.0f,
                               Color::rgb(0xffffff, 0.14f));
    }

    // Controller glyphs as frosted caps with white lettering: no grey outline
    // to fight the glass they sit on.
    static ui::GlyphStyle glyphs()
    {
        ui::GlyphStyle style = ui::GlyphStyle::dark();
        style.body = Color::rgb(0xffffff, 0.14f);
        style.edge = Color::rgb(0xffffff, 0.0f);
        style.ink = Color::rgb(0xffffff);
        style.label = Color::rgb(0xffffff, 0.92f);
        return style;
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

    // What the system said to a launch: a glass card rising above the hints.
    void draw_launch_card(gfx::DrawList &list, ui::Painter &paint) const
    {
        const float shown = tween::cubic_out(launch_card_.value);
        if (shown <= 0.01f)
            return;
        const ui::Fonts &fonts = context_.fonts;
        // Long messages (an errno, a path) are shortened to fit the screen.
        const std::string message = fonts.semibold.font->fit(launch_message_, 24.0f, 1400.0f);
        const float width = fonts.semibold.font->measure(message, 24.0f) + 96.0f;
        const Rect card{gfx::kVirtualWidth * 0.5f - width * 0.5f, 880.0f + 24.0f * (1.0f - shown),
                        width, 68.0f};
        list.push_opacity(shown);
        glass_panel(list, paint, card, 34.0f);
        const Color dot = launch_ok_ ? theme_.success : theme_.warning;
        list.glow({card.x + 28.0f, card.y + 27.0f, 14.0f, 14.0f}, 7.0f, 10.0f,
                  dot.with_alpha(0.6f));
        list.circle(card.x + 35.0f, card.y + 34.0f, 7.0f, dot);
        ui::text(list, fonts.semibold, message.c_str(), card.x + 60.0f, card.y + 43.0f, 24,
                 paint.page_text());
        list.pop_opacity();
    }

    // The keyboard overlay: a veil, what is being typed in a field, the keys.
    void draw_keyboard(ui::Canvas &canvas, ui::Painter &paint) const
    {
        gfx::DrawList &list = canvas.list;
        const float shown = tween::cubic_out(keyboard_shown_.value);
        // A light veil: the filtered covers above the panel are the point.
        list.rounded_rect({0, 0, gfx::kVirtualWidth, gfx::kVirtualHeight}, 0,
                          Color::rgb(0x000000, 0.22f * shown));
        list.push_opacity(shown);
        list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, 40.0f * (1.0f - shown));
        // Frosted glass from the sky and covers behind; it is opaque to the
        // overlay, so nothing drawn above the glass capture shows through.
        glass_panel(list, paint, kSearchPanel, theme_.radius_card, Color::rgb(0xffffff, 0.05f),
                    0.25f);
        const Rect field = kSearchField;
        list.rounded_rect(field, field.h * 0.5f, Color::rgb(0xffffff, 0.08f));
        list.bordered_rect(field, field.h * 0.5f, Color::rgb(0x000000, 0.0f), 1.0f,
                           Color::rgb(0xffffff, 0.18f));
        const ui::Fonts &fonts = context_.fonts;
        const bool search = typing_ == Typing::search || typing_ == Typing::none;
        const std::string &text = search ? query_ : name_;
        const char *placeholder = search                          ? "Search titles or IDs"
                                  : typing_ == Typing::rename_tab ? "Rename the tab"
                                  : typing_ == Typing::theme_name ? "Name the new theme"
                                                                  : "Name the new tab";
        const bool empty = text.empty();
        ui::text(list, fonts.regular, empty ? placeholder : text.c_str(), field.x + 28,
                 field.y + 37, 25, empty ? Color::rgb(0xffffff, 0.66f) : Color::rgb(0xffffff));
        if (!empty || std::fmod(clock_, 1.0f) < 0.5f)
        {
            const float caret =
                empty ? field.x + 26 : field.x + 30 + fonts.regular.font->measure(text, 25.0f);
            list.rounded_rect({caret, field.y + 13, 2.5f, 30}, 1.0f, theme_.accent);
        }
        keys_.draw(canvas);
        list.pop_transform();
        list.pop_opacity();
    }

    app::Context &context_;
    orion::ThemeLibrary themes_; // before the look, which is read through it
    orion::Look look_;
    orion::LibraryPrefs prefs_;
    ui::Theme theme_;
    bool reduced_ = false;
    std::vector<std::string> favourites_;
    std::vector<std::string> tab_keys_; // the key of each tab, in tab order
    std::vector<int> view_;             // indices into the library, in display order
    std::string query_;
    std::string name_; // a tab name being typed
    Typing typing_ = Typing::none;
    MenuFor menu_for_ = MenuFor::title;
    std::string menu_title_; // the title a menu is about
    std::string menu_tab_;   // the tab a menu (or a rename) is about
    ui::TabBar tabs_;
    ui::GridView grid_;
    ui::Keyboard keys_;
    ui::Menu menu_;
    orion::SettingsPanel settings_;
    tween::Spring keyboard_shown_;
    std::string launch_message_;
    bool launch_ok_ = false;
    float launch_shown_ = 0.0f;   // seconds the card stays
    float launch_pending_ = 0.0f; // seconds before another launch is taken
    float launch_exit_ = 0.0f;    // seconds until ORION closes for a launch
    tween::Spring launch_card_;
    float age_ = 0.0f;
    float clock_ = 0.0f;
};

} // namespace

std::unique_ptr<app::Concept> make_library(app::Context &context)
{
    return std::make_unique<Library>(context);
}

} // namespace hui::concepts
