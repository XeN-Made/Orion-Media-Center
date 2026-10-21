// ORION Media Center - PS5 system services: logging, clock, splash screen.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace hui::sys
{

// Monotonic time in microseconds.
std::int64_t monotonic_us();

// Writes one line to the app log (stdout, redirected by runtime_shims.c) and
// mirrors it to klog so hardware runs can observe lifecycle markers live.
void log(const char *format, ...) __attribute__((format(printf, 1, 2)));

// Hides the system splash screen. Call once the first frame has been presented.
bool hide_splash_screen();

void sleep_us(std::uint32_t microseconds);

// Never returns: keeps the title alive until the shell closes it. Returning
// from main or calling exit() is not safe for a native title.
[[noreturn]] void park();

// Ends the title the one way that is clean for a native app: the system is
// asked to close it (exit() and _Exit() end in the crash reporter instead).
// Parks if the request is refused.
[[noreturn]] void quit();

// True on the console, false in the PC builds (tests and pictures).
bool on_console();

// What launch_app returns on a PC, where there is nothing to launch.
constexpr int kLaunchUnsupported = -1;

// Asks the system to start an installed title for the foreground user
// (sceSystemServiceLaunchApp). 0 means the request was accepted and the
// system takes it from there (it closes this app to start the other); any
// other value is the system's refusal code.
int launch_app(const char *title_id);

// Streams an ELF to the console's ELF loader on this machine
// (127.0.0.1:port), which runs it outside any app. 0 when every byte was
// accepted, else the errno of what failed (kLaunchUnsupported on a PC).
int send_payload(const std::string &elf, int port);

// Appends one line to /data/orion/art-trace.txt and syncs it, so the last
// line names the step a crash happened in. reset starts the file over.
// Nothing on a PC.
void art_trace(const char *step, bool reset = false);

// Fetches an https:// URL into *body (at most limit bytes), verifying the
// server against the CA list shipped in assets/certs. False with *error set
// otherwise, a page that is not 200 included. Blocking: call it from a
// worker thread. On a PC it always says there is no network.
bool http_get(const std::string &url, std::size_t limit, std::string *body, std::string *error);

// The same for a large file: written to `path` as it arrives (through
// <path>.part, renamed when complete), so it never sits in memory. ORION's
// heap is 128 MB; a release ZIP held in a string, doubling as it grows,
// does not fit beside everything else.
bool http_download(const std::string &url, const std::string &path, std::size_t limit,
                   std::string *error);

} // namespace hui::sys
