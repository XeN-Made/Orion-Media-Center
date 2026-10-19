// ps5-homebrew-ui - Process-level runtime shims for the OpenGL runtime.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Adapted from ps5-opengl native-app/runtime_shims.c: the app log receipt,
// the never-return main policy, and libc entry points the clean-room libc
// shim does not provide but the statically linked Mesa runtime references.

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <pwd.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
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
