// ORION Media Center - Serves one package to the console's installer, streamed from its drive.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace hui::orion
{

inline constexpr int kPackagePort = 9063;

// A file read at any place: a package on a network drive (sys::SmbFile).
class PackageSource
{
  public:
    virtual ~PackageSource() = default;
    // Up to `count` bytes from `offset`; how many were read, or -1.
    virtual long long read(std::uint64_t offset, void *buffer, std::size_t count) = 0;
};
// Opens the file again: each connection reads through its own.
using PackageOpener = std::function<std::unique_ptr<PackageSource>(std::string *error)>;

// The console's installer reads a package from a URL, in ranges, on more
// than one connection at once. This answers GET and HEAD for one file at
// http://127.0.0.1:<port>/<token>/package.pkg, with Range, on 127.0.0.1
// only, streaming from the source as it is asked: nothing is copied first.
//
//   PackageServer server;
//   server.start("t1", size, opener);
//   ... server.url(), server.sent(), server.quiet_seconds()
//   server.stop();
class PackageServer
{
  public:
    PackageServer() = default;
    ~PackageServer();
    PackageServer(const PackageServer &) = delete;
    PackageServer &operator=(const PackageServer &) = delete;

    bool start(std::string token, std::uint64_t size, PackageOpener open, int port = kPackagePort);
    void stop();
    bool running() const
    {
        return running_.load();
    }
    std::string url() const;
    // Bytes sent so far, all connections together (a range read twice
    // counts twice).
    std::uint64_t sent() const
    {
        return sent_.load();
    }
    std::uint64_t size() const
    {
        return size_;
    }
    // Seconds since the last byte went out (or since it started).
    int quiet_seconds() const;
    int connections() const
    {
        return connections_.load();
    }
    // The first requests and what each was answered ("GET /t1/package.pkg
    // HTTP/1.1 -> 206 0-65535"), for the install log: when the installer
    // refuses an address, they say what it asked for.
    std::vector<std::string> requests() const;

  private:
    void note_request(const std::string &line, const std::string &answer);
    void listen_loop(int listener);
    void serve(int client);

    std::string token_;
    std::uint64_t size_ = 0;
    int port_ = kPackagePort;
    PackageOpener open_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<std::uint64_t> sent_{0};
    std::atomic<long long> last_ms_{0};
    std::atomic<int> connections_{0};
    int listener_ = -1;
    std::thread listener_thread_;
    std::mutex clients_mutex_;
    std::vector<std::thread> clients_;
    mutable std::mutex requests_mutex_;
    std::vector<std::string> requests_;
};

// The path a request line asks for: "/t1/package.pkg" from "GET
// /t1/package.pkg?x=1 HTTP/1.1" or from "GET http://127.0.0.1:9063/t1/package.pkg
// HTTP/1.1" (an absolute address, which HTTP/1.1 servers must take).
std::string request_path(const std::string &line);

// The parts of a Range header: "bytes=100-199" (both), "bytes=100-"
// (from), "bytes=-100" (the last 100). False when there is none to
// honour; *first and *last are inclusive and within `size`.
bool parse_range(const std::string &header, std::uint64_t size, std::uint64_t *first,
                 std::uint64_t *last);

} // namespace hui::orion
