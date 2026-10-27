// ORION Media Center - ORION's Settings: a full screen of sections, with a page for every tab.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/settings.hpp"
#include "core/tween.hpp"
#include "library/library.hpp"
#include "orion/languages.hpp"
#include "orion/look.hpp"
#include "orion/drives.hpp"
#include "orion/prefs.hpp"
#include "ui/components/form.hpp"
#include "ui/components/sheet.hpp"
#include "ui/components/sidenav.hpp"

#include <string>
#include <utility>
#include <vector>

namespace hui::orion
{

// One tab as Settings lists it: hidden tabs too, so they can come back.
struct TabEntry
{
    std::string key;  // "all", "games", "sys:Nintendo - Nintendo DS", "tab:t1"
    std::string name; // as the tab bar shows it
};

// A glass sheet over the whole screen: a rail of sections on the left, the
// section's rows on the right.
//
//   Library      sort order, reopen on the last tab, moved titles leave their shelf
//   Tabs         new tab, then every tab; each opens a page of its own:
//                shown, theme, cover shape, sort, (its core,) rename, move, delete
//   Emulators    scan for emulators; every emulator found, each with a page:
//                its folders and the player's game folders (each with a page:
//                system, remove); then per system with games: on or off, its core
//   Box art      find it by itself, find it now
//   Appearance   theme, background, sky, nebula, glass, colours, chrome
//
// Up and down move in the rail and show its section; right or Cross goes
// into the rows; left (on a row that does not take it) or Circle comes back,
// from a tab's page first to the list of tabs. L1 and R1 step through the
// sections from anywhere. Circle in the rail closes Settings.
//
// It edits the Look and the LibraryPrefs it is handed directly and says what
// changed, so the screen can restyle, re-sort and save.
//
//   orion::SettingsPanel settings;
//   settings.set_theme(theme, reduced_motion);
//   settings.set_tabs(every_tab);
//   settings.open(look, prefs, feedback);
//   ...
//   switch (settings.handle(input, feedback, &look, &prefs))
//   {
//   case SettingsPanel::Change::look:  restyle(); save_look(dir, look); break;
//   case SettingsPanel::Change::prefs: rebuild(); save_prefs(dir, prefs); break;
//   case SettingsPanel::Change::new_tab: open_keyboard(); break;
//   case SettingsPanel::Change::rename_tab: rename(settings.tab_id()); break;
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
        look,            // the Look was edited
        prefs,           // the LibraryPrefs were edited
        new_tab,         // "New tab" was chosen: the screen asks for a name
        save_theme,      // "Save as new theme" was chosen: the screen asks for a name
        rename_tab,      // "Rename" on a tab's page: tab_id() says which custom tab
        fetch_art,       // "Find box art now" was chosen
        art_folder,      // the box art folder changed (prefs.art_dir)
        type_art_folder, // "Your own folder" was chosen: the screen asks for a path
        type_grid_key,   // "SteamGridDB key" was chosen: the screen asks for it
        add_rom_folder,  // "Type a path" in the folder browser: the screen asks for one, for
                         // emulator_page()
        add_folder, // "Use this folder" in the folder browser: browse_result(), for emulator_page()
        copy_backdrops, // "Copy them again": the screen asks the helper anew
        folders,        // a game folder changed or went: save and scan again
        scan_emulators, // "Scan for emulators" was chosen
        run_setup,      // "Run setup again": the screen closes Settings and opens the setup
        sound,          // a volume changed in the app's Settings (set_app_settings)
        skip_song,      // "Next song" was chosen
        check_updates,  // "Check now" (Library, updates)
        install_update, // "Install ORION <version>": the screen hands it to the helper
        language,       // the Language row changed (prefs.language)
        whats_new,      // "What's new" (About): the screen shows this version's notes
        type_romm_url,  // "RomM server" was chosen: the screen asks for the address
        type_romm_code, // "Pair with RomM" was chosen: the screen asks for the code
        romm_sync,      // "Sync RomM games" was chosen
        romm_browse,    // "Browse RomM games": the screen opens the RomM Games screen
    };

    enum class Section : std::uint8_t
    {
        library,
        tabs,
        emulators,
        art,
        storage,
        sound,
        appearance,
        romm,   // the RomM server: its address, pairing, which games show
        system, // the language, autoboot, rest mode, returning after a game
        about,  // the version, updates, where things are
    };

    SettingsPanel();

    void set_theme(const ui::Theme &theme, bool reduced_motion);
    // The themes the Theme rows offer; must outlive the panel.
    void set_themes(const ThemeLibrary &themes)
    {
        themes_ = &themes;
    }
    // What the Language row offers (LanguageLibrary::choices).
    void set_languages(std::vector<LanguageChoice> languages)
    {
        languages_ = std::move(languages);
    }
    // Every tab, in the tab bar's order, hidden ones included.
    // The tab the library was on when Settings opened: Appearance offers a
    // theme for it alone, above the theme every tab wears.
    void set_current_tab(std::string key)
    {
        current_tab_ = std::move(key);
    }
    void set_tabs(std::vector<TabEntry> tabs)
    {
        tabs_ = std::move(tabs);
    }
    // What the box art row says under itself: the last run, or this one.
    void set_art_status(std::string status);
    // Whether the player has given a SteamGridDB key (orion/steamgriddb.hpp).
    void set_grid_key(bool given)
    {
        grid_key_ = given;
    }
    // RomM (orion/romm.hpp): the server's address (empty: none), whether
    // ORION has a token for it, and what the last sync did.
    void set_romm(std::string address, bool paired, std::string status)
    {
        romm_address_ = std::move(address);
        romm_paired_ = paired;
        romm_status_ = std::move(status);
    }
    // Where the key page answers ("http://192.168.1.30:9062"), or empty
    // while it does not (orion/key_page.hpp).
    void set_key_page(std::string address)
    {
        key_page_ = std::move(address);
    }
    // ORION's folder as the launch helper sees it, and what its backdrops
    // folder holds (shown under Storage).
    void set_storage(std::string self_path, int backdrops)
    {
        self_path_ = std::move(self_path);
        backdrops_ = backdrops;
    }
    // The drives Storage shows, how full each is and the titles on it.
    void set_drives(std::vector<Drive> drives)
    {
        drives_ = std::move(drives);
    }
    // ORION's own art folder, to show beside the choice that means it.
    void set_state_dir(std::string dir)
    {
        state_dir_ = std::move(dir);
    }
    // The emulators and systems the Emulators rows offer; must outlive the panel.
    void set_emulator(const library::Emulator &emulator)
    {
        emulator_ = &emulator;
    }
    // Where the autostart payload is, as the jailbreak's autoloader sees it.
    void set_autostart_payload(std::string path)
    {
        payload_ = std::move(path);
    }
    // What Scan for emulators says under itself: the last scan, or this one.
    void set_scan_status(std::string status)
    {
        scan_status_ = std::move(status);
    }
    // What Check now says under itself: this version, and the newest one.
    void set_update_status(std::string status)
    {
        update_status_ = std::move(status);
    }
    // The version an update made ready is for, which adds "Install ORION
    // <version>"; empty for none.
    void set_update_ready(std::string version)
    {
        update_ready_ = std::move(version);
    }
    // This ORION's version, for About ("01.001.002"; empty: unknown).
    void set_title_id(std::string id)
    {
        title_id_ = std::move(id);
    }
    void set_version(std::string version)
    {
        version_ = std::move(version);
    }
    // The emulator whose page is open ("porpoise"), or empty.
    const std::string &emulator_page() const
    {
        return emulator_page_;
    }
    // The app's own settings, whose volumes the Sound rows edit; must outlive
    // the panel. Without them there is no Sound section.
    void set_app_settings(Settings &settings)
    {
        app_settings_ = &settings;
    }
    // The song playing now, for the Sound section.
    void set_now_playing(std::string song)
    {
        now_playing_ = std::move(song);
    }
    // Where the folder browser starts: the drives and folders a game folder
    // can be in, as (name, path). Ones that do not exist are left out.
    void set_browse_places(std::vector<std::pair<std::string, std::string>> places)
    {
        places_ = std::move(places);
    }
    // The folder "Use this folder" chose.
    const std::string &browse_result() const
    {
        return browse_path_;
    }
    // After a scan: the rail (Box art comes and goes with the systems) and
    // the rows, staying in the section that is showing.
    void rebuild_nav(const Look &look, const LibraryPrefs &prefs);
    void open(const Look &look, const LibraryPrefs &prefs, ui::Feedback &feedback);
    // Opens straight on one tab's page.
    void open_tab(const std::string &key, const Look &look, const LibraryPrefs &prefs,
                  ui::Feedback &feedback);
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
    // The custom tab a rename is for ("t1").
    const std::string &tab_id() const
    {
        return tab_id_;
    }
    Section section() const
    {
        return section_;
    }
    // The tab whose page is showing, or empty.
    const std::string &tab_page() const
    {
        return page_;
    }
    // Previewing: in the rows that change the look (Appearance, a tab's own
    // page) Settings narrows to a panel at the side, so the library behind
    // shows each change as it is made.
    bool previewing() const;
    // The tab the library shows while a tab's page is previewed, or empty.
    std::string preview_tab() const;

    void update(float dt);
    void draw(ui::Canvas &canvas) const;

  private:
    void build(const Look &look, const LibraryPrefs &prefs);
    void build_library(const LibraryPrefs &prefs);
    void build_romm(const LibraryPrefs &prefs);
    void build_system(const LibraryPrefs &prefs);
    void build_tabs(const LibraryPrefs &prefs);
    void build_tab_page(const LibraryPrefs &prefs);
    void build_emulators(const LibraryPrefs &prefs);
    void build_emulator_page(const library::EmulatorApp &app, const LibraryPrefs &prefs);
    void build_browser();
    void browse(std::string path, const Look &look, const LibraryPrefs &prefs);
    void browse_up(const Look &look, const LibraryPrefs &prefs);
    const library::EmulatorApp *page_app() const;
    void build_art(const LibraryPrefs &prefs);
    void build_storage(const LibraryPrefs &prefs);
    void build_sound(const LibraryPrefs &prefs);
    void build_about(const LibraryPrefs &prefs);
    void build_folder_page(const LibraryPrefs &prefs);
    Change edit_folder_page(int id, LibraryPrefs *prefs);
    void build_appearance(const Look &look, const LibraryPrefs &prefs);
    void build_nav();
    void layout();
    void show(Section section, const Look &look, const LibraryPrefs &prefs);
    void enter_rows(bool snap);
    void leave_rows();
    Change edit_tab_page(int id, LibraryPrefs *prefs);
    Change edit_appearance(int id, Look *look, const LibraryPrefs &prefs);
    std::vector<std::string> theme_names() const;
    const TabEntry *tab_entry(const std::string &key) const;
    const library::System *system_of(const std::string &key) const;
    void draw_content(ui::Canvas &canvas, float opacity) const;
    void draw_drive(ui::Canvas &canvas, const gfx::Rect &control, const ui::FormRow &row) const;

    ui::Sheet sheet_;
    ui::SideNav nav_;
    ui::Form form_;
    tween::Spring width_;           // the sheet's width: the whole screen, or the preview's panel
    float wide_ = 1.0f;             // 1 at the whole screen, 0 at the panel: the rail fades with it
    std::vector<Section> sections_; // the rail's entries
    Section section_ = Section::library;
    bool in_rows_ = false;
    std::string page_;        // the tab whose page is open; empty: the list
    std::string current_tab_; // the library's tab behind Settings
    const ThemeLibrary *themes_ = nullptr;
    std::vector<LanguageChoice> languages_;
    std::string on_word_; // the switches' On and Off (form_.style holds pointers)
    std::string off_word_;
    const library::Emulator *emulator_ = nullptr;
    std::vector<TabEntry> tabs_;
    std::string tab_id_;
    std::string state_dir_ = "/data/orion";
    std::string self_path_;
    int backdrops_ = 0;
    std::string emulator_page_; // the emulator whose page is open; empty: the list
    // The folder browser, open over an emulator's page: where it is (empty:
    // the places), and the folders in it.
    Settings *app_settings_ = nullptr;
    std::string now_playing_;
    bool browsing_ = false;
    std::string browse_path_;
    std::vector<std::string> browse_folders_;
    std::vector<std::pair<std::string, std::string>> places_;
    std::vector<Drive> drives_; // Storage: how full each drive is
    int folder_page_ = -1; // the game folder whose page is open (in prefs.rom_folders); -1: none
    std::string scan_status_;
    bool grid_key_ =
        false; // a SteamGridDB key is in keys.txt // empty until a scan reports: what a scan finds
    std::string key_page_; // the key page address, or empty
    std::string romm_address_;
    bool romm_paired_ = false;
    std::string romm_status_;
    std::string update_status_ = "Not checked yet";
    std::string update_ready_;
    std::string version_;
    std::string title_id_ = "PPSA99062";
    std::string payload_ = "/data/homebrew/PPSA99062/assets/payloads/orion-autostart.elf";
    std::string art_status_ = "Covers from thumbnails.libretro.com for games without one";
};

} // namespace hui::orion
