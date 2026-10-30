// ORION Media Center - Weather over the player's background picture: snow, rain, fireflies.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/weather.hpp"

#include <algorithm>
#include <cmath>

namespace hui::orion
{

namespace
{

using gfx::Color;

constexpr float kWidth = gfx::kVirtualWidth;
constexpr float kHeight = gfx::kVirtualHeight;
// Particles leave by an edge and come back by the opposite one this far
// outside the screen, so none pops in or out where it can be seen.
constexpr float kMargin = 80.0f;
// Reduced motion keeps the weather on screen but nearly at rest.
constexpr float kStillSpeed = 1.0f / 6.0f;

// Rain falls slanted, as if a light wind came from the left.
constexpr float kRainSlant = 0.16f;

// Snow in three bands of depth, as a camera sees it: specks far away,
// crystals in the middle that turn as they fall, and flakes so near they
// are out of focus, large soft discs.
constexpr float kCrystalFrom = 0.55f;
constexpr float kBlurFrom = 0.88f;

} // namespace

int Weather::count(Kind kind)
{
    switch (kind)
    {
    case Kind::snow:
        return 200;
    case Kind::rain:
        return 200;
    case Kind::fireflies:
        return 42;
    case Kind::none:
        break;
    }
    return 0;
}

float Weather::random()
{
    // xorshift32: the same weather every run, which keeps PC pictures and
    // tests stable; nobody can tell one snowfall from another.
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return static_cast<float>(seed_ >> 8) / static_cast<float>(1u << 24);
}

void Weather::scatter(Particle &particle, bool anywhere)
{
    particle.depth = random();
    particle.phase = random() * 6.2831853f;
    particle.x = -kMargin + random() * (kWidth + 2.0f * kMargin);
    if (anywhere || kind_ == Kind::fireflies)
        particle.y = -kMargin + random() * (kHeight + 2.0f * kMargin);
    else
        particle.y = -kMargin * random(); // back in at the top
}

void Weather::set_kind(Kind kind)
{
    if (kind == kind_)
        return;
    kind_ = kind;
    particles_.assign(static_cast<std::size_t>(count(kind)), Particle{});
    for (Particle &particle : particles_)
        scatter(particle, true);
}

void Weather::update(float dt, bool still)
{
    still_ = still;
    if (kind_ == Kind::none)
        return;
    dt = std::clamp(dt, 0.0f, 0.1f) * (still ? kStillSpeed : 1.0f);
    clock_ += dt;
    for (Particle &p : particles_)
    {
        const float d = p.depth;
        switch (kind_)
        {
        case Kind::snow:
        {
            // A wind that rises and dies away carries every flake the same
            // way, nearer ones more; each flutters and slows on its own.
            const float wind = std::sin(clock_ * 0.07f) * 22.0f + std::sin(clock_ * 0.23f) * 9.0f;
            const float flutter = std::sin(clock_ * (1.1f + 0.6f * d) + p.phase * 3.0f);
            p.y += (30.0f + 96.0f * d) * (0.85f + 0.25f * flutter) * dt;
            p.x += (wind * (0.4f + 0.6f * d) + 6.0f + 10.0f * flutter * d) * dt;
            break;
        }
        case Kind::rain:
        {
            const float fall = (1100.0f + 900.0f * d) * dt;
            p.y += fall;
            p.x += fall * kRainSlant;
            break;
        }
        case Kind::fireflies:
            // A slow wander: two sines per axis at speeds of their own.
            p.x += (std::sin(clock_ * 0.31f + p.phase) * 18.0f +
                    std::sin(clock_ * 0.77f + p.phase * 2.1f) * 9.0f) *
                   dt;
            p.y += (std::cos(clock_ * 0.27f + p.phase * 1.3f) * 14.0f +
                    std::sin(clock_ * 0.63f + p.phase) * 7.0f - 3.0f) *
                   dt;
            break;
        case Kind::none:
            break;
        }
        if (p.y > kHeight + kMargin || p.x > kWidth + kMargin)
        {
            if (kind_ == Kind::fireflies)
            {
                p.x = std::fmod(p.x + kMargin, kWidth + 2.0f * kMargin) - kMargin;
                p.y = std::fmod(p.y + kMargin, kHeight + 2.0f * kMargin) - kMargin;
            }
            else
                scatter(p, false);
        }
        else if (p.x < -kMargin)
            p.x += kWidth + 2.0f * kMargin;
        else if (p.y < -kMargin)
            p.y += kHeight + 2.0f * kMargin;
    }
}

void Weather::draw(gfx::DrawList &list) const
{
    const float sway_amount = still_ ? 0.0f : 1.0f;
    for (const Particle &p : particles_)
    {
        const float d = p.depth;
        switch (kind_)
        {
        case Kind::snow:
        {
            const float x =
                p.x + std::sin(clock_ * (0.6f + 0.5f * d) + p.phase) * 18.0f * d * sway_amount;
            if (d < kCrystalFrom)
            {
                // Far: a speck, fainter the further.
                list.circle(x, p.y, 0.9f + 1.9f * d, Color::rgb(0xeef4ff, 0.25f + 0.5f * d));
                break;
            }
            if (d >= kBlurFrom)
            {
                // Out of focus: a wide soft disc with a brighter middle.
                const float t = (d - kBlurFrom) / (1.0f - kBlurFrom);
                const float soft = 9.0f + 10.0f * t;
                list.shadow({x - soft, p.y - soft, soft * 2.0f, soft * 2.0f}, soft, soft * 0.9f,
                            Color::rgb(0xffffff, 0.16f + 0.08f * t));
                list.circle(x, p.y, soft * 0.35f, Color::rgb(0xffffff, 0.10f));
                break;
            }
            // In focus: a six-armed crystal that turns as it falls, catching
            // the light as it turns (brighter, then dimmer).
            const float t = (d - kCrystalFrom) / (kBlurFrom - kCrystalFrom);
            const float arm = 4.2f + 6.0f * t;
            const float turn =
                p.phase + clock_ * (0.5f + 0.7f * p.phase / 6.2831853f) * sway_amount;
            const float glint = still_ ? 0.8f : 0.7f + 0.3f * std::sin(turn * 2.0f);
            const float alpha = (0.55f + 0.35f * t) * glint;
            const Color ink = Color::rgb(0xf6faff, alpha);
            list.shadow({x - arm * 1.6f, p.y - arm * 1.6f, arm * 3.2f, arm * 3.2f}, arm * 1.6f, arm,
                        Color::rgb(0xdfeaff, alpha * 0.25f));
            for (int i = 0; i < 3; ++i)
            {
                const float angle = turn + static_cast<float>(i) * 1.0471976f; // 60 degrees
                const float dx = std::cos(angle) * arm;
                const float dy = std::sin(angle) * arm;
                list.line(x - dx, p.y - dy, x + dx, p.y + dy, 1.1f + 0.6f * t, ink);
            }
            list.circle(x, p.y, 0.9f + 0.6f * t, ink);
            break;
        }
        case Kind::rain:
        {
            const float length = 26.0f + 46.0f * d;
            const float alpha = 0.14f + 0.3f * d;
            list.line(p.x, p.y, p.x - length * kRainSlant, p.y - length, 1.0f + 1.4f * d,
                      Color::rgb(0xcfe0f4, alpha));
            break;
        }
        case Kind::fireflies:
        {
            const float pulse =
                still_ ? 0.7f : 0.5f + 0.5f * std::sin(clock_ * (1.1f + 0.9f * d) + p.phase * 3.0f);
            const float glow = 0.25f + 0.75f * pulse * pulse;
            const float halo = 14.0f + 16.0f * d;
            list.shadow({p.x - halo, p.y - halo, halo * 2.0f, halo * 2.0f}, halo, halo,
                        Color::rgb(0xd9f26a, 0.32f * glow));
            list.circle(p.x, p.y, 1.8f + 1.6f * d, Color::rgb(0xfff4b0, 0.4f + 0.6f * glow));
            break;
        }
        case Kind::none:
            break;
        }
    }
}

} // namespace hui::orion
