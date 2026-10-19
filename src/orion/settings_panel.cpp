// ps5-homebrew-ui - ORION's Settings: a full screen of sections, with a page for every tab.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/settings_panel.hpp"

#include "gfx/draw_list.hpp"
#include "ui/glyphs.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hui::orion
{

namespace
{

using gfx::Color;

enum : int
{
    kSort = 1,
    kReopen,
    kLeaveShelf,
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
    kArtNow,
    kArtFolder,
    // A tab's page.
    kPageShow,
    kPageTheme,
    kPageCover,
    kPageSort,
    kPageCore,
    kPageRename,
    kPageLeft,
    kPageRight,
    kPageDelete,
    kTabRow = 100,    // + the tab's index in tabs_
    kSystemRow = 200, // + the system's index: on or off
    kCoreRow = 300,   // + the system's index: which core
};

constexpr const char *kSortKeys[] = {"name", "name_reversed", "recent", "platform"};
constexpr const char *kSortNames[] = {"Name A–Z", "Name Z–A", "Recently added", "Platform"};
// Where box art can live: ORION's folder, a USB drive, the extended storage,
// or a folder the player types.
constexpr const char *kArtPlaces[] = {"ORION's folder", "USB drive", "Extended storage",
                                      "Your own folder"};
constexpr const char *kArtPaths[] = {"", "/mnt/usb0/ORION/art", "/mnt/ext0/ORION/art"};

int art_place(const std::string &dir)
{
    for (int i = 0; i < 3; ++i)
    {
        if (dir == kArtPaths[i])
            return i;
    }
    return 3;
}

constexpr const char *kCoverNames[] = {"Fit the pictures", "Square", "Portrait (5:7)",
                                       "Landscape (7:5)"};

const char *section_name(SettingsPanel::Section section)
{
    switch (section)
    {
    case SettingsPanel::Section::library:
        return "Library";
    case SettingsPanel::Section::tabs:
        return "Tabs";
    case SettingsPanel::Section::emulators:
        return "Emulators";
    case SettingsPanel::Section::art:
        return "Box art";
    default:
        return "Appearance";
    }
}

int sort_index(const std::string &key)
{
    for (int i = 0; i < 4; ++i)
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
        names.emplace_back(swatch.name);
    if (custom)
        names.emplace_back("Custom");
    return names;
}

void add_swatches(ui::Form &form, int id, const char *label, std::span<const Swatch> swatches,
                  Color first, Color second, bool pair, const char *description)
{
    const int index = swatch_index(swatches, first, second, pair);
    form.add_choice(id, label, swatch_names(swatches, index < 0),
                    index < 0 ? static_cast<int>(swatches.size()) : index)
        .description = description;
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
    sheet_.set_title("Settings");
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
    sections_ = {Section::library, Section::tabs};
    if (emulator_ != nullptr && !emulator_->systems.empty())
    {
        sections_.push_back(Section::emulators);
        sections_.push_back(Section::art);
    }
    sections_.push_back(Section::appearance);
    std::vector<ui::NavEntry> entries;
    for (Section section : sections_)
    {
        ui::NavEntry entry;
        entry.label = section_name(section);
        entries.push_back(std::move(entry));
    }
    nav_.set_entries(std::move(entries));
}

void SettingsPanel::layout()
{
    const gfx::Rect area = sheet_.content_rect();
    constexpr float kHints = 70.0f; // the button hints under everything
    nav_.set_bounds({area.x, area.y, 0.0f, area.h - kHints});
    const float left = area.x + nav_.style.expanded_width + 56.0f;
    const float width = std::min(area.x + area.w - left, 1180.0f);
    form_.set_bounds({left, area.y, width, area.h - kHints});
}

void SettingsPanel::show(Section section, const Look &look, const LibraryPrefs &prefs)
{
    section_ = section;
    page_.clear();
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
        build_emulators(prefs);
        break;
    case Section::art:
        build_art(prefs);
        break;
    case Section::appearance:
        build_appearance(look);
        break;
    }
}

void SettingsPanel::build_library(const LibraryPrefs &prefs)
{
    form_.add_header("Library");
    form_.add_choice(kSort, "Sort by", {kSortNames, kSortNames + 4}, sort_index(prefs.sort))
        .description = "The order of the covers, on every tab without an order of its own";
    form_.add_toggle(kReopen, "Reopen on last tab", prefs.reopen_last_tab).description =
        "ORION starts on the tab you left it on";
    form_.add_toggle(kLeaveShelf, "Moved titles leave shelf", prefs.moved_leave_shelf).description =
        "A title moved to your own tab leaves Games, Homebrew or Media";
}

void SettingsPanel::build_tabs(const LibraryPrefs &prefs)
{
    form_.add_header("Tabs");
    ui::FormRow &add = form_.add_action(kNewTab, "New tab");
    add.chevron = true;
    add.description = "A shelf of your own: move any game into it from its menu";
    for (std::size_t i = 0; i < tabs_.size(); ++i)
    {
        const TabEntry &tab = tabs_[i];
        ui::FormRow &row = form_.add_action(kTabRow + static_cast<int>(i), tab.name);
        row.chevron = true;
        // What is special about it, at a glance.
        std::vector<std::string> notes;
        const library::System *system = system_of(tab.key);
        if (system != nullptr ? !prefs.system_enabled(system->name) : prefs.tab_hidden(tab.key))
            notes.emplace_back("Hidden");
        const std::string theme = prefs.tab_theme(tab.key);
        if (const ThemeFile *file =
                theme.empty() || themes_ == nullptr ? nullptr : themes_->find(theme))
            notes.push_back(file->name);
        if (prefs.tab_cover(tab.key) != CoverShape::automatic)
            notes.emplace_back(kCoverNames[static_cast<int>(prefs.tab_cover(tab.key))]);
        if (!prefs.tab_sort(tab.key).empty())
            notes.emplace_back(kSortNames[sort_index(prefs.tab_sort(tab.key))]);
        for (std::size_t n = 0; n < notes.size(); ++n)
            row.text += (n == 0 ? "" : ", ") + notes[n];
        row.description = "Theme, covers, sort and whether it shows";
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
    form_.add_header("Tabs  ·  " + tab->name);
    const library::System *system = system_of(page_);
    if (page_ != "all")
    {
        const bool shown =
            system != nullptr ? prefs.system_enabled(system->name) : !prefs.tab_hidden(page_);
        form_.add_toggle(kPageShow, "Show this tab", shown).description =
            system != nullptr ? "Off hides the tab and its games everywhere"
                              : "Off takes it off the tab bar; nothing is deleted";
    }
    std::vector<std::string> themes = {"Main theme"};
    int theme = 0;
    const std::string chosen = prefs.tab_theme(page_);
    std::vector<std::string> names = theme_names();
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        themes.push_back(names[i]);
        if (themes_ != nullptr && themes_->at(i).id == chosen)
            theme = static_cast<int>(i) + 1;
    }
    form_.add_choice(kPageTheme, "Theme", std::move(themes), theme).description =
        "This tab can wear a theme of its own";
    form_
        .add_choice(kPageCover, "Cover shape", {kCoverNames, kCoverNames + 4},
                    static_cast<int>(prefs.tab_cover(page_)))
        .description = "Square for console icons; the box's own shape for box art";
    std::vector<std::string> sorts = {std::string("Library's (") +
                                      kSortNames[sort_index(prefs.sort)] + ")"};
    sorts.insert(sorts.end(), kSortNames, kSortNames + 4);
    const std::string sort = prefs.tab_sort(page_);
    form_
        .add_choice(kPageSort, "Sort by", std::move(sorts), sort.empty() ? 0 : sort_index(sort) + 1)
        .description = "The order of the covers on this tab";
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
        form_.add_choice(kPageCore, "Core", std::move(cores), core).description =
            "The RetroArch core these games start with";
    }
    if (page_.rfind("tab:", 0) == 0)
    {
        ui::FormRow &rename = form_.add_action(kPageRename, "Rename");
        rename.chevron = true;
        form_.add_action(kPageLeft, "Move left").description = "Earlier on the tab bar";
        form_.add_action(kPageRight, "Move right").description = "Later on the tab bar";
        ui::FormRow &remove = form_.add_action(kPageDelete, "Delete tab");
        remove.danger = true;
        remove.description = "Its games go back to their shelves";
    }
}

