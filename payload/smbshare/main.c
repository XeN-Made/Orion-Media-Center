// ORION Media Center - Shares the console's drives with PCs as Windows network drives (SMB).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORION sends this ELF to the ELF loader when Settings, Network drives,
// "Share this console's drives" is on (and as it starts, while it stays on).
// It reads /data/orion/smb-share.txt (on, user, password: orion/console_share),
// shares the console's storage and each USB or extended storage drive that
// is there (\\<console>\data, \usb0, \ext0...) on port 445, and keeps
// going after ORION closes. When the file changes it starts again with what
// it says, when a drive comes or goes it starts again with the drives there
// are, and when the file says off it ends. One runs at a time: a second
// finds the lock taken and ends at once.

#include "smb_server.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CONFIG_PATH "/data/orion/smb-share.txt"
#define STATUS_PATH "/data/orion/smb-share-status.txt"
#define LOCK_PATH "/data/orion/smb-share.lock"
#define LOG_PATH "/data/orion/launcher.txt"

static void say(const char *format, ...)
{
    char line[600];
    int length = snprintf(line, sizeof(line), "%lld  smbshare  ", (long long)time(NULL));
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

static void log_line(void *context, const char *line)
{
    (void)context;
    say("%s", line);
}

static void set_status(const char *state, const char *detail)
{
    const int fd = open(STATUS_PATH ".part", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return;
    char line[600];
    const int length = snprintf(line, sizeof(line), "%s\t%s\n", state, detail);
    write(fd, line, (size_t)(length > 0 && length < (int)sizeof(line) ? length : 0));
    close(fd);
    rename(STATUS_PATH ".part", STATUS_PATH);
}

typedef struct
{
    int on;
    char user[64];
    char password[256];
} settings_t;

static int read_settings(settings_t *settings)
{
    memset(settings, 0, sizeof(*settings));
    FILE *file = fopen(CONFIG_PATH, "r");
    if (file == NULL)
        return 0;
    char line[512];
    while (fgets(line, sizeof(line), file) != NULL)
    {
        line[strcspn(line, "\r\n")] = 0;
        char *tab = strchr(line, '\t');
        if (tab == NULL)
            continue;
        *tab = 0;
        const char *value = tab + 1;
        if (strcmp(line, "on") == 0)
            settings->on = strcmp(value, "1") == 0;
        else if (strcmp(line, "user") == 0)
            snprintf(settings->user, sizeof(settings->user), "%s", value);
        else if (strcmp(line, "password") == 0)
            snprintf(settings->password, sizeof(settings->password), "%s", value);
    }
    fclose(file);
    return 1;
}

// A drive is there when its mount point has something in it.
static int has_entries(const char *path)
{
    DIR *dir = opendir(path);
    if (dir == NULL)
        return 0;
    int found = 0;
    struct dirent *entry;
    while (!found && (entry = readdir(dir)) != NULL)
        found = strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0;
    closedir(dir);
    return found;
}

// The drives there are, and a word for them (which ones), to notice a change.
static unsigned find_drives(smb_config_t *config)
{
    unsigned present = 0;
    config->share_count = 0;
    smb_add_share(config, "data", "/data");
    for (int i = 0; i < 8; ++i)
    {
        char path[32];
        snprintf(path, sizeof(path), "/mnt/usb%d", i);
        if (has_entries(path))
        {
            char name[16];
            snprintf(name, sizeof(name), "usb%d", i);
            smb_add_share(config, name, path);
            present |= 1u << i;
        }
    }
    for (int i = 0; i < 2; ++i)
    {
        char path[32];
        snprintf(path, sizeof(path), "/mnt/ext%d", i);
        if (has_entries(path))
        {
            char name[16];
            snprintf(name, sizeof(name), "ext%d", i);
            smb_add_share(config, name, path);
            present |= 1u << (8 + i);
        }
    }
    return present;
}

typedef struct
{
    time_t config_time;
    off_t config_size;
    unsigned drives;
    int ticks;
} watch_t;

static int config_changed(const watch_t *watch)
{
    struct stat info;
    if (stat(CONFIG_PATH, &info) != 0)
        return 1;
    return info.st_mtime != watch->config_time || info.st_size != watch->config_size;
}

// Asked once a second: a new configuration, or a drive that came or went,
// ends this round (main starts the next).
static int keep_going(void *context)
{
    watch_t *watch = (watch_t *)context;
    if (config_changed(watch))
        return 0;
    if (++watch->ticks % 5 == 0)
    {
        smb_config_t probe;
        memset(&probe, 0, sizeof(probe));
        if (find_drives(&probe) != watch->drives)
        {
            say("the drives changed");
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    const int lock = open(LOCK_PATH, O_RDWR | O_CREAT, 0666);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) != 0)
    {
        say("already sharing: this copy ends");
        return 0;
    }
    say("started");
    for (;;)
    {
        settings_t settings;
        watch_t watch;
        memset(&watch, 0, sizeof(watch));
        struct stat info;
        if (stat(CONFIG_PATH, &info) == 0)
        {
            watch.config_time = info.st_mtime;
            watch.config_size = info.st_size;
        }
        if (!read_settings(&settings) || !settings.on)
        {
            set_status("off", "");
            say("off: stopped");
            return 0;
        }
        if (settings.password[0] == 0)
        {
            set_status("nopassword", "");
            say("no password set: not sharing");
            return 0;
        }
        smb_config_t config;
        memset(&config, 0, sizeof(config));
        config.port = 445;
        snprintf(config.computer, sizeof(config.computer), "PS5");
        smb_set_password(&config, settings.user[0] != 0 ? settings.user : "ps5",
                         settings.password);
        memset(settings.password, 0, sizeof(settings.password));
        watch.drives = find_drives(&config);
        config.keep_going = keep_going;
        config.context = &watch;
        config.log = log_line;
        char names[256] = "";
        for (int i = 0; i < config.share_count; ++i)
        {
            if (i > 0)
                strncat(names, ", ", sizeof(names) - strlen(names) - 1);
            strncat(names, config.shares[i].name, sizeof(names) - strlen(names) - 1);
        }
        set_status("on", names);
        const int error = smb_serve(&config);
        if (error == EADDRINUSE)
        {
            set_status("busy", "");
            say("port 445 is taken by something else: not sharing");
            return 1;
        }
        if (error != 0)
        {
            char detail[64];
            snprintf(detail, sizeof(detail), "errno %d", error);
            set_status("error", detail);
            say("could not listen on port 445 (errno %d)", error);
            return 1;
        }
    }
}
