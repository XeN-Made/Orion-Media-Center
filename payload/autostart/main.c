// ORION Media Center - ORION's autostart: a payload that opens ORION once the jailbreak is done.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// For a jailbreak's autoloader: add this ELF to the payloads it sends after
// the exploit (it ships as /data/homebrew/PPSA99061/assets/payloads/
// orion-autostart.elf). A title cannot start while another app is in
// front, and a browser-based jailbreak still has the browser in front when
// its payloads run, so the payload waits:
//
//   - nothing in front (the autoloader ran from the home screen): ORION
//     opens after a moment, once the system has settled;
//   - an app in front (the browser): ORION opens once it has closed;
//   - that app gives way to another (a game started straight away): the
//     player chose something else, and the payload ends without opening ORION.
//
// It gives up after AUTOSTART_WINDOW_S, and does nothing at all while
// /data/orion/autostart-off exists (Settings in ORION writes that file, so
// autostart can be turned off without touching the autoloader). Every step
// is appended to /data/orion/autostart.txt.

#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define ORION_TITLE "PPSA99061"
#define OFF_PATH "/data/orion/autostart-off"
#define LOG_PATH "/data/orion/autostart.txt"
#define AUTOSTART_WINDOW_S (30 * 60) // waiting longer than this, the moment has passed
#define SETTLE_MS 3000               // nothing in front: let the home screen finish coming up
#define GONE_FOR_MS 1500             // nothing in front this long: the app has closed
#define POLL_MS 250
#define LAUNCH_TRIES 5

// The launch parameters as the PS5 homebrew loader passes them: only the
// user is filled in (payload/launcher/main.c).
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
    int length = snprintf(line, sizeof(line), "%lld  autostart  ", (long long)time(NULL));
    va_list arguments;
    va_start(arguments, format);
    length += vsnprintf(line + length, sizeof(line) - (size_t)length - 1, format, arguments);
    va_end(arguments);
    if (length > (int)sizeof(line) - 2)
        length = (int)sizeof(line) - 2;
    line[length++] = '\n';
    mkdir("/data/orion", 0777);
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

// A start the system takes answers the new app's ID (positive); a refusal
// is a negative error code. A refusal straight after the browser closed is
// the system still tearing it down, so it is tried again a few times.
static int open_orion(void)
{
    int user = 0;
    if (sceUserServiceGetForegroundUser(&user) != 0)
        say("no foreground user; launching for user 0");
    for (int attempt = 1; attempt <= LAUNCH_TRIES; ++attempt)
    {
        launch_param_t param;
        memset(&param, 0, sizeof(param));
        param.structsize = sizeof(param);
        param.user_id = (uint32_t)user;
        const int result = sceSystemServiceLaunchApp(ORION_TITLE, NULL, &param);
        say("launch %d of %d: 0x%08x", attempt, LAUNCH_TRIES, (unsigned)result);
        if (result >= 0)
            return 0;
        usleep(1000 * 1000);
    }
    return 1;
}

int main(void)
{
    if (exists(OFF_PATH))
    {
        say("off (%s): not opening ORION", OFF_PATH);
        return 0;
    }
    sceUserServiceInitialize(NULL);
    const time_t start = time(NULL);
    const int first = sceSystemServiceGetAppIdOfRunningBigApp();
    say("started; app in front 0x%08x", (unsigned)first);
    if (first <= 0)
    {
        usleep(SETTLE_MS * 1000);
        return open_orion();
    }
    int gone = 0;
    while (time(NULL) - start < AUTOSTART_WINDOW_S)
    {
        usleep(POLL_MS * 1000);
        const int app = sceSystemServiceGetAppIdOfRunningBigApp();
        if (app > 0 && app != first)
        {
            say("another app came to the front (0x%08x): not opening ORION", (unsigned)app);
            return 0;
        }
        gone = app <= 0 ? gone + POLL_MS : 0;
        if (gone >= GONE_FOR_MS)
        {
            say("0x%08x closed", (unsigned)first);
            return open_orion();
        }
    }
    say("waited %d s for 0x%08x to close: giving up", AUTOSTART_WINDOW_S, (unsigned)first);
    return 0;
}
