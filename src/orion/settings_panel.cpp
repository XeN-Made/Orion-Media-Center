// ORION Media Center - ORION's Settings: a full screen of sections, with a page for every tab.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/settings_panel.hpp"

#include "core/i18n.hpp"
#include "core/save_file.hpp"
#include "gfx/draw_list.hpp"
#include "library/emulators.hpp"
#include "orion/updates.hpp"
#include "ui/glyphs.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <sys/stat.h>

namespace hui::orion
{

using i18n::tr;
using i18n::trf;

namespace
{

using gfx::Color;

enum : int
{
    kLanguage = 1,
    kSort,
    kView,
    kReopen,
    kContinueRow,
    kGroupSystems,
    kAskPlayer,
    kAutostart,
    kReopenAfterRest,
    kPayloadInfo,
    kPayloadFolder,
    kRunSetup,
    kCheckUpdates,
    kAutoUpdate,
    kUpdateNow,
    kInstallUpdate,
    kAboutVersion,
    kAboutTitle,
    kAboutReleases,
    kAboutLog,
    kLeaveShelf,
    kTabTheme,
    kBase,
    kBackground,
    kSky,
    kNebula,
    kClouds,
    kMotion,
    kGlass,
    kOpacity,
    kHighlight,
    kAccent,
    kCorners,
    kTabBar,
    kFocusRing,
    kBars,
    kBadges,
    kRim,
    kRestore,
    kSaveTheme,
    kNewTab,
    kArtAuto,
    kReturn,
    kGameBackgrounds,
    kBlurredCovers,
    kBackgroundStrength,
    kArtNow,
    kGridKey,
    kArtFolder,
    kAddRomFolder,
    kBackdropsInfo,
    kBackdropsAgain,
    kStateInfo,
    kThemesInfo,
    kFolderSystem,
    kFolderRemove,
    kMusicVolume,
    kSfxVolume,
    kUiVolume,
    kNowPlaying,
    kNextSong,
    kTitleMusic,
    kMusicFolder,
    kBrowseUse,
    kBrowseUp,
    kBrowseType,
    kScanEmulators,
    kEveryVersion,
    kAppInfo,
    kAppStarts,
    // A tab's page.
    kPageShow,
    kPageTheme,
    kPageCover,
    kPageSort,
    kPageView,
    kPageCore,
    kPageRename,
    kPageLeft,
    kPageRight,
    kPageDelete,
    kTabRow = 100,       // + the tab's index in tabs_
    kSystemRow = 200,    // + the system's index: on or off
    kCoreRow = 300,      // + the system's index: which core
    kRomFolderRow = 400, // + the folder's index in prefs.rom_folders
    kEmulatorRow = 500,  // + the emulator's index in emulator_->apps
    kOwnFolderRow = 600, // + the index of a folder the emulator looks in by itself
    kMissingRow = 700,   // + the index of a known emulator that is not installed
    kBrowseRow = 800,    // + the folder's index in browse_folders_ (or in places_)
};

// The emulator a game folder is for: empty in the prefs is RetroArch.
std::string folder_key(const RomFolder &folder)
{
    return folder.emulator.empty() ? std::string("retroarch") : folder.emulator;
}

// Where box art can live (prefs.hpp), or a folder the player types.
constexpr const char *kArtPlaces[] = {kArtPlaceNames[0], kArtPlaceNames[1], kArtPlaceNames[2],
                                      i18n::marked("Your own folder")};
constexpr const char *const *kArtPaths = kArtPlacePaths;

constexpr const char *kCoverNames[] = {i18n::marked("Fit the pictures"), i18n::marked("Square"),
                                       i18n::marked("Portrait (5:7)"),
                                       i18n::marked("Landscape (7:5)")};

const char *section_name(SettingsPanel::Section section)
{
    switch (section)
    {
    case SettingsPanel::Section::library:
        return i18n::marked("Library");
    case SettingsPanel::Section::tabs:
        return i18n::marked("Tabs");
    case SettingsPanel::Section::emulators:
        return i18n::marked("Emulators");
    case SettingsPanel::Section::art:
        return i18n::marked("Box art");
    case SettingsPanel::Section::storage:
        return i18n::marked("Storage");
    case SettingsPanel::Section::sound:
        return i18n::marked("Sound");
    case SettingsPanel::Section::about:
        return i18n::marked("About");
    default:
        return i18n::marked("Appearance");
    }
}

int view_index(const std::string &key)
{
    for (int i = 0; i < kViewCount; ++i)
    {
        if (key == kViewKeys[i])
            return i;
    }
    return 0;
}

int sort_index(const std::string &key)
{
    for (int i = 0; i < kSortCount; ++i)
    {
        if (key == kSortKeys[i])
            return i;
    }
    return 0;
}

// A choice over named swatches. The current colour picks its swatch; one that
// matches none (edited by hand in theme.json) shows as "Custom" at the end.
int swatch_index(std::span<const Swatch> swatches, Color first, Color second, bool pair)
{
    for (std::size_t i = 0; i < swatches.size(); ++i)
    {
        if (same_rgb(first, Color::rgb(swatches[i].first)) &&
            (!pair || same_rgb(second, Color::rgb(swatches[i].second))))
            return static_cast<int>(i);
    }
    return -1;
}

std::vector<std::string> swatch_names(std::span<const Swatch> swatches, bool custom)
{
    std::vector<std::string> names;
    for (const Swatch &swatch : swatches)
        names.push_back(tr(swatch.name));
    if (custom)
        names.push_back(tr("Custom"));
    return names;
}

void add_swatches(ui::Form &form, int id, const char *label, std::span<const Swatch> swatches,
                  Color first, Color second, bool pair, const char *description)
{
    const int index = swatch_index(swatches, first, second, pair);
    form.add_choice(id, tr(label), swatch_names(swatches, index < 0),
                    index < 0 ? static_cast<int>(swatches.size()) : index)
        .description = tr(description);
}

// The swatch a choice row points at, or nullptr for "Custom".
const Swatch *picked(const ui::Form &form, int id, std::span<const Swatch> swatches)
{
    const int index = form.choice_index(id);
    return index >= 0 && index < static_cast<int>(swatches.size())
               ? &swatches[static_cast<std::size_t>(index)]
               : nullptr;
}

} // namespace

SettingsPanel::SettingsPanel()
{
    sheet_.style.edge = ui::SheetEdge::right;
    sheet_.style.size = gfx::kVirtualWidth; // the whole screen, less the margin
    sheet_.style.margin = 28.0f;            // floats, fully rounded: a pane of glass
    // Rows of text want a calmer glass than the keyboard: more frost over
    // the covers behind, which still show through as colour.
    sheet_.style.frost = 0.65f;
    sheet_.style.scrim = 0.35f;
    sheet_.style.title_size = 40.0f;
    sheet_.style.handle = false; // nothing to drag: it fills the screen
    width_.snap(gfx::kVirtualWidth);
    sheet_.set_title(tr("Settings"));
    sheet_.content = [this](ui::Canvas &canvas, const gfx::Rect &, float opacity)
    { draw_content(canvas, opacity); };

    nav_.style.expanded_width = 320.0f;
    nav_.style.row_height = 66.0f;
    nav_.style.text_size = 26.0f;
    nav_.style.icon_size = 0.0f; // words only
    nav_.style.label_gap = 0.0f;
    nav_.style.panel = false; // the sheet is the panel

    form_.style.on_page = false;
    form_.style.row_height = 72.0f;
    form_.style.header_height = 58.0f;
    form_.style.label_size = 25.0f;
    form_.style.value_size = 24.0f;
    form_.style.description_size = 20.0f;
    form_.style.control_width = 420.0f; // room for theme names
}

void SettingsPanel::set_theme(const ui::Theme &theme, bool reduced_motion)
{
    // A screen full of text wants denser glass than the library: the player's
    // tint, but at least this opaque, so bright covers behind it become
    // colour rather than shapes under the rows.
    ui::Theme denser = theme;
    if (denser.style == ui::SurfaceStyle::glass)
        denser.surface = gfx::mix(denser.page, denser.surface, 0.35f)
                             .with_alpha(std::max(0.82f, denser.surface.a));
    sheet_.style.theme = form_.style.theme = nav_.style.theme = denser;
    sheet_.style.reduced_motion = form_.style.reduced_motion = nav_.style.reduced_motion =
        reduced_motion;
}

void SettingsPanel::open(const Look &look, const LibraryPrefs &prefs, ui::Feedback &feedback)
{
    page_.clear();
    in_rows_ = false;
    build_nav();
    show(sections_.front(), look, prefs);
    sheet_.open(feedback);
    layout();
    nav_.enter();
    leave_rows();
}

void SettingsPanel::open_tab(const std::string &key, const Look &look, const LibraryPrefs &prefs,
                             ui::Feedback &feedback)
{
    build_nav();
    show(Section::tabs, look, prefs);
    page_ = tab_entry(key) != nullptr ? key : std::string();
    build(look, prefs);
    sheet_.open(feedback);
    layout();
    nav_.enter();
    enter_rows(true);
}

void SettingsPanel::close(ui::Feedback &feedback)
{
    sheet_.close(feedback);
}

void SettingsPanel::refresh(const Look &look, const LibraryPrefs &prefs)
{
    const int focus = form_.focus();
    build(look, prefs);
    layout();
    form_.set_focus(std::clamp(focus, 0, std::max(form_.row_count() - 1, 0)));
}

void SettingsPanel::build_nav()
{
    // Emulators always, so Scan for emulators is there before one is found.
    sections_ = {Section::library, Section::tabs, Section::emulators};
    if (emulator_ != nullptr && !emulator_->systems.empty())
        sections_.push_back(Section::art);
    sections_.push_back(Section::storage);
    sections_.push_back(Section::appearance);
    if (app_settings_ != nullptr)
        sections_.push_back(Section::sound);
    sections_.push_back(Section::about);
    std::vector<ui::NavEntry> entries;
    for (Section section : sections_)
    {
        ui::NavEntry entry;
        entry.label = tr(section_name(section));
        entries.push_back(std::move(entry));
    }
    nav_.set_entries(std::move(entries));
}

void SettingsPanel::rebuild_nav(const Look &look, const LibraryPrefs &prefs)
{
    build_nav();
    auto at = std::find(sections_.begin(), sections_.end(), section_);
    if (at == sections_.end())
    {
        section_ = Section::emulators;
        at = std::find(sections_.begin(), sections_.end(), section_);
    }
    const int index = static_cast<int>(at - sections_.begin());
    nav_.set_focus(index);
    nav_.set_current(index);
    if (!emulator_page_.empty() && page_app() == nullptr)
    {
        emulator_page_.clear();
        folder_page_ = -1;
    }
    refresh(look, prefs);
}

void SettingsPanel::layout()
{
    const gfx::Rect area = sheet_.content_rect();
    constexpr float kHints = 70.0f; // the button hints under everything
    nav_.set_bounds({area.x, area.y, 0.0f, area.h - kHints});
    // The rail of sections gives its room up as the sheet narrows.
    const float left = area.x + (nav_.style.expanded_width + 56.0f) * wide_;
    const float width = std::min(area.x + area.w - left, 1180.0f);
    form_.style.control_width = 340.0f + 80.0f * wide_;
    form_.set_bounds({left, area.y, width, area.h - kHints});
}

bool SettingsPanel::previewing() const
{
    return sheet_.is_open() && in_rows_ &&
           (section_ == Section::appearance || (section_ == Section::tabs && !page_.empty()));
}

std::string SettingsPanel::preview_tab() const
{
    return previewing() && section_ == Section::tabs ? page_ : std::string();
}

void SettingsPanel::show(Section section, const Look &look, const LibraryPrefs &prefs)
{
    section_ = section;
    page_.clear();
    emulator_page_.clear();
    folder_page_ = -1;
    browsing_ = false;
    const auto at = std::find(sections_.begin(), sections_.end(), section);
    const int index = at == sections_.end() ? 0 : static_cast<int>(at - sections_.begin());
    nav_.set_focus(index);
    nav_.set_current(index);
    build(look, prefs);
    layout();
    form_.set_focus(0);
}

void SettingsPanel::enter_rows(bool snap)
{
    in_rows_ = true;
    nav_.set_focused(false);
    form_.set_active(true);
    form_.set_focus(0, snap);
    form_.enter();
}

void SettingsPanel::leave_rows()
{
    in_rows_ = false;
    nav_.set_focused(true);
    form_.set_active(false);
}

void SettingsPanel::build(const Look &look, const LibraryPrefs &prefs)
{
    // In the language chosen now; the form keeps pointers to the words.
    sheet_.set_title(tr("Settings"));
    on_word_ = tr("On");
    off_word_ = tr("Off");
    form_.style.on_text = on_word_.c_str();
    form_.style.off_text = off_word_.c_str();
    form_.clear();
    switch (section_)
    {
    case Section::library:
        build_library(prefs);
        break;
    case Section::tabs:
        if (page_.empty())
            build_tabs(prefs);
        else
            build_tab_page(prefs);
        break;
    case Section::emulators:
    {
        const library::EmulatorApp *app = page_app();
        if (app == nullptr)
            emulator_page_.clear();
        if (folder_page_ >= static_cast<int>(prefs.rom_folders.size()))
            folder_page_ = -1;
        if (app == nullptr)
            browsing_ = false;
        if (browsing_)
            build_browser();
        else if (app != nullptr && folder_page_ >= 0)
            build_folder_page(prefs);
        else if (app != nullptr)
            build_emulator_page(*app, prefs);
        else
            build_emulators(prefs);
        break;
    }
    case Section::art:
        build_art(prefs);
        break;
    case Section::storage:
        build_storage(prefs);
        break;
    case Section::sound:
        build_sound(prefs);
        break;
    case Section::appearance:
        build_appearance(look, prefs);
        break;
    case Section::about:
        build_about(prefs);
        break;
    }
}

void SettingsPanel::build_library(const LibraryPrefs &prefs)
{
    form_.add_header(tr("Library"));
    // The language first: everything after it reads in it.
    if (!languages_.empty())
    {
        std::vector<std::string> names;
        int chosen = 0;
        for (std::size_t i = 0; i < languages_.size(); ++i)
        {
            names.push_back(languages_[i].name);
            if (languages_[i].code == prefs.language)
                chosen = static_cast<int>(i);
        }
        form_.add_choice(kLanguage, tr("Language"), std::move(names), chosen).description =
            tr("Automatic follows the console's language");
    }
    form_
        .add_choice(kSort, tr("Sort by"), i18n::tr_range(kSortNames, kSortNames + kSortCount),
                    sort_index(prefs.sort))
        .description = tr("The order of the covers, on every tab without an order of its own");
    form_
        .add_choice(kView, tr("View"), i18n::tr_range(kViewNames, kViewNames + kViewCount),
                    view_index(prefs.view))
        .description = tr("Grid of covers, big cases three to a screen, or a list");
    form_.add_toggle(kReopen, tr("Reopen on last tab"), prefs.reopen_last_tab).description =
        tr("ORION starts on the tab you left it on");
    form_.add_toggle(kContinueRow, tr("Continue playing"), prefs.continue_row).description =
        tr("A row of what you played last over the All tab's covers");
    form_.add_toggle(kLeaveShelf, tr("Moved titles leave shelf"), prefs.moved_leave_shelf)
        .description = tr("A title moved to your own tab leaves Games, Homebrew or Media");
    form_.add_toggle(kReturn, tr("Return to ORION after a game"), prefs.return_on_close)
        .description = tr("Close a game from the PS button menu and ORION opens again");
    form_.add_toggle(kReopenAfterRest, tr("Open ORION after rest mode"), prefs.reopen_after_rest)
        .description = tr("When the console wakes on the home screen, ORION opens; a game left "
                          "running stays in front");
    form_.add_toggle(kAutostart, tr("Autoboot ORION after the jailbreak"), prefs.autostart)
        .description = tr("ORION opens on its own after each jailbreak, once the payload below is "
                          "in your autoloader");
    const std::size_t slash = payload_.rfind('/');
    form_.add_value(kPayloadInfo, tr("Autoboot payload"), payload_.substr(slash + 1));
    form_.add_value(kPayloadFolder, tr("In the folder"), payload_.substr(0, slash));
    ui::FormRow &setup = form_.add_action(kRunSetup, tr("Run setup again"));
    setup.chevron = true;
    setup.description = tr("The first-run steps: look, emulators, box art, autostart");
}

void SettingsPanel::build_about(const LibraryPrefs &prefs)
{
    form_.add_header(tr("ORION Media Center"));
    form_.add_value(kAboutVersion, tr("Version"), version_.empty() ? tr("Unknown") : version_);
    form_.add_value(kAboutTitle, tr("Title ID"), "PPSA99061");
    form_.add_value(kAboutReleases, tr("Releases"), kReleasesPage);
    form_.add_header(tr("Updates"));
    if (!update_ready_.empty())
        form_.add_action(kInstallUpdate, trf("Install ORION {0}", {update_ready_})).description =
            tr("ORION closes, updates itself and opens again");
    form_.add_action(kUpdateNow, tr("Check now")).description = update_status_;
    form_.add_toggle(kCheckUpdates, tr("Check for updates"), prefs.check_updates).description =
        tr("Each time ORION opens, it asks GitHub whether a newer ORION is out");
    form_.add_toggle(kAutoUpdate, tr("Download updates by themselves"), prefs.auto_update)
        .description = tr("A newer ORION is made ready in the background; ORION asks before it "
                          "installs it");
    form_.add_value(kAboutLog, tr("Update log"), "/data/orion/update/log.txt");
}

void SettingsPanel::build_tabs(const LibraryPrefs &prefs)
{
    form_.add_header(tr("Tabs"));
    ui::FormRow &add = form_.add_action(kNewTab, tr("New tab"));
    add.chevron = true;
    add.description = tr("A shelf of your own: move any game into it from its menu");
    form_.add_toggle(kGroupSystems, tr("Group emulator tabs"), prefs.group_systems).description =
        tr("One Emulators tab, with a row for each system under it: L2 and R2 change system");
    for (std::size_t i = 0; i < tabs_.size(); ++i)
    {
        const TabEntry &tab = tabs_[i];
        ui::FormRow &row = form_.add_action(kTabRow + static_cast<int>(i), tab.name);
        row.chevron = true;
        // What is special about it, at a glance.
        std::vector<std::string> notes;
        const library::System *system = system_of(tab.key);
        if (system != nullptr ? !prefs.system_enabled(system->name) : prefs.tab_hidden(tab.key))
            notes.push_back(tr("Hidden"));
        const std::string theme = prefs.tab_theme(tab.key);
        if (const ThemeFile *file =
                theme.empty() || themes_ == nullptr ? nullptr : themes_->find(theme))
            notes.push_back(file->name);
        if (prefs.tab_cover(tab.key) != CoverShape::automatic)
            notes.push_back(tr(kCoverNames[static_cast<int>(prefs.tab_cover(tab.key))]));
        if (!prefs.tab_sort(tab.key).empty())
            notes.push_back(tr(kSortNames[sort_index(prefs.tab_sort(tab.key))]));
        for (std::size_t n = 0; n < notes.size(); ++n)
            row.text += (n == 0 ? "" : ", ") + notes[n];
        row.description = tr("Theme, covers, sort and whether it shows");
    }
}

void SettingsPanel::build_tab_page(const LibraryPrefs &prefs)
{
    const TabEntry *tab = tab_entry(page_);
    if (tab == nullptr)
    {
        page_.clear();
        build_tabs(prefs);
        return;
    }
    form_.add_header(tr("Tabs") + "  ·  " + tab->name);
    const library::System *system = system_of(page_);
    {
        const bool shown =
            system != nullptr ? prefs.system_enabled(system->name) : !prefs.tab_hidden(page_);
        form_.add_toggle(kPageShow, tr("Show this tab"), shown).description =
            system != nullptr ? tr("Off hides the tab and its games everywhere")
                              : tr("Off takes it off the tab bar; nothing is deleted");
    }
    // Every tab can be renamed: the player's own here, a built-in one below.
    if (page_.rfind("tab:", 0) != 0)
    {
        ui::FormRow &rename = form_.add_action(kPageRename, tr("Rename"));
        rename.chevron = true;
        rename.description = tr("A name of your own on the tab bar; empty gives it its own back");
    }
    std::vector<std::string> themes = {tr("Main theme")};
    int theme = 0;
    const std::string chosen = prefs.tab_theme(page_);
    std::vector<std::string> names = theme_names();
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        themes.push_back(names[i]);
        if (themes_ != nullptr && themes_->at(i).id == chosen)
            theme = static_cast<int>(i) + 1;
    }
    form_.add_choice(kPageTheme, tr("Theme"), std::move(themes), theme).description =
        tr("This tab can wear a theme of its own");
    form_
        .add_choice(kPageCover, tr("Cover shape"), i18n::tr_range(kCoverNames, kCoverNames + 4),
                    static_cast<int>(prefs.tab_cover(page_)))
        .description = tr("Square for console icons; the box's own shape for box art");
    std::vector<std::string> sorts = {
        trf("Default ({0})", {tr(kSortNames[sort_index(prefs.sort)])})};
    for (const std::string &name : i18n::tr_range(kSortNames, kSortNames + kSortCount))
        sorts.push_back(name);
    const std::string sort = prefs.tab_sort(page_);
    form_
        .add_choice(kPageSort, tr("Sort by"), std::move(sorts),
                    sort.empty() ? 0 : sort_index(sort) + 1)
        .description = tr("The order of the covers on this tab");
    std::vector<std::string> views = {
        trf("Default ({0})", {tr(kViewNames[view_index(prefs.view)])})};
    for (const std::string &name : i18n::tr_range(kViewNames, kViewNames + kViewCount))
        views.push_back(name);
    const std::string view = prefs.tab_view(page_);
    form_
        .add_choice(kPageView, tr("View"), std::move(views),
                    view.empty() ? 0 : view_index(view) + 1)
        .description = tr("Grid, Cases or List on this tab");
    if (system != nullptr && emulator_ != nullptr && system->cores.size() >= 2)
    {
        std::vector<std::string> cores;
        int core = 0;
        for (std::size_t c = 0; c < system->cores.size(); ++c)
        {
            std::string name = system->cores[c];
            for (const library::Core &known : emulator_->cores)
            {
                if (known.file == system->cores[c])
                    name = known.name;
            }
            cores.push_back(std::move(name));
            if (system->cores[c] == prefs.system_core(system->name))
                core = static_cast<int>(c);
        }
        form_.add_choice(kPageCore, tr("Core"), std::move(cores), core).description =
            tr("The RetroArch core these games start with");
    }
    if (page_.rfind("tab:", 0) == 0)
    {
        ui::FormRow &rename = form_.add_action(kPageRename, tr("Rename"));
        rename.chevron = true;
        form_.add_action(kPageLeft, tr("Move left")).description = tr("Earlier on the tab bar");
        form_.add_action(kPageRight, tr("Move right")).description = tr("Later on the tab bar");
        ui::FormRow &remove = form_.add_action(kPageDelete, tr("Delete tab"));
        remove.danger = true;
        remove.description = tr("Its games go back to their shelves");
    }
}