void SettingsPanel::build_emulators(const LibraryPrefs &prefs)
{
    form_.add_header("Emulators");
    for (std::size_t i = 0; emulator_ != nullptr && i < emulator_->systems.size(); ++i)
    {
        const library::System &system = emulator_->systems[i];
        const std::string games =
            std::to_string(system.games) + (system.games == 1 ? " game" : " games");
        form_
            .add_toggle(kSystemRow + static_cast<int>(i), system.short_name + "  (" + games + ")",
                        prefs.system_enabled(system.name))
            .description = system.name + ", played by RetroArch: on shows its tab";
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
            .add_choice(kCoreRow + static_cast<int>(i), system.short_name + " core",
                        std::move(names), chosen)
            .description = "The RetroArch core " + system.short_name + " games start with";
    }
}

void SettingsPanel::build_art(const LibraryPrefs &prefs)
{
    form_.add_header("Box art");
    form_.add_toggle(kArtAuto, "Find box art by itself", prefs.auto_art).description =
        "When ORION starts, new games get covers from the internet";
    form_.add_action(kArtNow, "Find box art now").description = art_status_;
    form_
        .add_choice(kArtFolder, "Box art folder", {kArtPlaces, kArtPlaces + 4},
                    art_place(prefs.art_dir))
        .description = "Looked in first, and where downloads go: " + art_folder(prefs, state_dir_);
}

