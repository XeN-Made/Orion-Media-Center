// ORION Media Center - The key page: ORION's SteamGridDB key typed on a phone or PC.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <string>
#include <string_view>
#include <thread>

namespace hui::orion
{

// The port the page answers on, while ORION is open: http://<console>:9062.
inline constexpr int kKeyPagePort = 9062;

// The answer to one HTTP request (the whole response, headers and all).
// GET shows the form; a POST of "steamgriddb=<key>" to /key saves the key
// in <state_dir>/keys.txt (orion/steamgriddb.hpp) and sets *saved. The key
// is never written back into a page. Exposed for tests.
std::string answer_key_request(std::string_view request, const std::string &state_dir, bool *saved);

// One form field's value from an x-www-form-urlencoded body, decoded; false
// when the field is not there.
bool form_value(std::string_view body, std::string_view name, std::string *value);

// The console's address on the local network ("192.168.4.41"), or empty.
// Asked of the system's routing (a UDP socket aimed outward sends nothing).
std::string local_address();

// The page's server, on a thread of its own: one request at a time, each
// with a time limit, so a stuck browser cannot hold it.
//
//   KeyPage page;
//   page.start("/data/orion");
//   ...every frame: if (page.take_saved()) re-read the key
class KeyPage
{
  public:
    KeyPage() = default;
    ~KeyPage();
    KeyPage(const KeyPage &) = delete;
    KeyPage &operator=(const KeyPage &) = delete;

    // False when the port cannot be had (another app holds it).
    bool start(std::string state_dir, int port = kKeyPagePort);
    void stop();
    bool running() const
    {
        return running_.load();
    }
    // True once after a key was saved from the page.
    bool take_saved()
    {
        return saved_.exchange(false);
    }

  private:
    void serve(int listener, std::string state_dir);

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> saved_{false};
};

} // namespace hui::orion
