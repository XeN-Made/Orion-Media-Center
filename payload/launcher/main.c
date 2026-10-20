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
// /data/orion/launch-helper.txt, or, where the helper may not write there,
// to launch-helper.txt in ORION's own folder.
//
// The request: the title ID on the first line, then one line each of
//   <argument>                 an argument for the title (the system hands
//                              the RetroArch port none, but others may read them)
//   @install <staged>\t<file>  for this one start the title is to read
//                              <staged> (a file ORION wrote in /data/orion) as
//                              its <file> (RetroArch's args.txt and
//                              test-run.txt). The original is kept as
//                              <file>.orion-backup (or <file>.orion-absent
//                              marks that there was none), the staged copy is
//                              written readable by every app, and once the
//                              title has read it the original comes back. The
//                              helper writes it because the title cannot read
//                              files ORION writes from inside its sandbox until
//                              it has repaired them, which is too late.
//   @notify <text>             a system notification once the title starts
//   @return <title ID>         when the title closes, start this one (ORION)
//   @record <id>\t<log>        when the title closes, append "<start> <end> <id>"
//                              (seconds since 1970) to <log>, a play-log.txt
//                              in ORION's folder: how long it was played
//   @shortcut                  experiment: while the title runs, see whether
//                              L3 + R3 held for two seconds can be read, and
//                              say so in a notification. Nothing is closed.
//
// The helper stays until the title closes when it has something to watch
// for, and ends at once otherwise.
//
// ORION also sends it at start without a launch, with
// /data/orion/backdrop-request.txt: the first line the folder to fill (its
// own backdrops folder), then one title ID per line. For each, the system's
// copy of the title's home-screen picture (/user/appmeta/<ID>/pic0.png or
// pic1.png), which ORION's sandbox cannot read, is copied there as <ID>.png;
// a title without one leaves an empty <ID>.none, so it is not asked again.

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define REQUEST_PATH "/data/orion/launch-request.txt"
#define BACKDROP_REQUEST_PATH "/data/orion/backdrop-request.txt"
#define MAX_PICTURE (24 << 20) // bytes of one home-screen picture
#define FRESH_REQUEST_S 120     // a launch request older than this is a leftover
#define LOG_PATH "/data/orion/launch-helper.txt"
// ORION's own folder, for when /data/orion does not take the helper's files.
#define LOG_FALLBACK "/data/homebrew/PPSA99061/launch-helper.txt"
#define WAIT_FOR_EXIT_MS 20000
#define WAIT_FOR_LAUNCH_MS 15000
#define WAIT_FOR_TITLE_MS 20000
#define TITLE_READS_MS 15000 // after it is up, before its files go back
#define GONE_FOR_MS 1000     // no app in front this long: the title has closed
#define POLL_MS 100
#define SHORTCUT_HOLD_MS 2000
#define MAX_ARGUMENTS 16
#define MAX_INSTALLS 4
#define MAX_STAGED 4096 // bytes of one staged file

// DualSense buttons as scePadReadState reports them (src/core/input.hpp).
#define PAD_L3 0x00000002u
#define PAD_R3 0x00000004u

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

// A system notification, as the payload SDK's hello_world sends one.
typedef struct notify_request
{
    char useless1[45];
    char message[3075];
} notify_request_t;

// scePadReadState's sample (src/platform/ps5/pad.cpp): the buttons come first.
typedef struct pad_state
{
    uint32_t buttons;
    uint8_t rest[116];
} pad_state_t;

int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceLaunchApp(const char *title_id, char **argv, launch_param_t *param);
int sceUserServiceInitialize(void *params);
int sceUserServiceGetForegroundUser(int *user);
int sceKernelSendNotificationRequest(int device, notify_request_t *request, size_t size, int blocking);
int scePadInit(void);
int scePadOpen(int user, int type, int index, const void *parameters);
int scePadGetHandle(int user, int type, int index);
int scePadReadState(int handle, pad_state_t *state);

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

static void notify(const char *text)
{
    static notify_request_t request;
    memset(&request, 0, sizeof(request));
    strncpy(request.message, text, sizeof(request.message) - 1);
    const int result = sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
    say("notification \"%s\": 0x%08x", text, (unsigned)result);
}