std::vector<std::string> SettingsPanel::theme_names() const
{
    std::vector<std::string> names;
    for (std::size_t i = 0; themes_ != nullptr && i < themes_->size(); ++i)
    {
        const ThemeFile &file = themes_->at(i);
        // The player's own themes say so.
        names.push_back(file.user ? file.name + " (yours)" : file.name);
    }
    return names;
}

void SettingsPanel::build_appearance(const Look &look)
{
    form_.add_header("Appearance");
    std::vector<std::string> style_names = theme_names();
    int base = 0;
    for (std::size_t i = 0; themes_ != nullptr && i < themes_->size(); ++i)
    {
        if (look.base == themes_->at(i).id)
            base = static_cast<int>(i);
    }
    if (style_names.empty())
        style_names.emplace_back("ORION");
    form_.add_choice(kBase, "Theme", std::move(style_names), base).description =
        "Built in, or a theme file of yours; everything below adjusts it";
    std::vector<std::string> background_names;
    int background = 0;
    for (std::size_t i = 0; i < backgrounds().size(); ++i)
    {
        background_names.emplace_back(backgrounds()[i].name);
        if (backgrounds()[i].mode == look.background)
            background = static_cast<int>(i);
    }
    form_.add_choice(kBackground, "Background", std::move(background_names), background)
        .description = "What moves behind the glass";
    add_swatches(form_, kSky, "Sky", skies(), look.sky_top, look.sky_bottom, true,
                 "The colour of empty space");
    add_swatches(form_, kNebula, "Nebula", nebulas(), look.nebula, look.nebula_far, true,
                 "The glow in the sky, and the clouds that drift through it");
    form_.add_slider(kClouds, "Clouds", look.clouds * 100.0f, 0.0f, 200.0f, 10.0f).unit = " %";
    form_.add_toggle(kMotion, "Motion", look.motion).description =
        "The sky drifts slowly; off holds it still";
    add_swatches(form_, kGlass, "Glass tint", glasses(), look.glass, look.glass, false,
                 "The colour frosted panels are tinted with");
    form_.add_slider(kOpacity, "Glass opacity", look.glass.a * 100.0f, 0.0f, 100.0f, 5.0f).unit =
        " %";
    add_swatches(form_, kHighlight, "Highlight", highlights(), look.highlight, look.highlight,
                 false, "The selected tab and the focused key");
    add_swatches(form_, kAccent, "Accent", accents(), look.accent, look.accent, false,
                 "Labels, counts and the text cursor");
    form_.add_slider(kCorners, "Cover corners", look.corners, 0.0f, 40.0f, 2.0f).unit = " px";
    form_
        .add_choice(kTabBar, "Tab bar", {"Plain", "Pills"},
                    look.chrome.tabs == TabStyle::plain ? 0 : 1)
        .description = "Plain text like the home screen, or pills on a frosted track";
    form_
        .add_choice(kFocusRing, "Focus ring", {"Aurora", "Theme"},
                    look.chrome.focus == FocusRing::aurora ? 0 : 1)
        .description = "A ring that drifts through the colours, or the theme's own";
    form_
        .add_choice(kBars, "Bottom bars", {"None", "Outline", "Glass"},
                    look.chrome.plates == Plates::none      ? 0
                    : look.chrome.plates == Plates::outline ? 1
                                                            : 2)
        .description = "Behind the title details and button hints: nothing, a rim, or glass";
    form_.add_toggle(kBadges, "Category banners", look.chrome.badges).description =
        "GAME, HOMEBREW, MEDIA or PS4 on each cover";
    form_.add_toggle(kRim, "Cover rim", look.chrome.cover_rim).description =
        "A hairline of light around each cover";
    ui::FormRow &save = form_.add_action(kSaveTheme, "Save as new theme");
    save.description = "Keeps this look as a theme file in /data/orion/themes, to reuse or share";
    save.chevron = true;
    form_.add_action(kRestore, "Restore the ORION look").description =
        "Back to the galaxy glass ORION starts with";
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
        if (!page_.empty())
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
    case kSort:
        prefs->sort = kSortKeys[std::clamp(form_.choice_index(kSort), 0, 3)];
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
    case kArtFolder:
    {
        const int place = form_.choice_index(kArtFolder);
        if (place >= 3)
            return Change::type_art_folder;
        prefs->art_dir = kArtPaths[std::max(place, 0)];
        if (ui::FormRow *row = form_.row(kArtFolder))
            row->description =
                "Looked in first, and where downloads go: " + art_folder(*prefs, state_dir_);
        return Change::art_folder;
    }
    default:
        return edit_appearance(id, look, *prefs);
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
        prefs->set_tab_sort(page_, index <= 0 ? std::string() : kSortKeys[std::min(index - 1, 3)]);
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
        tab_id_ = custom;
        return custom.empty() ? Change::none : Change::rename_tab;
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
    sheet_.update(dt);
    nav_.update(dt);
    form_.update(dt);
}

void SettingsPanel::draw_content(ui::Canvas &canvas, float opacity) const
{
    (void)opacity;
    nav_.draw(canvas);
    form_.draw(canvas);
    // What the buttons do here, under the rows.
    const ui::Theme &theme = form_.style.theme;
    ui::GlyphStyle glyphs = ui::GlyphStyle::dark();
    glyphs.body = theme.text.with_alpha(0.14f);
    glyphs.edge = theme.text.with_alpha(0.0f);
    glyphs.ink = theme.text;
    glyphs.label = theme.text_muted;
    const ui::Hint hints[] = {
        {ui::Button::l1, "Sections", ui::Button::r1},
        {ui::Button::cross, "Select"},
        {ui::Button::circle, in_rows_ ? "Back" : "Close"},
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
