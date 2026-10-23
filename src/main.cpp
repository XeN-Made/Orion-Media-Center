// ORION Media Center - Application entry point.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Opens the display, controller and audio, then runs the shell every frame:
// read input, update, play the sounds it asked for, draw, present. Lifecycle
// markers and frame pacing are logged for hardware runs.

#include "app/shell.hpp"
#include "app/tour.hpp"
#include "audio/cues.hpp"
#include "audio/mixer.hpp"
#include "audio/music.hpp"
#include "core/frame_stats.hpp"
#include "core/input.hpp"
#include "core/save_file.hpp"
#include "core/settings.hpp"
#include "core/version.hpp"
#include "demo/catalog.hpp"
#include "gfx/canvas.hpp"
#include "gfx/renderer.hpp"
#include "library/emulators.hpp"
#include "library/icon.hpp"
#include "library/library.hpp"
#include "orion/prefs.hpp"
#include "platform/ps5/audio_out.hpp"
#include "platform/ps5/display_egl.hpp"
#include "platform/ps5/pad.hpp"
#include "platform/ps5/system.hpp"
#include "ui/fonts.hpp"

#include <cstring>
#include <GL/glcorearb.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

extern "C" void hui_heap_stats(std::size_t *live_bytes, std::size_t *peak_bytes,
                               std::size_t *blocks, std::size_t *failures);

namespace
{

constexpr const char *kAssets = "/app0/assets";
// Settings live in the title's own save area.
constexpr const char *kDataRoot = "/download0/hui";
// Development output (the tour's pictures and report, the log). A title's
// sandbox has no /data, so it lives in the title's own storage. While the
// title runs, a PC can *read* the folder over FTP as
// /mnt/sandbox/<TITLE_ID>_000/download0/hui/dev, but cannot write there.
constexpr const char *kDevRoot = "/download0/hui/dev";
// Requests therefore come in through the install folder, which a PC can
// write: /data/homebrew/<TITLE_ID>/dev/request.txt is /app0/dev/request.txt
// here. One line: "tour <design id|all> <token>" or "quit - <token>". A
// request is honoured once per token (the last one is remembered in kDevRoot).
constexpr const char *kRequestPath = "/app0/dev/request.txt";
// How long a finished tour stays up so the PC can copy its evidence (the
// storage is unmounted when the title closes) before the app closes itself.
constexpr double kCollectSeconds = 240.0;
// Quarter of a quarter: the title's storage is small and a tour takes
// hundreds of pictures. Enough to compare layout and colour with the PC's.
constexpr int kCaptureWidth = 480;
constexpr int kCaptureHeight = 270;

void log_heap(std::uint64_t frames)
{
    std::size_t live = 0;
    std::size_t peak = 0;
    std::size_t blocks = 0;
    std::size_t failures = 0;
    hui_heap_stats(&live, &peak, &blocks, &failures);
    hui::sys::log("[HUI] heap frames=%llu live=%zu peak=%zu blocks=%zu failures=%zu",
                  static_cast<unsigned long long>(frames), live, peak, blocks, failures);
}

// Opens at *resolution (an index into Settings::kResolutions), falling back
// to 1080p; *resolution reports the mode that opened.
bool open_display(hui::ps5::Display &display, int *resolution)
{
    using hui::Settings;
    const Settings::Resolution &mode = Settings::kResolutions[*resolution];
    if (display.open(mode.width, mode.height))
    {
        hui::sys::log("[HUI] display mode %s %dx%d", mode.label, display.width(), display.height());
        return true;
    }
    if (*resolution == 0)
        return false;
    hui::sys::log("[HUI] display mode %s failed, using 1080p", mode.label);
    *resolution = 0;
    return display.open(Settings::kResolutions[0].width, Settings::kResolutions[0].height);
}

bool load_font(hui::gfx::Renderer &renderer, const char *name, hui::gfx::Font *font,
               hui::ui::FontRef *ref)
{
    std::string data;
    const std::string path = std::string(kAssets) + "/fonts/" + name;
    if (!hui::save::read_file(path, &data) || !font->load(data))
    {
        hui::sys::log("[HUI] font %s failed: %s", name, font->error().c_str());
        return false;
    }
    ref->font = font;
    ref->texture = renderer.batch().create_font_texture(*font);
    return true;
}

// The frame as drawn, bottom row first, as a 24-bit BMP (tour pictures).
bool save_picture(const std::string &path, int width, int height)
{
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const std::uint32_t row = (static_cast<std::uint32_t>(width) * 3 + 3) & ~3u;
    const std::uint32_t size = 54 + row * static_cast<std::uint32_t>(height);
    unsigned char header[54] = {'B', 'M'};
    const auto put = [&](int at, std::uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
            header[at + i] = static_cast<unsigned char>(value >> (8 * i));
    };
    put(2, size);
    put(10, 54);
    put(14, 40);
    put(18, static_cast<std::uint32_t>(width));
    put(22, static_cast<std::uint32_t>(height));
    header[26] = 1;
    header[28] = 24;
    put(34, size - 54);
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr)
        return false;
    bool ok = std::fwrite(header, 1, sizeof(header), file) == sizeof(header);
    std::vector<unsigned char> line(row);
    for (int y = 0; ok && y < height; ++y)
    {
        const unsigned char *in = pixels.data() + static_cast<std::size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x)
        {
            line[static_cast<std::size_t>(x) * 3 + 0] = in[x * 4 + 2];
            line[static_cast<std::size_t>(x) * 3 + 1] = in[x * 4 + 1];
            line[static_cast<std::size_t>(x) * 3 + 2] = in[x * 4 + 0];
        }
        ok = std::fwrite(line.data(), 1, line.size(), file) == line.size();
    }
    return std::fclose(file) == 0 && ok;
}

