// ps5-homebrew-ui - ORION's Settings: library, appearance and tabs in a glass drawer.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/settings_panel.hpp"

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
    kTabTheme,
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
    kTabRow = 100,    // + the tab's index
    kSystemRow = 200, // + the system's index: on or off
    kCoreRow = 300,   // + the system's index: which core
};

constexpr const char *kSortKeys[] = {"name", "name_reversed", "recent", "platform"};
constexpr const char *kSortNames[] = {"Name A–Z", "Name Z–A", "Recently added", "Platform"};

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
    sheet_.style.size = 700.0f;
    sheet_.style.margin = 28.0f; // floats, fully rounded: a pane of glass
    // Rows of text want a calmer glass than the keyboard: more frost over
    // the covers behind, which still show through as colour.
    sheet_.style.frost = 0.65f;
    sheet_.style.scrim = 0.35f;
    sheet_.style.title_size = 36.0f;
    sheet_.set_title("Settings");
    sheet_.content = [this](ui::Canvas &canvas, const gfx::Rect &, float) { form_.draw(canvas); };

    form_.style.on_page = false;
    form_.style.row_height = 64.0f;
    form_.style.header_height = 54.0f;
    form_.style.label_size = 23.0f;
    form_.style.value_size = 22.0f;
    form_.style.description_size = 19.0f;
    form_.style.control_width = 340.0f; // room for theme names
}

void SettingsPanel::set_theme(const ui::Theme &theme, bool reduced_motion)
{
    // A drawer full of text wants denser glass than the rest of the screen:
    // the player's tint, but at least this opaque, so bright covers behind it
    // become colour rather than shapes under the rows.
    ui::Theme denser = theme;
    if (denser.style == ui::SurfaceStyle::glass)
        denser.surface = gfx::mix(denser.page, denser.surface, 0.35f)
                             .with_alpha(std::max(0.78f, denser.surface.a));
    sheet_.style.theme = form_.style.theme = denser;
    sheet_.style.reduced_motion = form_.style.reduced_motion = reduced_motion;
}

void SettingsPanel::open(const Look &look, const LibraryPrefs &prefs, ui::Feedback &feedback)
{
    build(look, prefs);
    sheet_.open(feedback);
    form_.set_bounds(sheet_.content_rect());
    form_.set_focus(0);
    form_.enter();
}

void SettingsPanel::close(ui::Feedback &feedback)
{
    sheet_.close(feedback);
}

void SettingsPanel::refresh(const Look &look, const LibraryPrefs &prefs)
{
    const int focus = form_.focus();
    build(look, prefs);
    form_.set_bounds(sheet_.content_rect());
    form_.set_focus(std::min(focus, form_.row_count() - 1));
}

