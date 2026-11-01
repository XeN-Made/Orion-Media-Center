// ORION Media Center - Checks a console run makes on itself, for tools/console-check.py.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/self_test.hpp"

#include "core/save_file.hpp"
#include "orion/console_share.hpp"

#include <sys/stat.h>

#include <cstdio>
#include <map>
#include <utility>

namespace hui::orion
{

namespace
{

using Outcome = CheckResult::Outcome;

constexpr int kLoaderPort = 9021;
constexpr int kMostTried = 8; // network drives tried; each can take a while to time out

void add(std::vector<CheckResult> *results, std::string name, Outcome outcome, std::string detail)
{
    CheckResult result;
    result.name = std::move(name);
    result.outcome = outcome;
    result.detail = std::move(detail);
    results->push_back(std::move(result));
}

bool starts_with(const std::string &text, const std::string &prefix)
{
    return text.compare(0, prefix.size(), prefix) == 0;
}

// The player report "ORION cannot reach /data" is this one failing.
void check_data(const SelfTestInput &in, std::vector<CheckResult> *results)
{
    const std::string dir = in.state_dir + "/selftest";
    const std::string probe = dir + "/probe.txt";
    std::string back;
    const bool made = save::ensure_directory(dir);
    const std::string error = made ? save::write_atomic(probe, "orion\n") : "no folder";
    const bool read = error.empty() && save::read_file(probe, &back, 64) && back == "orion\n";
    std::remove(probe.c_str());
    add(results, "data-writable", read ? Outcome::pass : Outcome::fail,
        read ? in.state_dir + " written and read back"
             : "cannot write " + dir + (error.empty() ? std::string() : ": " + error));
}

void check_payloads(const SelfTestInput &in, std::vector<CheckResult> *results)
{
    std::string missing;
    for (const std::string &name : in.payloads)
    {
        struct stat info = {};
        const std::string path = in.app_dir + "/assets/payloads/" + name;
        if (::stat(path.c_str(), &info) != 0 || info.st_size == 0)
            missing += (missing.empty() ? "" : ", ") + name;
    }
    add(results, "payloads", missing.empty() ? Outcome::pass : Outcome::fail,
        missing.empty() ? std::to_string(in.payloads.size()) + " present" : "missing " + missing);
    const bool loader = in.port_open && in.port_open(kLoaderPort);
    add(results, "elf-loader", loader ? Outcome::pass : Outcome::fail,
        loader ? "answers on port 9021"
               : "nothing on port 9021: games, packages and sharing cannot start");
}

void check_library(const SelfTestInput &in, std::vector<CheckResult> *results)
{
    int homebrew = 0;
    int emulated = 0;
    int payloads = 0;
    for (const library::Title &title : in.titles)
    {
        if (title.source == library::Source::payload)
            ++payloads;
        else if (!title.system.empty())
            ++emulated;
        else if (title.homebrew)
            ++homebrew;
    }
    const int total = static_cast<int>(in.titles.size());
    add(results, "library", total > 0 ? Outcome::pass : Outcome::fail,
        std::to_string(total) +
            " titles: " + std::to_string(total - homebrew - emulated - payloads) + " games, " +
            std::to_string(homebrew) + " homebrew, " + std::to_string(emulated) + " emulated, " +
            std::to_string(payloads) + " payloads");

    // A drive that holds installed games (its user/app) and gave the library
    // none of them is the extended-storage helper failing.
    std::map<std::string, int> found;
    for (const library::Title &title : in.titles)
    {
        for (const std::string &root : in.drive_roots)
        {
            if (starts_with(title.path, root + "/"))
                ++found[root];
        }
    }
    std::string seen;
    std::string lost;
    for (const std::string &root : in.drive_roots)
    {
        const std::size_t installed = save::list_directory(root + "/user/app").size();
        const auto it = found.find(root);
        const int titles = it == found.end() ? 0 : it->second;
        if (installed == 0 && titles == 0)
            continue;
        const std::string line = root + " " + std::to_string(titles) + " titles";
        seen += (seen.empty() ? "" : ", ") + line;
        if (installed > 0 && titles == 0)
            lost += (lost.empty() ? "" : ", ") + root + " has " + std::to_string(installed) +
                    " in user/app";
    }
    if (seen.empty())
        add(results, "drives", Outcome::skip, "no extended storage or USB drive with games");
    else
        add(results, "drives", lost.empty() ? Outcome::pass : Outcome::fail,
            lost.empty() ? seen : lost + " but the library has none of them");
}

void check_network_drives(const SelfTestInput &in, std::vector<CheckResult> *results)
{
    const std::vector<Share> shares = load_shares(in.state_dir);
    if (shares.empty() || !in.list)
    {
        add(results, "network-drives", Outcome::skip, "none saved");
        return;
    }
    int tried = 0;
    for (const Share &share : shares)
    {
        if (tried++ == kMostTried)
            break;
        std::vector<sys::SmbEntry> entries;
        std::string error;
        const bool listed =
            in.list(share_login(share, in.state_dir), share.folder, 0, 64, &entries, &error);
        const char *hint = listed ? "" : share_hint(error);
        add(results, "network-drive " + share_address(share),
            listed ? Outcome::pass : Outcome::fail,
            listed ? "signed in, " + std::to_string(entries.size()) + " entries"
                   : error + (*hint != '\0' ? std::string(" (") + hint + ")" : std::string()));
    }
}

// Signs in to the console's own share the way a PC does, over the loopback:
// the payload's server, its sign-in and its signing, end to end.
void check_console_share(const SelfTestInput &in, std::vector<CheckResult> *results)
{
    const ConsoleShare share = read_console_share(in.state_dir);
    if (!share.on)
    {
        add(results, "console-share", Outcome::skip, "sharing is off in Settings");
        return;
    }
    std::string status;
    save::read_file(in.state_dir + "/smb-share-status.txt", &status, 4096);
    while (!status.empty() && (status.back() == '\n' || status.back() == '\r'))
        status.pop_back();
    if (!starts_with(status, "on"))
    {
        add(results, "console-share", Outcome::fail,
            "the payload says \"" + status + "\": " + console_share_problem(in.state_dir));
        return;
    }
    sys::SmbLogin login;
    login.server = "127.0.0.1";
    login.share = "data";
    login.user = share.user;
    login.password = share.password;
    std::vector<sys::SmbEntry> entries;
    std::string error;
    const bool listed = in.list && in.list(login, "", 0, 64, &entries, &error);
    add(results, "console-share", listed ? Outcome::pass : Outcome::fail,
        listed ? "signed in to \\\\127.0.0.1\\data, " + std::to_string(entries.size()) +
                     " entries; shared: " + status.substr(status.find('\t') + 1)
               : "sharing, but a sign-in to it failed: " + error);
}

} // namespace

std::vector<CheckResult> run_self_test(const SelfTestInput &in)
{
    std::vector<CheckResult> results;
    check_data(in, &results);
    check_payloads(in, &results);
    check_library(in, &results);
    check_network_drives(in, &results);
    check_console_share(in, &results);
    return results;
}

std::string self_test_report(const std::string &token, const std::vector<CheckResult> &results)
{
    std::string text = "token " + token + "\n";
    int counts[3] = {};
    for (const CheckResult &result : results)
    {
        static constexpr const char *kWords[] = {"PASS", "FAIL", "SKIP"};
        const int at = static_cast<int>(result.outcome);
        ++counts[at];
        text += std::string(kWords[at]) + " " + result.name + ": " + result.detail + "\n";
    }
    text += "passed " + std::to_string(counts[0]) + " failed " + std::to_string(counts[1]) +
            " skipped " + std::to_string(counts[2]) + "\n";
    return text;
}

SelfTest::~SelfTest()
{
    wait();
}

void SelfTest::wait()
{
    if (worker_.joinable())
        worker_.join();
}

bool SelfTest::start(SelfTestInput in)
{
    if (running_.load())
        return false;
    wait();
    running_ = true;
    worker_ = std::thread(
        [this, in = std::move(in)]()
        {
            std::vector<CheckResult> results = run_self_test(in);
            const std::lock_guard<std::mutex> lock(mutex_);
            results_ = std::move(results);
            finished_ = true;
            running_ = false;
        });
    return true;
}

bool SelfTest::take_finished(std::vector<CheckResult> *results)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!finished_)
        return false;
    finished_ = false;
    *results = std::move(results_);
    return true;
}

} // namespace hui::orion
