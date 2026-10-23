// ORION Media Center - ORION's rest-mode watcher: opens ORION when the console wakes on the home screen.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORION sends this ELF to the ELF loader when it starts, while Settings,
// Library, "Open ORION after rest mode" is on. It stays in the background:
//
//   - One copy runs at a time. The first holds a socket on 127.0.0.1
//     (WATCH_PORT) for as long as it lives; a later copy cannot bind it and
//     ends at once.
//   - Rest mode freezes it with everything else. It notices the wake by the
//     clock: a short sleep that took far longer than asked was a rest.
//   - On a wake it waits for the system to settle. With nothing in front
//     (the home screen) it opens ORION; with an app in front (a game left
//     running, ORION itself) it leaves it there.
//   - It ends for good once /data/orion/reopen-after-rest is gone (ORION
//     removes that file when the setting is turned off).
//
// The title it opens is the one in /data/orion/title.txt, which ORION writes
// as it starts, so it follows ORION to a new title ID. Every step is appended
// to /data/orion/resume.txt.

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define ON_PATH "/data/orion/reopen-after-rest"
#define TITLE_PATH "/data/orion/title.txt"
#define LOG_PATH "/data/orion/resume.txt"
#define FALLBACK_TITLE "PPSA99061"
#define WATCH_PORT 10161     // held while a watcher runs: one at a time
#define TICK_S 2             // how often it looks at the clock
#define REST_GAP_S 20        // a tick this much longer than asked was a rest
#define SETTLE_S 6           // after a wake: the home screen comes back first
#define USER_WAIT_S 120      // waiting for someone to sign in after the wake
#define LAUNCH_TRIES 5

typedef struct launch_param
{
    uint32_t structsize;
    uint32_t user_id;
    uint32_t app_opt;
    uint64_t crash_report;
    uint32_t check_flag;
} launch_param_t;

int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceLaunchApp(const char *title_id, char **argv, launch_param_t *param);
int sceUserServiceInitialize(void *params);
int sceUserServiceGetForegroundUser(int *user);

static void say(const char *format, ...)
{
    char line[512];
    int length = snprintf(line, sizeof(line), "%lld  resume  ", (long long)time(NULL));
    va_list arguments;
    va_start(arguments, format);
    length += vsnprintf(line + length, sizeof(line) - (size_t)length - 1, format, arguments);
    va_end(arguments);
    if (length > (int)sizeof(line) - 2)
        length = (int)sizeof(line) - 2;
    line[length++] = '\n';
    const int fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        return;
    write(fd, line, (size_t)length);
    close(fd);
}

static int exists(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0;
}

// The title ORION has now, as it last wrote it; FALLBACK_TITLE without one.
static void orion_title(char *title, size_t size)
{
    snprintf(title, size, "%s", FALLBACK_TITLE);
    char text[32] = "";
    const int fd = open(TITLE_PATH, O_RDONLY);
    if (fd < 0)
        return;
    const ssize_t got = read(fd, text, sizeof(text) - 1);
    close(fd);
    if (got < 9)
        return;
    text[9] = '\0';
    for (int i = 0; i < 9; ++i)
    {
        const char c = text[i];
        if (i < 4 ? (c < 'A' || c > 'Z') : (c < '0' || c > '9'))
            return;
    }
    snprintf(title, size, "%s", text);
}

// Holds WATCH_PORT for the life of the process; -1 when another watcher has it.
static int claim(void)
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(WATCH_PORT);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(fd, 1) != 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

static void open_orion(void)
{
    // Someone has to be signed in for a title to start for them.
    int user = 0;
    for (int waited = 0; sceUserServiceGetForegroundUser(&user) != 0 && waited < USER_WAIT_S;
         ++waited)
        sleep(1);
    sleep(SETTLE_S);
    const int app = sceSystemServiceGetAppIdOfRunningBigApp();
    if (app > 0)
    {
        say("woke with an app in front (0x%08x): leaving it", (unsigned)app);
        return;
    }
    char title[16];
    orion_title(title, sizeof(title));
    for (int attempt = 1; attempt <= LAUNCH_TRIES; ++attempt)
    {
        launch_param_t param;
        memset(&param, 0, sizeof(param));
        param.structsize = sizeof(param);
        param.user_id = (uint32_t)user;
        const int result = sceSystemServiceLaunchApp(title, NULL, &param);
        say("woke on the home screen: opening %s, try %d: 0x%08x", title, attempt,
            (unsigned)result);
        if (result >= 0 || sceSystemServiceGetAppIdOfRunningBigApp() > 0)
            return;
        sleep(1);
    }
}

int main(void)
{
    if (!exists(ON_PATH))
        return 0;
    const int held = claim();
    if (held < 0)
        return 0; // a watcher is already running
    sceUserServiceInitialize(NULL);
    say("watching for the console to wake");
    time_t last = time(NULL);
    for (;;)
    {
        sleep(TICK_S);
        const time_t now = time(NULL);
        const long long gap = (long long)(now - last) - TICK_S;
        last = now;
        if (!exists(ON_PATH))
        {
            say("turned off in ORION: stopping");
            break;
        }
        if (gap < REST_GAP_S)
            continue;
        say("the console rested for about %lld s", gap);
        open_orion();
        last = time(NULL);
    }
    close(held);
    return 0;
}
