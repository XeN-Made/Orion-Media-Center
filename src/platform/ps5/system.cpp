// ORION Media Center - PS5 system services: logging, clock, splash screen.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/ps5/system.hpp"

#include "core/local_host.hpp"

#include "platform/ps5/ca_bundle.hpp"

#include <arpa/inet.h>
#include <curl/curl.h>
#include <fcntl.h>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <netdb.h>
#include <thread>
#include <utility>
#include <vector>
#include <cerrno>
#include <cstdarg>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

extern "C"
{
    int sceKernelDebugOutText(int channel, const char *text);
    int sceKernelUsleep(unsigned int microseconds);
    int sceSystemServiceHideSplashScreen(void);
    int sceSystemServiceLoadExec(const char *path, const char **arguments);
    int sceSystemServiceParamGetInt(int param_id, int *value);
    int sceUserServiceGetForegroundUser(int *user);
    int sceUserServiceGetInitialUser(int *user);

    // The launch parameters as the PS5 homebrew loader passes them
    // (github.com/ps5-payload-dev, hbldr.c): only the user is filled in.
    struct HuiLaunchParam
    {
        std::uint32_t structsize;
        std::uint32_t user_id;
        std::uint32_t app_opt;
        std::uint64_t crash_report;
        std::uint32_t check_flag;
    };
    int sceSystemServiceLaunchApp(const char *title_id, char **argv, HuiLaunchParam *param);
}

extern "C" void hui_release_splash(void) __attribute__((weak));

namespace hui::sys
{

std::int64_t monotonic_us()
{
    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return static_cast<std::int64_t>(now.tv_sec) * 1000000 + now.tv_nsec / 1000;
}

void log(const char *format, ...)
{
    // klog lines are short; longer messages are truncated rather than split.
    char line[384];
    va_list arguments;
    va_start(arguments, format);
    int length = std::vsnprintf(line, sizeof(line) - 1, format, arguments);
    va_end(arguments);
    if (length < 0)
        return;
    std::size_t used = std::strlen(line);
    line[used] = '\n';
    line[used + 1] = '\0';
    std::fputs(line, stdout);
    sceKernelDebugOutText(0, line);
}

bool hide_splash_screen()
{
    // The runtime holds earlier requests back (runtime_shims.c): this is the
    // one that counts. An app with a runtime of its own has no such hold.
    if (hui_release_splash)
        hui_release_splash();
    return sceSystemServiceHideSplashScreen() == 0;
}

void sleep_us(std::uint32_t microseconds)
{
    sceKernelUsleep(microseconds);
}

void park()
{
    std::fflush(nullptr);
    for (;;)
        sceKernelUsleep(100000);
}

void quit()
{
    log("[HUI] quit requested");
    std::fflush(nullptr);
    const int refused = sceSystemServiceLoadExec("exit", nullptr);
    log("[HUI] quit refused rc=0x%x", static_cast<unsigned>(refused));
    park();
}

std::string system_language()
{
    // SCE_SYSTEM_SERVICE_PARAM_ID_LANG, and its values in the order the
    // system numbers its languages.
    constexpr int kParamLanguage = 1;
    static constexpr const char *kCodes[] = {
        "ja",      "en",      "fr", "es", "de", "it", "nl", "pt-BR", "ru", "ko",
        "zh-Hant", "zh-Hans", "fi", "sv", "da", "no", "pl", "pt-BR", "en", "tr",
        "es",      "ar",      "fr", "cs", "hu", "el", "ro", "th",    "vi", "id"};
    int value = -1;
    if (sceSystemServiceParamGetInt(kParamLanguage, &value) != 0 || value < 0 ||
        value >= static_cast<int>(sizeof(kCodes) / sizeof(kCodes[0])))
        return "en";
    return kCodes[value];
}

bool on_console()
{
    return true;
}

int send_payload(const std::string &elf, int port)
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return errno != 0 ? errno : EIO;
    // A loader that accepts but does not read (busy with another payload)
    // must not hold ORION: the send gives up after this long.
    timeval timeout{10, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0)
    {
        const int error = errno != 0 ? errno : ECONNREFUSED;
        ::close(fd);
        return error;
    }
    std::size_t sent = 0;
    while (sent < elf.size())
    {
        const ssize_t wrote = ::send(fd, elf.data() + sent, elf.size() - sent, MSG_NOSIGNAL);
        if (wrote <= 0)
        {
            const int error = errno != 0 ? errno : EIO;
            ::close(fd);
            return error;
        }
        sent += static_cast<std::size_t>(wrote);
    }
    // The loader starts the payload once the stream ends.
    ::shutdown(fd, SHUT_WR);
    ::close(fd);
    log("[HUI] payload sent: %zu bytes to 127.0.0.1:%d", sent, port);
    return 0;
}