const library::EmulatorApp *SettingsPanel::page_app() const
{
    return emulator_ != nullptr && !emulator_page_.empty() ? emulator_->app(emulator_page_)
                                                           : nullptr;
}

void SettingsPanel::build_emulators(const LibraryPrefs &prefs)
{
    form_.add_header(tr("Emulators"));
    form_.add_action(kScanEmulators, tr("Scan for emulators")).description =
        scan_status_.empty()
            ? trf("Finds {0}, and their games",
                  {"RetroArch, Porpoise, PS5SX2, PSwanStation, ProsperoEden, PS5-ECAM"})
            : scan_status_;
    for (std::size_t i = 0; emulator_ != nullptr && i < emulator_->apps.size(); ++i)
    {
        const library::EmulatorApp &app = emulator_->apps[i];
        ui::FormRow &row = form_.add_action(kEmulatorRow + static_cast<int>(i), app.name);
        row.chevron = true;
        row.text = i18n::plural(app.games, "{0} game", "{0} games");
        std::string plays;
        for (const library::KnownEmulator &known : library::known_emulators())
        {
            if (app.key == known.key)
                plays = tr(known.plays);
        }
        row.description = app.starts_games
                              ? trf("{0}. ORION starts its games", {plays})
                              : trf("{0}. Its games open it; you pick the game there", {plays});
    }
    int missing = 0;
    for (const library::KnownEmulator &known : library::known_emulators())
    {
        if (emulator_ != nullptr && emulator_->app(known.key) != nullptr)
            continue;
        form_.add_value(kMissingRow + missing++, known.name, tr("Not installed")).description =
            trf("{0}. Install it, then scan", {tr(known.plays)});
    }

    form_.add_toggle(kAskPlayer, tr("Ask which emulator to use"), prefs.ask_player)
        .description = tr(
        "A game more than one emulator plays asks the first time; Options, Play with changes it");
    form_.add_toggle(kEveryVersion, tr("Every version of a game"), prefs.every_version)
        .description = tr("Bad dumps, hacks, translations and other regions as covers of their "
                          "own; off shows the best one");
    if (emulator_ == nullptr || emulator_->systems.empty())
        return;
    form_.add_header(tr("Systems"));
    for (std::size_t i = 0; i < emulator_->systems.size(); ++i)
    {
        const library::System &system = emulator_->systems[i];
        const std::string games = i18n::plural(system.games, "{0} game", "{0} games");
        form_
            .add_toggle(kSystemRow + static_cast<int>(i), system.short_name + "  (" + games + ")",
                        prefs.system_enabled(system.name))
            .description = trf("{0}: on shows its tab", {system.name});
        if (system.cores.size() < 2)
            continue;
        std::vector<std::string> names;
        int chosen = 0;
        for (std::size_t c = 0; c < system.cores.size(); ++c)
        {
            std::string name = system.cores[c];
            for (const library::Core &core : emulator_->cores)
            {
                if (core.file == system.cores[c])
                    name = core.name;
            }
            names.push_back(std::move(name));
            if (system.cores[c] == prefs.system_core(system.name))
                chosen = static_cast<int>(c);
        }
        form_
            .add_choice(kCoreRow + static_cast<int>(i), trf("{0} core", {system.short_name}),
                        std::move(names), chosen)
            .description = trf("The RetroArch core {0} games start with", {system.short_name});
    }
}

