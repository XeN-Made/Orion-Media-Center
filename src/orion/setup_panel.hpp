// ORION Media Center - ORION's first-run setup: a few steps that set everything up.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "library/library.hpp"
#include "orion/languages.hpp"
#include "orion/look.hpp"
#include "orion/prefs.hpp"
#include "ui/components/form.hpp"
#include "ui/components/sheet.hpp"
#include "ui/components/wizard.hpp"

#include <string>
#include <vector>

namespace hui::orion
{

// A glass sheet over the whole screen, like Settings: the steps across the
// top, the step's rows under them, the last row of each going on.
//
//   Welcome     what ORION found; get started, or skip setup
//   Look        theme, game backgrounds
//   Emulators   the emulators found (scan again), group their tabs
//   Box art     find it by itself, where it goes
//   Library     return to ORION after a game, reopen on the last tab, sort
//   Autoboot    ORION opens on its own after the jailbreak, and the payload for it
//   Done        where everything is
//
// Up and down move in the rows, Cross takes one, left and right change a
// choice; Circle goes back a step. It edits the Look and the LibraryPrefs it
// is handed and says what changed, as SettingsPanel does; Finish sets
// prefs.setup_done.
//
//   orion::SetupPanel setup;
//   setup.set_theme(theme, reduced_motion);
//   setup.open(look, prefs, feedback);
//   ...
//   switch (setup.handle(input, feedback, &look, &prefs))
//   {
//   case SetupPanel::Change::look:     restyle(); save_look(dir, look); break;
//   case SetupPanel::Change::prefs:    rebuild(); save_prefs(dir, prefs); break;
//   case SetupPanel::Change::finished: save_prefs(dir, prefs); break;   // it has closed
//   default: break;
//   }
//   setup.update(dt);
//   setup.draw(canvas);
class SetupPanel
{
  public:
    enum class Change : std::uint8_t
    {
        none,
        look,           // the Look was edited
        prefs,          // the LibraryPrefs were edited
        art_folder,     // the box art folder changed (prefs.art_dir)
        scan_emulators, // "Scan for emulators" was chosen
        finished,       // the last step (or Skip setup): setup_done is set, the sheet closes
        language,       // the Language row changed (prefs.language)
    };

    enum class Step : std::uint8_t
    {
        welcome,
        look,
        emulators,
        art,
        library,
        autostart,
        done,
    };

    SetupPanel();

    void set_theme(const ui::Theme &theme, bool reduced_motion);
    // The themes the Theme row offers; must outlive the panel.
    void set_themes(const ThemeLibrary &themes)
    {
        themes_ = &themes;
    }
    // What the Language row offers (LanguageLibrary::choices).
    void set_languages(std::vector<LanguageChoice> languages)
    {
        languages_ = std::move(languages);
    }
    // The emulators the Emulators step lists; must outlive the panel.
    void set_emulator(const library::Emulator &emulator)
    {
        emulator_ = &emulator;
    }
    // How many console titles the Welcome step says were found.
    void set_titles(int titles)
    {
        titles_ = titles;
    }
    // What Scan for emulators says under itself.
    void set_scan_status(std::string status)
    {
        scan_status_ = std::move(status);
    }
    // Where the autostart payload is, as the jailbreak's autoloader sees it.
    void set_autostart_payload(std::string path)
    {
        payload_ = std::move(path);
    }
    // ORION's own art folder, to show beside the choice that means it.
    void set_state_dir(std::string dir)
    {
        state_dir_ = std::move(dir);
    }

    void open(const Look &look, const LibraryPrefs &prefs, ui::Feedback &feedback);
    void close(ui::Feedback &feedback);
    bool is_open() const
    {
        return sheet_.is_open();
    }
    bool visible() const
    {
        return sheet_.visible();
    }
    Step step() const
    {
        return step_;
    }
    // Rebuilds the rows from new values (after a scan), keeping the focus.
    void refresh(const Look &look, const LibraryPrefs &prefs);

    Change handle(const InputFrame &input, ui::Feedback &feedback, Look *look, LibraryPrefs *prefs);
    void update(float dt);
    void draw(ui::Canvas &canvas) const;

  private:
    void go(Step step, const Look &look, const LibraryPrefs &prefs);
    void build(const Look &look, const LibraryPrefs &prefs);
    void layout();
    void draw_content(ui::Canvas &canvas, float opacity) const;

    ui::Sheet sheet_;
    ui::Wizard steps_;
    ui::Form form_;
    Step step_ = Step::welcome;
    const ThemeLibrary *themes_ = nullptr;
    std::vector<LanguageChoice> languages_;
    const library::Emulator *emulator_ = nullptr;
    int titles_ = 0;
    std::string scan_status_ = "Finds RetroArch, Porpoise, PS5SX2, PSwanStation, ProsperoEden and "
                               "PS5-ECAM, and their games";
    std::string payload_ = "/data/homebrew/PPSA99061/assets/payloads/orion-autostart.elf";
    std::string state_dir_ = "/data/orion";
};

} // namespace hui::orion
