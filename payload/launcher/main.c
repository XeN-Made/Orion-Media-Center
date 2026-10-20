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
// A request line "@install <staged>\t<file>" is not an argument either: for
// this one start the title is to read <staged> (a file ORION wrote in
// /data/orion) as its <file> (RetroArch's args.txt and frontend.cfg). The
// helper keeps the original as <file>.orion-backup (or leaves
// <file>.orion-absent when there was none), writes the staged copy, readable
// by every app, and once the title is up and has read it puts the original
// back. It does the writing because the title cannot read files ORION writes
// there from inside its sandbox until it has repaired them, too late.

#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define REQUEST_PATH "/data/orion/launch-request.txt"
#define LOG_PATH "/data/orion/launch-helper.txt"
// ORION's own folder, for when /data/orion does not take the helper's files.
#define LOG_FALLBACK "/data/homebrew/PPSA99061/launch-helper.txt"
#define WAIT_FOR_EXIT_MS 20000
#define WAIT_FOR_LAUNCH_MS 15000
#define MAX_ARGUMENTS 16
#define MAX_RESTORES 4
#define INSTALL_LINE "@install "
#define WAIT_FOR_TITLE_MS 20000
#define TITLE_READS_MS 15000

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
    int fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        fd = open(LOG_FALLBACK, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        return;
    write(fd, line, (size_t)length);
    close(fd);
}

// Four capital letters and five digits.
// Only args.txt and config/frontend.cfg of a title under /data/homebrew.
static int changeable(const char *path)
{
    const size_t length = strlen(path);
    const int args = length > 9 && strcmp(path + length - 9, "/args.txt") == 0;
    const int frontend = length > 20 && strcmp(path + length - 20, "/config/frontend.cfg") == 0;
    return strncmp(path, "/data/homebrew/", 15) == 0 && strstr(path, "..") == NULL &&
           length <= 400 && (args || frontend);
}

// Writes bytes as a file every app can read, whatever the umask.
static int write_shared(const char *path, const char *bytes, size_t size)
{
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return 0;
    const int whole = write(fd, bytes, size) == (ssize_t)size;
    fchmod(fd, 0666);
    close(fd);
    return whole;
}

// Keeps the original of <path> aside and writes the staged copy in its
// place. A backup still there from an earlier start is the original: it stays.
static int install(const char *staged, const char *path)
{
    if (!changeable(path) || strncmp(staged, "/data/orion/", 12) != 0 ||
        strstr(staged, "..") != NULL)
    {
        say("not installing %s: not a file ORION changes", path);
        return 0;
    }
    static char bytes[65536];
    FILE *file = fopen(staged, "rb");
    if (file == NULL)
    {
        say("cannot read %s", staged);
        return 0;
    }
    const size_t size = fread(bytes, 1, sizeof(bytes), file);
    fclose(file);
    char backup[512];
    char absent[512];
    snprintf(backup, sizeof(backup), "%s.orion-backup", path);
    snprintf(absent, sizeof(absent), "%s.orion-absent", path);
    if (access(backup, F_OK) != 0 && access(absent, F_OK) != 0)
    {
        if (access(path, F_OK) == 0)
        {
            if (rename(path, backup) != 0)
            {
                say("could not keep %s aside", path);
                return 0;
            }
        }
        else if (!write_shared(absent, "", 0))
        {
            say("could not mark %s as new", path);
            return 0;
        }
    }
    if (!write_shared(path, bytes, size))
    {
        say("could not write %s", path);
        return 0;
    }
    say("installed %s (%u bytes)", path, (unsigned)size);
    return 1;
}

// Puts back a file ORION changed for one start.
static void restore(const char *path)
{
    if (!changeable(path))
    {
        say("not restoring %s: not a file ORION changes", path);
        return;
    }
    char backup[512];
    char absent[512];
    snprintf(backup, sizeof(backup), "%s.orion-backup", path);
    snprintf(absent, sizeof(absent), "%s.orion-absent", path);
    if (access(backup, F_OK) == 0)
        say(rename(backup, path) == 0 ? "restored %s" : "could not restore %s", path);
    else if (access(absent, F_OK) == 0)
    {
        remove(path);
        remove(absent);
        say("removed %s (there was none before)", path);
    }
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
    const char *restores[MAX_RESTORES];
    const char *staged[MAX_RESTORES];
    int restore_count = 0;
    int arguments = 0;
    for (int i = 1; i < count; ++i)
    {
        if (strncmp(lines[i], INSTALL_LINE, strlen(INSTALL_LINE)) == 0)
        {
            // "<staged>\t<file>"
            char *tab = strchr(lines[i], '\t');
            if (tab != NULL && restore_count < MAX_RESTORES)
            {
                *tab = '\0';
                staged[restore_count] = lines[i] + strlen(INSTALL_LINE);
                restores[restore_count++] = tab + 1;
            }
        }
        else
            job.argv[arguments++] = lines[i];
    }
    say("started for %s with %d argument(s)", job.title, arguments);
    for (int i = 0; i < arguments; ++i)
        say("  argument %d: %s", i + 1, job.argv[i]);
    for (int i = 0; i < restore_count; ++i)
        say("  %s is for this start only", restores[i]);

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
        for (int i = 0; i < restore_count; ++i)
            restore(restores[i]);
        return 1;
    }
    say("no app in front after %d ms", waited);
    for (int i = 0; i < restore_count; ++i)
    {
        if (!install(staged[i], restores[i]))
        {
            // Half a change is worse than none: put back what was done.
            for (int j = 0; j <= i; ++j)
                restore(restores[j]);
            say("not launching: the title would not get its game");
            return 1;
        }
    }
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
    if (restore_count > 0)
    {
        // Give the title time to come up and read the files, then put them back.
        if (!job.done || job.result == 0)
        {
            for (waited = 0; sceSystemServiceGetAppIdOfRunningBigApp() <= 0 &&
                             waited < WAIT_FOR_TITLE_MS;
                 waited += 200)
                usleep(200 * 1000);
            usleep(TITLE_READS_MS * 1000);
        }
        for (int i = 0; i < restore_count; ++i)
            restore(restores[i]);
    }
    return job.done && job.result == 0 ? 0 : 1;
}