int launch_app(const char *title_id)
{
    int user = 0;
    if (sceUserServiceGetForegroundUser(&user) != 0)
        sceUserServiceGetInitialUser(&user);
    HuiLaunchParam param = {};
    param.user_id = static_cast<std::uint32_t>(user);
    const int result = sceSystemServiceLaunchApp(title_id, nullptr, &param);
    log("[HUI] launch %s user=0x%08x result=0x%08x", title_id, static_cast<unsigned>(user),
        static_cast<unsigned>(result));
    return result;
}

constexpr const char *kArtTrace = "/data/orion/art-trace.txt";

void art_trace(const char *step, bool reset)
{
    const int fd = ::open(kArtTrace, O_WRONLY | O_CREAT | (reset ? O_TRUNC : O_APPEND), 0666);
    if (fd < 0)
        return;
    char line[256];
    const int length = std::snprintf(line, sizeof(line), "%lld ms  %s\n",
                                     static_cast<long long>(monotonic_us() / 1000), step);
    if (length > 0)
    {
        const ssize_t written = ::write(fd, line, static_cast<std::size_t>(length));
        (void)written;
    }
    ::fsync(fd);
    ::close(fd);
}

namespace
{

constexpr int kMaxHops = 5; // redirects followed (GitHub's downloads take one)

// curl's global set-up, once, whichever request comes first.
void curl_ready()
{
    static std::once_flag once;
    std::call_once(once,
                   []
                   {
                       art_trace("curl_global_init");
                       curl_global_init(CURL_GLOBAL_DEFAULT);
                       art_trace("curl_global_init done");
                   });
}

// ---- handles let go of in the background ----
//
// On the console a finished request's curl_easy_cleanup took exactly 30 s,
// and once never returned: curl waits there for its own name lookup to end
// (the threaded resolver), which the console's resolver drags out. Requests
// now give curl the address themselves (resolve_ipv4), and the clean-up is
// handed to this thread anyway, so a request returns as soon as its data is
// in. Past kMaxPending waiting, a request cleans up its own handle again.
constexpr std::size_t kMaxPending = 16;

struct Reaper
{
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::pair<CURL *, curl_slist *>> pending;
    bool started = false;
};

Reaper &reaper()
{
    static Reaper instance;
    return instance;
}

void let_go(CURL *curl, curl_slist *resolve)
{
    Reaper &r = reaper();
    {
        std::lock_guard<std::mutex> lock(r.mutex);
        if (r.pending.size() < kMaxPending)
        {
            r.pending.emplace_back(curl, resolve);
            if (!r.started)
            {
                r.started = true;
                std::thread(
                    [&r]
                    {
                        for (;;)
                        {
                            std::unique_lock<std::mutex> lock(r.mutex);
                            r.wake.wait(lock, [&r] { return !r.pending.empty(); });
                            const auto [handle, list] = r.pending.front();
                            r.pending.pop_front();
                            lock.unlock();
                            curl_easy_cleanup(handle);
                            curl_slist_free_all(list);
                        }
                    })
                    .detach();
            }
            r.wake.notify_one();
            return;
        }
    }
    curl_easy_cleanup(curl);
    curl_slist_free_all(resolve);
}

// ---- handles kept for the next request ----
//
// Handed to the thread above, cleanups of 30 s each still fell behind: past
// kMaxPending waiting, every request cleaned up its own, and a box art run
// stalled after its first few games. A finished handle is now reset and kept
// for the next request, so nothing is cleaned up (and its connections are
// used again); only past kMaxIdle at once does one go to the thread above.
constexpr std::size_t kMaxIdle = 8;

struct Idle
{
    std::mutex mutex;
    std::vector<CURL *> handles;
};

Idle &idle()
{
    static Idle instance;
    return instance;
}

CURL *take_handle()
{
    Idle &pool = idle();
    {
        std::lock_guard<std::mutex> lock(pool.mutex);
        if (!pool.handles.empty())
        {
            CURL *curl = pool.handles.back();
            pool.handles.pop_back();
            return curl;
        }
    }
    return curl_easy_init();
}

// The request is over: its options go (the resolve list with them), the
// handle stays for the next one.
void put_back(CURL *curl, curl_slist *resolve)
{
    curl_easy_reset(curl);
    curl_slist_free_all(resolve);
    Idle &pool = idle();
    {
        std::lock_guard<std::mutex> lock(pool.mutex);
        if (pool.handles.size() < kMaxIdle)
        {
            pool.handles.push_back(curl);
            return;
        }
    }
    let_go(curl, nullptr);
}

// The host's IPv4 address, from the system's resolver; empty when it has
// none. Given to curl (CURLOPT_RESOLVE), so curl starts no lookup of its own.
std::string resolve_ipv4(const std::string &host)
{
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *found = nullptr;
    if (::getaddrinfo(host.c_str(), nullptr, &hints, &found) != 0 || found == nullptr)
        return {};
    char text[INET_ADDRSTRLEN] = "";
    const auto *address = reinterpret_cast<const sockaddr_in *>(found->ai_addr);
    const bool ok = ::inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text)) != nullptr;
    ::freeaddrinfo(found);
    return ok ? std::string(text) : std::string();
}

