// ORION Media Center - Checks a console run makes on itself, for tools/console-check.py.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the release test list asks of a console that ORION can answer
// without a person: can it write /data, are its payloads and the ELF loader
// there, what did the library find, do the saved network drives sign in,
// and does the console's own share (orion/console_share.hpp) let a client
// in. A PC asks with a line "check <what> <token>" (main.cpp) and reads the
// report from <state>/selftest/report.txt, which outlives the app.
//
//   orion::SelfTestInput in;
//   in.state_dir = "/data/orion";
//   in.titles = titles.titles;
//   in.list = sys::smb_list;
//   in.port_open = [](int port) { ... };
//   orion::SelfTest test;
//   test.start(std::move(in));
//   ...  // every frame
//   if (test.take_finished(&results))
//       save::write_atomic(path, orion::self_test_report(token, results));

#pragma once

#include "library/library.hpp"
#include "orion/shares.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace hui::orion
{

struct CheckResult
{
    enum class Outcome
    {
        pass,
        fail,
        skip, // nothing to check on this console (no drive saved, sharing off)
    };
    std::string name; // "data-writable"
    Outcome outcome = Outcome::pass;
    std::string detail;
};

struct SelfTestInput
{
    std::string state_dir;             // /data/orion
    std::string app_dir;               // /app0
    std::vector<std::string> payloads; // file names expected in <app_dir>/assets/payloads
    std::vector<library::Title> titles;
    // Drives games are installed on: "/mnt/ext0", "/mnt/usb0".
    std::vector<std::string> drive_roots;
    SmbLister list;                          // sys::smb_list on the console
    std::function<bool(int port)> port_open; // something answers on 127.0.0.1:<port>
};

// Every check, in the report's order. Blocking: network drives are tried.
std::vector<CheckResult> run_self_test(const SelfTestInput &in);

// "token <token>" first, then "PASS name: detail" a line, then a summary
// "passed N failed N skipped N".
std::string self_test_report(const std::string &token, const std::vector<CheckResult> &results);

// run_self_test on a thread of its own, so the frame keeps going.
class SelfTest
{
  public:
    SelfTest() = default;
    ~SelfTest();
    SelfTest(const SelfTest &) = delete;
    SelfTest &operator=(const SelfTest &) = delete;

    bool start(SelfTestInput in);
    bool running() const
    {
        return running_.load();
    }
    // True once after the checks ended, with what they found.
    bool take_finished(std::vector<CheckResult> *results);
    void wait();

  private:
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    bool finished_ = false;
    std::vector<CheckResult> results_;
};

} // namespace hui::orion