void SettingsPanel::build_art(const LibraryPrefs &prefs)
{
    form_.add_header(tr("Box art"));
    form_.add_toggle(kArtAuto, tr("Find box art by itself"), prefs.auto_art).description =
        tr("When ORION starts, new games get covers from the internet");
    form_.add_action(kArtNow, tr("Find box art now")).description = art_status_;
    ui::FormRow &grid = form_.add_action(kGridKey, tr("SteamGridDB key"));
    grid.text = grid_key_ ? tr("Set") : tr("Not set");
    grid.description = tr("Free at steamgriddb.com: your profile, Preferences, API. Or a line in "
                          "/data/orion/keys.txt");
    form_.add_value(kStateInfo, tr("Box art folder"), art_folder(prefs, state_dir_)).description =
        tr("Chosen under Storage");
}

void SettingsPanel::build_emulator_page(const library::EmulatorApp &app, const LibraryPrefs &prefs)
{
    form_.add_header(tr("Emulators") + "  ·  " + app.name);
    form_.add_value(kAppInfo, tr("App"), app.title_id).description = app.root;
    form_.add_value(kAppStarts, tr("Games"), std::to_string(app.games)).description =
        app.starts_games ? trf("Cross on a game starts it in {0}", {app.name})
                         : trf("Cross on a game opens {0}; pick the game there", {app.name});

    form_.add_header(tr("Game folders"));
    ui::FormRow &add = form_.add_action(kAddRomFolder, tr("Add a game folder"));
    add.chevron = true;
    add.description =
        app.key == "retroarch"
            ? tr("A system's folder (roms/gba) or a folder of them (roms), under /data or /mnt")
            : tr("A folder of discs (and the folders in it), under /data or /mnt");
    for (std::size_t i = 0; i < prefs.rom_folders.size(); ++i)
    {
        const RomFolder &folder = prefs.rom_folders[i];
        if (folder_key(folder) != app.key)
            continue;
        ui::FormRow &row = form_.add_action(kRomFolderRow + static_cast<int>(i), folder.path);
        row.chevron = true;
        row.text = folder.system.empty() ? tr("Auto") : library::short_system_name(folder.system);
        row.description = tr("Yours: its games join their systems' tabs");
    }
    for (std::size_t i = 0; i < app.folders.size(); ++i)
        form_.add_value(kOwnFolderRow + static_cast<int>(i), app.folders[i], tr("Built in"))
            .description =
            app.key == "retroarch"
                ? tr("RetroArch's content, or a folder its playlists list games from: ORION lists "
                     "them too")
                : trf("Where {0} looks by itself; ORION looks there too", {app.name});
}