// How far start-up got, in /data/orion/boot.txt: klog is gone after a crash
// that takes the console down, this file is not. Each step is flushed to
// the disk before the next begins.
void boot_step(const char *step)
{
    static const std::int64_t start = hui::sys::monotonic_us();
    static bool first = true;
    hui::save::ensure_directory("/data/orion");
    std::FILE *file = std::fopen("/data/orion/boot.txt", first ? "w" : "a");
    first = false;
    if (file == nullptr)
        return;
    std::fprintf(file, "%lld ms  %s\n",
                 static_cast<long long>((hui::sys::monotonic_us() - start) / 1000), step);
    std::fflush(file);
    ::fsync(fileno(file));
    std::fclose(file);
}

} // namespace

int main()
{
    using namespace hui;
    boot_step("entry");
    sys::log("[HUI] entry");
    sys::log("[HUI] storage dir=%d", save::ensure_directory(kDataRoot) ? 1 : 0);

    // ORION's folders from before its files were made open to everyone are
    // opened up once: its own and any copy a player made of it
    // (/data/orion-backup), which FTP could otherwise not change or delete.
    // Until it has all worked, it is tried again on the next start; the
    // file says how it went.
    if (std::string done; save::ensure_directory("/data/orion") &&
                          (!save::read_file("/data/orion/opened-up.txt", &done, 256) ||
                           done.rfind("done (3)", 0) != 0))
    {
        int changed = 0;
        int failed = 0;
        std::string report;
        // /data/orion always; any copy of it a player made, as far as /data
        // can be listed from inside the sandbox.
        std::vector<std::string> roots = {"/data/orion"};
        save::ListingStats stats;
        for (const save::DirectoryEntry &entry : save::list_directory("/data", &stats))
        {
            if (entry.directory && entry.name.rfind("orion", 0) == 0 && entry.name != "orion")
                roots.push_back("/data/" + entry.name);
        }
        report += "/data: " + std::to_string(stats.records) + " entries, open errno " +
                  std::to_string(stats.open_error) + "\n";
        for (const std::string &root : roots)
        {
            struct stat info = {};
            const int seen = ::lstat(root.c_str(), &info);
            int missed = 0;
            const int set = save::open_up_tree(root, &missed);
            changed += set;
            failed += missed;
            report += root + ": lstat " + std::to_string(seen) + " mode " +
                      std::to_string(info.st_mode) + ", set " + std::to_string(set) + ", refused " +
                      std::to_string(missed) + "\n";
        }
        save::write_atomic("/data/orion/opened-up.txt",
                           std::string(changed > 0 && failed == 0 ? "done (3)" : "not yet") + ": " +
                               std::to_string(changed) + " opened up for FTP, " +
                               std::to_string(failed) + " refused\n" + report);
        sys::log("[HUI] opened up %d files and folders for FTP, %d refused", changed, failed);
    }
    // Where a player pastes keys for other services (orion/steamgriddb.hpp):
    // there from the start, so it can be edited over FTP.
    if (std::string keys; !save::read_file("/data/orion/keys.txt", &keys, 64u << 10))
        save::write_atomic("/data/orion/keys.txt",
                           "# ORION's keys for other services, one a line. Paste a key after\n"
                           "# the = and take the # away, then open ORION again.\n"
                           "# steamgriddb = \n");

    // The display opens at the saved resolution (1080p if that fails).
    const Settings saved = app::Shell::load_settings(kDataRoot);
    int resolution = ps5::Display::supports_display_modes() ? saved.resolution : 0;
    ps5::Display display;
    if (!open_display(display, &resolution))
    {
        sys::log("[HUI] fatal: display open failed");
        sys::park();
    }

    boot_step("display open");
    gfx::Renderer renderer;
    gfx::Font regular;
    gfx::Font semibold;
    gfx::Font display_font;
    gfx::Font mono;
    gfx::Font pixel;
    gfx::Font hand;
    ui::Fonts fonts;
    if (!renderer.init() ||
        !load_font(renderer, "inter-regular.huifont", &regular, &fonts.regular) ||
        !load_font(renderer, "inter-semibold.huifont", &semibold, &fonts.semibold) ||
        !load_font(renderer, "montserrat-medium.huifont", &display_font, &fonts.display) ||
        !load_font(renderer, "dejavu-sans-mono.huifont", &mono, &fonts.mono) ||
        !load_font(renderer, "press-start-2p.huifont", &pixel, &fonts.pixel) ||
        !load_font(renderer, "patrick-hand.huifont", &hand, &fonts.hand))
    {
        sys::log("[HUI] fatal: renderer init failed");
        sys::park();
    }
    boot_step("renderer and fonts");
    demo::Catalog catalog;
    const std::int64_t art_start = sys::monotonic_us();
    if (!catalog.build_covers(renderer, fonts))
        sys::log("[HUI] cover art failed; designs draw without it");
    sys::log("[HUI] cover art %zu covers in %lld ms", catalog.size(),
             static_cast<long long>((sys::monotonic_us() - art_start) / 1000));

    // The installed titles, as ShadowMountPlus sees them. /data is visible
    // here only because ShadowMountPlus mounts it into the titles it launches.
    boot_step("cover art");
    library::Library titles;
    // The emulators' games: at start-up, and again when Settings asks (Scan
    // for emulators). The prefs hold the player's folders.
    const auto scan_emulators = [&titles](bool starting)
    {
        const orion::LibraryPrefs prefs = orion::load_prefs(titles.state_dir);
        return library::add_emulator_games(
            titles, orion::game_folders(prefs), orion::art_folder(prefs, titles.state_dir),
            prefs.every_version, starting ? boot_step : std::function<void(const char *)>());
    };
    {
        library::ScanOptions scan = library::console_defaults();
        std::string param;
        library::Title self;
        const bool known = save::read_file("/app0/sce_sys/param.json", &param) &&
                           library::read_param_json(param, &self);
        std::vector<std::string> report;
        scan.report = &report;
        const std::int64_t scan_start = sys::monotonic_us();
        titles.titles = library::scan(scan);
        boot_step("titles scanned");
        // ORION is found like any title, so its folder is known, then left
        // out of what it lists.
        if (known)
        {
            const auto own = std::find_if(titles.titles.begin(), titles.titles.end(),
                                          [&](const library::Title &t) { return t.id == self.id; });
            if (own != titles.titles.end())
            {
                titles.self_path = own->path;
                titles.titles.erase(own);
            }
        }
        titles.scanned = true;
        titles.state_dir = "/data/orion";
        titles.self_id = known ? self.id : std::string();
        // What the system has registered beyond the homebrew folders, as the
        // last run found it (orion/registered_titles.hpp refreshes it).
        {
            std::string cache;
            if (save::read_file(titles.state_dir + "/registered.tsv", &cache, 4u << 20))
                library::merge_registered(titles, cache, titles.state_dir + "/icons",
                                          titles.self_id);
        }
        titles.themes_dir = std::string(kAssets) + "/themes";
        titles.offer_setup = true;
        library::prefer_saved_icons(titles, titles.state_dir + "/icons");
        scan_emulators(true);
        library::apply_names(titles, orion::load_prefs(titles.state_dir).title_names);
        library::apply_custom_art(titles, titles.state_dir + "/custom-art");
        const int icons = library::load_icons(titles, renderer.batch());
        char summary[128];
        std::snprintf(summary, sizeof(summary), "titles=%zu icons=%d in %lld ms",
                      titles.titles.size(), icons,
                      static_cast<long long>((sys::monotonic_us() - scan_start) / 1000));
        sys::log("[HUI] library %s", summary);
        // The same report where a PC can read it over FTP after the app has
        // closed (klog is gone by then): /data/orion/scan.txt.
        std::string text = std::string(summary) + "\n";
        for (const std::string &line : report)
        {
            sys::log("[HUI] library %s", line.c_str());
            text += line + "\n";
        }
        // How this sandbox sees /data: a separate device for /data/homebrew
        // means a mount on top of /data that this view does not show.
        {
            save::ListingStats stats;
            const std::vector<save::DirectoryEntry> top = save::list_directory("/data", &stats);
            std::string names;
            for (const save::DirectoryEntry &entry : top)
                names += " " + entry.name + (entry.directory ? "/" : "");
            struct stat data_info = {};
            struct stat homebrew_info = {};
            const int data_rc = ::stat("/data", &data_info);
            const int homebrew_rc = ::stat("/data/homebrew", &homebrew_info);
            char line[192];
            std::snprintf(line, sizeof(line),
                          "/data: %zu entries (open errno %d); stat /data rc=%d dev=%llu; "
                          "/data/homebrew rc=%d dev=%llu mode=%o",
                          top.size(), stats.open_error, data_rc,
                          static_cast<unsigned long long>(data_info.st_dev), homebrew_rc,
                          static_cast<unsigned long long>(homebrew_info.st_dev),
                          static_cast<unsigned>(homebrew_info.st_mode));
            text += std::string(line) + "\n/data names:" + names + "\n";
            sys::log("[HUI] library %s", line);
        }
        if (save::ensure_directory("/data/orion"))
            save::write_atomic("/data/orion/scan.txt", text);
    }

    boot_step("library scanned");
    ps5::Pad pad;
    pad.open();
    InputTracker tracker;
    audio::Mixer mixer;
    // The music stream attaches to the mixer before the audio thread starts.
    audio::MusicPlayer music;
    // ORION's own songs, and the player's in /data/orion/music (48 kHz .ogg),
    // a folder made here so it is there to copy songs into over FTP.
    save::ensure_directory("/data/orion/music");
    const int tracks = music.init(
        mixer, std::vector<std::string>{std::string(kAssets) + "/audio/music", "/data/orion/music"},
        static_cast<std::uint64_t>(sys::monotonic_us()));
    sys::log("[HUI] music songs=%d", tracks);
    ps5::AudioOut audio_out;
    audio_out.start(mixer);
    audio::SoundBank sounds;
    const auto bank = sounds.load(std::string(kAssets) + "/audio/sfx");
    sys::log("[HUI] sounds files=%d rejected=%d", bank.files, bank.rejected);
    for (const std::string &error : bank.errors)
        sys::log("[HUI] sound rejected %s", error.c_str());

    boot_step("audio");
    app::Shell shell(fonts, catalog, titles, kDataRoot, renderer.glass_texture(),
                     app::orion_registry());
    boot_step("screen built");
    const std::string version = read_content_version("/app0/sce_sys/param.json");
    shell.set_version(version);
    sys::log("[HUI] version %s designs=%zu", version.empty() ? "unknown" : version.c_str(),
             shell.concept_count());

    // A tour request turns this launch into an unattended validation run:
    // every design is shown with scripted input, pictures and a report are
    // written to kDevRoot, and the app closes itself a while later.
    std::unique_ptr<app::Tour> tour;
    save::ensure_directory(kDevRoot);
    const std::string handled_path = std::string(kDevRoot) + "/handled.txt";
    const std::string report_path = std::string(kDevRoot) + "/tour/report.txt";
    bool quit_requested = false;
    const auto poll_request = [&]()
    {
        std::string request;
        if (!save::read_file(kRequestPath, &request, 256))
            return;
        char verb[16] = {};
        char argument[64] = {};
        char token[64] = {};
        if (std::sscanf(request.c_str(), "%15s %63s %63s", verb, argument, token) != 3)
            return;
        std::string handled;
        save::read_file(handled_path, &handled, 128);
        if (handled == token)
            return; // this request was honoured by an earlier launch (or frame)
        save::write_atomic(handled_path, token);
        if (std::strcmp(verb, "quit") == 0)
        {
            quit_requested = true;
        }
        else if (std::strcmp(verb, "tour") == 0 && !tour)
        {
            save::ensure_directory(std::string(kDevRoot) + "/tour");
            std::remove(report_path.c_str()); // a new report must not be taken for an old one
            const bool all = std::strcmp(argument, "all") == 0;
            tour = std::make_unique<app::Tour>(shell, all ? std::string() : std::string(argument));
            sys::log("[HUI] tour requested: %s token=%s", argument, token);
        }
    };
    poll_request();
    double collect_wait = -1.0; // seconds a finished tour has waited to be collected

    std::int64_t previous = sys::monotonic_us();
    std::uint64_t frames = 0;
    FrameStats stats;
    // The FPS readout averages over half a second so the number is readable.
    double fps_seconds = 0.0;
    int fps_frames = 0;
    PadSample samples[64];
    gfx::Canvas capture;
    std::int64_t last_frame_start = sys::monotonic_us();
    std::vector<std::string> icon_queue; // box art wanted on screen, not loaded yet
    for (;;)
    {
        const std::int64_t now = sys::monotonic_us();
        // Animation time is start-to-start (one full frame), not the gap
        // between the previous swap returning and this frame beginning.
        float dt = frames == 0 ? 1.0f / 60.0f : static_cast<float>(now - last_frame_start) / 1e6f;
        last_frame_start = now;
        if (dt > 0.05f)
            dt = 0.05f; // a hitch must not teleport the animations
        const std::size_t count = pad.read(samples);
        InputFrame input = tracker.update(std::span<const PadSample>(samples, count),
                                          static_cast<std::uint64_t>(now));
        if (tour)
            input = tour->step(dt);

        // Before the design's update, which rebuilds on the new generation
        // before it touches an index into the titles.
        if (shell.take_rescan_request())
        {
            std::vector<library::Title> before;
            for (const library::Title &title : titles.titles)
            {
                if (title.is_rom() && title.texture != 0)
                    before.push_back(title);
            }
            const int games = scan_emulators(false);
            library::apply_names(titles, orion::load_prefs(titles.state_dir).title_names);
            library::apply_custom_art(titles, titles.state_dir + "/custom-art");
            // A game still there keeps its picture; the others' go.
            for (library::Title &title : titles.titles)
            {
                for (library::Title &old : before)
                {
                    if (old.texture == 0 || old.id != title.id ||
                        !(title.icon.empty() || title.icon == old.icon))
                        continue;
                    title.icon = old.icon;
                    title.texture = std::exchange(old.texture, 0u);
                    title.icon_width = old.icon_width;
                    title.icon_height = old.icon_height;
                }
            }
            for (const library::Title &old : before)
            {
                if (old.texture != 0)
                    renderer.batch().delete_texture(old.texture);
            }
            library::load_icons(titles, renderer.batch());
            ++titles.generation;
            sys::log("[ORION] emulators scanned again: %zu apps, %d games",
                     titles.emulator.apps.size(), games);
        }
        // The player renamed titles.
        if (auto names = shell.take_new_names(); !names.empty())
        {
            library::apply_names(titles, names);
            ++titles.generation;
        }
        // The system's registered titles changed: the new ones join.
        if (shell.take_registered_change())
        {
            std::string cache;
            if (save::read_file(titles.state_dir + "/registered.tsv", &cache, 4u << 20) &&
                library::merge_registered(titles, cache, titles.state_dir + "/icons",
                                          titles.self_id) > 0)
            {
                library::apply_names(titles, orion::load_prefs(titles.state_dir).title_names);
                library::apply_custom_art(titles, titles.state_dir + "/custom-art");
                library::load_icons(titles, renderer.batch());
                ++titles.generation;
            }
        }
        // The player chose a picture for a title, or gave it its own back.
        if (shell.take_custom_art_change())
        {
            for (const std::string &id :
                 library::apply_custom_art(titles, titles.state_dir + "/custom-art"))
            {
                for (library::Title &title : titles.titles)
                {
                    if (title.id != id)
                        continue;
                    if (title.texture != 0)
                        renderer.batch().delete_texture(title.texture);
                    if (title.sharp_texture != 0)
                        renderer.batch().delete_texture(title.sharp_texture);
                    title.texture = title.sharp_texture = 0;
                    title.sharp_width = title.sharp_height = 0;
                    if (!title.icon.empty())
                        icon_queue.insert(icon_queue.begin(), id);
                }
            }
            ++titles.generation;
        }
        shell.update(input, dt);
        // Games' box art that comes into view, a few a frame. The newest
        // wishes go first: after a quick run through the tabs, the one on
        // screen loads before those passed on the way.
        if (auto wanted = shell.take_wanted_icons(); !wanted.empty())
            icon_queue.insert(icon_queue.begin(), wanted.begin(), wanted.end());
        if (!icon_queue.empty())
            library::load_wanted_icons(titles, &icon_queue, renderer.batch(), 3);
        // Sharp copies for what is shown large: one new one a frame.
        library::load_sharp_icons(titles, shell.sharp_icons(), renderer.batch(), 1);
        // Box art ORION downloaded: loaded as the scan's icons were (a
        // game's picture waits until it is wanted).
        if (auto icons = shell.take_new_icons(); !icons.empty())
        {
            for (const auto &[id, path] : icons)
            {
                for (library::Title &title : titles.titles)
                {
                    if (title.id != id)
                        continue;
                    // A picture already shown is replaced (Download HD picture);
                    // a game's first one waits until it is wanted on screen.
                    const bool shown = title.texture != 0;
                    if (shown)
                        renderer.batch().delete_texture(title.texture);
                    if (title.sharp_texture != 0)
                        renderer.batch().delete_texture(title.sharp_texture);
                    title.texture = title.sharp_texture = 0;
                    title.sharp_width = title.sharp_height = 0;
                    title.icon = path;
                    if (shown || !title.is_rom())
                        icon_queue.insert(icon_queue.begin(), id);
                }
            }
        }
        if (shell.take_settings_changed())
        {
            const Settings &settings = shell.settings();
            mixer.set_bus_gain(audio::Bus::music, Settings::gain(settings.music_volume));
            mixer.set_bus_gain(audio::Bus::sfx, Settings::gain(settings.sfx_volume));
            mixer.set_bus_gain(audio::Bus::ui, Settings::gain(settings.ui_volume));
            InputSettings input_settings = tracker.settings();
            input_settings.swap_confirm = settings.swap_confirm;
            tracker.set_settings(input_settings);
        }
        const app::Feedback &feedback = shell.feedback();
        for (const audio::CueEvent &event : feedback.cues)
        {
            sounds.play(mixer, event.set == audio::SoundSet::count ? shell.sound_set() : event.set,
                        event);
            if (event.cue == audio::Cue::complete || event.cue == audio::Cue::welcome)
                music.duck();
        }
        if (shell.settings().haptics && feedback.rumble_strength > 0.0f)
            pad.rumble(feedback.rumble_strength, feedback.rumble_seconds);
        pad.tick(dt);
        if (shell.settings().light_bar)
        {
            const gfx::Color accent = shell.accent();
            pad.set_light_bar(static_cast<std::uint8_t>(accent.r * 255.0f),
                              static_cast<std::uint8_t>(accent.g * 255.0f),
                              static_cast<std::uint8_t>(accent.b * 255.0f));
        }
        if (shell.take_skip_song())
            music.skip();
        music.pump(dt);
        shell.set_now_playing(music.current());

        shell.compose(renderer);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        renderer.present(0, display.width(), display.height());
        if (tour && !tour->capture().empty())
        {
            // The picture is the same frame drawn once more into a small
            // off-screen target: reading the display surface back is slow, and
            // a quarter-size picture is enough to compare with the PC render.
            if (capture.texture() == 0)
                capture.create(kCaptureWidth, kCaptureHeight, 1);
            capture.bind();
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            renderer.present(capture.framebuffer(), kCaptureWidth, kCaptureHeight);
            glBindFramebuffer(GL_FRAMEBUFFER, capture.framebuffer());
            const std::string path = std::string(kDevRoot) + "/tour/" + tour->capture() + ".bmp";
            const bool ok = save_picture(path, kCaptureWidth, kCaptureHeight);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            sys::log("[HUI] tour picture %s ok=%d", tour->capture().c_str(), ok ? 1 : 0);
            tour->capture_done();
            last_frame_start = sys::monotonic_us(); // saving is slow; the frame was not
        }
        if (!display.swap())
        {
            sys::log("[HUI] fatal: swap failed frame=%llu error=%s",
                     static_cast<unsigned long long>(frames),
                     ps5::egl_error_name(display.last_error()));
            sys::park();
        }
        ++frames;
        const std::int64_t presented = sys::monotonic_us();
        const float frame_ms = static_cast<float>(presented - previous) / 1000.0f;
        app::Telemetry &telemetry = shell.telemetry();
        if (frames == 1)
        {
            boot_step("first frame shown");
            sys::log("[HUI] first-swap ok shapes=%zu draws=%zu", renderer.last_instances(),
                     renderer.last_draw_calls());
            const bool hidden = sys::hide_splash_screen();
            sys::log("[HUI] ready splash_hidden=%d", hidden ? 1 : 0);
            log_heap(frames);
        }
        else
        {
            stats.add(static_cast<double>(frame_ms));
            telemetry.push(frame_ms);
            if (tour)
                tour->record_frame(frame_ms, renderer.last_draw_calls(), renderer.last_instances());
        }
        previous = presented;
        telemetry.draw_calls = renderer.last_draw_calls();
        telemetry.instances = renderer.last_instances();
        telemetry.voices = mixer.active_voices();
        fps_seconds += static_cast<double>(frame_ms) / 1000.0;
        ++fps_frames;
        if (fps_seconds >= 0.5)
        {
            telemetry.fps = static_cast<float>(fps_frames / fps_seconds);
            telemetry.average_ms = static_cast<float>(fps_seconds * 1000.0 / fps_frames);
            fps_seconds = 0.0;
            fps_frames = 0;
        }

        if (tour && tour->finished())
        {
            std::string report;
            for (const std::string &line : tour->report())
            {
                sys::log("[HUI] %s", line.c_str());
                report += line + "\n";
            }
            save::write_atomic(report_path, report);
            sys::log("[HUI] tour finished designs=%zu", tour->report().size());
            // The storage is unmounted when the title closes, so the app stays
            // up for a while: the PC copies the evidence, then the app closes.
            tour.reset();
            collect_wait = 0.0;
        }
        if (collect_wait >= 0.0)
            collect_wait += static_cast<double>(frame_ms) / 1000.0;
        // A design handed the console to another title: close the way the
        // system expects (never a kill), and let the launch helper take over.
        if (shell.exit_requested())
        {
            sys::log("[HUI] closing: a launch was handed to the helper");
            pad.close();
            sys::quit();
        }
        if (frames % 60 == 0)
        {
            poll_request();
            if (quit_requested || collect_wait > kCollectSeconds)
            {
                sys::log("[HUI] closing: %s",
                         quit_requested ? "quit requested" : "tour evidence window over");
                pad.close();
                sys::quit();
            }
        }
        if (stats.count() == 600)
        {
            char summary[160];
            stats.format(summary, sizeof(summary));
            sys::log("[HUI] %s draws=%zu shapes=%zu", summary, renderer.last_draw_calls(),
                     renderer.last_instances());
            sys::log("[HUI] audio grains=%llu errors=%llu voices=%d music=%s underruns=%llu",
                     static_cast<unsigned long long>(audio_out.grains()),
                     static_cast<unsigned long long>(audio_out.errors()), mixer.active_voices(),
                     music.current().c_str(),
                     static_cast<unsigned long long>(mixer.stream_underruns()));
            stats.reset();
            if (frames % 3600 < 600)
                log_heap(frames);
        }
    }
}
