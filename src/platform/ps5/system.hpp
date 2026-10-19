// ps5-homebrew-ui - PS5 system services: logging, clock, splash screen.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

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

} // namespace hui::sys
