// ORION Media Center - Pictures that move: animated GIF and WebP, decoded within a memory cap.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "library/animation.hpp"

#include "third_party/stb/image.h"

#include <algorithm>
#include <cstdint>

// Animated WebP needs libwebp's demuxer, which the console's build links
// (PacBrew's libwebpdemux); elsewhere such a file is refused.
#if defined(__PROSPERO__)
#include <webp/demux.h>
#define HUI_WEBP_ANIMATION 1
#endif

namespace hui::library
{

namespace
{

std::uint32_t le16(std::string_view bytes, std::size_t at)
{
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[at])) |
           static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[at + 1])) << 8;
}

std::uint32_t le24(std::string_view bytes, std::size_t at)
{
    return le16(bytes, at) | static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[at + 2]))
                                 << 16;
}

std::uint32_t le32(std::string_view bytes, std::size_t at)
{
    return le16(bytes, at) | le16(bytes, at + 2) << 16;
}

bool is_gif(std::string_view bytes)
{
    return bytes.size() >= 13 &&
           (bytes.compare(0, 6, "GIF87a") == 0 || bytes.compare(0, 6, "GIF89a") == 0);
}

bool is_webp(std::string_view bytes)
{
    return bytes.size() >= 16 && bytes.compare(0, 4, "RIFF") == 0 &&
           bytes.compare(8, 4, "WEBP") == 0;
}

// Skips a run of GIF data sub-blocks (a length byte, that many bytes, until
// a zero length). False if the file ends first.
bool skip_sub_blocks(std::string_view bytes, std::size_t *at)
{
    while (*at < bytes.size())
    {
        const std::size_t length = static_cast<std::uint8_t>(bytes[*at]);
        *at += 1;
        if (length == 0)
            return true;
        *at += length;
    }
    return false;
}

std::size_t colour_table(unsigned flags)
{
    return (flags & 0x80) != 0 ? 3u * (2u << (flags & 7)) : 0u;
}

bool probe_gif(std::string_view bytes, AnimationInfo *out)
{
    out->width = static_cast<int>(le16(bytes, 6));
    out->height = static_cast<int>(le16(bytes, 8));
    std::size_t at = 13 + colour_table(static_cast<std::uint8_t>(bytes[10]));
    while (at < bytes.size())
    {
        const auto tag = static_cast<std::uint8_t>(bytes[at++]);
        if (tag == 0x21) // an extension: its label, then sub-blocks
        {
            ++at;
            if (!skip_sub_blocks(bytes, &at))
                break;
        }
        else if (tag == 0x2c) // a frame: where it goes, its colours, its pixels
        {
            if (at + 10 > bytes.size())
                break;
            ++out->frames;
            at += 9 + colour_table(static_cast<std::uint8_t>(bytes[at + 8]));
            ++at; // the LZW code size
            if (!skip_sub_blocks(bytes, &at))
                break;
        }
        else // 0x3b, the end; anything else is a broken file, read so far
            break;
    }
    return out->frames > 1 && out->width > 0 && out->height > 0;
}

bool probe_webp(std::string_view bytes, AnimationInfo *out)
{
    bool animated = false;
    std::size_t at = 12;
    while (at + 8 <= bytes.size())
    {
        const std::string_view kind = bytes.substr(at, 4);
        const std::size_t size = le32(bytes, at + 4);
        const std::size_t payload = at + 8;
        if (kind == "VP8X" && payload + 10 <= bytes.size())
        {
            animated = (static_cast<std::uint8_t>(bytes[payload]) & 0x02) != 0;
            out->width = static_cast<int>(le24(bytes, payload + 4) + 1);
            out->height = static_cast<int>(le24(bytes, payload + 7) + 1);
        }
        else if (kind == "ANMF")
            ++out->frames;
        at = payload + size + (size & 1);
        if (at < payload) // a size that wraps around: a broken file
            break;
    }
    // Even one frame: libwebp's plain decoder refuses an animated file.
    return animated && out->frames > 0;
}

} // namespace

bool probe_animation(std::string_view bytes, AnimationInfo *out)
{
    *out = AnimationInfo();
    bool moving = false;
    if (is_gif(bytes))
        moving = probe_gif(bytes, out);
    else if (is_webp(bytes))
        moving = probe_webp(bytes, out);
    if (!moving)
        *out = AnimationInfo();
    return moving;
}

