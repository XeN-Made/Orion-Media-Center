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
//   - the whole process raised first (root, the real root folder, every
//     capability) as ps5upload's installer does: the SYSTEM authid alone
//     still had the installer refuse its http:// address (0x80B22404);
//   - MetaInfo carries a zeroed tail: the installer reads 8 bytes past the
//     six pointers, and stack leftovers there turn the install into a patch
//     install or a refusal.
// "started" is logged first, so a payload the loader cannot link shows as
// one that never started. Before asking, it asks ORION's package server for
// the same address itself, so the log tells "the server did not answer"
// from "the installer would not take it".

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
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
    uint64_t read_by_the_installer; // 0 for a plain install
    uint64_t reserved[7];
} MetaInfo;
_Static_assert(sizeof(MetaInfo) == 0x70, "MetaInfo carries the tail the installer reads");

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

// Root, the real root folder, the SYSTEM authid and every capability for
// this process, as ps5upload's installer takes before any installer call.
static int raise_self(void)
{
    const pid_t pid = getpid();
    int failed = 0;
    failed |= kernel_set_ucred_uid(pid, 0);
    failed |= kernel_set_ucred_ruid(pid, 0);
    failed |= kernel_set_ucred_svuid(pid, 0);
    failed |= kernel_set_ucred_rgid(pid, 0);
    failed |= kernel_set_ucred_svgid(pid, 0);
    const intptr_t root = kernel_get_root_vnode();
    if (root != 0)
    {
        failed |= kernel_set_proc_rootdir(pid, root);
        failed |= kernel_set_proc_jaildir(pid, root);
    }
    failed |= kernel_set_ucred_authid(pid, SYSTEM_AUTHID);
    uint8_t caps[16];
    memset(caps, 0xff, sizeof(caps));
    failed |= kernel_set_ucred_caps(pid, caps);
    uint8_t attrs[32];
    memset(attrs, 0, sizeof(attrs));
    attrs[0] = 0x80;
    failed |= kernel_set_ucred_attrs(pid, attrs);
    return failed;
}

// What ORION's package server answers this address with: its status line,
// or why there was none. The address is http://127.0.0.1:<port>/<path>.
static void probe(const char *url, char *said, size_t size)
{
    snprintf(said, size, "not an address it can try");
    const char *at = strstr(url, "127.0.0.1:");
    if (at == NULL)
        return;
    at += strlen("127.0.0.1:");
    const int port = atoi(at);
    const char *path = strchr(at, '/');
    if (port <= 0 || path == NULL)
        return;
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        snprintf(said, size, "no socket: %d", errno);
        return;
    }
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons((uint16_t)port);
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (const struct sockaddr *)&to, sizeof(to)) != 0)
    {
        snprintf(said, size, "could not connect: %d", errno);
        close(fd);
        return;
    }
    char request[600];
    const int length =
        snprintf(request, sizeof(request), "HEAD %s HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", path);
    if (length <= 0 || length >= (int)sizeof(request) ||
        send(fd, request, (size_t)length, 0) != length)
    {
        snprintf(said, size, "could not send");
        close(fd);
        return;
    }
    char reply[256];
    const ssize_t got = recv(fd, reply, sizeof(reply) - 1, 0);
    close(fd);
    if (got <= 0)
    {
        snprintf(said, size, "no answer");
        return;
    }
    reply[got] = '\0';
    reply[strcspn(reply, "\r\n")] = '\0';
    snprintf(said, size, "%s", reply);
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
    if (raise_self() != 0)
        say("some of the process's rights could not be raised");
    char said[256];
    probe(url, said, sizeof(said));
    say("the package server says: %s", said);
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
    static MetaInfo meta;
    memset(&meta, 0, sizeof(meta));
    meta.uri = url;
    meta.ex_uri = "";
    meta.playgo_scenario_id = "";
    meta.content_id = "";
    meta.content_name = name;
    meta.icon_url = "";
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
