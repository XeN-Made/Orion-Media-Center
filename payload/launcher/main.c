// ORION Media Center - ORION's launch helper: a payload that starts a title.
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
//                              title has read it the original comes back
//                              (PS5SX2's disc-launch.txt: once it closes). The
//                              helper writes it because the title cannot read
//                              files ORION writes from inside its sandbox until
//                              it has repaired them, which is too late.
//   @notify <text>             a system notification once the title starts
//   @return <title ID>         when the title closes, start this one (ORION)
//   @record <id>\t<log>        when the title closes, append "<start> <end> <id>"
//                              (seconds since 1970) to <log>, a play-log.txt
//                              in ORION's folder: how long it was played
//   @update <stage>\t<folder>  before the title starts (ORION, updating
//                              itself): copy the files <stage>/files.txt lists
//                              over <folder>'s, keep the old ones beside the
//                              stage, refresh the home screen's pictures, and
//                              say how it went in result.txt (orion/updates.hpp)
//   @moved-from <title ID>\t<folder>
//                              with @update: ORION moving to a title ID of its
//                              own. <folder> is the old title's; the update
//                              goes into a new folder (the request's title),
//                              its play log comes along, and the new title
//                              starts once the system has registered it (the
//                              old one starts again if it never does)
//
// The helper stays until the title closes when it has something to watch
// for, and ends at once otherwise.


#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define REQUEST_PATH "/data/orion/launch-request.txt"
#define FRESH_REQUEST_S 120     // a launch request older than this is a leftover
#define LOG_PATH "/data/orion/launch-helper.txt"
// ORION's own folder, for when /data/orion does not take the helper's files.
#define LOG_FALLBACK "/data/homebrew/PPSA99061/launch-helper.txt"
#define WAIT_FOR_EXIT_MS 20000
#define SMP_PORT 10101 // ShadowMountPlus's API, for removing games
#define WAIT_FOR_LAUNCH_MS 15000
#define WAIT_FOR_TITLE_MS 20000
#define TITLE_READS_MS 15000 // after it is up, before its files go back
#define REFUSED_GRACE_MS 8000 // a refused title that still comes up, within this
#define GONE_FOR_MS 1000     // no app in front this long: the title has closed
#define POLL_MS 100
#define MAX_ARGUMENTS 16
#define MAX_INSTALLS 4
#define MAX_STAGED 4096 // bytes of one staged file

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

int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceLaunchApp(const char *title_id, char **argv, launch_param_t *param);
int sceUserServiceInitialize(void *params);
int sceUserServiceGetForegroundUser(int *user);
int sceKernelSendNotificationRequest(int device, notify_request_t *request, size_t size, int blocking);

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

// PS5SX2's launch note: it deletes the note once read, after a jailbreak
// that can take ten seconds, so the note is put back only when it closes.
static const char *const kPs5sx2Note = "/data/PCSX2/logs/disc-launch.txt";

// Only args.txt, test-run.txt (and config/frontend.cfg, which earlier builds
// changed) of a title under /data/homebrew, and PS5SX2's launch note.
static int changeable(const char *path)
{
    if (strcmp(path, kPs5sx2Note) == 0)
        return 1;
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

// ---- removing a game through ShadowMountPlus ----
//
// ShadowMountPlus refuses to touch games while one is open, and to it ORION
// is one ("a game is active or awaiting exit"). So ORION hands a removal to
// the helper and closes; with nothing in front, the helper asks
// ShadowMountPlus's API (127.0.0.1:10101, JSON over POST, "status": 0 when it
// worked) to delete the game's files (then watches the job to its end) and to
// take it off the home screen. It says how it went in a notification and in
// a result file ORION reads when it opens again.

// POSTs json to path on the API; the reply's body into body. -1 when the API
// did not answer.
static int smp_post(const char *path, const char *json, char *body, size_t size)
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    struct timeval timeout = {10, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(SMP_PORT);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (const struct sockaddr *)&address, sizeof(address)) != 0)
    {
        close(fd);
        return -1;
    }
    static char request[1024];
    const int length = snprintf(request, sizeof(request),
                                "POST %s HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: "
                                "application/json\r\nContent-Length: %zu\r\nConnection: "
                                "close\r\n\r\n%s",
                                path, strlen(json), json);
    if (length <= 0 || length >= (int)sizeof(request) || send(fd, request, (size_t)length, 0) != length)
    {
        close(fd);
        return -1;
    }
    static char raw[16384];
    size_t got = 0;
    for (;;)
    {
        const ssize_t n = recv(fd, raw + got, sizeof(raw) - 1 - got, 0);
        if (n <= 0)
            break;
        got += (size_t)n;
        if (got >= sizeof(raw) - 1)
            break;
    }
    close(fd);
    raw[got] = '\0';
    const char *start = strstr(raw, "\r\n\r\n");
    if (strncmp(raw, "HTTP/", 5) != 0 || start == NULL)
        return -1;
    // The replies are small JSON objects; a chunked one keeps its chunk
    // sizes between the pieces, which the field reading below steps over.
    snprintf(body, size, "%s", start + 4);
    return 0;
}

