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
// installs describes them; fake packages need kstuff, as for any installer.
// What the system wants around them follows ps5upload (phantomptr, GPL-3),
// which installs packages from a payload on firmware 9 to 12:
//   - libSceIpmi linked before libSceAppInstUtil (the SDK's install_app
//     sample, ps5-payload-dev/sdk#26): without it the loader refuses the
//     ELF before main, and dlopen at run time ends the payload;
//   - Initialize under the SYSTEM authid on firmware 10 and later
//     (ShellCore's left the installer half-wedged there), ShellCore's
//     before;
//   - InstallByPackage under ShellCore's authid before firmware 11
//     (others are refused: 0x80B22404), SYSTEM from 11.
// "started" is logged first, so a payload the loader cannot link shows as
// one that never started.

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <ps5/kernel.h>

#define JOB_PATH "/data/orion/pkg-install.txt"
#define RESULT_PATH "/data/orion/pkg-install-result.txt"
#define LOG_PATH "/data/orion/launcher.txt"
#define SHELLCORE_AUTHID 0x3800000000000010ull
#define SYSTEM_AUTHID 0x4801000000000013ull
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

int sceAppInstUtilInitialize(void);
int sceAppInstUtilInstallByPackage(MetaInfo *meta, PkgInfo *info, PlayGoInfo *playgo);
int sceAppInstUtilTerminate(void);

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

// The firmware's major version from kern.version ("releases/11..."), or 0.
static int firmware_major(void)
{
    char version[256];
    size_t size = sizeof(version);
    if (sysctlbyname("kern.version", version, &size, NULL, 0) != 0)
        return 0;
    version[sizeof(version) - 1] = '\0';
    const char *at = strstr(version, "releases/");
    if (at == NULL)
        return 0;
    at += strlen("releases/");
    int major = 0;
    while (*at >= '0' && *at <= '9')
        major = major * 10 + (*at++ - '0');
    return major;
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
    say("job %s: %s", kept, name);

    // The installer's rights first: a system library may want them to open.
    const pid_t self = getpid();
    const uint64_t authid = kernel_get_ucred_authid(self);
    const int firmware = firmware_major();
    // Unknown firmware: the newer rules, as most consoles run 10 or later.
    const uint64_t init_as = firmware == 0 || firmware >= 10 ? SYSTEM_AUTHID : SHELLCORE_AUTHID;
    const uint64_t install_as =
        firmware == 0 || firmware >= 11 ? SYSTEM_AUTHID : SHELLCORE_AUTHID;
    kernel_set_ucred_authid(self, init_as);
    say("firmware %d; initializing the installer as %016llx", firmware,
        (unsigned long long)init_as);
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
    kernel_set_ucred_authid(self, install_as);
    say("asking for %s as %016llx", url, (unsigned long long)install_as);
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
