// ps5-homebrew-ui - ORION's launch helper: a payload that starts a title.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A title cannot start while another app is in front: from inside ORION the
// system answers 0x8094000f. So ORION writes the title to start into
// /data/orion/launch-request.txt, sends this payload to the console's ELF
// loader (127.0.0.1:9021) and closes itself. The payload runs outside any
// app: it waits until no app is in front, then asks the system to start the
// title for the foreground user. ShadowMountPlus mounts the title on its way
// up, as it does for a launch from the home screen. Every step is appended to
// /data/orion/launch-helper.txt: a file of its own, since launch.txt belongs to
// ORION's sandbox and the helper may not be allowed to write to it.

#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define REQUEST_PATH "/data/orion/launch-request.txt"
#define LOG_PATH "/data/orion/launch-helper.txt"
#define WAIT_FOR_EXIT_MS 20000
#define WAIT_FOR_LAUNCH_MS 15000

// The launch parameters as the PS5 homebrew loader passes them: only the
// user is filled in.
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
    int length = snprintf(line, sizeof(line), "%lld  helper  ", (long long)time(NULL));
    va_list arguments;
    va_start(arguments, format);
    length += vsnprintf(line + length, sizeof(line) - (size_t)length - 1, format, arguments);
    va_end(arguments);
    if (length > (int)sizeof(line) - 2)
        length = (int)sizeof(line) - 2;
    line[length++] = '\n';
    // Plain appends, readable by everyone: the PC reads this file over FTP.
    const int fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        return;
    write(fd, line, (size_t)length);
    close(fd);
}

// Four capital letters and five digits.
static int is_title_id(const char *text)
{
    if (strlen(text) != 9)
        return 0;
    for (int i = 0; i < 4; ++i)
    {
        if (text[i] < 'A' || text[i] > 'Z')
            return 0;
    }
    for (int i = 4; i < 9; ++i)
    {
        if (text[i] < '0' || text[i] > '9')
            return 0;
    }
    return 1;
}

typedef struct launch_job
{
    char title[16];
    launch_param_t param;
    volatile int done;
    volatile int result;
} launch_job_t;

// The launch request can block while the system brings the title up, so it
// runs on its own thread and the main one keeps the log going.
static void *launch_thread(void *argument)
{
    launch_job_t *job = (launch_job_t *)argument;
    job->result = sceSystemServiceLaunchApp(job->title, NULL, &job->param);
    job->done = 1;
    return NULL;
}

int main(void)
{
    char request[64] = {0};
    FILE *file = fopen(REQUEST_PATH, "r");
    if (file == NULL)
    {
        say("no request at " REQUEST_PATH "; nothing to do");
        return 1;
    }
    const size_t got = fread(request, 1, sizeof(request) - 1, file);
    fclose(file);
    request[got] = '\0';
    // A request is used once.
    remove(REQUEST_PATH);

    static launch_job_t job;
    memset(&job, 0, sizeof(job));
    if (sscanf(request, "%15s", job.title) != 1 || !is_title_id(job.title))
    {
        say("request is not a title ID: \"%.20s\"", request);
        return 1;
    }
    say("started for %s", job.title);

    // ORION closes itself after sending this payload; wait until no app is
    // in front before asking for the next one.
    int waited = 0;
    int app = sceSystemServiceGetAppIdOfRunningBigApp();
    while (app > 0 && waited < WAIT_FOR_EXIT_MS)
    {
        usleep(200 * 1000);
        waited += 200;
        app = sceSystemServiceGetAppIdOfRunningBigApp();
    }
    if (app > 0)
    {
        say("an app (0x%08x) is still in front after %d ms; not launching", (unsigned)app,
            waited);
        return 1;
    }
    say("no app in front after %d ms", waited);
    usleep(500 * 1000); // let the system finish tearing the last one down

    int user = 0;
    sceUserServiceInitialize(NULL);
    if (sceUserServiceGetForegroundUser(&user) != 0)
        say("no foreground user; launching for user 0");
    job.param.user_id = (uint32_t)user;
    say("launching %s for user 0x%08x", job.title, (unsigned)user);

    pthread_t thread;
    if (pthread_create(&thread, NULL, launch_thread, &job) != 0)
    {
        say("could not start the launch thread");
        return 1;
    }
    for (waited = 0; !job.done && waited < WAIT_FOR_LAUNCH_MS; waited += 100)
        usleep(100 * 1000);
    if (job.done)
        say("system answered 0x%08x (%s)", (unsigned)job.result,
            job.result == 0 ? "accepted" : "refused");
    else
        say("system has not answered after %d ms; the title may still be starting", waited);
    return job.done && job.result == 0 ? 0 : 1;
}
