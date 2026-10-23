// ORION Media Center - ORION's rest-mode watcher: opens ORION when the console wakes on the home screen.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORION sends this ELF to the ELF loader when it starts, while Settings,
// Library, "Open ORION after rest mode" is on. It stays in the background:
//
//   - One copy runs at a time: it holds a socket on 127.0.0.1 (WATCH_PORT)
//     for as long as it lives. A later copy (ORION sends one each time it
//     starts, maybe a newer one) connects to it, which tells it to end, and
//     takes the socket over.
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
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define ON_PATH "/data/orion/reopen-after-rest"
#define TITLE_PATH "/data/orion/title.txt"
#define LOG_PATH "/data/orion/resume.txt"
#define TICK_PATH "/data/orion/resume-tick.txt" // rewritten every tick: was it alive, and when
#define FALLBACK_TITLE "PPSA99061"
#define WATCH_PORT 10161     // held while a watcher runs: one at a time
#define TICK_S 2             // how often it looks at the clock
#define REST_GAP_S 8         // a tick this much longer than asked was a rest (one of 21 s was seen)
#define SETTLE_S 6           // after a wake: the home screen comes back first
#define USER_WAIT_S 120      // waiting for someone to sign in after the wake
#define LAUNCH_WAIT_S 300 // a wake often stops on the profile screen: until one is picked

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

// The clocks now, rewritten each tick: after a rest, the file shows whether
// the watcher kept running through it, was frozen, or was ended.
static void tick(long long wall, long long up)
{
    char text[96];
    const int length = snprintf(text, sizeof(text), "wall %lld up %lld pid %d\n", wall, up,
                                (int)getpid());
    const int fd = open(TICK_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return;
    write(fd, text, (size_t)length);
    close(fd);
}

static long long uptime_s(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long long)now.tv_sec;
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

static struct sockaddr_in watch_address(void)
{
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(WATCH_PORT);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return address;
}

static int try_claim(void)
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    const int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    struct sockaddr_in address = watch_address();
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(fd, 1) != 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

// Holds WATCH_PORT for the life of the process. A watcher already holding
// it is asked to end (a connection says so) and the port is taken over;
// -1 when it does not let go (an older watcher that does not listen).
static int claim(void)
{
    int fd = try_claim();
    if (fd >= 0)
        return fd;
    const int knock = socket(AF_INET, SOCK_STREAM, 0);
    if (knock >= 0)
    {
        struct sockaddr_in address = watch_address();
        connect(knock, (struct sockaddr *)&address, sizeof(address));
        close(knock);
    }
    for (int tries = 0; tries < 20 && fd < 0; ++tries)
    {
        usleep(250 * 1000);
        fd = try_claim();
    }
    return fd;
}

// Sleeps for a tick, or less when another watcher knocks: 1 then.
static int knocked(int held)
{
    fd_set ready;
    FD_ZERO(&ready);
    FD_SET(held, &ready);
    struct timeval wait = {TICK_S, 0};
    if (select(held + 1, &ready, NULL, NULL, &wait) <= 0)
        return 0;
    const int other = accept(held, NULL, NULL);
    if (other >= 0)
        close(other);
    return 1;
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
    // The system refuses a title until a profile is picked on the screen a
    // wake can stop on (0x8094000a, seen on a console), so it is asked again
    // every couple of seconds; anything coming to the front ends the wait.
    int last = 0;
    for (int waited = 0; waited < LAUNCH_WAIT_S; waited += 2)
    {
        if (sceUserServiceGetForegroundUser(&user) != 0)
            user = 0;
        launch_param_t param;
        memset(&param, 0, sizeof(param));
        param.structsize = sizeof(param);
        param.user_id = (uint32_t)user;
        const int result = sceSystemServiceLaunchApp(title, NULL, &param);
        if (result != last || result >= 0)
            say("opening %s after %d s: 0x%08x", title, waited, (unsigned)result);
        last = result;
        if (result >= 0)
            return;
        sleep(2);
        const int app = sceSystemServiceGetAppIdOfRunningBigApp();
        if (app > 0)
        {
            say("an app came to the front (0x%08x): not opening ORION", (unsigned)app);
            return;
        }
    }
    say("the system did not take %s in %d s: giving up until the next wake", title,
        LAUNCH_WAIT_S);
}

int main(void)
{
    if (!exists(ON_PATH))
        return 0;
    const int held = claim();
    if (held < 0)
    {
        say("an older watcher is running and does not let go (pid %d here)", (int)getpid());
        return 0;
    }
    sceUserServiceInitialize(NULL);
    say("watching for the console to wake (pid %d)", (int)getpid());
    time_t last = time(NULL);
    long long last_up = uptime_s();
    for (;;)
    {
        if (knocked(held))
        {
            say("a newer watcher takes over (pid %d ends)", (int)getpid());
            break;
        }
        const time_t now = time(NULL);
        const long long up = uptime_s();
        const long long gap = (long long)(now - last) - TICK_S;
        if (gap > 5 || up - last_up > TICK_S + 5)
            say("tick gap: wall +%lld s, uptime +%lld s", (long long)(now - last), up - last_up);
        last = now;
        last_up = up;
        tick((long long)now, up);
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