void SettingsPanel::browse(std::string path, const Look &look, const LibraryPrefs &prefs)
{
    browsing_ = true;
    browse_path_ = std::move(path);
    browse_folders_.clear();
    if (!browse_path_.empty())
    {
        for (const save::DirectoryEntry &entry : save::list_directory(browse_path_))
        {
            if (entry.directory && entry.name.front() != '.' && browse_folders_.size() < 300)
                browse_folders_.push_back(entry.name);
        }
        std::sort(browse_folders_.begin(), browse_folders_.end());
    }
    build(look, prefs);
    layout();
    form_.set_focus(0);
    form_.enter();
}

// Up one folder; from a place itself (/mnt/usb0), back to the places.
void SettingsPanel::browse_up(const Look &look, const LibraryPrefs &prefs)
{
    const bool place = std::any_of(places_.begin(), places_.end(),
                                   [&](const auto &p) { return p.second == browse_path_; });
    const std::size_t slash = browse_path_.rfind('/');
    const std::string was = browse_path_;
    browse(place || slash == std::string::npos || slash == 0 ? std::string()
                                                             : browse_path_.substr(0, slash),
           look, prefs);
    // The focus lands on the folder just left.
    const std::string left = was.substr(was.rfind('/') + 1);
    for (std::size_t i = 0; i < browse_folders_.size(); ++i)
    {
        if (browse_folders_[i] == left)
            form_.focus_row(kBrowseRow + static_cast<int>(i));
    }
    for (std::size_t i = 0; browse_path_.empty() && i < places_.size(); ++i)
    {
        if (places_[i].second == was)
            form_.focus_row(kBrowseRow + static_cast<int>(i));
    }
}

