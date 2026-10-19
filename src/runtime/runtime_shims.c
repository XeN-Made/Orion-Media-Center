// ps5-homebrew-ui - Process-level runtime shims for the OpenGL runtime.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Adapted from ps5-opengl native-app/runtime_shims.c: the app log receipt,
// the never-return main policy, and libc entry points the clean-room libc
// shim does not provide but the statically linked Mesa runtime references.

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fnmatch.h>
#include <netdb.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <net/if.h>
#include <pwd.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern int sceKernelUsleep(uint32_t microseconds);
extern uint64_t sceKernelGetProcessTime(void);

/* The log goes into the title's own storage (a sandbox has no /data). While
 * the title runs, FTP reads it at
 * /mnt/sandbox/<TITLE_ID>_000/download0/hui/dev/app.log. */
#define HUI_PARENT_DIR "/download0/hui"
#define HUI_DATA_DIR HUI_PARENT_DIR "/dev"
#define HUI_LOG_PATH HUI_DATA_DIR "/app.log"

__attribute__((constructor)) static void hui_open_log(void)
{
    mkdir(HUI_PARENT_DIR, 0755);
    mkdir(HUI_DATA_DIR, 0755);
    /* Keep the previous launch's log for post-close inspection. */
    rename(HUI_LOG_PATH, HUI_DATA_DIR "/app.prev.log");
    FILE *stream = freopen(HUI_LOG_PATH, "w", stdout);
    /* Start a fresh receipt, then make both streams append-only and unbuffered
     * so the log survives a shell close or a GPU fail-stop. */
    if (stream != NULL)
        stream = freopen(HUI_LOG_PATH, "a", stdout);
    if (stream != NULL)
        setvbuf(stream, NULL, _IONBF, 0);
    stream = freopen(HUI_LOG_PATH, "a", stderr);
    if (stream != NULL)
        setvbuf(stream, NULL, _IONBF, 0);
}

/* Returning from main or calling exit() crashes a native title; stay alive
 * until the shell closes the title. */
__attribute__((noreturn)) void catchReturnFromMain(int status)
{
    printf("[HUI] main returned status=%d\n", status);
    fflush(NULL);
    for (;;)
        sceKernelUsleep(100000);
}

void hui_glapi_tls_context_init(void) __asm__("_ZTH23_mesa_glapi_tls_Context");

void hui_glapi_tls_context_init(void)
{
}

__attribute__((noreturn)) void __assert(const char *function, const char *file, int line,
                                        const char *expression)
{
    fprintf(stderr, "[HUI] assertion failed: %s (%s:%d, %s)\n", expression, file, line, function);
    abort();
}

/* The OpenGL runtime's shader cache (PS5_SHADER_CACHE_DIR) writes through
 * mkstemp(). The SDK binds mkstemp and isatty to libScePosixForWebKit, a
 * system library a native title does not load: a call jumps to address 0, and
 * an app that sets the cache folder stops at its first shader. These
 * definitions are linked instead of those imports. */
