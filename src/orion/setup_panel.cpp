// ps5-homebrew-ui - ORION's first-run setup: a few steps that set everything up.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/setup_panel.hpp"

#include "library/emulators.hpp"
#include "ui/glyphs.hpp"

#include <algorithm>

namespace hui::orion
{

namespace
{

enum : int
{
    kNext = 1, // the row that goes on, last on every step
    kSkip,
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

constexpr const char *kStepNames[] = {"Welcome", "Look",      "Emulators", "Box art",
                                      "Library", "Autostart", "Done"};

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
    sheet_.set_title("Set up ORION");
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
    form_.clear();
    const auto next = [&](const char *label, const std::string &description)
    {
        ui::FormRow &row = form_.add_action(kNext, label);
        row.chevron = true;
        row.description = description;
    };
    switch (step_)
    {
    case Step::welcome:
    {
        form_.add_header("Welcome to ORION Media Center");
        next("Get started", "A few choices, all of them in Settings later");
        int games = 0;
        std::string emulators;
        for (std::size_t i = 0; emulator_ != nullptr && i < emulator_->apps.size(); ++i)
        {
            games += emulator_->apps[i].games;
            emulators += (i == 0 ? "" : ", ") + emulator_->apps[i].name;
        }
        form_.add_value(kInfo, "On this console", std::to_string(titles_) + " titles");
        form_.add_value(kInfoEmulators, "Emulators",
                        emulators.empty() ? std::string("None yet")
                                          : emulators + "  ·  " + std::to_string(games) +
                                                (games == 1 ? " game" : " games"));
        form_.add_action(kSkip, "Skip setup").description =
            "Use ORION as it is; Settings (R3) has everything";
        break;
    }
    case Step::look:
    {
        form_.add_header("Look");
        std::vector<std::string> names;
        int base = 0;
        for (std::size_t i = 0; themes_ != nullptr && i < themes_->size(); ++i)
        {
            names.push_back(themes_->at(i).name);
            if (themes_->at(i).id == look.base)
                base = static_cast<int>(i);
        }
        if (names.empty())
            names.emplace_back("ORION");
        form_.add_choice(kBase, "Theme", std::move(names), base).description =
            "ORION's galaxy glass, or another; the screen changes as you choose";
        form_.add_toggle(kGameBackgrounds, "Game backgrounds", prefs.game_backgrounds).description =
            "The focused game's picture behind the covers, as the home screen shows it";
        next("Continue", "Colours, glass and more are in Settings, Appearance");
        break;
    }
    case Step::emulators:
    {
        form_.add_header("Emulators");
        int row = 0;
        for (std::size_t i = 0; emulator_ != nullptr && i < emulator_->apps.size(); ++i)
        {
            const library::EmulatorApp &app = emulator_->apps[i];
            form_
                .add_value(kEmulatorRow + row++, app.name,
                           std::to_string(app.games) + (app.games == 1 ? " game" : " games"))
                .description = app.starts_games ? "ORION starts its games"
                                                : "Its games open it; you pick the game there";
        }
        for (const library::KnownEmulator &known : library::known_emulators())
        {
            if (emulator_ != nullptr && emulator_->app(known.key) != nullptr)
                continue;
            form_.add_value(kEmulatorRow + row++, known.name, "Not installed").description =
                std::string(known.plays) + ": install it, then scan";
        }
        form_.add_action(kScan, "Scan for emulators").description = scan_status_;
        form_.add_toggle(kGroup, "Group emulator tabs", prefs.group_systems).description =
            "One Emulators tab with a row of systems under it (L2 and R2), or a tab each";
        next("Continue", "Game folders of your own: Settings, Emulators");
        break;
    }
    case Step::art:
    {
        form_.add_header("Box art");
        form_.add_toggle(kArtAuto, "Find box art by itself", prefs.auto_art).description =
            "Games without a cover get one from the internet while ORION runs";
        const int place = art_place(prefs.art_dir);
        std::vector<std::string> places(kArtPlaceNames, kArtPlaceNames + kArtPlaceCount);
        if (place >= kArtPlaceCount)
            places.push_back("Your own folder");
        form_.add_choice(kArtFolder, "Box art folder", std::move(places), place).description =
            "Where covers go: " + art_folder(prefs, state_dir_);
        next("Continue", "Find box art now and more: Settings, Box art");
        break;
    }
    case Step::library:
    {
        form_.add_header("Library");
        form_.add_toggle(kReturn, "Return to ORION after a game", prefs.return_on_close)
            .description = "Close a game from the PS button menu and ORION opens again";
        form_.add_toggle(kReopen, "Reopen on last tab", prefs.reopen_last_tab).description =
            "ORION starts on the tab you left it on";
        form_
            .add_choice(kSort, "Sort by", {kSortNames, kSortNames + kSortCount},
                        sort_index(prefs.sort))
            .description = "Every tab can have its own: Up from the first row of covers";
        next("Continue", "Tabs of your own, themes per tab: Settings, Tabs");
        break;
    }
    case Step::autostart:
    {
        form_.add_header("Autostart");
        // A row that only shows a value never shows its description: what to
        // do goes on the toggle, the file's place on a row of its own.
        form_.add_toggle(kAutostart, "Open ORION after the jailbreak", prefs.autostart)
            .description =
            "Add the payload below to your autoloader; ORION opens as the browser closes";
        const std::size_t slash = payload_.rfind('/');
        form_.add_value(kPayload, "Payload", payload_.substr(slash + 1));
        form_.add_value(kPayloadFolder, "In the folder", payload_.substr(0, slash));
        next("Continue",
             "Turn it off here or in Settings, Library, without touching the autoloader");
        break;
    }
    case Step::done:
    {
        form_.add_header("You're set");
        next("Start using ORION", "Settings changes any of this later");
        form_.add_value(kTip, "Settings", "R3").description = "Or Options on an empty tab";
        form_.add_value(kTip + 1, "Search", "Triangle").description = "Type part of a name";
        form_.add_value(kTip + 2, "Sort a tab", "Up").description =
            "From the first row of covers, then Cross";
        form_.add_value(kTip + 3, "Tabs and systems", "L1 R1, L2 R2").description =
            "The shelves, and the systems under Emulators";
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
    const ui::Hint hints[] = {
        {ui::Button::cross, "Select"},
        {ui::Button::circle, "Previous step"},
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
