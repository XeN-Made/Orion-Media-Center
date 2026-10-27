// ORION Media Center - Title music: a game's own song (ATRAC9 or Ogg) while its tile is focused.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/title_music.hpp"

#include "audio/music.hpp"
#include "core/save_file.hpp"
#include "third_party/libatrac9/libatrac9.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace hui::audio
{

namespace
{

// A title's song is brought to this loudness (RMS of the samples): the
// playlist's tracks are mastered at about -18 LUFS and play at 0.4, about
// 0.05; a little over that.
constexpr float kTitleLoudness = 0.09f;
constexpr float kLeastGain = 0.25f;
constexpr float kMostGain = 4.0f;
constexpr float kMostPeak = 0.95f;
constexpr int kMeasureFrames = 48000 * 3; // the first three seconds
constexpr float kFadeIn = 0.6f;
constexpr float kFadeOut = 0.25f;
constexpr std::size_t kAhead = 12288; // frames kept buffered (~0.25 s): a switch is quick
constexpr int kChunk = 1024;
constexpr std::size_t kMaxFile = 16u << 20;

std::uint32_t u32(std::string_view bytes, std::size_t at)
{
    return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at])) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 1])) << 8 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 2])) << 16 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 3])) << 24;
}

std::uint16_t u16(std::string_view bytes, std::size_t at)
{
    return static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[at]) |
                                      static_cast<unsigned char>(bytes[at + 1]) << 8);
}

} // namespace

bool parse_at9(std::string_view file, At9Info *info, std::string *error)
{
    *info = At9Info();
    if (file.size() < 12 || file.compare(0, 4, "RIFF") != 0 || file.compare(8, 4, "WAVE") != 0)
    {
        *error = "not a RIFF/WAVE file";
        return false;
    }
    bool format = false;
    std::size_t at = 12;
    while (at + 8 <= file.size())
    {
        const std::string_view id = file.substr(at, 4);
        const std::size_t size = u32(file, at + 4);
        const std::size_t body = at + 8;
        if (size > file.size() - body)
            break;
        if (id == "fmt ")
        {
            // WAVE_FORMAT_EXTENSIBLE: 40 bytes, then ATRAC9's version (4)
            // and its config (4).
            if (size < 52 || u16(file, body) != 0xFFFE)
            {
                *error = "not ATRAC9";
                return false;
            }
            info->channels = u16(file, body + 2);
            info->sample_rate = static_cast<int>(u32(file, body + 4));
            info->block_align = u16(file, body + 12);
            std::memcpy(info->config, file.data() + body + 44, 4);
            format = true;
        }
        else if (id == "data")
        {
            info->data = body;
            info->data_size = size;
        }
        at = body + size + (size & 1);
    }
    if (!format || info->data_size == 0 || info->block_align <= 0 || info->channels < 1 ||
        info->channels > 2)
    {
        *error = format ? "no ATRAC9 audio in it" : "no format chunk";
        return false;
    }
    return true;
}

At9Track::~At9Track()
{
    if (decoder_ != nullptr)
        Atrac9ReleaseHandle(decoder_);
}

std::string At9Track::open(std::string data)
{
    data_ = std::move(data);
    std::string error;
    if (!parse_at9(data_, &info_, &error))
        return error;
    if (info_.sample_rate != kSampleRate)
        return "ATRAC9 at " + std::to_string(info_.sample_rate) + " Hz (48000 only)";
    decoder_ = Atrac9GetHandle();
    if (decoder_ == nullptr || Atrac9InitDecoder(decoder_, info_.config) != 0)
        return "the ATRAC9 decoder refused its config";
    Atrac9CodecInfo codec = {};
    if (Atrac9GetCodecInfo(decoder_, &codec) != 0 || codec.channels != info_.channels ||
        codec.superframeSize != info_.block_align)
        return "the ATRAC9 config does not match the file";
    frames_ = codec.framesInSuperframe;
    frame_samples_ = codec.frameSamples;
    samples_ = frames_ * frame_samples_;
    pcm_.assign(static_cast<std::size_t>(samples_ * info_.channels), 0);
    return {};
}

bool At9Track::next_superframe()
{
    if (position_ + static_cast<std::size_t>(info_.block_align) > info_.data_size)
        position_ = 0; // the song loops from its start
    // A superframe is several frames; each call decodes one and says how
    // many bytes it took. The superframe's size then steps to the next one.
    const auto *bytes =
        reinterpret_cast<const unsigned char *>(data_.data() + info_.data + position_);
    std::size_t offset = 0;
    for (int frame = 0; frame < frames_; ++frame)
    {
        int used = 0;
        if (offset >= static_cast<std::size_t>(info_.block_align) ||
            Atrac9Decode(decoder_, bytes + offset,
                         pcm_.data() +
                             static_cast<std::size_t>(frame * frame_samples_ * info_.channels),
                         &used) != 0 ||
            used <= 0)
            return false;
        offset += static_cast<std::size_t>(used);
    }
    position_ += static_cast<std::size_t>(info_.block_align);
    pcm_frames_ = static_cast<std::size_t>(samples_);
    pcm_position_ = 0;
    return true;
}

