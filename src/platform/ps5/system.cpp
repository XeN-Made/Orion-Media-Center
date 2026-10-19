// ps5-homebrew-ui - PS5 system services: logging, clock, splash screen.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/ps5/system.hpp"

#include <arpa/inet.h>
#include <curl/curl.h>
#include <mutex>
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

bool on_console()
{
    return true;
}

int send_payload(const std::string &elf, int port)
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return errno != 0 ? errno : EIO;
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
        const ssize_t wrote = ::send(fd, elf.data() + sent, elf.size() - sent, 0);
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

namespace
{

// Mozilla's CA list, as Ubuntu's ca-certificates ships it (THIRD_PARTY_NOTICES.md).
constexpr const char *kCaBundle = "/app0/assets/certs/cacert.pem";

struct Sink
{
    std::string *body;
    std::size_t limit;
    bool over = false;
};

std::size_t sink_write(char *data, std::size_t size, std::size_t count, void *user)
{
    auto *sink = static_cast<Sink *>(user);
    const std::size_t bytes = size * count;
    if (sink->body->size() + bytes > sink->limit)
    {
        sink->over = true;
        return 0; // curl stops with CURLE_WRITE_ERROR
    }
    sink->body->append(data, bytes);
    return bytes;
}

} // namespace

bool http_get(const std::string &url, std::size_t limit, std::string *body, std::string *error)
{
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    body->clear();
    CURL *curl = curl_easy_init();
    if (curl == nullptr)
    {
        *error = "curl did not start";
        return false;
    }
    Sink sink{body, limit};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, sink_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl, CURLOPT_CAINFO, kCaBundle);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#endif
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    // Signals and threads do not mix: a worker thread calls this.
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ORION-Media-Center/1");
    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    if (code != CURLE_OK)
    {
        *error = sink.over ? std::string("too large") : std::string(curl_easy_strerror(code));
        return false;
    }
    if (status != 200)
    {
        *error = "HTTP " + std::to_string(status);
        return false;
    }
    return true;
}

} // namespace hui::sys