void SettingsPanel::build_browser()
{
    const library::EmulatorApp *app = page_app();
    const std::string name = app != nullptr ? app->name : tr("the emulator");
    if (browse_path_.empty())
    {
        form_.add_header(tr("Add a game folder") + "  ·  " + name);
        int shown = 0;
        for (std::size_t i = 0; i < places_.size(); ++i)
        {
            struct stat info = {};
            if (::stat(places_[i].second.c_str(), &info) != 0 || !S_ISDIR(info.st_mode))
                continue;
            ui::FormRow &row = form_.add_action(kBrowseRow + static_cast<int>(i), places_[i].first);
            row.chevron = true;
            row.text = places_[i].second;
            row.description = tr("Open it and pick the folder your games are in");
            ++shown;
        }
        if (shown == 0)
            form_.add_value(kBrowseUp, tr("No drive found"), tr("Plug one in, or type a path"));
    }
    else
    {
        form_.add_header(tr("Add a game folder") + "  ·  " + browse_path_);
        form_.add_action(kBrowseUse, tr("Use this folder")).description =
            trf("Its games join {0}'s; the folders in it are looked in too", {name});
        ui::FormRow &up = form_.add_action(kBrowseUp, tr("Up one folder"));
        up.description = tr("Circle does the same");
        for (std::size_t i = 0; i < browse_folders_.size(); ++i)
        {
            ui::FormRow &row =
                form_.add_action(kBrowseRow + static_cast<int>(i), browse_folders_[i]);
            row.chevron = true;
            row.description = tr("Open it");
        }
        if (browse_folders_.empty())
            form_.add_value(kBrowseRow, tr("No folders in here"), "");
    }
    form_.add_action(kBrowseType, tr("Type a path instead")).description =
        tr("Any folder under /data or /mnt, with the keyboard");
}

void SettingsPanel::build_sound(const LibraryPrefs &prefs)
{
    form_.add_header(tr("Sound"));
    if (app_settings_ == nullptr)
        return;
    // 0..10, 0 said as Off.
    std::vector<std::string> levels = {tr("Off")};
    for (int i = 1; i <= 10; ++i)
        levels.push_back(std::to_string(i));
    form_
        .add_choice(kMusicVolume, tr("Music"), levels,
                    std::clamp(app_settings_->music_volume, 0, 10))
        .description = tr("The songs behind ORION; Off stops them");
    form_
        .add_choice(kSfxVolume, tr("Sound effects"), levels,
                    std::clamp(app_settings_->sfx_volume, 0, 10))
        .description = tr("Launching, saving, refusing");
    form_
        .add_choice(kUiVolume, tr("Menu sounds"), levels,
                    std::clamp(app_settings_->ui_volume, 0, 10))
        .description = tr("Moving the focus, opening and closing");
    form_.add_value(kNowPlaying, tr("Now playing"),
                    app_settings_->music_volume == 0 ? tr("Music is off")
                    : now_playing_.empty()           ? tr("Between songs")
                                                     : now_playing_);
    form_.add_toggle(kTitleMusic, tr("Title music"), prefs.title_music).description =
        tr("A focused game plays its own music, as on the home screen; yours from "
           "/data/orion/hover-music first");
    form_.add_action(kNextSong, tr("Next song")).description =
        tr("Your own songs join when ORION starts: .ogg files, 48 kHz, in the folder below");
    form_.add_value(kMusicFolder, tr("Your music"), "/data/orion/music");
}

void SettingsPanel::build_storage(const LibraryPrefs &prefs)
{
    form_.add_header(tr("Folders"));
    form_
        .add_choice(kArtFolder, tr("Box art folder"), i18n::tr_range(kArtPlaces, kArtPlaces + 4),
                    art_place(prefs.art_dir))
        .description =
        trf("Looked in first, and where downloads go: {0}. Moving it copies what is there",
            {art_folder(prefs, state_dir_)});
    const std::string backdrops = state_dir_ + "/backdrops";
    form_
        .add_value(kBackdropsInfo, tr("Game background copies"),
                   i18n::plural(backdrops_, "{0} picture", "{0} pictures"))
        .description = trf(
        "{0}: the console's pictures for games that ship none, through the FTP server on port 2121",
        {backdrops});
    form_.add_action(kBackdropsAgain, tr("Copy them again")).description =
        tr("Asks the FTP server for every missing picture once more");
    form_.add_value(kThemesInfo, tr("Your themes"), state_dir_ + "/themes").description =
        tr("Theme files you save, and ones you add by hand");
    form_.add_value(kStateInfo + 100, tr("Settings and play records"), state_dir_).description =
        tr("library.json, theme.json and the launch logs; the launch helper looks for them here");
}

void SettingsPanel::build_folder_page(const LibraryPrefs &prefs)
{
    const RomFolder &folder = prefs.rom_folders[static_cast<std::size_t>(folder_page_)];
    form_.add_header(tr("Game folders") + "  ·  " + folder.path);
    std::vector<std::string> names = {folder_key(folder) == "retroarch"
                                          ? tr("Auto (from the folder's name)")
                                          : tr("Auto (from each disc)")};
    int chosen = 0;
    if (emulator_ != nullptr)
    {
        const std::vector<std::string> systems =
            library::known_systems(*emulator_, folder_key(folder));
        for (std::size_t i = 0; i < systems.size(); ++i)
        {
            names.push_back(library::short_system_name(systems[i]));
            if (systems[i] == folder.system)
                chosen = static_cast<int>(i) + 1;
        }
    }
    form_.add_choice(kFolderSystem, tr("System"), std::move(names), chosen).description =
        tr("Auto reads gba, nds, ps2... from the folder's name, or from the folders inside it");
    ui::FormRow &remove = form_.add_action(kFolderRemove, tr("Remove this folder"));
    remove.danger = true;
    remove.description = tr("ORION stops listing its games; nothing on the drive is touched");
}

SettingsPanel::Change SettingsPanel::edit_folder_page(int id, LibraryPrefs *prefs)
{
    if (folder_page_ < 0 || folder_page_ >= static_cast<int>(prefs->rom_folders.size()))
        return Change::none;
    RomFolder &folder = prefs->rom_folders[static_cast<std::size_t>(folder_page_)];
    if (id == kFolderSystem)
    {
        const int index = form_.choice_index(kFolderSystem);
        const std::vector<std::string> systems =
            emulator_ != nullptr ? library::known_systems(*emulator_, folder_key(folder))
                                 : std::vector<std::string>();
        folder.system = index > 0 && index <= static_cast<int>(systems.size())
                            ? systems[static_cast<std::size_t>(index - 1)]
                            : std::string();
        return Change::folders;
    }
    if (id == kFolderRemove)
    {
        prefs->rom_folders.erase(prefs->rom_folders.begin() + folder_page_);
        folder_page_ = -1; // its page is gone: back to the emulator's
        return Change::folders;
    }
    return Change::none;
}

std::vector<std::string> SettingsPanel::theme_names() const
{
    std::vector<std::string> names;
    for (std::size_t i = 0; themes_ != nullptr && i < themes_->size(); ++i)
    {
        const ThemeFile &file = themes_->at(i);
        // The player's own themes say so.
        names.push_back(file.user ? trf("{0} (yours)", {file.name}) : file.name);
    }
    return names;
}

