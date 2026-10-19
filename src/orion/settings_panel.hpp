// ps5-homebrew-ui - ORION's Settings: library, appearance and tabs in a glass drawer.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "orion/look.hpp"
#include "orion/prefs.hpp"
#include "ui/components/form.hpp"
#include "ui/components/sheet.hpp"

#include <string>
#include <vector>

namespace hui::orion
{

// A drawer from the right with one form in three sections:
//
//   Library     sort order, reopen on the last tab, moved titles leave their shelf
//   Appearance  base style, background, sky, nebula, clouds, motion, glass,
//               highlight, accent, cover corners, restore ORION's look
//   Tabs        new tab, then one row per custom tab
//
// It edits the Look and the LibraryPrefs it is handed directly and says what
// changed, so the screen can restyle, re-sort and save. Choosing another base
// style starts the appearance over from that theme's own look.
//
//   orion::SettingsPanel settings;
//   settings.set_theme(theme);
//   settings.open(look, prefs, feedback);
//   ...
//   switch (settings.handle(input, feedback, &look, &prefs))
//   {
//   case SettingsPanel::Change::look:  restyle(); save_look(dir, look); break;
//   case SettingsPanel::Change::prefs: rebuild(); save_prefs(dir, prefs); break;
//   case SettingsPanel::Change::new_tab: open_keyboard(); break;
//   case SettingsPanel::Change::edit_tab: open_tab_menu(settings.tab_id()); break;
//   default: break;
//   }
//   settings.update(dt);
//   settings.draw(canvas);
class SettingsPanel
{
  public:
    enum class Change : std::uint8_t
    {
        none,
        look,       // the Look was edited
        prefs,      // the LibraryPrefs were edited
        new_tab,    // "New tab" was chosen: the screen asks for a name
        save_theme, // "Save as new theme" was chosen: the screen asks for a name
        edit_tab,   // a tab row was chosen: tab_id() says which
    };

    SettingsPanel();

    void set_theme(const ui::Theme &theme, bool reduced_motion);
    // The themes the Style row offers; must outlive the panel.
    void set_themes(const ThemeLibrary &themes)
    {
        themes_ = &themes;
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
    // Rebuilds the rows from new values, keeping the focus where it was.
    void refresh(const Look &look, const LibraryPrefs &prefs);

    Change handle(const InputFrame &input, ui::Feedback &feedback, Look *look, LibraryPrefs *prefs);
    const std::string &tab_id() const
    {
        return tab_id_;
    }
    // Where the focused row is on screen, for anchoring a menu to it.
    gfx::Rect focused_row() const;

    void update(float dt);
    void draw(ui::Canvas &canvas) const;

  private:
    void build(const Look &look, const LibraryPrefs &prefs);

    ui::Sheet sheet_;
    ui::Form form_;
    const ThemeLibrary *themes_ = nullptr;
    std::vector<std::string> tab_ids_; // the tab of each tab row, by row id
    std::string tab_id_;
};

} // namespace hui::orion