static int ends_with(const char *text, const char *end)
{
    const size_t length = strlen(text);
    const size_t tail = strlen(end);
    return length > tail && strcmp(text + length - tail, end) == 0;
}

// Only args.txt, test-run.txt (and config/frontend.cfg, which earlier builds
// changed) of a title under /data/homebrew.
static int changeable(const char *path)
{
    return strncmp(path, "/data/homebrew/", 15) == 0 && strstr(path, "..") == NULL &&
           strlen(path) <= 400 &&
           (ends_with(path, "/args.txt") || ends_with(path, "/test-run.txt") ||
            ends_with(path, "/config/frontend.cfg"));
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

// Up to size bytes of a file; -1 when it cannot be read.
static long read_up_to(const char *path, char *bytes, size_t size)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return -1;
    const size_t got = fread(bytes, 1, size, file);
    fclose(file);
    return (long)got;
}

// One file changed for this start, and what this helper wrote there.
typedef struct installed
{
    const char *staged;
    const char *path;
    char bytes[MAX_STAGED];
    long size;
    int done;
} installed_t;

// Keeps the original of <path> aside and writes the staged copy in its
// place. A backup still there from an earlier start is the original: it stays.
static int install(installed_t *file)
{
    if (!changeable(file->path) || strncmp(file->staged, "/data/orion/", 12) != 0 ||
        strstr(file->staged, "..") != NULL)
    {
        say("not installing %s: not a file ORION changes", file->path);
        return 0;
    }
    file->size = read_up_to(file->staged, file->bytes, sizeof(file->bytes));
    if (file->size < 0)
    {
        say("cannot read %s", file->staged);
        return 0;
    }
    char backup[512];
    char absent[512];
    snprintf(backup, sizeof(backup), "%s.orion-backup", file->path);
    snprintf(absent, sizeof(absent), "%s.orion-absent", file->path);
    if (access(backup, F_OK) != 0 && access(absent, F_OK) != 0)
    {
        if (access(file->path, F_OK) == 0)
        {
            if (rename(file->path, backup) != 0)
            {
                say("could not keep %s aside", file->path);
                return 0;
            }
        }
        else if (!write_shared(absent, "", 0))
        {
            say("could not mark %s as new", file->path);
            return 0;
        }
    }
    if (!write_shared(file->path, file->bytes, (size_t)file->size))
    {
        say("could not write %s", file->path);
        return 0;
    }
    file->done = 1;
    say("installed %s (%ld bytes)", file->path, file->size);
    return 1;
}

// Puts back a file ORION changed for one start, unless a later start has
// written its own there since (ORION's copies carry a token of their start):
// that start puts it back itself.
static void restore(installed_t *file)
{
    if (!file->done)
        return;
    file->done = 0;
    static char now[MAX_STAGED];
    const long size = read_up_to(file->path, now, sizeof(now));
    if (size >= 0 && (size != file->size || memcmp(now, file->bytes, (size_t)size) != 0))
    {
        say("%s belongs to a later start now; leaving it", file->path);
        return;
    }
    char backup[512];
    char absent[512];
    snprintf(backup, sizeof(backup), "%s.orion-backup", file->path);
    snprintf(absent, sizeof(absent), "%s.orion-absent", file->path);
    if (access(backup, F_OK) == 0)
        say(rename(backup, file->path) == 0 ? "restored %s" : "could not restore %s",
            file->path);
    else if (access(absent, F_OK) == 0)
    {
        remove(file->path);
        remove(absent);
        say("removed %s (there was none before)", file->path);
    }
}