// "host:443:1.2.3.4" for an https URL, or empty.
std::string resolve_entry(const std::string &url, std::string *host_out)
{
    CURLU *parts = curl_url();
    char *host = nullptr;
    char *port = nullptr;
    std::string entry;
    if (curl_url_set(parts, CURLUPART_URL, url.c_str(), 0) == CURLUE_OK &&
        curl_url_get(parts, CURLUPART_HOST, &host, 0) == CURLUE_OK &&
        curl_url_get(parts, CURLUPART_PORT, &port, CURLU_DEFAULT_PORT) == CURLUE_OK)
    {
        *host_out = host;
        const std::string address = resolve_ipv4(host);
        if (!address.empty())
            entry = std::string(host) + ":" + port + ":" + address;
    }
    curl_free(host);
    curl_free(port);
    curl_url_cleanup(parts);
    return entry;
}

// What a request writes to: a string or a file. reset() starts it over (a
// redirect's own body is thrown away).
struct Target
{
    std::string *body = nullptr; // or
    int fd = -1;
    const std::vector<std::string> *headers = nullptr; // for the first address only
    const std::string *post = nullptr;                 // a POST's body; nullptr: a GET
    std::size_t limit = 0;
    std::size_t written = 0;
    bool over = false;
    bool failed = false;

    void reset()
    {
        written = 0;
        if (body != nullptr)
            body->clear();
        if (fd >= 0 && (::ftruncate(fd, 0) != 0 || ::lseek(fd, 0, SEEK_SET) != 0))
            failed = true;
    }
};

std::size_t target_write(char *data, std::size_t size, std::size_t count, void *user)
{
    auto *target = static_cast<Target *>(user);
    const std::size_t bytes = size * count;
    if (target->written + bytes > target->limit)
    {
        target->over = true;
        return 0; // curl stops with CURLE_WRITE_ERROR
    }
    if (target->body != nullptr)
        target->body->append(data, bytes);
    else
    {
        std::size_t done = 0;
        while (done < bytes)
        {
            const ssize_t result = ::write(target->fd, data + done, bytes - done);
            if (result < 0 && errno == EINTR)
                continue;
            if (result <= 0)
            {
                target->failed = true;
                return 0;
            }
            done += static_cast<std::size_t>(result);
        }
    }
    target->written += bytes;
    return bytes;
}

// One https GET into `target`, redirects followed here, each host looked up
// by resolve_ipv4. `whole` caps the transfer in seconds (0: none, only a
// stall of 30 s ends it). Empty when it worked, else why not.
std::string fetch(std::string url, Target *target, long whole)
{
    curl_ready();
    for (int hop = 0; hop <= kMaxHops; ++hop)
    {
        // Plain http only to a server at home (net::is_local_host); the
        // internet is asked over https.
        const bool plain = url.rfind("http://", 0) == 0;
        if (plain && !net::is_local_host(net::url_host(url)))
            return "plain http is only for servers on the local network";
        std::string host;
        const std::string entry = resolve_entry(url, &host);
        if (entry.empty())
            return host.empty() ? "not a web address" : "cannot find " + host;
        CURL *curl = take_handle();
        if (curl == nullptr)
            return "curl did not start";
        curl_slist *resolve = curl_slist_append(nullptr, entry.c_str());
        target->reset();
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, target_write);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, target);
        // Mozilla's CA list (THIRD_PARTY_NOTICES.md), built into the app: the
        // file in assets/certs can be missing, as some upload tools skip it.
        curl_blob certificates{const_cast<char *>(ca_bundle().data()), ca_bundle().size(),
                               CURL_BLOB_NOCOPY};
        curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &certificates);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L); // followed here