int At9Track::decode(float *out, int frames)
{
    if (decoder_ == nullptr)
        return 0;
    int done = 0;
    while (done < frames)
    {
        if (pcm_position_ >= pcm_frames_ && !next_superframe())
            break;
        const std::size_t take =
            std::min(pcm_frames_ - pcm_position_, static_cast<std::size_t>(frames - done));
        for (std::size_t i = 0; i < take; ++i)
        {
            const std::size_t frame = pcm_position_ + i;
            const float left = static_cast<float>(pcm_[frame * info_.channels]) / 32768.0f;
            const float right =
                info_.channels == 2 ? static_cast<float>(pcm_[frame * 2 + 1]) / 32768.0f : left;
            out[(done + static_cast<int>(i)) * 2] = left;
            out[(done + static_cast<int>(i)) * 2 + 1] = right;
        }
        pcm_position_ += take;
        done += static_cast<int>(take);
    }
    return done;
}

TitleMusic::TitleMusic() : buffer_(static_cast<std::size_t>(kChunk) * 2)
{
}

TitleMusic::~TitleMusic() = default;

void TitleMusic::init(Mixer &mixer, MusicPlayer &playlist)
{
    mixer_ = &mixer;
    playlist_ = &playlist;
    mixer.attach_stream(1, &ring_);
    mixer.set_stream_gain(1, 0.0f, 0.0f);
}

float title_loudness_gain(const float *samples, std::size_t count)
{
    double sum = 0.0;
    float peak = 0.0f;
    for (std::size_t i = 0; i < count; ++i)
    {
        sum += static_cast<double>(samples[i]) * samples[i];
        peak = std::max(peak, std::fabs(samples[i]));
    }
    const float rms =
        count > 0 ? static_cast<float>(std::sqrt(sum / static_cast<double>(count))) : 0.0f;
    if (rms < 1e-4f)
        return 1.0f; // silence (or a quiet intro): nothing to go by
    float gain = std::clamp(kTitleLoudness / rms, kLeastGain, kMostGain);
    if (peak > 0.0f)
        gain = std::min(gain, std::max(kLeastGain, kMostPeak / peak));
    return gain;
}

float TitleMusic::effective_gain() const
{
    const float gain = gain_ * volume_;
    // Turned up, still held under clipping by the opening's peak.
    return peak_ > 0.0f ? std::min(gain, std::max(gain_, kMostPeak / peak_)) : gain;
}

void TitleMusic::set_volume(float scale)
{
    volume_ = std::max(scale, 0.0f);
    if (mixer_ != nullptr && switching_ <= 0.0f && (at9_ || ogg_))
        mixer_->set_stream_gain(1, effective_gain(), 0.2f);
}

void TitleMusic::play(const std::string &path)
{
    if (mixer_ == nullptr || path == path_)
        return;
    path_ = path;
    // What is buffered of the last song plays out under the fade.
    mixer_->set_stream_gain(1, 0.0f, kFadeOut);
    switching_ = kFadeOut;
    if (path.empty() && playlist_ != nullptr)
        playlist_->set_lowered(false);
}

bool TitleMusic::open(const std::string &path)
{
    at9_.reset();
    ogg_.reset();
    opened_ = path;
    std::string data;
    if (!save::read_file(path, &data, kMaxFile) || data.size() < 12)
    {
        error_ = "cannot read " + path;
        return false;
    }
    // Its opening decoded once to measure how loud it is, then the song is
    // opened again from the start to play.
    const bool ogg = data.compare(0, 4, "OggS") == 0;
    {
        std::vector<float> opening(static_cast<std::size_t>(kMeasureFrames) * 2);
        int got = 0;
        if (ogg)
        {
            MusicTrack probe;
            if (probe.open(data).empty())
                got = probe.decode(opening.data(), kMeasureFrames);
        }
        else
        {
            At9Track probe;
            if (probe.open(data).empty())
                got = probe.decode(opening.data(), kMeasureFrames);
        }
        const std::size_t samples = static_cast<std::size_t>(std::max(got, 0)) * 2;
        gain_ = title_loudness_gain(opening.data(), samples);
        peak_ = 0.0f;
        for (std::size_t i = 0; i < samples; ++i)
            peak_ = std::max(peak_, std::fabs(opening[i]));
    }
    if (ogg)
    {
        auto track = std::make_unique<MusicTrack>();
        error_ = track->open(std::move(data));
        if (error_.empty())
            ogg_ = std::move(track);
    }
    else
    {
        auto track = std::make_unique<At9Track>();
        error_ = track->open(std::move(data));
        if (error_.empty())
            at9_ = std::move(track);
    }
    return error_.empty();
}

int TitleMusic::decode(float *out, int frames)
{
    if (at9_)
        return at9_->decode(out, frames);
    if (ogg_)
        return ogg_->decode(out, frames);
    return 0;
}

void TitleMusic::pump(float dt)
{
    if (mixer_ == nullptr)
        return;
    if (switching_ > 0.0f)
    {
        switching_ -= dt;
        if (switching_ > 0.0f)
            return;
        // Faded out: the next song (if any) starts, the playlist under it.
        if (path_.empty() || !open(path_))
        {
            at9_.reset();
            ogg_.reset();
            opened_.clear();
            if (playlist_ != nullptr)
                playlist_->set_lowered(false);
            return;
        }
        if (playlist_ != nullptr)
            playlist_->set_lowered(true);
        mixer_->set_stream_gain(1, effective_gain(), kFadeIn);
    }
    if (!at9_ && !ogg_)
        return;
    while (ring_.available() < kAhead)
    {
        const int room = static_cast<int>(std::min<std::size_t>(kChunk, ring_.space()));
        if (room <= 0)
            break;
        const int got = decode(buffer_.data(), room);
        if (got <= 0)
        {
            at9_.reset();
            ogg_.reset();
            break;
        }
        ring_.write(buffer_.data(), static_cast<std::size_t>(got));
    }
}

} // namespace hui::audio
