// ORION Media Center - Weather over the player's background picture: snow, rain, fireflies.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/draw_list.hpp"

#include <cstdint>
#include <vector>

namespace hui::orion
{

// A few hundred particles drawn with the kit's own shapes over the player's
// picture, the way the default sky has its stars and clouds move. They live
// in the 1920 x 1080 virtual canvas: the renderer scales the shapes, so 4K
// costs the same few hundred quads (one batch) as 1080p.
//
//   orion::Weather weather;
//   weather.set_kind(orion::Weather::Kind::snow);
//   weather.update(dt, settings.reduced_motion);   // in update()
//   weather.draw(frame.scene);                     // in draw(), over the picture
class Weather
{
  public:
    // In the order of kPictureEffects (orion/prefs.hpp).
    enum class Kind : std::uint8_t
    {
        none,
        snow,
        rain,
        fireflies,
    };

    // A new kind scatters its particles over the whole screen at once: the
    // weather is there when the picture shows, not falling in from the top.
    void set_kind(Kind kind);
    Kind kind() const
    {
        return kind_;
    }
    // still (reduced motion, or the sky's motion off): the particles stay on
    // screen but move at a sixth of their speed, with no sway or flicker.
    void update(float dt, bool still);
    void draw(gfx::DrawList &list) const;

    // How many particles each kind keeps (for tests and the cost in
    // docs/PERFORMANCE.md): each is one or two quads, a snow crystal five.
    static int count(Kind kind);

  private:
    struct Particle
    {
        float x = 0.0f;
        float y = 0.0f;
        float depth = 0.0f; // 0 far (small, slow, faint) .. 1 near
        float phase = 0.0f; // where it is in its sway, pulse or wander
    };
    float random();
    void scatter(Particle &particle, bool anywhere);

    Kind kind_ = Kind::none;
    std::vector<Particle> particles_;
    float clock_ = 0.0f;
    bool still_ = false;
    std::uint32_t seed_ = 0x9e3779b9u;
};

} // namespace hui::orion
