// ORION Media Center - Checks a console run makes on itself, for tools/console-check.py.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/self_test.hpp"

#include "core/save_file.hpp"
#include "core/sha256.hpp"
#include "orion/console_share.hpp"

#include <sys/stat.h>

#include <chrono>
#include <cstdio>
#include <map>
#include <thread>
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

// Every file under `folder`, as paths relative to it.
void files_under(const std::string &folder, const std::string &prefix,
                 std::vector<std::string> *files)
{
    for (const save::DirectoryEntry &entry : save::list_directory(folder))
    {
        if (entry.directory)
            files_under(folder + "/" + entry.name, prefix + entry.name + "/", files);
        else
            files->push_back(prefix + entry.name);
    }
}

// The file's SHA-256, read a piece at a time (a game's files are large).
std::string hash_file(const std::string &path)
{
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return {};
    Sha256 hash;
    std::string piece(1u << 20, '\0');
    std::size_t got = 0;
    while ((got = std::fread(piece.data(), 1, piece.size(), file)) > 0)
        hash.update(std::string_view(piece.data(), got));
    std::fclose(file);
    return hash.hex();
}

// "<sha256>  <path>" a line, as sha256sum writes them.
std::map<std::string, std::string> read_sums(const std::string &path)
{
    std::map<std::string, std::string> sums;
    std::string text;
    save::read_file(path, &text, 1u << 20);
    for (std::size_t start = 0; start < text.size();)
    {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        const std::string line = text.substr(start, end - start);
        start = end + 1;
        if (line.size() > 66 && line.compare(64, 2, "  ") == 0)
            sums[line.substr(66)] = line.substr(0, 64);
    }
    return sums;
}

// What was installed against what the PC packed: the same files, no more,
// each with the same bytes. An image is one file, compared by itself.
std::string compare_installed(const std::string &installed,
                              const std::map<std::string, std::string> &sums)
{
    struct stat info = {};
    if (::stat(installed.c_str(), &info) != 0)
        return "nothing at " + installed;
    if (!S_ISDIR(info.st_mode))
    {
        if (sums.size() != 1)
            return "an image, where a folder was expected";
        return hash_file(installed) == sums.begin()->second ? std::string()
                                                            : "the image's bytes differ";
    }
    std::vector<std::string> found;
    files_under(installed, "", &found);
    if (found.size() != sums.size())
        return std::to_string(found.size()) + " files, " + std::to_string(sums.size()) +
               " expected";
    for (const auto &[path, sum] : sums)
    {
        if (hash_file(installed + "/" + path) != sum)
            return path + " differs";
    }
    return {};
}