int mkstemps(char *template_name, int suffix_length)
{
    static const char letters[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    static unsigned counter;
    const size_t length = template_name ? strlen(template_name) : 0;

    if (suffix_length < 0 || length < (size_t)suffix_length + 6u)
    {
        errno = EINVAL;
        return -1;
    }
    char *name = template_name + length - (size_t)suffix_length - 6u;
    for (int i = 0; i < 6; ++i)
    {
        if (name[i] != 'X')
        {
            errno = EINVAL;
            return -1;
        }
    }
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        uint64_t value = sceKernelGetProcessTime() +
                         (uint64_t)__atomic_add_fetch(&counter, 1u, __ATOMIC_RELAXED) *
                             UINT64_C(0x9E3779B97F4A7C15);
        for (int i = 0; i < 6; ++i)
        {
            name[i] = letters[value % 36u];
            value /= 36u;
        }
        const int file = open(template_name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (file >= 0 || errno != EEXIST)
            return file;
    }
    errno = EEXIST;
    return -1;
}

int mkstemp(char *template_name)
{
    return mkstemps(template_name, 0);
}

int isatty(int descriptor)
{
    (void)descriptor;
    errno = ENOTTY;
    return 0;
}

/* The console's splash picture stays up until the app has presented its first
 * frame. The OpenGL runtime asks to hide it as soon as the display opens,
 * which leaves a black screen while programs build and fonts load (several
 * seconds at 4K). The build routes every such call here (--wrap), and
 * sys::hide_splash_screen() lets them through. */
extern int __real_sceSystemServiceHideSplashScreen(void);
static int hui_splash_released;

void hui_release_splash(void)
{
    hui_splash_released = 1;
}

int __wrap_sceSystemServiceHideSplashScreen(void)
{
    return hui_splash_released ? __real_sceSystemServiceHideSplashScreen() : 0;
}

void openlog(const char *identifier, int option, int facility)
{
    (void)identifier;
    (void)option;
    (void)facility;
}

FILE *popen(const char *command, const char *mode)
{
    (void)command;
    (void)mode;
    errno = ENOSYS;
    return NULL;
}

int pclose(FILE *stream)
{
    (void)stream;
    errno = ENOSYS;
    return -1;
}

// --- What libcurl and OpenSSL reference (ORION's box art) ---

// OpenSSL checks certificate dates with it: it has to be right. Days to a
// civil date as in Howard Hinnant's "chrono-compatible low-level date
// algorithms" (public domain).
struct tm *gmtime_r(const time_t *clock, struct tm *result)
{
    static const int kBefore[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int64_t seconds = (int64_t)*clock;
    int64_t days = seconds / 86400;
    int64_t rest = seconds % 86400;
    if (rest < 0)
    {
        rest += 86400;
        --days;
    }
    result->tm_hour = (int)(rest / 3600);
    result->tm_min = (int)(rest % 3600 / 60);
    result->tm_sec = (int)(rest % 60);
    int64_t weekday = (days + 4) % 7; // 1970-01-01 was a Thursday
    result->tm_wday = (int)(weekday < 0 ? weekday + 7 : weekday);
    const int64_t shifted = days + 719468;
    const int64_t era = (shifted >= 0 ? shifted : shifted - 146096) / 146097;
    const int64_t day_of_era = shifted - era * 146097;
    const int64_t year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    const int64_t day_of_year =
        day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const int64_t month_index = (5 * day_of_year + 2) / 153;
    const int64_t day = day_of_year - (153 * month_index + 2) / 5 + 1;
    const int64_t month = month_index < 10 ? month_index + 3 : month_index - 9;
    const int64_t year = year_of_era + era * 400 + (month <= 2 ? 1 : 0);
    const int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    result->tm_year = (int)(year - 1900);
    result->tm_mon = (int)(month - 1);
    result->tm_mday = (int)day;
    result->tm_yday = kBefore[month - 1] + (int)day - 1 + (leap && month > 2 ? 1 : 0);
    result->tm_isdst = 0;
    result->tm_gmtoff = 0;
    result->tm_zone = (char *)"UTC";
    return result;
}

// curl wakes its own waits through a pipe.
int pipe2(int descriptors[2], int flags)
{
    if (pipe(descriptors) != 0)
        return -1;
    for (int i = 0; i < 2; ++i)
    {
        if ((flags & O_NONBLOCK) != 0)
            fcntl(descriptors[i], F_SETFL, fcntl(descriptors[i], F_GETFL) | O_NONBLOCK);
        if ((flags & O_CLOEXEC) != 0)
            fcntl(descriptors[i], F_SETFD, FD_CLOEXEC);
    }
    return 0;
}

// A title has no user database: OpenSSL then looks no further for a home.
int getpwuid_r(uid_t uid, struct passwd *entry, char *buffer, size_t size, struct passwd **result)
{
    (void)uid;
    (void)entry;
    (void)buffer;
    (void)size;
    *result = NULL;
    return 0;
}

// Scoped IPv6 addresses, batched UDP (HTTP/3) and symbol lookup: unused by
// plain HTTPS, so they say "not here".
unsigned int if_nametoindex(const char *name)
{
    (void)name;
    errno = ENXIO;
    return 0;
}

ssize_t sendmmsg(int socket, struct mmsghdr *messages, size_t count, int flags)
{
    (void)socket;
    (void)messages;
    (void)count;
    (void)flags;
    errno = ENOSYS;
    return -1;
}

ssize_t recvmmsg(int socket, struct mmsghdr *__restrict messages, size_t count, int flags,
                 const struct timespec *__restrict timeout)
{
    (void)socket;
    (void)messages;
    (void)count;
    (void)flags;
    (void)timeout;
    errno = ENOSYS;
    return -1;
}

int dladdr(const void *address, Dl_info *info)
{
    (void)address;
    (void)info;
    return 0;
}

// _setjmp and _longjmp (no signal mask) for x86-64: the registers a callee
// must preserve, the stack pointer and the return address, in a FreeBSD
// jmp_buf (12 longs; 8 are used).
__asm__(".text\n"
        ".globl _setjmp\n"
        ".type _setjmp,@function\n"
        "_setjmp:\n"
        "    movq (%rsp), %rdx\n"
        "    movq %rdx, 0(%rdi)\n"
        "    movq %rbx, 8(%rdi)\n"
        "    leaq 8(%rsp), %rdx\n"
        "    movq %rdx, 16(%rdi)\n"
        "    movq %rbp, 24(%rdi)\n"
        "    movq %r12, 32(%rdi)\n"
        "    movq %r13, 40(%rdi)\n"
        "    movq %r14, 48(%rdi)\n"
        "    movq %r15, 56(%rdi)\n"
        "    xorl %eax, %eax\n"
        "    ret\n"
        ".size _setjmp, .-_setjmp\n"
        ".globl _longjmp\n"
        ".type _longjmp,@function\n"
        "_longjmp:\n"
        "    movq 0(%rdi), %rdx\n"
        "    movq 8(%rdi), %rbx\n"
        "    movq 16(%rdi), %rsp\n"
        "    movq 24(%rdi), %rbp\n"
        "    movq 32(%rdi), %r12\n"
        "    movq 40(%rdi), %r13\n"
        "    movq 48(%rdi), %r14\n"
        "    movq 56(%rdi), %r15\n"
        "    movl %esi, %eax\n"
        "    testl %eax, %eax\n"
        "    jnz 1f\n"
        "    incl %eax\n"
        "1:  jmp *%rdx\n"
        ".size _longjmp, .-_longjmp\n");

// zstd (curl decompresses with it) asks for optional tracing hooks; 0 from a
// begin hook turns tracing off.
unsigned long long ZSTD_trace_compress_begin(const void *context)
{
    (void)context;
    return 0;
}

void ZSTD_trace_compress_end(unsigned long long context, const void *trace)
{
    (void)context;
    (void)trace;
}

unsigned long long ZSTD_trace_decompress_begin(const void *context)
{
    (void)context;
    return 0;
}

void ZSTD_trace_decompress_end(unsigned long long context, const void *trace)
{
    (void)context;
    (void)trace;
}

// --- What the SDK binds to libScePosixForWebKit ---
//
// The SDK's stubs take getaddrinfo and a few more from the browser's POSIX
// module, which a game's process cannot reach: the call lands on address 0
// (a page fault on an instruction read). These replace them; names are
// looked up with libSceNet's resolver, which every title may use. IPv4 only.

int sceNetInit(void);
int sceNetPoolCreate(const char *name, int size, int flags);
int sceNetResolverCreate(const char *name, int pool, int flags);
int sceNetResolverStartNtoa(int resolver, const char *hostname, struct in_addr *address,
                            int timeout_us, int retries, int flags);
int sceNetResolverDestroy(int resolver);

static int hui_net_pool = -1;

static int hui_resolve(const char *host, struct in_addr *address)
{
    // One pool for the process; a resolver per lookup (they are cheap).
    static atomic_int ready;
    int expected = 0;
    if (atomic_compare_exchange_strong(&ready, &expected, 1))
    {
        sceNetInit(); // fails harmlessly when the system already did it
        hui_net_pool = sceNetPoolCreate("orion-dns", 16 * 1024, 0);
        atomic_store(&ready, 2);
    }
    while (atomic_load(&ready) != 2)
        sceKernelUsleep(1000);
    if (hui_net_pool < 0)
        return EAI_FAIL;
    const int resolver = sceNetResolverCreate("orion", hui_net_pool, 0);
    if (resolver < 0)
        return EAI_AGAIN;
    const int result = sceNetResolverStartNtoa(resolver, host, address, 10 * 1000 * 1000, 2, 0);
    sceNetResolverDestroy(resolver);
    return result < 0 ? EAI_NONAME : 0;
}

// "93.184.216.34" -> its address; 0 when it is not a dotted quad.
static int hui_parse_ipv4(const char *text, struct in_addr *address)
{
    uint32_t value = 0;
    for (int part = 0; part < 4; ++part)
    {
        if (*text < '0' || *text > '9')
            return 0;
        unsigned number = 0;
        int digits = 0;
        while (*text >= '0' && *text <= '9' && digits < 4)
        {
            number = number * 10u + (unsigned)(*text - '0');
            ++text;
            ++digits;
        }
        if (number > 255 || digits > 3)
            return 0;
        value = (value << 8) | number;
        if (part < 3 && *text++ != '.')
            return 0;
    }
    if (*text != '\0')
        return 0;
    address->s_addr = htonl(value);
    return 1;
}

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints,
                struct addrinfo **result)
{
    if (result == NULL || (node == NULL && service == NULL))
        return EAI_NONAME;
    *result = NULL;
    const int family = hints != NULL ? hints->ai_family : AF_UNSPEC;
    if (family != AF_UNSPEC && family != AF_INET)
        return EAI_FAMILY;
    long port = 0;
    if (service != NULL && *service != '\0')
    {
        char *end = NULL;
        port = strtol(service, &end, 10);
        if (*end != '\0' || port < 0 || port > 65535)
            return EAI_SERVICE;
    }
    struct in_addr address;
    const int flags = hints != NULL ? hints->ai_flags : 0;
    if (node == NULL)
        address.s_addr = htonl((flags & AI_PASSIVE) != 0 ? INADDR_ANY : INADDR_LOOPBACK);
    else if (!hui_parse_ipv4(node, &address))
    {
        if ((flags & AI_NUMERICHOST) != 0)
            return EAI_NONAME;
        const int failed = hui_resolve(node, &address);
        if (failed != 0)
            return failed;
    }
    struct
    {
        struct addrinfo info;
        struct sockaddr_in socket;
    } *entry = calloc(1, sizeof(*entry));
    if (entry == NULL)
        return EAI_MEMORY;
    entry->socket.sin_len = sizeof(entry->socket);
    entry->socket.sin_family = AF_INET;
    entry->socket.sin_port = htons((uint16_t)port);
    entry->socket.sin_addr = address;
    entry->info.ai_family = AF_INET;
    entry->info.ai_socktype =
        hints != NULL && hints->ai_socktype != 0 ? hints->ai_socktype : SOCK_STREAM;
    entry->info.ai_protocol = hints != NULL ? hints->ai_protocol : 0;
    entry->info.ai_addrlen = sizeof(entry->socket);
    entry->info.ai_addr = (struct sockaddr *)&entry->socket;
    *result = &entry->info;
    return 0;
}

void freeaddrinfo(struct addrinfo *list)
{
    // Each entry is one allocation with its address (see getaddrinfo).
    while (list != NULL)
    {
        struct addrinfo *next = list->ai_next;
        free(list);
        list = next;
    }
}

const char *gai_strerror(int code)
{
    switch (code)
    {
    case 0:
        return "success";
    case EAI_AGAIN:
        return "temporary failure in name resolution";
    case EAI_FAMILY:
        return "address family not supported";
    case EAI_MEMORY:
        return "out of memory";
    case EAI_NONAME:
        return "host not found";
    case EAI_SERVICE:
        return "service not supported";
    default:
        return "name resolution failed";
    }
}

// Numbers only: an IPv4 address and a port, as curl asks for them.
int getnameinfo(const struct sockaddr *address, socklen_t length, char *host, size_t host_size,
                char *service, size_t service_size, int flags)
{
    (void)flags;
    if (address == NULL || address->sa_family != AF_INET ||
        length < (socklen_t)sizeof(struct sockaddr_in))
        return EAI_FAMILY;
    const struct sockaddr_in *in = (const struct sockaddr_in *)address;
    const uint32_t value = ntohl(in->sin_addr.s_addr);
    if (host != NULL && host_size > 0 &&
        snprintf(host, host_size, "%u.%u.%u.%u", (unsigned)(value >> 24),
                 (unsigned)(value >> 16) & 255u, (unsigned)(value >> 8) & 255u,
                 (unsigned)value & 255u) >= (int)host_size)
        return EAI_OVERFLOW;
    if (service != NULL && service_size > 0 &&
        snprintf(service, service_size, "%u", (unsigned)ntohs(in->sin_port)) >= (int)service_size)
        return EAI_OVERFLOW;
    return 0;
}

struct hostent *gethostbyname(const char *name)
{
    (void)name;
    return NULL;
}

// Directory listing: readdir is broken in this runtime anyway (the library
// scan uses getdents); the HTTPS path never lists folders.
DIR *opendir(const char *path)
{
    (void)path;
    errno = ENOSYS;
    return NULL;
}

struct dirent *readdir(DIR *directory)
{
    (void)directory;
    errno = EBADF;
    return NULL;
}

int closedir(DIR *directory)
{
    (void)directory;
    errno = EBADF;
    return -1;
}

int fnmatch(const char *pattern, const char *text, int flags)
{
    (void)pattern;
    (void)text;
    (void)flags;
    return FNM_NOMATCH;
}

// The console refuses FD_CLOEXEC on sockets (EINVAL), and curl drops every
// socket it cannot mark. Nothing here ever execs another program, so the
// flag means nothing: F_SETFD succeeds without asking. Everything else goes
// to the system call.
int _fcntl(int descriptor, int command, ...);

int fcntl(int descriptor, int command, ...)
{
    va_list arguments;
    va_start(arguments, command);
    const intptr_t argument = va_arg(arguments, intptr_t);
    va_end(arguments);
    if (command == F_SETFD)
        return 0;
    return _fcntl(descriptor, command, argument);
}
