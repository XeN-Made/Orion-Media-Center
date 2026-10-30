// ORION Media Center - The shell: switches between designs and draws shared chrome.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/concept.hpp"
#include "core/tween.hpp"

#include "library/animation.hpp"
#include "library/icon.hpp"

#include <atomic>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace hui::gfx
{
class Renderer;
}

namespace hui::app
{

// Owns every design and shows one at a time.
//
//   L1 / R1    previous / next design (cross-fade and slide)
//   touchpad   info panel: what the design demonstrates and where its code is
//
// Everything else goes to the active design. The shell also keeps the
// settings on disk and feeds the telemetry the designs may display.
class Shell
{
  public:
    // designs is what L1/R1 cycles through; with a single design there is no
    // switch and no design banner.
    Shell(const ui::Fonts &fonts, const demo::Catalog &catalog, const library::Library &library,
          std::string data_root, std::uint32_t glass_texture,
          std::span<const ConceptFactory> designs = concept_registry());

    static Settings load_settings(const std::string &data_root);
    // True once a design asked for the app to close (see Context).
    bool exit_requested() const
    {
        return context_.exit_requested;
    }
    // Pictures the designs found since the last call (title id, PNG path).
    std::vector<std::pair<std::string, std::string>> take_new_icons()
    {
        return std::exchange(context_.new_icons, {});
    }
    // Titles whose icons the designs want on screen since the last call.
    std::vector<std::string> take_wanted_icons()
    {
        return std::exchange(context_.wanted_icons, {});
    }
    const std::vector<std::string> &wide_icons() const
    {
        return context_.wide_icons;
    }
    // Titles the designs show large now (Context::sharp_icons).
    const std::vector<std::string> &sharp_icons() const
    {
        return context_.sharp_icons;
    }
    // The song the music plays now, for the designs (Context::now_playing).
    void set_now_playing(const std::string &song)
    {
        if (context_.now_playing != song)
            context_.now_playing = song;
    }
    // True once since a design asked for the next song.
    bool take_skip_song()
    {
        return std::exchange(context_.skip_song, false);
    }
    // True once since a design asked for the emulators to be scanned again.
    bool take_rescan_request()
    {
        return std::exchange(context_.rescan_requested, false);
    }
    bool take_registered_change()
    {
        return std::exchange(context_.registered_changed, false);
    }
    const std::string &title_music() const
    {
        return context_.title_music;
    }
    bool take_custom_art_change()
    {
        return std::exchange(context_.custom_art_changed, false);
    }
    std::vector<std::pair<std::string, std::string>> take_new_names()
    {
        return std::exchange(context_.new_names, {});
    }
    std::vector<std::pair<std::string, std::string>> take_downloaded_games()
    {
        return std::exchange(context_.downloaded_games, {});
    }

    void update(const InputFrame &input, float dt);
    // Queues this frame's layers; the caller presents them.
    void compose(gfx::Renderer &renderer);
    // Records the active design's frame without rendering it. A console draws
    // every frame; a tour on a PC only renders its pictures, so it calls this
    // on the other frames to keep draw-time work (lazy text layout) honest.
    void rehearse();

    // Sounds and rumble requested during the last update.
    const Feedback &feedback() const
    {
        return feedback_;
    }
    audio::SoundSet sound_set() const;
    gfx::Color accent() const;

    const Settings &settings() const
    {
        return settings_;
    }
    // True once after the settings changed: apply volumes, button swap, ...
    bool take_settings_changed();

    Telemetry &telemetry()
    {
        return telemetry_;
    }
    void set_version(std::string version)
    {
        version_ = std::move(version);
        context_.version = version_;
    }

    // ---- for the tour and tests ----
    std::size_t concept_count() const
    {
        return concepts_.size();
    }
    std::size_t current() const
    {
        return current_;
    }
    const Concept &concept_at(std::size_t index) const
    {
        return *concepts_[index];
    }
    // animate = false switches at once (and still restarts the entrance).
    void show(std::size_t index, bool animate);
    void set_info_open(bool open);
    // Drops the switcher banner at once (the tour keeps it out of pictures).
    void hide_banner()
    {
        banner_ = 0.0f;
        banner_show_.snap(0.0f);
    }
    bool transitioning() const
    {
        return transition_.running;
    }

    ~Shell();
    Shell(const Shell &) = delete;
    Shell &operator=(const Shell &) = delete;

  private:
    void record(Frame &frame, const Concept &design, float opacity, float dx, float scale);
    // A large picture read and decoded on a thread of its own (a 4K PNG
    // takes a while), then uploaded by compose().
    struct PictureLoad
    {
        std::thread worker;
        std::atomic<bool> busy{false};
        std::atomic<bool> cancel{false}; // another picture is wanted: the worker stops
        std::mutex mutex;
        std::string decoding; // what the worker was asked for (path and version)
        library::Image image; // what it made, waiting for compose()
        std::string made;     // ... and for which request
        bool ready = false;
        // A picture that moves (library/animation.hpp): the worker queues
        // its frames (and their delays) as it decodes them, compose() makes
        // each a texture and lets its pixels go.
        bool moving = false;
        bool moving_done = false; // the worker queued its last frame
        std::deque<std::pair<library::Image, int>> frames;
        std::size_t frame_bytes = 0; // the pixels waiting in frames
        // The main thread's side, no lock: the frames' textures so far and
        // which shows (advanced by update()).
        std::vector<std::uint32_t> textures;
        library::AnimationClock clock;
        std::string shown;                  // the request the texture holds
        std::vector<std::uint32_t> retired; // freed a frame later: lists may still use them
    };
    // may_move: an animated GIF or WebP plays (the player's own background);
    // otherwise its first frame is the picture.
    void load_picture(PictureLoad &load, const std::string &path, int version, int size,
                      bool may_move);
    static bool stream_frames(PictureLoad &load, const std::string &bytes,
                              const library::AnimationPlan &plan, const std::string &wanted,
                              bool bounded);
    // Uploads what was made; with let_go, frees the texture instead.
    void upload_picture(PictureLoad &load, gfx::Renderer &renderer, bool let_go,
                        std::uint32_t *texture, float *aspect);
    void load_backdrop();
    void upload_backdrop(gfx::Renderer &renderer);
    void draw_chrome();
    void save_settings();

    ui::Fonts fonts_;
    std::string data_root_;
    Settings settings_;
    Telemetry telemetry_;
    Context context_;
    std::vector<std::unique_ptr<Concept>> concepts_;
    std::size_t current_ = 0;
    std::size_t previous_ = 0;
    int direction_ = 1;
    tween::Timer transition_;
    Frame frames_[2];
    gfx::DrawList chrome_;
    Feedback feedback_;
    std::uint32_t glass_texture_ = 0;
    std::string version_;
    float banner_ = 0.0f; // seconds the switcher banner stays up
    tween::Spring banner_show_;
    bool info_open_ = false;
    tween::Spring info_show_;
    bool settings_changed_ = true; // applied once at start-up
    float save_delay_ = 0.0f;      // > 0: a save is pending
    bool welcomed_ = false;
    // The pictures Context::backdrop_path and wallpaper_path name.
    PictureLoad backdrop_;
    PictureLoad wallpaper_;
};

} // namespace hui::app
