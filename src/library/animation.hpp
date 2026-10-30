// ORION Media Center - Pictures that move: animated GIF and WebP, decoded within a memory cap.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "library/icon.hpp"

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace hui::library
{

// A moving background is kept as one texture per frame, every frame decoded
// once and shrunk to fit these caps. One 3840 x 2160 frame is 33 MB, so a
// 4K GIF of any length could never be held; 64 MB of frames is 17 frames at
// 1280 x 720, 72 at 640 x 360, which is what GIFs usually are anyway. The
// frames are uploaded as they are decoded and their pixels let go, so the
// app's fixed heap only ever holds a few of them.
constexpr int kAnimationSide = 1280;               // longest side of a frame, at most
constexpr std::size_t kAnimationBytes = 64u << 20; // every frame's pixels together
constexpr int kAnimationFrames = 480;              // whatever their size
constexpr int kAnimationSmallest = 320;            // shrunk below this: refused instead
constexpr int kAnimationCanvas = 1920 * 1080;      // pixels of the file's own frame
constexpr std::size_t kAnimationFile = 32u << 20;  // the file itself

struct AnimationInfo
{
    int width = 0;
    int height = 0;
    int frames = 0;
};

// Whether the file is a GIF of more than one frame or a WebP marked as
// animated, and its size and frame count. Read from the file's structure
// without decoding anything; anything else (a still picture, a broken file)
// is false.
bool probe_animation(std::string_view bytes, AnimationInfo *out);

// Why a moving picture cannot be a background.
enum class AnimationRefusal
{
    none,
    canvas, // its frames are larger than kAnimationCanvas
    length, // too many frames to keep at a size worth showing
};

struct AnimationPlan
{
    int factor = 1; // every frame is box-filtered down by this
    int width = 0;
    int height = 0;
    std::size_t bytes = 0; // all frames together, as textures
};

// The smallest shrink that brings the frames within the caps above.
AnimationRefusal plan_animation(const AnimationInfo &info, AnimationPlan *plan);

// Gives the frames one at a time, whole (each drawn over the ones before
// as the file says) and shrunk by the plan's factor. GIF everywhere; WebP
// where the build has libwebp's demuxer (the console's).
class AnimationDecoder
{
  public:
    AnimationDecoder();
    ~AnimationDecoder();
    AnimationDecoder(const AnimationDecoder &) = delete;
    AnimationDecoder &operator=(const AnimationDecoder &) = delete;

    // The bytes must outlive the decoder.
    bool open(std::string_view bytes, int factor);
    // The next frame and how long it shows. A delay of 10 ms or less shows
    // for 100 ms, as browsers do: GIFs written with 0 ("as fast as you
    // can") were made to be seen that way. False at the end or on a broken
    // frame.
    bool next(Image *frame, int *delay_ms);

  private:
    struct State;
    std::unique_ptr<State> state_;
};

// Which frame of a looping animation shows. Frames still being decoded:
// it waits on the last one there instead of looping back early.
struct AnimationClock
{
    std::vector<int> delays; // milliseconds, one per frame there so far
    bool complete = false;   // every frame is there: loop
    std::size_t frame = 0;
    float into = 0.0f; // seconds into the frame

    void advance(float dt);
};

} // namespace hui::library
