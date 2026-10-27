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
#include "library/art_match.hpp"
#include "library/case_templates.hpp"
#include "library/emulators.hpp"
#include "library/favourites.hpp"
#include "library/json.hpp"
#include "orion/art_fetcher.hpp"
#include "core/i18n.hpp"
#include "orion/backdrop_copies.hpp"
#include "orion/languages.hpp"
#include "orion/registered_titles.hpp"
#include "orion/key_page.hpp"
#include "orion/romm.hpp"
#include "orion/romm_browser.hpp"
#include "orion/romm_saves.hpp"
#include "orion/steamgriddb.hpp"
#include "orion/wide_copies.hpp"
#include "orion/look.hpp"
#include "orion/playtime.hpp"
#include "orion/prefs.hpp"
#include "orion/settings_panel.hpp"
#include "orion/setup_panel.hpp"
#include "orion/storage.hpp"
#include "orion/theme_file.hpp"
#include "orion/drives.hpp"
#include "orion/title_details.hpp"
#include "orion/updates.hpp"
#include "orion/whats_new.hpp"
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
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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
// ORION's title ID since 01.103.000. Another app had PPSA99061 in a catalog
// first, so ORION moved: an ORION still on PPSA99061 installs a release built
// as PPSA99062 beside itself, the player deletes the old tile and opens the
// new one (everything else lives in /data/orion and comes along).
constexpr const char *kNextTitleId = "PPSA99062";
constexpr float kSortWidth = 330.0f; // the sort picker in the header
constexpr float kViewWidth = 250.0f; // the view picker, left of it
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
// The rest-mode watcher, which ORION sends itself (payload/resume).
constexpr const char *kResumePath = "/app0/assets/payloads/orion-resume.elf";
// The reader of the system's play time, sent once as ORION starts (payload/playtime).
constexpr const char *kPlaytimePath = "/app0/assets/payloads/orion-playtime.elf";
constexpr float kHoldToDelete = 1.5f; // seconds of Cross on Delete
// The launch helper reads a request of 4 KiB; a game's path is one line of it.
constexpr std::size_t kMaxRequestPath = 1024;
// The FTP server payload players commonly run (ftpsrv): it can read what
// ORION's sandbox cannot, the system's copies of home-screen pictures.
constexpr int kFtpPort = 2121;
// Continue's config for one start, as RetroArch sees it (its folder is /app0).
constexpr const char *kContinueConfig = "/app0/orion-continue.cfg";
// Games without box art a run looks for by itself (Find box art now: all).
constexpr std::size_t kArtPerRun = 300;
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
    grid_key,     // from Settings > Box art > SteamGridDB key
    art_name,     // from a title's menu: the name SteamGridDB is asked for
    romm_url,     // from Settings > Emulators > RomM server
    romm_code,    // from Settings > Emulators > Pair with RomM
    romm_search,  // from the RomM Games screen: words to look for
};

// Tags of the menu items.
enum : int
{
    kItemFavourite = 1,
    kItemNewTab,
    kItemUnplace,
    kItemLaunch,
    kItemHide,
    kItemPlayWith,
    kItemRemove,
    kItemRemoveHome,
    kItemRemoveErase,
    kItemRemoveRom,
    kItemHdArt,
    kItemRename,
    kItemPicture,         // Choose picture…
    kItemOwnPicture,      // Use its own picture
    kItemFindArt,         // Find other art… (SteamGridDB)
    kItemMusic,           // Choose music…
    kItemOwnMusic,        // Use its own music
    kItemPickUp,          // the picture browser: up one folder
    kItemPickMore,        // its next page
    kItemPickLess,        // its page before
    kItemMoveMenu,        // Move to tab…: the list of tabs
    kItemDetails,         // Details…: the title's page
    kItemFindArtName,     // Find art by name… (SteamGridDB)
    kItemContinue,        // Continue from the last save state (RetroArch)
    kItemMove = 100,      // + the tab's index
    kItemShelf = 250,     // + a built-in shelf's index in kShelves
    kItemPlayer = 300,    // 0: the default, then 1 + an index in library::players_for
    kItemPickPlace = 500, // + an index in pick_places_
    kItemPickDir = 1000,  // + an index in pick_dirs_
    kItemPickFile = 3000, // + an index in pick_files_
};

// The picture browser shows this many folders and pictures at a time: the
// menu does not scroll.
constexpr int kPickPage = 10;
constexpr std::size_t kPickMost = 1500; // entries read from one folder

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
    // Details… opens the title's page; Circle closes it.
    {0.4f, action_bit(Action::menu)},
    {0.3f, 0, Direction::down},
    {0.3f, action_bit(Action::confirm)},
    {0.9f, 0, Direction::none, "details"},
    {0.5f, action_bit(Action::back)},
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
    // Storage, next: the drives first (two invented ones on a PC), then the
    // folders.
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
    {0.3f, action_bit(Action::jump_next)},
    {0.2f, 0, Direction::right},
    {0.8f, 0, Direction::none, "settings-system"},
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
    // Last, as nothing after it may depend on the view: Cases, from the View
    // picker (left of the sort's; Default, Grid, then Cases), a PS4 case
    // focused, then a PS5 one two to the right.
    {0.6f, 0, Direction::up},
    {0.2f, 0, Direction::left},
    {0.3f, action_bit(Action::confirm)},
    {0.2f, 0, Direction::down},
    {0.2f, 0, Direction::down},
    {0.3f, action_bit(Action::confirm)},
    {0.3f, 0, Direction::down},
    {1.0f, 0, Direction::none, "cases"},
    {0.3f, 0, Direction::right},
    {0.3f, 0, Direction::right},
    {1.0f, 0, Direction::none, "cases-ps5"},
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
// A RomM game's file is on the console once it was played (downloaded).
bool game_file_here(const std::string &path)
{
    struct stat info = {};
    return !path.empty() && ::stat(path.c_str(), &info) == 0;
}

