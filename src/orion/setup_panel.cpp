// ORION Media Center - ORION's first-run setup: a few steps that set everything up.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/setup_panel.hpp"

#include "core/i18n.hpp"
#include "library/emulators.hpp"
#include "ui/glyphs.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <span>

namespace hui::orion
{

using i18n::tr;
using i18n::trf;

namespace
{

enum : int
{
    kNext = 1, // the row that goes on, last on every step
    kSkip,
    kLanguage,
    kInfo,
    kInfoEmulators,
    kBase,
    kGameBackgrounds,
    kScan,
    kGroup,
    kArtAuto,
    kArtFolder,
    kReturn,
    kReopen,
    kSort,
    kAutostart,
    kPayload,
    kPayloadFolder,
    kTip,
    kNewTab,
    kNewLabel,
    kNoGames,
    kBackground,
    kClearBackground,
    kPictureEffect,
    kEmulatorRow = 100, // + the emulator's index, or a missing one's past them
    kPresetRow = 500,   // + the ready-made label's index in kPresets
    kOwnLabelRow = 600, // + the label's index in prefs.labels
    kTabRow = 1000,     // + the tab's index in tabs_
    kGameRow = 10000,   // + the game's index in games_
};

// Ready-made labels the Labels step offers: on makes one, off takes it away.
constexpr const char *kPresets[] = {i18n::marked("CO-OP"),    i18n::marked("RPG"),
                                    i18n::marked("RACING"),   i18n::marked("SHOOTER"),
                                    i18n::marked("FIGHTING"), i18n::marked("SPORTS"),
                                    i18n::marked("HORROR"),   i18n::marked("KIDS"),
                                    i18n::marked("PARTY"),    i18n::marked("VR")};
constexpr int kPresetCount = static_cast<int>(std::size(kPresets));
// Dark text: the palette's first colours are light ones.
constexpr std::uint32_t kLabelInk = 0x08101c;

// The player's label with this text, or nullptr.
const Label *label_named(const LibraryPrefs &prefs, const std::string &text)
{
    for (const Label &label : prefs.labels)
    {
        if (label.custom && label.text == text)
            return &label;
    }
    return nullptr;
}

bool is_preset(const std::string &text)
{
    for (const char *preset : kPresets)
    {
        if (text == tr(preset))
            return true;
    }
    return false;
}

// The built-in shelves a title can sit on, in the order Your games offers
// them; the player's own tabs follow.
constexpr const char *kShelfKeys[] = {"games", "homebrew", "media"};
constexpr const char *kShelfNames[] = {i18n::marked("Games"), i18n::marked("Homebrew"),
                                       i18n::marked("Media")};
constexpr int kShelfCount = 3;

constexpr const char *kStepNames[] = {i18n::marked("Welcome"),   i18n::marked("Look"),
                                      i18n::marked("Emulators"), i18n::marked("Labels"),
                                      i18n::marked("Tabs"),      i18n::marked("Your games"),
                                      i18n::marked("Box art"),   i18n::marked("Library"),
                                      i18n::marked("Autoboot"),  i18n::marked("Done")};
constexpr int kStepCount = static_cast<int>(std::size(kStepNames));

int sort_index(const std::string &key)
{
    for (int i = 0; i < kSortCount; ++i)
    {
        if (key == kSortKeys[i])
            return i;
    }
    return 0;
}

} // namespace

SetupPanel::SetupPanel()
{
    sheet_.style.edge = ui::SheetEdge::right;
    sheet_.style.size = gfx::kVirtualWidth;
    sheet_.style.margin = 28.0f;
    sheet_.style.frost = 0.65f;
    sheet_.style.scrim = 0.35f;
    sheet_.style.title_size = 40.0f;
    sheet_.style.handle = false;
    sheet_.set_title(tr("Set up ORION"));
    sheet_.content = [this](ui::Canvas &canvas, const gfx::Rect &, float opacity)
    { draw_content(canvas, opacity); };

    steps_.style.kind = ui::WizardKind::horizontal;
    steps_.style.marker_size = 38.0f;
    steps_.style.label_size = 19.0f;
    steps_.style.number_size = 17.0f;
    steps_.style.on_page = false;
    std::vector<ui::WizardStep> steps;
    for (const char *name : kStepNames)
        steps.push_back({name});
    steps_.set_steps(std::move(steps));

    form_.style.on_page = false;
    form_.style.row_height = 72.0f;
    form_.style.header_height = 58.0f;
    form_.style.label_size = 25.0f;
    form_.style.value_size = 24.0f;
    form_.style.description_size = 20.0f;
    form_.style.control_width = 420.0f;
}

void SetupPanel::set_theme(const ui::Theme &theme, bool reduced_motion)
{
    // Denser glass than the library, as Settings wears it.
    ui::Theme denser = theme;
    if (denser.style == ui::SurfaceStyle::glass)
        denser.surface = gfx::mix(denser.page, denser.surface, 0.35f)
                             .with_alpha(std::max(0.82f, denser.surface.a));
    sheet_.style.theme = form_.style.theme = steps_.style.theme = denser;
    sheet_.style.reduced_motion = form_.style.reduced_motion = steps_.style.reduced_motion =
        reduced_motion;
}

void SetupPanel::open(const Look &look, const LibraryPrefs &prefs, ui::Feedback &feedback)
{
    sheet_.open(feedback);
    go(Step::welcome, look, prefs);
    steps_.set_step(0, true);
}

void SetupPanel::close(ui::Feedback &feedback)
{
    sheet_.close(feedback);
}

void SetupPanel::refresh(const Look &look, const LibraryPrefs &prefs)
{
    const int focus = form_.focus();
    build(look, prefs);
    layout();
    form_.set_focus(std::clamp(focus, 0, std::max(form_.row_count() - 1, 0)));
}

void SetupPanel::go(Step step, const Look &look, const LibraryPrefs &prefs)
{
    step_ = step;
    steps_.set_step(static_cast<int>(step));
    build(look, prefs);
    layout();
    form_.set_active(true);
    form_.set_focus(0, true);
    // Welcome starts on Get started: Cross is all it takes to go on.
    if (step == Step::welcome)
        form_.focus_row(kNext, true);
    form_.enter();
}

std::string SetupPanel::tab_name(const std::string &key, const char *own) const
{
    for (const TabEntry &tab : tabs_)
    {
        if (tab.key == key)
            return tab.name;
    }
    return tr(own);
}

const library::System *SetupPanel::system_of(const std::string &key) const
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

void SetupPanel::layout()
{
    const gfx::Rect area = sheet_.content_rect();
    constexpr float kHints = 70.0f;
    const float width = std::min(area.w, 1180.0f);
    const float left = area.x + (area.w - width) * 0.5f;
    // Ten steps: the bar is wider than the rows, so each name has room.
    const float bar = std::min(area.w, 1640.0f);
    steps_.set_bounds({area.x + (area.w - bar) * 0.5f, area.y + 4.0f, bar, 96.0f});
    form_.set_bounds({left, area.y + 128.0f, width, area.h - 128.0f - kHints});
}

void SetupPanel::build(const Look &look, const LibraryPrefs &prefs)
{
    // In the language chosen now: the title and the steps' names too.
    sheet_.set_title(tr("Set up ORION"));
    on_word_ = tr("On");
    off_word_ = tr("Off");
    form_.style.on_text = on_word_.c_str();
    form_.style.off_text = off_word_.c_str();
    std::vector<ui::WizardStep> steps;
    for (const char *name : kStepNames)
        steps.push_back({tr(name)});
    steps_.set_steps(std::move(steps));
    steps_.set_step(static_cast<int>(step_), true);
    form_.clear();
    pills_.clear();
    const auto next = [&](const std::string &label, const std::string &description)
    {
        ui::FormRow &row = form_.add_action(kNext, label);
        row.chevron = true;
        row.description = description;
    };
    switch (step_)
    {
    case Step::welcome:
    {
        form_.add_header(tr("Welcome to ORION Media Center"));
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
        next(tr("Get started"), tr("A few choices, all of them in Settings later"));
        int games = 0;
        std::string emulators;
        for (std::size_t i = 0; emulator_ != nullptr && i < emulator_->apps.size(); ++i)
        {
            games += emulator_->apps[i].games;
            emulators += (i == 0 ? "" : ", ") + emulator_->apps[i].name;
        }
        form_.add_value(kInfo, tr("On this console"),
                        i18n::plural(titles_, "{0} title", "{0} titles"));
        form_.add_value(kInfoEmulators, tr("Emulators"),
                        emulators.empty()
                            ? tr("None yet")
                            : emulators + "  ·  " + i18n::plural(games, "{0} game", "{0} games"));
        form_.add_action(kSkip, tr("Skip setup")).description =
            tr("Use ORION as it is; Settings (R3) has everything");
        break;
    }
    case Step::look:
    {
        form_.add_header(tr("Look"));
        std::vector<std::string> names;
        int base = 0;
        for (std::size_t i = 0; themes_ != nullptr && i < themes_->size(); ++i)
        {
            names.push_back(themes_->at(i).name);
            if (themes_->at(i).id == look.base)
                base = static_cast<int>(i);
        }
        if (names.empty())
            names.emplace_back("ORION"); // a name
        form_.add_choice(kBase, tr("Theme"), std::move(names), base).description =
            tr("ORION's galaxy glass, or another; the screen changes as you choose");
        form_.add_toggle(kGameBackgrounds, tr("Game backgrounds"), prefs.game_backgrounds)
            .description =
            tr("The focused game's picture behind the covers, as the home screen shows it");
        ui::FormRow &picture = form_.add_action(kBackground, tr("Background picture"));
        picture.chevron = true;
        picture.text = own_background_ ? tr("Your picture") : tr("None");
        picture.description =
            tr("A picture of yours behind the library: a USB drive, or your screenshots");
        if (own_background_)
        {
            form_
                .add_choice(
                    kPictureEffect, tr("Effect over your picture"),
                    i18n::tr_range(kPictureEffectNames, kPictureEffectNames + kPictureEffectCount),
                    picture_effect_index(prefs.picture_effect))
                .description = tr("Snow, rain or fireflies drifting over your background picture");
            form_.add_action(kClearBackground, tr("Use the sky")).description =
                tr("The galaxy behind the library again");
        }
        next(tr("Continue"), tr("Colours, glass and more are in Settings, Appearance"));
        break;
    }
    case Step::emulators:
    {
        form_.add_header(tr("Emulators"));
        int row = 0;
        for (std::size_t i = 0; emulator_ != nullptr && i < emulator_->apps.size(); ++i)
        {
            const library::EmulatorApp &app = emulator_->apps[i];
            form_
                .add_value(kEmulatorRow + row++, app.name,
                           i18n::plural(app.games, "{0} game", "{0} games"))
                .description = app.starts_games ? tr("ORION starts its games")
                                                : tr("Its games open it; you pick the game there");
        }
        for (const library::KnownEmulator &known : library::known_emulators())
        {
            if (emulator_ != nullptr && emulator_->app(known.key) != nullptr)
                continue;
            form_.add_value(kEmulatorRow + row++, known.name, tr("Not installed")).description =
                trf("{0}: install it, then scan", {tr(known.plays)});
        }
        form_.add_action(kScan, tr("Scan for emulators")).description =
            scan_status_.empty()
                ? trf("Finds {0}, and their games",
                      {"RetroArch, Porpoise, PS5SX2, PSwanStation, ProsperoEden, PS5-ECAM"})
                : scan_status_;
        form_.add_toggle(kGroup, tr("Group emulator tabs"), prefs.group_systems).description =
            tr("One Emulators tab with a row of systems under it (L2 and R2), or a tab each");
        next(tr("Continue"), tr("Game folders of your own: Settings, Emulators"));
        break;
    }
    case Step::labels:
    {
        // A label is the banner across a cover (GAME, APP...): one of the
        // player's own says what they want it to, on the games they give it.
        form_.add_header(tr("Labels"));
        const std::span<const Swatch> colours = label_colours();
        const std::size_t light = std::min<std::size_t>(10, colours.size());
        for (int i = 0; i < kPresetCount; ++i)
        {
            const std::string text = tr(kPresets[i]);
            const Label *own = label_named(prefs, text);
            ui::FormRow &row = form_.add_toggle(kPresetRow + i, text, own != nullptr);
            row.description = tr("On: give it to games in Your games with Square");
            pills_.push_back({kPresetRow + i, text,
                              own != nullptr ? own->fill
                              : light > 0    ? colours[static_cast<std::size_t>(i) % light].first
                                             : 0xffffffu,
                              own != nullptr ? own->ink : kLabelInk});
        }
        for (std::size_t i = 0; i < prefs.labels.size(); ++i)
        {
            const Label &label = prefs.labels[i];
            if (!label.custom || is_preset(label.text))
                continue;
            ui::FormRow &row =
                form_.add_toggle(kOwnLabelRow + static_cast<int>(i), label.text, true);
            row.description = tr("Yours; off takes it off every game");
            pills_.push_back({row.id, label.text, label.fill, label.ink});
        }
        form_.add_action(kNewLabel, tr("New label…")).description =
            tr("Your own words on a cover; colours later in Settings, Labels");
        next(tr("Continue"), tr("Next: which tabs show, then where each game goes"));
        break;
    }
    case Step::tabs:
    {
        form_.add_header(tr("Tabs"));
        form_.add_action(kNewTab, tr("New tab…")).description =
            tr("A shelf of your own, named as you like; Your games fills it");
        const auto add_tab = [&](std::size_t i, const std::string &description)
        {
            const TabEntry &tab = tabs_[i];
            const library::System *system = system_of(tab.key);
            const bool shown =
                system != nullptr ? prefs.system_enabled(system->name) : !prefs.tab_hidden(tab.key);
            form_.add_toggle(kTabRow + static_cast<int>(i), tab.name, shown).description =
                description;
        };
        // The Hidden tab is for titles put out of the way: Settings shows it.
        bool emulated = false;
        for (std::size_t i = 0; i < tabs_.size(); ++i)
        {
            const std::string &key = tabs_[i].key;
            if (key == kHiddenTabKey)
                continue;
            if (key == "emulators" || system_of(key) != nullptr)
            {
                emulated = true;
                continue;
            }
            add_tab(i, key == "all"                ? tr("Every title in one place")
                       : key == "favourites"       ? tr("The titles you star")
                       : key.rfind("tab:", 0) == 0 ? tr("One of your own")
                                                   : std::string());
        }
        // The systems under their own heading: under the Emulators tab
        // when they are grouped, a tab each when not.
        if (emulated)
        {
            form_.add_header(tr("Emulators"));
            for (std::size_t i = 0; i < tabs_.size(); ++i)
            {
                if (tabs_[i].key == "emulators")
                    add_tab(i, tr("One tab with every system under it (L2 and R2)"));
            }
            for (std::size_t i = 0; i < tabs_.size(); ++i)
            {
                if (system_of(tabs_[i].key) != nullptr)
                    add_tab(i, prefs.group_systems
                                   ? tr("A system under Emulators; off leaves its games out")
                                   : tr("Off leaves its games out too"));
            }
        }
        next(tr("Continue"), tr("Rename, reorder and theme them: Settings, Tabs"));
        break;
    }
    case Step::games:
    {
        form_.add_header(tr("Your games"));
        std::vector<std::string> places;
        for (int i = 0; i < kShelfCount; ++i)
            places.push_back(tab_name(kShelfKeys[i], kShelfNames[i]));
        for (const CustomTab &tab : prefs.tabs)
            places.push_back(tab.name);
        labels_to_give_ = std::any_of(prefs.labels.begin(), prefs.labels.end(),
                                      [](const Label &label) { return label.custom; });
        const bool labels = labels_to_give_;
        for (std::size_t i = 0; i < games_.size(); ++i)
        {
            const Game &game = games_[i];
            int at = 0;
            std::string shelf = prefs.placed_shelf(game.id);
            if (shelf.empty())
                shelf = game.shelf;
            for (int s = 0; s < kShelfCount; ++s)
            {
                if (shelf == kShelfKeys[s])
                    at = s;
            }
            if (const CustomTab *placed = prefs.placement(game.id))
            {
                for (std::size_t t = 0; t < prefs.tabs.size(); ++t)
                {
                    if (prefs.tabs[t].id == placed->id)
                        at = kShelfCount + static_cast<int>(t);
                }
            }
            const int id = kGameRow + static_cast<int>(i);
            form_.add_choice(id, game.name, places, at).description =
                labels ? tr("Left and right: its tab · Square: its label")
                       : tr("Left and right: its tab");
            if (const Label *worn = prefs.label(prefs.title_label(game.id)))
                pills_.push_back({id, worn->text, worn->fill, worn->ink});
        }
        if (games_.empty())
            form_.add_value(kNoGames, tr("No console titles yet"), "");
        next(tr("Continue"), tr("Later: Options on a title, Move to tab…"));
        break;
    }
    case Step::art:
    {
        form_.add_header(tr("Box art"));
        form_.add_toggle(kArtAuto, tr("Find box art by itself"), prefs.auto_art).description =
            tr("Games without a cover get one from the internet while ORION runs");
        const int place = art_place(prefs.art_dir);
        std::vector<std::string> places =
            i18n::tr_range(kArtPlaceNames, kArtPlaceNames + kArtPlaceCount);
        if (place >= kArtPlaceCount)
            places.push_back(tr("Your own folder"));
        form_.add_choice(kArtFolder, tr("Box art folder"), std::move(places), place).description =
            trf("Where covers go: {0}", {art_folder(prefs, state_dir_)});
        next(tr("Continue"), tr("Find box art now and more: Settings, Box art"));
        break;
    }
    case Step::library:
    {
        form_.add_header(tr("Library"));
        form_.add_toggle(kReturn, tr("Return to ORION after a game"), prefs.return_on_close)
            .description = tr("Close a game from the PS button menu and ORION opens again");
        form_.add_toggle(kReopen, tr("Reopen on last tab"), prefs.reopen_last_tab).description =
            tr("ORION starts on the tab you left it on");
        form_
            .add_choice(kSort, tr("Sort by"), i18n::tr_range(kSortNames, kSortNames + kSortCount),
                        sort_index(prefs.sort))
            .description = tr("Every tab can have its own: Up from the first row of covers");
        next(tr("Continue"), tr("Tabs of your own, themes per tab: Settings, Tabs"));
        break;
    }
    case Step::autostart:
    {
        form_.add_header(tr("Autoboot"));
        // A row that only shows a value never shows its description: what to
        // do goes on the toggle, the file's place on a row of its own.
        form_.add_toggle(kAutostart, tr("Autoboot ORION after the jailbreak"), prefs.autostart)
            .description = tr("ORION opens on its own after each jailbreak, once the payload below "
                              "is in your autoloader");
        const std::size_t slash = payload_.rfind('/');
        form_.add_value(kPayload, tr("Autoboot payload"), payload_.substr(slash + 1));
        form_.add_value(kPayloadFolder, tr("In the folder"), payload_.substr(0, slash));
        next(tr("Continue"),
             tr("Turn it off here or in Settings, System, without touching the autoloader"));
        break;
    }
    case Step::done:
    {
        form_.add_header(tr("You're set"));
        next(tr("Start using ORION"), tr("Settings changes any of this later"));
        form_.add_value(kTip, tr("Settings"), tr("R3")).description =
            tr("Or Options on an empty tab");
        form_.add_value(kTip + 1, tr("Search"), tr("Triangle")).description =
            tr("Type part of a name");
        form_.add_value(kTip + 2, tr("Sort a tab"), tr("Up")).description =
            tr("From the first row of covers, then Cross");
        form_.add_value(kTip + 3, tr("Tabs and systems"), tr("L1 R1, L2 R2")).description =
            tr("The shelves, and the systems under Emulators");
        break;
    }
    }
}

SetupPanel::Change SetupPanel::handle(const InputFrame &input, ui::Feedback &feedback, Look *look,
                                      LibraryPrefs *prefs)
{
    if (!sheet_.is_open())
        return Change::none;
    if (input.is_pressed(Action::page_next))
        return step_by(1, feedback, look, prefs);
    const bool left_out = input.nav == Direction::left && !form_.uses_horizontal();
    if (input.is_pressed(Action::back) || input.is_pressed(Action::page_prev) || left_out)
        return step_by(-1, feedback, look, prefs);
    if (input.is_pressed(Action::west) && step_ == Step::games)
        return next_label(feedback, look, prefs);

    const ui::Event event = form_.handle(input, feedback);
    const int id = form_.changed_id();
    if (event == ui::Event::activated)
    {
        if (id == kSkip || (id == kNext && step_ == Step::done))
        {
            prefs->setup_done = true;
            close(feedback);
            return Change::finished;
        }
        if (id == kNext)
            return step_by(1, feedback, look, prefs);
        if (id == kScan)
            return Change::scan_emulators;
        if (id == kNewTab)
            return Change::new_tab;
        if (id == kNewLabel)
            return Change::new_label;
        if (id == kBackground)
            return Change::choose_background;
        if (id == kClearBackground)
            return Change::clear_background;
        return Change::none;
    }
    if (event != ui::Event::changed)
        return Change::none;
    if (id >= kTabRow && id - kTabRow < static_cast<int>(tabs_.size()))
    {
        const std::string &key = tabs_[static_cast<std::size_t>(id - kTabRow)].key;
        if (const library::System *system = system_of(key))
            prefs->set_system_enabled(system->name, form_.toggle_value(id));
        else
            prefs->set_tab_hidden(key, !form_.toggle_value(id));
        return Change::prefs;
    }
    if (id >= kGameRow && id - kGameRow < static_cast<int>(games_.size()))
    {
        const Game &game = games_[static_cast<std::size_t>(id - kGameRow)];
        const int at = form_.choice_index(id);
        if (at >= kShelfCount && at - kShelfCount < static_cast<int>(prefs->tabs.size()))
            prefs->place(game.id, prefs->tabs[static_cast<std::size_t>(at - kShelfCount)].id);
        else if (at >= 0 && at < kShelfCount && kShelfKeys[at] == game.shelf)
            prefs->unplace(game.id); // its own shelf: no move kept
        else if (at >= 0 && at < kShelfCount)
            prefs->place(game.id, kShelfKeys[at]);
        return Change::prefs;
    }
    if (id >= kPresetRow && id - kPresetRow < kPresetCount)
    {
        const int index = id - kPresetRow;
        const std::string text = tr(kPresets[index]);
        if (form_.toggle_value(id))
        {
            const std::span<const Swatch> colours = label_colours();
            const std::size_t light = std::min<std::size_t>(10, colours.size());
            if (label_named(*prefs, text) == nullptr)
                prefs->add_label(text,
                                 light > 0 ? colours[static_cast<std::size_t>(index) % light].first
                                           : 0xffffffu,
                                 kLabelInk);
        }
        else if (const Label *own = label_named(*prefs, text))
            prefs->remove_label(own->id);
        refresh(*look, *prefs);
        return Change::prefs;
    }
    if (id >= kOwnLabelRow && id - kOwnLabelRow < static_cast<int>(prefs->labels.size()))
    {
        // Off is the end of it: a label of the player's own is not kept off.
        if (!form_.toggle_value(id))
            prefs->remove_label(prefs->labels[static_cast<std::size_t>(id - kOwnLabelRow)].id);
        refresh(*look, *prefs);
        return Change::prefs;
    }
    switch (id)
    {
    case kBase:
        if (themes_ != nullptr && themes_->size() > 0)
        {
            const int index =
                std::clamp(form_.choice_index(kBase), 0, static_cast<int>(themes_->size()) - 1);
            *look = look_of(themes_->at(static_cast<std::size_t>(index)));
            return Change::look;
        }
        return Change::none;
    case kLanguage:
    {
        const int index = form_.choice_index(id);
        if (index < 0 || index >= static_cast<int>(languages_.size()))
            return Change::none;
        prefs->language = languages_[static_cast<std::size_t>(index)].code;
        return Change::language;
    }
    case kGameBackgrounds:
        prefs->game_backgrounds = form_.toggle_value(id);
        return Change::prefs;
    case kPictureEffect:
        prefs->picture_effect =
            kPictureEffects[std::clamp(form_.choice_index(id), 0, kPictureEffectCount - 1)];
        return Change::prefs;
    case kGroup:
        prefs->group_systems = form_.toggle_value(id);
        return Change::prefs;
    case kArtAuto:
        prefs->auto_art = form_.toggle_value(id);
        return Change::prefs;
    case kArtFolder:
    {
        const int place = form_.choice_index(id);
        if (place < 0 || place >= kArtPlaceCount)
            return Change::none;
        prefs->art_dir = kArtPlacePaths[place];
        refresh(*look, *prefs); // the row says where, now
        return Change::art_folder;
    }
    case kReturn:
        prefs->return_on_close = form_.toggle_value(id);
        return Change::prefs;
    case kReopen:
        prefs->reopen_last_tab = form_.toggle_value(id);
        return Change::prefs;
    case kSort:
        prefs->sort = kSortKeys[std::clamp(form_.choice_index(id), 0, kSortCount - 1)];
        return Change::prefs;
    case kAutostart:
        prefs->autostart = form_.toggle_value(id);
        return Change::prefs;
    default:
        return Change::none;
    }
}

// R1 and L1 (and Next, Circle) move a step; on to the end finishes.
SetupPanel::Change SetupPanel::step_by(int direction, ui::Feedback &feedback, Look *look,
                                       LibraryPrefs *prefs)
{
    const int to = static_cast<int>(step_) + direction;
    if (to < 0)
    {
        feedback.play(audio::Cue::error);
        return Change::none;
    }
    if (to >= kStepCount)
    {
        prefs->setup_done = true;
        close(feedback);
        return Change::finished;
    }
    feedback.play(direction > 0 ? audio::Cue::select : audio::Cue::back);
    go(static_cast<Step>(to), *look, *prefs);
    return Change::none;
}

// Square on a game in Your games: the next of the player's labels, then
// none (its own) again.
SetupPanel::Change SetupPanel::next_label(ui::Feedback &feedback, Look *look, LibraryPrefs *prefs)
{
    const int index = form_.focus_id() - kGameRow;
    std::vector<std::string> ids;
    for (const Label &label : prefs->labels)
    {
        if (label.custom)
            ids.push_back(label.id);
    }
    if (index < 0 || index >= static_cast<int>(games_.size()) || ids.empty())
    {
        feedback.play(audio::Cue::error);
        return Change::none;
    }
    const std::string &game = games_[static_cast<std::size_t>(index)].id;
    const auto worn = std::find(ids.begin(), ids.end(), prefs->title_label(game));
    const std::string next = worn == ids.end()       ? ids.front()
                             : worn + 1 == ids.end() ? std::string()
                                                     : *(worn + 1);
    prefs->set_title_label(game, next);
    feedback.play(audio::Cue::toggle);
    refresh(*look, *prefs);
    return Change::prefs;
}

void SetupPanel::update(float dt)
{
    sheet_.update(dt);
    steps_.update(dt);
    form_.update(dt);
}

void SetupPanel::draw_content(ui::Canvas &canvas, float opacity) const
{
    (void)opacity;
    steps_.draw(canvas);
    form_.draw(canvas);
    draw_pills(canvas);
    const ui::Theme &theme = form_.style.theme;
    ui::GlyphStyle glyphs = ui::GlyphStyle::dark();
    glyphs.body = theme.text.with_alpha(0.14f);
    glyphs.edge = theme.text.with_alpha(0.0f);
    glyphs.ink = theme.text;
    glyphs.label = theme.text_muted;
    // Hints hold pointers: their words live until the hints are drawn.
    const std::string label = tr("Label");
    const std::string select = tr("Select");
    const std::string next = step_ == Step::done ? tr("Finish") : tr("Next step");
    const std::string previous = tr("Previous step");
    std::vector<ui::Hint> hints;
    // Square says what it does only once there is a label to give.
    if (step_ == Step::games && labels_to_give_)
        hints.push_back({ui::Button::square, label.c_str()});
    hints.push_back({ui::Button::cross, select.c_str()});
    hints.push_back({ui::Button::r1, next.c_str()});
    // On the first step there is no step before it.
    if (step_ != Step::welcome)
        hints.push_back({ui::Button::l1, previous.c_str(), ui::Button::circle});
    ui::HintLayout layout;
    layout.size = 38.0f;
    layout.text_size = 22.0f;
    const gfx::Rect area = sheet_.content_rect();
    layout.cy = area.y + area.h - 26.0f;
    ui::draw_hints(canvas.list, canvas.fonts, glyphs, hints.data(), static_cast<int>(hints.size()),
                   area.x + area.w, true, layout);
}

// Each label as the covers wear it: a pill after its row's name, faded with
// the row as it scrolls out.
void SetupPanel::draw_pills(ui::Canvas &canvas) const
{
    const gfx::Rect &bounds = form_.bounds();
    ui::Painter paint(canvas.list, canvas.fonts, form_.style.theme, canvas.glass);
    const ui::FontRef &font = canvas.fonts.semibold;
    constexpr float kSize = 16.0f;
    constexpr float kTracking = 1.6f;
    for (int i = 0; i < form_.row_count(); ++i)
    {
        const ui::FormRow &row = form_.row_at(i);
        const auto pill = std::find_if(pills_.begin(), pills_.end(),
                                       [&](const Pill &p) { return p.row == row.id; });
        if (pill == pills_.end() || font.font == nullptr)
            continue;
        const gfx::Rect rect = form_.row_rect(i);
        if (rect.y < bounds.y || rect.y + rect.h > bounds.y + bounds.h)
            continue;
        const float x = rect.x + form_.style.padding +
                        (i == form_.focus() ? form_.style.focus_shift : 0.0f) +
                        paint.label_width(row.label, form_.style.label_size) + 22.0f;
        const float width = font.font->measure(pill->text, kSize) +
                            kTracking * static_cast<float>(pill->text.size()) + 24.0f;
        // On the name's line: a focused row unfolds its description below it.
        const float top = rect.y + form_.style.row_height * 0.5f - 15.0f;
        canvas.list.rounded_rect({x, top, width, 30.0f}, 15.0f, gfx::Color::rgb(pill->fill));
        ui::text(canvas.list, font, pill->text.c_str(), x + width * 0.5f, top + 21.0f, kSize,
                 gfx::Color::rgb(pill->ink), gfx::Align::center, kTracking);
    }
}

void SetupPanel::draw(ui::Canvas &canvas) const
{
    sheet_.draw(canvas);
}

} // namespace hui::orion
