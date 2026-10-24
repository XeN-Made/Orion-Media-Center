// ORION Media Center - ORION's first-run setup: a few steps that set everything up.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/setup_panel.hpp"

#include "core/i18n.hpp"
#include "library/emulators.hpp"
#include "ui/glyphs.hpp"

#include <algorithm>

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
    kEmulatorRow = 100, // + the emulator's index, or a missing one's past them
};

constexpr const char *kStepNames[] = {i18n::marked("Welcome"),   i18n::marked("Look"),
                                      i18n::marked("Emulators"), i18n::marked("Box art"),
                                      i18n::marked("Library"),   i18n::marked("Autoboot"),
                                      i18n::marked("Done")};

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
    form_.enter();
}

void SetupPanel::layout()
{
    const gfx::Rect area = sheet_.content_rect();
    constexpr float kHints = 70.0f;
    const float width = std::min(area.w, 1180.0f);
    const float left = area.x + (area.w - width) * 0.5f;
    steps_.set_bounds({left, area.y + 4.0f, width, 96.0f});
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
    const bool left_out = input.nav == Direction::left && !form_.uses_horizontal();
    if (input.is_pressed(Action::back) || left_out)
    {
        if (step_ == Step::welcome)
        {
            feedback.play(audio::Cue::error);
            return Change::none;
        }
        feedback.play(audio::Cue::back);
        go(static_cast<Step>(static_cast<int>(step_) - 1), *look, *prefs);
        return Change::none;
    }

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
        {
            go(static_cast<Step>(static_cast<int>(step_) + 1), *look, *prefs);
            feedback.play(audio::Cue::select);
            return Change::none;
        }
        if (id == kScan)
            return Change::scan_emulators;
        return Change::none;
    }
    if (event != ui::Event::changed)
        return Change::none;
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
    const ui::Theme &theme = form_.style.theme;
    ui::GlyphStyle glyphs = ui::GlyphStyle::dark();
    glyphs.body = theme.text.with_alpha(0.14f);
    glyphs.edge = theme.text.with_alpha(0.0f);
    glyphs.ink = theme.text;
    glyphs.label = theme.text_muted;
    // Hints hold pointers: their words live until the hints are drawn.
    const std::string select = tr("Select");
    const std::string previous = tr("Previous step");
    const ui::Hint hints[] = {
        {ui::Button::cross, select.c_str()},
        {ui::Button::circle, previous.c_str()},
    };
    ui::HintLayout layout;
    layout.size = 38.0f;
    layout.text_size = 22.0f;
    const gfx::Rect area = sheet_.content_rect();
    layout.cy = area.y + area.h - 26.0f;
    // On the first step there is no step before it.
    ui::draw_hints(canvas.list, canvas.fonts, glyphs, hints, step_ == Step::welcome ? 1 : 2,
                   area.x + area.w, true, layout);
}

void SetupPanel::draw(ui::Canvas &canvas) const
{
    sheet_.draw(canvas);
}

} // namespace hui::orion