void SettingsPanel::build_appearance(const Look &look, const LibraryPrefs &prefs)
{
    form_.add_header(tr("Appearance"));
    std::vector<std::string> style_names = theme_names();
    int base = 0;
    for (std::size_t i = 0; themes_ != nullptr && i < themes_->size(); ++i)
    {
        if (look.base == themes_->at(i).id)
            base = static_cast<int>(i);
    }
    if (style_names.empty())
        style_names.emplace_back("ORION");
    // The tab behind Settings can wear a theme of its own: chosen here, where
    // the preview shows it, as well as on its page under Tabs.
    if (const TabEntry *tab = tab_entry(current_tab_))
    {
        std::vector<std::string> themes = {tr("Same as every tab")};
        int theme = 0;
        const std::string chosen = prefs.tab_theme(current_tab_);
        const std::vector<std::string> names = theme_names();
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            themes.push_back(names[i]);
            if (themes_ != nullptr && themes_->at(i).id == chosen)
                theme = static_cast<int>(i) + 1;
        }
        form_.add_choice(kTabTheme, trf("Theme for {0}", {tab->name}), std::move(themes), theme)
            .description = theme == 0 ? tr("Only this tab; it now wears the theme below")
                                      : tr("Only this tab; the rows below are every other tab's");
    }
    form_.add_choice(kBase, tr("Theme for every tab"), std::move(style_names), base).description =
        tr("Built in, or a theme file of yours; everything below adjusts it");
    std::vector<std::string> background_names;
    int background = 0;
    for (std::size_t i = 0; i < backgrounds().size(); ++i)
    {
        background_names.push_back(tr(backgrounds()[i].name));
        if (backgrounds()[i].mode == look.background)
            background = static_cast<int>(i);
    }
    form_.add_choice(kBackground, tr("Background"), std::move(background_names), background)
        .description = tr("What moves behind the glass");
    add_swatches(form_, kSky, i18n::marked("Sky"), skies(), look.sky_top, look.sky_bottom, true,
                 i18n::marked("The colour of empty space"));
    add_swatches(form_, kNebula, i18n::marked("Nebula"), nebulas(), look.nebula, look.nebula_far,
                 true, i18n::marked("The glow in the sky, and the clouds that drift through it"));
    form_.add_slider(kClouds, tr("Clouds"), look.clouds * 100.0f, 0.0f, 200.0f, 10.0f).unit = " %";
    form_.add_toggle(kMotion, tr("Motion"), look.motion).description =
        tr("The sky drifts slowly; off holds it still");
    add_swatches(form_, kGlass, i18n::marked("Glass tint"), glasses(), look.glass, look.glass,
                 false, i18n::marked("The colour frosted panels are tinted with"));
    form_.add_slider(kOpacity, tr("Glass opacity"), look.glass.a * 100.0f, 0.0f, 100.0f, 5.0f)
        .unit = " %";
    add_swatches(form_, kHighlight, i18n::marked("Highlight"), highlights(), look.highlight,
                 look.highlight, false, i18n::marked("The selected tab and the focused key"));
    add_swatches(form_, kAccent, i18n::marked("Accent"), accents(), look.accent, look.accent, false,
                 i18n::marked("Labels, counts and the text cursor"));
    form_.add_slider(kCorners, tr("Cover corners"), look.corners, 0.0f, 40.0f, 2.0f).unit = " px";
    form_
        .add_choice(kTabBar, tr("Tab bar"), {tr("Plain"), tr("Pills")},
                    look.chrome.tabs == TabStyle::plain ? 0 : 1)
        .description = tr("Plain text like the home screen, or pills on a frosted track");
    form_
        .add_choice(kFocusRing, tr("Focus ring"), {tr("Aurora"), tr("Theme")},
                    look.chrome.focus == FocusRing::aurora ? 0 : 1)
        .description = tr("A ring that drifts through the colours, or the theme's own");
    form_
        .add_choice(kBars, tr("Bottom bars"), {tr("None"), tr("Outline"), tr("Glass")},
                    look.chrome.plates == Plates::none      ? 0
                    : look.chrome.plates == Plates::outline ? 1
                                                            : 2)
        .description = tr("Behind the title details and button hints: nothing, a rim, or glass");
    form_.add_toggle(kBadges, tr("Category banners"), look.chrome.badges).description =
        tr("GAME, HOMEBREW, MEDIA or PS4 on each cover");
    form_.add_toggle(kRim, tr("Cover rim"), look.chrome.cover_rim).description =
        tr("A hairline of light around each cover");
    form_.add_toggle(kGameBackgrounds, tr("Game backgrounds"), prefs.game_backgrounds).description =
        tr("The focused game's home-screen picture fades in behind the library; off keeps the sky");
    form_.add_toggle(kBlurredCovers, tr("Blurred covers"), prefs.blurred_covers).description = tr(
        "For games with no picture of their own: their cover, blurred. Off keeps the sky for them");
    form_
        .add_slider(kBackgroundStrength, tr("Background strength"),
                    prefs.background_strength * 100.0f, 20.0f, 100.0f, 10.0f)
        .unit = " %";
    ui::FormRow &save = form_.add_action(kSaveTheme, tr("Save as new theme"));
    save.description =
        tr("Keeps this look as a theme file in /data/orion/themes, to reuse or share");
    save.chevron = true;
    form_.add_action(kRestore, tr("Restore the ORION look")).description =
        tr("Back to the galaxy glass ORION starts with");
}

void SettingsPanel::set_art_status(std::string status)
{
    if (status == art_status_)
        return;
    art_status_ = std::move(status);
    if (section_ == Section::art)
    {
        if (ui::FormRow *row = form_.row(kArtNow))
            row->description = art_status_;
    }
}

const TabEntry *SettingsPanel::tab_entry(const std::string &key) const
{
    for (const TabEntry &tab : tabs_)
    {
        if (tab.key == key)
            return &tab;
    }
    return nullptr;
}

const library::System *SettingsPanel::system_of(const std::string &key) const
{
    if (emulator_ == nullptr || key.rfind("sys:", 0) != 0)
        return nullptr;
    for (const library::System &system : emulator_->systems)
    {
        if (key.compare(4, std::string::npos, system.name) == 0)
            return &system;
    }
    return nullptr;
}