// One session in the play log ORION reads: "<start> <end> <id>".
static void record(const char *id, const char *log, long long from)
{
    if (id == NULL || log == NULL)
        return;
    if (strncmp(log, "/data/homebrew/", 15) != 0 || strstr(log, "..") != NULL ||
        !ends_with(log, "/play-log.txt") || strchr(id, ' ') != NULL || strlen(id) > 40)
    {
        say("not recording to %s", log);
        return;
    }
    const long long to = (long long)time(NULL);
    char line[128];
    const int length = snprintf(line, sizeof(line), "%lld %lld %s\n", from, to, id);
    const int fd = open(log, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0 || length <= 0)
    {
        say("could not record the session in %s", log);
        return;
    }
    write(fd, line, (size_t)length);
    fchmod(fd, 0666);
    close(fd);
    say("recorded %lld s of %s", to - from, id);
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

// Starts a title for the foreground user. A start the system takes answers
// the new app's ID (0x00002018 on a console), not 0; a refusal is a negative
// error code (0x8...). Returns the answer, or -1 when there was none in time.
static int launch(launch_job_t *job)
{
    int user = 0;
    if (sceUserServiceGetForegroundUser(&user) != 0)
        say("no foreground user; launching for user 0");
    job->param.user_id = (uint32_t)user;
    say("launching %s for user 0x%08x", job->title, (unsigned)user);
    pthread_t thread;
    if (pthread_create(&thread, NULL, launch_thread, job) != 0)
    {
        say("could not start the launch thread");
        return -1;
    }
    int waited = 0;
    for (; !job->done && waited < WAIT_FOR_LAUNCH_MS; waited += 100)
        usleep(100 * 1000);
    if (!job->done)
    {
        say("system has not answered after %d ms; the title may still be starting", waited);
        return -1;
    }
    say("system answered 0x%08x (%s)", (unsigned)job->result,
        job->result >= 0 ? "accepted" : "refused");
    return job->result;
}

// The experiment: can a payload read the controller while a game has it?
typedef struct shortcut
{
    int handle;    // < 0: none
    int held_ms;   // how long L3 + R3 have been down
    int reads;     // successful reads, for the log
    int failures;  // failed reads, for the log
    int announced; // this hold has been reported
} shortcut_t;

static void shortcut_open(shortcut_t *shortcut)
{
    memset(shortcut, 0, sizeof(*shortcut));
    const int init = scePadInit();
    int user = 0;
    sceUserServiceGetForegroundUser(&user);
    shortcut->handle = scePadOpen(user, 0, 0, NULL);
    say("shortcut: scePadInit 0x%08x, scePadOpen 0x%08x", (unsigned)init,
        (unsigned)shortcut->handle);
    if (shortcut->handle < 0)
    {
        shortcut->handle = scePadGetHandle(user, 0, 0);
        say("shortcut: scePadGetHandle 0x%08x", (unsigned)shortcut->handle);
    }
}

static void shortcut_poll(shortcut_t *shortcut)
{
    if (shortcut->handle < 0)
        return;
    pad_state_t state;
    memset(&state, 0, sizeof(state));
    const int result = scePadReadState(shortcut->handle, &state);
    if (result != 0)
    {
        if (shortcut->failures++ == 0)
            say("shortcut: scePadReadState 0x%08x", (unsigned)result);
        return;
    }
    if (shortcut->reads++ == 0)
        say("shortcut: the controller can be read (buttons 0x%08x)", (unsigned)state.buttons);
    const uint32_t both = PAD_L3 | PAD_R3;
    if ((state.buttons & both) != both)
    {
        shortcut->held_ms = 0;
        shortcut->announced = 0;
        return;
    }
    shortcut->held_ms += POLL_MS;
    if (shortcut->held_ms >= SHORTCUT_HOLD_MS && !shortcut->announced)
    {
        shortcut->announced = 1;
        say("shortcut: L3 + R3 held for %d ms", shortcut->held_ms);
        notify("ORION saw L3 + R3 (shortcut test: nothing is closed)");
    }
}

// Copies one file, readable by every app; 0 on failure.
static int copy_shared(const char *from, const char *to)
{
    char *bytes = malloc(MAX_PICTURE);
    if (bytes == NULL)
        return 0;
    const long size = read_up_to(from, bytes, MAX_PICTURE);
    const int ok = size > 0 && size < MAX_PICTURE && write_shared(to, bytes, (size_t)size);
    free(bytes);
    return ok;
}

// The backdrop request (see the top of this file). True when there was one.
static int copy_backdrops(void)
{
    static char request[8192];
    memset(request, 0, sizeof(request));
    if (read_up_to(BACKDROP_REQUEST_PATH, request, sizeof(request) - 1) < 0)
        return 0;
    remove(BACKDROP_REQUEST_PATH);
    char *line = strtok(request, "\n");
    const char *folder = line;
    if (folder == NULL || strncmp(folder, "/data/homebrew/", 15) != 0 ||
        strstr(folder, "..") != NULL || !ends_with(folder, "/backdrops") || strlen(folder) > 300)
    {
        say("backdrops: not a backdrops folder: %.60s", folder != NULL ? folder : "");
        return 1;
    }
    int checked = 0;
    mkdir(folder, 0777);
    chmod(folder, 0777);
    int copied = 0;
    int none = 0;
    while ((line = strtok(NULL, "\n")) != NULL)
    {
        if (!is_title_id(line))
            continue;
        char to[400];
        char from[128];
        int done = 0;
        for (int i = 0; i < 2 && !done; ++i)
        {
            snprintf(from, sizeof(from), "/user/appmeta/%s/pic%d.png", line, i);
            snprintf(to, sizeof(to), "%s/%s.png", folder, line);
            done = access(from, R_OK) == 0 && copy_shared(from, to);
            // The first title says whether /user can be seen from here at all.
            if (!done && checked++ == 0)
                say("backdrops: cannot read %s (errno %d)", from, errno);
        }
        if (done)
            ++copied;
        else
        {
            snprintf(to, sizeof(to), "%s/%s.none", folder, line);
            write_shared(to, "", 0);
            ++none;
        }
    }
    say("backdrops: copied %d, none for %d", copied, none);
    return 1;
}

int main(void)
{
    // A run for the backdrops does that and nothing else: never a launch.
    if (copy_backdrops())
        return 0;
    // The helper may not remove ORION's request after reading it, so an old
    // one can still be there: only a fresh one is a launch.
    struct stat info;
    if (stat(REQUEST_PATH, &info) != 0)
    {
        say("no request at " REQUEST_PATH "; nothing to do");
        return 1;
    }
    const long long age = (long long)time(NULL) - (long long)info.st_mtime;
    if (age > FRESH_REQUEST_S)
    {
        say("the launch request is %lld s old: a leftover, not launching", age);
        return 1;
    }
    static char request[4096];
    memset(request, 0, sizeof(request));
    if (read_up_to(REQUEST_PATH, request, sizeof(request) - 1) < 0)
        return 1;
    // A request is used once.
    remove(REQUEST_PATH);

    static launch_job_t job;
    memset(&job, 0, sizeof(job));
    static installed_t files[MAX_INSTALLS];
    int file_count = 0;
    const char *note = NULL;
    const char *return_to = NULL;
    int shortcut_test = 0;
    const char *record_id = NULL;
    const char *record_log = NULL;
    int line_number = 0;
    int arguments = 0;
    for (char *line = request; *line != '\0';)
    {
        char *end = strchr(line, '\n');
        if (end != NULL)
            *end = '\0';
        const size_t length = strlen(line);
        if (length > 0 && line[length - 1] == '\r')
            line[length - 1] = '\0';
        if (line[0] == '\0')
        {
            // blank lines say nothing
        }
        else if (line_number++ == 0)
        {
            if (strlen(line) >= sizeof(job.title) || !is_title_id(line))
            {
                say("request is not a title ID: \"%.20s\"", line);
                return 1;
            }
            strcpy(job.title, line);
        }
        else if (strncmp(line, "@install ", 9) == 0)
        {
            char *tab = strchr(line, '\t');
            if (tab != NULL && file_count < MAX_INSTALLS)
            {
                *tab = '\0';
                files[file_count].staged = line + 9;
                files[file_count++].path = tab + 1;
            }
        }
        else if (strncmp(line, "@notify ", 8) == 0)
            note = line + 8;
        else if (strncmp(line, "@return ", 8) == 0 && is_title_id(line + 8))
            return_to = line + 8;
        else if (strncmp(line, "@record ", 8) == 0 && strchr(line, '\t') != NULL)
        {
            char *tab = strchr(line, '\t');
            *tab = '\0';
            record_id = line + 8;
            record_log = tab + 1;
        }
        else if (strcmp(line, "@shortcut") == 0)
            shortcut_test = 1;
        else if (arguments < MAX_ARGUMENTS)
            job.argv[arguments++] = line;
        if (end == NULL)
            break;
        line = end + 1;
    }
    if (job.title[0] == '\0')
    {
        say("empty request");
        return 1;
    }
    say("started for %s with %d argument(s)%s%s", job.title, arguments,
        return_to != NULL ? ", returning to " : "", return_to != NULL ? return_to : "");
    for (int i = 0; i < arguments; ++i)
        say("  argument %d: %s", i + 1, job.argv[i]);
    for (int i = 0; i < file_count; ++i)
        say("  %s is for this start only", files[i].path);

    // ORION closes itself after sending this payload; wait until no app is
    // in front before asking for the next one.
    int waited = 0;
    int app = sceSystemServiceGetAppIdOfRunningBigApp();
    for (; app > 0 && waited < WAIT_FOR_EXIT_MS; waited += 200)
    {
        usleep(200 * 1000);
        app = sceSystemServiceGetAppIdOfRunningBigApp();
    }
    if (app > 0)
    {
        say("an app (0x%08x) is still in front after %d ms; not launching", (unsigned)app, waited);
        return 1;
    }
    say("no app in front after %d ms", waited);
    for (int i = 0; i < file_count; ++i)
    {
        if (!install(&files[i]))
        {
            // Half a change is worse than none: put back what was done.
            for (int j = 0; j < i; ++j)
                restore(&files[j]);
            say("not launching: the title would not get what it needs");
            return 1;
        }
    }
    usleep(500 * 1000); // let the system finish tearing the last one down
    sceUserServiceInitialize(NULL);
    // -1: no answer yet (the title may still come up); below that, refused.
    if (launch(&job) < -1)
    {
        for (int i = 0; i < file_count; ++i)
            restore(&files[i]);
        return 1;
    }

    // Wait for the title to be in front.
    for (waited = 0; sceSystemServiceGetAppIdOfRunningBigApp() <= 0 && waited < WAIT_FOR_TITLE_MS;
         waited += POLL_MS)
        usleep(POLL_MS * 1000);
    int title_app = sceSystemServiceGetAppIdOfRunningBigApp();
    const long long played_from = (long long)time(NULL);
    say("the title is in front after %d ms (app 0x%08x)", waited, (unsigned)title_app);
    if (note != NULL && title_app > 0)
        notify(note);

    // Then watch it: put its files back once it has read them, try the
    // shortcut, and notice when it closes.
    shortcut_t shortcut;
    shortcut.handle = -1;
    if (shortcut_test && title_app > 0)
        shortcut_open(&shortcut);
    int elapsed = 0;
    int gone = 0;
    int restored = file_count == 0;
    const int watching =
        (return_to != NULL || shortcut_test || record_id != NULL) && title_app > 0;
    while (!restored || watching)
    {
        usleep(POLL_MS * 1000);
        elapsed += POLL_MS;
        if (!restored && elapsed >= TITLE_READS_MS)
        {
            for (int i = 0; i < file_count; ++i)
                restore(&files[i]);
            restored = 1;
        }
        if (!watching)
            continue;
        const int now = sceSystemServiceGetAppIdOfRunningBigApp();
        if (now == title_app)
        {
            gone = 0;
            shortcut_poll(&shortcut);
            continue;
        }
        if (now > 0)
        {
            // The player started something else: not ours to follow.
            say("another app (0x%08x) is in front; not returning", (unsigned)now);
            record(record_id, record_log, played_from);
            break;
        }
        gone += POLL_MS;
        if (gone < GONE_FOR_MS)
            continue;
        say("the title has closed");
        record(record_id, record_log, played_from);
        if (return_to != NULL)
        {
            static launch_job_t back;
            memset(&back, 0, sizeof(back));
            strcpy(back.title, return_to);
            launch(&back);
        }
        break;
    }
    if (!restored)
    {
        for (int i = 0; i < file_count; ++i)
            restore(&files[i]);
    }
    say("done");
    return 0;
}