void SettingsPanel::build(const Look &look, const LibraryPrefs &prefs)
{
    form_.clear();
    tab_ids_.clear();

    form_.add_header("Library");
    int sort = 0;
    for (int i = 0; i < 4; ++i)
    {
        if (prefs.sort == kSortKeys[i])
            sort = i;
    }
    form_.add_choice(kSort, "Sort by", {kSortNames, kSortNames + 4}, sort).description =
        "The order of the covers on every tab";
    form_.add_toggle(kReopen, "Reopen on last tab", prefs.reopen_last_tab).description =
        "ORION starts on the tab you left it on";
    form_.add_toggle(kLeaveShelf, "Moved titles leave shelf", prefs.moved_leave_shelf).description =
        "A title moved to your own tab leaves Games, Homebrew or Media";

    if (emulator_ != nullptr && !emulator_->systems.empty())
    {
        form_.add_header("Emulators");
        for (std::size_t i = 0; i < emulator_->systems.size(); ++i)
        {
            const library::System &system = emulator_->systems[i];
            const std::string games =
                std::to_string(system.games) + (system.games == 1 ? " game" : " games");
            form_
                .add_toggle(kSystemRow + static_cast<int>(i),
                            system.short_name + "  (" + games + ")",
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
        form_.add_toggle(kArtAuto, "Find box art by itself", prefs.auto_art).description =
            "When ORION starts, new games get covers from the internet";
        form_.add_action(kArtNow, "Find box art now").description = art_status_;
    }

    form_.add_header("Appearance");
    std::vector<std::string> style_names;
    int base = 0;
    for (std::size_t i = 0; themes_ != nullptr && i < themes_->size(); ++i)
    {
        const ThemeFile &file = themes_->at(i);
        // The player's own themes say so.
        style_names.push_back(file.user ? file.name + " (yours)" : file.name);
        if (look.base == file.id)
            base = static_cast<int>(i);
    }
    if (style_names.empty())
        style_names.emplace_back("ORION");
    form_.add_choice(kBase, "Theme", std::move(style_names), base).description =
        "Built in, or a theme file of yours; everything below adjusts it";
    // This tab may wear a theme of its own instead.
    std::vector<std::string> tab_names = {"Main theme"};
    int tab_choice = 0;
    const std::string tab_theme = prefs.tab_theme(tab_key_);
    for (std::size_t i = 0; themes_ != nullptr && i < themes_->size(); ++i)
    {
        const ThemeFile &file = themes_->at(i);
        tab_names.push_back(file.user ? file.name + " (yours)" : file.name);
        if (file.id == tab_theme)
            tab_choice = static_cast<int>(i) + 1;
    }
    form_.add_choice(kTabTheme, "Theme for " + tab_name_, std::move(tab_names), tab_choice)
        .description = "This tab can wear a theme of its own; the rows below edit the main one";
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

    form_.add_header("Tabs");
    form_.add_action(kNewTab, "New tab").chevron = true;
    for (std::size_t i = 0; i < prefs.tabs.size(); ++i)
    {
        ui::FormRow &row = form_.add_action(kTabRow + static_cast<int>(i), prefs.tabs[i].name);
        row.chevron = true;
        row.description = "Rename, move or delete this tab";
        tab_ids_.push_back(prefs.tabs[i].id);
    }
}

void SettingsPanel::set_art_status(std::string status)
{
    if (status == art_status_)
        return;
    art_status_ = std::move(status);
    if (ui::FormRow *row = form_.row(kArtNow))
        row->description = art_status_;
}

SettingsPanel::Change SettingsPanel::handle(const InputFrame &input, ui::Feedback &feedback,
                                            Look *look, LibraryPrefs *prefs)
{
    // The sheet only wants back; everything else is the form's.
    if (sheet_.handle(input, feedback) != ui::Event::none || !sheet_.is_open())
        return Change::none;
    const ui::Event event = form_.handle(input, feedback);
    if (event == ui::Event::activated)
    {
        const int id = form_.changed_id();
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
        if (id >= kTabRow && id - kTabRow < static_cast<int>(tab_ids_.size()))
        {
            tab_id_ = tab_ids_[static_cast<std::size_t>(id - kTabRow)];
            return Change::edit_tab;
        }
        return Change::none;
    }
    if (event != ui::Event::changed)
        return Change::none;

    const int id = form_.changed_id();
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
    if (id == kTabTheme)
    {
        const int index = form_.choice_index(kTabTheme);
        const bool own =
            themes_ != nullptr && index > 0 && index <= static_cast<int>(themes_->size());
        prefs->set_tab_theme(tab_key_,
                             own ? themes_->at(static_cast<std::size_t>(index - 1)).id : "");
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
    case kBase:
    {
        if (themes_ == nullptr || themes_->size() == 0)
            return Change::none;
        const int index =
            std::clamp(form_.choice_index(kBase), 0, static_cast<int>(themes_->size()) - 1);
        *look = look_of(themes_->at(static_cast<std::size_t>(index)));
        refresh(*look, *prefs); // every appearance row starts over
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

gfx::Rect SettingsPanel::focused_row() const
{
    return form_.row_rect(form_.focus());
}

void SettingsPanel::update(float dt)
{
    sheet_.update(dt);
    form_.update(dt);
}

void SettingsPanel::draw(ui::Canvas &canvas) const
{
    sheet_.draw(canvas);
}

} // namespace hui::orion