SettingsPanel::Change SettingsPanel::handle(const InputFrame &input, ui::Feedback &feedback,
                                            Look *look, LibraryPrefs *prefs)
{
    if (!sheet_.is_open())
        return Change::none;

    // L1 and R1 step through the sections from anywhere (and the jumps the
    // library takes for shelves, as the shelves do).
    const bool next = input.is_pressed(Action::page_next) || input.is_pressed(Action::jump_next);
    const bool previous =
        input.is_pressed(Action::page_prev) || input.is_pressed(Action::jump_prev);
    if (next || previous)
    {
        const auto at = std::find(sections_.begin(), sections_.end(), section_);
        const int index = static_cast<int>(at - sections_.begin()) + (next ? 1 : -1);
        if (index < 0 || index >= static_cast<int>(sections_.size()))
        {
            feedback.play(audio::Cue::error);
            return Change::none;
        }
        show(sections_[static_cast<std::size_t>(index)], *look, *prefs);
        if (in_rows_)
            enter_rows(true);
        feedback.play(audio::Cue::tab);
        return Change::none;
    }

    if (!in_rows_)
    {
        if (input.nav == Direction::right)
        {
            enter_rows(false);
            feedback.play(audio::Cue::focus);
            return Change::none;
        }
        const ui::Event event = nav_.handle(input, feedback);
        if (event == ui::Event::moved)
            show(sections_[static_cast<std::size_t>(nav_.focus())], *look, *prefs);
        else if (event == ui::Event::activated)
            enter_rows(false);
        else if (event == ui::Event::cancelled)
            close(feedback);
        return Change::none;
    }

    // In the rows: back (or left where it means nothing) goes out a level.
    const bool left_out = input.nav == Direction::left && !form_.uses_horizontal();
    if (input.is_pressed(Action::back) || left_out)
    {
        feedback.play(audio::Cue::back);
        if (browsing_ && !browse_path_.empty())
            browse_up(*look, *prefs);
        else if (browsing_)
        {
            browsing_ = false;
            build(*look, *prefs);
            layout();
            form_.focus_row(kAddRomFolder);
        }
        else if (folder_page_ >= 0)
        {
            const int was = folder_page_;
            folder_page_ = -1;
            build(*look, *prefs);
            layout();
            form_.focus_row(kRomFolderRow + was);
        }
        else if (!emulator_page_.empty())
        {
            const std::string was = emulator_page_;
            emulator_page_.clear();
            build(*look, *prefs);
            layout();
            for (std::size_t i = 0; emulator_ != nullptr && i < emulator_->apps.size(); ++i)
            {
                if (emulator_->apps[i].key == was)
                    form_.focus_row(kEmulatorRow + static_cast<int>(i));
            }
        }
        else if (!page_.empty())
        {
            const std::string was = page_;
            page_.clear();
            build(*look, *prefs);
            layout();
            for (std::size_t i = 0; i < tabs_.size(); ++i)
            {
                if (tabs_[i].key == was)
                    form_.focus_row(kTabRow + static_cast<int>(i));
            }
        }
        else
            leave_rows();
        return Change::none;
    }

    const ui::Event event = form_.handle(input, feedback);
    const int id = form_.changed_id();
    if (event == ui::Event::activated)
    {
        if (id == kSaveTheme)
            return Change::save_theme;
        if (id == kRestore && themes_ != nullptr)
        {
            *look = default_look(*themes_);
            refresh(*look, *prefs);
            return Change::look;
        }
        if (id == kNewTab)
            return Change::new_tab;
        if (id == kArtNow)
            return Change::fetch_art;
        if (id == kGridKey)
            return Change::type_grid_key;
        if (id == kAddRomFolder)
        {
            browse("", *look, *prefs);
            return Change::none;
        }
        if (browsing_)
        {
            if (id == kBrowseUse)
            {
                browsing_ = false;
                build(*look, *prefs);
                layout();
                form_.focus_row(kAddRomFolder);
                return Change::add_folder;
            }
            if (id == kBrowseUp)
            {
                browse_up(*look, *prefs);
                return Change::none;
            }
            if (id == kBrowseType)
            {
                browsing_ = false;
                build(*look, *prefs);
                layout();
                form_.focus_row(kAddRomFolder);
                return Change::add_rom_folder;
            }
            const int index = id - kBrowseRow;
            if (browse_path_.empty() && index >= 0 && index < static_cast<int>(places_.size()))
                browse(places_[static_cast<std::size_t>(index)].second, *look, *prefs);
            else if (index >= 0 && index < static_cast<int>(browse_folders_.size()))
                browse(browse_path_ + "/" + browse_folders_[static_cast<std::size_t>(index)], *look,
                       *prefs);
            return Change::none;
        }
        if (id == kBackdropsAgain)
            return Change::copy_backdrops;
        if (id == kNextSong)
            return Change::skip_song;
        if (id == kRunSetup)
            return Change::run_setup;
        if (id == kUpdateNow)
            return Change::check_updates;
        if (id == kInstallUpdate)
            return Change::install_update;
        if (id == kScanEmulators)
            return Change::scan_emulators;
        if (emulator_ != nullptr && id >= kEmulatorRow &&
            id - kEmulatorRow < static_cast<int>(emulator_->apps.size()))
        {
            emulator_page_ = emulator_->apps[static_cast<std::size_t>(id - kEmulatorRow)].key;
            build(*look, *prefs);
            layout();
            form_.set_focus(0);
            form_.enter();
            return Change::none;
        }
        if (id >= kRomFolderRow && id - kRomFolderRow < static_cast<int>(prefs->rom_folders.size()))
        {
            folder_page_ = id - kRomFolderRow;
            build(*look, *prefs);
            layout();
            form_.set_focus(0);
            form_.enter();
            return Change::none;
        }
        if (folder_page_ >= 0)
            return edit_folder_page(id, prefs);
        if (id >= kTabRow && id - kTabRow < static_cast<int>(tabs_.size()))
        {
            page_ = tabs_[static_cast<std::size_t>(id - kTabRow)].key;
            build(*look, *prefs);
            layout();
            form_.set_focus(0);
            form_.enter();
            return Change::none;
        }
        if (!page_.empty())
            return edit_tab_page(id, prefs);
        return Change::none;
    }
    if (event != ui::Event::changed)
        return Change::none;

    if (!page_.empty())
        return edit_tab_page(id, prefs);
    if (folder_page_ >= 0)
        return edit_folder_page(id, prefs);
    if (emulator_ != nullptr && id >= kSystemRow &&
        id < kSystemRow + static_cast<int>(emulator_->systems.size()))
    {
        const library::System &system =
            emulator_->systems[static_cast<std::size_t>(id - kSystemRow)];
        prefs->set_system_enabled(system.name, form_.toggle_value(id));
        return Change::prefs;
    }
    if (emulator_ != nullptr && id >= kCoreRow &&
        id < kCoreRow + static_cast<int>(emulator_->systems.size()))
    {
        const library::System &system = emulator_->systems[static_cast<std::size_t>(id - kCoreRow)];
        const int index = form_.choice_index(id);
        if (index >= 0 && index < static_cast<int>(system.cores.size()))
            prefs->set_system_core(system.name, system.cores[static_cast<std::size_t>(index)]);
        return Change::prefs;
    }
    switch (id)
    {
    case kLanguage:
    {
        const int index = form_.choice_index(kLanguage);
        if (index < 0 || index >= static_cast<int>(languages_.size()))
            return Change::none;
        prefs->language = languages_[static_cast<std::size_t>(index)].code;
        return Change::language;
    }
    case kSort:
        prefs->sort = kSortKeys[std::clamp(form_.choice_index(kSort), 0, kSortCount - 1)];
        return Change::prefs;
    case kAutoUpdate:
        prefs->auto_update = form_.toggle_value(kAutoUpdate);
        return Change::prefs;
    case kCheckUpdates:
        prefs->check_updates = form_.toggle_value(kCheckUpdates);
        return Change::prefs;
    case kView:
        prefs->view = kViewKeys[std::clamp(form_.choice_index(kView), 0, kViewCount - 1)];
        return Change::prefs;
    case kMusicVolume:
    case kSfxVolume:
    case kUiVolume:
    {
        if (app_settings_ == nullptr)
            return Change::none;
        const int level = std::clamp(form_.choice_index(id), 0, 10);
        (id == kMusicVolume ? app_settings_->music_volume
         : id == kSfxVolume ? app_settings_->sfx_volume
                            : app_settings_->ui_volume) = level;
        return Change::sound;
    }
    case kAutostart:
        prefs->autostart = form_.toggle_value(kAutostart);
        return Change::prefs;
    case kReopenAfterRest:
        prefs->reopen_after_rest = form_.toggle_value(kReopenAfterRest);
        return Change::prefs;
    case kTitleMusic:
        prefs->title_music = form_.toggle_value(kTitleMusic);
        return Change::prefs;
    case kAskPlayer:
        prefs->ask_player = form_.toggle_value(kAskPlayer);
        return Change::prefs;
    case kEveryVersion:
        // The games are listed again, with or without the other versions.
        prefs->every_version = form_.toggle_value(kEveryVersion);
        return Change::scan_emulators;
    case kGroupSystems:
        prefs->group_systems = form_.toggle_value(kGroupSystems);
        return Change::prefs;
    case kContinueRow:
        prefs->continue_row = form_.toggle_value(kContinueRow);
        return Change::prefs;
    case kReopen:
        prefs->reopen_last_tab = form_.toggle_value(kReopen);
        return Change::prefs;
    case kLeaveShelf:
        prefs->moved_leave_shelf = form_.toggle_value(kLeaveShelf);
        return Change::prefs;
    case kArtAuto:
        prefs->auto_art = form_.toggle_value(kArtAuto);
        return Change::prefs;
    case kReturn:
        prefs->return_on_close = form_.toggle_value(kReturn);
        return Change::prefs;
    case kGameBackgrounds:
        prefs->game_backgrounds = form_.toggle_value(kGameBackgrounds);
        return Change::prefs;
    case kBlurredCovers:
        prefs->blurred_covers = form_.toggle_value(kBlurredCovers);
        return Change::prefs;
    case kBackgroundStrength:
        prefs->background_strength = form_.slider_value(kBackgroundStrength) / 100.0f;
        return Change::prefs;
    case kArtFolder:
    {
        const int place = form_.choice_index(kArtFolder);
        if (place >= 3)
            return Change::type_art_folder;
        prefs->art_dir = kArtPaths[std::max(place, 0)];
        if (ui::FormRow *row = form_.row(kArtFolder))
            row->description =
                trf("Looked in first, and where downloads go: {0}. Moving it copies what is there",
                    {art_folder(*prefs, state_dir_)});
        return Change::art_folder;
    }
    default:
    {
        // The one Appearance row that is the tab's, kept in the prefs.
        if (id == kTabTheme)
        {
            const int index = form_.choice_index(kTabTheme);
            const bool own =
                themes_ != nullptr && index > 0 && index <= static_cast<int>(themes_->size());
            prefs->set_tab_theme(current_tab_,
                                 own ? themes_->at(static_cast<std::size_t>(index - 1)).id : "");
            refresh(*look, *prefs); // its description says which rows apply
            return Change::prefs;
        }
        return edit_appearance(id, look, *prefs);
    }
    }
}

