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
    kRestore,
    kNewTab,
    kTabRow = 100, // + the tab's index
};

constexpr const char *kSortKeys[] = {"name", "name_reversed", "recent", "platform"};
constexpr const char *kSortNames[] = {"Name A–Z", "Name Z–A", "Recently added", "Platform"};

// The base styles in the order Settings lists them: ORION first.
std::vector<const ui::Theme *> bases()
{
    std::vector<const ui::Theme *> out;
    for (const ui::Theme &theme : ui::themes())
    {
        if (std::strcmp(theme.id, "orion") == 0)
            out.insert(out.begin(), &theme);
        else
            out.push_back(&theme);
    }
    return out;
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
    form_.style.control_width = 270.0f;
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

    form_.add_header("Appearance");
    const std::vector<const ui::Theme *> styles = bases();
    std::vector<std::string> style_names;
    int base = 0;
    for (std::size_t i = 0; i < styles.size(); ++i)
    {
        style_names.emplace_back(styles[i]->name);
        if (look.base == styles[i]->id)
            base = static_cast<int>(i);
    }
    form_.add_choice(kBase, "Style", std::move(style_names), base).description =
        "A starting point; everything below can then be changed";
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
        if (id == kRestore)
        {
            *look = default_look();
            refresh(*look, *prefs);
            return Change::look;
        }
        if (id == kNewTab)
            return Change::new_tab;
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
    case kBase:
    {
        const std::vector<const ui::Theme *> styles = bases();
        const int index =
            std::clamp(form_.choice_index(kBase), 0, static_cast<int>(styles.size()) - 1);
        *look = look_of(*styles[static_cast<std::size_t>(index)]);
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