#if LIBCURL_VERSION_NUM >= 0x075500
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, plain ? "http" : "https");
#endif
        curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
        if (whole > 0)
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, whole);
        // Signals and threads do not mix: worker threads call this.
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "ORION-Media-Center/1");
        // A key in a header goes to the address it was meant for, not on to
        // another host a redirect names.
        curl_slist *headers = nullptr;
        if (hop == 0 && target->headers != nullptr)
        {
            for (const std::string &header : *target->headers)
                headers = curl_slist_append(headers, header.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }
        if (target->post != nullptr)
        {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, target->post->data());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                             static_cast<curl_off_t>(target->post->size()));
        }
        art_trace(("perform " + url).c_str());
        const CURLcode code = curl_easy_perform(curl);
        if (headers != nullptr)
            curl_slist_free_all(headers);
        art_trace(("perform done: " + std::string(curl_easy_strerror(code))).c_str());
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        char *location = nullptr;
        curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &location);
        const std::string next = location != nullptr ? location : "";
        put_back(curl, resolve);
        if (code != CURLE_OK)
            return target->over     ? std::string("too large")
                   : target->failed ? std::string("could not write the file (disk full?)")
                                    : std::string(curl_easy_strerror(code));
        if (status >= 300 && status < 400 && !next.empty() && target->post == nullptr)
        {
            url = next;
            continue;
        }
        if (status != 200)
            return "HTTP " + std::to_string(status);
        return {};
    }
    return "too many redirects";
}

} // namespace

// The request as the payload SDK's hello_world sends it (payload/launcher).
struct NotifyRequest
{
    char useless1[45];
    char message[3075];
};
extern "C" int sceKernelSendNotificationRequest(int device, NotifyRequest *request,
                                                std::size_t size, int blocking);

void notify(const std::string &text)
{
    static NotifyRequest request;
    std::memset(&request, 0, sizeof(request));
    std::snprintf(request.message, sizeof(request.message), "%s", text.c_str());
    sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
}

bool http_download(const std::string &url, const std::string &path, std::size_t limit,
                   std::string *error)
{
    return http_download(url, {}, path, limit, error);
}

bool http_download(const std::string &url, const std::vector<std::string> &headers,
                   const std::string &path, std::size_t limit, std::string *error)
{
    const std::string part = path + ".part";
    const int fd = ::open(part.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
    {
        *error = "cannot write " + part + " (errno " + std::to_string(errno) + ")";
        return false;
    }
    Target target;
    target.fd = fd;
    target.limit = limit;
    target.headers = &headers;
    // No limit on the whole but 15 minutes: a slow line takes a while.
    std::string why = fetch(url, &target, 15L * 60L);
    const bool closed = ::fsync(fd) == 0 && ::close(fd) == 0;
    if (why.empty() && !closed)
        why = "could not finish the file";
    if (why.empty() && ::rename(part.c_str(), path.c_str()) != 0)
        why = "cannot rename the download (errno " + std::to_string(errno) + ")";
    if (!why.empty())
    {
        ::unlink(part.c_str());
        *error = why;
        return false;
    }
    return true;
}

bool http_get(const std::string &url, std::size_t limit, std::string *body, std::string *error)
{
    return http_get(url, {}, limit, body, error);
}

bool http_get(const std::string &url, const std::vector<std::string> &headers, std::size_t limit,
              std::string *body, std::string *error)
{
    Target target;
    target.body = body;
    target.limit = limit;
    target.headers = &headers;
    const std::string why = fetch(url, &target, 60L);
    if (!why.empty())
    {
        *error = why;
        return false;
    }
    return true;
}

bool http_post(const std::string &url, const std::vector<std::string> &headers,
               const std::string &body, std::size_t limit, std::string *answer, std::string *error)
{
    Target target;
    target.body = answer;
    target.limit = limit;
    target.headers = &headers;
    target.post = &body;
    const std::string why = fetch(url, &target, 60L);
    if (!why.empty())
    {
        *error = why;
        return false;
    }
    return true;
}

} // namespace hui::sys