// Where a field's value starts in a JSON object: after "key" and its colon.
static const char *json_field(const char *json, const char *key)
{
    char quoted[64];
    snprintf(quoted, sizeof(quoted), "\"%s\"", key);
    const char *at = strstr(json, quoted);
    if (at == NULL)
        return NULL;
    at += strlen(quoted);
    while (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n')
        ++at;
    if (*at != ':')
        return NULL;
    ++at;
    while (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n')
        ++at;
    return at;
}

static double json_number(const char *json, const char *key, double fallback)
{
    const char *at = json_field(json, key);
    return at != NULL && (*at == '-' || (*at >= '0' && *at <= '9')) ? strtod(at, NULL) : fallback;
}

static int json_true(const char *json, const char *key)
{
    const char *at = json_field(json, key);
    return at != NULL && strncmp(at, "true", 4) == 0;
}

static void json_text(const char *json, const char *key, char *out, size_t size)
{
    out[0] = '\0';
    const char *at = json_field(json, key);
    if (at == NULL || *at != '"' || size == 0)
        return;
    ++at;
    size_t n = 0;
    while (*at != '\0' && *at != '"' && n + 1 < size)
    {
        if (*at == '\\' && at[1] != '\0')
            ++at;
        out[n++] = *at++;
    }
    out[n] = '\0';
}

// One call: 1 when the API said status 0; why not in error.
static int smp_call(const char *path, const char *json, char *body, size_t size, char *error,
                    size_t error_size)
{
    if (smp_post(path, json, body, size) != 0)
    {
        snprintf(error, error_size, "ShadowMountPlus did not answer (is it running?)");
        return 0;
    }
    if (json_number(body, "status", -1.0) != 0.0)
    {
        json_text(body, "error", error, error_size);
        if (error[0] == '\0')
            snprintf(error, error_size, "ShadowMountPlus said no");
        say("ShadowMountPlus %s: %s", path, error);
        return 0;
    }
    return 1;
}

// Whether ShadowMountPlus refused because a game is still open or closing:
// ORION itself, for a few seconds after it closes (seen on a console).
static int still_closing(const char *body, const char *reason_key, const char *error)
{
    char reason[32] = "";
    json_text(body, reason_key, reason, sizeof(reason));
    return strcmp(reason, "game_active") == 0 || strstr(error, "awaiting exit") != NULL ||
           strstr(error, "game is active") != NULL;
}

#define REMOVE_PATIENCE_S 45 // how long a refusal while ORION closes is asked again

// One try of remove_game; *busy says the refusal was only ORION closing.
static int remove_once(int erase, const char *id, const char *name, int first, int *busy,
                       char *error, size_t error_size)
{
    static char body[16384];
    *busy = 0;
    char json[128];
    snprintf(json, sizeof(json), "{\"title_id\": \"%s\"%s}", id, erase ? ", \"confirm\": true" : "");
    if (!erase)
    {
        const int ok = smp_call("/api/v1/games/uninstall", json, body, sizeof(body), error,
                                error_size);
        *busy = !ok && still_closing(body, "error_reason", error);
        say("uninstall %s: %s", id, ok ? "done" : error);
        return ok;
    }
    if (!smp_call("/api/v1/games/delete", json, body, sizeof(body), error, error_size))
    {
        *busy = still_closing(body, "error_reason", error);
        return 0;
    }
    int active = !json_true(body, "source_missing");
    say("delete %s: %s", id, active ? "job started" : "its files were already gone");
    char note[256];
    snprintf(note, sizeof(note), "Deleting %s\xe2\x80\xa6 ORION opens again when it is done", name);
    if (first)
        notify(note);
    int last_tenth = -1;
    for (int poll = 0; active && poll < 6 * 60 * 60; ++poll)
    {
        sleep(1);
        if (!smp_call("/api/v1/games/storage/status", "{}", body, sizeof(body), error, error_size))
            return 0;
        active = json_true(body, "active");
        const double done = json_number(body, "processed_bytes", 0.0);
        const double total = json_number(body, "total_bytes", 0.0);
        const int tenth = total > 0.0 ? (int)(done * 10.0 / total) : -1;
        if (active && tenth > last_tenth && tenth > 0)
        {
            // Every tenth of the way, so a long delete shows it is moving.
            last_tenth = tenth;
            snprintf(note, sizeof(note), "Deleting %s: %d%%", name, tenth * 10);
            notify(note);
        }
        if (!active && json_number(body, "result_status", 0.0) != 0.0)
        {
            json_text(body, "result_error", error, error_size);
            if (error[0] == '\0')
                snprintf(error, error_size, "the delete did not finish");
            *busy = still_closing(body, "result_error_reason", error);
            say("delete %s stopped: %s", id, error);
            return 0;
        }
    }
    if (active)
    {
        snprintf(error, error_size, "the delete is taking too long; ShadowMountPlus is still on it");
        return 0;
    }
    // The files are gone, which is what was asked: a refused uninstall now
    // (ShadowMountPlus may have let the title go itself) changes nothing.
    char ignored[160];
    snprintf(json, sizeof(json), "{\"title_id\": \"%s\"}", id);
    smp_call("/api/v1/games/uninstall", json, body, sizeof(body), ignored, sizeof(ignored));
    say("delete %s: done", id);
    return 1;
}

// erase: delete the files, watch the job, then uninstall. Otherwise uninstall
// only. 1 when it did what it was asked; why not in error. ORION has just
// closed when this runs, and ShadowMountPlus refuses any removal while it
// still counts ORION as closing, so that refusal is asked again for a while.
static int remove_game(int erase, const char *id, const char *name, char *error, size_t error_size)
{
    int busy = 0;
    for (int waited = 0;; waited += 2)
    {
        if (remove_once(erase, id, name, waited == 0, &busy, error, error_size))
            return 1;
        if (!busy || waited >= REMOVE_PATIENCE_S)
            return 0;
        say("remove %s: ORION is still closing; asking again", id);
        sleep(2);
    }
}

// "<kind> <ok> <title ID>\t<message>", for ORION to read when it opens.
static void write_result(const char *path, const char *kind, int ok, const char *id,
                         const char *message)
{
    if (path == NULL || strncmp(path, "/data/homebrew/", 15) != 0 || strstr(path, "..") != NULL ||
        !ends_with(path, "/remove-result.txt"))
    {
        say("not writing a result to %s", path != NULL ? path : "(nowhere)");
        return;
    }
    char line[512];
    const int length = snprintf(line, sizeof(line), "%s %d %s\t%s\n", kind, ok, id, message);
    if (length > 0)
        write_shared(path, line, (size_t)(length < (int)sizeof(line) ? length : (int)sizeof(line) - 1));
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

// ---- @update: ORION's own files, replaced while it is closed ----

#define UPDATE_ROOT "/data/orion/update/"
#define MAX_UPDATE_LIST (256 * 1024) // files.txt: a path a line
#define COPY_CHUNK (256 * 1024)

// Makes every folder of <base>/<relative>'s parent.
static int make_folders(const char *base, const char *relative)
{
    char path[1024];
    for (const char *slash = strchr(relative, '/'); slash != NULL; slash = strchr(slash + 1, '/'))
    {
        if (snprintf(path, sizeof(path), "%s/%.*s", base, (int)(slash - relative), relative) >=
            (int)sizeof(path))
            return 0;
        if (mkdir(path, 0777) != 0 && errno != EEXIST)
            return 0;
    }
    return 1;
}

// Copies a file, written beside its place first and renamed into it, so a
// reader never sees half of one. 1 when it worked.
static int copy_file(const char *from, const char *to)
{
    static char chunk[COPY_CHUNK];
    char temporary[1100];
    snprintf(temporary, sizeof(temporary), "%s.orion-new", to);
    const int in = open(from, O_RDONLY);
    if (in < 0)
        return 0;
    const int out = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (out < 0)
    {
        close(in);
        return 0;
    }
    int ok = 1;
    for (;;)
    {
        const ssize_t got = read(in, chunk, sizeof(chunk));
        if (got == 0)
            break;
        if (got < 0 || write(out, chunk, (size_t)got) != got)
        {
            ok = 0;
            break;
        }
    }
    fchmod(out, 0777);
    close(out);
    close(in);
    if (!ok || rename(temporary, to) != 0)
    {
        remove(temporary);
        return 0;
    }
    return 1;
}

// Whether two files hold the same bytes (both must be readable).
static int same_file(const char *a, const char *b)
{
    struct stat sa;
    struct stat sb;
    if (stat(a, &sa) != 0 || stat(b, &sb) != 0 || sa.st_size != sb.st_size)
        return 0;
    static char left[COPY_CHUNK];
    static char right[COPY_CHUNK];
    const int fa = open(a, O_RDONLY);
    const int fb = open(b, O_RDONLY);
    int same = fa >= 0 && fb >= 0;
    while (same)
    {
        const ssize_t got = read(fa, left, sizeof(left));
        if (got <= 0)
            break;
        same = read(fb, right, (size_t)got) == got && memcmp(left, right, (size_t)got) == 0;
    }
    if (fa >= 0)
        close(fa);
    if (fb >= 0)
        close(fb);
    return same;
}

// The home screen keeps copies of a title's pictures from when it was
// registered: a new icon or background is put there too.
static void refresh_home_screen(const char *title, const char *target)
{
    static const char *const pictures[] = {"icon0.png", "pic0.dds", "pic1.dds"};
    char from[1024];
    char to[1024];
    for (size_t i = 0; i < sizeof(pictures) / sizeof(pictures[0]); ++i)
    {
        snprintf(from, sizeof(from), "%s/sce_sys/%s", target, pictures[i]);
        const char *const places[] = {"/user/app/%s/sce_sys/%s", "/user/appmeta/%s/%s"};
        for (size_t p = 0; p < 2; ++p)
        {
            snprintf(to, sizeof(to), places[p], title, pictures[i]);
            if (access(to, F_OK) == 0 && !same_file(from, to))
                say(copy_file(from, to) ? "home screen: %s" : "home screen: could not write %s", to);
        }
        if (i == 0)
        {
            snprintf(to, sizeof(to), "/user/app/%s/icon0.png", title);
            if (access(to, F_OK) == 0 && !same_file(from, to))
                say(copy_file(from, to) ? "home screen: %s" : "home screen: could not write %s", to);
        }
    }
}

// A line of files.txt the helper acts on: a relative path that stays inside.
static int installable(const char *line)
{
    return line[0] != '\0' && line[0] != '/' && strstr(line, "..") == NULL;
}

// Copies the staged files over the title's, keeping each one it replaces in
// <update dir>/previous. A failure puts back what was already replaced.
static int apply_update(const char *title, const char *stage, const char *target, char *error,
                        size_t error_size)
{
    static char list[MAX_UPDATE_LIST + 1];
    char path[1024];
    snprintf(path, sizeof(path), "%s/files.txt", stage);
    const long size = read_up_to(path, list, MAX_UPDATE_LIST);
    if (size <= 0)
    {
        snprintf(error, error_size, "the update has no file list");
        return 0;
    }
    list[size] = '\0';
    // The old files go beside the stage; where the helper may not write in
    // ORION's data folder (it cannot make folders there), into a hidden one
    // in ORION's own folder instead.
    char previous[512];
    snprintf(previous, sizeof(previous), "%.*sprevious", (int)(strrchr(stage, '/') - stage + 1),
             stage);
    if (mkdir(previous, 0777) != 0 && errno != EEXIST)
    {
        snprintf(previous, sizeof(previous), "%s/.orion-previous", target);
        if (mkdir(previous, 0777) != 0 && errno != EEXIST)
        {
            snprintf(error, error_size, "no folder for the old files (errno %d)", errno);
            return 0;
        }
    }
    say("update: the old files go to %s", previous);
    int done = 0;
    int ok = 1;
    char *line = list;
    while (ok && *line != '\0')
    {
        char *end = strchr(line, '\n');
        if (end != NULL)
            *end = '\0';
        if (installable(line))
        {
            char from[1024];
            char to[1024];
            char keep[1024];
            snprintf(from, sizeof(from), "%s/%s", stage, line);
            snprintf(to, sizeof(to), "%s/%s", target, line);
            snprintf(keep, sizeof(keep), "%s/%s", previous, line);
            if (!make_folders(target, line) || !make_folders(previous, line))
                ok = 0;
            else if (access(to, F_OK) == 0 && !copy_file(to, keep))
                ok = 0;
            else if (!copy_file(from, to))
                ok = 0;
            if (!ok)
                snprintf(error, error_size, "could not replace %s (errno %d)", line, errno);
            else
                ++done;
        }
        if (end == NULL)
            break;
        line = end + 1;
    }
    if (!ok)
    {
        // Back to the old files: the first `done` installable lines, the
        // ones that were replaced (the list's newlines are NULs by now).
        line = list;
        for (int i = 0; i < done && line < list + size; line += strlen(line) + 1)
        {
            if (!installable(line))
                continue;
            char to[1024];
            char keep[1024];
            snprintf(to, sizeof(to), "%s/%s", target, line);
            snprintf(keep, sizeof(keep), "%s/%s", previous, line);
            if (access(keep, F_OK) == 0)
                copy_file(keep, to);
            ++i;
        }
        return 0;
    }
    say("update: %d files replaced", done);
    refresh_home_screen(title, target);
    return 1;
}

// <target>/update-result.txt: "1 <version>" or "0 <version>\t<why>".
// In ORION's own folder (<target>/update-result.txt), where the helper can
// write, as it does remove-result.txt; ORION's data folder it may not.
static void write_update_result(const char *stage, const char *target, int ok,
                                const char *error)
{
    char version[64] = "";
    char path[1024];
    snprintf(path, sizeof(path), "%s/version.txt", stage);
    const long size = read_up_to(path, version, sizeof(version) - 1);
    version[size > 0 ? size : 0] = '\0';
    char *newline = strchr(version, '\n');
    if (newline != NULL)
        *newline = '\0';
    snprintf(path, sizeof(path), "%s/update-result.txt", target);
    char text[512];
    const int length = snprintf(text, sizeof(text), "%d %s%s%s\n", ok, version, ok ? "" : "\t",
                                ok ? "" : error);
    write_shared(path, text, (size_t)length);
}

// ---- @moved-from: ORION installed under a new title ID ----

// ShadowMountPlus defers its scans after an app closes until another opens
// (a console showed the new title listed two seconds after one did, and
// not in three minutes before). So the helper looks briefly, then lets the
// old ORION open, which waits for the new title itself.
#define REGISTER_WAIT_S 20
#define RESCAN_EVERY_S 30

// A title's folder: under /data/homebrew or /mnt, named after the title.
static int title_folder(const char *title, const char *folder)
{
    const size_t title_length = strlen(title);
    const size_t length = strlen(folder);
    return is_title_id(title) && strstr(folder, "..") == NULL && length > title_length + 1 &&
           strcmp(folder + length - title_length, title) == 0 &&
           folder[length - title_length - 1] == '/' &&
           (strncmp(folder, "/data/homebrew/", 15) == 0 || strncmp(folder, "/mnt/", 5) == 0);
}

// What the old folder holds that is ORION's record rather than its program:
// the play log (seconds played per game), copied unless the new one has it.
static void carry_records(const char *from, const char *to)
{
    char source[1024];
    char target[1024];
    snprintf(source, sizeof(source), "%s/play-log.txt", from);
    snprintf(target, sizeof(target), "%s/play-log.txt", to);
    if (access(source, F_OK) == 0 && access(target, F_OK) != 0)
        say(copy_file(source, target) ? "move: the play log came along"
                                      : "move: could not copy the play log");
}

// Whether the system has the title installed, as ShadowMountPlus reports it
// from the system's own database ("installed"). The files the system keeps
// under /user/app are not a reliable sign from a payload: a test found the
// title installed (and opened) while they never showed.
static int installed(const char *title)
{
    static char body[16384];
    char json[64];
    char error[160] = "";
    snprintf(json, sizeof(json), "{\"title_id\": \"%s\"}", title);
    return smp_call("/api/v1/games/info", json, body, sizeof(body), error, sizeof(error)) &&
           json_true(body, "installed");
}

// Waits for the system to list the new title. ShadowMountPlus finds a new
// folder on its own schedule, so it is asked to scan now, and again every
// RESCAN_EVERY_S (it defers a scan while an app is starting or open).
static int wait_for_registration(const char *title)
{
    static char body[4096];
    for (int waited = 0; waited < REGISTER_WAIT_S; ++waited)
    {
        if (waited % 3 == 0 && installed(title))
        {
            say("move: %s is registered after %d s", title, waited);
            return 1;
        }
        if (waited % RESCAN_EVERY_S == 0)
        {
            char error[160] = "";
            const int asked = smp_call("/api/v1/scan", "{\"reset_attempts\": true}", body,
                                       sizeof(body), error, sizeof(error));
            say("move: asked ShadowMountPlus to scan: %s", asked ? "queued" : error);
        }
        if (waited == 20)
            notify("ORION is waiting for the system to list its new title ID");
        sleep(1);
    }
    say("move: %s is not registered after %d s", title, REGISTER_WAIT_S);
    return 0;
}

// Whether the staged param.json is the title's: its "titleId" is <title>.
// An update staged for another title ID never goes over this one.
static int staged_for(const char *title, const char *stage)
{
    static char param[64 * 1024];
    char path[1024];
    snprintf(path, sizeof(path), "%s/sce_sys/param.json", stage);
    const long size = read_up_to(path, param, sizeof(param) - 1);
    if (size <= 0)
        return 0;
    param[size] = '\0';
    char value[16] = "";
    json_text(param, "titleId", value, sizeof(value));
    return strcmp(value, title) == 0;
}

// Only ORION's own staging folder, onto the title's own folder, and only
// what was staged for that title.
static int update_allowed(const char *title, const char *stage, const char *target)
{
    const size_t title_length = strlen(title);
    const size_t target_length = strlen(target);
    return staged_for(title, stage) && strncmp(stage, UPDATE_ROOT, strlen(UPDATE_ROOT)) == 0 &&
           strstr(stage, "..") == NULL &&
           strstr(target, "..") == NULL && target_length > title_length + 1 &&
           strcmp(target + target_length - title_length, title) == 0 &&
           target[target_length - title_length - 1] == '/' &&
           (strncmp(target, "/data/homebrew/", 15) == 0 || strncmp(target, "/mnt/", 5) == 0);
}

int main(void)
{
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
    const char *record_id = NULL;
    const char *record_log = NULL;
    const char *remove_kind = NULL; // "home" or "erase"
    const char *remove_id = NULL;
    const char *remove_name = NULL;
    const char *remove_result = NULL;
    const char *update_stage = NULL;  // @update
    const char *update_target = NULL;
    const char *moved_from = NULL;    // @moved-from
    const char *moved_folder = NULL;
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
        else if (strncmp(line, "@remove ", 8) == 0)
        {
            // "@remove <home|erase>\t<title ID>\t<name>"
            char *id = strchr(line, '\t');
            char *name = id != NULL ? strchr(id + 1, '\t') : NULL;
            if (name != NULL)
            {
                *id++ = '\0';
                *name++ = '\0';
                if (is_title_id(id) &&
                    (strcmp(line + 8, "home") == 0 || strcmp(line + 8, "erase") == 0))
                {
                    remove_kind = line + 8;
                    remove_id = id;
                    remove_name = name;
                }
            }
        }
        else if (strncmp(line, "@update ", 8) == 0 && strchr(line, '\t') != NULL)
        {
            // "@update <staging folder>\t<the title's folder>"
            char *tab = strchr(line, '\t');
            *tab = '\0';
            update_stage = line + 8;
            update_target = tab + 1;
        }
        else if (strncmp(line, "@moved-from ", 12) == 0 && strchr(line, '\t') != NULL)
        {
            // "@moved-from <old title ID>\t<its folder>"
            char *tab = strchr(line, '\t');
            *tab = '\0';
            moved_from = line + 12;
            moved_folder = tab + 1;
        }
        else if (strncmp(line, "@remove-result ", 15) == 0)
            remove_result = line + 15;
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
    // A removal ORION asked for goes first; then ORION opens again.
    if (remove_id != NULL)
    {
        usleep(1500 * 1000); // ShadowMountPlus sees the game end a moment later
        char error[256] = "";
        const int erase = strcmp(remove_kind, "erase") == 0;
        const int ok = remove_game(erase, remove_id, remove_name, error, sizeof(error));
        char note[384];
        if (!ok)
            snprintf(note, sizeof(note), "Could not remove %s: %s", remove_name, error);
        else if (erase)
            snprintf(note, sizeof(note), "Deleted %s", remove_name);
        else
            snprintf(note, sizeof(note), "%s left the home screen", remove_name);
        notify(note);
        write_result(remove_result, remove_kind, ok, remove_id, ok ? "" : error);
    }
    // An update of ORION itself: its files replaced, then it opens again.
    if (update_stage != NULL)
    {
        char error[256] = "could not be applied";
        const int moving = moved_from != NULL;
        int ok = update_allowed(job.title, update_stage, update_target);
        if (ok && moving)
            ok = title_folder(moved_from, moved_folder) && strcmp(moved_from, job.title) != 0;
        if (!ok)
            snprintf(error, sizeof(error), "the request names folders it may not change");
        else if (moving && mkdir(update_target, 0777) != 0 && errno != EEXIST)
        {
            ok = 0;
            snprintf(error, sizeof(error), "could not make %s (errno %d)", update_target, errno);
        }
        else
        {
            if (moving)
                notify("ORION is moving to its own title ID; it opens again in a moment");
            ok = apply_update(job.title, update_stage, update_target, error, sizeof(error));
        }
        say("update: %s%s", ok ? "done" : "failed: ", ok ? "" : error);
        int listed = 1;
        if (ok && moving)
        {
            carry_records(moved_folder, update_target);
            // Either way the new ORION removes the old one when it first
            // opens; the old one, open meanwhile, opens the new one once the
            // system lists it (concepts/library.cpp, move-pending.txt).
            listed = wait_for_registration(job.title);
            say(listed ? "move: opening the new ORION"
                       : "move: not listed yet; the old ORION opens and waits for it");
            if (!listed)
                strcpy(job.title, moved_from);
        }
        if (!moving)
            notify(ok ? "ORION is updated; it opens again now" : "ORION could not be updated");
        else if (!ok)
            notify("ORION could not move to its new title ID");
        else
            notify(listed ? "ORION has its own title ID now; it opens again now"
                          : "The new ORION is installed; it opens as soon as the system lists it");
        // The new ORION says how it went; one not listed yet has nothing to say.
        if (!moving || !ok || listed)
            write_update_result(update_stage, ok || !moving ? update_target : moved_folder, ok,
                                error);
        // A move that did not finish opens the ORION that asked for it.
        if (moving && !ok)
            strcpy(job.title, moved_from);
    }
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
    // A refusal is not always the end: PS5SX2 has come up three seconds
    // after a 0x80940010. So it counts only once nothing has appeared.
    const int answer = launch(&job);
    if (answer < -1)
    {
        int late = 0;
        for (; sceSystemServiceGetAppIdOfRunningBigApp() <= 0 && late < REFUSED_GRACE_MS;
             late += POLL_MS)
            usleep(POLL_MS * 1000);
        if (sceSystemServiceGetAppIdOfRunningBigApp() > 0)
            say("the title came up %d ms after the refusal; carrying on", late);
        else
        {
            for (int i = 0; i < file_count; ++i)
                restore(&files[i]);
            char said[160];
            snprintf(said, sizeof(said), "The system did not start %s (0x%08x)", job.title,
                     (unsigned)answer);
            notify(said);
            // Not left on the home screen: back to ORION, which asked.
            if (return_to != NULL)
            {
                static launch_job_t back;
                memset(&back, 0, sizeof(back));
                strcpy(back.title, return_to);
                launch(&back);
            }
            return 1;
        }
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

    // Then watch it: put its files back once it has read them, and notice
    // when it closes.
    int elapsed = 0;
    int gone = 0;
    int restored = file_count == 0;
    int note_out = 0;
    for (int i = 0; i < file_count; ++i)
        note_out |= strcmp(files[i].path, kPs5sx2Note) == 0;
    const int watching =
        (return_to != NULL || record_id != NULL || note_out) && title_app > 0;
    while (!restored || watching)
    {
        usleep(POLL_MS * 1000);
        elapsed += POLL_MS;
        if (!restored && elapsed >= TITLE_READS_MS)
        {
            for (int i = 0; i < file_count; ++i)
            {
                if (strcmp(files[i].path, kPs5sx2Note) != 0)
                    restore(&files[i]);
            }
            restored = 1;
        }
        if (!watching)
            continue;
        const int now = sceSystemServiceGetAppIdOfRunningBigApp();
        if (now == title_app)
        {
            gone = 0;
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
    // restore() skips what is already back, so this catches the note too.
    for (int i = 0; i < file_count; ++i)
        restore(&files[i]);
    say("done");
    return 0;
}
