// ORION Media Center - Reads the console's own play time for ORION.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The system keeps how long each game was played, for every game and not
// only those started from ORION, in /system_data/priv/mms/app.db (system
// software 10 and later): a table per user, tbl_iconinfo_<user>, with
// titleId, playedTimeOnConsole (seconds) and lastPlayedDate. ORION's sandbox
// cannot open it, so ORION sends this ELF to the ELF loader as it starts. It
// opens the database read-only, writes /data/orion/system-playtime.txt
// ("<title ID>\t<seconds>\t<last played>" a line, each title's most played
// user) and ends.
//
// The idea and the table's layout come from PlayTime by ghost1372 (MIT).

#include <errno.h>
#include <fcntl.h>
#include <sqlite3.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define APP_DB "/system_data/priv/mms/app.db"
#define OUT_PATH "/data/orion/system-playtime.txt"
#define LOG_PATH "/data/orion/launcher.txt"
#define MAX_TABLES 16
#define MAX_TITLES 4096

typedef struct
{
    char id[10];
    long long seconds;
    char last[32];
} played_t;

static played_t titles[MAX_TITLES];
static int title_count = 0;

static void say(const char *format, ...)
{
    char line[512];
    int length = snprintf(line, sizeof(line), "%lld  playtime  ", (long long)time(NULL));
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

// A PS5 or PS4 game's title ID: four capitals, five digits.
static int is_game_id(const char *id)
{
    if (id == NULL || strlen(id) != 9 ||
        (strncmp(id, "PPSA", 4) != 0 && strncmp(id, "CUSA", 4) != 0))
        return 0;
    for (int i = 4; i < 9; ++i)
    {
        if (id[i] < '0' || id[i] > '9')
            return 0;
    }
    return 1;
}

// The title's entry, made when it has none; NULL when the list is full.
static played_t *entry(const char *id)
{
    for (int i = 0; i < title_count; ++i)
    {
        if (strcmp(titles[i].id, id) == 0)
            return &titles[i];
    }
    if (title_count >= MAX_TITLES)
        return NULL;
    played_t *added = &titles[title_count++];
    memset(added, 0, sizeof(*added));
    snprintf(added->id, sizeof(added->id), "%s", id);
    return added;
}

// One user's table. Several users play on one console: each title keeps
// the most played one's time, and the latest date of any.
static void read_table(sqlite3 *db, const char *table)
{
    char *sql = sqlite3_mprintf(
        "SELECT titleId, playedTimeOnConsole, lastPlayedDate FROM \"%w\"", table);
    sqlite3_stmt *statement = NULL;
    if (sql == NULL)
        return;
    const int prepared = sqlite3_prepare_v2(db, sql, -1, &statement, NULL);
    sqlite3_free(sql);
    if (prepared != SQLITE_OK)
    {
        say("%s: %s", table, sqlite3_errmsg(db));
        return;
    }
    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        const char *id = (const char *)sqlite3_column_text(statement, 0);
        if (!is_game_id(id))
            continue;
        played_t *title = entry(id);
        if (title == NULL)
            break;
        const long long seconds = sqlite3_column_int64(statement, 1);
        if (seconds > title->seconds)
            title->seconds = seconds;
        const char *last = (const char *)sqlite3_column_text(statement, 2);
        // The dates read alike ("2026-10-12T13:44:10..."): the larger is later.
        if (last != NULL && strcmp(last, title->last) > 0)
        {
            snprintf(title->last, sizeof(title->last), "%s", last);
            for (char *c = title->last; *c != '\0'; ++c)
            {
                if (*c == '\t' || *c == '\n' || *c == '\r')
                    *c = ' ';
            }
        }
    }
    sqlite3_finalize(statement);
}

static int write_out(void)
{
    static char text[MAX_TITLES * 64];
    size_t used = 0;
    for (int i = 0; i < title_count; ++i)
    {
        if (titles[i].seconds <= 0)
            continue;
        const int wrote = snprintf(text + used, sizeof(text) - used, "%s\t%lld\t%s\n",
                                   titles[i].id, titles[i].seconds, titles[i].last);
        if (wrote < 0 || (size_t)wrote >= sizeof(text) - used)
            break;
        used += (size_t)wrote;
    }
    const char *part = OUT_PATH ".part";
    const int fd = open(part, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
    {
        say("cannot write " OUT_PATH " (errno %d)", errno);
        return 1;
    }
    const ssize_t written = write(fd, text, used);
    fchmod(fd, 0666);
    close(fd);
    if (written != (ssize_t)used || rename(part, OUT_PATH) != 0)
    {
        say("cannot finish " OUT_PATH " (errno %d)", errno);
        return 1;
    }
    say("%d titles' play time from the system", title_count);
    return 0;
}

int main(void)
{
    struct stat info;
    if (stat(APP_DB, &info) != 0)
    {
        say("no " APP_DB " (errno %d): older system software", errno);
        return 1;
    }
    sqlite3 *db = NULL;
    if (sqlite3_open_v2(APP_DB, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK)
    {
        say("cannot open " APP_DB ": %s", db != NULL ? sqlite3_errmsg(db) : "no memory");
        sqlite3_close(db);
        return 1;
    }
    // The system may be writing it: wait a little for its lock, never long.
    sqlite3_busy_timeout(db, 2000);
    char tables[MAX_TABLES][64];
    int table_count = 0;
    sqlite3_stmt *statement = NULL;
    if (sqlite3_prepare_v2(db,
                           "SELECT name FROM sqlite_master WHERE type = 'table' AND name GLOB "
                           "'tbl_iconinfo_*'",
                           -1, &statement, NULL) == SQLITE_OK)
    {
        while (table_count < MAX_TABLES && sqlite3_step(statement) == SQLITE_ROW)
        {
            const char *name = (const char *)sqlite3_column_text(statement, 0);
            if (name != NULL && strlen(name) < sizeof(tables[0]))
                snprintf(tables[table_count++], sizeof(tables[0]), "%s", name);
        }
    }
    sqlite3_finalize(statement);
    for (int i = 0; i < table_count; ++i)
        read_table(db, tables[i]);
    sqlite3_close(db);
    if (table_count == 0)
    {
        say("no play time tables in " APP_DB);
        return 1;
    }
    return write_out();
}