std::string describe(const Title &title, const std::string &emulator)
{
    if (title.is_rom() && !title.remote.empty() && !game_file_here(title.path))
        return library::short_system_name(title.system) + "  ·  " + tr("On RomM");
    if (title.is_rom())
        return library::short_system_name(title.system) + "  ·  " + emulator;
    std::string text = library::platform_name(title.platform);
    if (!text.empty())
        text += "  ·  ";
    text += title.source == library::Source::image       ? tr("Image")
            : title.source == library::Source::installed ? tr("Installed")
            : title.source == library::Source::payload   ? tr("ELF program")
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
        // ORION keeps everything in /data/orion. A title's sandbox has /data
        // only when ShadowMountPlus mounts it there (its sandbox watcher,
        // backport_fakelib); without it nothing can be saved or launched,
        // and the setup would come back on every start. Say so plainly.
        data_missing_ = sys::on_console() && (context_.library.state_dir.empty() ||
                                              !save::ensure_directory(context_.library.state_dir));
        if (data_missing_)
            sys::notify(tr("ORION cannot reach /data: update ShadowMountPlus, keep its "
                           "backport_fakelib on and turn BackPork off"));
        setup_pending_ = context_.library.offer_setup && !prefs_.setup_done && !data_missing_;
        restyle();
        // ORION reopens where it was left, when asked to.
        std::string start = prefs_.reopen_last_tab ? prefs_.last_tab : std::string("all");
        // A removal the launch helper did while ORION was closed.
        take_remove_result();
        take_update_result();
        tidy_update_folder();
        start_resume_watcher();
        read_hover_music();
        take_new_title_id();
        if (!own_title_id().empty())
            settings_.set_title_id(own_title_id());
        settings_.set_grid_key(!grid_key().empty());
        show_romm_state();
        // The key page: the SteamGridDB key typed on a phone or PC.
        if (sys::on_console() && !data_missing_ && !context_.library.state_dir.empty() &&
            key_page_.start(context_.library.state_dir))
        {
            const std::string address = orion::local_address();
            if (!address.empty())
                settings_.set_key_page("http://" + address + ":" +
                                       std::to_string(orion::kKeyPagePort));
        }
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
            registered_.start(state, ftp_port());
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
            // A copy made before is in the run too: it is fetched again when
            // the system's picture changed (orion/backdrop_copies.hpp).
            const bool copied = ::stat((folder + "/" + title.id + ".png").c_str(), &info) == 0;
            if (title.is_rom() ||
                (!copied && title.icon.find("/icon-cache/") == std::string::npos) ||
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
        return icon_copies_.start(std::move(ids), icon_folder(), ftp_port(), {"/icon0.png"});
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
    // "The system has none" is not for ever: a game just installed gets its
    // pictures a while later (a console showed two games marked so, their
    // pic0.png there the next day). An answer this old is asked again.
    static constexpr std::time_t kNoneFor = 12 * 60 * 60;

    // Whether <file>.none says "none" still: there, and younger than kNoneFor.
    static bool still_none(const std::string &file)
    {
        struct stat info = {};
        if (::stat(file.c_str(), &info) != 0)
            return false;
        if (std::time(nullptr) - info.st_mtime < kNoneFor)
            return true;
        std::remove(file.c_str());
        return false;
    }

    void request_backdrops()
    {
        if (context_.library.state_dir.empty() || backdrop_copies_.running())
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
            if (still_none(folder + "/" + title.id + ".none"))
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
        if (!ids.empty())
        {
            // ORION's payload copies them too, with no FTP server.
            std::string want;
            for (const std::string &id : ids)
                want += "pic0 " + id + "\n";
            save::write_atomic(context_.library.state_dir + "/system-want.txt", want);
        }
        if (!ids.empty() && orion::make_directories(folder) &&
            save::write_atomic(marker, "").empty())
            backdrop_copies_.start(std::move(ids), folder, ftp_port());
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
        // A message waiting (the new title ID), once nothing else is up.
        if (!info_title_.empty() && clock_ > 1.5f && !dialog_.is_open() && !setup_.is_open() &&
            !settings_.is_open() && !menu_.is_open() && typing_ == Typing::none)
        {
            ui::DialogContent content;
            content.icon = ui::StatusKind::info;
            content.title = std::exchange(info_title_, std::string());
            content.body = std::exchange(info_body_, std::string());
            content.buttons = {{tr("OK"), ui::ButtonKind::primary, false}};
            dialog_info_ = true;
            dialog_.open(std::move(content), feedback);
        }
        update_title_music(dt);
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
        poll_preview(feedback);
        if (dialog_.is_open())
            update_dialog(input, dt, feedback);
        else if (previewing())
            update_preview(input, feedback);
        else if (typing_ != Typing::none)
            update_typing(input, feedback);
        else if (menu_.is_open())
            update_menu(input, feedback);
        else if (romm_browser_.is_open())
            update_romm_browser(input, feedback);
        else if (whats_new_.is_open())
        {
            if (whats_new_.handle(input, feedback))
                whats_new_.close(feedback);
        }
        else if (details_.is_open())
            update_details(input, feedback);
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
        update_wide();
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
        details_.update(dt);
        whats_new_.update(dt);
        romm_browser_.update(dt);
        // After an update, once: what changed (orion/whats_new.hpp).
        if (!whats_new_checked_ && clock_ > 2.0f && !setup_.is_open() && !dialog_.is_open() &&
            info_title_.empty() && launch_pending_ <= 0.0f)
        {
            whats_new_checked_ = true;
            const orion::WhatsNewNotes notes = whats_new_notes();
            if (orion::show_whats_new(notes, context_.version, prefs_.whats_new_seen,
                                      prefs_.setup_done))
                whats_new_.open(notes, feedback);
            // Seen, or nothing to see (a new install): not asked again for it.
            if (!notes.version.empty() && prefs_.whats_new_seen != notes.version)
            {
                prefs_.whats_new_seen = notes.version;
                save_prefs();
            }
        }
        follow_drive_space();
        follow_system_play_time();
        follow_romm(feedback);
        if (key_page_.take_saved())
        {
            // Either form: the SteamGridDB key, or RomM's address and a code
            // to pair with (exchanged here, where the network calls are).
            const std::string &state = context_.library.state_dir;
            const std::string key = orion::read_service_key(state, orion::kSteamGridDb);
            const std::string code = orion::read_service_key(state, orion::kRommPairCode);
            show_romm_state();
            if (key != grid_key_)
            {
                grid_key_ = key;
                settings_.set_grid_key(!grid_key_.empty());
                if (settings_.is_open())
                    settings_.refresh(look_, prefs_);
                arm_grid();
                show_note(true, grid_key_.empty() ? tr("SteamGridDB key removed")
                                                  : tr("SteamGridDB key saved"));
                feedback.play(audio::Cue::saved);
            }
            else if (!code.empty())
            {
                orion::write_service_key(state, orion::kRommPairCode, "");
                pair_romm(code, feedback);
            }
            else
            {
                show_note(true, trf("RomM server: {0}. Now pair with it", {romm_base()}));
                feedback.play(audio::Cue::saved);
            }
        }
        last_dt_ = dt;
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
        // And over the RomM Games screen (its search).
        const bool over_settings = settings_.visible() || romm_browser_.is_open();
        if (keyboard_shown_.value > 0.01f && !over_settings)
            draw_keyboard(canvas, paint);
        else if (keyboard_shown_.value <= 0.01f)
            draw_hints(list, paint);
        draw_launch_card(list, paint);
        draw_hold_to_exit(list, paint);
        details_.draw(canvas);
        settings_.draw(canvas);
        whats_new_.draw(canvas);
        romm_browser_.draw(canvas);
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
        details_.set_theme(theme_, reduced_);
        whats_new_.set_theme(theme_, reduced_);
        romm_browser_.set_theme(theme_, reduced_);
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

    // A tab the player gave wide covers (Landscape) in the grid: console
    // titles there show their 16:9 home-screen picture, not the square icon.
    bool wide_tab() const
    {
        return view() == View::grid &&
               prefs_.tab_cover(active_key()) == orion::CoverShape::landscape;
    }

    // Each frame in a wide tab: the covers near the focus want their wide
    // copies (made once, orion/wide_copies.hpp) and wear them once loaded;
    // any other tab lets them go.
    void update_wide()
    {
        std::vector<std::string> wanted;
        std::vector<std::pair<std::string, std::string>> make;
        if (wide_tab() && grid_.count() > 0)
        {
            const std::string folder = context_.library.state_dir + "/wide";
            const int columns = std::max(1, grid_.style.columns);
            const int focus = grid_.focus();
            const int first = std::max(0, focus - focus % columns - 2 * columns);
            const int last = std::min(grid_.count(), focus + 4 * columns);
            for (int i = first; i < last; ++i)
            {
                ui::CardItem &item = grid_.item(i);
                const Title &title = titles()[static_cast<std::size_t>(item.tag)];
                if (title.is_rom())
                    continue;
                wanted.push_back(title.id);
                if (title.wide_texture != 0 && item.texture != title.wide_texture)
                {
                    // Cropped to the tile's shape from the middle, so it fills it
                    // (16:9 in a 7:5 tile loses a tenth at each side).
                    const float picture = static_cast<float>(title.wide_width) /
                                          static_cast<float>(std::max(1, title.wide_height));
                    const float tile = std::max(0.2f, grid_.style.card.art_aspect);
                    item.texture = title.wide_texture;
                    item.uv = gfx::kFullUv;
                    if (picture > tile)
                    {
                        const float keep = tile / picture;
                        item.uv.x = (1.0f - keep) * 0.5f;
                        item.uv.w = keep;
                    }
                    item.image_aspect = std::min(picture, tile);
                    item.bare = false;
                }
            }
            // Every title of the tab gets its copy (from its own folder, else
            // the copy of the system's picture the backgrounds use), so
            // scrolling meets them made; each title is looked at once.
            for (int i = 0; i < grid_.count() && !wide_copies_.running(); ++i)
            {
                const Title &title = titles()[static_cast<std::size_t>(grid_.item(i).tag)];
                if (title.is_rom() || !wide_asked_.insert(title.id).second)
                    continue;
                struct stat info = {};
                if (::stat((folder + "/" + title.id + ".tga").c_str(), &info) == 0 ||
                    still_none(folder + "/" + title.id + ".none"))
                    continue;
                for (const std::string &file : {title.path + "/sce_sys/pic0.png",
                                                backdrop_folder() + "/" + title.id + ".png"})
                {
                    if (::stat(file.c_str(), &info) == 0)
                    {
                        make.emplace_back(title.id, file);
                        break;
                    }
                }
            }
            if (!make.empty())
                wide_copies_.start(std::move(make), folder);
        }
        else if (!context_.wide_icons.empty())
        {
            // Back to the square icons.
            for (int i = 0; i < grid_.count(); ++i)
            {
                ui::CardItem &item = grid_.item(i);
                const Title &title = titles()[static_cast<std::size_t>(item.tag)];
                if (title.wide_texture != 0 && item.texture == title.wide_texture)
                {
                    item.texture = title.texture;
                    item.uv = gfx::kFullUv;
                    if (title.icon_width > 0 && title.icon_height > 0)
                        item.image_aspect = static_cast<float>(title.icon_width) /
                                            static_cast<float>(title.icon_height);
                }
            }
        }
        if (wanted != context_.wide_icons)
            context_.wide_icons = std::move(wanted);
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
            save::write_atomic(off, "ORION's autostart is off (Settings, System)\n");
        // The rest-mode watcher the same way: the file says "on".
        const std::string on = state + "/reopen-after-rest";
        if (prefs_.reopen_after_rest)
            save::write_atomic(on, "ORION opens after rest mode (Settings, System)\n");
        else
            std::remove(on.c_str());
    }

    // The rest-mode watcher (payload/resume): sent while the setting is on,
    // as ORION starts and when it is turned on. A second copy ends by itself.
    // It opens the title in title.txt, which follows ORION's own title ID.
    void start_resume_watcher() const
    {
        const std::string &state = context_.library.state_dir;
        const std::string self = own_title_id();
        if (!sys::on_console() || state.empty() || self.empty())
            return;
        save::write_atomic(state + "/title.txt", self + "\n");
        if (!prefs_.reopen_after_rest)
            return;
        save_prefs(); // the "on" file, for a watcher that wakes before any change
        std::string elf;
        if (!save::read_file(kResumePath, &elf, 1u << 20) || elf.empty())
            return;
        // On a thread of its own: a busy loader must not keep the library
        // from opening (it did: the first frame waited on the send).
        std::thread(
            [elf = std::move(elf), log = update_log_path()]()
            {
                const int error = sys::send_payload(elf, kLoaderPort);
                orion::update_log(log, error == 0 ? "orion: the rest-mode watcher is sent"
                                                  : "orion: could not send the rest-mode watcher "
                                                    "(errno " +
                                                        std::to_string(error) + ")");
            })
            .detach();
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
        else if (romm_download_.running())
            status = trf("Downloading {0} from RomM  ·  {1}",
                         {romm_downloading_name_, megabytes(romm_download_.written())});
        else if (art_.running())
        {
            const orion::ArtFetcher::Progress now = art_.progress();
            status = trf("Downloading box art  ·  {0} of {1}",
                         {std::to_string(now.done), std::to_string(now.total)});
            if (now.total > 0)
                progress = static_cast<float>(now.done) / static_cast<float>(now.total);
        }
        else if (romm_sync_.running())
        {
            const orion::RommSync::Progress now = romm_sync_.progress();
            status = now.total > 0 ? trf("Listing RomM's games  ·  {0} of {1}",
                                         {std::to_string(now.games), std::to_string(now.total)})
                                   : tr("Asking RomM for its games…");
            if (now.total > 0)
                progress =
                    std::min(1.0f, static_cast<float>(now.games) / static_cast<float>(now.total));
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
        // How full each drive is: the helper's last answer now, a fresh one
        // asked for (follow_drive_space shows it when it comes).
        show_drive_space();
        ask_drive_space();
        settings_.set_current_tab(active_key());
        settings_.open(look_, prefs_, feedback);
    }

    // The drives as the launch helper last saw them (drives-space.txt); on a
    // PC, the invented ones.
    void show_drive_space()
    {
        std::string report;
        struct stat info = {};
        const std::string file = context_.library.state_dir + "/drives-space.txt";
        if (sys::on_console() && ::stat(file.c_str(), &info) == 0)
        {
            drive_space_time_ = info.st_mtime;
            save::read_file(file, &report, 64u << 10);
        }
        settings_.set_drives(orion::find_drives(titles(), sys::on_console()
                                                              ? orion::space_from_report(report)
                                                              : orion::SpaceOf(preview_space)));
    }

    // The helper (payload/launcher, "@space") asks the system, which refuses
    // ORION: on a thread of its own, so a busy loader never holds Settings.
    void ask_drive_space()
    {
        const std::string &dir = context_.library.state_dir;
        std::string elf;
        if (!sys::on_console() || data_missing_ || dir.empty() || launch_pending_ > 0.0f ||
            !save::read_file(kLauncherPath, &elf, 4u << 20) || elf.empty() ||
            !save::write_atomic(dir + "/launch-request.txt", "@space\n").empty())
            return;
        drive_space_asked_ = clock_;
        std::thread([elf = std::move(elf)]() { sys::send_payload(elf, kLoaderPort); }).detach();
    }

    // The helper's answer, once it is written: Storage shows it.
    void follow_drive_space()
    {
        if (drive_space_asked_ < 0.0f || clock_ - drive_space_asked_ > 15.0f ||
            static_cast<int>(clock_ * 2.0f) == static_cast<int>((clock_ - last_dt_) * 2.0f))
            return;
        struct stat info = {};
        const std::string file = context_.library.state_dir + "/drives-space.txt";
        if (::stat(file.c_str(), &info) != 0 || info.st_mtime == drive_space_time_)
            return;
        drive_space_asked_ = -1.0f;
        show_drive_space();
        if (settings_.is_open() && settings_.section() == orion::SettingsPanel::Section::storage)
            settings_.refresh(look_, prefs_);
    }

    // The PC preview has no console drives: two invented ones, so its
    // pictures show the Drives rows.
    static bool preview_space(const std::string &path, orion::DriveSpace *space)
    {
        constexpr std::uint64_t kGb = 1000ull * 1000ull * 1000ull;
        if (path == "/data")
            *space = {825 * kGb, 312 * kGb};
        else if (path == "/mnt/usb1")
            *space = {2000 * kGb, 1460 * kGb};
        else
            return false;
        return true;
    }

    // Every tab ORION can show, in tab bar order, hidden ones included.
    std::vector<orion::TabEntry> every_tab() const
    {
        std::vector<orion::TabEntry> tabs;
        for (const Shelf &shelf : kShelves)
            tabs.push_back({shelf.key, tab_label(shelf.key, tr(shelf.name))});
        if (prefs_.group_systems &&
            (!context_.library.emulator.systems.empty() || !context_.library.emulator.apps.empty()))
            tabs.push_back({kEmulatorsTab, tab_label(kEmulatorsTab, tr("Emulators"))});
        for (const library::System &system : context_.library.emulator.systems)
            tabs.push_back(
                {"sys:" + system.name, tab_label("sys:" + system.name, system.short_name)});
        for (const orion::CustomTab &tab : prefs_.tabs)
            tabs.push_back({"tab:" + tab.id, tab.name});
        // Hidden too, while there is something on it: renamed or hidden like
        // the others.
        if (any_hidden())
            tabs.push_back({kHiddenTab, tab_label(kHiddenTab, tr("Hidden"))});
        std::vector<std::string> keys;
        for (const orion::TabEntry &tab : tabs)
            keys.push_back(tab.key);
        std::vector<orion::TabEntry> ordered;
        for (const std::string &key : prefs_.ordered_tabs(keys))
        {
            for (const orion::TabEntry &tab : tabs)
            {
                if (tab.key == key)
                    ordered.push_back(tab);
            }
        }
        return ordered;
    }

    bool any_hidden() const
    {
        return std::any_of(titles().begin(), titles().end(),
                           [&](const Title &t) { return prefs_.title_hidden(t.id); });
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
        // Titles that joined (installed while ORION ran) get their pictures.
        if (prefs_.game_backgrounds && sys::on_console())
            request_backdrops();
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
        return (self.empty() ? "/data/homebrew/" + own_title_id() : self) +
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
            if (prefs_.reopen_after_rest)
                start_resume_watcher();
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
            start_typing(Typing::rename_tab,
                         tab != nullptr ? tab->name : prefs_.tab_name(menu_tab_));
            feedback.play(audio::Cue::modal_open);
            break;
        }
        case orion::SettingsPanel::Change::art_folder:
            art_folder_changed();
            break;
        case orion::SettingsPanel::Change::type_romm_url:
            start_typing(Typing::romm_url,
                         orion::read_service_key(context_.library.state_dir, orion::kRommUrl));
            feedback.play(audio::Cue::modal_open);
            break;
        case orion::SettingsPanel::Change::type_romm_code:
            start_typing(Typing::romm_code, "");
            feedback.play(audio::Cue::modal_open);
            break;
        case orion::SettingsPanel::Change::romm_sync:
            sync_romm(feedback, true);
            break;
        case orion::SettingsPanel::Change::romm_browse:
            open_romm_browser(feedback);
            break;
        case orion::SettingsPanel::Change::type_grid_key:
            start_typing(Typing::grid_key, grid_key());
            feedback.play(audio::Cue::modal_open);
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
                                             : folder_emulator_ == "psswanstation" ? "/mnt/usb0/psx"
                                             : folder_emulator_ == "ps5x360" ? "/mnt/usb0/xbox360"
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
        case orion::SettingsPanel::Change::whats_new:
        {
            const orion::WhatsNewNotes notes = whats_new_notes();
            if (notes.items.empty())
            {
                show_note(false, tr("This version has no notes"));
                feedback.play(audio::Cue::error);
                break;
            }
            settings_.close(feedback);
            whats_new_.open(notes, feedback);
            break;
        }
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
        return typing == Typing::search ? kMaxQuery
               : typing == Typing::art_folder || typing == Typing::rom_folder ||
                       typing == Typing::grid_key || typing == Typing::romm_url ||
                       typing == Typing::romm_code
                   ? kMaxPath
               : typing == Typing::rename_title || typing == Typing::art_name ? orion::kMaxTitleName
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
        if (typing == Typing::art_name)
        {
            find_other_art(feedback, name_);
            return;
        }
        if (typing == Typing::romm_url)
        {
            set_romm_address(name_, feedback);
            return;
        }
        if (typing == Typing::romm_code)
        {
            pair_romm(name_, feedback);
            return;
        }
        if (typing == Typing::romm_search)
        {
            romm_browser_.set_query(name_);
            return;
        }
        if (typing == Typing::grid_key)
        {
            const std::string failed =
                orion::write_service_key(context_.library.state_dir, orion::kSteamGridDb, name_);
            grid_key_ = orion::read_service_key(context_.library.state_dir, orion::kSteamGridDb);
            const bool given = !grid_key_.empty();
            settings_.set_grid_key(given);
            settings_.refresh(look_, prefs_);
            if (!failed.empty())
                show_note(false, trf("Could not save the key: {0}", {failed}));
            else
                show_note(true,
                          given ? tr("SteamGridDB key saved") : tr("SteamGridDB key removed"));
            feedback.play(failed.empty() ? audio::Cue::saved : audio::Cue::error);
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
            if (prefs_.tab(menu_tab_) != nullptr)
                prefs_.rename_tab(menu_tab_, name_);
            else
                prefs_.set_tab_name(menu_tab_, name_); // a built-in tab; empty: its own name
            settings_.set_tabs(every_tab());
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

    // ---- the player's own picture for a title ----

    // ---- Find other art: SteamGridDB's covers, looked through on the tile ----

    bool previewing() const
    {
        return !preview_id_.empty() && !preview_files_.empty();
    }

    // query: the name to look for; empty, the title's own.
    void find_other_art(app::Feedback &feedback, const std::string &query = {})
    {
        const Title *title = find_title(menu_title_);
        std::string why;
        if (title == nullptr)
            return;
        if (!sys::on_console())
            why = tr("Pictures are downloaded on the console");
        else if (grid_key().empty())
            why = tr("Add your SteamGridDB key first: Settings, Box art");
        else if (!preview_id_.empty())
            why = tr("Still looking: try again in a moment");
        if (!why.empty())
        {
            show_note(false, why);
            feedback.play(audio::Cue::error);
            return;
        }
        preview_id_ = title->id;
        preview_name_ = title->name;
        preview_query_ = !orion::grid_query(query).empty() ? query
                         : title->own_name.empty()         ? title->name
                                                           : title->own_name;
        preview_was_ = title->icon;
        preview_files_.clear();
        preview_index_ = 0;
        // A search put back with Circle may still be ending its download:
        // this one starts as soon as it has (poll_preview).
        preview_asked_ = picker_.search(grid_key_, preview_query_,
                                        context_.library.state_dir + "/art/picks", title->id);
        show_note(true, trf("Looking on SteamGridDB for {0}…", {preview_query_}));
        feedback.play(audio::Cue::select);
    }

    // The notes ORION ships for its version (assets/whats-new.txt).
    static orion::WhatsNewNotes whats_new_notes()
    {
        std::string text;
        orion::WhatsNewNotes notes;
        if (save::read_file("/app0/assets/whats-new.txt", &text, 256u << 10))
            orion::parse_whats_new(text, &notes);
        return notes;
    }

    // ---- RomM (orion/romm.hpp) ----

    std::string romm_base() const
    {
        return orion::romm_base(
            orion::read_service_key(context_.library.state_dir, orion::kRommUrl));
    }

    std::string romm_token() const
    {
        return orion::read_service_key(context_.library.state_dir, orion::kRommToken);
    }

    static bool file_exists(const std::string &path)
    {
        struct stat info = {};
        return !path.empty() && ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
    }

    void show_romm_state()
    {
        settings_.set_romm(romm_base(), !romm_token().empty(), romm_status_);
        if (settings_.is_open())
            settings_.refresh(look_, prefs_);
    }

    void set_romm_address(const std::string &typed, app::Feedback &feedback)
    {
        const std::string base = orion::romm_base(typed);
        if (!typed.empty() && base.empty())
        {
            show_note(false, trf("{0} is not an address RomM can be reached at", {typed}));
            feedback.play(audio::Cue::error);
            return;
        }
        const std::string &state = context_.library.state_dir;
        // Another server: the old one's token means nothing there.
        if (base != romm_base())
            orion::write_service_key(state, orion::kRommToken, "");
        const std::string failed = orion::write_service_key(state, orion::kRommUrl, base);
        show_romm_state();
        show_note(failed.empty(), !failed.empty() ? trf("Could not save it: {0}", {failed})
                                  : base.empty()
                                      ? tr("RomM server removed")
                                      : trf("RomM server: {0}. Now pair with it", {base}));
        feedback.play(failed.empty() ? audio::Cue::saved : audio::Cue::error);
    }

    // The code exchanged for a token beside the screen; follow_romm takes the answer.
    void pair_romm(const std::string &code, app::Feedback &feedback)
    {
        const std::string base = romm_base();
        if (base.empty() || !sys::on_console() || romm_pairing_ != nullptr)
        {
            show_note(false, base.empty()         ? tr("Set the RomM server first")
                             : !sys::on_console() ? tr("Pairing is done on the console")
                                                  : tr("Still pairing: a moment"));
            feedback.play(audio::Cue::error);
            return;
        }
        auto pairing = std::make_shared<RommPairing>();
        romm_pairing_ = pairing;
        std::thread(
            [pairing, base, code]()
            {
                std::string token;
                std::string error = orion::romm_pair(
                    [](const std::string &url, const std::vector<std::string> &headers,
                       const std::string &body, std::size_t limit, std::string *answer,
                       std::string *why)
                    { return sys::http_post(url, headers, body, limit, answer, why); },
                    base, code, &token);
                {
                    std::lock_guard<std::mutex> lock(pairing->mutex);
                    pairing->token = std::move(token);
                    pairing->error = std::move(error);
                }
                pairing->done = true;
            })
            .detach();
        show_note(true, tr("Pairing with RomM…"));
        feedback.play(audio::Cue::select);
    }

    void sync_romm(app::Feedback &feedback, bool asked)
    {
        const std::string base = romm_base();
        const std::string token = romm_token();
        if (!sys::on_console() || base.empty() || token.empty() || romm_sync_.running())
        {
            if (asked)
            {
                show_note(false, base.empty()           ? tr("Set the RomM server first")
                                 : token.empty()        ? tr("Pair with RomM first")
                                 : romm_sync_.running() ? tr("Already syncing RomM")
                                                        : tr("RomM is synced on the console"));
                feedback.play(audio::Cue::error);
            }
            return;
        }
        romm_sync_.start(base, token, context_.library.state_dir);
        romm_status_ = tr("Asking RomM for its games…");
        show_romm_state();
        if (asked)
        {
            show_note(true, tr("Syncing RomM games…"));
            feedback.play(audio::Cue::select);
        }
    }

    void download_romm_game(const Title &title, app::Feedback &feedback)
    {
        if (romm_download_.running())
        {
            show_note(false, trf("Still downloading {0}", {romm_downloading_name_}));
            feedback.play(audio::Cue::error);
            return;
        }
        pause_art_for_romm();
        if (!sys::on_console() ||
            !romm_download_.start(title.remote, orion::romm_headers(romm_token()), title.path))
        {
            show_note(false, trf("Could not download {0}", {title.name}));
            feedback.play(audio::Cue::error);
            return;
        }
        romm_downloading_id_ = title.id;
        romm_downloading_name_ = title.name;
        romm_launch_after_ = true;
        show_note(true, trf("Downloading {0} from RomM…", {title.name}));
        feedback.play(audio::Cue::select);
    }

    // The RomM Games screen (orion/romm_browser.hpp), from what the last
    // sync listed and what the console has.
    void open_romm_browser(app::Feedback &feedback)
    {
        const std::string &state = context_.library.state_dir;
        std::string games;
        std::string saves;
        save::read_file(state + "/romm-games.tsv", &games, 32u << 20);
        save::read_file(state + "/romm-saves.tsv", &saves, 8u << 20);
        const library::OwnedGames owned(titles());
        settings_.close(feedback);
        romm_browser_.open(orion::romm_entries(orion::read_romm_catalogue(games),
                                               orion::read_romm_saves(saves),
                                               context_.library.emulator, owned, romm_base(),
                                               orion::romm_folder(state) + "/games"),
                           !saves.empty(), feedback);
    }

    void update_romm_browser(const InputFrame &input, app::Feedback &feedback)
    {
        switch (romm_browser_.handle(input, feedback))
        {
        case orion::RommBrowser::Choice::closed:
            romm_browser_.close(feedback);
            break;
        case orion::RommBrowser::Choice::download:
        {
            std::vector<orion::RommEntry> picked = romm_browser_.take_picked();
            show_note(true, i18n::plural(static_cast<long long>(picked.size()),
                                         "Downloading {0} game from RomM",
                                         "Downloading {0} games from RomM"));
            feedback.play(audio::Cue::select);
            for (orion::RommEntry &entry : picked)
            {
                if (find_title(library::rom_id(entry.path)) == nullptr)
                    romm_unlisted_.emplace_back(entry.path, entry.game.name);
                romm_queue_.push_back(std::move(entry));
            }
            break;
        }
        case orion::RommBrowser::Choice::search:
            start_typing(Typing::romm_search, romm_browser_.query());
            feedback.play(audio::Cue::modal_open);
            break;
        case orion::RommBrowser::Choice::none:
            break;
        }
    }

    // A game from RomM comes before box art: a box art run stops for it (a
    // few hundred covers would share the line with it) and starts again when
    // the downloads are done, from the games still without a picture.
    void pause_art_for_romm()
    {
        if (!art_.running())
            return;
        art_.stop();
        art_paused_for_romm_ = true;
    }

    // ---- saves between RomM and RetroArch (orion/romm_saves.hpp) ----

    // RetroArch's folder for a core's saves or states ("savefiles/mGBA"), or
    // empty when the core is not known.
    std::string retroarch_folder(const library::Player &player, const char *kind) const
    {
        if (player.app == nullptr)
            return {};
        for (const library::Core &core : context_.library.emulator.cores)
        {
            if (core.file == player.core && !core.name.empty())
                return player.app->root + "/" + kind + "/" + core.name;
        }
        return {};
    }

    // RomM's id for a game: a RomM game's own, else the one of the same file.
    long long romm_id_of(const Title &title)
    {
        if (const long long id = orion::romm_id_from_url(title.remote); id > 0)
            return id;
        if (romm_ids_.empty())
        {
            std::string text;
            save::read_file(context_.library.state_dir + "/romm-games.tsv", &text, 32u << 20);
            romm_ids_ = orion::romm_ids_by_file(orion::read_romm_catalogue(text));
        }
        std::string file = title.path.substr(title.path.rfind('/') + 1);
        for (char &c : file)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const auto found = romm_ids_.find(file);
        return found == romm_ids_.end() ? 0 : found->second;
    }

    // Before RetroArch starts a game: when RomM has a newer save of it than
    // the console (another device played on), that one is brought first and
    // the game starts after it; the save it replaces is kept beside it
    // (<save>.orion-backup). Once per game per run of ORION. True when a
    // download started (the launch comes back when it ends).
    bool pull_romm_save(const Title &title, const library::Player &player, app::Feedback &feedback)
    {
        if (!sys::on_console() || !romm_save_checked_.insert(title.id).second ||
            romm_download_.running())
            return false;
        const std::string base = romm_base();
        const std::string token = romm_token();
        const std::string folder = retroarch_folder(player, "savefiles");
        if (base.empty() || token.empty() || folder.empty())
            return false;
        const long long id = romm_id_of(title);
        std::string text;
        if (id <= 0 ||
            !save::read_file(context_.library.state_dir + "/romm-saves.tsv", &text, 8u << 20))
            return false;
        const std::vector<orion::RommSave> saves = orion::read_romm_saves(text);
        const orion::RommSave *save =
            orion::latest_romm_save(saves, id, orion::retroarch_core_key(player.core), false);
        if (save == nullptr)
            return false;
        const std::string target = folder + "/" + orion::retroarch_stem(player.rom) + ".srm";
        struct stat info = {};
        const bool here = ::stat(target.c_str(), &info) == 0;
        // The console's is as new (a minute either way): nothing to bring.
        if (here &&
            static_cast<std::int64_t>(info.st_mtime) + 60 >= orion::parse_date_time(save->updated))
            return false;
        if (here)
            std::rename(target.c_str(), (target + ".orion-backup").c_str());
        pause_art_for_romm();
        if (!romm_download_.start(orion::romm_save_content_url(base, *save),
                                  orion::romm_headers(token), target))
        {
            if (here)
                std::rename((target + ".orion-backup").c_str(), target.c_str());
            return false;
        }
        romm_downloading_id_ = title.id;
        romm_downloading_name_ = title.name;
        romm_launch_after_ = true;
        romm_save_target_ = target;
        show_note(true, trf("Bringing {0}'s save from RomM…", {title.name}));
        feedback.play(audio::Cue::select);
        return true;
    }

    // The game's newest save state, for Continue; empty when it has none.
    std::string continue_state(const Title &title)
    {
        if (!title.is_rom())
            return {};
        const library::Player player = player_of(title);
        if (player.app == nullptr || player.app->key != "retroarch")
            return {};
        const std::string folder = retroarch_folder(player, "savestates");
        return folder.empty() ? std::string()
                              : orion::newest_state(folder, orion::retroarch_stem(player.rom));
    }

    // Continue: the newest state as the one RetroArch loads by itself at the
    // start (<stem>.state.auto, written where it stays), and a config for
    // this start only that turns that loading on (put in place by the helper).
    bool prepare_continue(const library::Player &player, std::string *request)
    {
        const std::string folder = retroarch_folder(player, "savestates");
        const std::string stem = orion::retroarch_stem(player.rom);
        const std::string newest =
            folder.empty() ? std::string() : orion::newest_state(folder, stem);
        if (newest.empty())
            return false;
        const std::string automatic = folder + "/" + stem + ".state.auto";
        if (newest != automatic)
        {
            std::string bytes;
            if (!save::read_file(newest, &bytes, 256u << 20) ||
                !save::write_atomic(automatic, bytes).empty())
                return false;
        }
        const std::string staged = context_.library.state_dir + "/retroarch-continue.cfg";
        if (!write_shared(staged, "savestate_auto_load = \"true\"\n"))
            return false;
        *request += "@install " + staged + "\t" + player.app->root + "/orion-continue.cfg\n";
        return true;
    }

    // The picked games, one after another (a launch's download first).
    void follow_romm_queue()
    {
        if (romm_queue_.empty() || romm_download_.running() || !sys::on_console())
            return;
        const orion::RommEntry entry = romm_queue_.front();
        romm_queue_.pop_front();
        pause_art_for_romm();
        if (romm_download_.start(entry.url, orion::romm_headers(romm_token()), entry.path))
        {
            romm_downloading_id_.clear();
            romm_downloading_name_ = entry.game.name;
            romm_launch_after_ = false;
            ++romm_queued_done_;
        }
    }

    // Pairing's answer, the sync's progress and a download's end.
    void follow_romm(app::Feedback &feedback)
    {
        // At start, a little after the rest, while paired: a sync left
        // unfinished goes on (orion/romm.hpp keeps its progress), and a list
        // a day old is read again. A large library is not read on every start.
        if (!romm_auto_synced_ && clock_ > 12.0f && launch_pending_ <= 0.0f)
        {
            romm_auto_synced_ = true;
            const std::string &state = context_.library.state_dir;
            struct stat info = {};
            const bool unfinished = ::stat((state + "/romm-sync-done.txt").c_str(), &info) == 0;
            const bool stale = ::stat((state + "/romm-games.tsv").c_str(), &info) != 0 ||
                               std::time(nullptr) - info.st_mtime > 24 * 3600;
            if (unfinished || stale)
                sync_romm(feedback, false);
            // Else the saves alone: a save made on another device shows today.
            else if (!romm_base().empty() && !romm_token().empty() && sys::on_console())
                romm_sync_.start(romm_base(), romm_token(), state, true);
        }
        if (romm_pairing_ != nullptr && romm_pairing_->done.load())
        {
            std::string token;
            std::string error;
            {
                std::lock_guard<std::mutex> lock(romm_pairing_->mutex);
                token = romm_pairing_->token;
                error = romm_pairing_->error;
            }
            romm_pairing_.reset();
            if (error.empty())
                error =
                    orion::write_service_key(context_.library.state_dir, orion::kRommToken, token);
            show_romm_state();
            show_note(error.empty(), error.empty() ? tr("Paired with RomM")
                                                   : trf("Could not pair with RomM: {0}", {error}));
            feedback.play(error.empty() ? audio::Cue::saved : audio::Cue::error);
            if (error.empty())
                sync_romm(feedback, false);
        }
        if (romm_sync_.running() && !romm_sync_.progress().saves_only)
        {
            const orion::RommSync::Progress now = romm_sync_.progress();
            romm_status_ =
                now.resumed > 0
                    ? trf("{0} games on {1} systems so far, going on from {2} read before…",
                          {std::to_string(now.games), std::to_string(now.platforms),
                           std::to_string(now.resumed)})
                    : trf("{0} games on {1} systems so far…",
                          {std::to_string(now.games), std::to_string(now.platforms)});
            if (settings_.is_open() &&
                static_cast<int>(clock_ * 2.0f) != static_cast<int>((clock_ - last_dt_) * 2.0f))
                show_romm_state();
        }
        if (romm_sync_.take_finished())
        {
            const orion::RommSync::Progress done = romm_sync_.progress();
            // A token without the right to read saves gets 401 or 403.
            const bool refused = done.error.find("HTTP 401") != std::string::npos ||
                                 done.error.find("HTTP 403") != std::string::npos;
            const std::string saves =
                done.saves >= 0 ? i18n::plural(done.saves, "{0} save", "{0} saves") : std::string();
            if (done.saves_only)
                romm_status_ = !done.error.empty()
                                   ? trf("RomM saves could not be read: {0}", {done.error})
                                   : trf("{0} on RomM", {saves});
            else
                romm_status_ =
                    done.error.empty()
                        ? trf("Last sync: {0} games on {1} systems, {2}",
                              {std::to_string(done.games), std::to_string(done.platforms), saves})
                        : trf("Last sync failed: {0}", {done.error});
            if (refused)
                romm_status_ += "  ·  " + tr("Pair again with a token that may read saves "
                                             "(RomM: Client tokens, the assets.read permission)");
            show_romm_state();
            if (done.error.empty() && !done.saves_only)
                context_.rescan_requested = true; // the library lists them again
            else if (!done.error.empty())
                show_note(false, trf("RomM: {0}", {done.error}));
        }
        if (romm_download_.running() &&
            static_cast<int>(clock_ * 2.0f) != static_cast<int>((clock_ - last_dt_) * 2.0f))
            show_note(true,
                      trf("Downloading {0} from RomM: {1}",
                          {romm_downloading_name_, orion::format_bytes(romm_download_.written())}));
        if (romm_download_.take_finished())
        {
            const std::string error = romm_download_.error();
            const std::string save = std::exchange(romm_save_target_, std::string());
            if (!error.empty())
            {
                show_note(false,
                          trf("Could not download {0}: {1}", {romm_downloading_name_, error}));
                feedback.play(audio::Cue::error);
                // A save that did not come: the console's own is put back,
                // and the game starts with it.
                if (!save.empty())
                {
                    std::rename((save + ".orion-backup").c_str(), save.c_str());
                    launch(romm_downloading_id_, feedback);
                }
            }
            else if (romm_launch_after_)
                launch(romm_downloading_id_, feedback);
            // The last picked game in. A game the library lists already plays
            // from where it landed (its file is there now): no rebuild, which
            // on a large library was heavy enough to end ORION. One it does
            // not list yet (past its cap) needs one.
            if (romm_queue_.empty() && romm_queued_done_ > 0)
            {
                show_note(true, i18n::plural(romm_queued_done_, "{0} game downloaded from RomM",
                                             "{0} games downloaded from RomM"));
                feedback.play(audio::Cue::saved);
                romm_queued_done_ = 0;
                for (auto &game : std::exchange(romm_unlisted_, {}))
                {
                    struct stat info = {};
                    if (::stat(game.first.c_str(), &info) == 0)
                        context_.downloaded_games.push_back(std::move(game));
                }
                rebuild(); // their subtitles: no longer "On RomM"
            }
        }
        follow_romm_queue();
        if (art_paused_for_romm_ && romm_queue_.empty() && !romm_download_.running() &&
            !art_.running())
        {
            art_paused_for_romm_ = false;
            if (prefs_.auto_art)
                start_art(false);
        }
    }

    struct RommPairing
    {
        std::atomic<bool> done{false};
        std::mutex mutex;
        std::string token;
        std::string error;
    };

    // The console's FTP server: where the last look for installed titles
    // found one (orion/registered_titles.hpp), else the usual 2121.
    int ftp_port() const
    {
        return orion::ftp_port_of(context_.library.state_dir, kFtpPort);
    }

    // The player's SteamGridDB key, as keys.txt last gave it.
    std::string grid_key()
    {
        const std::string read =
            orion::read_service_key(context_.library.state_dir, orion::kSteamGridDb);
        if (!read.empty())
            grid_key_ = read;
        return grid_key_;
    }

    // The covers as they arrive; the first one shows on the tile at once.
    void poll_preview(app::Feedback &feedback)
    {
        if (preview_id_.empty())
            return;
        if (!preview_asked_)
        {
            if (!picker_.busy())
                preview_asked_ =
                    picker_.search(grid_key_, preview_query_,
                                   context_.library.state_dir + "/art/picks", preview_id_);
            return;
        }
        for (auto &[index, path] : picker_.take_ready())
        {
            (void)index;
            preview_files_.push_back(path);
            if (preview_files_.size() == 1)
                context_.new_icons.emplace_back(preview_id_, path);
        }
        if (!picker_.take_finished())
        {
            if (previewing())
                keep_preview_note();
            return;
        }
        if (keeping_)
        {
            // A cover kept (or its download failed): the preview ends.
            const std::string kept = picker_.kept();
            context_.new_icons.emplace_back(preview_id_, preview_was_);
            if (!kept.empty())
            {
                context_.custom_art_changed = true;
                show_note(true, trf("New picture for {0}", {preview_name_}));
                feedback.play(audio::Cue::saved);
            }
            else
            {
                show_note(false, trf("Could not get box art: {0}", {picker_.error()}));
                feedback.play(audio::Cue::error);
            }
            end_preview();
            return;
        }
        if (preview_files_.empty())
        {
            show_note(false, picker_.error().empty()
                                 ? trf("SteamGridDB has no covers for {0}: try Find art by name…",
                                       {preview_query_})
                                 : trf("Could not get box art: {0}", {picker_.error()}));
            feedback.play(audio::Cue::error);
            end_preview();
        }
    }

    void keep_preview_note()
    {
        launch_ok_ = true;
        launch_message_ =
            keeping_
                ? tr("Downloading the cover…")
                : trf("Cover {0} of {1}  ·  Left and Right to look, Cross keeps "
                      "it, Circle puts the old one back",
                      {std::to_string(preview_index_ + 1), std::to_string(preview_files_.size())});
        launch_shown_ = 2.0f;
    }

    void update_preview(const InputFrame &input, app::Feedback &feedback)
    {
        if (keeping_)
            return;
        if (input.nav == Direction::left || input.nav == Direction::right)
        {
            const int step = input.nav == Direction::right ? 1 : -1;
            const int count = static_cast<int>(preview_files_.size());
            const int next = preview_index_ + step;
            if (next < 0 || next >= count)
            {
                if (!input.nav_repeat)
                    feedback.play(audio::Cue::error);
                return;
            }
            preview_index_ = next;
            context_.new_icons.emplace_back(preview_id_,
                                            preview_files_[static_cast<std::size_t>(next)]);
            feedback.play(audio::Cue::focus);
            keep_preview_note();
            return;
        }
        if (input.is_pressed(Action::confirm))
        {
            // The full cover, under the title's ID in the player's pictures.
            const std::string folder = custom_art_folder();
            save::ensure_directory(folder);
            for (const char *old : {".png", ".jpg", ".jpeg"})
                std::remove((folder + "/" + preview_id_ + old).c_str());
            const std::string file = preview_files_[static_cast<std::size_t>(preview_index_)];
            const std::size_t dash = file.rfind('-');
            const std::size_t cover = static_cast<std::size_t>(std::atoi(file.c_str() + dash + 1));
            if (picker_.keep(cover, folder + "/" + preview_id_))
            {
                keeping_ = true;
                keep_preview_note();
                feedback.play(audio::Cue::select);
            }
            return;
        }
        if (input.is_pressed(Action::back))
        {
            context_.new_icons.emplace_back(preview_id_, preview_was_);
            picker_.cancel();
            end_preview();
            feedback.play(audio::Cue::back);
        }
    }

    void end_preview()
    {
        preview_id_.clear();
        preview_files_.clear();
        preview_asked_ = false;
        keeping_ = false;
        launch_shown_ = std::min(launch_shown_, 4.0f);
    }

    // SteamGridDB for the box art downloader: the player's key, as it is now.
    void arm_grid()
    {
        art_.set_grid_source(grid_key(), grid_http());
    }

    static orion::HttpGetWith grid_http()
    {
        return [](const std::string &url, const std::vector<std::string> &headers,
                  std::size_t limit, std::string *body, std::string *error)
        { return sys::http_get(url, headers, limit, body, error); };
    }

    std::string custom_art_folder() const
    {
        return context_.library.state_dir + "/custom-art";
    }

    // Choose picture…: the places, then folders and pictures, a page at a
    // time in the title's menu (pick_dir_ empty: the places).
    void open_picture_menu(app::Feedback &feedback)
    {
        std::vector<ui::MenuItem> items;
        const auto add = [&](std::string label, int tag, bool disabled = false)
        {
            ui::MenuItem item = menu_item(std::move(label));
            item.tag = tag;
            item.disabled = disabled;
            items.push_back(item);
        };
        pick_dirs_.clear();
        pick_files_.clear();
        if (pick_dir_.empty())
        {
            save::ensure_directory(custom_art_folder());
            pick_places_.clear();
            if (picking_music_)
            {
                save::ensure_directory(hover_music_folder());
                pick_places_.emplace_back(tr("Your music"), hover_music_folder());
                pick_places_.emplace_back(tr("Your songs"), context_.library.state_dir + "/music");
            }
            else
                pick_places_.emplace_back(tr("Your pictures"), custom_art_folder());
            for (int i = 0; i < 8; ++i)
                pick_places_.emplace_back(trf("USB drive {0}", {std::to_string(i + 1)}),
                                          "/mnt/usb" + std::to_string(i));
            pick_places_.emplace_back(tr("Extended storage"), "/mnt/ext0");
            pick_places_.emplace_back(tr("The console's storage"), "/data");
            if (!sys::on_console())
                pick_places_.emplace_back(tr("This folder (PC preview)"), ".");
            add(picking_music_ ? tr("Choose music") : tr("Choose a picture"), 0, true);
            for (std::size_t i = 0; i < pick_places_.size(); ++i)
            {
                struct stat info = {};
                if (::stat(pick_places_[i].second.c_str(), &info) == 0 && S_ISDIR(info.st_mode))
                    add(pick_places_[i].first, kItemPickPlace + static_cast<int>(i));
            }
        }
        else
        {
            for (const save::DirectoryEntry &entry : save::list_directory(pick_dir_))
            {
                if (entry.name.empty() || entry.name.front() == '.' ||
                    pick_dirs_.size() + pick_files_.size() >= kPickMost)
                    continue;
                std::string lower = entry.name;
                for (char &c : lower)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                const auto ends = [&](std::string_view end)
                {
                    return lower.size() > end.size() &&
                           lower.compare(lower.size() - end.size(), end.size(), end) == 0;
                };
                if (entry.directory)
                    pick_dirs_.push_back(entry.name);
                else if (picking_music_ ? ends(".ogg") || ends(".at9")
                                        : ends(".png") || ends(".jpg") || ends(".jpeg"))
                    pick_files_.push_back(entry.name);
            }
            std::sort(pick_dirs_.begin(), pick_dirs_.end());
            std::sort(pick_files_.begin(), pick_files_.end());
            add(pick_dir_, 0, true);
            add(tr("Up one folder"), kItemPickUp);
            const int total = static_cast<int>(pick_dirs_.size() + pick_files_.size());
            pick_page_ = std::clamp(pick_page_, 0, std::max(0, (total - 1) / kPickPage));
            if (pick_page_ > 0)
                add(tr("Back…"), kItemPickLess);
            for (int i = pick_page_ * kPickPage; i < std::min(total, (pick_page_ + 1) * kPickPage);
                 ++i)
            {
                const std::size_t index = static_cast<std::size_t>(i);
                if (index < pick_dirs_.size())
                    add(pick_dirs_[index] + "/", kItemPickDir + i);
                else
                {
                    const int file = i - static_cast<int>(pick_dirs_.size());
                    add(pick_files_[static_cast<std::size_t>(file)], kItemPickFile + file);
                }
            }
            if ((pick_page_ + 1) * kPickPage < total)
                add(tr("More…"), kItemPickMore);
            if (total == 0)
                add(picking_music_ ? tr("No music or folders here")
                                   : tr("No pictures or folders here"),
                    0, true);
        }
        menu_.set_items(std::move(items));
        menu_.open(grid_.cell_rect(grid_.focus()), feedback);
    }

    // ---- a title's music, as it plays while the title is focused ----

    std::string hover_music_folder() const
    {
        return context_.library.state_dir + "/hover-music";
    }

    // The player's music for a title: <ID>.ogg/.at9 in hover-music, else one
    // named after it (art_key folding, as custom art). Empty when none.
    std::string custom_music(const Title &title) const
    {
        const std::string folder = hover_music_folder();
        for (const char *extension : {".ogg", ".at9"})
        {
            struct stat info = {};
            const std::string path = folder + "/" + title.id + extension;
            if (::stat(path.c_str(), &info) == 0)
                return path;
        }
        const std::string key = library::art_key(title.name);
        const auto found = hover_names_.find(key);
        return key.empty() || found == hover_names_.end() ? std::string() : found->second;
    }

    // The player's songs by their folded names, read when the folder changes.
    void read_hover_music()
    {
        hover_names_.clear();
        for (const save::DirectoryEntry &entry : save::list_directory(hover_music_folder()))
        {
            const std::size_t dot = entry.name.rfind('.');
            if (entry.directory || dot == std::string::npos)
                continue;
            std::string extension = entry.name.substr(dot);
            for (char &c : extension)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (extension == ".ogg" || extension == ".at9")
                hover_names_.emplace(library::art_key(entry.name.substr(0, dot)),
                                     hover_music_folder() + "/" + entry.name);
        }
        music_paths_.clear();
    }

    // What a title plays when focused: the player's music, else its own
    // (snd0.at9: in its folder, or the system's copy fetched over FTP into
    // <state>/sounds). Empty when it has none yet.
    std::string title_music_for(const Title &title)
    {
        if (const auto known = music_paths_.find(title.id); known != music_paths_.end())
            return known->second;
        std::string path = custom_music(title);
        if (path.empty() && !title.is_rom())
        {
            const std::string folder = context_.library.state_dir + "/sounds";
            struct stat info = {};
            const auto exists = [&](const std::string &file)
            { return ::stat(file.c_str(), &info) == 0; };
            if (exists(folder + "/" + title.id + ".at9"))
                path = folder + "/" + title.id + ".at9";
            else if (exists(title.path + "/sce_sys/snd0.at9"))
                path = title.path + "/sce_sys/snd0.at9";
            else if (!still_none(folder + "/" + title.id + ".none"))
            {
                // Asked of the system once; the answer comes on a later frame.
                if (sys::on_console() && !sound_copies_.running() &&
                    sound_copies_.start({title.id}, folder, ftp_port(), {"/snd0.at9"}, ".at9"))
                    orion::update_log(context_.library.state_dir + "/title-music.txt",
                                      "asking the system for " + title.id + "'s music");
                return {};
            }
        }
        if (path.empty())
            orion::update_log(context_.library.state_dir + "/title-music.txt",
                              "no music for " + title.id + " (" + title.path + ")");
        music_paths_[title.id] = path;
        return path;
    }

    // The focused title's music, once the focus has rested on it a moment.
    void update_title_music(float dt)
    {
        // Over Settings (R3 on a title), a menu or the keyboard the song
        // playing goes on: they are about that title. The setup and the
        // cover preview have their own sound.
        if (prefs_.title_music && (settings_.is_open() || menu_.is_open() || dialog_.is_open() ||
                                   details_.is_open() || typing_ != Typing::none))
            return;
        std::string want;
        if (prefs_.title_music && !view_.empty() && !setup_.is_open() && !previewing())
        {
            const Title &title = focused();
            if (title.id != music_focus_)
            {
                music_focus_ = title.id;
                music_dwell_ = 0.0f;
            }
            music_dwell_ += dt;
            if (music_dwell_ > 0.8f)
                want = title_music_for(title);
        }
        else
            music_focus_.clear();
        context_.title_music = want;
    }

    void choose_music(const std::string &file, app::Feedback &feedback)
    {
        const Title *title = find_title(menu_title_);
        std::string bytes;
        if (title == nullptr || !save::read_file(file, &bytes, 32u << 20) || bytes.empty())
        {
            show_note(false, trf("Could not read {0}", {file}));
            feedback.play(audio::Cue::error);
            return;
        }
        std::string extension = file.substr(file.rfind('.'));
        for (char &c : extension)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const std::string folder = hover_music_folder();
        save::ensure_directory(folder);
        for (const char *old : {".ogg", ".at9"})
            std::remove((folder + "/" + title->id + old).c_str());
        const std::string failed = save::write_atomic(folder + "/" + title->id + extension, bytes);
        if (!failed.empty())
        {
            show_note(false, trf("Could not save the music: {0}", {failed}));
            feedback.play(audio::Cue::error);
            return;
        }
        read_hover_music();
        show_note(true, trf("New music for {0}", {title->name}));
        feedback.play(audio::Cue::saved);
    }

    void use_own_music(app::Feedback &feedback)
    {
        const Title *title = find_title(menu_title_);
        if (title == nullptr)
            return;
        for (const char *old : {".ogg", ".at9"})
            std::remove((hover_music_folder() + "/" + title->id + old).c_str());
        read_hover_music();
        const std::string left = custom_music(*title);
        if (!left.empty())
        {
            show_note(false, trf("{0} plays {1}: remove that file to hear its own music",
                                 {title->name, left}));
            feedback.play(audio::Cue::error);
            return;
        }
        show_note(true, trf("{0} has its own music again", {title->name}));
        feedback.play(audio::Cue::select);
    }

    // The chosen file becomes the title's picture: copied into the player's
    // pictures as <ID>.<its extension>, which wins over a picture named after
    // the title (library::apply_custom_art).
    void choose_picture(const std::string &file, app::Feedback &feedback)
    {
        const Title *title = find_title(menu_title_);
        std::string bytes;
        if (title == nullptr || !save::read_file(file, &bytes, 32u << 20) || bytes.empty())
        {
            show_note(false, trf("Could not read {0}", {file}));
            feedback.play(audio::Cue::error);
            return;
        }
        std::string extension = file.substr(file.rfind('.'));
        for (char &c : extension)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const std::string folder = custom_art_folder();
        save::ensure_directory(folder);
        for (const char *old : {".png", ".jpg", ".jpeg"})
            std::remove((folder + "/" + title->id + old).c_str());
        const std::string failed = save::write_atomic(folder + "/" + title->id + extension, bytes);
        if (!failed.empty())
        {
            show_note(false, trf("Could not save the picture: {0}", {failed}));
            feedback.play(audio::Cue::error);
            return;
        }
        context_.custom_art_changed = true;
        show_note(true, trf("New picture for {0}", {title->name}));
        feedback.play(audio::Cue::saved);
    }

    // Use its own picture: the title's file in the player's pictures goes. A
    // picture named after the game (not its ID) is the player's file: said,
    // not deleted.
    void use_own_picture(app::Feedback &feedback)
    {
        const Title *title = find_title(menu_title_);
        if (title == nullptr)
            return;
        const std::string folder = custom_art_folder();
        for (const char *old : {".png", ".jpg", ".jpeg"})
            std::remove((folder + "/" + title->id + old).c_str());
        const std::string shown = title->icon;
        context_.custom_art_changed = true;
        if (shown.compare(0, folder.size() + 1, folder + "/") == 0 &&
            shown.compare(folder.size() + 1, title->id.size() + 1, title->id + ".") != 0)
        {
            show_note(false, trf("{0} shows {1}: remove that file to see its own picture",
                                 {title->name, shown}));
            feedback.play(audio::Cue::error);
            return;
        }
        show_note(true, trf("{0} has its own picture again", {title->name}));
        feedback.play(audio::Cue::select);
    }

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
        if (!continue_state(title).empty())
        {
            ui::MenuItem resume = menu_item(tr("Continue from the last save state"));
            resume.tag = kItemContinue;
            items.push_back(resume);
        }
        ui::MenuItem details = menu_item(tr("Details…"));
        details.tag = kItemDetails;
        items.push_back(details);
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
        ui::MenuItem picture = menu_item(tr("Choose picture…"));
        picture.tag = kItemPicture;
        items.push_back(picture);
        ui::MenuItem other = menu_item(tr("Find other art…"));
        other.tag = kItemFindArt;
        other.shortcut = "SteamGridDB";
        items.push_back(other);
        ui::MenuItem by_name = menu_item(tr("Find art by name…"));
        by_name.tag = kItemFindArtName;
        items.push_back(by_name);
        if (!title.own_icon.empty())
        {
            ui::MenuItem own = menu_item(tr("Use its own picture"));
            own.tag = kItemOwnPicture;
            items.push_back(own);
        }
        ui::MenuItem music = menu_item(tr("Choose music…"));
        music.tag = kItemMusic;
        items.push_back(music);
        if (!custom_music(title).empty())
        {
            ui::MenuItem own = menu_item(tr("Use its own music"));
            own.tag = kItemOwnMusic;
            items.push_back(own);
        }
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
        // One item, whatever the number of tabs: the menu does not scroll, and
        // an item per tab pushed the list off the screen. It opens them.
        ui::MenuItem move =
            menu_item(prefs_.tabs.empty() ? tr("Move to a new tab…") : tr("Move to tab…"));
        move.tag = prefs_.tabs.empty() ? kItemNewTab : kItemMoveMenu;
        move.shortcut = placed != nullptr ? placed->name : "";
        items.push_back(move);
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
        // An ELF program is a file the player put there: they take it away.
        if (title.source == library::Source::payload)
            return false;
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
        if (dialog_info_)
        {
            dialog_.handle(input, feedback);
            if (!dialog_.is_open())
                dialog_info_ = false;
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
    // Move to tab…: the player's tabs (the one it is on marked Here), then
    // New tab…, in the place of the title's menu. At most a page of them: the
    // menu does not scroll.
    void open_tab_menu(app::Feedback &feedback)
    {
        const orion::CustomTab *placed = prefs_.placement(menu_title_);
        std::vector<ui::MenuItem> items;
        // The built-in shelves first: a homebrew app can sit with the games.
        const Title *title = find_title(menu_title_);
        std::string shelf_now = prefs_.placed_shelf(menu_title_);
        if (shelf_now.empty() && placed == nullptr && title != nullptr)
            shelf_now = shelf_key_of(*title);
        for (std::size_t i = 0; i < std::size(kShelves); ++i)
        {
            const std::string key = kShelves[i].key;
            if (key != "games" && key != "homebrew" && key != "media")
                continue;
            const bool here = key == shelf_now;
            ui::MenuItem item = menu_item(tab_label(key, tr(kShelves[i].name)));
            item.tag = kItemShelf + static_cast<int>(i);
            item.disabled = here;
            item.shortcut = here ? tr("Here") : "";
            items.push_back(item);
        }
        for (std::size_t i = 0; i < prefs_.tabs.size() && i < kPickPage; ++i)
        {
            const bool here = placed != nullptr && placed->id == prefs_.tabs[i].id;
            ui::MenuItem item = menu_item(prefs_.tabs[i].name);
            item.tag = kItemMove + static_cast<int>(i);
            item.disabled = here;
            item.shortcut = here ? tr("Here") : "";
            items.push_back(item);
        }
        ui::MenuItem line;
        line.separator = true;
        items.push_back(line);
        ui::MenuItem fresh = menu_item(tr("New tab…"));
        fresh.tag = kItemNewTab;
        items.push_back(fresh);
        menu_.set_items(std::move(items));
        menu_.open(grid_.cell_rect(grid_.focus()), feedback);
    }

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
        if (tag == kItemDetails)
        {
            open_details(menu_title_, feedback);
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
        if (tag == kItemMoveMenu)
        {
            open_tab_menu(feedback);
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
        if (tag == kItemPicture || tag == kItemMusic)
        {
            picking_music_ = tag == kItemMusic;
            pick_dir_.clear();
            pick_page_ = 0;
            open_picture_menu(feedback);
            return;
        }
        if (tag == kItemOwnMusic)
        {
            use_own_music(feedback);
            return;
        }
        if (tag == kItemOwnPicture)
        {
            use_own_picture(feedback);
            return;
        }
        if (tag == kItemFindArt)
        {
            find_other_art(feedback);
            return;
        }
        if (tag == kItemContinue)
        {
            continue_next_ = true;
            launch(menu_title_, feedback);
            continue_next_ = false;
            return;
        }
        if (tag == kItemFindArtName)
        {
            const Title *title = find_title(menu_title_);
            if (title == nullptr)
                return;
            start_typing(Typing::art_name, title->own_name.empty() ? title->name : title->own_name);
            feedback.play(audio::Cue::modal_open);
            return;
        }
        if (tag == kItemPickUp || tag == kItemPickMore || tag == kItemPickLess ||
            (tag >= kItemPickPlace && tag < kItemPickDir) ||
            (tag >= kItemPickDir && tag < kItemPickFile))
        {
            if (tag == kItemPickMore)
                ++pick_page_;
            else if (tag == kItemPickLess)
                pick_page_ = std::max(0, pick_page_ - 1);
            else
            {
                pick_page_ = 0;
                if (tag == kItemPickUp)
                {
                    const bool place =
                        std::any_of(pick_places_.begin(), pick_places_.end(),
                                    [&](const auto &p) { return p.second == pick_dir_; });
                    const std::size_t slash = pick_dir_.rfind('/');
                    pick_dir_ = place || slash == std::string::npos || slash == 0
                                    ? std::string()
                                    : pick_dir_.substr(0, slash);
                }
                else if (tag < kItemPickDir)
                    pick_dir_ = pick_places_[static_cast<std::size_t>(tag - kItemPickPlace)].second;
                else
                    pick_dir_ += "/" + pick_dirs_[static_cast<std::size_t>(tag - kItemPickDir)];
            }
            open_picture_menu(feedback);
            return;
        }
        if (tag >= kItemPickFile)
        {
            const std::string file =
                pick_dir_ + "/" + pick_files_[static_cast<std::size_t>(tag - kItemPickFile)];
            if (picking_music_)
                choose_music(file, feedback);
            else
                choose_picture(file, feedback);
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
                if (!art_.running())
                    arm_grid();
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
        else if (tag >= kItemShelf && tag - kItemShelf < static_cast<int>(std::size(kShelves)))
        {
            // Its own shelf: back where it belongs, no move kept.
            const std::string key = kShelves[static_cast<std::size_t>(tag - kItemShelf)].key;
            const Title *title = find_title(menu_title_);
            if (title != nullptr && key == shelf_key_of(*title))
                prefs_.unplace(menu_title_);
            else
                prefs_.place(menu_title_, key);
        }
        save_prefs();
        rebuild_tabs(active_key());
        rebuild();
        feedback.play(audio::Cue::place);
    }

    // An ELF program goes to the console's ELF loader, as a jailbreak's
    // payloads do; it runs beside ORION, which stays open.
    void send_program(const Title &title, app::Feedback &feedback)
    {
        std::string elf;
        if (!sys::on_console())
        {
            show_note(false, tr("Programs are sent on the console"));
            feedback.play(audio::Cue::error);
            return;
        }
        if (!save::read_file(title.path, &elf, 64u << 20) || elf.size() < 4 ||
            elf.compare(0, 4,
                        "\x7f"
                        "ELF") != 0)
        {
            show_note(false, trf("{0} is not an ELF program ORION can read", {title.name}));
            feedback.play(audio::Cue::error);
            return;
        }
        std::thread([elf = std::move(elf)]() { sys::send_payload(elf, kLoaderPort); }).detach();
        show_note(true, trf("{0} is sent to the ELF loader", {title.name}));
        feedback.play(audio::Cue::select);
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
        if (title->source == library::Source::payload)
        {
            send_program(*title, feedback);
            return;
        }
        // A RomM game not on the console yet: fetched first, then started.
        if (!title->remote.empty() && !file_exists(title->path))
        {
            download_romm_game(*title, feedback);
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
        // RomM keeps a newer save of it: that one first, then the start.
        if (retroarch && pull_romm_save(*title, player, feedback))
            return;
        const bool continuing = retroarch && std::exchange(continue_next_, false);
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
        else if (app != nullptr && app->key == "ps5x360")
            // The game is its first argument, as a file it can open.
            request = launcher + "\n" + library::game_arguments(player.rom);
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
        else if (retroarch && !write_shared(staged_args, library::retroarch_args(
                                                             core, player.rom, token,
                                                             continuing ? kContinueConfig : "")))
            why = trf("could not write {0}", {staged_args});
        else if (continuing && !prepare_continue(player, &request))
            why = tr("its last save state could not be made ready");
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
        if (data_missing_)
            return tr("ORION cannot reach /data (see ShadowMountPlus)");
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
        orion::ArtJob job;
        job.id = title.id;
        job.system = title.system;
        job.rom_file = file;
        job.name = title.name;
        job.serial = library::cover_id(title);
        job.target = orion::art_folder(prefs_, context_.library.state_dir) + "/" + title.system +
                     "/" + library::art_stem(title.rom) + ".png";
        if (!title.remote_cover.empty())
        {
            job.first_url = title.remote_cover;
            job.first_headers = orion::romm_headers(romm_token());
        }
        return job;
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
        // The tab in view first, then the rest; by itself (not asked for), a
        // run is a few hundred games: a library of thousands without art took
        // an hour that looked stuck. The rest come on the next starts, as they
        // scroll into view, or all at once from Settings, Box art.
        std::vector<int> order(view_.begin(), view_.end());
        std::unordered_set<int> seen(order.begin(), order.end());
        for (int i = 0; i < static_cast<int>(titles().size()); ++i)
        {
            if (seen.insert(i).second)
                order.push_back(i);
        }
        for (const int index : order)
        {
            const Title &title = titles()[static_cast<std::size_t>(index)];
            // A server's game (RomM) is not on the console: its cover comes
            // when it is about to be seen (want_icons), not with the rest.
            if (!title.is_rom() || !title.icon.empty() || !title.remote.empty() ||
                !prefs_.system_enabled(library::shelf_system(title.system)) ||
                tried.find(title.id + "\n") != std::string::npos)
                continue;
            jobs.push_back(art_job(title));
            if (!retry && jobs.size() >= kArtPerRun)
                break;
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
        arm_grid();
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
        std::vector<orion::ArtJob> remote;
        const auto want = [&](int index)
        {
            const Title &title = titles()[static_cast<std::size_t>(index)];
            // A server game in view with no picture: its cover is fetched now.
            if (title.is_rom() && title.icon.empty() && !title.remote.empty() && prefs_.auto_art &&
                !art_.running() && romm_art_asked_.insert(title.id).second)
                remote.push_back(art_job(title));
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
        if (!remote.empty() && sys::on_console())
        {
            arm_grid();
            art_.start(std::move(remote), context_.library.state_dir + "/art.txt");
        }
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
        // A picture loaded again (a newer copy of a home-screen icon) is a
        // new texture, and the old one's number goes to whatever loads next,
        // often a game's: a cover holding the old number showed that game's
        // picture until the tab was built again. Covers follow their titles.
        if (covers_stale())
            refresh_covers();
        if (art_.running())
        {
            if (const std::vector<std::string> missing = art_.take_missing(); !missing.empty())
                remember_tried(missing);
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
    // Whether a cover shows a texture its title no longer has.
    bool covers_stale() const
    {
        const bool wide = wide_tab();
        for (int i = 0; i < grid_.count(); ++i)
        {
            const ui::CardItem &item = grid_.items()[static_cast<std::size_t>(i)];
            const Title &title = titles()[static_cast<std::size_t>(item.tag)];
            if (item.texture != title.texture &&
                !(wide && title.wide_texture != 0 && item.texture == title.wide_texture))
                return true;
        }
        for (std::size_t i = 0; i < continue_view_.size(); ++i)
        {
            const ui::CardItem &item = continue_.items()[i];
            if (item.texture != titles()[static_cast<std::size_t>(item.tag)].texture)
                return true;
        }
        return false;
    }

    void refresh_covers()
    {
        const bool wide = wide_tab();
        for (int i = 0; i < grid_.count(); ++i)
        {
            ui::CardItem &item = grid_.item(i);
            const Title &title = titles()[static_cast<std::size_t>(item.tag)];
            // A wide tab's covers keep their wide pictures (update_wide).
            if (item.texture == title.texture ||
                (wide && title.wide_texture != 0 && item.texture == title.wide_texture))
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
        updates_->set_source(orion::update_source(context_.library.state_dir));
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
                          trf("ORION {0} is out: Settings, About, Check now", {latest.version}));
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
        installer_.set_next_title(own_title_id() != kNextTitleId ? kNextTitleId : "");
        if (!installer_.start(release, own_title_id(), state + "/update"))
        {
            orion::update_log(update_log_path(), "orion: a download is already running");
            return;
        }
        // It is big: say so, so nobody closes ORION halfway; a system
        // notification too, which shows over Settings and anything else.
        const std::string note =
            release.zip_size > 0
                ? trf("Downloading ORION {0} ({1}). Keep ORION open; it asks before it "
                      "installs.",
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

    // ---- ORION's next title ID ----

    std::string autoboot_payload_for(const std::string &id) const
    {
        return "/data/homebrew/" + id + "/assets/payloads/orion-autostart.elf";
    }

    // A release built for the next title ID: installed into its own folder
    // beside this one, which stays. The helper opens the new ORION, or this
    // one again when the system has not listed the new title yet; either
    // way move-pending.txt says what happened at the next start.
    std::string install_beside(const std::string &id, const std::string &update)
    {
        const std::string &self_path = context_.library.self_path;
        const std::string &state = context_.library.state_dir;
        const std::string target = self_path.substr(0, self_path.rfind('/') + 1) + id;
        // The console settings are in this title's own save area, which the
        // new one cannot read: they wait in the shared folder.
        std::string settings;
        save::ensure_directory(state + "/carry");
        if (save::read_file("/download0/hui/settings.bin", &settings, 1u << 20))
            save::write_atomic(state + "/carry/settings.bin", settings);
        save::write_atomic(state + "/move-pending.txt",
                           id + "\t" + own_title_id() + "\t" + self_path + "\n");
        orion::update_log(update_log_path(), "orion: installing " + id + " beside, in " + target);
        return send_helper(id + "\n@update " + update + "/stage\t" + target + "\n@fallback " +
                           own_title_id() + "\n");
    }

    // At start: the heads-up before the move, or what to do after it.
    void take_new_title_id()
    {
        const std::string &state = context_.library.state_dir;
        const std::string self = own_title_id();
        if (!sys::on_console() || state.empty() || self.empty())
            return;
        // "<new ID>\t<old ID>\t<old folder>", as install_beside writes it; a
        // file in any other shape (an older ORION's) says nothing.
        std::string pending;
        save::read_file(state + "/move-pending.txt", &pending, 1024);
        const std::size_t tab = pending.find('\t');
        const std::size_t second =
            tab == std::string::npos ? std::string::npos : pending.find('\t', tab + 1);
        const bool moved =
            second != std::string::npos && pending.compare(0, tab, kNextTitleId) == 0;
        const std::string old_id =
            tab == std::string::npos ? std::string() : pending.substr(tab + 1, second - tab - 1);
        std::string old_path =
            second == std::string::npos ? std::string() : pending.substr(second + 1);
        old_path = old_path.substr(0, old_path.find_first_of("\r\n"));
        if (moved && self == kNextTitleId)
        {
            // The new ORION, opened for the first time: the play times come
            // along (the old one kept them in its own folder), once.
            const std::string &self_path = context_.library.self_path;
            std::string log;
            struct stat info = {};
            if (!self_path.empty() && ::stat((self_path + "/play-log.txt").c_str(), &info) != 0 &&
                !old_path.empty() && save::read_file(old_path + "/play-log.txt", &log, 8u << 20))
                save::write_atomic(self_path + "/play-log.txt", log);
            std::remove((state + "/move-pending.txt").c_str());
            info_title_ = tr("This is the new ORION");
            info_body_ = trf(
                "Delete the old ORION ({0}) from the home screen: Options on its tile, "
                "Delete.\n"
                "Your settings, favourites, tabs, themes and box art are all here.\n"
                "Autoboot: point your autoloader at {1}",
                {old_id.empty() ? std::string("PPSA99061") : old_id, autoboot_payload_for(self)});
            return;
        }
        if (moved && self != kNextTitleId)
        {
            // This ORION installed the new one, and opened again because the
            // system had not listed it yet.
            info_title_ = tr("The new ORION is installed");
            info_body_ = tr("It appears on the home screen in a moment, with its new icon.\n"
                            "Close this ORION, delete its tile (Options on it, Delete),\n"
                            "then open the new ORION. Everything comes along.");
            return;
        }
        // Before the move: said once.
        std::string told;
        if (self != kNextTitleId && !save::read_file(state + "/new-title-id.txt", &told, 64))
        {
            save::write_atomic(state + "/new-title-id.txt", std::string(kNextTitleId) + "\n");
            info_title_ = tr("A heads-up about the next update");
            info_body_ =
                trf("ORION's next update installs it as a new tile ({0}), with a slightly\n"
                    "different icon. When it appears, delete the old ORION and open the new\n"
                    "one: your settings, favourites, tabs, themes and box art come along.",
                    {kNextTitleId});
        }
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
            save::set_mode(update + "/previous", 0777, true);
            // What is staged says what it is: its own param.json. A release
            // built for another title ID is never installed over this one.
            std::string staged_id;
            std::string staged;
            json::Value root;
            if (save::read_file(update + "/stage/sce_sys/param.json", &staged, 1u << 20) &&
                json::parse(staged, &root))
                staged_id = root["titleId"].string_or("");
            if (staged_id == kNextTitleId && self != kNextTitleId)
                why = install_beside(staged_id, update);
            else if (staged_id != self)
                why = tr("no update is ready");
            else
            {
                orion::update_log(update_log_path(), "orion: handing the install to the helper");
                why = send_helper(self + "\n@update " + update + "/stage\t" + self_path + "\n");
            }
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
        return save::write_atomic(file, bytes).empty(); // written open to every app
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
    // (/app0/play-log.txt), and, for the PC preview, one in the state folder;
    // then the system's own time where it has more (system-playtime.txt).
    static orion::PlayLog load_plays(const std::string &state_dir)
    {
        std::string text;
        std::string more;
        save::read_file("/app0/play-log.txt", &text, 4u << 20);
        if (!state_dir.empty() && save::read_file(state_dir + "/play-log.txt", &more, 4u << 20))
            text += "\n" + more;
        orion::PlayLog log = orion::read_play_log(text);
        std::string system;
        if (!state_dir.empty() &&
            save::read_file(state_dir + "/system-playtime.txt", &system, 1u << 20))
            orion::merge_system_play_time(system, &log);
        return log;
    }

    // The system's play time, read by a payload (its database is outside
    // ORION's sandbox): sent once, a few seconds in, so it does not meet the
    // rest-mode watcher at the loader; its answer is read when it is written.
    void follow_system_play_time()
    {
        const std::string &state = context_.library.state_dir;
        if (!sys::on_console() || data_missing_ || state.empty())
            return;
        const std::string file = state + "/system-playtime.txt";
        if (playtime_asked_ < 0.0f)
        {
            std::string elf;
            if (clock_ < 5.0f || launch_pending_ > 0.0f)
                return;
            playtime_asked_ = clock_;
            struct stat info = {};
            if (::stat(file.c_str(), &info) == 0)
                playtime_time_ = info.st_mtime;
            const std::string apps = state + "/" + orion::kSystemApps + "/index.txt";
            if (::stat(apps.c_str(), &info) == 0)
                apps_time_ = info.st_mtime;
            if (!save::read_file(kPlaytimePath, &elf, 8u << 20) || elf.empty())
                return;
            std::thread([elf = std::move(elf)]() { sys::send_payload(elf, kLoaderPort); }).detach();
            return;
        }
        if (clock_ - playtime_asked_ > 30.0f ||
            static_cast<int>(clock_ * 2.0f) == static_cast<int>((clock_ - last_dt_) * 2.0f))
            return;
        struct stat info = {};
        // The installed apps it listed: the titles are looked for again,
        // and what is new joins the library.
        const std::string apps = state + "/" + orion::kSystemApps + "/index.txt";
        if (::stat(apps.c_str(), &info) == 0 && info.st_mtime != apps_time_)
        {
            apps_time_ = info.st_mtime;
            if (!registered_.running())
                registered_.start(state, ftp_port());
        }
        if (::stat(file.c_str(), &info) != 0 || info.st_mtime == playtime_time_)
            return;
        playtime_time_ = info.st_mtime;
        plays_ = load_plays(state);
        const Sort sort = sort_of(sort_key());
        if (sort == Sort::played || sort == Sort::most_played)
            rebuild();
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

    // ---- details ----

    // A title's page (orion/title_details.hpp): what it is, where it lives,
    // its history; Play and Favourite from there.
    void open_details(const std::string &id, app::Feedback &feedback)
    {
        const Title *title = find_title(id);
        if (title == nullptr)
            return;
        details_id_ = id;
        orion::TitleFacts facts;
        facts.name = title->name;
        if (title->is_rom())
        {
            const std::size_t slash = title->path.rfind('/');
            facts.file = slash == std::string::npos ? title->path : title->path.substr(slash + 1);
            facts.kind = library::short_system_name(title->system);
            facts.plays_with = player_label(*title);
            if (facts.plays_with.empty())
                facts.plays_with = emulator_name(*title);
        }
        else
        {
            facts.id = title->id;
            facts.version = title->version;
            facts.kind = tr(shelf_name(library::shelf_of(*title)));
            facts.plays_with = library::platform_name(title->platform);
            facts.kept_as = title->source == library::Source::image       ? tr("Image")
                            : title->source == library::Source::installed ? tr("Installed")
                            : title->source == library::Source::payload   ? tr("ELF program")
                                                                          : tr("Folder");
            facts.folder = title->source == library::Source::folder;
        }
        facts.path = title->path;
        facts.drive = orion::drive_name(orion::drive_of(title->path));
        const orion::PlayRecord &record = played(*title);
        const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
        if (record.sessions > 0)
        {
            facts.play_time = orion::play_time_text(record.seconds);
            facts.last_played = orion::ago_text(record.last, now);
        }
        if (title->added > 0)
            facts.added = orion::ago_text(title->added, now);
        facts.favourite = favourite_of(title->id);
        facts.texture = title->sharp_texture != 0 ? title->sharp_texture : title->texture;
        const int w = title->sharp_texture != 0 ? title->sharp_width : title->icon_width;
        const int h = title->sharp_texture != 0 ? title->sharp_height : title->icon_height;
        facts.aspect = w > 0 && h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;
        details_.open(std::move(facts), feedback);
    }

    void update_details(const InputFrame &input, app::Feedback &feedback)
    {
        switch (details_.handle(input, feedback))
        {
        case orion::TitleDetails::Choice::play:
            details_.close(feedback);
            launch(details_id_, feedback);
            break;
        case orion::TitleDetails::Choice::favourite:
            toggle_favourite(details_id_, feedback);
            details_.set_favourite(favourite_of(details_id_));
            break;
        case orion::TitleDetails::Choice::closed:
        case orion::TitleDetails::Choice::none:
            break;
        }
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
        // A built-in shelf; a moved title may have left it, or been moved
        // to another.
        if (const std::string shelf = prefs_.placed_shelf(title.id); !shelf.empty())
            return key == shelf;
        if (placed != nullptr && prefs_.moved_leave_shelf)
            return false;
        const library::Shelf shelf = library::shelf_of(title);
        return (key == "games" && shelf == library::Shelf::games) ||
               (key == "homebrew" && shelf == library::Shelf::homebrew) ||
               (key == "media" && shelf == library::Shelf::media);
    }

    // The built-in shelf a title belongs on by itself.
    static std::string shelf_key_of(const Title &title)
    {
        switch (library::shelf_of(title))
        {
        case library::Shelf::games:
            return "games";
        case library::Shelf::homebrew:
            return "homebrew";
        case library::Shelf::media:
            return "media";
        }
        return {};
    }

    int focus_index() const
    {
        return std::clamp(grid_.focus(), 0, std::max(0, static_cast<int>(view_.size()) - 1));
    }

    // A built-in tab's name on the tab bar: the player's, or its own.
    std::string tab_label(const std::string &key, std::string own) const
    {
        std::string name = prefs_.tab_name(key);
        return name.empty() ? own : name;
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
                add(shelf.key, tab_label(shelf.key, tr(shelf.name)));
        }
        // The systems: a tab each, or one Emulators tab with a row of them.
        sub_keys_.clear();
        std::vector<ui::TabItem> systems;
        for (const library::System &system : context_.library.emulator.systems)
        {
            if (!prefs_.system_enabled(system.name))
                continue;
            ui::TabItem item;
            item.label = tab_label("sys:" + system.name, system.short_name);
            item.badge = system.games;
            systems.push_back(std::move(item));
            sub_keys_.push_back("sys:" + system.name);
        }
        // Emulators found and no games for them yet: the tab is there all the
        // same, and says where games go (draw_empty).
        const bool grouped =
            prefs_.group_systems && (!sub_keys_.empty() || !context_.library.emulator.apps.empty());
        if (grouped && !prefs_.tab_hidden(kEmulatorsTab))
            add(kEmulatorsTab, tab_label(kEmulatorsTab, tr("Emulators")));
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
        // Hidden, last, while something is hidden, unless the player hid
        // the tab itself (Settings, Tabs, its page, brings it back).
        if (any_hidden() && !prefs_.tab_hidden(kHiddenTab))
            add(kHiddenTab, tab_label(kHiddenTab, tr("Hidden")));
        // Every tab hidden: All stays, or nothing could be reached (Settings
        // brings the others back).
        if (tab_keys_.empty())
            add("all", tab_label("all", tr("All")));
        // The player's order (Settings, Tabs, a tab's page, Move left/right).
        {
            const std::vector<std::string> order = prefs_.ordered_tabs(tab_keys_);
            std::vector<ui::TabItem> sorted;
            for (const std::string &key : order)
            {
                const auto at = std::find(tab_keys_.begin(), tab_keys_.end(), key);
                sorted.push_back(tabs[static_cast<std::size_t>(at - tab_keys_.begin())]);
            }
            tab_keys_ = order;
            tabs = std::move(sorted);
        }
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
        // A console case as it is moulded: a squarer shell, the clips along
        // its top and bottom edges, a rule under the header and the name set
        // wide, as the console's own cases print it.
        bool console = false;
        Color rule{0.0f, 0.0f, 0.0f, 0.0f}; // under the header; alpha 0: none
    };
    CaseBand case_band(const Title &title) const
    {
        if (library::shelf_of(title) == library::Shelf::homebrew)
            return {Color::rgb(0x161a3d), Color::rgb(0x0b0e20), theme_.accent, tr("HOMEBREW"),
                    true};
        if (library::shelf_of(title) == library::Shelf::media)
            return {Color::rgb(0x202228), Color::rgb(0xffc94a), Color::rgb(0x1a1204), tr("MEDIA")};
        if (title.platform == library::Platform::ps4)
        {
            CaseBand band{Color::rgb(0x1f56c8), Color::rgb(0x003087), Color::rgb(0xffffff), "PS4"};
            band.console = true;
            band.rule = Color::rgb(0x001b52);
            return band;
        }
        if (!title.is_rom())
        {
            CaseBand band{Color::rgb(0x1d5fe0), Color::rgb(0xfbfbfc), Color::rgb(0x0a0b0d), "PS5"};
            band.console = true;
            band.rule = Color::rgb(0x0b2f8a);
            return band;
        }
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

    // A cover filled into drawn (trimmed evenly at the edges that differ), or
    // its colours and monogram centred in cover when it has no picture yet.
    void draw_cover_art(ui::Canvas &canvas, const Rect &drawn, const Rect &cover,
                        const ui::CardItem &item, const Title &title, float radius) const
    {
        gfx::DrawList &list = canvas.list;
        const bool sharp = title.sharp_texture != 0 && title.sharp_height > 0;
        const std::uint32_t texture = sharp ? title.sharp_texture : item.texture;
        if (texture == 0)
        {
            list.gradient_rect(drawn, radius, item.top, item.bottom);
            const std::string letters = monogram(item.title);
            ui::text(list, canvas.fonts.display, letters.c_str(), cover.cx(),
                     cover.y + cover.h * 0.5f + cover.h * 0.12f, cover.h * 0.3f,
                     Color::rgb(0xffffff, 0.9f), gfx::Align::center);
            return;
        }
        const float picture =
            sharp ? static_cast<float>(title.sharp_width) / static_cast<float>(title.sharp_height)
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
        list.image(texture, drawn, uv, Color::rgb(0xffffff), radius);
    }

    // The player's picture of this title's case (<state_dir>/cases), if any.
    const library::CaseTemplate *case_template(const Title &title) const
    {
        if (context_.library.cases.empty())
            return nullptr;
        std::string alias;
        const std::string key = library::case_key(title, &alias);
        for (const library::CaseTemplate &shell : context_.library.cases)
        {
            if (shell.key == key || (!alias.empty() && shell.key == alias))
                return &shell;
        }
        return nullptr;
    }

    // A game in a case picture: the cover in its window, the picture over it
    // (or under it, for a flat window), both stretched to the case's cell.
    // A picture of another shape (a PS1 jewel case, near square) is drawn to
    // this one too: the cover is cropped to its window, never stretched, and
    // a cell left partly empty would show the grid's backing around it.
    void draw_case_template(ui::Canvas &canvas, const Rect &art, const ui::CardItem &item,
                            const Title &title, const library::CaseTemplate &shell,
                            float focus) const
    {
        gfx::DrawList &list = canvas.list;
        constexpr float kRadius = 7.0f;
        const bool aurora = shown_.chrome.focus == orion::FocusRing::aurora && focus > 0.01f;
        if (aurora)
        {
            const float time = context_.settings.reduced_motion ? 0.0f : clock_;
            const Color light = hsv(std::fmod(time * 0.05f, 1.0f), 0.55f, 1.0f);
            list.glow(art, kRadius, 30.0f * focus, light.with_alpha(0.5f * focus));
        }
        const Rect window{art.x + shell.window_x * art.w, art.y + shell.window_y * art.h,
                          shell.window_w * art.w, shell.window_h * art.h};
        // A clear window shows the cover through the picture; a flat one
        // (no transparency in the file) has it laid over the picture.
        if (shell.window_solid)
            list.image(shell.texture, art, gfx::kFullUv, Color::rgb(0xffffff));
        list.push_clip(window);
        draw_cover_art(canvas, window, window, item, title, 0.0f);
        list.pop_clip();
        if (!shell.window_solid)
            list.image(shell.texture, art, gfx::kFullUv, Color::rgb(0xffffff));
        if (favourite_of(title.id))
        {
            const float r = std::max(16.0f, art.w * 0.06f);
            const float cx = window.x + window.w - r - 12.0f;
            const float cy = window.y + r + 12.0f;
            list.circle(cx, cy, r, Color::rgb(0x0b0e20, 0.72f));
            list.star(cx, cy, r * 0.72f, Color::rgb(0xffc94a));
        }
        if (aurora)
            draw_aurora_ring(list, art, kRadius, focus);
    }

    // A game in Cases view. The picture is the sharp copy when the app has
    // made one (Context::sharp_icons), else the grid's small one. With the
    // player's picture of its case, the cover goes in that instead.
    void draw_case(ui::Canvas &canvas, const Rect &art, const ui::CardItem &item, float focus) const
    {
        gfx::DrawList &list = canvas.list;
        const Title &title = titles()[static_cast<std::size_t>(item.tag)];
        if (const library::CaseTemplate *shell = case_template(title))
        {
            draw_case_template(canvas, art, item, title, *shell, focus);
            return;
        }
        const CaseBand band = case_band(title);
        // A console's case is squarer than the drawn ones for other systems.
        const float kRadius = band.console ? 7.0f : 14.0f;
        const float kInner = band.console ? 2.0f : 5.0f;
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
        // The sleeve inside it: the header, then the cover. A console case's
        // shell is thin at the sides and a little deeper top and bottom,
        // where its clips are.
        const float side = std::round(art.w * (band.console ? 0.018f : 0.024f));
        const float ends = band.console ? std::round(art.h * 0.02f) : side;
        const Rect sleeve{art.x + side, art.y + ends, art.w - 2.0f * side, art.h - 2.0f * ends};
        const float band_h = std::round(sleeve.h * (band.console ? 0.125f : 0.115f));
        if (band.console)
        {
            // The clips: short lighter ridges along the top and bottom edge,
            // and the darker hinge line down the right side.
            const float clip_w = art.w * 0.09f;
            for (const float at : {0.12f, 0.36f, 0.62f, 0.86f})
            {
                const float cx = art.x + art.w * at;
                list.rounded_rect({cx - clip_w * 0.5f, art.y + 1.5f, clip_w, ends * 0.45f}, 1.0f,
                                  Color::rgb(0xffffff, 0.28f));
                list.rounded_rect(
                    {cx - clip_w * 0.5f, art.y + art.h - 1.5f - ends * 0.45f, clip_w, ends * 0.45f},
                    1.0f, Color::rgb(0xffffff, 0.18f));
            }
            list.rounded_rect(
                {art.x + art.w - side * 0.55f, art.y + art.h * 0.3f, 1.5f, art.h * 0.4f}, 0.0f,
                Color::rgb(0x000000, 0.25f));
        }
        list.rounded_rect(sleeve, kInner, band.fill);
        if (band.orion)
            list.gradient_rect(sleeve, kInner, Color::rgb(0x1b1450), Color::rgb(0x0b0e20));
        const Rect cover{sleeve.x, sleeve.y + band_h, sleeve.w, sleeve.h - band_h};
        // Drawn a corner taller and clipped flat: a straight edge under the
        // header, round corners at the bottom.
        const Rect drawn{cover.x, cover.y - kInner, cover.w, cover.h + kInner};
        list.push_clip(cover);
        draw_cover_art(canvas, drawn, cover, item, title, kInner);
        list.pop_clip();
        if (band.rule.a > 0.0f)
            list.rounded_rect({sleeve.x, cover.y - 2.0f, sleeve.w, 3.0f}, 0.0f, band.rule);
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
        if (band.console)
            // Set wide in the display face, as the console's own name is.
            ui::text(list, canvas.fonts.display, band.label.c_str(), x, mid + size * 0.38f,
                     size * 1.05f, band.ink, gfx::Align::left, size * 0.16f);
        else
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
        else if (key == kEmulatorsTab)
        {
            title = tr("No games for your emulators yet");
            first = tr("Put games in the folders Settings, Emulators lists for each emulator,");
            second = tr("or add a folder of your own there; then Scan for emulators.");
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
            : typing_ == Typing::grid_key     ? tr("Your SteamGridDB API key; empty removes it")
            : typing_ == Typing::art_name     ? tr("The name to look for on SteamGridDB")
            : typing_ == Typing::romm_url  ? tr("Its address with the port, like 192.168.1.20:8080")
            : typing_ == Typing::romm_code ? tr("The pairing code RomM shows")
            : typing_ == Typing::romm_search ? tr("A game's name, or part of it")
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
    bool data_missing_ = false; // no /data in ORION's sandbox (ShadowMountPlus)
    // Title music: the player's songs by name, what each title plays (empty:
    // none), the title the focus rests on and for how long, and the copier
    // of the system's snd0.at9 files.
    std::unordered_map<std::string, std::string> hover_names_;
    std::unordered_map<std::string, std::string> music_paths_;
    std::string music_focus_;
    float music_dwell_ = 0.0f;
    orion::BackdropCopies sound_copies_;
    // Wide covers: the copies being made, and the titles already asked for.
    orion::WideCopies wide_copies_;
    std::unordered_set<std::string> wide_asked_;
    bool picking_music_ = false; // the browser lists music, not pictures
    // Find other art: the title looked through, its picture before, the
    // covers so far, the one shown, and whether the full one is coming.
    orion::GridPicker picker_{grid_http()};
    std::string preview_id_;
    std::string preview_name_;
    std::string preview_query_; // the name searched for
    std::string preview_was_;
    bool preview_asked_ = false; // false: waiting for the picker to be free
    std::string grid_key_;       // keys.txt's, as last read
    orion::KeyPage key_page_;    // the key typed on a phone or PC (http://<console>:9062)
    orion::WhatsNew whats_new_;  // this version's notes, once after an update
    bool whats_new_checked_ = false;
    // RomM: the sync, a game being downloaded, a pairing under way.
    orion::RommSync romm_sync_{[](const std::string &url, const std::vector<std::string> &headers,
                                  std::size_t limit, std::string *body, std::string *error)
                               { return sys::http_get(url, headers, limit, body, error); }};
    orion::FileDownload romm_download_{
        [](const std::string &url, const std::vector<std::string> &headers, const std::string &path,
           std::size_t limit, std::string *error)
        { return sys::http_download(url, headers, path, limit, error); }};
    std::string romm_downloading_id_;
    std::string romm_downloading_name_;
    std::shared_ptr<RommPairing> romm_pairing_;
    std::string romm_status_;
    bool romm_auto_synced_ = false;
    std::unordered_set<std::string> romm_art_asked_; // RomM games whose cover was asked for
    orion::RommBrowser romm_browser_;
    std::deque<orion::RommEntry> romm_queue_; // picked games waiting their turn
    bool romm_launch_after_ = false;          // the download is a launch's: start it after
    bool art_paused_for_romm_ = false;        // a box art run stopped for a RomM game
    std::unordered_set<std::string> romm_save_checked_;   // games whose RomM save was looked at
    std::unordered_map<std::string, long long> romm_ids_; // RomM's ids by file name
    std::string romm_save_target_; // a save being brought from RomM: where it goes
    bool continue_next_ = false;   // the next launch continues from the last state
    int romm_queued_done_ = 0;     // picked games fetched in this run of the queue
    // Picked games the library does not list yet (file, name): added once in.
    std::vector<std::pair<std::string, std::string>> romm_unlisted_;
    std::vector<std::string> preview_files_;
    int preview_index_ = 0;
    bool keeping_ = false;
    // Choose picture…: where the browser is (empty: the places), what is
    // there, and the page shown.
    std::string pick_dir_;
    std::vector<std::pair<std::string, std::string>> pick_places_;
    std::vector<std::string> pick_dirs_;
    std::vector<std::string> pick_files_;
    int pick_page_ = 0;
    float check_started_ = 0.0f;       // clock_ when the running check began
    bool update_asked_ = false;        // the check this launch makes by itself has started
    bool manual_update_check_ = false; // the running check was asked for: say how it went
    orion::UpdateInstaller installer_{
        [](const std::string &url, std::size_t limit, std::string *body, std::string *error)
        { return sys::http_get(url, limit, body, error); },
        [](const std::string &url, const std::string &path, std::size_t limit, std::string *error)
        { return sys::http_download(url, path, limit, error); }};
    bool update_waiting_ = false;      // an update is ready: ask when nothing else is open
    bool dialog_update_ = false;       // the dialog asks about the update, not a removal
    bool dialog_info_ = false;         // the dialog only tells something (OK)
    orion::TitleDetails details_;      // a title's page (Details… in its menu)
    float drive_space_asked_ = -1.0f;  // clock_ when the helper was sent for the drives' space
    std::time_t drive_space_time_ = 0; // when the answer shown was written
    float playtime_asked_ = -1.0f;     // clock_ when the play time reader was sent
    std::time_t playtime_time_ = 0;    // when the system's play time read was written
    std::time_t apps_time_ = 0;        // when the payload's list of installed apps was
    float last_dt_ = 0.0f;
    std::string details_id_; // the title it shows
    std::string info_title_; // a message waiting to be shown
    std::string info_body_;
};

} // namespace

std::unique_ptr<app::Concept> make_library(app::Context &context)
{
    return std::make_unique<Library>(context);
}

} // namespace hui::concepts
