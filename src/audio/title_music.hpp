// ORION Media Center - Title music: a game's own song (ATRAC9 or Ogg) while its tile is focused.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "audio/mixer.hpp"
#include "audio/stream_ring.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hui::audio
{

class MusicPlayer;
class MusicTrack;

// What the home screen plays for a focused title is its sce_sys/snd0.at9:
// ATRAC9 in a RIFF/WAVE file (format 0xFFFE, the decoder's four config
// bytes after the extensible format's fields). LibAtrac9 decodes it.
struct At9Info
{
    int channels = 0;
    int sample_rate = 0;
    int block_align = 0;       // one superframe, in bytes
    std::size_t data = 0;      // where the superframes start in the file
    std::size_t data_size = 0; // and how many bytes of them there are
    unsigned char config[4] = {};
};

// Reads an ATRAC9 file's header. False with *error when it is not one.
bool parse_at9(std::string_view file, At9Info *info, std::string *error);

// One ATRAC9 song decoded from memory, looping, as stereo float at 48 kHz.
class At9Track
{
  public:
    At9Track() = default;
    ~At9Track();
    At9Track(const At9Track &) = delete;
    At9Track &operator=(const At9Track &) = delete;

    // Takes the whole file; returns an error, or "" on success. 48 kHz only.
    std::string open(std::string data);
    // Decodes stereo frames into out (mono is duplicated); fewer only when
    // the stream is broken.
    int decode(float *out, int frames);

  private:
    bool next_superframe();

    std::string data_;
    At9Info info_;
    void *decoder_ = nullptr;
    std::size_t position_ = 0;     // the next superframe, from info_.data
    int frames_ = 0;               // frames in a superframe
    int frame_samples_ = 0;        // samples per channel in a frame
    int samples_ = 0;              // per channel in one superframe
    std::vector<short> pcm_;       // one superframe, interleaved
    std::size_t pcm_frames_ = 0;   // frames of it decoded
    std::size_t pcm_position_ = 0; // frames of it already given out
};

// The gain that brings a song's opening (interleaved stereo samples) to the
// loudness a title's song plays at, a little over ORION's playlist. The
// home screen's previews are mastered far quieter than ORION's tracks, and
// differently from game to game: one fixed gain left most of them faint.
// Never so much that the loudest sample would clip.
float title_loudness_gain(const float *samples, std::size_t count);

// Plays one title's music on the mixer's second music deck, the playlist
// lowered under it: play() fades the last one out and the new one in; an
// empty path ends it and the playlist comes back. Decoding runs on the
// caller's thread (pump, once per frame), a little ahead of the audio thread.
class TitleMusic
{
  public:
    TitleMusic();
    ~TitleMusic();
    TitleMusic(const TitleMusic &) = delete;
    TitleMusic &operator=(const TitleMusic &) = delete;

    // Attaches the deck (before the audio thread starts).
    void init(Mixer &mixer, MusicPlayer &playlist);
    // A file to play (.at9 or .ogg), or "" for none. The same path again
    // changes nothing.
    void play(const std::string &path);
    // The player's volume for titles' songs, over their measured loudness
    // (1: as measured). Applies at once to a song playing.
    void set_volume(float scale);
    void pump(float dt);
    const std::string &playing() const
    {
        return path_;
    }
    // Why the last file could not be played; empty when it could.
    const std::string &error() const
    {
        return error_;
    }

  private:
    bool open(const std::string &path);
    int decode(float *out, int frames);

    Mixer *mixer_ = nullptr;
    MusicPlayer *playlist_ = nullptr;
    StreamRing ring_{1u << 15};
    std::unique_ptr<At9Track> at9_;
    std::unique_ptr<MusicTrack> ogg_;
    std::string path_;   // asked for
    std::string opened_; // decoding now
    std::string error_;
    float switching_ = 0.0f; // seconds of silence while the last one fades
    float gain_ = 1.0f;      // the song's own (title_loudness_gain)
    float peak_ = 0.0f;      // its opening's loudest sample
    float volume_ = 1.0f;    // the player's (set_volume)
    float effective_gain() const;
    std::vector<float> buffer_;
};

} // namespace hui::audio