// One install, start to end, the way the library screen runs it; `stop`
// cancels it as soon as bytes have started to arrive.
GameInstall::Result install_once(const SelfTestInput &in, const sys::GameSource &source,
                                 const std::string &name, const std::string &place, bool stop,
                                 double *mb_per_second)
{
    GameInstall install(in.copy);
    GameInstall::Result result;
    install.start(source, name, place);
    while (!install.take_finished(&result))
    {
        if (stop && install.done() > 0)
            install.cancel();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const double seconds = install.seconds();
    *mb_per_second = seconds > 0.0 ? static_cast<double>(install.done()) / 1e6 / seconds : 0.0;
    return result;
}

std::string problem_name(GameInstall::Problem problem)
{
    switch (problem)
    {
    case GameInstall::Problem::none:
        return "none";
    case GameInstall::Problem::copy:
        return "copy";
    case GameInstall::Problem::stopped:
        return "stopped";
    case GameInstall::Problem::no_game:
        return "no game";
    case GameInstall::Problem::already_there:
        return "already there";
    case GameInstall::Problem::cannot_write:
        return "cannot write";
    }
    return "?";
}

// The test games the PC packed (tools/console-check.py), installed off the
// console's own share as a game off a PC's would be. fixtures/cases.txt has
// a line each: "<ok|twice|stop>\t<file or folder>\t<sums file>". ok:
// installs and matches the sums; twice: then the same again is refused as
// already there; stop: stopped once it has begun leaves nothing behind.
void check_installs(const SelfTestInput &in, std::vector<CheckResult> *results)
{
    const std::string fixtures = in.state_dir + "/selftest/fixtures";
    std::string cases;
    if (!save::read_file(fixtures + "/cases.txt", &cases, 64u << 10))
        return; // the PC asked for no install checks
    const ConsoleShare share = read_console_share(in.state_dir);
    if (!share.on || !in.copy || !in.list)
    {
        add(results, "installs", Outcome::skip,
            "turn sharing on in Settings: the test games are installed off the console's share");
        return;
    }
    sys::SmbLogin login;
    login.server = "127.0.0.1";
    login.share = "data";
    login.user = share.user;
    login.password = share.password;
    const std::string on_share = "orion/selftest/fixtures";
    std::vector<sys::SmbEntry> entries;
    std::string error;
    if (!in.list(login, on_share, 0, 256, &entries, &error))
    {
        add(results, "installs", Outcome::fail, "cannot list the test games: " + error);
        return;
    }
    std::vector<std::string> names;
    for (const sys::SmbEntry &entry : entries)
        names.push_back(entry.path);

    const std::string place = in.state_dir + "/selftest/installed";
    save::remove_tree(place);
    for (std::size_t start = 0; start < cases.size();)
    {
        std::size_t end = cases.find('\n', start);
        if (end == std::string::npos)
            end = cases.size();
        const std::string line = cases.substr(start, end - start);
        start = end + 1;
        const std::size_t first_tab = line.find('\t');
        const std::size_t second_tab = line.find('\t', first_tab + 1);
        if (first_tab == std::string::npos || second_tab == std::string::npos)
            continue;
        const std::string expect = line.substr(0, first_tab);
        const std::string path = line.substr(first_tab + 1, second_tab - first_tab - 1);
        const std::map<std::string, std::string> sums =
            read_sums(fixtures + "/" + line.substr(second_tab + 1));

        sys::GameSource source;
        source.login = login;
        source.kind = install_kind(path);
        if (source.kind == sys::GameSourceKind::none)
            source.kind = sys::GameSourceKind::folder;
        const std::vector<std::string> volumes = source.kind == sys::GameSourceKind::archive
                                                     ? archive_volumes(names, path)
                                                     : std::vector<std::string>{path};
        for (const std::string &volume : volumes)
            source.paths.push_back(on_share + "/" + volume);
        const std::string label = "install " + expect + " " + path;
        const std::string name = install_name(path);
        const std::string count = volumes.size() > 1
                                      ? " (" + std::to_string(volumes.size()) + " volumes)"
                                      : std::string();
        double speed = 0.0;
        const GameInstall::Result result =
            install_once(in, source, name, place, expect == "stop", &speed);

        if (expect == "stop")
        {
            std::vector<std::string> left;
            files_under(in.state_dir + "/selftest", "", &left);
            std::string stray;
            for (const std::string &file : left)
            {
                if (starts_with(file, "installed/") || starts_with(file, ".orion-install/"))
                    stray = file;
            }
            const bool good = result.problem == GameInstall::Problem::stopped && stray.empty();
            add(results, label, good ? Outcome::pass : Outcome::fail,
                good ? "stopped, nothing left behind"
                     : "ended " + problem_name(result.problem) +
                           (stray.empty() ? std::string() : ", left " + stray));
            save::remove_tree(place);
            continue;
        }
        if (!result.ok)
        {
            add(results, label, Outcome::fail,
                problem_name(result.problem) + (result.detail.empty() ? "" : ": " + result.detail));
            save::remove_tree(place);
            continue;
        }
        std::string differs = compare_installed(result.installed, sums);
        if (differs.empty() && expect == "twice")
        {
            double again_speed = 0.0;
            const GameInstall::Result again =
                install_once(in, source, name, place, false, &again_speed);
            if (again.problem != GameInstall::Problem::already_there)
                differs = "a second install ended " + problem_name(again.problem) +
                          " instead of already there";
        }
        char rate[32];
        std::snprintf(rate, sizeof(rate), "%.1f MB/s", speed);
        add(results, label, differs.empty() ? Outcome::pass : Outcome::fail,
            differs.empty() ? std::to_string(sums.size()) + " files match" + count + ", " + rate +
                                  (expect == "twice" ? "; again: already there" : "")
                            : differs);
        save::remove_tree(place);
    }
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
    check_installs(in, &results);
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