SettingsPanel::Change SettingsPanel::edit_tab_page(int id, LibraryPrefs *prefs)
{
    const library::System *system = system_of(page_);
    const std::string custom = page_.rfind("tab:", 0) == 0 ? page_.substr(4) : std::string();
    switch (id)
    {
    case kPageShow:
        if (system != nullptr)
            prefs->set_system_enabled(system->name, form_.toggle_value(kPageShow));
        else
            prefs->set_tab_hidden(page_, !form_.toggle_value(kPageShow));
        return Change::prefs;
    case kPageTheme:
    {
        const int index = form_.choice_index(kPageTheme);
        const bool own =
            themes_ != nullptr && index > 0 && index <= static_cast<int>(themes_->size());
        prefs->set_tab_theme(page_, own ? themes_->at(static_cast<std::size_t>(index - 1)).id : "");
        return Change::prefs;
    }
    case kPageCover:
        prefs->set_tab_cover(
            page_, static_cast<CoverShape>(std::clamp(form_.choice_index(kPageCover), 0, 3)));
        return Change::prefs;
    case kPageSort:
    {
        const int index = form_.choice_index(kPageSort);
        prefs->set_tab_sort(page_, index <= 0 ? std::string()
                                              : kSortKeys[std::min(index - 1, kSortCount - 1)]);
        return Change::prefs;
    }
    case kPageView:
    {
        const int index = form_.choice_index(kPageView);
        prefs->set_tab_view(page_, index <= 0 ? std::string()
                                              : kViewKeys[std::min(index - 1, kViewCount - 1)]);
        return Change::prefs;
    }
    case kPageCore:
        if (system != nullptr)
        {
            const int index = form_.choice_index(kPageCore);
            if (index >= 0 && index < static_cast<int>(system->cores.size()))
                prefs->set_system_core(system->name,
                                       system->cores[static_cast<std::size_t>(index)]);
        }
        return Change::prefs;
    case kPageRename:
        // The player's own tab by its id, a built-in one by its key.
        tab_id_ = custom.empty() ? page_ : custom;
        return Change::rename_tab;
    case kPageLeft:
    case kPageRight:
        return !custom.empty() && prefs->move_tab(custom, id == kPageLeft ? -1 : 1) ? Change::prefs
                                                                                    : Change::none;
    case kPageDelete:
        if (custom.empty() || !prefs->remove_tab(custom))
            return Change::none;
        page_.clear(); // its page is gone: back to the list
        return Change::prefs;
    default:
        return Change::none;
    }
}

SettingsPanel::Change SettingsPanel::edit_appearance(int id, Look *look, const LibraryPrefs &prefs)
{
    switch (id)
    {
    case kBase:
    {
        if (themes_ == nullptr || themes_->size() == 0)
            return Change::none;
        const int index =
            std::clamp(form_.choice_index(kBase), 0, static_cast<int>(themes_->size()) - 1);
        *look = look_of(themes_->at(static_cast<std::size_t>(index)));
        refresh(*look, prefs); // every appearance row starts over
        return Change::look;
    }
    case kBackground:
        look->background = backgrounds()[static_cast<std::size_t>(std::clamp(
                                             form_.choice_index(kBackground), 0,
                                             static_cast<int>(backgrounds().size()) - 1))]
                               .mode;
        return Change::look;
    case kSky:
        if (const Swatch *swatch = picked(form_, kSky, skies()))
        {
            look->sky_top = Color::rgb(swatch->first);
            look->sky_bottom = Color::rgb(swatch->second);
        }
        return Change::look;
    case kNebula:
        if (const Swatch *swatch = picked(form_, kNebula, nebulas()))
        {
            look->nebula = Color::rgb(swatch->first);
            look->nebula_far = Color::rgb(swatch->second);
        }
        return Change::look;
    case kClouds:
        look->clouds = form_.slider_value(kClouds) / 100.0f;
        return Change::look;
    case kMotion:
        look->motion = form_.toggle_value(kMotion);
        return Change::look;
    case kGlass:
        if (const Swatch *swatch = picked(form_, kGlass, glasses()))
            look->glass = Color::rgb(swatch->first, look->glass.a);
        return Change::look;
    case kOpacity:
        look->glass.a = form_.slider_value(kOpacity) / 100.0f;
        return Change::look;
    case kHighlight:
        if (const Swatch *swatch = picked(form_, kHighlight, highlights()))
            look->highlight = Color::rgb(swatch->first);
        return Change::look;
    case kAccent:
        if (const Swatch *swatch = picked(form_, kAccent, accents()))
            look->accent = Color::rgb(swatch->first);
        return Change::look;
    case kCorners:
        look->corners = form_.slider_value(kCorners);
        return Change::look;
    case kTabBar:
        look->chrome.tabs = form_.choice_index(kTabBar) == 0 ? TabStyle::plain : TabStyle::pills;
        return Change::look;
    case kFocusRing:
        look->chrome.focus =
            form_.choice_index(kFocusRing) == 0 ? FocusRing::aurora : FocusRing::theme;
        return Change::look;
    case kBars:
        look->chrome.plates = form_.choice_index(kBars) == 0   ? Plates::none
                              : form_.choice_index(kBars) == 1 ? Plates::outline
                                                               : Plates::glass;
        return Change::look;
    case kBadges:
        look->chrome.badges = form_.toggle_value(kBadges);
        return Change::look;
    case kRim:
        look->chrome.cover_rim = form_.toggle_value(kRim);
        return Change::look;
    default:
        return Change::none;
    }
}

void SettingsPanel::update(float dt)
{
    // Wide enough for the rows and their values, and no wider: the rest of
    // the screen is the preview.
    constexpr float kPanel = 820.0f;
    width_.target = previewing() ? kPanel : gfx::kVirtualWidth;
    if (sheet_.style.reduced_motion || !sheet_.visible())
        width_.snap(width_.target);
    else
        width_.update(dt, 14.0f);
    const float wide =
        std::clamp((width_.value - kPanel) / (gfx::kVirtualWidth - kPanel), 0.0f, 1.0f);
    if (sheet_.style.size != width_.value || wide != wide_)
    {
        sheet_.style.size = width_.value;
        // No veil over what is being previewed.
        sheet_.style.scrim = 0.35f * wide;
        wide_ = wide;
        layout();
    }
    sheet_.update(dt);
    nav_.update(dt);
    form_.update(dt);
}

void SettingsPanel::draw_content(ui::Canvas &canvas, float opacity) const
{
    (void)opacity;
    if (wide_ > 0.01f)
    {
        canvas.list.push_opacity(wide_);
        nav_.draw(canvas);
        canvas.list.pop_opacity();
    }
    form_.draw(canvas);
    // What the buttons do here, under the rows.
    const ui::Theme &theme = form_.style.theme;
    ui::GlyphStyle glyphs = ui::GlyphStyle::dark();
    glyphs.body = theme.text.with_alpha(0.14f);
    glyphs.edge = theme.text.with_alpha(0.0f);
    glyphs.ink = theme.text;
    glyphs.label = theme.text_muted;
    // Hints hold pointers: their words live until the hints are drawn.
    const std::string sections = tr("Sections");
    const std::string select = tr("Select");
    const std::string back = in_rows_ ? tr("Back") : tr("Close");
    const ui::Hint hints[] = {
        {ui::Button::l1, sections.c_str(), ui::Button::r1},
        {ui::Button::cross, select.c_str()},
        {ui::Button::circle, back.c_str()},
    };
    ui::HintLayout layout;
    layout.size = 38.0f;
    layout.text_size = 22.0f;
    const gfx::Rect area = sheet_.content_rect();
    layout.cy = area.y + area.h - 26.0f;
    ui::draw_hints(canvas.list, canvas.fonts, glyphs, hints, 3, area.x + area.w, true, layout);
}

void SettingsPanel::draw(ui::Canvas &canvas) const
{
    sheet_.draw(canvas);
}

} // namespace hui::orion