AnimationRefusal plan_animation(const AnimationInfo &info, AnimationPlan *plan)
{
    *plan = AnimationPlan();
    if (info.width < 1 || info.height < 1 ||
        static_cast<long long>(info.width) * info.height > kAnimationCanvas)
        return AnimationRefusal::canvas;
    if (info.frames < 1 || info.frames > kAnimationFrames)
        return AnimationRefusal::length;
    for (int factor = 1;; ++factor)
    {
        const int width = info.width / factor;
        const int height = info.height / factor;
        const int longest = std::max(width, height);
        if (factor > 1 && longest < kAnimationSmallest)
            return AnimationRefusal::length;
        if (width < 1 || height < 1)
            return AnimationRefusal::length;
        const std::size_t bytes = static_cast<std::size_t>(width) *
                                  static_cast<std::size_t>(height) * 4u *
                                  static_cast<std::size_t>(info.frames);
        if (longest <= kAnimationSide && bytes <= kAnimationBytes)
        {
            plan->factor = factor;
            plan->width = width;
            plan->height = height;
            plan->bytes = bytes;
            return AnimationRefusal::none;
        }
    }
}

struct AnimationDecoder::State
{
    int factor = 1;
    hui_gif *gif = nullptr;
#if defined(HUI_WEBP_ANIMATION)
    WebPAnimDecoder *webp = nullptr;
    int webp_width = 0;
    int webp_height = 0;
    int webp_time = 0; // when the frame given last ended, in ms
#endif

    ~State()
    {
        hui_gif_close(gif);
#if defined(HUI_WEBP_ANIMATION)
        if (webp != nullptr)
            WebPAnimDecoderDelete(webp);
#endif
    }
};

AnimationDecoder::AnimationDecoder() = default;
AnimationDecoder::~AnimationDecoder() = default;

bool AnimationDecoder::open(std::string_view bytes, int factor)
{
    state_.reset();
    if (bytes.empty() || bytes.size() > kAnimationFile)
        return false;
    auto state = std::make_unique<State>();
    state->factor = std::max(factor, 1);
    if (is_gif(bytes))
        state->gif = hui_gif_open(reinterpret_cast<const unsigned char *>(bytes.data()),
                                  static_cast<int>(bytes.size()));
#if defined(HUI_WEBP_ANIMATION)
    else if (is_webp(bytes))
    {
        WebPAnimDecoderOptions options;
        if (!WebPAnimDecoderOptionsInit(&options))
            return false;
        options.color_mode = MODE_RGBA;
        options.use_threads = 0; // it already runs off the main thread
        WebPData data = {reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()};
        state->webp = WebPAnimDecoderNew(&data, &options);
        WebPAnimInfo info;
        if (state->webp == nullptr || !WebPAnimDecoderGetInfo(state->webp, &info))
            return false;
        state->webp_width = static_cast<int>(info.canvas_width);
        state->webp_height = static_cast<int>(info.canvas_height);
    }
    if (state->gif == nullptr && state->webp == nullptr)
        return false;
#else
    if (state->gif == nullptr)
        return false;
#endif
    state_ = std::move(state);
    return true;
}

bool AnimationDecoder::next(Image *frame, int *delay_ms)
{
    *frame = Image();
    *delay_ms = 0;
    if (!state_)
        return false;
    const std::uint8_t *pixels = nullptr;
    int width = 0;
    int height = 0;
    int delay = 0;
    if (state_->gif != nullptr)
        pixels = hui_gif_next(state_->gif, &width, &height, &delay);
#if defined(HUI_WEBP_ANIMATION)
    else if (state_->webp != nullptr && WebPAnimDecoderHasMoreFrames(state_->webp))
    {
        std::uint8_t *canvas = nullptr;
        int ends = 0;
        if (WebPAnimDecoderGetNext(state_->webp, &canvas, &ends))
        {
            pixels = canvas;
            width = state_->webp_width;
            height = state_->webp_height;
            delay = ends - state_->webp_time;
            state_->webp_time = ends;
        }
    }
#endif
    if (pixels == nullptr)
        return false;
    shrink_rgba(pixels, width, height, state_->factor, frame);
    *delay_ms = delay <= 10 ? 100 : delay;
    return !frame->rgba.empty();
}

void AnimationClock::advance(float dt)
{
    if (delays.empty())
        return;
    frame = std::min(frame, delays.size() - 1);
    into += std::max(dt, 0.0f);
    // A frame skipped is better than the animation falling behind the
    // clock; never more than one lap in a frame.
    for (std::size_t step = 0; step < delays.size(); ++step)
    {
        const float lasts = static_cast<float>(delays[frame]) / 1000.0f;
        if (into < lasts)
            return;
        if (frame + 1 >= delays.size() && !complete)
        {
            into = lasts; // wait on the last frame there for the next one
            return;
        }
        into -= lasts;
        frame = (frame + 1) % delays.size();
    }
    into = 0.0f;
}

} // namespace hui::library
