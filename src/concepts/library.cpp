// ORION Media Center - Design "Library": the titles installed on this console.
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
#include "library/emulators.hpp"
#include "library/favourites.hpp"
#include "library/json.hpp"
#include "orion/art_fetcher.hpp"
#include "core/i18n.hpp"
#include "orion/backdrop_copies.hpp"
#include "orion/languages.hpp"
#include "orion/registered_titles.hpp"
#include "orion/look.hpp"
#include "orion/playtime.hpp"
#include "orion/prefs.hpp"
#include "orion/settings_panel.hpp"
#include "orion/setup_panel.hpp"
#include "orion/storage.hpp"
#include "orion/theme_file.hpp"
#include "orion/updates.hpp"
#include "platform/ps5/system.hpp"
#include "ui/components/carousel.hpp"
#include "ui/components/grid.hpp"
#include "ui/components/keyboard.hpp"
#include "ui/components/dialog.hpp"
#include "ui/components/menu.hpp"
#include "ui/components/select.hpp"
#include "ui/components/tabs.hpp"
#include "ui/glyphs.hpp"
#include "ui/motion.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <sys/stat.h>

namespace hui::concepts
{

using i18n::tr;
using i18n::trf;

namespace
{

using gfx::Color;
using gfx::Rect;
using library::Title;

constexpr float kMargin = 96.0f;
constexpr float kTabsTop = 204.0f;
constexpr float kTabEdge = 76.0f;     // room for L1 (and R1 after the tabs)
constexpr float kTabsWidth = 1180.0f; // the tab bar between them
constexpr float kSortWidth = 330.0f;  // the sort picker in the header
constexpr float kViewWidth = 250.0f;  // the view picker, left of it
constexpr float kGridTop = 280.0f;
constexpr float kSubTop = 262.0f;                  // the systems' row under the Emulators tab
constexpr float kSubRoom = 56.0f;                  // how far the covers move down for it
constexpr const char *kEmulatorsTab = "emulators"; // its key
constexpr const char *kHiddenTab = "hidden";       // the titles the player hid, last
constexpr float kContinueRoom = 196.0f;    // the Continue playing row over the All tab's covers
constexpr std::size_t kContinueCount = 10; // titles in it
constexpr float kGridBottom = 972.0f;
// Seven columns keep two whole rows in view, so scrolling never leaves a
// single row alone on the screen.
constexpr int kColumns = 7;
constexpr std::size_t kMaxQuery = 40; // bytes
constexpr std::size_t kMaxName = 24;  // bytes, as orion::LibraryPrefs keeps them
constexpr std::size_t kMaxPath = 160; // bytes, as orion::valid_art_dir takes them
// The launch helper shipped in the app folder, and the console's ELF loader.
constexpr const char *kLauncherPath = "/app0/assets/payloads/orion-launcher.elf";
constexpr int kLoaderPort = 9021;
constexpr float kHoldToDelete = 1.5f; // seconds of Cross on Delete
// The launch helper reads a request of 4 KiB; a game's path is one line of it.
constexpr std::size_t kMaxRequestPath = 1024;
// The FTP server payload players commonly run (ftpsrv): it can read what
// ORION's sandbox cannot, the system's copies of home-screen pictures.
constexpr int kFtpPort = 2121;
// How long Circle is held to close ORION.
constexpr float kHoldToExit = 1.2f;
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
    {"all", i18n::marked("All")},
    {"games", i18n::marked("Games")},
    {"homebrew", i18n::marked("Homebrew")},
    {"media", i18n::marked("Media")},
    {"favourites", i18n::marked("Favourites")},
};

// In the order of orion::kSortKeys.
enum class Sort : int
{
    name,
    name_reversed,
    recent,
    platform,
    tag,
    played,
    most_played,
};

// What the on-screen keyboard is typing into.
enum class Typing : std::uint8_t
{
    none,
    search,
    new_tab,           // from Settings > Tabs > New tab
    new_tab_for_title, // from a title's menu: the title moves into it
    rename_tab,
    rename_title, // from a title's menu
    theme_name,   // from Settings > Save as new theme
    art_folder,   // from Settings > Box art > Your own folder
    rom_folder,   // from Settings > Storage > Add a ROM folder
};

// Tags of the menu items.
enum : int
{
    kItemFavourite = 1,
    kItemNewTab,
    kItemUnplace,
    kItemLaunch,
    kItemClose,
    kItemHide,
    kItemPlayWith,
    kItemRemove,
    kItemRemoveHome,
    kItemRemoveErase,
    kItemRemoveRom,
    kItemHdArt,
    kItemRename,
    kItemMove = 100,   // + the tab's index
    kItemPlayer = 300, // 0: the default, then 1 + an index in library::players_for
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
    {0.2f, 0, Direction::right},
    {0.2f, 0, Direction::right},
    {0.6f, 0, Direction::none, "backdrop-loading"},
    {1.2f, 0, Direction::none, "backdrop"},
    {0.2f, 0, Direction::right},
    {0.2f, 0, Direction::right},
    {1.6f, 0, Direction::none, "backdrop-icon"},
    {0.2f, 0, Direction::left},
    {0.2f, 0, Direction::left},
    {0.2f, 0, Direction::left},
    {0.2f, 0, Direction::left},
    {0.4f, action_bit(Action::jump_next)},
    {0.6f, action_bit(Action::jump_next), Direction::none, "games"},
    {0.6f, action_bit(Action::west), Direction::none, "homebrew"},
    {0.5f, action_bit(Action::jump_prev)},
    {0.3f, action_bit(Action::jump_prev)},
    // Up from the first row: the sort picker, then its list; Circle closes
    // it and down goes back to the covers.
    {0.3f, 0, Direction::up},
    {0.6f, 0, Direction::none, "sort-focus"},
    {0.3f, action_bit(Action::confirm)},
    {0.7f, 0, Direction::none, "sort-open"},
    {0.3f, action_bit(Action::back)},
    {0.3f, 0, Direction::down},
    {0.6f, action_bit(Action::menu), Direction::none, "favourite"},
    {0.6f, action_bit(Action::back), Direction::none, "title-menu"},
    // Cross launches: on a PC the card says it needs the console.
    {0.4f, action_bit(Action::confirm)},
    {0.7f, 0, Direction::none, "launch"},
    // Settings: the sections, the list of tabs, the last tab's page (the PS2
    // tab with RetroArch installed), then Appearance; Circle twice closes it.
    {0.4f, action_bit(Action::r3)},
    {0.9f, 0, Direction::none, "settings"},
    {0.3f, action_bit(Action::jump_next)},
    {0.2f, 0, Direction::right},
    {0.12f, 0, Direction::down},
    {0.12f, 0, Direction::down},
    {0.12f, 0, Direction::down},
    {0.12f, 0, Direction::down},
    {0.12f, 0, Direction::down},
    {0.12f, 0, Direction::down},
    {0.12f, 0, Direction::down},
    {0.12f, 0, Direction::down},
    {0.12f, 0, Direction::down}, // past Group emulator tabs
    {0.7f, 0, Direction::none, "settings-tabs"},
    {0.3f, action_bit(Action::confirm)},
    {0.8f, 0, Direction::none, "settings-tab-page"},
    {0.2f, action_bit(Action::back)},
    {0.2f, action_bit(Action::back)},
    // Emulators: Scan, every emulator found, then the first installed one's
    // page (its game folders; the rows of ones not installed are skipped).
    {0.3f, action_bit(Action::jump_next)},
    {0.2f, 0, Direction::right},
    {0.8f, 0, Direction::none, "settings-emulators"},
    {0.12f, 0, Direction::down},
    {0.3f, action_bit(Action::confirm)},
    {0.8f, 0, Direction::none, "settings-emulator-page"},
    {0.2f, action_bit(Action::back)},
    {0.2f, action_bit(Action::back)},
    {0.3f, action_bit(Action::jump_next)},
    {0.2f, 0, Direction::right},
    {0.12f, 0, Direction::down},
    {0.12f, 0, Direction::down},
    {0.8f, 0, Direction::none, "settings-art"},
    {0.2f, action_bit(Action::back)},
    {0.3f, action_bit(Action::jump_next)},
    {0.2f, 0, Direction::right},
    {0.8f, 0, Direction::none, "settings-storage"},
    {0.2f, action_bit(Action::back)},
    {0.3f, action_bit(Action::jump_next)},
    {0.2f, 0, Direction::right},
    {0.8f, 0, Direction::none, "settings-appearance"},
    // A theme for this tab alone (past ORION, the first), seen at once
    // beside the panel, then back to the theme every tab wears.
    {0.2f, 0, Direction::right},
    {0.2f, 0, Direction::right},
    {0.9f, 0, Direction::none, "settings-appearance-live"},
    {0.2f, 0, Direction::left},
    {0.2f, 0, Direction::left},
    {0.2f, action_bit(Action::back)},
    {0.3f, action_bit(Action::jump_next)},
    {0.2f, 0, Direction::right},
    {0.8f, 0, Direction::none, "settings-sound"},
    {0.2f, action_bit(Action::back)},
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
    // On to the emulated systems' tabs, past the five shelves.
    {0.2f, action_bit(Action::back)},
    {0.2f, action_bit(Action::jump_next)},
    {0.2f, action_bit(Action::jump_next)},
    {0.2f, action_bit(Action::jump_next)},
    {0.2f, action_bit(Action::jump_next)},
    {0.3f, action_bit(Action::jump_next)},
    {0.7f, 0, Direction::none, "system-tab"},
    {0.2f, action_bit(Action::jump_prev)},
    {0.2f, action_bit(Action::jump_prev)},
    {0.2f, action_bit(Action::jump_prev)},
    {0.2f, action_bit(Action::jump_prev)},
    {0.2f, action_bit(Action::jump_prev)},
    // Circle clears the search; held, it starts closing ORION (not long
    // enough here to finish).
    {0.1f, action_bit(Action::back)},
    {0.8f, 0, Direction::none, "hold-exit", 0.0f, 0.0f, action_bit(Action::back)},
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

// h, s and v in 0..1.
Color hsv(float h, float s, float v)
{
    const float sector = h * 6.0f;
    const float chroma = v * s;
    const float x = chroma * (1.0f - std::fabs(std::fmod(sector, 2.0f) - 1.0f));
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    switch (static_cast<int>(sector) % 6)
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
    const float m = v - chroma;
    return Color{r + m, g + m, b + m, 1.0f};
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

// emulator: the app that plays a game for an emulator ("RetroArch").
std::string describe(const Title &title, const std::string &emulator)
{
    if (title.is_rom())
        return library::short_system_name(title.system) + "  ·  " + emulator;
    std::string text = library::platform_name(title.platform);
    if (!text.empty())
        text += "  ·  ";
    text += title.source == library::Source::image       ? tr("Image")
            : title.source == library::Source::installed ? tr("Installed")
                                                         : tr("Folder");
    return text;
}

const char *shelf_name(library::Shelf shelf)
{
    switch (shelf)
    {
    case library::Shelf::games:
        return i18n::marked("Game");
    case library::Shelf::homebrew:
        return i18n::marked("Homebrew");
    case library::Shelf::media:
        return i18n::marked("Media");
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

// In the order of orion::kViewKeys.
enum class View : int
{
    grid,
    cases,
    list,
};

View view_of(const std::string &key)
{
    for (int i = 0; i < orion::kViewCount; ++i)
    {
        if (key == orion::kViewKeys[i])
            return static_cast<View>(i);
    }
    return View::grid;
}

Sort sort_of(const std::string &key)
{
    for (int i = 0; i < orion::kSortCount; ++i)
    {
        if (key == orion::kSortKeys[i])
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
          favourites_(library::load_favourites(context.library.state_dir)),
          plays_(load_plays(context.library.state_dir))
    {
        tabs_.style.kind = ui::TabKind::pill;
        tabs_.style.height = 52.0f;
        tabs_.style.text_size = 21.0f;
        tabs_.style.badge_size = 16.0f;
        tabs_.style.padding = 20.0f;
        tabs_.style.on_page = true;
        tabs_.style.focus_ring = false;
        // Between L1 and R1, which say there is more to either side.
        tabs_.set_bounds({kMargin + kTabEdge, kTabsTop, kTabsWidth, 56.0f});
        // The systems, when they share the Emulators tab: a lighter row.
        subtabs_.style.kind = ui::TabKind::underline;
        subtabs_.style.track = false;
        subtabs_.style.height = 42.0f;
        subtabs_.style.text_size = 19.0f;
        subtabs_.style.badge_size = 14.0f;
        subtabs_.style.padding = 16.0f;
        subtabs_.style.thickness = 3.0f;
        subtabs_.style.on_page = true;
        subtabs_.style.focus_ring = false;
        subtabs_.set_bounds({kMargin + kTabEdge, kSubTop, kTabsWidth, 44.0f});

        // The tab's order, at the header's right.
        sort_.style.label = ui::SelectLabel::none;
        sort_.style.field_height = 50.0f;
        sort_.style.value_size = 22.0f;
        sort_.style.padding = 18.0f;
        sort_.style.on_page = true;
        sort_.style.max_rows = 7;
        sort_.style.row_height = 52.0f;
        sort_.style.option_size = 22.0f;
        sort_.set_bounds({gfx::kVirtualWidth - kMargin - kSortWidth, 132.0f, kSortWidth, 50.0f});
        sort_.set_limits({0.0f, 0.0f, gfx::kVirtualWidth, gfx::kVirtualHeight - 60.0f});
        view_pick_.style = sort_.style;
        view_pick_.style.max_rows = 4;
        view_pick_.set_bounds({sort_.bounds().x - 16.0f - kViewWidth, 132.0f, kViewWidth, 50.0f});
        view_pick_.set_limits({0.0f, 0.0f, gfx::kVirtualWidth, gfx::kVirtualHeight - 60.0f});

        grid_.style.columns = kColumns;
        grid_.style.gap_x = 28.0f;
        grid_.style.gap_y = 30.0f;
        // Every edge refuses, as the home screen's own grids do, but the top
        // one: up from the first row goes to the sort picker.
        grid_.style.wrap = ui::GridWrap::none;
        grid_.style.exits.up = true;
        grid_.style.card.title_size = 22.0f;
        grid_.style.card.subtitle_size = 17.0f;
        grid_.style.card.badge_corner = ui::CardCorner::top_right;
        grid_.style.card.glow = true;
        grid_.art = [this](ui::Canvas &canvas, const Rect &art, float radius,
                           const ui::CardItem &item, float focus)
        {
            if (view() == View::cases)
                draw_case(canvas, art, item, focus);
            else
                draw_art(canvas, art, radius, item, focus);
        };
        grid_card_ = grid_.style.card;
        cases_.art = grid_.art;
        grid_.set_bounds(
            {kMargin, kGridTop, gfx::kVirtualWidth - 2.0f * kMargin, kGridBottom - kGridTop});

        // Continue playing: small covers in a row, the name under each.
        continue_.title = tr("Continue playing");
        continue_.style.title_size = 20.0f;
        continue_.style.title_gap = 10.0f;
        continue_.style.counter = false;
        continue_.style.item_width = 118.0f;
        continue_.style.gap = 20.0f;
        continue_.style.peek = 0.0f;
        continue_.style.card.art_aspect = 1.0f;
        continue_.style.card.title_size = 16.0f;
        continue_.style.card.subtitle_size = 0.0f;
        continue_.style.card.text_gap = 8.0f;
        continue_.style.card.focus_scale = 1.04f;
        continue_.style.card.glow = true;
        continue_.art = grid_.art;
        continue_.set_bounds(
            {kMargin, kGridTop - 6.0f, gfx::kVirtualWidth - 2.0f * kMargin, kContinueRoom});

        keys_.style.bindings = ui::KeyboardBindings::standard();
        keys_.style.bindings.done = Action::menu;
        keys_.set_bounds(kKeysRect);
        keys_.on_text = [this](std::string_view utf8) { type(utf8); };
        keys_.on_backspace = [this] { erase(); };

        menu_.set_bounds({kMargin, 120.0f, gfx::kVirtualWidth - 2.0f * kMargin, 900.0f});
        menu_.style.width = 420.0f;  // room for "Remove from home screen…"
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
        // The languages: ORION's files beside its themes, then the player's.
        {
            std::string builtin = context_.library.themes_dir;
            const std::size_t slash = builtin.rfind('/');
            builtin = slash == std::string::npos ? "lang" : builtin.substr(0, slash) + "/lang";
            languages_.load(builtin, state.empty() ? "" : state + "/lang");
            if (!state.empty() && !languages_.problems().empty())
            {
                std::string report;
                for (const std::string &problem : languages_.problems())
                    report += problem + "\n";
                save::write_atomic(state + "/languages.txt", report);
            }
            use_language();
        }
        settings_.set_themes(themes_);
        settings_.set_state_dir(state);
        art_folder_was_ = orion::art_folder(prefs_, state);
        settings_.set_emulator(context_.library.emulator);
        setup_.set_themes(themes_);
        setup_.set_emulator(context_.library.emulator);
        setup_.set_state_dir(state);
        setup_.set_titles(static_cast<int>(std::count_if(
            titles().begin(), titles().end(), [](const Title &t) { return !t.is_rom(); })));
        setup_.set_autostart_payload(autostart_payload());
        // Where the folder browser starts; the ones not plugged in are left out.
        set_browse_places();
        settings_.set_app_settings(context_.settings);
        settings_.set_autostart_payload(autostart_payload());
        // A console that has not been set up yet starts on the setup.
        setup_pending_ = context_.library.offer_setup && !prefs_.setup_done;
        restyle();
        // ORION reopens where it was left, when asked to.
        std::string start = prefs_.reopen_last_tab ? prefs_.last_tab : std::string("all");
        // A removal the launch helper did while ORION was closed.
        take_remove_result();
        take_update_result();
        tidy_update_folder();
        rebuild_tabs(start);
        restyle();
        rebuild();
        // RetroArch's own files, if the launch helper could not put them back
        // after a game (library/emulators.hpp): ORION does it now.
        if (const library::EmulatorApp *retroarch = context_.library.emulator.app("retroarch"))
        {
            put_back(retroarch->root + "/args.txt");
            put_back(retroarch->root + "/test-run.txt");
            put_back(retroarch->root + "/config/frontend.cfg"); // earlier builds
        }
        generation_ = context_.library.generation;
        // New games for an emulator look for their box art in the background.
        if (prefs_.auto_art && sys::on_console())
        {
            sys::art_trace("ORION started; looking for box art", true);
            start_art(false);
        }
        // Requests are read once, by a helper that cannot remove them: any
        // still here are leftovers, never to be taken for new ones.
        if (sys::on_console())
        {
            std::remove((state + "/launch-request.txt").c_str());
            std::remove((state + "/backdrop-request.txt").c_str());
        }
        if (prefs_.game_backgrounds && sys::on_console())
            request_backdrops();
        if (sys::on_console())
            request_icons();
        // Installed packages and images mounted from elsewhere.
        if (sys::on_console() && !state.empty())
            registered_.start(state, kFtpPort);
    }

    // ---- sharper pictures for the console's titles ----
    // ShadowMountPlus keeps 128-pixel icons of the games it mounts from
    // images; the home screen has the 512-pixel icon0.png in
    // /user/appmeta/<ID>/. It is copied once, through the console's FTP
    // server, to <state>/icons/<ID>.png (or <ID>.none when there is none),
    // and shown from there (library::prefer_saved_icons).
    std::string icon_folder() const
    {
        return context_.library.state_dir + "/icons";
    }

    void request_icons()
    {
        if (context_.library.state_dir.empty())
            return;
        const std::string folder = icon_folder();
        std::vector<std::string> ids;
        for (const Title &title : titles())
        {
            struct stat info = {};
            if (title.is_rom() || title.icon.find("/icon-cache/") == std::string::npos ||
                ::stat((folder + "/" + title.id + ".png").c_str(), &info) == 0 ||
                ::stat((folder + "/" + title.id + ".none").c_str(), &info) == 0)
                continue;
            ids.push_back(title.id);
        }
        if (!ids.empty())
            icons_waiting_ = std::move(ids); // after the backgrounds: one FTP run at a time
    }

    // False while a run is going.
    bool start_icon_copies(std::vector<std::string> ids)
    {
        if (icon_copies_.running() || backdrop_copies_.running() ||
            !orion::make_directories(icon_folder()))
            return false;
        icon_ids_ = ids;
        return icon_copies_.start(std::move(ids), icon_folder(), kFtpPort, {"/icon0.png"});
    }

    // Starts the waiting run when the FTP server is free; reports what a run
    // copied, so the covers show it at once.
    void poll_icon_copies()
    {
        if (!icons_waiting_.empty() && start_icon_copies(icons_waiting_))
            icons_waiting_.clear();
        if (icon_ids_.empty() || icon_copies_.running())
            return;
        for (const std::string &id : icon_ids_)
        {
            const std::string path = icon_folder() + "/" + id + ".png";
            struct stat info = {};
            if (::stat(path.c_str(), &info) == 0)
                context_.new_icons.emplace_back(id, path);
        }
        icon_ids_.clear();
    }

    // Titles with no home-screen picture in their own folder: the launch
    // helper, outside the sandbox, copies the system's into ORION's
    // backdrops folder. Each title is asked about once (<ID>.png or <ID>.none).
    // Where the copies of the system's pictures go: ORION's own folder.
    std::string backdrop_folder() const
    {
        return context_.library.state_dir + "/backdrops";
    }

    // Titles with no home-screen picture in their own folder get the
    // system's, fetched through the console's FTP server (orion/backdrop_copies.hpp).
    // Each title is asked about once (<ID>.png or <ID>.none).
    void request_backdrops()
    {
        if (context_.library.state_dir.empty())
            return;
        const std::string folder = backdrop_folder();
        std::vector<std::string> ids;
        for (const Title &title : titles())
        {
            if (title.is_rom())
                continue;
            struct stat info = {};
            const auto exists = [&](const std::string &file)
            { return ::stat(file.c_str(), &info) == 0; };
            if (title.source == library::Source::folder &&
                (exists(title.path + "/sce_sys/pic0.png") ||
                 exists(title.path + "/sce_sys/pic1.png")))
                continue;
            if (exists(folder + "/" + title.id + ".png") ||
                exists(folder + "/" + title.id + ".none"))
                continue;
            ids.push_back(title.id);
        }
        // A run that never ended (ORION closed, or worse, during it) leaves
        // its marker: then this start does without, so a failing fetch can
        // never keep ORION from opening.
        const std::string marker = folder + "/.fetching";
        struct stat info = {};
        if (::stat(marker.c_str(), &info) == 0)
        {
            std::remove(marker.c_str());
            sys::art_trace("background copies skipped: the last run did not finish");
            return;
        }
        if (!ids.empty() && orion::make_directories(folder) &&
            save::write_atomic(marker, "").empty())
            backdrop_copies_.start(std::move(ids), folder, kFtpPort);
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
        // The app scanned the emulators again: indices into the titles are
        // stale until everything is rebuilt.
        if (context_.library.generation != generation_)
        {
            generation_ = context_.library.generation;
            if (std::exchange(scan_asked_, false))
                emulators_scanned(feedback);
            else
                library_changed();
        }
        // The song playing now, for Settings, Sound.
        if (context_.now_playing != now_playing_)
        {
            now_playing_ = context_.now_playing;
            settings_.set_now_playing(now_playing_);
            if (settings_.is_open() && settings_.section() == orion::SettingsPanel::Section::sound)
                settings_.refresh(look_, prefs_);
        }
        age_ += dt;
        clock_ += dt;
        follow_backdrop(dt);
        poll_art(feedback);
        poll_icon_copies();
        if (registered_.take_finished() && registered_.result().changed)
            context_.registered_changed = true;
        const bool reduced = context_.settings.reduced_motion;
        if (reduced != reduced_)
        {
            reduced_ = reduced;
            restyle();
        }

        const bool browsing = typing_ == Typing::none && !menu_.is_open() && !settings_.is_open() &&
                              !setup_.is_open() && !dialog_.is_open();
        const bool back_held = (input.held & action_bit(Action::back)) != 0;
        if (browsing && input.is_pressed(Action::back))
            hold_armed_ = true;
        if (!back_held || !browsing)
            hold_armed_ = false;
        hold_back_ = hold_armed_ ? hold_back_ + dt : 0.0f;
        if (hold_back_ >= kHoldToExit && !context_.exit_requested)
        {
            context_.exit_requested = true;
            feedback.play(audio::Cue::back);
        }

        if (setup_pending_)
        {
            setup_pending_ = false;
            setup_.open(look_, prefs_, feedback);
        }
        // One layer has the input: the keyboard, then a menu, then the setup
        // or Settings, then the library itself.
        if (dialog_.is_open())
            update_dialog(input, dt, feedback);
        else if (typing_ != Typing::none)
            update_typing(input, feedback);
        else if (menu_.is_open())
            update_menu(input, feedback);
        else if (setup_.is_open())
            update_setup(input, feedback);
        else if (settings_.is_open())
            update_settings(input, feedback);
        else
            update_browse(input, feedback);

        tabs_.update(dt);
        subtabs_.update(dt);
        grid_.set_active(!header_focus_ && !continue_focus_);
        grid_.update(dt);
        continue_.set_active(continue_focus_ && !header_focus_);
        continue_.update(dt);
        update_cases(dt);
        sort_.set_active(header_focus_ && !header_on_view_);
        sort_.update(dt);
        view_pick_.set_active(header_focus_ && header_on_view_);
        view_pick_.update(dt);
        header_refusal_.update(dt);
        update_status(dt);
        update_updates();
        update_installer();
        if (update_waiting_ && !dialog_.is_open() && !menu_.is_open() && !settings_.is_open() &&
            !setup_.is_open() && typing_ == Typing::none && launch_pending_ <= 0.0f)
            ask_update(feedback);
        want_icons();
        keys_.update(dt);
        menu_.update(dt);
        dialog_.update(dt);
        settings_.update(dt);
        follow_preview();
        setup_.update(dt);
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
        const bool still = context_.settings.reduced_motion || !shown_.motion;
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
        draw_backdrop(frame.scene);

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
            if (view() == View::cases)
                cases_.draw(covers);
            else
                grid_.draw(covers);
            frame.scene.pop_opacity();
        }
        if (continue_shown())
        {
            ui::Canvas row{frame.scene, context_.fonts, 0, clock_};
            frame.scene.push_opacity(intro);
            continue_.draw(row);
            frame.scene.pop_opacity();
        }
        list.push_opacity(intro);

        draw_header(list, paint);
        sort_.draw(canvas);
        view_pick_.draw(canvas);
        tabs_.draw(canvas);
        draw_tab_edges(list, paint, tabs_, ui::Button::l1, ui::Button::r1, 40.0f, false);
        if (on_emulators_tab())
        {
            subtabs_.draw(canvas);
            draw_tab_edges(list, paint, subtabs_, ui::Button::l2, ui::Button::r2, 34.0f, true);
        }
        draw_tab_sheen(list);
        draw_query(list, paint);
        if (grid_.count() > 0)
            draw_details(list, paint);
        else
            draw_empty(list, paint);
        // A keyboard Settings asked for (a folder to type) goes over it: the
        // sheet covers the whole screen.
        const bool over_settings = settings_.visible();
        if (keyboard_shown_.value > 0.01f && !over_settings)
            draw_keyboard(canvas, paint);
        else if (keyboard_shown_.value <= 0.01f)
            draw_hints(list, paint);
        draw_launch_card(list, paint);
        draw_hold_to_exit(list, paint);
        settings_.draw(canvas);
        setup_.draw(canvas);
        if (keyboard_shown_.value > 0.01f && over_settings)
            draw_keyboard(canvas, paint);
        menu_.draw(canvas);   // last: it belongs to whatever it was opened over
        dialog_.draw(canvas); // a question over everything
        sort_.draw_popover(canvas);
        view_pick_.draw_popover(canvas);
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
        shown_ = shown_look();
        theme_ = orion::themed(shown_, themes_);
        tabs_.style.theme = grid_.style.theme = menu_.style.theme = sort_.style.theme = theme_;
        dialog_.style.theme = theme_;
        dialog_.style.reduced_motion = reduced_;
        subtabs_.style.theme = theme_;
        sort_.style.reduced_motion = context_.settings.reduced_motion;
        view_pick_.style.theme = theme_;
        view_pick_.style.reduced_motion = context_.settings.reduced_motion;
        keys_.style.theme = clear_glass(theme_);
        grid_.style.card.radius = shown_.corners;
        apply_cover_shape();
        if (!shown_.chrome.cover_rim)
        {
            // The card strokes the theme's border over every picture.
            ui::Theme covers = theme_;
            covers.border = 0.0f;
            grid_.style.theme = covers;
        }
        // Plain tabs are text on the page, the active one lit and underlined,
        // as on the home screen; pills sit on a frosted track.
        const bool plain = shown_.chrome.tabs == orion::TabStyle::plain;
        tabs_.style.kind = plain ? ui::TabKind::underline : ui::TabKind::pill;
        tabs_.style.track = !plain;
        tabs_.style.text_size = plain ? 24.0f : 21.0f;
        tabs_.style.thickness = 3.0f;
        // The aurora ring replaces the theme's ring and glow on covers.
        const bool aurora = shown_.chrome.focus == orion::FocusRing::aurora;
        grid_.style.card.ring = !aurora;
        grid_.style.card.glow = !aurora;
        // The Continue playing row wears the covers' look.
        cases_.style.theme = grid_.style.theme;
        cases_.style.reduced_motion = reduced_;
        continue_.style.theme = grid_.style.theme;
        continue_.style.card.radius = shown_.corners;
        continue_.style.card.ring = !aurora;
        continue_.style.card.glow = !aurora;
        tabs_.style.reduced_motion = grid_.style.reduced_motion = keys_.style.reduced_motion =
            menu_.style.reduced_motion = continue_.style.reduced_motion = reduced_;
        settings_.set_theme(theme_, reduced_);
        setup_.set_theme(theme_, reduced_);
    }

    // The shape of the active tab's covers: the one chosen in Settings, else
    // the one most of its pictures have (console icons are square; box art
    // is often a tall case or a wide box).
    orion::CoverShape cover_shape() const
    {
        const std::string key = active_key();
        const orion::CoverShape chosen = prefs_.tab_cover(key);
        if (chosen != orion::CoverShape::automatic)
            return chosen;
        int square = 0;
        int portrait = 0;
        int landscape = 0;
        for (const Title &title : titles())
        {
            if (title.icon_width <= 0 || title.icon_height <= 0 || !on_tab(title, key))
                continue;
            const float aspect =
                static_cast<float>(title.icon_width) / static_cast<float>(title.icon_height);
            (aspect < 0.85f ? portrait : aspect > 1.18f ? landscape : square) += 1;
        }
        if (portrait > square && portrait >= landscape)
            return orion::CoverShape::portrait;
        if (landscape > square && landscape > portrait)
            return orion::CoverShape::landscape;
        return orion::CoverShape::square;
    }

    // Tall cases want narrower columns, wide boxes wider ones, so a row of
    // covers keeps about the same weight whatever their shape.
    void apply_cover_shape()
    {
        if (view() != View::grid)
            return; // apply_view() sizes the other views
        switch (cover_shape())
        {
        case orion::CoverShape::portrait:
            grid_.style.columns = kColumns + 1;
            grid_.style.card.art_aspect = 5.0f / 7.0f;
            break;
        case orion::CoverShape::landscape:
            grid_.style.columns = kColumns - 2;
            grid_.style.card.art_aspect = 7.0f / 5.0f;
            break;
        default:
            grid_.style.columns = kColumns;
            grid_.style.card.art_aspect = 1.0f;
            break;
        }
    }

    // The view of the active tab: its own, else the Library's.
    View view() const
    {
        const std::string own = prefs_.tab_view(active_key());
        return view_of(own.empty() ? prefs_.view : own);
    }

    // The grid as the view wants it. Cases: one row of three big cases,
    // centred, as tall as the room under the tabs allows, scrolled a row at a
    // time. List: one wide row per title, drawn by draw_row().
    void apply_view(float top)
    {
        const float room = kGridBottom - top;
        const float width = gfx::kVirtualWidth - 2.0f * kMargin;
        grid_.style.card.text = grid_card_.text;
        grid_.style.card.focus_scale = grid_card_.focus_scale;
        grid_.style.cell_width = 0.0f;
        grid_.style.cell_height = 0.0f;
        grid_.style.padding = -1.0f;
        grid_.style.gap_x = 28.0f;
        grid_.style.gap_y = 30.0f;
        grid_.content = nullptr;
        switch (view())
        {
        case View::cases:
        {
            // A shelf, not a grid: the case in focus in the middle, larger,
            // the one before and after either side; left and right turn it.
            // The grid keeps the order and the focus, out of sight.
            constexpr float kAspect = 0.79f; // a disc case, 135 x 170
            constexpr float kGrow = 1.1f;    // the case in focus
            cases_.style.mode = ui::CarouselMode::centered;
            cases_.style.card = grid_.style.card;
            cases_.style.card.art_aspect = kAspect;
            cases_.style.card.focus_scale = kGrow;
            cases_.style.card.badge_corner = ui::CardCorner::top_right;
            cases_.style.counter = false;
            cases_.style.neighbour_fade = 0.3f;
            cases_.style.edge_fade = 0.6f;
            cases_.style.gap = 70.0f;
            cases_.style.peek = 0.0f;
            const float text = ui::card_height(cases_.style.card, 100.0f) - 100.0f / kAspect;
            cases_.style.item_width = std::min(((room - 48.0f) / kGrow - text) * kAspect, 540.0f);
            cases_.set_bounds({kMargin, top, width, room});
            grid_.set_bounds({kMargin, top, width, room});
            return;
        }
        case View::list:
            grid_.style.columns = 1;
            grid_.style.cell_height = 92.0f;
            grid_.style.gap_y = 12.0f;
            grid_.style.card.focus_scale = 1.0f;
            grid_.content = [this](ui::Canvas &canvas, const Rect &cell, const ui::CardItem &item,
                                   int, float focus) { draw_row(canvas, cell, item, focus); };
            break;
        default:
            apply_cover_shape();
            break;
        }
        grid_.set_bounds({kMargin, top, width, room});
    }

    // Cases view each frame: the shelf follows the grid's focus (a jump, a
    // search) and its pictures as they arrive, and the cases near the focus
    // get sharp pictures; any other view lets them go.
    void update_cases(float dt)
    {
        std::vector<std::string> sharp;
        if (view() == View::cases && grid_.count() > 0)
        {
            if (static_cast<int>(cases_.items().size()) != grid_.count())
                cases_.set_items(grid_.items());
            if (cases_.focus() != grid_.focus())
                cases_.set_focus(grid_.focus(), false);
            for (int i = 0; i < grid_.count(); ++i)
            {
                const ui::CardItem &from = grid_.items()[static_cast<std::size_t>(i)];
                ui::CardItem &to = cases_.item(i);
                to.texture = from.texture;
                to.image_aspect = from.image_aspect;
            }
            // The three in view and two either side, ready for a turn.
            constexpr int kReach = 3;
            const int focus = grid_.focus();
            for (int i = std::max(0, focus - kReach);
                 i <= std::min(grid_.count() - 1, focus + kReach); ++i)
                sharp.push_back(titles()[static_cast<std::size_t>(
                                             grid_.items()[static_cast<std::size_t>(i)].tag)]
                                    .id);
        }
        if (sharp != context_.sharp_icons)
            context_.sharp_icons = std::move(sharp);
        cases_.set_active(!header_focus_);
        cases_.update(dt);
    }

    // The order of the active tab: its own, else the Library's.
    std::string sort_key() const
    {
        const std::string own = prefs_.tab_sort(active_key());
        return own.empty() ? prefs_.sort : own;
    }

    // What the screen wears: the active tab's own theme, else the main look.
    orion::Look shown_look() const
    {
        const std::string id = prefs_.tab_theme(active_key());
        if (const orion::ThemeFile *file = id.empty() ? nullptr : themes_.find(id))
            return orion::look_of(*file);
        return look_;
    }

    void save_prefs() const
    {
        const std::string &state = context_.library.state_dir;
        orion::save_prefs(state, prefs_);
        // The autostart payload reads no JSON: a file that exists says "off".
        if (state.empty())
            return;
        const std::string off = state + "/autostart-off";
        if (prefs_.autostart)
            std::remove(off.c_str());
        else
            save::write_atomic(off, "ORION's autostart is off (Settings, Library)\n");
    }

    // ---- input ----

    // What ORION is doing in the background, for the line at the header's
    // top right: it shows while there is something, then fades.
    bool updating() const
    {
        return installer_.state() == orion::UpdateInstaller::State::downloading ||
               installer_.state() == orion::UpdateInstaller::State::unpacking;
    }

    static std::string megabytes(std::size_t bytes)
    {
        return std::to_string((bytes + 512u * 1024u) / (1024u * 1024u)) + " MB";
    }

    void update_status(float dt)
    {
        std::string status;
        float progress = -1.0f;
        if (context_.rescan_requested)
            status = tr("Looking for emulators and their games…");
        else if (art_.running())
        {
            const orion::ArtFetcher::Progress now = art_.progress();
            status = trf("Downloading box art  ·  {0} of {1}",
                         {std::to_string(now.done), std::to_string(now.total)});
            if (now.total > 0)
                progress = static_cast<float>(now.done) / static_cast<float>(now.total);
        }
        else if (backdrop_copies_.running())
            status = tr("Copying game backgrounds…");
        else if (icon_copies_.running())
            status = tr("Copying HD pictures…");
        else if (updating())
        {
            const orion::Release release = installer_.release();
            if (installer_.state() == orion::UpdateInstaller::State::unpacking)
                status = trf("Checking and unpacking ORION {0}…", {release.version});
            else
            {
                // The file as it grows, against the size GitHub lists.
                struct stat part = {};
                const std::size_t done = ::stat(installer_.download_path().c_str(), &part) == 0
                                             ? static_cast<std::size_t>(part.st_size)
                                             : 0;
                status =
                    release.zip_size > 0
                        ? trf("Downloading ORION {0}  ·  {1} of {2}",
                              {release.version, megabytes(done), megabytes(release.zip_size)})
                        : trf("Downloading ORION {0}  ·  {1}", {release.version, megabytes(done)});
                if (release.zip_size > 0)
                {
                    progress = static_cast<float>(done) / static_cast<float>(release.zip_size);
                }
            }
        }
        if (!status.empty())
        {
            status_ = std::move(status);
            status_progress_ = progress;
        }
        status_shown_.target = art_.running() || backdrop_copies_.running() ||
                                       icon_copies_.running() || context_.rescan_requested ||
                                       updating()
                                   ? 1.0f
                                   : 0.0f;
        status_shown_.update(dt, context_.settings.reduced_motion ? 60.0f : 6.0f);
    }

    // A header picker wide enough for its longest choice, never narrower than
    // the English layout. Measured in the semibold face with room to spare,
    // since a theme may label in a wider one.
    float picker_width(const std::vector<ui::SelectOption> &options, float least) const
    {
        const gfx::Font *font = context_.fonts.semibold.font;
        if (font == nullptr)
            return least;
        float widest = 0.0f;
        for (const ui::SelectOption &option : options)
            widest = std::max(widest, font->measure(option.label, sort_.style.value_size));
        const float chrome = 2.0f * sort_.style.padding + 2.0f * sort_.style.chevron_size + 24.0f;
        return std::clamp(widest * 1.1f + chrome, least, least * 1.6f);
    }

    // The sort picker's choices for the active tab: the library's order,
    // then each order of its own.
    void sync_sort()
    {
        std::vector<ui::SelectOption> options;
        options.emplace_back(
            trf("Default ({0})", {tr(orion::kSortNames[static_cast<int>(sort_of(prefs_.sort))])}));
        for (int i = 0; i < orion::kSortCount; ++i)
            options.emplace_back(tr(orion::kSortNames[i]));
        const float sort_width = picker_width(options, kSortWidth);
        if (!sort_.is_open())
            sort_.set_options(std::move(options));
        const std::string own = prefs_.tab_sort(active_key());
        sort_.set_index(own.empty() ? 0 : static_cast<int>(sort_of(own)) + 1);
        // The view picker the same way: the Library's view, then each.
        std::vector<ui::SelectOption> views;
        views.emplace_back(
            trf("Default ({0})", {tr(orion::kViewNames[static_cast<int>(view_of(prefs_.view))])}));
        for (int i = 0; i < orion::kViewCount; ++i)
            views.emplace_back(tr(orion::kViewNames[i]));
        const float view_width = picker_width(views, kViewWidth);
        if (!view_pick_.is_open())
            view_pick_.set_options(std::move(views));
        // Both stay right-aligned; a language with longer words widens them.
        sort_.set_bounds({gfx::kVirtualWidth - kMargin - sort_width, 132.0f, sort_width, 50.0f});
        view_pick_.set_bounds({sort_.bounds().x - 16.0f - view_width, 132.0f, view_width, 50.0f});
        const std::string own_view = prefs_.tab_view(active_key());
        view_pick_.set_index(own_view.empty() ? 0 : static_cast<int>(view_of(own_view)) + 1);
    }

    // The sort picker has the focus: Cross opens it, down (or Circle) goes
    // back to the covers. The shoulder buttons, search and Settings still work.
    void update_header(const InputFrame &input, app::Feedback &feedback)
    {
        ui::Select &picker = header_on_view_ ? view_pick_ : sort_;
        const ui::Event event = picker.handle(input, feedback);
        if (event == ui::Event::changed)
        {
            const int choice = picker.index();
            if (header_on_view_)
                prefs_.set_tab_view(active_key(),
                                    choice <= 0 ? std::string() : orion::kViewKeys[choice - 1]);
            else
                prefs_.set_tab_sort(active_key(),
                                    choice <= 0 ? std::string() : orion::kSortKeys[choice - 1]);
            tabs_changed();
            return;
        }
        if (picker.is_open() || event != ui::Event::none)
            return;
        // Left and right step between the two pickers.
        if ((input.nav == Direction::left && !header_on_view_) ||
            (input.nav == Direction::right && header_on_view_))
        {
            header_on_view_ = !header_on_view_;
            feedback.play(audio::Cue::focus);
        }
        else if (input.nav == Direction::down || input.is_pressed(Action::back))
        {
            header_focus_ = false;
            continue_focus_ = input.nav == Direction::down && continue_shown();
            feedback.play(input.nav == Direction::down ? audio::Cue::focus : audio::Cue::back);
        }
        else if (input.nav != Direction::none)
            ui::refuse(feedback, picker.style, input, header_refusal_, picker.bounds().cx());
    }

    void update_browse(const InputFrame &input, app::Feedback &feedback)
    {
        // An open sort or view list takes everything.
        if (sort_.is_open() || view_pick_.is_open())
        {
            update_header(input, feedback);
            return;
        }
        // L1 / R1 change shelf; L2 / R2 do too, which is also what the tour
        // uses, since L1 / R1 belong to the shell wherever it holds several
        // designs (the PC tools).
        const bool next =
            input.is_pressed(Action::page_next) || input.is_pressed(Action::jump_next);
        const bool previous =
            input.is_pressed(Action::page_prev) || input.is_pressed(Action::jump_prev);
        // On the Emulators tab, L2 and R2 change system instead.
        const bool sub_next = input.is_pressed(Action::jump_next);
        const bool sub_previous = input.is_pressed(Action::jump_prev);
        if (on_emulators_tab() && (sub_next || sub_previous))
        {
            if (subtabs_.step(sub_next ? 1 : -1, input, feedback) == ui::Event::changed)
            {
                prefs_.last_tab = active_key();
                save_prefs();
                restyle();
                rebuild();
            }
            return;
        }
        if (next || previous)
        {
            if (tabs_.step(next ? 1 : -1, input, feedback) == ui::Event::changed)
            {
                prefs_.last_tab = active_key();
                save_prefs();
                restyle();
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
            open_settings(feedback);
            return;
        }
        if (header_focus_)
        {
            update_header(input, feedback);
            return;
        }
        if (input.is_pressed(Action::back) && !query_.empty())
        {
            query_.clear();
            feedback.play(audio::Cue::erase);
            rebuild();
            return;
        }
        // The Continue playing row: left and right in it, down to the covers,
        // up to the sort picker; the buttons act on its title.
        if (continue_focus_ && continue_shown())
        {
            if (input.nav == Direction::down && grid_.count() > 0)
            {
                continue_focus_ = false;
                feedback.play(audio::Cue::focus);
                return;
            }
            if (input.nav == Direction::up)
            {
                continue_focus_ = false;
                focus_header(feedback);
                return;
            }
            if (input.nav == Direction::left || input.nav == Direction::right)
            {
                continue_.handle(input, feedback);
                return;
            }
        }
        // Up from the first row (or from an empty tab) reaches the row above
        // it: Continue playing, else the sort picker.
        if (grid_.count() == 0 && !continue_focus_)
        {
            if (input.nav == Direction::up)
                focus_above_grid(feedback);
            return;
        }
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
        if (continue_focus_)
            return; // the row's other input is above
        if (view() == View::cases)
        {
            // The shelf turns left and right; up leaves it, down has nowhere to go.
            if (input.nav == Direction::up)
                focus_above_grid(feedback);
            else if (input.nav == Direction::down)
                ui::refuse(feedback, cases_.style, input, header_refusal_,
                           cases_.item_rect(cases_.focus()).cx());
            else
            {
                cases_.handle(input, feedback);
                grid_.set_focus(cases_.focus(), false);
            }
            return;
        }
        grid_.handle(input, feedback);
        if (grid_.exit() == Direction::up)
            focus_above_grid(feedback);
    }

    void focus_above_grid(app::Feedback &feedback)
    {
        if (!continue_shown())
        {
            focus_header(feedback);
            return;
        }
        continue_focus_ = true;
        feedback.play(audio::Cue::focus);
    }

    void focus_header(app::Feedback &feedback)
    {
        header_focus_ = true;
        header_on_view_ = false;
        sync_sort();
        feedback.play(audio::Cue::focus);
    }

    void open_settings(app::Feedback &feedback)
    {
        settings_.set_tabs(every_tab());
        settings_.set_version(context_.version);
        settings_.set_storage(context_.library.self_path,
                              orion::count_files(backdrop_folder(), ".png"));
        settings_.set_current_tab(active_key());
        settings_.open(look_, prefs_, feedback);
    }

    // Every tab ORION can show, in tab bar order, hidden ones included.
    std::vector<orion::TabEntry> every_tab() const
    {
        std::vector<orion::TabEntry> tabs;
        for (const Shelf &shelf : kShelves)
            tabs.push_back({shelf.key, tr(shelf.name)});
        for (const library::System &system : context_.library.emulator.systems)
            tabs.push_back({"sys:" + system.name, system.short_name});
        for (const orion::CustomTab &tab : prefs_.tabs)
            tabs.push_back({"tab:" + tab.id, tab.name});
        return tabs;
    }

    // Scan for emulators: the app does it between frames (it owns the
    // titles and their pictures), then update() sees a new generation.
    void scan_emulators(app::Feedback &feedback)
    {
        context_.rescan_requested = true;
        scan_asked_ = true;
        settings_.set_scan_status(tr("Looking for emulators and their games…"));
        if (settings_.is_open())
            settings_.refresh(look_, prefs_);
        setup_.set_scan_status(tr("Looking for emulators and their games…"));
        if (setup_.is_open())
            setup_.refresh(look_, prefs_);
        feedback.play(audio::Cue::select);
    }

    void emulators_scanned(app::Feedback &feedback)
    {
        const library::Emulator &emulator = context_.library.emulator;
        int games = 0;
        for (const library::EmulatorApp &app : emulator.apps)
            games += app.games;
        const std::string found =
            trf("Found {0}, {1}", {i18n::plural(static_cast<long long>(emulator.apps.size()),
                                                "{0} emulator", "{0} emulators"),
                                   i18n::plural(games, "{0} game", "{0} games")});
        settings_.set_scan_status(found);
        setup_.set_scan_status(found);
        rebuild_tabs(active_key());
        restyle();
        rebuild();
        settings_.set_tabs(every_tab());
        settings_.set_version(context_.version);
        if (settings_.is_open())
            settings_.rebuild_nav(look_, prefs_);
        if (setup_.is_open())
            setup_.refresh(look_, prefs_);
        show_note(true, found);
        feedback.play(audio::Cue::complete);
        // New games look for their box art, as at start-up.
        if (prefs_.auto_art && sys::on_console())
            start_art(false);
    }

    // While Settings previews a tab's page, the library shows that tab (its
    // own theme, view and covers, as they are being chosen), then goes back
    // to the tab it was on. A hidden tab has nothing to show.
    void follow_preview()
    {
        const std::string want = settings_.preview_tab();
        const bool shown = std::find(tab_keys_.begin(), tab_keys_.end(), want) != tab_keys_.end() ||
                           std::find(sub_keys_.begin(), sub_keys_.end(), want) != sub_keys_.end();
        if (!want.empty() && shown && want != active_key())
        {
            if (preview_return_.empty())
                preview_return_ = active_key();
            show_tab(want);
        }
        else if (want.empty() && !preview_return_.empty())
        {
            show_tab(preview_return_);
            preview_return_.clear();
        }
    }

    void show_tab(const std::string &key)
    {
        rebuild_tabs(key);
        restyle();
        rebuild();
    }

    // Titles joined while ORION runs (the system's registered ones): every
    // index into them is rebuilt, quietly.
    void library_changed()
    {
        rebuild_tabs(active_key());
        restyle();
        rebuild();
        settings_.set_tabs(every_tab());
    }

    // After the tabs changed: the tab bar, the covers and Settings follow.
    void tabs_changed()
    {
        save_prefs();
        rebuild_tabs(active_key());
        restyle();
        rebuild();
        settings_.set_tabs(every_tab());
        settings_.set_version(context_.version);
        if (settings_.is_open())
            settings_.refresh(look_, prefs_);
    }

    // The language the prefs ask for, everywhere: the text built ahead of
    // time (tabs, rows, the keyboard's keys) is built again in it.
    void use_language()
    {
        i18n::use(languages_.pick(prefs_.language, sys::system_language()));
        const std::vector<orion::LanguageChoice> choices =
            languages_.choices(sys::system_language());
        settings_.set_languages(choices);
        setup_.set_languages(choices);
        keys_.style.space_label = i18n::tr("Space");
        keys_.style.done_label = i18n::tr("Done");
        // The hint bar holds pointers: its words live in hint_words_.
        hint_words_ = {tr("System"), tr("Shelf"), tr("Launch"),  tr("Favourite"),
                       tr("Search"), tr("More"),  tr("Settings")};
        hints_ = {ui::Hint{ui::Button::l2, hint_words_[0].c_str(), ui::Button::r2},
                  ui::Hint{ui::Button::l1, hint_words_[1].c_str(), ui::Button::r1},
                  ui::Hint{ui::Button::cross, hint_words_[2].c_str()},
                  ui::Hint{ui::Button::square, hint_words_[3].c_str()},
                  ui::Hint{ui::Button::triangle, hint_words_[4].c_str()},
                  ui::Hint{ui::Button::options, hint_words_[5].c_str()},
                  ui::Hint{ui::Button::right_stick, hint_words_[6].c_str()}};
    }

    // Where the folder browser starts; the ones not plugged in are left out.
    void set_browse_places()
    {
        std::vector<std::pair<std::string, std::string>> places;
        for (int i = 0; i < 8; ++i)
            places.emplace_back(trf("USB drive {0}", {std::to_string(i + 1)}),
                                "/mnt/usb" + std::to_string(i));
        places.emplace_back(tr("Extended storage"), "/mnt/ext0");
        places.emplace_back(trf("Extended storage {0}", {"2"}), "/mnt/ext1");
        places.emplace_back(tr("The console's storage"), "/data");
        if (!sys::on_console())
            places.emplace_back(tr("This folder (PC preview)"), ".");
        settings_.set_browse_places(std::move(places));
    }

    void language_changed(app::Feedback &feedback)
    {
        set_browse_places();
        save_prefs();
        use_language();
        settings_.set_tabs(every_tab());
        rebuild_tabs(active_key());
        restyle();
        rebuild();
        if (settings_.is_open())
            settings_.rebuild_nav(look_, prefs_);
        if (setup_.is_open())
            setup_.refresh(look_, prefs_);
        feedback.play(audio::Cue::select);
    }

    void update_setup(const InputFrame &input, app::Feedback &feedback)
    {
        switch (setup_.handle(input, feedback, &look_, &prefs_))
        {
        case orion::SetupPanel::Change::language:
            language_changed(feedback);
            break;
        case orion::SetupPanel::Change::look:
            restyle();
            orion::save_look(context_.library.state_dir, look_);
            break;
        case orion::SetupPanel::Change::prefs:
            tabs_changed();
            break;
        case orion::SetupPanel::Change::art_folder:
            art_folder_changed();
            break;
        case orion::SetupPanel::Change::scan_emulators:
            scan_emulators(feedback);
            break;
        case orion::SetupPanel::Change::finished:
            tabs_changed();
            break;
        case orion::SetupPanel::Change::none:
            break;
        }
    }

    // The autostart payload, where the jailbreak's autoloader finds it.
    std::string autostart_payload() const
    {
        const std::string &self = context_.library.self_path;
        return (self.empty() ? std::string("/data/homebrew/PPSA99061") : self) +
               "/assets/payloads/orion-autostart.elf";
    }

    void update_settings(const InputFrame &input, app::Feedback &feedback)
    {
        switch (settings_.handle(input, feedback, &look_, &prefs_))
        {
        case orion::SettingsPanel::Change::language:
            language_changed(feedback);
            break;
        case orion::SettingsPanel::Change::look:
            restyle();
            orion::save_look(context_.library.state_dir, look_);
            break;
        case orion::SettingsPanel::Change::prefs:
            tabs_changed();
            break;
        case orion::SettingsPanel::Change::new_tab:
            start_typing(Typing::new_tab, "");
            feedback.play(audio::Cue::modal_open);
            break;
        case orion::SettingsPanel::Change::save_theme:
            start_typing(Typing::theme_name, "");
            feedback.play(audio::Cue::modal_open);
            break;
        case orion::SettingsPanel::Change::rename_tab:
        {
            menu_tab_ = settings_.tab_id();
            const orion::CustomTab *tab = prefs_.tab(menu_tab_);
            start_typing(Typing::rename_tab, tab != nullptr ? tab->name : "");
            feedback.play(audio::Cue::modal_open);
            break;
        }
        case orion::SettingsPanel::Change::art_folder:
            art_folder_changed();
            break;
        case orion::SettingsPanel::Change::type_art_folder:
            start_typing(Typing::art_folder, orion::art_folder(prefs_, context_.library.state_dir));
            feedback.play(audio::Cue::modal_open);
            break;
        case orion::SettingsPanel::Change::add_rom_folder:
            folder_emulator_ = settings_.emulator_page();
            start_typing(Typing::rom_folder, folder_emulator_ == "porpoise" ? "/mnt/usb0/games"
                                             : folder_emulator_ == "ps5sx2" ? "/mnt/usb0/PS2"
                                             : folder_emulator_ == "ecam"   ? "/mnt/usb0/switch"
                                             : folder_emulator_ == "eden"   ? "/mnt/usb0/switch"
                                             : folder_emulator_ == "psswanstation"
                                                 ? "/mnt/usb0/psx"
                                                 : "/mnt/usb0/roms");
            feedback.play(audio::Cue::modal_open);
            break;
        case orion::SettingsPanel::Change::add_folder:
        {
            const std::string path = settings_.browse_result();
            if (!orion::valid_art_dir(path))
            {
                show_note(false, trf("A game folder goes under /data or /mnt: {0}", {path}));
                feedback.play(audio::Cue::error);
                break;
            }
            if (std::none_of(prefs_.rom_folders.begin(), prefs_.rom_folders.end(),
                             [&](const orion::RomFolder &f) { return f.path == path; }) &&
                prefs_.rom_folders.size() < 16)
                prefs_.rom_folders.push_back({path, "", settings_.emulator_page()});
            save_prefs();
            scan_emulators(feedback);
            show_note(true, trf("Added {0}: looking for its games", {path}));
            break;
        }
        case orion::SettingsPanel::Change::folders:
            save_prefs();
            scan_emulators(feedback);
            break;
        case orion::SettingsPanel::Change::scan_emulators:
            // The scan reads the prefs from disk (Every version of a game).
            save_prefs();
            scan_emulators(feedback);
            break;
        case orion::SettingsPanel::Change::sound:
            // The app applies the volumes and saves them (Context::settings).
            context_.settings_changed = true;
            settings_.refresh(look_, prefs_);
            break;
        case orion::SettingsPanel::Change::skip_song:
            context_.skip_song = true;
            feedback.play(audio::Cue::select);
            break;
        case orion::SettingsPanel::Change::install_update:
            settings_.close(feedback);
            install_update(feedback);
            break;
        case orion::SettingsPanel::Change::check_updates:
            check_for_updates(true);
            feedback.play(audio::Cue::select);
            break;
        case orion::SettingsPanel::Change::run_setup:
            settings_.close(feedback);
            setup_.open(look_, prefs_, feedback);
            break;
        case orion::SettingsPanel::Change::copy_backdrops:
        {
            // Every copy goes, and every title without a picture is asked about again.
            const int gone = orion::remove_files(backdrop_folder(), ".png") +
                             orion::remove_files(backdrop_folder(), ".none");
            request_backdrops();
            settings_.set_storage(context_.library.self_path, 0);
            settings_.refresh(look_, prefs_);
            show_note(true, trf("Copying the game backgrounds again ({0} cleared)",
                                {std::to_string(gone)}));
            feedback.play(audio::Cue::select);
            break;
        }
        case orion::SettingsPanel::Change::fetch_art:
            start_art(true);
            feedback.play(art_.running() ? audio::Cue::select : audio::Cue::error);
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
        keys_.style.max_length = static_cast<int>(limit_of(typing));
        keys_.set_length(static_cast<int>(buffer().size()));
    }

    static std::size_t limit_of(Typing typing)
    {
        return typing == Typing::search                                       ? kMaxQuery
               : typing == Typing::art_folder || typing == Typing::rom_folder ? kMaxPath
               : typing == Typing::rename_title                               ? orion::kMaxTitleName
                                                                              : kMaxName;
    }

    std::string &buffer()
    {
        return typing_ == Typing::search ? query_ : name_;
    }

    void type(std::string_view utf8)
    {
        const std::size_t limit = limit_of(typing_);
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
        if (typing == Typing::rename_title)
        {
            rename_title(feedback);
            return;
        }
        if (typing == Typing::rom_folder)
        {
            struct stat info = {};
            if (!orion::valid_art_dir(name_) || ::stat(name_.c_str(), &info) != 0 ||
                !S_ISDIR(info.st_mode))
            {
                show_note(false, trf("No folder at {0} (it goes under /data or /mnt)", {name_}));
                feedback.play(audio::Cue::error);
                return;
            }
            if (prefs_.rom_folders.size() < 16)
                prefs_.rom_folders.push_back({name_, "", folder_emulator_});
            save_prefs();
            scan_emulators(feedback);
            show_note(true, trf("Added {0}: looking for its games", {name_}));
            return;
        }
        if (typing == Typing::art_folder)
        {
            if (!orion::valid_art_dir(name_))
            {
                show_note(
                    false,
                    tr("A box art folder goes under /data or /mnt, like /mnt/usb0/ORION/art"));
                feedback.play(audio::Cue::error);
                settings_.refresh(look_, prefs_); // the row goes back to where it was
                return;
            }
            prefs_.art_dir = name_;
            art_folder_changed();
            feedback.play(audio::Cue::saved);
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
        tabs_changed();
        feedback.play(audio::Cue::saved);
    }

    // The name typed for the title whose menu was open: the player's name for
    // it from now on, or with nothing typed, its own name back. The app
    // renames it (Context::new_names) and the screen rebuilds.
    void rename_title(app::Feedback &feedback)
    {
        const Title *title = find_title(menu_title_);
        if (title == nullptr)
            return;
        std::string name = name_;
        while (!name.empty() && name.back() == ' ')
            name.pop_back();
        while (!name.empty() && name.front() == ' ')
            name.erase(0, 1);
        const std::string own = title->own_name.empty() ? title->name : title->own_name;
        if (name == own)
            name.clear();
        prefs_.set_title_name(title->id, name);
        save_prefs();
        context_.new_names.emplace_back(title->id, name);
        show_note(true, name.empty() ? trf("{0} has its own name again: {1}", {title->id, own})
                                     : trf("Renamed to {0}", {name}));
        feedback.play(audio::Cue::saved);
    }

    // ---- menus ----

    void open_title_menu(app::Feedback &feedback)
    {
        const Title &title = focused();
        menu_title_ = title.id;
        const orion::CustomTab *placed = prefs_.placement(title.id);

        std::vector<ui::MenuItem> items;
        ui::MenuItem launch = menu_item(tr("Launch"));
        launch.tag = kItemLaunch;
        launch.disabled = launch_pending_ > 0.0f;
        items.push_back(launch);
        ui::MenuItem gap;
        gap.separator = true;
        items.push_back(gap);
        ui::MenuItem favourite = menu_item(favourite_of(title.id) ? tr("Remove from Favourites")
                                                                  : tr("Add to Favourites"));
        favourite.shortcut = tr("Square");
        favourite.tag = kItemFavourite;
        items.push_back(favourite);
        // A game more than one emulator plays: which one, for this game.
        if (library::players_for(context_.library.emulator, title).size() >= 2)
        {
            ui::MenuItem with = menu_item(tr("Play with…"));
            with.shortcut = player_label(title);
            with.tag = kItemPlayWith;
            items.push_back(with);
        }
        const bool hidden = prefs_.title_hidden(title.id);
        ui::MenuItem hide = menu_item(hidden ? tr("Show this title") : tr("Hide this title"));
        hide.tag = kItemHide;
        items.push_back(hide);
        ui::MenuItem rename = menu_item(tr("Rename…"));
        rename.tag = kItemRename;
        items.push_back(rename);
        // A sharper picture: the home screen's for a console title shown from
        // ShadowMountPlus's small copy, box art again for a game.
        if (title.is_rom() || title.icon.find("/icon-cache/") != std::string::npos ||
            title.icon.find("/icons/") != std::string::npos)
        {
            ui::MenuItem art = menu_item(title.is_rom() ? tr("Download box art again")
                                                        : tr("Download HD picture"));
            art.tag = kItemHdArt;
            items.push_back(art);
        }
        ui::MenuItem line;
        line.separator = true;
        items.push_back(line);
        for (std::size_t i = 0; i < prefs_.tabs.size(); ++i)
        {
            const bool here = placed != nullptr && placed->id == prefs_.tabs[i].id;
            ui::MenuItem move = menu_item(trf("Move to {0}", {prefs_.tabs[i].name}));
            move.tag = kItemMove + static_cast<int>(i);
            move.disabled = here;
            move.shortcut = here ? tr("Here") : "";
            items.push_back(move);
        }
        ui::MenuItem fresh = menu_item(tr("Move to a new tab…"));
        fresh.tag = kItemNewTab;
        items.push_back(fresh);
        if (placed != nullptr)
        {
            ui::MenuItem unplace = menu_item(trf("Remove from {0}", {placed->name}));
            unplace.tag = kItemUnplace;
            unplace.danger = true;
            items.push_back(unplace);
        }
        // Sorting is the picker over the covers and Settings is R3, both in
        // view all the time: the menu keeps to the title.
        ui::MenuItem rule;
        rule.separator = true;
        items.push_back(rule);
        if (removable(title))
        {
            ui::MenuItem remove = menu_item(tr("Remove…"));
            remove.tag = kItemRemove;
            remove.danger = true;
            items.push_back(remove);
        }
        ui::MenuItem close = menu_item(tr("Close ORION"));
        close.shortcut = tr("Hold Circle");
        close.tag = kItemClose;
        items.push_back(close);
        menu_.set_items(std::move(items));
        menu_.open(grid_.cell_rect(grid_.focus()), feedback);
    }

    // The orders for the active tab: its own choice, or the Library's.
    // ---- removing ----

    // What Remove… is offered for: a game for an emulator (its file), or a
    // console title ShadowMountPlus registered from a folder or an image. An
    // emulator other games play with is not, while it has them.
    bool removable(const Title &title) const
    {
        if (title.is_rom())
            return !title.path.empty();
        if (title.path.empty())
            return false;
        for (const library::EmulatorApp &app : context_.library.emulator.apps)
        {
            if (app.title_id == title.id && app.games > 0)
                return false;
        }
        return true;
    }

    void open_remove_menu(app::Feedback &feedback)
    {
        const Title *title = find_title(menu_title_);
        if (title == nullptr)
            return;
        std::vector<ui::MenuItem> items;
        if (title->is_rom())
        {
            ui::MenuItem erase = menu_item(tr("Delete the game file…"));
            erase.tag = kItemRemoveRom;
            erase.danger = true;
            items.push_back(erase);
        }
        else
        {
            ui::MenuItem home = menu_item(tr("Remove from home screen…"));
            home.tag = kItemRemoveHome;
            items.push_back(home);
            ui::MenuItem erase = menu_item(tr("Delete from the console…"));
            erase.tag = kItemRemoveErase;
            erase.danger = true;
            items.push_back(erase);
        }
        menu_.set_items(std::move(items));
        menu_.open(grid_.cell_rect(grid_.focus()), feedback);
    }

    // The question before a removal. A delete asks for Cross held down on
    // its button, so a stray press never erases anything.
    void ask_remove(int kind, app::Feedback &feedback)
    {
        const Title *title = find_title(menu_title_);
        if (title == nullptr)
            return;
        remove_kind_ = kind;
        remove_id_ = title->id;
        remove_hold_ = 0.0f;
        ui::DialogContent content;
        if (kind == kItemRemoveHome)
        {
            content.icon = ui::StatusKind::warning;
            content.title = trf("Remove {0} from the home screen?", {title->name});
            content.body =
                trf("Its files stay where they are:\n{0}\nORION moves it to the Hidden tab.",
                    {title->path});
            content.buttons = {{tr("Cancel")}, {tr("Remove"), ui::ButtonKind::primary, false}};
        }
        else
        {
            content.icon = ui::StatusKind::danger;
            content.title = trf("Delete {0}?", {title->name});
            content.body =
                kind == kItemRemoveRom
                    ? trf("Its game file is erased for good:\n{0}\nThis cannot be undone.",
                          {title->path})
                    : trf("Its files are erased for good:\n{0}\nIt leaves the home screen "
                          "too.\nThis cannot be undone.",
                          {title->path});
            content.buttons = {{tr("Cancel")}, {delete_label(0.0f), ui::ButtonKind::primary, true}};
        }
        content.default_button = 0;
        dialog_.open(std::move(content), feedback);
    }

    static std::string delete_label(float held)
    {
        if (held <= 0.0f)
            return tr("Hold Cross to delete"); // the fonts have no ✕
        return trf("Keep holding… {0}%",
                   {std::to_string(static_cast<int>(held / kHoldToDelete * 100.0f))});
    }

    void update_dialog(const InputFrame &input, float dt, app::Feedback &feedback)
    {
        if (dialog_update_)
        {
            const ui::Event event = dialog_.handle(input, feedback);
            if (event == ui::Event::activated && dialog_.choice() == 1)
                install_update(feedback);
            if (!dialog_.is_open())
                dialog_update_ = false;
            return;
        }
        const bool hold = remove_kind_ != kItemRemoveHome && dialog_.focus() == 1;
        if (hold &&
            (input.is_pressed(Action::confirm) || (input.held & action_bit(Action::confirm)) != 0))
        {
            const float was = remove_hold_;
            remove_hold_ += dt;
            if (remove_hold_ >= kHoldToDelete)
            {
                dialog_.close(feedback);
                remove_hold_ = 0.0f;
                do_remove(feedback);
                return;
            }
            // The label counts up in tenths, so it changes as you hold.
            if (static_cast<int>(was * 10.0f) != static_cast<int>(remove_hold_ * 10.0f) ||
                was == 0.0f)
            {
                ui::DialogContent content = dialog_.content();
                content.buttons[1].label = delete_label(remove_hold_);
                dialog_.set_content(std::move(content));
            }
            return;
        }
        if (remove_hold_ > 0.0f)
        {
            // Let go too soon: nothing happens.
            remove_hold_ = 0.0f;
            ui::DialogContent content = dialog_.content();
            content.buttons[1].label = delete_label(0.0f);
            dialog_.set_content(std::move(content));
        }
        const ui::Event event = dialog_.handle(input, feedback);
        if (event == ui::Event::activated && dialog_.choice() == 1 &&
            remove_kind_ == kItemRemoveHome)
            do_remove(feedback);
    }

    void do_remove(app::Feedback &feedback)
    {
        const Title *title = find_title(remove_id_);
        if (title == nullptr)
            return;
        if (remove_kind_ == kItemRemoveRom)
        {
            // A game's file: ORION erases it itself, then looks again.
            struct stat info = {};
            if (::stat(title->path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) ||
                std::remove(title->path.c_str()) != 0)
            {
                show_note(false,
                          trf("Could not delete {0} ({1})", {title->path, std::strerror(errno)}));
                feedback.play(audio::Cue::error);
                return;
            }
            removed_ids_.insert(title->id);
            tabs_changed();
            show_note(true, trf("Deleted {0}", {title->name}));
            feedback.play(audio::Cue::erase);
            return;
        }
        // ShadowMountPlus touches no game while one is open, and ORION is
        // one: the launch helper does it once ORION has closed, then opens
        // ORION again (payload/launcher, "@remove"). How it went comes back
        // in remove-result.txt, read when ORION starts (take_remove_result).
        const std::string self = own_title_id();
        const std::string &self_path = context_.library.self_path;
        std::string why;
        if (!sys::on_console())
            why = tr("not on a console");
        else if (self.empty() || self_path.empty())
            why = tr("ORION does not know its own folder");
        else
        {
            const std::string request = self + "\n@remove " +
                                        (remove_kind_ == kItemRemoveHome ? "home" : "erase") +
                                        "\t" + title->id + "\t" + one_line(title->name) +
                                        "\n@remove-result " + self_path + "/remove-result.txt\n";
            std::remove((self_path + "/remove-result.txt").c_str());
            why = send_helper(request);
        }
        if (!why.empty())
        {
            show_note(false, why == tr("not on a console")
                                 ? tr("Removing works on the console only")
                                 : trf("Could not remove {0}: {1}", {title->name, why}));
            feedback.play(audio::Cue::error);
            return;
        }
        launch_ok_ = true;
        launch_message_ =
            remove_kind_ == kItemRemoveHome
                ? trf("Removing {0}: ORION closes now and opens again when it is done",
                      {title->name})
                : trf("Deleting {0}: ORION closes now and opens again when it is done",
                      {title->name});
        launch_shown_ = 6.0f;
        launch_exit_ = 1.6f; // a moment longer: there is more to read
        launch_pending_ = 30.0f;
        feedback.play(audio::Cue::select);
    }

    // What the launch helper did with a removal while ORION was closed: said
    // once, and the title taken off its tabs (a game left on the home screen
    // with its files waits on Hidden, or ORION would list it again).
    void take_remove_result()
    {
        const std::string &self_path = context_.library.self_path;
        std::string text;
        if (self_path.empty() || !save::read_file(self_path + "/remove-result.txt", &text, 4096) ||
            text.empty())
            return;
        std::remove((self_path + "/remove-result.txt").c_str());
        // "<home|erase> <0|1> <title ID>\t<message>"
        const std::size_t tab = text.find('\t');
        const std::string head = text.substr(0, tab);
        std::string message = tab == std::string::npos ? std::string() : text.substr(tab + 1);
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
            message.pop_back();
        const std::size_t first = head.find(' ');
        const std::size_t second = head.find(' ', first + 1);
        if (first == std::string::npos || second == std::string::npos)
            return;
        const std::string kind = head.substr(0, first);
        const bool ok = head.substr(first + 1, second - first - 1) == "1";
        const std::string id = head.substr(second + 1);
        const Title *title = find_title(id);
        const std::string name = title != nullptr ? title->name : id;
        if (!ok)
        {
            show_note(false, trf("Could not remove {0}: {1}", {name, message}));
            return;
        }
        if (kind == "home")
        {
            prefs_.set_title_hidden(id, true);
            save_prefs();
            show_note(true, trf("{0} left the home screen; it is on the Hidden tab", {name}));
        }
        else
        {
            removed_ids_.insert(id);
            show_note(true, trf("Deleted {0}", {name}));
        }
    }

    // What a game plays with, short: "Snes9x", "Porpoise".
    std::string player_label(const Title &title) const
    {
        const library::Player player = player_of(title);
        if (player.app == nullptr)
            return "";
        return player.core.empty() ? player.app->name : core_name(player.core);
    }

    // Play with…: Default (what it plays with when nothing is chosen), then
    // every emulator and core that can play it.
    // launch_after: the game is starting and has no emulator chosen yet; the
    // list is just the emulators, and the one picked is kept and started.
    void open_player_menu(app::Feedback &feedback, bool launch_after = false)
    {
        const Title *title = find_title(menu_title_);
        if (title == nullptr)
            return;
        launch_after_pick_ = launch_after;
        const std::string chosen = prefs_.game_player(title->id);
        std::vector<ui::MenuItem> items;
        if (launch_after)
        {
            const std::vector<library::Player> players =
                library::players_for(context_.library.emulator, *title);
            for (std::size_t i = 0; i < players.size() && i < 60; ++i)
            {
                ui::MenuItem item = menu_item(players[i].label);
                item.tag = kItemPlayer + 1 + static_cast<int>(i);
                items.push_back(item);
            }
            menu_.set_items(std::move(items));
            menu_.open(grid_.cell_rect(grid_.focus()), feedback);
            show_note(true,
                      trf("Which emulator for {0}? ORION remembers; Options, Play with… changes it",
                          {title->name}));
            return;
        }
        // The default, as it would be without a choice.
        std::string usual;
        {
            const library::EmulatorApp *app = context_.library.emulator.app_of(*title);
            if (app != nullptr)
                usual = app->key == "retroarch" ? "RetroArch · " + core_name(core_for(*title))
                                                : app->name;
        }
        ui::MenuItem standard = menu_item(trf("Default ({0})", {usual}));
        standard.tag = kItemPlayer;
        standard.shortcut = chosen.empty() ? tr("Now") : "";
        items.push_back(standard);
        ui::MenuItem line;
        line.separator = true;
        items.push_back(line);
        const std::vector<library::Player> players =
            library::players_for(context_.library.emulator, *title);
        for (std::size_t i = 0; i < players.size() && i < 60; ++i)
        {
            ui::MenuItem item = menu_item(players[i].label);
            item.tag = kItemPlayer + 1 + static_cast<int>(i);
            item.shortcut = players[i].key == chosen ? tr("Now") : "";
            items.push_back(item);
        }
        menu_.set_items(std::move(items));
        menu_.open(grid_.cell_rect(grid_.focus()), feedback);
    }

    void update_menu(const InputFrame &input, app::Feedback &feedback)
    {
        if (menu_.handle(input, feedback) != ui::Event::activated)
        {
            // Backed out of the emulator question: the game does not start.
            if (!menu_.is_open())
                launch_after_pick_ = false;
            return;
        }
        const int tag = menu_.items()[static_cast<std::size_t>(menu_.focus())].tag;
        if (menu_.is_open())
            menu_.close(feedback);

        if (tag == kItemLaunch)
        {
            launch(menu_title_, feedback);
            return;
        }
        if (tag == kItemRemove)
        {
            open_remove_menu(feedback);
            return;
        }
        if (tag == kItemRemoveHome || tag == kItemRemoveErase || tag == kItemRemoveRom)
        {
            ask_remove(tag, feedback);
            return;
        }
        if (tag == kItemClose)
        {
            context_.exit_requested = true;
            return;
        }
        if (tag == kItemFavourite)
        {
            toggle_favourite(menu_title_, feedback);
            return;
        }
        if (tag == kItemPlayWith)
        {
            open_player_menu(feedback);
            return;
        }
        if (tag >= kItemPlayer && tag < kItemPlayer + 100)
        {
            const Title *title = find_title(menu_title_);
            if (title == nullptr)
                return;
            const std::vector<library::Player> players =
                library::players_for(context_.library.emulator, *title);
            const int choice = tag - kItemPlayer - 1;
            prefs_.set_game_player(title->id,
                                   choice >= 0 && choice < static_cast<int>(players.size())
                                       ? players[static_cast<std::size_t>(choice)].key
                                       : std::string());
            tabs_changed();
            if (std::exchange(launch_after_pick_, false))
            {
                launch(title->id, feedback);
                return;
            }
            show_note(true, trf("{0} plays with {1}", {title->name, player_label(*title)}));
            feedback.play(audio::Cue::select);
            return;
        }
        if (tag == kItemRename)
        {
            const Title *title = find_title(menu_title_);
            if (title == nullptr)
                return;
            start_typing(Typing::rename_title, title->name);
            feedback.play(audio::Cue::modal_open);
            return;
        }
        if (tag == kItemHdArt)
        {
            const Title *title = find_title(menu_title_);
            if (title == nullptr)
                return;
            if (!sys::on_console())
            {
                show_note(false, tr("Pictures are downloaded on the console"));
                feedback.play(audio::Cue::error);
                return;
            }
            if (title->is_rom())
            {
                if (art_.running() ||
                    !art_.start({art_job(*title)}, context_.library.state_dir + "/art.txt"))
                {
                    show_note(false, tr("Box art is downloading; try again when it is done"));
                    feedback.play(audio::Cue::error);
                    return;
                }
                art_manual_ = true;
                show_note(true, trf("Looking for box art for {0}…", {title->name}));
            }
            else
            {
                const std::string folder = icon_folder();
                std::remove((folder + "/" + title->id + ".png").c_str());
                std::remove((folder + "/" + title->id + ".none").c_str());
                if (!start_icon_copies({title->id}))
                {
                    show_note(false, tr("Pictures are being copied; try again in a moment"));
                    feedback.play(audio::Cue::error);
                    return;
                }
                show_note(true, trf("Copying the home screen's picture of {0}…", {title->name}));
            }
            feedback.play(audio::Cue::select);
            return;
        }
        if (tag == kItemHide)
        {
            const bool hide = !prefs_.title_hidden(menu_title_);
            const Title *title = find_title(menu_title_);
            const std::string name = title != nullptr ? title->name : menu_title_;
            if (!prefs_.set_title_hidden(menu_title_, hide))
            {
                feedback.play(audio::Cue::error);
                return;
            }
            tabs_changed();
            show_note(true, hide ? trf("{0} is hidden: it is on the Hidden tab, at the end", {name})
                                 : trf("{0} is back on its shelves", {name}));
            feedback.play(audio::Cue::select);
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
            prefs_.place(menu_title_, prefs_.tabs[static_cast<std::size_t>(tag - kItemMove)].id);
        save_prefs();
        rebuild_tabs(active_key());
        rebuild();
        feedback.play(audio::Cue::place);
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
        // A game several emulators play, started for the first time: ask which.
        if (title->is_rom() && prefs_.ask_player && prefs_.game_player(title->id).empty() &&
            library::players_for(context_.library.emulator, *title).size() >= 2)
        {
            menu_title_ = title->id;
            open_player_menu(feedback, true);
            return;
        }
        const std::string &dir = context_.library.state_dir;
        launch_ok_ = false;
        std::string why;
        // A game for an emulator starts in the app that plays it, each the
        // way it takes a game (library/emulators.hpp).
        // The emulator and core the player chose for it (Play with…), or the default.
        const library::Player player = title->is_rom() ? player_of(*title) : library::Player();
        const library::EmulatorApp *app = player.app;
        const bool retroarch = app != nullptr && app->key == "retroarch";
        // RetroArch, as its own ES-DE does: -L <core> <game>.
        const std::string core = retroarch ? player.core : std::string();
        const std::string launcher = app != nullptr ? app->title_id : std::string();
        // Its launcher is told through args.txt and test-run.txt. ORION
        // stages them in its own folder; the helper, outside any sandbox,
        // puts them in RetroArch's and the originals back afterwards
        // (payload/launcher). Files ORION writes there itself the launcher
        // cannot read before it decides.
        const std::string root = app != nullptr ? app->root : std::string();
        const std::string args = root + "/args.txt";
        const std::string test_run = root + "/test-run.txt";
        const std::string staged_args = dir + "/retroarch-args.txt";
        const std::string staged_test_run = dir + "/retroarch-test-run.txt";
        const bool ps5sx2 = app != nullptr && app->key == "ps5sx2";
        const std::string staged_note = dir + "/ps5sx2-launch.txt";
        // Unique to this start: the helper tells its files from a later start's.
        const std::string token = std::to_string(std::time(nullptr)) + "-" +
                                  std::to_string(sys::monotonic_us() % 1000000);
        std::string request = title->id + "\n";
        if (retroarch)
            request = launcher + "\n@install " + staged_args + "\t" + args + "\n@install " +
                      staged_test_run + "\t" + test_run + "\n";
        else if (app != nullptr && (app->key == "porpoise" || app->key == "ecam" ||
                                    app->key == "psswanstation" || app->key == "eden"))
            // These start a game from their command line, as their own home-screen
            // tiles do, and close when it ends, so ORION comes back.
            request = launcher + "\n" +
                      library::porpoise_arguments(launcher, player.rom, prefs_.return_on_close);
        else if (ps5sx2)
            // The note PS5SX2's disc helper leaves it, written by the helper:
            // /data/PCSX2 is out of ORION's reach.
            request =
                launcher + "\n@install " + staged_note + "\t" + library::kPs5sx2LaunchNote + "\n";
        else if (app != nullptr)
            request = launcher + "\n"; // opens it; the game is picked there
        // Coming back: the helper waits for the game to close, then starts ORION.
        const std::string self = own_title_id();
        if (prefs_.return_on_close && !self.empty())
            request += "@return " + self + "\n@notify Close " + one_line(title->name) +
                       " from the PS button menu to return to ORION\n";
        // How long it is played goes in ORION's own folder, which the helper
        // can write to (orion/playtime.hpp).
        const std::string &self_path = context_.library.self_path;
        if (!self_path.empty())
            request += "@record " + title->id + "\t" + self_path + "/play-log.txt\n";
        if (title->is_rom() && app == nullptr)
            why = tr("the emulator for it is not installed (Settings, Emulators: Scan)");
        else if (retroarch && core.empty())
            why = trf("no core plays {0}", {library::short_system_name(title->system)});
        else if (!sys::on_console())
            why = tr("not on a console");
        else if (retroarch && player.rom.size() > library::kRetroArchArgMax)
            why = tr("its path is longer than RetroArch reads");
        else if (title->is_rom() && player.rom.size() > kMaxRequestPath)
            why = tr("its path is too long");
        else if (retroarch &&
                 !write_shared(staged_args, library::retroarch_args(core, player.rom, token)))
            why = trf("could not write {0}", {staged_args});
        else if (retroarch && !write_shared(staged_test_run, "ORION " + token + "\n"))
            why = trf("could not write {0}", {staged_test_run});
        else if (ps5sx2 && !write_shared(staged_note, library::ps5sx2_launch_note(player.rom)))
            why = trf("could not write {0}", {staged_note});
        else
        {
            why = send_helper(request);
            launch_ok_ = why.empty();
        }

        if (launch_ok_)
        {
            log_launch(*title, "sent the helper to 127.0.0.1:" + std::to_string(kLoaderPort) +
                                   "; closing ORION");
            launch_message_ = app != nullptr && !app->starts_games
                                  ? trf("Opening {0}: choose {1} there", {app->name, title->name})
                                  : trf("Starting {0}…", {title->name});
            // Long enough to read the card, then ORION gets out of the way.
            launch_exit_ = 0.9f;
            launch_pending_ = 30.0f;
        }
        else
        {
            log_launch(*title, "not launched: " + why);
            std::remove((dir + "/launch-request.txt").c_str());
            launch_message_ = why == tr("not on a console")
                                  ? tr("Launching works on the console only")
                                  : trf("Could not start {0}: {1}", {title->name, why});
        }
        launch_shown_ = 6.0f;
        feedback.play(launch_ok_ ? audio::Cue::launch : audio::Cue::error);
    }

    // Hands a request to the launch helper: it goes into
    // <state_dir>/launch-request.txt and the helper to the console's ELF
    // loader. Empty when that worked, else why not ("not on a console").
    std::string send_helper(const std::string &request) const
    {
        const std::string &dir = context_.library.state_dir;
        std::string elf;
        if (dir.empty() || !save::write_atomic(dir + "/launch-request.txt", request).empty())
            return tr("could not write the launch request");
        if (!save::read_file(kLauncherPath, &elf, 4u << 20) || elf.empty())
            return trf("the launch helper is missing ({0})", {kLauncherPath});
        const int error = sys::send_payload(elf, kLoaderPort);
        if (error == 0)
            return "";
        std::remove((dir + "/launch-request.txt").c_str());
        if (error == sys::kLaunchUnsupported)
            return tr("not on a console");
        return trf("the ELF loader did not answer (errno {0})", {std::to_string(error)});
    }

    // What the box art downloader needs for one game: where it goes, and
    // what it is found by (its file, its name, its GameTDB ID).
    orion::ArtJob art_job(const Title &title) const
    {
        const std::size_t slash = title.rom.rfind('/');
        const std::string file =
            slash == std::string::npos ? title.rom : title.rom.substr(slash + 1);
        const std::size_t dot = file.rfind('.');
        const std::string stem = dot == std::string::npos || dot == 0 ? file : file.substr(0, dot);
        orion::ArtJob job;
        job.id = title.id;
        job.system = title.system;
        job.rom_file = file;
        job.name = title.name;
        job.serial = title.serial;
        // A Wii U game is its folder's title.tmd: the folder names it.
        const std::string own =
            stem == "title" && slash != std::string::npos ? folder_name(title.rom) : stem;
        job.target = orion::art_folder(prefs_, context_.library.state_dir) + "/" + title.system +
                     "/" + own + ".png";
        return job;
    }

    static std::string folder_name(const std::string &file)
    {
        const std::size_t slash = file.rfind('/');
        const std::size_t before = slash == std::string::npos || slash == 0
                                       ? std::string::npos
                                       : file.rfind('/', slash - 1);
        return before == std::string::npos ? std::string("title")
                                           : file.substr(before + 1, slash - before - 1);
    }

    // Games for an emulator that have no box art yet. Without retry, games a
    // run already found nothing for are left alone (art/tried.txt).
    std::vector<orion::ArtJob> art_jobs(bool retry) const
    {
        const std::string &state = context_.library.state_dir;
        std::vector<orion::ArtJob> jobs;
        if (state.empty())
            return jobs;
        std::string tried;
        if (!retry)
            save::read_file(state + "/art/tried.txt", &tried, 1u << 20);
        for (const Title &title : titles())
        {
            if (!title.is_rom() || !title.icon.empty() ||
                !prefs_.system_enabled(library::shelf_system(title.system)) ||
                tried.find(title.id + "\n") != std::string::npos)
                continue;
            jobs.push_back(art_job(title));
        }
        return jobs;
    }

    void start_art(bool manual)
    {
        if (art_.running())
            return;
        std::vector<orion::ArtJob> jobs = art_jobs(manual);
        const std::size_t count = jobs.size();
        if (count == 0)
        {
            if (manual)
                show_note(true, tr("Every game already has its box art"));
            return;
        }
        const std::string &state = context_.library.state_dir;
        sys::art_trace(("starting the art thread for " + std::to_string(count) + " games").c_str());
        if (!art_.start(std::move(jobs), state + "/art.txt"))
            return;
        sys::art_trace("art thread started");
        art_manual_ = manual;
        const std::string games =
            i18n::plural(static_cast<long long>(count), "{0} game", "{0} games");
        settings_.set_art_status(trf("Looking for box art for {0}…", {games}));
        if (manual)
            show_note(true, trf("Looking for box art for {0}…", {games}));
    }

    // Games' box art is uploaded only once it is about to be seen
    // (library/icon.hpp): the rows around the focus, a little ahead of a
    // held direction. Each picture is asked for once.
    void want_icons()
    {
        const int focus = focus_index();
        const int first = std::max(0, focus - focus % kColumns - 2 * kColumns);
        const int last = std::min(static_cast<int>(view_.size()), focus + 5 * kColumns);
        bool asked = false;
        const auto want = [&](int index)
        {
            const Title &title = titles()[static_cast<std::size_t>(index)];
            if (!title.is_rom() || title.texture != 0 || title.icon.empty())
                return;
            std::string &wanted = wanted_icons_[title.id];
            if (wanted == title.icon)
                return;
            wanted = title.icon;
            context_.wanted_icons.push_back(title.id);
            asked = true;
        };
        for (int i = first; i < last; ++i)
            want(view_[static_cast<std::size_t>(i)]);
        // The Continue playing row is in view whenever it shows.
        if (continue_shown())
        {
            for (int index : continue_view_)
                want(index);
        }
        if (asked)
            art_sweep_ = std::max(art_sweep_, 2);
    }

    // Pictures found reach the shell, which uploads them; the covers pick the
    // textures up a frame later. A finished run is reported once.
    void poll_art(app::Feedback &feedback)
    {
        std::vector<orion::ArtFound> found = art_.take_found();
        for (orion::ArtFound &picture : found)
            context_.new_icons.emplace_back(std::move(picture.id), std::move(picture.path));
        if (!found.empty())
            art_sweep_ = 2;
        if (art_sweep_ > 0 && --art_sweep_ == 0)
            refresh_covers();
        if (art_.running())
        {
            const orion::ArtFetcher::Progress now = art_.progress();
            settings_.set_art_status(trf("Found {0} of {1} so far…",
                                         {std::to_string(now.found), std::to_string(now.total)}));
        }
        if (!art_.take_finished())
            return;
        const orion::ArtFetcher::Progress done = art_.progress();
        remember_tried(art_.take_missing());
        std::string summary =
            trf("Found {0} of {1}", {std::to_string(done.found), std::to_string(done.total)});
        if (done.missing > 0)
            summary += trf(", none for {0}", {std::to_string(done.missing)});
        if (done.failed > 0)
            summary += trf(", {0} failed ({1})", {std::to_string(done.failed), done.error});
        settings_.set_art_status(trf("Last run: {0}", {summary}));
        if (done.found == 0 && done.failed > 0)
            show_note(false, trf("Could not get box art: {0}", {done.error}));
        else if (art_manual_ || done.found > 0)
            show_note(true, i18n::plural(done.found, "Found box art for {0} game",
                                         "Found box art for {0} games"));
        else
            return;
        feedback.play(done.found > 0 ? audio::Cue::saved : audio::Cue::error);
    }

    void remember_tried(const std::vector<std::string> &ids) const
    {
        const std::string &state = context_.library.state_dir;
        if (ids.empty() || state.empty() || !orion::make_directories(state + "/art"))
            return;
        std::string tried;
        save::read_file(state + "/art/tried.txt", &tried, 1u << 20);
        for (const std::string &id : ids)
        {
            if (tried.find(id + "\n") == std::string::npos)
                tried += id + "\n";
        }
        save::write_atomic(state + "/art/tried.txt", tried);
    }

    // A new box art folder: saved, and searched now for the games that have
    // no picture yet (the ones that have keep theirs until ORION restarts).
    void art_folder_changed()
    {
        // What the old folder holds comes along to the new one.
        const std::string now = orion::art_folder(prefs_, context_.library.state_dir);
        const int moved = orion::copy_tree(art_folder_was_, now);
        art_folder_was_ = now;
        if (moved > 0)
            sys::log("[ORION] box art: %d pictures copied to %s", moved, now.c_str());
        save_prefs();
        if (settings_.is_open())
            settings_.refresh(look_, prefs_);
        std::vector<Title> roms;
        for (const Title &title : titles())
        {
            if (title.is_rom() && title.icon.empty())
                roms.push_back(title);
        }
        const library::EmulatorApp *retroarch = context_.library.emulator.app("retroarch");
        library::attach_rom_art(roms, retroarch != nullptr ? retroarch->root : std::string(),
                                orion::art_folder(prefs_, context_.library.state_dir));
        int found = 0;
        for (const Title &rom : roms)
        {
            if (rom.icon.empty())
                continue;
            context_.new_icons.emplace_back(rom.id, rom.icon);
            ++found;
        }
        if (found > 0)
            art_sweep_ = 2;
        show_note(true, found > 0 ? trf("Box art folder: {0}  ({1} found)",
                                        {orion::art_folder(prefs_, context_.library.state_dir),
                                         std::to_string(found)})
                                  : trf("Box art folder: {0}",
                                        {orion::art_folder(prefs_, context_.library.state_dir)}));
    }

    // The covers on screen take the textures the shell has uploaded since.
    void refresh_covers()
    {
        for (int i = 0; i < grid_.count(); ++i)
        {
            ui::CardItem &item = grid_.item(i);
            const Title &title = titles()[static_cast<std::size_t>(item.tag)];
            if (item.texture == title.texture)
                continue;
            item.texture = title.texture;
            if (title.icon_width > 0 && title.icon_height > 0)
                item.image_aspect =
                    static_cast<float>(title.icon_width) / static_cast<float>(title.icon_height);
        }
        for (std::size_t i = 0; i < continue_view_.size(); ++i)
        {
            ui::CardItem &item = continue_.item(static_cast<int>(i));
            const Title &title = titles()[static_cast<std::size_t>(item.tag)];
            if (item.texture == title.texture)
                continue;
            item.texture = title.texture;
            if (title.icon_width > 0 && title.icon_height > 0)
                item.image_aspect =
                    static_cast<float>(title.icon_width) / static_cast<float>(title.icon_height);
            item.bare = std::fabs(std::log(std::max(0.2f, item.image_aspect))) >= 0.18f;
        }
        const int columns = grid_.style.columns;
        apply_cover_shape();
        if (grid_.style.columns != columns)
            rebuild();
        else
            mark_bare_covers();
    }

    // The card above the hints, for a note that is not a launch.
    // Asks GitHub whether a newer ORION is out: by itself once a launch, a
    // few seconds in (the library first), or when Settings says Check now.
    static std::unique_ptr<orion::UpdateCheck> new_update_check()
    {
        return std::make_unique<orion::UpdateCheck>(
            [](const std::string &url, std::size_t limit, std::string *body, std::string *error)
            { return sys::http_get(url, limit, body, error); });
    }

    // The update log (orion/updates.hpp): every step, kept across starts.
    std::string update_log_path() const
    {
        const std::string &state = context_.library.state_dir;
        if (state.empty())
            return {};
        save::ensure_directory(state + "/update");
        return state + "/update/log.txt";
    }

    void check_for_updates(bool asked)
    {
        if (context_.version.empty() || !sys::on_console())
        {
            settings_.set_update_status(
                trf("ORION {0}: checking works on the console only",
                    {context_.version.empty() ? tr("(no version)") : context_.version}));
            settings_.refresh(look_, prefs_);
            return;
        }
        const std::string log = update_log_path();
        if (updates_->state() == orion::UpdateCheck::State::checking)
        {
            const int waited = static_cast<int>(clock_ - check_started_);
            if (waited < 20)
            {
                settings_.set_update_status(trf("ORION {0}: still checking ({1} s)",
                                                {context_.version, std::to_string(waited)}));
                settings_.refresh(look_, prefs_);
                return;
            }
            // No answer in 20 s: the request hangs. Its thread cannot be
            // stopped; the check is left to it, and a new one starts.
            orion::update_log(log, "check: no answer after " + std::to_string(waited) +
                                       " s; starting a new one");
            (void)updates_.release();
            updates_ = new_update_check();
        }
        updates_->set_log(log);
        if (!updates_->start(context_.version))
            return;
        check_started_ = clock_;
        manual_update_check_ = asked;
        settings_.set_update_status(trf("ORION {0}: checking…", {context_.version}));
        settings_.refresh(look_, prefs_);
    }

    void update_updates()
    {
        if (!update_asked_ && prefs_.check_updates && age_ > 4.0f)
        {
            update_asked_ = true;
            check_for_updates(false);
        }
        if (!updates_->take_finished())
            return;
        const orion::Release latest = updates_->latest();
        std::string status;
        switch (updates_->state())
        {
        case orion::UpdateCheck::State::available:
            status = trf("ORION {0}. ORION {1} is out", {context_.version, latest.version});
            if (!manual_update_check_ && failed_update().first == latest.version)
            {
                status += trf(". It could not be installed ({0}): Check now tries again",
                              {failed_update().second});
                orion::update_log(update_log_path(),
                                  "orion: " + latest.version + " failed before; not by itself");
            }
            else if (prefs_.auto_update || manual_update_check_)
            {
                orion::update_log(update_log_path(), "orion: downloading " + latest.version);
                download_update(latest);
                status += tr(": downloading it…");
            }
            else
                show_note(true,
                          trf("ORION {0} is out: Settings, Library, Check now", {latest.version}));
            break;
        case orion::UpdateCheck::State::up_to_date:
            status = trf("ORION {0} is the newest", {context_.version});
            if (manual_update_check_)
                show_note(true, status);
            break;
        default:
            status = trf("ORION {0}. Could not check: {1}", {context_.version, updates_->error()});
            if (manual_update_check_)
                show_note(false, trf("Could not check for updates: {0}", {updates_->error()}));
            break;
        }
        settings_.set_update_status(std::move(status));
        settings_.refresh(look_, prefs_);
    }

    // The release, downloaded, checked and unpacked beside ORION on a thread
    // of its own (orion/updates.hpp); the player is asked once it is ready.
    void download_update(const orion::Release &release)
    {
        const std::string &state = context_.library.state_dir;
        if (state.empty() || own_title_id().empty())
            return;
        installer_.set_log(update_log_path());
        if (installer_.resume(release, state + "/update"))
            return; // downloaded before, never installed: asked again
        if (!installer_.start(release, own_title_id(), state + "/update"))
        {
            orion::update_log(update_log_path(), "orion: a download is already running");
            return;
        }
        // It is big: say so, so nobody closes ORION halfway; a system
        // notification too, which shows over Settings and anything else.
        const std::string note =
            release.zip_size > 0
                ? trf("Downloading ORION {0} ({1}). Keep ORION open; it asks before it installs.",
                      {release.version, megabytes(release.zip_size)})
                : trf("Downloading ORION {0}. Keep ORION open; it asks before it installs.",
                      {release.version});
        show_note(true, note);
        sys::notify(note);
    }

    void update_installer()
    {
        if (!installer_.take_finished())
            return;
        const orion::Release release = installer_.release();
        if (installer_.state() == orion::UpdateInstaller::State::ready)
        {
            settings_.set_update_ready(release.version);
            settings_.set_update_status(trf("ORION {0}. ORION {1} is ready to install",
                                            {context_.version, release.version}));
            settings_.refresh(look_, prefs_);
            update_waiting_ = true; // asked as soon as nothing else is open
            sys::notify(trf("ORION {0} is ready to install", {release.version}));
            return;
        }
        settings_.set_update_status(trf("ORION {0}. The update could not be made ready: {1}",
                                        {context_.version, installer_.error()}));
        settings_.refresh(look_, prefs_);
        show_note(false, trf("ORION {0} could not be downloaded: {1}",
                             {release.version, installer_.error()}));
    }

    // Asked once, over the library: now, or later from Settings.
    void ask_update(app::Feedback &feedback)
    {
        update_waiting_ = false;
        const orion::Release release = installer_.release();
        ui::DialogContent content;
        content.icon = ui::StatusKind::info;
        content.title = trf("ORION {0} is ready", {release.version});
        content.body = trf("You have ORION {0}. ORION closes, updates itself and opens "
                           "again;\nyour settings, favourites and box art stay.",
                           {context_.version});
        content.buttons = {{tr("Later")}, {tr("Update now"), ui::ButtonKind::primary, false}};
        content.default_button = 1;
        dialog_update_ = true;
        dialog_.open(std::move(content), feedback);
    }

    // ORION cannot replace its own files while it runs: the launch helper
    // copies the staged ones over them once ORION has closed, then opens it
    // again (payload/launcher, "@update"). How it went is read at the next
    // start (take_update_result).
    void install_update(app::Feedback &feedback)
    {
        const std::string self = own_title_id();
        const std::string &self_path = context_.library.self_path;
        const std::string update = context_.library.state_dir + "/update";
        std::string why;
        if (!sys::on_console())
            why = tr("not on a console");
        else if (installer_.state() != orion::UpdateInstaller::State::ready)
            why = tr("no update is ready");
        else if (self.empty() || self_path.empty())
            why = tr("ORION does not know its own folder");
        else
        {
            std::remove((self_path + "/update-result.txt").c_str());
            // The helper keeps the old files here; it may not make folders in
            // ORION's data folder, so the folder is made for it, open to it.
            save::ensure_directory(update + "/previous");
            ::chmod((update + "/previous").c_str(), 0777);
            orion::update_log(update_log_path(), "orion: handing the install to the helper");
            why = send_helper(self + "\n@update " + update + "/stage\t" + self_path + "\n");
        }
        if (!why.empty())
        {
            show_note(false, why == tr("not on a console")
                                 ? tr("Updating works on the console only")
                                 : trf("Could not update ORION: {0}", {why}));
            feedback.play(audio::Cue::error);
            return;
        }
        launch_ok_ = true;
        launch_message_ = trf("Updating to ORION {0}: ORION opens again when done",
                              {installer_.release().version});
        launch_shown_ = 6.0f;
        launch_exit_ = 1.6f;
        launch_pending_ = 30.0f;
        feedback.play(audio::Cue::select);
    }

    // What the helper did with an update while ORION was closed, said once.
    void take_update_result()
    {
        const std::string update = context_.library.state_dir + "/update";
        const std::string result = context_.library.self_path + "/update-result.txt";
        std::string text;
        if (context_.library.state_dir.empty() || context_.library.self_path.empty() ||
            !save::read_file(result, &text, 4096) || text.empty())
            return;
        std::remove(result.c_str());
        std::remove((update + "/stage/files.txt").c_str()); // the stage is spent
        // "1 <version>" or "0 <version>\t<why>"
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.pop_back();
        const bool ok = text.rfind("1 ", 0) == 0;
        const std::size_t tab = text.find('\t');
        const std::string version =
            text.size() > 2 ? text.substr(2, tab == std::string::npos ? std::string::npos : tab - 2)
                            : std::string();
        const std::string why = tab == std::string::npos ? std::string() : text.substr(tab + 1);
        orion::update_log(update_log_path(),
                          ok ? "helper: installed " + version
                             : "helper: could not install " + version + ": " + why);
        if (ok)
        {
            show_note(true, trf("ORION is updated to {0}", {version}));
            std::remove((update + "/failed.txt").c_str());
            tidy_update_folder();
            return;
        }
        show_note(false, trf("ORION could not update to {0}: {1}", {version, why}));
        // Not fetched again by itself, or it would ask on every start: Check
        // now (Settings, About) tries again.
        save::write_atomic(update + "/failed.txt", version + "\t" + why + "\n");
    }

    // A staged update this ORION is already as new as is spent: tens of MB,
    // removed. "previous" (the files the last update replaced) stays, as
    // the way back; the helper wrote it, and the next update overwrites it.
    void tidy_update_folder()
    {
        const std::string &state = context_.library.state_dir;
        if (state.empty() || context_.version.empty())
            return;
        const std::string stage = state + "/update/stage";
        std::string staged;
        struct stat info = {};
        if (::stat(stage.c_str(), &info) != 0)
            return;
        save::read_file(stage + "/version.txt", &staged, 64);
        while (!staged.empty() && (staged.back() == '\n' || staged.back() == '\r'))
            staged.pop_back();
        if (!staged.empty() && orion::compare_versions(staged, context_.version) > 0)
            return; // newer than this ORION: still to be installed
        const bool gone = save::remove_tree(stage);
        std::remove((state + "/update/" + own_title_id() + ".zip").c_str());
        orion::update_log(update_log_path(), gone ? "orion: removed the spent update files"
                                                  : "orion: could not remove " + stage);
    }

    // The version whose install failed, and why; empty when none did.
    std::pair<std::string, std::string> failed_update() const
    {
        std::string text;
        if (!save::read_file(context_.library.state_dir + "/update/failed.txt", &text, 4096))
            return {};
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.pop_back();
        const std::size_t tab = text.find('\t');
        return {text.substr(0, tab), tab == std::string::npos ? "" : text.substr(tab + 1)};
    }

    void show_note(bool ok, std::string message)
    {
        launch_ok_ = ok;
        launch_message_ = std::move(message);
        launch_shown_ = 5.0f;
    }

    // A file another title reads: written, then readable by every app. ORION's
    // sandbox creates files for itself alone, and RetroArch's launcher, which
    // decides what to do before it repairs permissions in its folder, then
    // reads its defaults instead.
    static bool write_shared(const std::string &file, std::string_view bytes)
    {
        return save::write_atomic(file, bytes).empty() && ::chmod(file.c_str(), 0666) == 0;
    }

    // The focused game's home-screen picture, once the focus has rested on it
    // a moment (as the home screen does); the shell loads it (app::Context).
    // What the focused game shows behind the library: "file:<png>" (its
    // home-screen picture, from its folder or the console's copy in
    // the copy of the system's one), else "icon:<id>" (its cover, blurred), else nothing.
    std::string backdrop_of(const Title &title) const
    {
        if (!prefs_.game_backgrounds)
            return "";
        const auto is_file = [](const std::string &file)
        {
            struct stat info = {};
            return ::stat(file.c_str(), &info) == 0 && S_ISREG(info.st_mode);
        };
        if (!title.is_rom())
        {
            std::vector<std::string> places;
            if (title.source == library::Source::folder)
            {
                places.push_back(title.path + "/sce_sys/pic0.png");
                places.push_back(title.path + "/sce_sys/pic1.png");
            }
            // The system keeps a copy of every registered title's pictures,
            // disc images included, out of ORION's sight; request_backdrops
            // fetches it through the console's FTP server.
            places.push_back(backdrop_folder() + "/" + title.id + ".png");
            for (const std::string &place : places)
            {
                if (is_file(place))
                    return "file:" + place;
            }
        }
        return prefs_.blurred_covers && title.texture != 0 ? "icon:" + title.id : std::string();
    }

    // Asks the shell for a picture once the focus has rested on a cover a
    // moment (as the home screen does), and cross-fades through the sky: the
    // old one fades out before the new one fades in.
    void follow_backdrop(float dt)
    {
        constexpr float kDwell = 0.45f; // seconds on a cover before its picture is asked for
        const std::string wanted =
            view_.empty() || typing_ == Typing::search ? std::string() : backdrop_of(focused());
        if (wanted != backdrop_candidate_)
        {
            backdrop_candidate_ = wanted;
            backdrop_dwell_ = 0.0f;
        }
        backdrop_dwell_ += dt;
        const bool rested = backdrop_dwell_ >= kDwell;
        const bool file = backdrop_candidate_.rfind("file:", 0) == 0;
        if (rested && file)
            context_.backdrop_path = backdrop_candidate_.substr(5);
        const bool ready = rested && !backdrop_candidate_.empty() &&
                           (!file || (context_.backdrop_shown == backdrop_candidate_.substr(5) &&
                                      context_.backdrop_texture != 0));
        // Swap what is drawn only once the old one has gone.
        if (backdrop_drawn_ != backdrop_candidate_ && backdrop_alpha_ <= 0.0f && ready)
            backdrop_drawn_ = backdrop_candidate_;
        const bool in = ready && backdrop_drawn_ == backdrop_candidate_;
        const float speed = context_.settings.reduced_motion ? 10.0f : 2.2f;
        backdrop_alpha_ = std::clamp(backdrop_alpha_ + (in ? speed : -speed) * dt, 0.0f, 1.0f);
    }

    // The part of a picture of this aspect that fills the screen.
    static Rect fill_uv(float picture)
    {
        const float screen = gfx::kVirtualWidth / gfx::kVirtualHeight;
        Rect uv = gfx::kFullUv;
        picture = std::max(0.2f, picture);
        if (picture > screen)
        {
            const float keep = screen / picture;
            uv.x += (1.0f - keep) * 0.5f;
            uv.w = keep;
        }
        else
        {
            const float keep = picture / screen;
            uv.y += (1.0f - keep) * 0.5f;
            uv.h = keep;
        }
        return uv;
    }

    // The picture fills the screen (cropped to it), under a veil of the page
    // colour that deepens towards the covers, so text stays readable. A cover
    // icon is a few hundred pixels: stretched over the screen and drawn again
    // at small offsets around itself it becomes a soft blur of its colours.
    void draw_backdrop(gfx::DrawList &scene) const
    {
        if (backdrop_alpha_ <= 0.0f || backdrop_drawn_.empty())
            return;
        // Below full strength the sky and its clouds show through.
        const float a = tween::smoothstep(backdrop_alpha_) * prefs_.background_strength;
        const Rect screen = {0.0f, 0.0f, gfx::kVirtualWidth, gfx::kVirtualHeight};
        const Color page = theme_.page;
        if (backdrop_drawn_.rfind("file:", 0) == 0)
        {
            if (context_.backdrop_texture == 0)
                return;
            scene.image(context_.backdrop_texture, screen, fill_uv(context_.backdrop_aspect),
                        Color::rgb(0xffffff, a), 0.0f);
            scene.gradient_rect(screen, 0.0f, page.with_alpha(0.55f * a),
                                page.with_alpha(0.9f * a));
            return;
        }
        const Title *title = find_title(backdrop_drawn_.substr(5));
        if (title == nullptr || title->texture == 0)
            return;
        const float aspect =
            title->icon_width > 0 && title->icon_height > 0
                ? static_cast<float>(title->icon_width) / static_cast<float>(title->icon_height)
                : 1.0f;
        const Rect uv = fill_uv(aspect);
        // Grown a little, so the offset copies still cover the edges.
        const Rect big = {-60.0f, -60.0f, screen.w + 120.0f, screen.h + 120.0f};
        scene.image(title->texture, big, uv, Color::rgb(0xffffff, 0.55f * a), 0.0f);
        constexpr int kTaps = 8;
        for (int i = 0; i < kTaps; ++i)
        {
            const float angle = 6.2831853f * static_cast<float>(i) / kTaps;
            const Rect moved = {big.x + std::cos(angle) * 44.0f, big.y + std::sin(angle) * 44.0f,
                                big.w, big.h};
            scene.image(title->texture, moved, uv, Color::rgb(0xffffff, 0.16f * a), 0.0f);
        }
        scene.gradient_rect(screen, 0.0f, page.with_alpha(0.62f * a), page.with_alpha(0.93f * a));
    }

    // The play records: what the launch helper wrote in ORION's folder
    // (/app0/play-log.txt), and, for the PC preview, one in the state folder.
    static orion::PlayLog load_plays(const std::string &state_dir)
    {
        std::string text;
        std::string more;
        save::read_file("/app0/play-log.txt", &text, 4u << 20);
        if (!state_dir.empty() && save::read_file(state_dir + "/play-log.txt", &more, 4u << 20))
            text += "\n" + more;
        return orion::read_play_log(text);
    }

    const orion::PlayRecord &played(const Title &title) const
    {
        static const orion::PlayRecord kNever;
        const auto at = plays_.find(title.id);
        return at == plays_.end() ? kNever : at->second;
    }

    const Title *find_title(const std::string &id) const
    {
        for (const Title &title : titles())
        {
            if (!id.empty() && title.id == id)
                return &title;
        }
        return nullptr;
    }

    // ORION's own title ID, from its param.json; empty off the console.
    static std::string own_title_id()
    {
        static const std::string id = []
        {
            std::string text;
            json::Value root;
            if (!save::read_file("/app0/sce_sys/param.json", &text, 1u << 20) ||
                !json::parse(text, &root))
                return std::string();
            return root["titleId"].string_or("");
        }();
        return id;
    }

    // A name for one line of the launch request.
    static std::string one_line(std::string text)
    {
        for (char &c : text)
        {
            if (c == '\n' || c == '\r' || c == '\t')
                c = ' ';
        }
        return text;
    }

    // The undoing of what the launch helper does for a game of RetroArch's,
    // for when it could not: the original is <file>.orion-backup, or there
    // was none (<file>.orion-absent).
    static void put_back(const std::string &file)
    {
        const std::string backup = file + ".orion-backup";
        const std::string absent = file + ".orion-absent";
        std::string bytes;
        if (save::read_file(backup, &bytes, 1u << 20))
        {
            if (write_shared(file, bytes))
                std::remove(backup.c_str());
        }
        else if (save::read_file(absent, &bytes, 1u << 20))
        {
            std::remove(file.c_str());
            std::remove(absent.c_str());
        }
    }

    // The core a game for an emulator runs with: the player's choice for
    // its system, else the first core that plays it.
    std::string core_for(const Title &title) const
    {
        const std::string shelf = library::shelf_system(title.system);
        const std::string chosen = prefs_.system_core(shelf);
        for (const library::System &system : context_.library.emulator.systems)
        {
            if (system.name != shelf)
                continue;
            if (!chosen.empty() &&
                std::find(system.cores.begin(), system.cores.end(), chosen) != system.cores.end())
                return chosen;
            return system.cores.empty() ? std::string() : system.cores.front();
        }
        return chosen;
    }

    // How a game for an emulator is played: the player's choice for it (Play
    // with…), else the emulator that found it, RetroArch with the system's core.
    library::Player player_of(const Title &title) const
    {
        const std::string chosen = prefs_.game_player(title.id);
        if (!chosen.empty())
        {
            for (library::Player &player : library::players_for(context_.library.emulator, title))
            {
                if (player.key == chosen)
                    return player;
            }
        }
        library::Player player;
        player.app = context_.library.emulator.app_of(title);
        player.rom = title.rom;
        if (player.app != nullptr && player.app->key == "retroarch")
        {
            player.core = core_for(title);
            player.key = "retroarch:" + player.core;
        }
        else if (player.app != nullptr)
            player.key = player.app->key;
        return player;
    }

    // The app a game for an emulator starts in, by name.
    std::string emulator_name(const Title &title) const
    {
        const library::Player player = player_of(title);
        return player.app != nullptr ? player.app->name : tr("not installed");
    }

    std::string core_name(const std::string &file) const
    {
        for (const library::Core &core : context_.library.emulator.cores)
        {
            if (core.file == file)
                return core.name;
        }
        return file.empty() ? tr("no core") : file;
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
        const std::string name = name_.empty() ? tr("My theme") : name_;
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
        file.description = file.family.empty() ? tr("Made in ORION from a theme")
                                               : trf("Made in ORION from {0}", {file.family});
        std::string error;
        if (themes_.save_user_theme(std::move(file), &error))
        {
            look_.base = id;
            orion::save_look(context_.library.state_dir, look_);
            restyle();
            launch_ok_ = true;
            launch_message_ = trf("Saved the theme “{0}”", {name});
            feedback.play(audio::Cue::saved);
        }
        else
        {
            launch_ok_ = false;
            launch_message_ = trf("Could not save the theme: {0}", {error});
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

    // The title the buttons act on: the Continue playing row's while it has
    // the focus, else the grid's.
    const Title &focused() const
    {
        if (continue_focus_ && continue_shown())
            return titles()[static_cast<std::size_t>(
                continue_view_[static_cast<std::size_t>(continue_.focus())])];
        return grid_focused();
    }

    const Title &grid_focused() const
    {
        return titles()[static_cast<std::size_t>(view_[focus_index()])];
    }

    // The row of what was played last, over the All tab's covers.
    bool continue_shown() const
    {
        // Cases want the whole height: the row stays away there.
        return !continue_view_.empty() && bar_key() == "all" && query_.empty() &&
               prefs_.continue_row && view() != View::cases;
    }

    bool favourite_of(const std::string &id) const
    {
        return std::find(favourites_.begin(), favourites_.end(), id) != favourites_.end();
    }

    // The tab whose covers show: on the Emulators tab, the system picked
    // under it, so each system keeps its own theme, sort and covers.
    std::string active_key() const
    {
        const std::string bar = bar_key();
        const int sub = subtabs_.active();
        if (bar == kEmulatorsTab && sub >= 0 && sub < static_cast<int>(sub_keys_.size()))
            return sub_keys_[static_cast<std::size_t>(sub)];
        return bar;
    }

    // The tab active in the tab bar itself.
    std::string bar_key() const
    {
        const int active = tabs_.active();
        return active >= 0 && active < static_cast<int>(tab_keys_.size())
                   ? tab_keys_[static_cast<std::size_t>(active)]
                   : std::string("all");
    }

    bool on_emulators_tab() const
    {
        return bar_key() == kEmulatorsTab;
    }

    bool on_tab(const Title &title, const std::string &key) const
    {
        // A title removed since the scan is on no tab.
        if (removed_ids_.count(title.id) != 0)
            return false;
        // A hidden title is on the Hidden tab and nowhere else.
        if (prefs_.title_hidden(title.id))
            return key == kHiddenTab;
        if (key == kHiddenTab)
            return false;
        // Games for an emulator live on their system's tab (and in
        // Favourites and the player's tabs), never on the console's shelves:
        // a few hundred ROMs would bury everything else.
        if (title.is_rom())
        {
            const std::string &shelf = library::shelf_of(title.system);
            if (!prefs_.system_enabled(shelf))
                return false;
            if (key == kEmulatorsTab)
                return true;
            if (key == "favourites")
                return favourite_of(title.id);
            if (key.rfind("sys:", 0) == 0)
                return key.compare(4, std::string::npos, shelf) == 0;
            if (key.rfind("tab:", 0) == 0)
            {
                const orion::CustomTab *placed = prefs_.placement(title.id);
                return placed != nullptr && key.compare(4, std::string::npos, placed->id) == 0;
            }
            return false;
        }
        if (key.rfind("sys:", 0) == 0)
            return false;
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
        {
            if (!prefs_.tab_hidden(shelf.key))
                add(shelf.key, tr(shelf.name));
        }
        // The systems: a tab each, or one Emulators tab with a row of them.
        sub_keys_.clear();
        std::vector<ui::TabItem> systems;
        for (const library::System &system : context_.library.emulator.systems)
        {
            if (!prefs_.system_enabled(system.name))
                continue;
            ui::TabItem item;
            item.label = system.short_name;
            item.badge = system.games;
            systems.push_back(std::move(item));
            sub_keys_.push_back("sys:" + system.name);
        }
        const bool grouped = prefs_.group_systems && !sub_keys_.empty();
        if (grouped)
            add(kEmulatorsTab, tr("Emulators"));
        else
        {
            for (std::size_t i = 0; i < sub_keys_.size(); ++i)
                add(sub_keys_[i], systems[i].label);
            sub_keys_.clear();
            systems.clear();
        }
        for (const orion::CustomTab &tab : prefs_.tabs)
        {
            if (!prefs_.tab_hidden("tab:" + tab.id))
                add("tab:" + tab.id, tab.name);
        }
        // Hidden, last, while something is hidden.
        if (std::any_of(titles().begin(), titles().end(),
                        [&](const Title &t) { return prefs_.title_hidden(t.id); }))
            add(kHiddenTab, tr("Hidden"));
        tabs_.set_tabs(std::move(tabs));
        subtabs_.set_tabs(std::move(systems));
        // A system asked for while grouped is the Emulators tab and its row.
        int sub = 0;
        for (std::size_t i = 0; i < sub_keys_.size(); ++i)
        {
            if (sub_keys_[i] == active)
                sub = static_cast<int>(i);
        }
        subtabs_.set_active(sub, true);
        const std::string bar =
            grouped && active.rfind("sys:", 0) == 0 ? std::string(kEmulatorsTab) : active;
        int index = 0;
        for (std::size_t i = 0; i < tab_keys_.size(); ++i)
        {
            if (tab_keys_[i] == bar)
                index = static_cast<int>(i);
        }
        tabs_.set_active(index, true);
    }

    // Recomputes what the grid shows from the tab, the query and the sort,
    // keeping the focus on the same title when it is still there.
    void rebuild()
    {
        sync_sort();
        std::string keep;
        if (!view_.empty())
            keep = grid_focused().id;
        // The counts follow favourites and placements.
        for (std::size_t i = 0; i < tab_keys_.size(); ++i)
            tabs_.tab(static_cast<int>(i)).badge =
                static_cast<int>(std::count_if(titles().begin(), titles().end(), [&](const Title &t)
                                               { return on_tab(t, tab_keys_[i]); }));
        for (std::size_t i = 0; i < sub_keys_.size(); ++i)
            subtabs_.tab(static_cast<int>(i)).badge =
                static_cast<int>(std::count_if(titles().begin(), titles().end(), [&](const Title &t)
                                               { return on_tab(t, sub_keys_[i]); }));
        rebuild_continue();
        // The covers make room for the systems' row on the Emulators tab, and
        // for Continue playing on All.
        const float top = on_emulators_tab() ? kGridTop + kSubRoom
                          : continue_shown() ? kGridTop + kContinueRoom
                                             : kGridTop;
        apply_view(top);

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
        // By name first: the scan sorted the console's titles, but games for
        // an emulator come after them, and a tab (Favourites, your own) can
        // hold both. Every other order is a stable sort on top of it, so ties
        // stay alphabetical.
        {
            std::vector<std::pair<std::string, int>> keyed;
            keyed.reserve(view_.size());
            for (const int index : view_)
                keyed.emplace_back(folded(titles()[static_cast<std::size_t>(index)].name), index);
            std::stable_sort(keyed.begin(), keyed.end(),
                             [](const auto &a, const auto &b) { return a.first < b.first; });
            for (std::size_t i = 0; i < keyed.size(); ++i)
                view_[i] = keyed[i].second;
        }
        switch (sort_of(sort_key()))
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
        case Sort::tag:
            std::stable_sort(view_.begin(), view_.end(), [&](int a, int b)
                             { return tag_of(titles()[a]) < tag_of(titles()[b]); });
            break;
        case Sort::played:
            std::stable_sort(view_.begin(), view_.end(), [&](int a, int b)
                             { return played(titles()[a]).last > played(titles()[b]).last; });
            break;
        case Sort::most_played:
            std::stable_sort(view_.begin(), view_.end(), [&](int a, int b)
                             { return played(titles()[a]).seconds > played(titles()[b]).seconds; });
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
            items.push_back(card_of(title, view_[i]));
        }
        grid_.set_items(std::move(items));
        // By tag, each tag is a section of its own with its name over it.
        if (sort_of(sort_key()) == Sort::tag)
        {
            std::vector<ui::GridSection> sections;
            for (std::size_t i = 0; i < view_.size(); ++i)
            {
                std::string label = tag_of(titles()[static_cast<std::size_t>(view_[i])]).second;
                if (sections.empty() || sections.back().label != label)
                {
                    ui::GridSection section;
                    section.first = static_cast<int>(i);
                    section.label = std::move(label);
                    sections.push_back(std::move(section));
                }
            }
            grid_.set_sections(std::move(sections));
        }
        mark_bare_covers();
        if (!view_.empty())
            grid_.set_focus(focus, true);
        if (view() == View::cases)
        {
            cases_.set_items(grid_.items());
            cases_.set_focus(grid_.focus(), true);
        }
    }

    // What a title is, in the order "Tag" sorts them: games (PS5, PS4 and
    // every emulated system together, by name), homebrew, media. Platform is
    // the order that splits games by system.
    std::pair<int, std::string> tag_of(const Title &title) const
    {
        if (title.is_rom())
            return {0, tr("Games")};
        if (library::shelf_of(title) == library::Shelf::media)
            return {2, tr("Media")};
        if (library::shelf_of(title) == library::Shelf::homebrew)
            return {1, tr("Homebrew")};
        return {0, tr("Games")};
    }

    // A title's cover, for the grid and the Continue playing row; index is
    // its place in titles().
    ui::CardItem card_of(const Title &title, int index) const
    {
        ui::CardItem item;
        item.title = title.name;
        item.subtitle = describe(title, emulator_name(title));
        item.badge.clear(); // the art slot draws the category banner
        item.texture = title.texture;
        if (title.icon_width > 0 && title.icon_height > 0)
            item.image_aspect =
                static_cast<float>(title.icon_width) / static_cast<float>(title.icon_height);
        item.top = hashed_color(title.id, 0.55f, 0.42f);
        item.bottom = hashed_color(title.id, 0.7f, 0.2f);
        item.accent = hashed_color(title.id, 0.6f, 1.0f);
        item.tag = index;
        return item;
    }

    // What was played last through ORION, newest first: console titles and
    // emulators' games alike, for the row over the All tab.
    void rebuild_continue()
    {
        std::string kept;
        if (!continue_view_.empty() && continue_.focus() >= 0 &&
            continue_.focus() < static_cast<int>(continue_view_.size()))
            kept = titles()[static_cast<std::size_t>(
                                continue_view_[static_cast<std::size_t>(continue_.focus())])]
                       .id;
        continue_view_.clear();
        for (std::size_t i = 0; i < titles().size(); ++i)
        {
            const Title &title = titles()[i];
            if (played(title).sessions > 0 && !prefs_.title_hidden(title.id) &&
                (!title.is_rom() || prefs_.system_enabled(library::shelf_system(title.system))))
                continue_view_.push_back(static_cast<int>(i));
        }
        std::stable_sort(continue_view_.begin(), continue_view_.end(), [&](int a, int b)
                         { return played(titles()[a]).last > played(titles()[b]).last; });
        if (continue_view_.size() > kContinueCount)
            continue_view_.resize(kContinueCount);
        std::vector<ui::CardItem> items;
        int focus = 0;
        for (std::size_t i = 0; i < continue_view_.size(); ++i)
        {
            const Title &title = titles()[static_cast<std::size_t>(continue_view_[i])];
            items.push_back(card_of(title, continue_view_[i]));
            items.back().subtitle.clear(); // a short row: the name is enough
            items.back().bare =
                items.back().texture != 0 &&
                std::fabs(std::log(std::max(0.2f, items.back().image_aspect))) >= 0.18f;
            if (title.id == kept)
                focus = static_cast<int>(i);
        }
        continue_.set_items(std::move(items));
        if (!continue_view_.empty())
            continue_.set_focus(focus, true);
        if (!continue_shown())
            continue_focus_ = false;
    }

    // A picture of another shape than the tiles (a box among square icons)
    // lies on the page, with no plate in the game's colours behind it: every
    // tile stays the same size and every picture looks as on its own tab.
    void mark_bare_covers()
    {
        const float tile = std::max(0.2f, grid_.style.card.art_aspect);
        for (int i = 0; i < grid_.count(); ++i)
        {
            ui::CardItem &item = grid_.item(i);
            item.bare = item.texture != 0 &&
                        std::fabs(std::log(std::max(0.2f, item.image_aspect) / tile)) >= 0.18f;
        }
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
        const bool own = orion::same_rgb(shown_.nebula, designed.nebula) &&
                         orion::same_rgb(shown_.nebula_far, designed.nebula_far);
        for (int i = 0; i < 4; ++i)
        {
            const Cloud &c = kClouds[i];
            const Color hue = own ? Color::rgb(c.designed)
                                  : lifted(c.far ? shown_.nebula_far : shown_.nebula, 2.0f);
            const float alpha = c.alpha * shown_.clouds;
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
        ui::text(list, fonts.semibold, tr("ORION MEDIA CENTER").c_str(), kMargin + 36.0f, 105, 17,
                 theme_.accent, gfx::Align::left, 4.0f);
        ui::text(list, fonts.display, tr("Library").c_str(), kMargin, 172, 62, paint.page_text());
        // The count, then the sort picker (drawn with the components).
        const std::size_t count = view_.size();
        const std::string summary =
            i18n::plural(static_cast<long long>(count), "{0} title", "{0} titles");
        ui::text(list, fonts.regular, summary.c_str(), view_pick_.bounds().x - 22.0f, 165, 24,
                 paint.page_text_muted(), gfx::Align::right);
        draw_status(list, paint);
    }

    // What ORION is doing in the background, over the sort picker: a turning
    // dot, the words and, when it can be counted, how far along it is.
    void draw_status(gfx::DrawList &list, const ui::Painter &paint) const
    {
        const float shown = status_shown_.value;
        if (shown < 0.01f || status_.empty())
            return;
        const ui::Fonts &fonts = context_.fonts;
        const float right = gfx::kVirtualWidth - kMargin;
        list.push_opacity(std::min(shown, 1.0f));
        const float width = fonts.semibold.font->measure(status_, 17.0f);
        const float cx = right - width - 20.0f;
        const float turn = context_.settings.reduced_motion ? 0.8f : clock_ * 4.0f;
        list.ring(cx, 99.0f, 7.0f, 1.5f, theme_.accent.with_alpha(0.45f));
        list.circle(cx + std::cos(turn) * 7.0f, 99.0f + std::sin(turn) * 7.0f, 2.5f, theme_.accent);
        ui::text(list, fonts.semibold, status_.c_str(), right, 105, 17, paint.page_text_muted(),
                 gfx::Align::right);
        if (status_progress_ >= 0.0f)
        {
            const Rect track = {right - width, 114.0f, width, 3.0f};
            list.rounded_rect(track, 1.5f, paint.page_text_muted().with_alpha(0.25f));
            list.rounded_rect(
                {track.x, track.y, track.w * std::clamp(status_progress_, 0.0f, 1.0f), track.h},
                1.5f, theme_.accent);
        }
        list.pop_opacity();
    }

    // The buttons that turn a tab row, either side of it: bright, with how
    // many, where tabs are out of view on that side; faint where there are
    // none. The count on the left goes above the button, or below it
    // (below: the row under the main one).
    void draw_tab_edges(gfx::DrawList &list, const ui::Painter &paint, const ui::TabBar &row,
                        ui::Button previous, ui::Button next, float size, bool below) const
    {
        const ui::Fonts &fonts = context_.fonts;
        const Rect bar = row.bounds();
        int left = 0;
        int right = 0;
        for (int i = 0; i < static_cast<int>(row.tabs().size()); ++i)
        {
            const Rect tab = row.tab_rect(fonts, i);
            if (tab.cx() < bar.x)
                ++left;
            else if (tab.cx() > bar.x + bar.w)
                ++right;
        }
        const float cy = bar.y + bar.h * 0.5f; // draw_button centres on it
        const auto edge = [&](ui::Button button, float x, int hidden, bool after)
        {
            list.push_opacity(hidden > 0 ? 1.0f : 0.35f);
            ui::draw_button(list, fonts, glyphs(), button, x, cy, size);
            list.pop_opacity();
            if (hidden <= 0)
                return;
            const std::string more = "+" + std::to_string(hidden);
            const float w = ui::button_width(button, size);
            const float y = after ? cy + 7.0f : below ? cy + size * 0.95f : cy - size * 0.5f;
            ui::text(list, fonts.semibold, more.c_str(), after ? x + w + 10.0f : x + w * 0.5f, y,
                     after ? 20.0f : 16.0f, paint.page_text_muted(),
                     after ? gfx::Align::left : gfx::Align::center);
        };
        edge(previous, kMargin, left, false);
        edge(next, bar.x + bar.w + 12.0f, right, true);
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
        const std::string label = "“" + query_ + "”";
        ui::text(list, fonts.semibold, label.c_str(), x, y, 22, paint.page_text(),
                 gfx::Align::right);
    }

    // The category on a cover: a small coloured banner in its top-right corner.
    void draw_banner(ui::Canvas &canvas, const Rect &art, const Title &title) const
    {
        std::string system;
        std::string label = tr("GAME");
        Color colour = Color::rgb(0x8fe3ff);
        if (title.is_rom())
        {
            system = library::short_system_name(title.system);
            for (char &c : system)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            label = system;
            colour = Color::rgb(0xcdb6ff);
        }
        else if (title.platform == library::Platform::ps4)
            label = "PS4", colour = Color::rgb(0x8fb0ff);
        else if (library::shelf_of(title) == library::Shelf::media)
            label = tr("MEDIA"), colour = Color::rgb(0xffc94a);
        else if (library::shelf_of(title) == library::Shelf::homebrew)
            label = tr("HOMEBREW"), colour = Color::rgb(0x86efb4);
        const ui::FontRef &font = canvas.fonts.semibold;
        constexpr float kSize = 14.0f;
        constexpr float kTracking = 1.6f;
        const float width =
            font.font->measure(label, kSize) + kTracking * static_cast<float>(label.size()) + 20.0f;
        const Rect pill{art.x + art.w - width - 10.0f, art.y + 10.0f, width, 26.0f};
        canvas.list.shadow({pill.x, pill.y + 3.0f, pill.w, pill.h}, 13.0f, 10.0f,
                           Color::rgb(0x000000, 0.35f));
        canvas.list.rounded_rect(pill, 13.0f, colour);
        ui::text(canvas.list, font, label.c_str(), pill.cx(), pill.y + 18.5f, kSize,
                 Color::rgb(0x08101c), gfx::Align::center, kTracking);
    }

    // The focused cover's ring: light whose colour drifts slowly through the
    // spectrum, with a second, shifted hue just inside it so the two blend.
    void draw_aurora_ring(gfx::DrawList &list, const Rect &art, float radius, float focus) const
    {
        const float time = context_.settings.reduced_motion ? 0.0f : clock_;
        const float hue = std::fmod(time * 0.05f, 1.0f);
        const Color outer = hsv(hue, 0.55f, 1.0f);
        const Color inner = hsv(std::fmod(hue + 0.22f, 1.0f), 0.5f, 1.0f);
        list.bordered_rect(art.inset(-5.0f), radius + 5.0f, Color::rgb(0x000000, 0.0f), 3.0f,
                           outer.with_alpha(0.95f * focus));
        list.bordered_rect(art.inset(-1.5f), radius + 1.5f, Color::rgb(0x000000, 0.0f), 1.5f,
                           inner.with_alpha(0.7f * focus));
    }

    // A case as its system's are made: the plastic's colour all round, a
    // header across the top of the sleeve with the system's name, the cover
    // under it. Drawn, not copied: plain type, no printed logos. Homebrew
    // wears ORION's own night sky and ring.
    struct CaseBand
    {
        Color rim;  // the plastic
        Color fill; // the sleeve's header
        Color ink;
        std::string label;
        bool orion = false;
    };
    CaseBand case_band(const Title &title) const
    {
        if (library::shelf_of(title) == library::Shelf::homebrew)
            return {Color::rgb(0x161a3d), Color::rgb(0x0b0e20), theme_.accent, tr("HOMEBREW"),
                    true};
        if (library::shelf_of(title) == library::Shelf::media)
            return {Color::rgb(0x202228), Color::rgb(0xffc94a), Color::rgb(0x1a1204), tr("MEDIA")};
        if (title.platform == library::Platform::ps4)
            return {Color::rgb(0x1c4fb5), Color::rgb(0x0a3d91), Color::rgb(0xffffff), "PS4"};
        if (!title.is_rom())
            return {Color::rgb(0x2463d9), Color::rgb(0xf7f8fa), Color::rgb(0x08090b), "PS5"};
        std::string name = library::short_system_name(title.system);
        for (char &c : name)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        struct Known
        {
            const char *name;
            std::uint32_t rim;
            std::uint32_t fill;
            std::uint32_t ink;
        };
        constexpr Known kKnown[] = {
            {"PS2", 0x14161b, 0x0d0f14, 0xffffff},     {"PS1", 0x14161b, 0x22252b, 0xffffff},
            {"PSP", 0x14161b, 0x1b1d22, 0xffffff},     {"GAMECUBE", 0x16171c, 0x4a3b8c, 0xffffff},
            {"WII", 0xe9edf2, 0xeef1f5, 0x2a6fd6},     {"WII U", 0x1d7fd0, 0x1f8ad8, 0xffffff},
            {"SNES", 0x16171c, 0x5f6278, 0xffffff},    {"NES", 0x16171c, 0x9b1c1c, 0xffffff},
            {"N64", 0x16171c, 0x2b2b2b, 0xffffff},     {"GBA", 0x16171c, 0x3a3c9a, 0xffffff},
            {"GBC", 0x16171c, 0x5b6b2c, 0xffffff},     {"GB", 0x16171c, 0x6f7a3c, 0xffffff},
            {"DS", 0x16171c, 0xd9dce3, 0x111111},      {"3DS", 0xd8dbe1, 0xc8242b, 0xffffff},
            {"GENESIS", 0x16171c, 0x151515, 0xffffff},
        };
        for (const Known &known : kKnown)
        {
            if (name == known.name)
                return {Color::rgb(known.rim), Color::rgb(known.fill), Color::rgb(known.ink), name};
        }
        return {Color::rgb(0x16171c), Color::rgb(0x4b3fa6), Color::rgb(0xffffff), name};
    }

    // A game in Cases view. The picture is the sharp copy when the app has
    // made one (Context::sharp_icons), else the grid's small one.
    void draw_case(ui::Canvas &canvas, const Rect &art, const ui::CardItem &item, float focus) const
    {
        gfx::DrawList &list = canvas.list;
        const Title &title = titles()[static_cast<std::size_t>(item.tag)];
        const CaseBand band = case_band(title);
        constexpr float kRadius = 14.0f;
        constexpr float kInner = 5.0f;
        const bool aurora = shown_.chrome.focus == orion::FocusRing::aurora && focus > 0.01f;
        if (aurora)
        {
            const float time = context_.settings.reduced_motion ? 0.0f : clock_;
            const Color light = hsv(std::fmod(time * 0.05f, 1.0f), 0.55f, 1.0f);
            list.glow(art, kRadius, 30.0f * focus, light.with_alpha(0.5f * focus));
        }
        // The plastic, lit from the top left.
        list.rounded_rect(art, kRadius, band.rim);
        list.gradient_rect(art, kRadius, Color::rgb(0xffffff, 0.22f), Color::rgb(0x000000, 0.18f));
        // The sleeve inside it: the header, then the cover.
        const Rect sleeve = art.inset(std::round(art.w * 0.024f));
        const float band_h = std::round(sleeve.h * 0.115f);
        list.rounded_rect(sleeve, kInner, band.fill);
        if (band.orion)
            list.gradient_rect(sleeve, kInner, Color::rgb(0x1b1450), Color::rgb(0x0b0e20));
        const Rect cover{sleeve.x, sleeve.y + band_h, sleeve.w, sleeve.h - band_h};
        // Drawn a corner taller and clipped flat: a straight edge under the
        // header, round corners at the bottom.
        const Rect drawn{cover.x, cover.y - kInner, cover.w, cover.h + kInner};
        const bool sharp = title.sharp_texture != 0 && title.sharp_height > 0;
        const std::uint32_t texture = sharp ? title.sharp_texture : item.texture;
        list.push_clip(cover);
        if (texture != 0)
        {
            // Filled, trimmed evenly at the edges that differ.
            const float picture = sharp ? static_cast<float>(title.sharp_width) /
                                              static_cast<float>(title.sharp_height)
                                        : std::max(0.2f, item.image_aspect);
            const float shape = drawn.w / std::max(drawn.h, 1.0f);
            Rect uv = sharp ? Rect{0.0f, 0.0f, 1.0f, 1.0f} : item.uv;
            if (picture > shape)
            {
                const float keep = shape / picture;
                uv.x += uv.w * (1.0f - keep) * 0.5f;
                uv.w *= keep;
            }
            else
            {
                const float keep = picture / shape;
                uv.y += uv.h * (1.0f - keep) * 0.5f;
                uv.h *= keep;
            }
            list.image(texture, drawn, uv, Color::rgb(0xffffff), kInner);
        }
        else
        {
            list.gradient_rect(drawn, kInner, item.top, item.bottom);
            const std::string letters = monogram(item.title);
            ui::text(list, canvas.fonts.display, letters.c_str(), cover.cx(),
                     cover.y + cover.h * 0.5f + cover.h * 0.12f, cover.h * 0.3f,
                     Color::rgb(0xffffff, 0.9f), gfx::Align::center);
        }
        list.pop_clip();
        // The header's name, large at its left, where a case prints its mark.
        const float size = band_h * 0.6f;
        float x = sleeve.x + band_h * 0.32f;
        const float mid = sleeve.y + band_h * 0.5f;
        if (band.orion)
        {
            // ORION's ring before the word, as on its header.
            const float ring = band_h * 0.22f;
            list.ring(x + ring, mid, ring, ring * 0.22f, band.ink);
            list.circle(x + ring + std::cos(0.8f) * ring, mid + std::sin(0.8f) * ring, ring * 0.32f,
                        Color::rgb(0xffffff));
            x += ring * 2.0f + band_h * 0.25f;
        }
        const float label_size = band.orion ? size * 0.72f : size;
        ui::text(list, canvas.fonts.semibold, band.label.c_str(), x, mid + label_size * 0.36f,
                 label_size, band.ink, gfx::Align::left, label_size * 0.06f);
        // The plastic over the sleeve: a faint sheen and the case's edge.
        list.gradient_rect(cover, kInner, Color::rgb(0xffffff, 0.1f), Color::rgb(0xffffff, 0.0f));
        list.bordered_rect(art, kRadius, Color::rgb(0x000000, 0.0f), 1.5f,
                           Color::rgb(0xffffff, 0.28f));
        if (favourite_of(title.id))
        {
            const float r = std::max(16.0f, art.w * 0.06f);
            const float cx = sleeve.x + sleeve.w - r - 12.0f;
            const float cy = cover.y + r + 12.0f;
            list.circle(cx, cy, r, Color::rgb(0x0b0e20, 0.72f));
            list.star(cx, cy, r * 0.72f, Color::rgb(0xffc94a));
        }
        if (aurora)
            draw_aurora_ring(list, art, kRadius, focus);
    }

    // A title in List view: its cover small, its name, what it is and when
    // it was last played, on one wide row.
    void draw_row(ui::Canvas &canvas, const Rect &cell, const ui::CardItem &item, float focus) const
    {
        gfx::DrawList &list = canvas.list;
        const Title &title = titles()[static_cast<std::size_t>(item.tag)];
        const ui::Fonts &fonts = canvas.fonts;
        list.rounded_rect(cell, 18.0f, Color::rgb(0xffffff, 0.05f + 0.07f * focus));
        list.bordered_rect(cell, 18.0f, Color::rgb(0x000000, 0.0f), 1.0f,
                           Color::rgb(0xffffff, 0.1f + 0.2f * focus));
        const float pad = 12.0f;
        const Rect thumb{cell.x + pad, cell.y + pad, cell.h - 2.0f * pad, cell.h - 2.0f * pad};
        if (item.texture != 0)
        {
            Rect uv = item.uv;
            const float picture = std::max(0.2f, item.image_aspect);
            if (picture > 1.0f)
            {
                uv.x += uv.w * (1.0f - 1.0f / picture) * 0.5f;
                uv.w /= picture;
            }
            else
            {
                uv.y += uv.h * (1.0f - picture) * 0.5f;
                uv.h *= picture;
            }
            list.image(item.texture, thumb, uv, Color::rgb(0xffffff), 10.0f);
        }
        else
        {
            list.gradient_rect(thumb, 10.0f, item.top, item.bottom);
            const std::string letters = monogram(item.title);
            ui::text(list, fonts.display, letters.c_str(), thumb.cx(), thumb.cy() + 10.0f, 28.0f,
                     Color::rgb(0xffffff, 0.9f), gfx::Align::center);
        }
        const float x = thumb.x + thumb.w + 22.0f;
        const CaseBand band = case_band(title);
        // The tag at the right, in its band's colours: what kind, at a glance.
        const float tag_w = fonts.semibold.font->measure(band.label, 15.0f) + 28.0f;
        const Rect tag{cell.x + cell.w - tag_w - 24.0f, cell.cy() - 15.0f, tag_w, 30.0f};
        list.rounded_rect(tag, 15.0f, band.fill);
        ui::text(list, fonts.semibold, band.label.c_str(), tag.cx(), tag.y + 20.5f, 15.0f, band.ink,
                 gfx::Align::center, 1.2f);
        // The name, cut to the room left of the tag.
        const float room = tag.x - 24.0f - x;
        std::string name = title.name;
        const gfx::Font &font = *fonts.semibold.font;
        if (font.measure(name, 24.0f) > room)
        {
            while (!name.empty() && font.measure(name + "…", 24.0f) > room)
            {
                std::size_t at = name.size() - 1;
                while (at > 0 && (static_cast<unsigned char>(name[at]) & 0xC0) == 0x80)
                    --at;
                name.erase(at);
            }
            name += "…";
        }
        ui::text(list, fonts.semibold, name.c_str(), x, cell.y + 40.0f, 24.0f, theme_.text);
        std::string line = item.subtitle;
        const orion::PlayRecord &record = played(title);
        if (record.sessions > 0)
            line += "   ·   " +
                    trf("Played {0}, {1}", {orion::play_time_text(record.seconds),
                                            orion::ago_text(record.last, std::time(nullptr))});
        if (favourite_of(title.id))
            line += "   ·   " + tr("Favourite");
        ui::text(list, fonts.regular, line.c_str(), x, cell.y + 70.0f, 18.0f, theme_.text_muted);
    }

    void draw_art(ui::Canvas &canvas, const Rect &art, float radius, const ui::CardItem &item,
                  float focus) const
    {
        const bool aurora = shown_.chrome.focus == orion::FocusRing::aurora && focus > 0.01f;
        if (aurora)
        {
            // glow() fills its inside too: drawn first, the art covers that.
            const float time = context_.settings.reduced_motion ? 0.0f : clock_;
            const Color light = hsv(std::fmod(time * 0.05f, 1.0f), 0.55f, 1.0f);
            canvas.list.glow(art, radius, 26.0f * focus, light.with_alpha(0.5f * focus));
        }
        const Title &title = titles()[static_cast<std::size_t>(item.tag)];
        const float picture = std::max(0.2f, item.image_aspect);
        const float cover = art.w / std::max(art.h, 1.0f);
        // What the cover's dressing (sheen, rim, badge, star) goes on: the
        // tile, or a picture of another shape where it sits in the tile.
        Rect shown = art;
        if (item.texture != 0 && std::fabs(std::log(picture / cover)) < 0.18f)
        {
            // About the cover's own shape: it fills the cover, trimmed evenly
            // at the edges that differ.
            Rect uv = item.uv;
            if (picture > cover)
            {
                const float keep = cover / picture;
                uv.x += uv.w * (1.0f - keep) * 0.5f;
                uv.w *= keep;
            }
            else
            {
                const float keep = picture / cover;
                uv.y += uv.h * (1.0f - keep) * 0.5f;
                uv.h *= keep;
            }
            canvas.list.image(item.texture, art, uv, Color::rgb(0xffffff), radius);
        }
        else if (item.texture != 0)
        {
            // Another shape (a box on a tab of square icons, as Favourites
            // mixes them): the whole picture as large as the tile allows,
            // centred, with no plate behind it, so every tile is the same
            // size and every picture looks as it does on its own tab.
            const float aspect = std::max(0.2f, item.image_aspect);
            Rect fit = art;
            if (aspect > fit.w / fit.h)
            {
                const float h = fit.w / aspect;
                fit.y += fit.h - h; // on the tile's bottom edge, by the title
                fit.h = h;
            }
            else
            {
                const float w = fit.h * aspect;
                fit.x += (fit.w - w) * 0.5f;
                fit.w = w;
            }
            canvas.list.image(item.texture, fit, item.uv, Color::rgb(0xffffff), radius);
            shown = fit;
        }
        else
        {
            canvas.list.gradient_rect(art, radius, item.top, item.bottom);
            const std::string letters = monogram(item.title);
            ui::text(canvas.list, canvas.fonts.display, letters.c_str(), art.cx(),
                     art.y + art.h * 0.5f + art.h * 0.13f, art.h * 0.36f,
                     Color::rgb(0xffffff, 0.9f), gfx::Align::center);
        }
        canvas.list.gradient_rect(shown, radius, Color::rgb(0xffffff, 0.14f),
                                  Color::rgb(0xffffff, 0.0f));
        if (shown_.chrome.cover_rim)
            canvas.list.bordered_rect(shown, radius, Color::rgb(0x000000, 0.0f), 1.0f,
                                      Color::rgb(0xffffff, 0.16f));
        // Small covers (Continue playing) leave the banner out: it would cover them.
        if (shown_.chrome.badges && art.w >= 160.0f)
            draw_banner(canvas, shown, title);
        if (aurora)
            draw_aurora_ring(canvas.list, shown, radius, focus);
        if (favourite_of(title.id))
        {
            // A gold star on a dark disc in the corner the badge leaves free.
            const float r = std::max(14.0f, art.w * 0.075f);
            const float cx = shown.x + r + 8.0f;
            const float cy = shown.y + r + 8.0f;
            canvas.list.circle(cx, cy, r, Color::rgb(0x0b0e20, 0.72f));
            canvas.list.star(cx, cy, r * 0.72f, Color::rgb(0xffc94a));
        }
    }

    void draw_details(gfx::DrawList &list, ui::Painter &paint) const
    {
        const Title &title = focused();
        std::string line = title.id;
        const library::Player player = title.is_rom() ? player_of(title) : library::Player();
        if (player.app != nullptr && player.app->key == "retroarch")
            line = library::short_system_name(title.system) + "   " +
                   trf("RetroArch, {0}", {core_name(player.core)});
        else if (title.is_rom())
            line = library::short_system_name(title.system) + "   " + emulator_name(title);
        else
        {
            if (!title.version.empty())
                line += "   v" + title.version;
            line += "   " + tr(shelf_name(library::shelf_of(title)));
        }
        // What it is goes on the first line, its history on a second: one
        // line beside the buttons left too little room for both.
        std::string history;
        const auto add = [&](const std::string &part)
        { history += (history.empty() ? "" : "   ·   ") + part; };
        const orion::PlayRecord &record = played(title);
        if (record.sessions > 0)
            add(trf("Played {0}, {1}", {orion::play_time_text(record.seconds),
                                        orion::ago_text(record.last, std::time(nullptr))}));
        if (favourite_of(title.id))
            add(tr("Favourite"));
        if (const orion::CustomTab *placed = prefs_.placement(title.id))
            add(placed->name);
        // Never under the buttons: what does not fit ends in "…".
        const gfx::Font &font = *context_.fonts.regular.font;
        const float room = gfx::kVirtualWidth - kMargin - hint_row().width - 30.0f - 24.0f -
                           kMargin - 24.0f - 20.0f;
        const auto fit = [&](std::string text, float size)
        {
            if (font.measure(text, size) <= room)
                return text;
            const std::string dots = "…";
            while (!text.empty() && font.measure(text + dots, size) > room)
            {
                // Back off one UTF-8 character: to the byte its last one starts at.
                std::size_t at = text.size() - 1;
                while (at > 0 && (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80)
                    --at;
                text.erase(at);
            }
            // No separator left dangling before the dots.
            while (text.ends_with(" ") || text.ends_with("·"))
                text.resize(text.size() - (text.back() == ' ' ? 1 : 2));
            return text + dots;
        };
        constexpr float kSize = 20.0f;
        constexpr float kSmall = 18.0f;
        line = fit(line, kSize);
        history = fit(history, kSmall);
        const float width = std::max(font.measure(line, kSize),
                                     history.empty() ? 0.0f : font.measure(history, kSmall));
        plate(list, paint, {kMargin - 24.0f, 972.0f, width + 48.0f, 76.0f}, 38.0f);
        if (history.empty())
        {
            ui::text(list, context_.fonts.regular, line.c_str(), kMargin, 1017, kSize,
                     paint.page_text_muted());
            return;
        }
        ui::text(list, context_.fonts.regular, line.c_str(), kMargin, 1004, kSize,
                 paint.page_text());
        ui::text(list, context_.fonts.regular, history.c_str(), kMargin, 1031, kSmall,
                 paint.page_text_muted());
    }

    void draw_empty(gfx::DrawList &list, const ui::Painter &paint) const
    {
        const ui::Fonts &fonts = context_.fonts;
        const float cx = gfx::kVirtualWidth * 0.5f;
        const std::string key = active_key();
        std::string title = tr("No titles yet");
        std::string first = tr("ORION shows what ShadowMountPlus can mount from /data/homebrew,");
        std::string second = tr("/mnt/ext0/homebrew and USB drives.");
        if (!titles().empty() && !query_.empty())
        {
            title = trf("Nothing matches “{0}”", {query_});
            first = tr("Try fewer letters, or press Circle to clear the search.");
            second.clear();
        }
        else if (!titles().empty() && key == "favourites")
        {
            title = tr("No favourites yet");
            first = tr("Press Square on any title to star it.");
            second = tr("Starred titles collect here and keep their star.");
        }
        else if (!titles().empty() && key.rfind("tab:", 0) == 0)
        {
            title = tr("Nothing here yet");
            first = tr("Press Cross on any title and choose where to move it.");
            second = tr("Titles you move here stay here.");
        }
        else if (!titles().empty())
        {
            title = tr("Nothing on this shelf");
            first = tr("Press L1 or R1 to look at another one.");
            second.clear();
        }
        ui::text(list, fonts.display, title.c_str(), cx, 560, 46, paint.page_text(),
                 gfx::Align::center);
        ui::text(list, fonts.regular, first.c_str(), cx, 616, 24, paint.page_text_muted(),
                 gfx::Align::center);
        ui::text(list, fonts.regular, second.c_str(), cx, 650, 24, paint.page_text_muted(),
                 gfx::Align::center);
    }

    // The buttons along the bottom: on the Emulators tab L2 / R2 change
    // system, said first, and seven hints take a little less room each.
    struct HintRow
    {
        const ui::Hint *hints;
        int count;
        ui::HintLayout layout;
        float width;
    };
    HintRow hint_row() const
    {
        const bool systems = on_emulators_tab();
        HintRow row{systems ? hints_.data() : hints_.data() + 1, systems ? 7 : 6, {}, 0.0f};
        row.layout.size = systems ? 36.0f : 38.0f;
        row.layout.text_size = systems ? 20.0f : 21.0f;
        row.layout.cy = 1010.0f;
        row.width = ui::measure_hints(context_.fonts, row.hints, row.count, row.layout);
        return row;
    }

    void draw_hints(gfx::DrawList &list, ui::Painter &paint) const
    {
        const HintRow row = hint_row();
        const float right = gfx::kVirtualWidth - kMargin;
        plate(list, paint, {right - row.width - 30.0f, 972.0f, row.width + 60.0f, 76.0f}, 38.0f);
        ui::draw_hints(list, context_.fonts, glyphs(), row.hints, row.count, right, true,
                       row.layout);
    }

    // A bar at the bottom of the screen, as the look asks: glass, or its rim.
    void plate(gfx::DrawList &list, ui::Painter &paint, const Rect &r, float radius) const
    {
        if (shown_.chrome.plates == orion::Plates::none)
            return;
        if (shown_.chrome.plates == orion::Plates::outline)
        {
            list.bordered_rect(r, radius, Color::rgb(0x000000, 0.0f), 1.5f,
                               Color::rgb(0xffffff, 0.3f));
            return;
        }
        glass_panel(list, paint, r, radius);
    }

    // While Circle is held: how long is left before ORION closes.
    void draw_hold_to_exit(gfx::DrawList &list, ui::Painter &paint) const
    {
        constexpr float kShowAfter = 0.2f;
        if (hold_back_ <= kShowAfter)
            return;
        const float progress =
            std::clamp((hold_back_ - kShowAfter) / (kHoldToExit - kShowAfter), 0.0f, 1.0f);
        const ui::Fonts &fonts = context_.fonts;
        const std::string words = tr("Keep holding to close ORION");
        const char *label = words.c_str();
        const float width = fonts.semibold.font->measure(label, 22.0f) + 110.0f;
        const Rect r{gfx::kVirtualWidth - kMargin - width, 72.0f, width, 64.0f};
        list.push_opacity(std::min(1.0f, (hold_back_ - kShowAfter) * 6.0f));
        glass_panel(list, paint, r, 32.0f);
        const float cx = r.x + 40.0f;
        const float cy = r.y + 32.0f;
        list.ring(cx, cy, 14.0f, 3.0f, Color::rgb(0xffffff, 0.2f));
        list.arc(cx, cy, 14.0f, 3.0f, -1.5707963f, -1.5707963f + 6.2831853f * progress,
                 theme_.accent);
        ui::text(list, fonts.semibold, label, r.x + 70.0f, r.y + 40.0f, 22, paint.page_text());
        list.pop_opacity();
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

    // Controller glyphs as frosted caps lettered in the theme's text: no grey
    // outline to fight the glass they sit on. Bare on the page, they take the
    // page's text; on a bar, the bar's.
    ui::GlyphStyle glyphs() const
    {
        const bool bare = shown_.chrome.plates == orion::Plates::none;
        Color ink = bare && theme_.page_text.a > 0.0f ? theme_.page_text : theme_.text;
        ui::GlyphStyle style = ui::GlyphStyle::dark();
        style.body = ink;
        style.body.a = 0.14f;
        style.edge = ink;
        style.edge.a = 0.0f;
        style.ink = ink;
        style.label = ink;
        style.label.a = 0.92f;
        return style;
    }

    // The tab bar draws its own frosted track; this adds the same gloss.
    void draw_tab_sheen(gfx::DrawList &list) const
    {
        const int count = static_cast<int>(tabs_.tabs().size());
        if (count == 0 || shown_.chrome.tabs != orion::TabStyle::pills)
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
        const std::string placeholder =
            search                            ? tr("Search titles or IDs")
            : typing_ == Typing::rename_tab   ? tr("Rename the tab")
            : typing_ == Typing::rename_title ? tr("A new name; empty gives it its own back")
            : typing_ == Typing::theme_name   ? tr("Name the new theme")
            : typing_ == Typing::art_folder   ? tr("A folder under /data or /mnt")
            : typing_ == Typing::rom_folder   ? tr("A folder of games, like /mnt/usb0/roms")
                                              : tr("Name the new tab");
        const bool empty = text.empty();
        ui::text(list, fonts.regular, empty ? placeholder.c_str() : text.c_str(), field.x + 28,
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
    orion::LanguageLibrary languages_;
    std::array<std::string, 7> hint_words_; // the hint bar's words (use_language)
    std::array<ui::Hint, 7> hints_{};
    orion::Look look_;  // the main look, as Settings edits it
    orion::Look shown_; // what the active tab wears (its own theme, or look_)
    orion::LibraryPrefs prefs_;
    ui::Theme theme_;
    bool reduced_ = false;
    std::vector<std::string> favourites_;
    orion::PlayLog plays_;
    std::string art_folder_was_; // where box art was before the folder last changed
    orion::BackdropCopies backdrop_copies_;
    orion::RegisteredTitles registered_;     // the system's titles beyond the homebrew folders
    bool scan_asked_ = false;                // the next generation is a scan someone asked for
    orion::BackdropCopies icon_copies_;      // the home screen's icon0.png (request_icons)
    std::vector<std::string> icons_waiting_; // to copy once the FTP server is free
    std::vector<std::string> icon_ids_;      // what the running copy is for
    std::string backdrop_candidate_;         // what the focused game shows (backdrop_of)
    std::string backdrop_drawn_;             // what is drawn now; it changes once faded out
    float backdrop_dwell_ = 0.0f;            // seconds the focus has rested on it
    float backdrop_alpha_ = 0.0f;
    std::vector<std::string> tab_keys_; // the key of each tab, in tab order
    std::string preview_return_;        // the tab to go back to after a tab's preview
    std::vector<int> view_;             // indices into the library, in display order
    std::string query_;
    std::string name_; // a tab name being typed
    Typing typing_ = Typing::none;
    std::string menu_title_; // the title a menu is about
    std::string menu_tab_;   // the tab a menu (or a rename) is about
    ui::TabBar tabs_;
    ui::TabBar subtabs_;                // the systems, under the Emulators tab (grouped)
    std::vector<std::string> sub_keys_; // "sys:..." for each of them
    ui::GridView grid_;
    ui::Carousel continue_;          // what was played last, over the All tab
    ui::Carousel cases_;             // Cases view: the grid's items as a shelf of cases
    std::vector<int> continue_view_; // indices into the library, newest first
    bool continue_focus_ = false;    // the row has the focus
    ui::Select sort_;                // the tab's order, in the header (Up from the top row)
    ui::Select view_pick_;           // the tab's view (Grid, Cases, List), left of the sort
    bool header_on_view_ = false;    // of the two, the view picker has the focus
    ui::CardLook grid_card_;         // the grid's card as Grid view wears it
    bool header_focus_ = false;      // the sort picker has the focus
    ui::Pulse header_refusal_;
    std::string status_;            // what ORION is doing in the background, or empty
    float status_progress_ = -1.0f; // 0..1 when it can be measured
    tween::Spring status_shown_;
    ui::Keyboard keys_;
    ui::Menu menu_;
    // Remove: the question and what it is about.
    ui::Dialog dialog_;
    int remove_kind_ = 0;                         // the menu tag that asked (kItemRemoveHome, ...)
    std::string remove_id_;                       // the title or game it is about
    float remove_hold_ = 0.0f;                    // seconds Cross has been held on a delete
    std::unordered_set<std::string> removed_ids_; // gone since the scan: on no tab
    orion::SettingsPanel settings_;
    orion::SetupPanel setup_;
    bool setup_pending_ = false;     // the setup opens on the first frame (it needs feedback)
    bool launch_after_pick_ = false; // the emulator menu opened because a game was started
    std::string now_playing_;        // the song Settings, Sound was last told of
    tween::Spring keyboard_shown_;
    std::string launch_message_;
    bool launch_ok_ = false;
    float launch_shown_ = 0.0f;   // seconds the card stays
    float launch_pending_ = 0.0f; // seconds before another launch is taken
    float launch_exit_ = 0.0f;    // seconds until ORION closes for a launch
    bool hold_armed_ = false;     // Circle went down while browsing
    float hold_back_ = 0.0f;      // seconds it has been held since
    tween::Spring launch_card_;
    float age_ = 0.0f;
    float clock_ = 0.0f;
    bool art_manual_ = false; // the run was asked for in Settings: report it
    int art_sweep_ = 0;       // frames until the covers take new textures
    int generation_ = 0;      // the library's, as last rebuilt for
    // Games whose box art was asked for, and which picture (want_icons).
    std::unordered_map<std::string, std::string> wanted_icons_;
    std::string folder_emulator_; // the emulator a game folder being typed is for
    // Last, so it is destroyed first: its thread stops before what it uses.
    orion::ArtFetcher art_{
        [](const std::string &url, std::size_t limit, std::string *body, std::string *error)
        { return sys::http_get(url, limit, body, error); }};
    // A check whose request never returns cannot be stopped, only left
    // behind: Check now starts a new one (check_for_updates).
    std::unique_ptr<orion::UpdateCheck> updates_ = new_update_check();
    float check_started_ = 0.0f;       // clock_ when the running check began
    bool update_asked_ = false;        // the check this launch makes by itself has started
    bool manual_update_check_ = false; // the running check was asked for: say how it went
    orion::UpdateInstaller installer_{
        [](const std::string &url, std::size_t limit, std::string *body, std::string *error)
        { return sys::http_get(url, limit, body, error); },
        [](const std::string &url, const std::string &path, std::size_t limit, std::string *error)
        { return sys::http_download(url, path, limit, error); }};
    bool update_waiting_ = false; // an update is ready: ask when nothing else is open
    bool dialog_update_ = false;  // the dialog asks about the update, not a removal
};

} // namespace

std::unique_ptr<app::Concept> make_library(app::Context &context)
{
    return std::make_unique<Library>(context);
}

} // namespace hui::concepts
