// ORION Media Center - The shell's frame composition (the part that needs the renderer).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/shell.hpp"

#include "gfx/renderer.hpp"

#include <algorithm>

namespace hui::app
{

namespace
{
// How far a design slides while it is switched in or out.
constexpr float kSlide = 90.0f;
} // namespace

void Shell::upload_picture(PictureLoad &load, gfx::Renderer &renderer, bool let_go,
                           std::uint32_t *texture, float *aspect)
{
    // A moving picture's frames go up a few at a time, while its first
    // already shows: about 4 MB of pixels a frame keeps each upload short.
    constexpr std::size_t kUploadBytes = 4u << 20;
    for (const std::uint32_t old : load.retired)
        renderer.batch().delete_texture(old);
    load.retired.clear();
    const auto retire = [&]
    {
        if (load.textures.empty() && *texture != 0)
            load.retired.push_back(*texture);
        load.retired.insert(load.retired.end(), load.textures.begin(), load.textures.end());
        load.textures.clear();
        load.clock = library::AnimationClock();
        *texture = 0;
    };
    if (let_go)
    {
        // Let go: the texture goes, and the same picture asked for again
        // is read again.
        retire();
        if (!load.busy.load())
            load.decoding.clear();
        const std::lock_guard<std::mutex> lock(load.mutex);
        load.frames.clear();
        load.frame_bytes = 0;
        load.ready = false;
        return;
    }
    {
        const std::lock_guard<std::mutex> lock(load.mutex);
        if (load.ready)
        {
            load.ready = false;
            retire();
            if (!load.moving && !load.image.rgba.empty())
            {
                *texture = renderer.batch().create_texture(load.image.width, load.image.height,
                                                           load.image.rgba.data());
                *aspect =
                    static_cast<float>(load.image.width) / static_cast<float>(load.image.height);
            }
            load.shown = load.made;
            load.image = library::Image();
        }
        if (load.moving)
        {
            std::size_t sent = 0;
            while (!load.frames.empty() && sent < kUploadBytes)
            {
                const library::Image &frame = load.frames.front().first;
                load.textures.push_back(
                    renderer.batch().create_texture(frame.width, frame.height, frame.rgba.data()));
                load.clock.delays.push_back(load.frames.front().second);
                *aspect = static_cast<float>(frame.width) / static_cast<float>(frame.height);
                sent += frame.rgba.size();
                load.frame_bytes -= frame.rgba.size();
                load.frames.pop_front();
            }
            load.clock.complete = load.moving_done && load.frames.empty();
        }
    }
    if (!load.textures.empty())
        *texture = load.textures[std::min(load.clock.frame, load.textures.size() - 1)];
}

void Shell::upload_backdrop(gfx::Renderer &renderer)
{
    // A game's picture stays up until another replaces it; the player's own
    // goes when its path is emptied.
    upload_picture(backdrop_, renderer, false, &context_.backdrop_texture,
                   &context_.backdrop_aspect);
    context_.backdrop_shown = backdrop_.shown.substr(0, backdrop_.shown.find('\n'));
    upload_picture(wallpaper_, renderer, context_.wallpaper_path.empty(),
                   &context_.wallpaper_texture, &context_.wallpaper_aspect);
}

void Shell::compose(gfx::Renderer &renderer)
{
    upload_backdrop(renderer);
    renderer.begin();
    if (concepts_.empty())
        return;
    Frame &now = frames_[0];
    if (transition_.running)
    {
        // The old design fades and slides away while the new one arrives
        // from the other side; their backdrops cross-fade underneath.
        const float t = transition_.progress();
        const float out = 1.0f - tween::smoothstep(t * 1.7f);
        const float in = tween::smoothstep((t - 0.2f) / 0.8f);
        const float slide = settings_.reduced_motion ? 0.0f : kSlide;
        const float move = tween::quint_out(t);
        const float direction = static_cast<float>(direction_);
        Frame &old = frames_[1];
        record(old, *concepts_[previous_], out, -direction * slide * move,
               settings_.reduced_motion ? 1.0f : 1.0f - 0.03f * move);
        record(now, *concepts_[current_], in, direction * slide * (1.0f - move),
               settings_.reduced_motion ? 1.0f : 0.97f + 0.03f * move);
        renderer.backdrop(old.backdrop);
        renderer.backdrop(now.backdrop, tween::smoothstep(t));
        renderer.draw(old.scene);
        renderer.draw(old.overlay);
        renderer.draw(now.scene);
        if (now.glass)
            renderer.glass();
        renderer.draw(now.overlay);
        renderer.backdrop(old.post, 1.0f - t);
        renderer.backdrop(now.post, t);
    }
    else
    {
        record(now, *concepts_[current_], 1.0f, 0.0f, 1.0f);
        renderer.backdrop(now.backdrop);
        renderer.draw(now.scene);
        if (now.glass)
            renderer.glass();
        renderer.draw(now.overlay);
        renderer.backdrop(now.post);
    }

    chrome_.clear();
    draw_chrome();
    if (info_show_.value > 0.01f)
        renderer.glass(); // the info panel frosts whatever the design drew
    renderer.draw(chrome_);
}

} // namespace hui::app
