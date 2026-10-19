// ps5-homebrew-ui - ORION's launch helper: a payload that starts a title.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A title cannot start while another app is in front: from inside ORION the
// system answers 0x8094000f. So ORION writes the title to start into
// /data/orion/launch-request.txt, sends this payload to the console's ELF
// loader (127.0.0.1:9021) and closes itself. The payload runs outside any
// app: it waits until no app is in front, then asks the system to start the
// title for the foreground user, with any arguments the request names (an
// emulator's core and game). ShadowMountPlus mounts the title on its way
// up, as it does for a launch from the home screen. Every step is appended to
// /data/orion/launch-helper.txt: a file of its own, since launch.txt belongs to
// ORION's sandbox and the helper may not be allowed to write to it.
//
// A request line "@args <path>" is not an argument: it names an args.txt
// ORION wrote for this one start (the RetroArch port takes `--ps5-mode=game`
// from /app0/args.txt, not from the launch's arguments). Once the title has
// had time to read it, the helper removes it, so the next start from the
// home screen is an ordinary one.

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
#define MAX_ARGUMENTS 16
#define ARGS_LINE "@args "
#define ARGS_CONTENT "--ps5-mode=game"
#define ARGS_READ_MS 20000

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
// Removes an args.txt ORION wrote, and nothing else: the path must be an
// args.txt under /data/homebrew and the file must still say what ORION put
// there.
static void remove_args(const char *path)
{
    const size_t length = strlen(path);
    if (strncmp(path, "/data/homebrew/", 15) != 0 || length < 9 ||
        strcmp(path + length - 9, "/args.txt") != 0 || strstr(path, "..") != NULL)
    {
        say("not removing %s: not an args.txt under /data/homebrew", path);
        return;
    }
    char content[64];
    memset(content, 0, sizeof(content));
    FILE *file = fopen(path, "r");
    if (file == NULL)
    {
        say("%s is already gone", path);
        return;
    }
    fread(content, 1, sizeof(content) - 1, file);
    fclose(file);
    if (strncmp(content, ARGS_CONTENT, strlen(ARGS_CONTENT)) != 0)
    {
        say("not removing %s: someone else's arguments", path);
        return;
    }
    say(remove(path) == 0 ? "removed %s" : "could not remove %s", path);
}

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
    char *argv[MAX_ARGUMENTS + 1]; // NULL-terminated; argv[0] NULL when there are none
    launch_param_t param;
    volatile int done;
    volatile int result;
} launch_job_t;

// The launch request can block while the system brings the title up, so it
// runs on its own thread and the main one keeps the log going.
static void *launch_thread(void *argument)
{
    launch_job_t *job = (launch_job_t *)argument;
    job->result =
        sceSystemServiceLaunchApp(job->title, job->argv[0] != NULL ? job->argv : NULL, &job->param);
    job->done = 1;
    return NULL;
}

int main(void)
{
    // The request: the title ID on the first line, then one argument per
    // line (ORION writes "-L", the core and the game for an emulator).
    static char request[4096];
    memset(request, 0, sizeof(request));
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
    char *lines[MAX_ARGUMENTS + 2];
    int count = 0;
    for (char *line = request; *line != '\0' && count < MAX_ARGUMENTS + 1;)
    {
        char *end = strchr(line, '\n');
        if (end != NULL)
            *end = '\0';
        const size_t length = strlen(line);
        if (length > 0 && line[length - 1] == '\r')
            line[length - 1] = '\0';
        if (line[0] != '\0')
            lines[count++] = line;
        if (end == NULL)
            break;
        line = end + 1;
    }
    if (count == 0 || strlen(lines[0]) >= sizeof(job.title) || !is_title_id(lines[0]))
    {
        say("request is not a title ID: \"%.20s\"", count > 0 ? lines[0] : "");
        return 1;
    }
    strcpy(job.title, lines[0]);
    const char *args_file = NULL;
    int arguments = 0;
    for (int i = 1; i < count; ++i)
    {
        if (strncmp(lines[i], ARGS_LINE, strlen(ARGS_LINE)) == 0)
            args_file = lines[i] + strlen(ARGS_LINE);
        else
            job.argv[arguments++] = lines[i];
    }
    say("started for %s with %d argument(s)", job.title, arguments);
    for (int i = 0; i < arguments; ++i)
        say("  argument %d: %s", i + 1, job.argv[i]);
    if (args_file != NULL)
        say("  %s is for this start only", args_file);

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
    if (args_file != NULL)
    {
        // A refused start leaves nothing to read it: remove it now.
        if (!job.done || job.result == 0)
            usleep(ARGS_READ_MS * 1000);
        remove_args(args_file);
    }
    return job.done && job.result == 0 ? 0 : 1;
}
