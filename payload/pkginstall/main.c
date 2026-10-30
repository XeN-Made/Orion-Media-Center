// ORION Media Center - Hands one PS4 or PS5 package to the console's own installer.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORION's sandbox cannot install a package; the system's installer
// (libSceAppInstUtil) takes one from a process with more rights. ORION sends
// this ELF to the ELF loader with the job in /data/orion/pkg-install.txt:
//
//   <token>\t<package URL>\t<name shown while it installs>
//
// The URL is ORION's own package server on 127.0.0.1, which streams the file
// off the network drive as the installer reads it. This asks for the
// install, writes "<token>\t<result, hex>" to pkg-install-result.txt, and
// stays (the installer's session ends with it) until ORION takes the job
// file away once the package has been read whole, or for twelve hours.
//
// The calls and their structures are as etaHEN's write-up on package
// installs describes them; the authid is the one other installers use for
// it, and fake packages need kstuff, as for any installer. The installer's
// library is opened here (dlopen) rather than linked: linked, a loader
// process without it ends the payload before main, with nothing said.

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <ps5/kernel.h>

#define JOB_PATH "/data/orion/pkg-install.txt"
#define RESULT_PATH "/data/orion/pkg-install-result.txt"
#define LOG_PATH "/data/orion/launcher.txt"
#define INSTALL_AUTHID 0x4800000000000006ull
#define MOST_SECONDS (12 * 60 * 60)

typedef struct
{
    const char *uri;
    const char *ex_uri;
    const char *playgo_scenario_id;
    const char *content_id;
    const char *content_name;
    const char *icon_url;
} MetaInfo;

typedef struct
{
    char content_id[48];
    int content_type;
    int content_platform;
} PkgInfo;

typedef struct
{
    char languages[30][8];
    char playgo_scenario_ids[64][3];
    char content_ids[64][48];
    unsigned char unknown[6480];
} PlayGoInfo;

static int (*sceAppInstUtilInitialize)(void);
static int (*sceAppInstUtilInstallByPackage)(MetaInfo *meta, PkgInfo *info, PlayGoInfo *playgo);
static int (*sceAppInstUtilTerminate)(void);

static void say(const char *format, ...)
{
    char line[768];
    int length = snprintf(line, sizeof(line), "%lld  pkginstall  ", (long long)time(NULL));
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

// The job file whole, or 0 bytes when it is gone.
static size_t read_job(char *text, size_t size)
{
    const int fd = open(JOB_PATH, O_RDONLY);
    if (fd < 0)
        return 0;
    const ssize_t got = read(fd, text, size - 1);
    close(fd);
    if (got <= 0)
        return 0;
    text[got] = '\0';
    return (size_t)got;
}

static void answer(const char *token, int code)
{
    char text[128];
    const int length = snprintf(text, sizeof(text), "%s\t%08x\n", token, (unsigned)code);
    const int fd = open(RESULT_PATH ".part", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return;
    write(fd, text, (size_t)length);
    fchmod(fd, 0666);
    close(fd);
    rename(RESULT_PATH ".part", RESULT_PATH);
}

// The installer's three calls, from its library; 0 when one is missing.
static int open_installer(void)
{
    void *library = dlopen("libSceAppInstUtil.sprx", RTLD_LAZY);
    if (library == NULL)
    {
        const char *why = dlerror();
        say("cannot open libSceAppInstUtil.sprx: %s", why != NULL ? why : "?");
        return 0;
    }
    sceAppInstUtilInitialize = (int (*)(void))dlsym(library, "sceAppInstUtilInitialize");
    sceAppInstUtilInstallByPackage = (int (*)(MetaInfo *, PkgInfo *, PlayGoInfo *))dlsym(
        library, "sceAppInstUtilInstallByPackage");
    sceAppInstUtilTerminate = (int (*)(void))dlsym(library, "sceAppInstUtilTerminate");
    if (sceAppInstUtilInitialize == NULL || sceAppInstUtilInstallByPackage == NULL ||
        sceAppInstUtilTerminate == NULL)
    {
        say("libSceAppInstUtil.sprx lacks the install calls");
        return 0;
    }
    return 1;
}

int main(void)
{
    say("started");
    static char job[2048];
    if (read_job(job, sizeof(job)) == 0)
    {
        say("no job in " JOB_PATH);
        return 1;
    }
    // <token>\t<url>\t<name>
    char *token = job;
    char *url = strchr(token, '\t');
    char *name = url != NULL ? strchr(url + 1, '\t') : NULL;
    if (url == NULL || name == NULL)
    {
        say("a job without its parts");
        return 1;
    }
    *url++ = '\0';
    *name++ = '\0';
    name[strcspn(name, "\r\n")] = '\0';
    char kept[64];
    snprintf(kept, sizeof(kept), "%s", token);
    if (!open_installer())
    {
        answer(kept, -1);
        return 1;
    }

    const pid_t self = getpid();
    const uint64_t authid = kernel_get_ucred_authid(self);
    kernel_set_ucred_authid(self, INSTALL_AUTHID);
    int code = sceAppInstUtilInitialize();
    if (code != 0)
    {
        say("the installer would not start: %08x", (unsigned)code);
        answer(kept, code);
        kernel_set_ucred_authid(self, authid);
        return 1;
    }
    MetaInfo meta = {url, "", "", "", name, ""};
    static PkgInfo info;
    static PlayGoInfo playgo;
    memset(&info, 0, sizeof(info));
    memset(&playgo, 0, sizeof(playgo));
    code = sceAppInstUtilInstallByPackage(&meta, &info, &playgo);
    say("%s from %s: %08x %s", name, url, (unsigned)code, info.content_id);
    answer(kept, code);
    // The installer reads the package through this session: it stays until
    // ORION says the package was read whole (the job file goes, or names
    // another token), or the time is up.
    if (code == 0)
    {
        for (int waited = 0; waited < MOST_SECONDS; waited += 2)
        {
            sleep(2);
            static char now[2048];
            if (read_job(now, sizeof(now)) == 0 || strncmp(now, kept, strlen(kept)) != 0 ||
                now[strlen(kept)] != '\t')
                break;
        }
    }
    sceAppInstUtilTerminate();
    kernel_set_ucred_authid(self, authid);
    say("%s: done", name);
    return code == 0 ? 0 : 1;
}
