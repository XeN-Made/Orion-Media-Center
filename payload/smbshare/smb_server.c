// ORION Media Center - A small SMB 2.1 server: the console's drives as Windows network shares.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The protocol is Microsoft's [MS-SMB2], [MS-NLMP] (NTLM), [MS-SPNG]
// (SPNEGO) and [MS-SRVS] (the share list). Field offsets below are those
// documents' structures, counted from the start of each body.

// POSIX and BSD calls (strdup, ftruncate, pread, utimes) under a strict C
// standard on glibc; the console's C library shows them anyway.
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "smb_server.h"
#include "smb_crypto.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

// ---- statuses ----
#define ST_OK 0x00000000u
#define ST_PENDING 0x00000103u
#define ST_BUFFER_OVERFLOW 0x80000005u
#define ST_NO_MORE_FILES 0x80000006u
#define ST_UNSUCCESSFUL 0xC0000001u
#define ST_NOT_IMPLEMENTED 0xC0000002u
#define ST_INVALID_INFO_CLASS 0xC0000003u
#define ST_INFO_LENGTH_MISMATCH 0xC0000004u
#define ST_INVALID_HANDLE 0xC0000008u
#define ST_INVALID_PARAMETER 0xC000000Du
#define ST_NO_SUCH_FILE 0xC000000Fu
#define ST_INVALID_DEVICE_REQUEST 0xC0000010u
#define ST_END_OF_FILE 0xC0000011u
#define ST_MORE_PROCESSING 0xC0000016u
#define ST_ACCESS_DENIED 0xC0000022u
#define ST_BUFFER_TOO_SMALL 0xC0000023u
#define ST_OBJECT_NAME_INVALID 0xC0000033u
#define ST_OBJECT_NAME_NOT_FOUND 0xC0000034u
#define ST_OBJECT_NAME_COLLISION 0xC0000035u
#define ST_OBJECT_PATH_NOT_FOUND 0xC000003Au
#define ST_DELETE_PENDING 0xC0000056u
#define ST_LOGON_FAILURE 0xC000006Du
#define ST_DISK_FULL 0xC000007Fu
#define ST_MEDIA_WRITE_PROTECTED 0xC00000A2u
#define ST_FILE_IS_A_DIRECTORY 0xC00000BAu
#define ST_NOT_SUPPORTED 0xC00000BBu
#define ST_NETWORK_NAME_DELETED 0xC00000C9u
#define ST_BAD_NETWORK_NAME 0xC00000CCu
#define ST_UNEXPECTED_IO_ERROR 0xC00000E9u
#define ST_DIRECTORY_NOT_EMPTY 0xC0000101u
#define ST_NOT_A_DIRECTORY 0xC0000103u
#define ST_CANCELLED 0xC0000120u
#define ST_FS_DRIVER_REQUIRED 0xC000019Cu
#define ST_USER_SESSION_DELETED 0xC0000203u

// ---- commands ----
enum
{
    CMD_NEGOTIATE,
    CMD_SESSION_SETUP,
    CMD_LOGOFF,
    CMD_TREE_CONNECT,
    CMD_TREE_DISCONNECT,
    CMD_CREATE,
    CMD_CLOSE,
    CMD_FLUSH,
    CMD_READ,
    CMD_WRITE,
    CMD_LOCK,
    CMD_IOCTL,
    CMD_CANCEL,
    CMD_ECHO,
    CMD_QUERY_DIRECTORY,
    CMD_CHANGE_NOTIFY,
    CMD_QUERY_INFO,
    CMD_SET_INFO,
    CMD_OPLOCK_BREAK,
};

#define FLAG_RESPONSE 0x1u
#define FLAG_RELATED 0x4u
#define FLAG_SIGNED 0x8u

#define MAX_IO (1u << 20)                // reads and writes, with large MTU
#define MAX_MESSAGE (MAX_IO + (64u << 10)) // the largest message taken in
#define MAX_HANDLES 512
#define MAX_TREES 16
#define MAX_CONNECTIONS 16
#define MAX_PATH_BYTES 1024

// NTLM's flags ([MS-NLMP] 2.2.2.5).
#define NTLM_UNICODE 0x00000001u
#define NTLM_REQUEST_TARGET 0x00000004u
#define NTLM_SIGN 0x00000010u
#define NTLM_SEAL 0x00000020u
#define NTLM_NTLM 0x00000200u
#define NTLM_ALWAYS_SIGN 0x00008000u
#define NTLM_TARGET_SERVER 0x00020000u
#define NTLM_EXTENDED 0x00080000u
#define NTLM_TARGET_INFO 0x00800000u
#define NTLM_VERSION 0x02000000u
#define NTLM_128 0x20000000u
#define NTLM_KEY_EXCH 0x40000000u
#define NTLM_56 0x80000000u

static const uint8_t kNtlmOid[] = {0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x02, 0x0a};
static const uint8_t kSpnegoOid[] = {0x2b, 0x06, 0x01, 0x05, 0x05, 0x02};
// NDR's transfer syntax, version 2.
static const uint8_t kNdr[20] = {0x04, 0x5d, 0x88, 0x8a, 0xeb, 0x1c, 0xc9, 0x11, 0x9f, 0xe8,
                                 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60, 0x02, 0x00, 0x00, 0x00};

// ---- little-endian fields ----

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32;
}

static void wr16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void wr64(uint8_t *p, uint64_t v)
{
    wr32(p, (uint32_t)v);
    wr32(p + 4, (uint32_t)(v >> 32));
}

// ---- a growing buffer: the responses of one message ----

typedef struct
{
    uint8_t *data;
    size_t size;
    size_t cap;
} buf_t;

static int buf_reserve(buf_t *b, size_t extra)
{
    if (b->size + extra <= b->cap)
        return 1;
    size_t cap = b->cap != 0 ? b->cap : 65536;
    while (cap < b->size + extra)
        cap *= 2;
    uint8_t *data = (uint8_t *)realloc(b->data, cap);
    if (data == NULL)
        return 0;
    b->data = data;
    b->cap = cap;
    return 1;
}

// n zeroed bytes at the end; their offset, or (size_t)-1. Pointers into the
// buffer do not survive this: work with offsets.
static size_t buf_add(buf_t *b, size_t n)
{
    if (!buf_reserve(b, n))
        return (size_t)-1;
    const size_t at = b->size;
    memset(b->data + at, 0, n);
    b->size += n;
    return at;
}

static size_t buf_put(buf_t *b, const void *data, size_t n)
{
    const size_t at = buf_add(b, n);
    if (at != (size_t)-1)
        memcpy(b->data + at, data, n);
    return at;
}

// ---- text: UTF-16LE on the wire, UTF-8 on the drives ----

// The length written (out stays terminated), or (size_t)-1 when it does not fit.
static size_t utf16_to_utf8(const uint8_t *in, size_t in_size, char *out, size_t cap)
{
    size_t n = 0;
    for (size_t i = 0; i + 1 < in_size; i += 2)
    {
        uint32_t c = rd16(in + i);
        if (c >= 0xd800 && c < 0xdc00 && i + 3 < in_size)
        {
            const uint32_t low = rd16(in + i + 2);
            if (low >= 0xdc00 && low < 0xe000)
            {
                c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
                i += 2;
            }
        }
        uint8_t bytes[4];
        size_t count;
        if (c < 0x80)
        {
            bytes[0] = (uint8_t)c;
            count = 1;
        }
        else if (c < 0x800)
        {
            bytes[0] = (uint8_t)(0xc0 | c >> 6);
            bytes[1] = (uint8_t)(0x80 | (c & 0x3f));
            count = 2;
        }
        else if (c < 0x10000)
        {
            bytes[0] = (uint8_t)(0xe0 | c >> 12);
            bytes[1] = (uint8_t)(0x80 | ((c >> 6) & 0x3f));
            bytes[2] = (uint8_t)(0x80 | (c & 0x3f));
            count = 3;
        }
        else
        {
            bytes[0] = (uint8_t)(0xf0 | c >> 18);
            bytes[1] = (uint8_t)(0x80 | ((c >> 12) & 0x3f));
            bytes[2] = (uint8_t)(0x80 | ((c >> 6) & 0x3f));
            bytes[3] = (uint8_t)(0x80 | (c & 0x3f));
            count = 4;
        }
        if (n + count + 1 > cap)
            return (size_t)-1;
        memcpy(out + n, bytes, count);
        n += count;
    }
    if (cap == 0)
        return (size_t)-1;
    out[n] = 0;
    return n;
}

// The bytes written; stops (whole characters) where it no longer fits.
static size_t utf8_to_utf16(const char *in, uint8_t *out, size_t cap)
{
    const uint8_t *p = (const uint8_t *)in;
    size_t n = 0;
    while (*p != 0)
    {
        uint32_t c = *p++;
        if (c >= 0xf0 && p[0] != 0 && p[1] != 0 && p[2] != 0)
        {
            c = (c & 0x07) << 18 | (uint32_t)(p[0] & 0x3f) << 12 | (uint32_t)(p[1] & 0x3f) << 6 |
                (p[2] & 0x3f);
            p += 3;
        }
        else if (c >= 0xe0 && p[0] != 0 && p[1] != 0)
        {
            c = (c & 0x0f) << 12 | (uint32_t)(p[0] & 0x3f) << 6 | (p[1] & 0x3f);
            p += 2;
        }
        else if (c >= 0xc0 && p[0] != 0)
        {
            c = (c & 0x1f) << 6 | (p[0] & 0x3f);
            p += 1;
        }
        if (c >= 0x10000)
        {
            if (n + 4 > cap)
                break;
            c -= 0x10000;
            wr16(out + n, 0xd800 + (c >> 10));
            wr16(out + n + 2, 0xdc00 + (c & 0x3ff));
            n += 4;
            continue;
        }
        if (n + 2 > cap)
            break;
        wr16(out + n, c);
        n += 2;
    }
    return n;
}

// ---- time and chance ----

static uint64_t filetime(time_t t)
{
    return ((uint64_t)t + 11644473600ull) * 10000000ull;
}

static uint64_t now_filetime(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((uint64_t)tv.tv_sec + 11644473600ull) * 10000000ull + (uint64_t)tv.tv_usec * 10u;
}

static time_t unix_time(uint64_t ft)
{
    return (time_t)(ft / 10000000ull - 11644473600ull);
}

static void random_bytes(uint8_t *out, size_t n)
{
    size_t got = 0;
    const int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0)
    {
        const ssize_t r = read(fd, out, n);
        got = r > 0 ? (size_t)r : 0;
        close(fd);
    }
    // No /dev/urandom: the clock and a counter, hashed. A server challenge
    // must not repeat; it need not be secret.
    static uint32_t counter = 0;
    while (got < n)
    {
        struct
        {
            struct timeval tv;
            uint32_t counter;
            clock_t clock;
        } seed;
        gettimeofday(&seed.tv, NULL);
        seed.counter = ++counter;
        seed.clock = clock();
        uint8_t hash[16];
        smb_md5(&seed, sizeof(seed), hash);
        for (size_t i = 0; i < 16 && got < n; ++i)
            out[got++] = hash[i];
    }
}

// ---- configuration ----

void smb_set_password(smb_config_t *config, const char *user, const char *password)
{
    snprintf(config->user, sizeof(config->user), "%s", user);
    smb_nt_hash(password, config->nt_hash);
}

int smb_add_share(smb_config_t *config, const char *name, const char *path)
{
    if (config->share_count >= SMB_MAX_SHARES)
        return 0;
    smb_share_t *share = &config->shares[config->share_count++];
    snprintf(share->name, sizeof(share->name), "%s", name);
    snprintf(share->path, sizeof(share->path), "%s", path);
    return 1;
}

static void say(const smb_config_t *config, const char *format, ...)
{
    if (config->log == NULL)
        return;
    char line[512];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    config->log(config->context, line);
}

// ---- names and paths ----

int smb_wildcard_match(const char *pattern, const char *name)
{
    // DOS forms: < is *, > is ?, " is . (Explorer sends "*" or a name).
    while (*pattern != 0)
    {
        char p = *pattern;
        if (p == '<')
            p = '*';
        else if (p == '>')
            p = '?';
        else if (p == '"')
            p = '.';
        if (p == '*')
        {
            ++pattern;
            if (*pattern == 0)
                return 1;
            for (const char *rest = name; *rest != 0; ++rest)
            {
                if (smb_wildcard_match(pattern, rest))
                    return 1;
            }
            return smb_wildcard_match(pattern, "");
        }
        if (*name == 0)
            return 0;
        if (p != '?' && tolower((unsigned char)p) != tolower((unsigned char)*name))
            return 0;
        ++pattern;
        ++name;
    }
    return *name == 0;
}

size_t smb_share_path(const char *root, const uint8_t *name, size_t name_size, char *out,
                      size_t out_size)
{
    char relative[MAX_PATH_BYTES];
    if (utf16_to_utf8(name, name_size, relative, sizeof(relative)) == (size_t)-1)
        return 0;
    for (char *c = relative; *c != 0; ++c)
    {
        if (*c == '\\')
            *c = '/';
        if (*c == ':')
            return 0; // a stream: none are offered
    }
    const int written = snprintf(out, out_size, "%s", root);
    if (written < 0 || (size_t)written >= out_size)
        return 0;
    size_t n = (size_t)written;
    while (n > 1 && out[n - 1] == '/')
        out[--n] = 0;
    // Component by component: empty and "." go, ".." is refused.
    const char *p = relative;
    while (*p != 0)
    {
        while (*p == '/')
            ++p;
        const char *end = p;
        while (*end != 0 && *end != '/')
            ++end;
        const size_t length = (size_t)(end - p);
        if (length == 0)
            break;
        if (length == 2 && p[0] == '.' && p[1] == '.')
            return 0;
        if (!(length == 1 && p[0] == '.'))
        {
            if (n + 1 + length + 1 > out_size)
                return 0;
            out[n++] = '/';
            memcpy(out + n, p, length);
            n += length;
            out[n] = 0;
        }
        p = end;
    }
    return n;
}

// UFS (/data) tells upper and lower case apart, Windows does not: a part of
// the path that is not there is looked for again without regard to case.
static void match_case(char *path)
{
    struct stat info;
    if (lstat(path, &info) == 0)
        return;
    char *slash = strrchr(path, '/');
    if (slash == NULL || slash == path)
        return;
    *slash = 0;
    match_case(path);
    DIR *dir = opendir(path);
    if (dir != NULL)
    {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            if (strcasecmp(entry->d_name, slash + 1) == 0 &&
                strlen(entry->d_name) == strlen(slash + 1))
            {
                memcpy(slash + 1, entry->d_name, strlen(entry->d_name));
                break;
            }
        }
        closedir(dir);
    }
    *slash = '/';
}

static uint32_t from_errno(int error)
{
    switch (error)
    {
    case ENOENT:
        return ST_OBJECT_NAME_NOT_FOUND;
    case ENOTDIR:
        return ST_OBJECT_PATH_NOT_FOUND;
    case EACCES:
    case EPERM:
        return ST_ACCESS_DENIED;
    case EEXIST:
        return ST_OBJECT_NAME_COLLISION;
    case ENOSPC:
        return ST_DISK_FULL;
    case EROFS:
        return ST_MEDIA_WRITE_PROTECTED;
    case ENOTEMPTY:
        return ST_DIRECTORY_NOT_EMPTY;
    case EISDIR:
        return ST_FILE_IS_A_DIRECTORY;
    case ENAMETOOLONG:
        return ST_OBJECT_NAME_INVALID;
    default:
        return ST_UNEXPECTED_IO_ERROR;
    }
}

// ---- a connection ----

typedef struct
{
    int used;
    int tree;
    int is_dir;
    int is_pipe;
    int fd;
    int delete_pending;
    char path[MAX_PATH_BYTES];
    // A folder's listing, read when it is first asked for.
    char **names;
    int name_count;
    int position;
    int listed;
    char pattern[256];
    // A pipe's answer, waiting for a READ.
    uint8_t *pipe_out;
    size_t pipe_out_size;
} handle_t;

typedef struct
{
    int used;
    int share; // an index in the shares, -1 for IPC$
} tree_t;

typedef struct
{
    int fd;
    const smb_config_t *config;
    uint16_t dialect;
    uint64_t session_id;
    int session_state; // 0 none, 1 challenged, 2 signed in
    uint8_t key[16];
    uint8_t challenge[8];
    uint32_t ntlm_flags;
    int raw_ntlm; // the client spoke NTLM without SPNEGO around it
    int need_mic; // NTLM was not the client's first choice: a mechListMIC is owed
    uint8_t mech_types[256];
    size_t mech_types_size;
    tree_t trees[MAX_TREES];
    handle_t *handles;
    uint64_t last_file; // the handle a related compound refers to
    uint64_t last_session;
    uint32_t last_tree;
    uint8_t *in;
    buf_t out;
    char peer[48];
} conn_t;

static handle_t *find_handle(conn_t *c, const uint8_t *file_id, int related)
{
    uint64_t id = rd64(file_id + 8);
    if (id == UINT64_MAX && related)
        id = c->last_file;
    if (id == 0 || id > MAX_HANDLES)
        return NULL;
    handle_t *h = &c->handles[id - 1];
    return h->used ? h : NULL;
}

static void free_listing(handle_t *h)
{
    for (int i = 0; i < h->name_count; ++i)
        free(h->names[i]);
    free(h->names);
    h->names = NULL;
    h->name_count = 0;
    h->listed = 0;
    h->position = 0;
}

static int folder_is_empty(const char *path)
{
    DIR *dir = opendir(path);
    if (dir == NULL)
        return 1;
    int empty = 1;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
        {
            empty = 0;
            break;
        }
    }
    closedir(dir);
    return empty;
}

static void close_handle(conn_t *c, handle_t *h)
{
    if (h->fd >= 0)
        close(h->fd);
    if (h->delete_pending)
    {
        const int failed = h->is_dir ? rmdir(h->path) : unlink(h->path);
        if (failed != 0)
            say(c->config, "%s: could not delete %s (errno %d)", c->peer, h->path, errno);
    }
    free_listing(h);
    free(h->pipe_out);
    memset(h, 0, sizeof(*h));
    h->fd = -1;
}

static void close_all(conn_t *c, int tree)
{
    for (int i = 0; i < MAX_HANDLES; ++i)
    {
        if (c->handles[i].used && (tree < 0 || c->handles[i].tree == tree))
            close_handle(c, &c->handles[i]);
    }
}

// ---- file facts as SMB gives them ----

typedef struct
{
    uint64_t created, accessed, written, changed;
    uint64_t size, allocation;
    uint32_t attributes;
    uint64_t id;
    int is_dir;
} facts_t;

static void facts_of(const conn_t *c, const struct stat *info, const char *name, facts_t *f)
{
    f->written = filetime(info->st_mtime);
    f->accessed = filetime(info->st_atime);
    f->changed = filetime(info->st_ctime);
    f->created = f->written < f->changed ? f->written : f->changed;
    f->is_dir = S_ISDIR(info->st_mode);
    f->size = f->is_dir ? 0 : (uint64_t)info->st_size;
    f->allocation = (f->size + 4095u) & ~(uint64_t)4095u;
    f->attributes = f->is_dir ? 0x10u : 0x20u; // DIRECTORY, ARCHIVE
    if (c->config->read_only)
        f->attributes |= 0x1u; // READONLY
    if (name != NULL && name[0] == '.' && strcmp(name, ".") != 0 && strcmp(name, "..") != 0)
        f->attributes |= 0x2u; // HIDDEN
    f->id = (uint64_t)info->st_ino;
}

static int stat_handle(const handle_t *h, struct stat *info)
{
    if (h->fd >= 0)
        return fstat(h->fd, info);
    return stat(h->path, info);
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash != NULL ? slash + 1 : path;
}

// ---- SPNEGO and NTLM ----

typedef struct
{
    const uint8_t *p;
    size_t n;
} span_t;

static int der_read(span_t *s, uint8_t *tag, span_t *content)
{
    if (s->n < 2)
        return 0;
    *tag = s->p[0];
    size_t length = s->p[1];
    size_t head = 2;
    if (length & 0x80)
    {
        const size_t bytes = length & 0x7f;
        if (bytes < 1 || bytes > 3 || s->n < 2 + bytes)
            return 0;
        length = 0;
        for (size_t i = 0; i < bytes; ++i)
            length = length << 8 | s->p[2 + i];
        head = 2 + bytes;
    }
    if (head + length > s->n)
        return 0;
    content->p = s->p + head;
    content->n = length;
    s->p += head + length;
    s->n -= head + length;
    return 1;
}

static size_t der_header(uint8_t *p, uint8_t tag, size_t length)
{
    p[0] = tag;
    if (length < 0x80)
    {
        p[1] = (uint8_t)length;
        return 2;
    }
    if (length < 0x100)
    {
        p[1] = 0x81;
        p[2] = (uint8_t)length;
        return 3;
    }
    p[1] = 0x82;
    p[2] = (uint8_t)(length >> 8);
    p[3] = (uint8_t)length;
    return 4;
}

// tag(content) into dst; the bytes written. dst and content must not overlap.
static size_t der_wrap(uint8_t *dst, uint8_t tag, const uint8_t *content, size_t length)
{
    const size_t head = der_header(dst, tag, length);
    memcpy(dst + head, content, length);
    return head + length;
}

static int is_ntlm_oid(span_t oid)
{
    return oid.n == sizeof(kNtlmOid) && memcmp(oid.p, kNtlmOid, sizeof(kNtlmOid)) == 0;
}

// The NegTokenInit a NEGOTIATE response offers: NTLM, and only it.
static size_t spnego_offer(uint8_t *out)
{
    uint8_t oid[16], list[24], field[32], sequence[40], init[48], inner[64];
    const size_t oid_n = der_wrap(oid, 0x06, kNtlmOid, sizeof(kNtlmOid));
    const size_t list_n = der_wrap(list, 0x30, oid, oid_n);
    const size_t field_n = der_wrap(field, 0xa0, list, list_n);
    const size_t sequence_n = der_wrap(sequence, 0x30, field, field_n);
    const size_t init_n = der_wrap(init, 0xa0, sequence, sequence_n);
    size_t inner_n = der_wrap(inner, 0x06, kSpnegoOid, sizeof(kSpnegoOid));
    memcpy(inner + inner_n, init, init_n);
    inner_n += init_n;
    return der_wrap(out, 0x60, inner, inner_n);
}

// A NegTokenResp: its state, NTLM as the mechanism on the first, the NTLM
// token and the mechListMIC when there are.
static size_t spnego_answer(uint8_t *out, int state, const uint8_t *token, size_t token_n,
                            const uint8_t *mic)
{
    uint8_t fields[1024];
    size_t n = 0;
    const uint8_t enumerated[3] = {0x0a, 0x01, (uint8_t)state};
    n += der_wrap(fields + n, 0xa0, enumerated, 3);
    if (state == 1)
    {
        uint8_t oid[16];
        const size_t oid_n = der_wrap(oid, 0x06, kNtlmOid, sizeof(kNtlmOid));
        n += der_wrap(fields + n, 0xa1, oid, oid_n);
    }
    if (token_n > 0)
    {
        uint8_t octets[700];
        const size_t octets_n = der_wrap(octets, 0x04, token, token_n);
        n += der_wrap(fields + n, 0xa2, octets, octets_n);
    }
    if (mic != NULL)
    {
        uint8_t octets[20];
        const size_t octets_n = der_wrap(octets, 0x04, mic, 16);
        n += der_wrap(fields + n, 0xa3, octets, octets_n);
    }
    uint8_t sequence[1100];
    const size_t sequence_n = der_wrap(sequence, 0x30, fields, n);
    return der_wrap(out, 0xa1, sequence, sequence_n);
}

// The NTLM token inside what SESSION_SETUP carried; the client's mechanism
// list is kept from its first token, for the mechListMIC.
static int ntlm_token(conn_t *c, const uint8_t *blob, size_t n, span_t *token)
{
    if (n >= 8 && memcmp(blob, "NTLMSSP\0", 8) == 0)
    {
        c->raw_ntlm = 1;
        token->p = blob;
        token->n = n;
        return 1;
    }
    span_t s = {blob, n};
    uint8_t tag;
    span_t body;
    if (!der_read(&s, &tag, &body))
        return 0;
    if (tag == 0x60)
    {
        span_t oid;
        span_t init;
        span_t sequence;
        if (!der_read(&body, &tag, &oid) || tag != 0x06 || !der_read(&body, &tag, &init) ||
            tag != 0xa0 || !der_read(&init, &tag, &sequence) || tag != 0x30)
            return 0;
        body = sequence;
    }
    else if (tag == 0xa1)
    {
        span_t sequence;
        if (!der_read(&body, &tag, &sequence) || tag != 0x30)
            return 0;
        body = sequence;
    }
    else
        return 0;
    int found = 0;
    while (body.n > 0)
    {
        span_t field;
        if (!der_read(&body, &tag, &field))
            return 0;
        if (tag == 0xa0 && field.n > 0 && field.p[0] == 0x30 && c->session_state == 0)
        {
            // mechTypes: kept whole (the SEQUENCE), and is NTLM first?
            if (field.n <= sizeof(c->mech_types))
            {
                memcpy(c->mech_types, field.p, field.n);
                c->mech_types_size = field.n;
            }
            span_t list = field;
            span_t oids;
            span_t first;
            uint8_t inner;
            c->need_mic = !(der_read(&list, &inner, &oids) && der_read(&oids, &inner, &first) &&
                            is_ntlm_oid(first));
        }
        else if (tag == 0xa2)
        {
            span_t octets;
            if (der_read(&field, &tag, &octets) && tag == 0x04)
            {
                *token = octets;
                found = 1;
            }
        }
        else if (tag == 0xa3)
            c->need_mic = 1; // the client sent one: one goes back
    }
    return found;
}

static size_t ntlm_challenge(conn_t *c, uint32_t client_flags, uint8_t *out)
{
    uint8_t name[64];
    const size_t name_n = utf8_to_utf16(c->config->computer, name, sizeof(name));
    uint8_t info[400];
    size_t info_n = 0;
    static const uint16_t kNames[] = {2, 1, 4, 3}; // NetBIOS domain, computer; DNS domain, computer
    for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); ++i)
    {
        wr16(info + info_n, kNames[i]);
        wr16(info + info_n + 2, (uint32_t)name_n);
        memcpy(info + info_n + 4, name, name_n);
        info_n += 4 + name_n;
    }
    wr16(info + info_n, 7); // the time
    wr16(info + info_n + 2, 8);
    wr64(info + info_n + 4, now_filetime());
    info_n += 12;
    wr32(info + info_n, 0); // the end
    info_n += 4;

    c->ntlm_flags = NTLM_UNICODE | NTLM_REQUEST_TARGET | NTLM_NTLM | NTLM_ALWAYS_SIGN |
                    NTLM_TARGET_SERVER | NTLM_EXTENDED | NTLM_TARGET_INFO | NTLM_VERSION |
                    NTLM_128 | NTLM_56 | (client_flags & (NTLM_SIGN | NTLM_SEAL | NTLM_KEY_EXCH));
    random_bytes(c->challenge, 8);
    memset(out, 0, 56);
    memcpy(out, "NTLMSSP\0", 8);
    wr32(out + 8, 2);
    wr16(out + 12, (uint32_t)name_n);
    wr16(out + 14, (uint32_t)name_n);
    wr32(out + 16, 56);
    wr32(out + 20, c->ntlm_flags);
    memcpy(out + 24, c->challenge, 8);
    wr16(out + 40, (uint32_t)info_n);
    wr16(out + 42, (uint32_t)info_n);
    wr32(out + 44, (uint32_t)(56 + name_n));
    static const uint8_t kVersion[8] = {10, 0, 0x7c, 0x4f, 0, 0, 0, 15}; // 10.0, NTLM 15
    memcpy(out + 48, kVersion, 8);
    memcpy(out + 56, name, name_n);
    memcpy(out + 56 + name_n, info, info_n);
    return 56 + name_n + info_n;
}

int smb_ntlmv2_check(const uint8_t nt_hash[16], const char *user, const char *domain,
                     const uint8_t server_challenge[8], const uint8_t *response,
                     size_t response_size, uint8_t session_key[16])
{
    if (response_size < 16 + 28 || response_size > 4000)
        return 0;
    char upper[128];
    size_t i = 0;
    for (; user[i] != 0 && i + 1 < sizeof(upper); ++i)
        upper[i] = (char)toupper((unsigned char)user[i]);
    upper[i] = 0;
    uint8_t identity[512];
    size_t identity_n = utf8_to_utf16(upper, identity, sizeof(identity));
    identity_n += utf8_to_utf16(domain, identity + identity_n, sizeof(identity) - identity_n);
    uint8_t ntowf[16];
    smb_hmac_md5(nt_hash, 16, identity, identity_n, ntowf);
    uint8_t message[4096];
    memcpy(message, server_challenge, 8);
    memcpy(message + 8, response + 16, response_size - 16);
    uint8_t proof[16];
    smb_hmac_md5(ntowf, 16, message, 8 + response_size - 16, proof);
    if (memcmp(proof, response, 16) != 0)
        return 0;
    smb_hmac_md5(ntowf, 16, proof, 16, session_key);
    return 1;
}

// The mechListMIC: NTLM's signature (server to client, sequence 0) over the
// client's mechanism list ([MS-NLMP] 3.4.4.2).
static void ntlm_mic(const conn_t *c, uint8_t out[16])
{
    static const char kSign[] = "session key to server-to-client signing key magic constant";
    static const char kSeal[] = "session key to server-to-client sealing key magic constant";
    uint8_t material[16 + sizeof(kSign)];
    uint8_t sign_key[16];
    uint8_t seal_key[16];
    memcpy(material, c->key, 16);
    memcpy(material + 16, kSign, sizeof(kSign));
    smb_md5(material, sizeof(material), sign_key);
    memcpy(material + 16, kSeal, sizeof(kSeal));
    smb_md5(material, 16 + sizeof(kSeal), seal_key);
    uint8_t message[4 + sizeof(c->mech_types)];
    wr32(message, 0);
    memcpy(message + 4, c->mech_types, c->mech_types_size);
    uint8_t mac[16];
    smb_hmac_md5(sign_key, 16, message, 4 + c->mech_types_size, mac);
    if (c->ntlm_flags & NTLM_KEY_EXCH)
        smb_rc4(seal_key, 16, mac, 8);
    wr32(out, 1);
    memcpy(out + 4, mac, 8);
    wr32(out + 12, 0);
}

static int field_of(const uint8_t *token, size_t n, size_t at, span_t *out)
{
    if (at + 8 > n)
        return 0;
    const size_t length = rd16(token + at);
    const size_t offset = rd32(token + at + 4);
    if (offset > n || length > n - offset)
        return 0;
    out->p = token + offset;
    out->n = length;
    return 1;
}

// The client's AUTHENTICATE message: 1 when the user and password are right.
static int ntlm_authenticate(conn_t *c, span_t token)
{
    span_t nt, domain16, user16, key;
    if (token.n < 64 || !field_of(token.p, token.n, 20, &nt) ||
        !field_of(token.p, token.n, 28, &domain16) || !field_of(token.p, token.n, 36, &user16) ||
        !field_of(token.p, token.n, 52, &key))
        return 0;
    char user[128];
    char domain[128];
    if (utf16_to_utf8(user16.p, user16.n, user, sizeof(user)) == (size_t)-1 ||
        utf16_to_utf8(domain16.p, domain16.n, domain, sizeof(domain)) == (size_t)-1)
        return 0;
    if (user[0] == 0)
    {
        say(c->config, "%s: refused a guest (anonymous) sign-in", c->peer);
        return 0;
    }
    if (strcasecmp(user, c->config->user) != 0)
    {
        say(c->config, "%s: refused user \"%s\"", c->peer, user);
        return 0;
    }
    uint8_t base[16];
    if (!smb_ntlmv2_check(c->config->nt_hash, user, domain, c->challenge, nt.p, nt.n, base) &&
        !smb_ntlmv2_check(c->config->nt_hash, user, "", c->challenge, nt.p, nt.n, base))
    {
        say(c->config, "%s: wrong password for \"%s\" (or not NTLMv2)", c->peer, user);
        return 0;
    }
    const uint32_t flags = rd32(token.p + 60);
    memcpy(c->key, base, 16);
    if ((flags & NTLM_KEY_EXCH) && key.n == 16)
    {
        memcpy(c->key, key.p, 16);
        smb_rc4(base, 16, c->key, 16);
    }
    c->ntlm_flags = flags;
    say(c->config, "%s: \"%s\" signed in", c->peer, user);
    return 1;
}

// ---- the commands ----

typedef struct
{
    const uint8_t *hdr;  // the request's header
    const uint8_t *body; // its body
    size_t size;         // header and body, to the next command or the end
    int related;
    size_t response;     // where its response's header starts in c->out
} request_t;

#define RESPONSE(c, r) ((c)->out.data + (r)->response)

static int body_fits(const request_t *r, size_t need)
{
    return r->size >= 64 + need;
}

// A buffer the request points at (offset from the header): NULL when it
// is outside the request.
static const uint8_t *request_bytes(const request_t *r, size_t offset, size_t length)
{
    if (offset > r->size || length > r->size - offset)
        return NULL;
    return r->hdr + offset;
}

static uint32_t do_negotiate(conn_t *c, const request_t *r)
{
    if (!body_fits(r, 36))
        return ST_INVALID_PARAMETER;
    const size_t count = rd16(r->body + 2);
    const uint8_t *dialects = request_bytes(r, 64 + 36, count * 2);
    if (dialects == NULL)
        return ST_INVALID_PARAMETER;
    uint16_t chosen = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const uint16_t d = rd16(dialects + 2 * i);
        if ((d == 0x0210 || d == 0x0202) && d > chosen)
            chosen = d;
    }
    if (chosen == 0)
    {
        say(c->config, "%s: offers no SMB 2.0.2 or 2.1", c->peer);
        return ST_NOT_SUPPORTED;
    }
    c->dialect = chosen;
    uint8_t blob[96];
    const size_t blob_n = spnego_offer(blob);
    const size_t at = buf_add(&c->out, 64 + blob_n);
    if (at == (size_t)-1)
        return ST_UNSUCCESSFUL;
    uint8_t *b = c->out.data + at;
    wr16(b, 65);
    wr16(b + 2, 0x03); // signing enabled and required
    wr16(b + 4, chosen);
    random_bytes(b + 8, 16);
    const int large = chosen >= 0x0210;
    wr32(b + 24, large ? 0x4u : 0u); // LARGE_MTU
    const uint32_t most = large ? MAX_IO : 65536u;
    wr32(b + 28, most);
    wr32(b + 32, most);
    wr32(b + 36, most);
    wr64(b + 40, now_filetime());
    wr16(b + 56, 128);
    wr16(b + 58, (uint32_t)blob_n);
    memcpy(b + 64, blob, blob_n);
    say(c->config, "%s: SMB %s", c->peer, chosen == 0x0210 ? "2.1" : "2.0.2");
    return ST_OK;
}

static uint32_t do_session_setup(conn_t *c, request_t *r)
{
    if (!body_fits(r, 24))
        return ST_INVALID_PARAMETER;
    const uint8_t *blob = request_bytes(r, rd16(r->body + 12), rd16(r->body + 14));
    if (blob == NULL)
        return ST_INVALID_PARAMETER;
    if (rd64(r->hdr + 40) == 0 || rd64(r->hdr + 40) != c->session_id)
    {
        // A new session: one per connection, a new one replaces the last.
        close_all(c, -1);
        memset(c->trees, 0, sizeof(c->trees));
        c->session_state = 0;
        c->raw_ntlm = 0;
        c->need_mic = 0;
        c->mech_types_size = 0;
        random_bytes((uint8_t *)&c->session_id, sizeof(c->session_id));
        c->session_id |= 1;
    }
    wr64(RESPONSE(c, r) + 40, c->session_id);
    span_t token;
    if (!ntlm_token(c, blob, rd16(r->body + 14), &token) || token.n < 12 ||
        memcmp(token.p, "NTLMSSP\0", 8) != 0)
    {
        say(c->config, "%s: a sign-in that is not NTLM", c->peer);
        return ST_LOGON_FAILURE;
    }
    const uint32_t type = rd32(token.p + 8);
    uint8_t answer[1200];
    size_t answer_n = 0;
    uint32_t status;
    if (type == 1)
    {
        uint8_t challenge[600];
        const size_t challenge_n =
            ntlm_challenge(c, token.n >= 16 ? rd32(token.p + 12) : 0, challenge);
        if (c->raw_ntlm)
        {
            memcpy(answer, challenge, challenge_n);
            answer_n = challenge_n;
        }
        else
            answer_n = spnego_answer(answer, 1, challenge, challenge_n, NULL);
        c->session_state = 1;
        status = ST_MORE_PROCESSING;
    }
    else if (type == 3 && c->session_state == 1)
    {
        if (!ntlm_authenticate(c, token))
        {
            c->session_state = 0;
            return ST_LOGON_FAILURE;
        }
        c->session_state = 2;
        if (!c->raw_ntlm)
        {
            uint8_t mic[16];
            if (c->need_mic && c->mech_types_size > 0)
                ntlm_mic(c, mic);
            answer_n = spnego_answer(answer, 0, NULL, 0,
                                     c->need_mic && c->mech_types_size > 0 ? mic : NULL);
        }
        status = ST_OK;
    }
    else
        return ST_LOGON_FAILURE;
    const size_t at = buf_add(&c->out, 8 + answer_n);
    if (at == (size_t)-1)
        return ST_UNSUCCESSFUL;
    uint8_t *b = c->out.data + at;
    wr16(b, 9);
    wr16(b + 4, answer_n > 0 ? 72 : 0);
    wr16(b + 6, (uint32_t)answer_n);
    memcpy(b + 8, answer, answer_n);
    return status;
}

static uint32_t do_tree_connect(conn_t *c, const request_t *r, uint32_t *tree_id)
{
    if (!body_fits(r, 8))
        return ST_INVALID_PARAMETER;
    const uint8_t *path16 = request_bytes(r, rd16(r->body + 4), rd16(r->body + 6));
    char path[300];
    if (path16 == NULL || utf16_to_utf8(path16, rd16(r->body + 6), path, sizeof(path)) == (size_t)-1)
        return ST_BAD_NETWORK_NAME;
    const char *name = strrchr(path, '\\');
    name = name != NULL ? name + 1 : path;
    int share = -2;
    if (strcasecmp(name, "IPC$") == 0)
        share = -1;
    for (int i = 0; i < c->config->share_count && share == -2; ++i)
    {
        if (strcasecmp(name, c->config->shares[i].name) == 0)
            share = i;
    }
    if (share == -2)
    {
        say(c->config, "%s: no share \"%s\"", c->peer, name);
        return ST_BAD_NETWORK_NAME;
    }
    int slot = -1;
    for (int i = 0; i < MAX_TREES && slot < 0; ++i)
    {
        if (!c->trees[i].used)
            slot = i;
    }
    if (slot < 0)
        return ST_UNSUCCESSFUL;
    c->trees[slot].used = 1;
    c->trees[slot].share = share;
    *tree_id = (uint32_t)slot + 1;
    const size_t at = buf_add(&c->out, 16);
    if (at == (size_t)-1)
        return ST_UNSUCCESSFUL;
    uint8_t *b = c->out.data + at;
    wr16(b, 16);
    b[2] = share < 0 ? 2 : 1; // PIPE or DISK
    wr32(b + 4, share < 0 ? 0u : 0x30u); // no client-side caching
    wr32(b + 12, c->config->read_only || share < 0 ? 0x001200a9u : 0x001f01ffu);
    if (share >= 0)
        say(c->config, "%s: opened \\%s", c->peer, c->config->shares[share].name);
    return ST_OK;
}

static size_t add_small(conn_t *c, uint16_t structure_size, size_t size)
{
    const size_t at = buf_add(&c->out, size);
    if (at != (size_t)-1)
        wr16(c->out.data + at, structure_size);
    return at;
}

static uint32_t write_create_facts(conn_t *c, size_t at, const facts_t *f)
{
    uint8_t *b = c->out.data + at;
    wr64(b, f->created);
    wr64(b + 8, f->accessed);
    wr64(b + 16, f->written);
    wr64(b + 24, f->changed);
    wr64(b + 32, f->allocation);
    wr64(b + 40, f->size);
    wr32(b + 48, f->attributes);
    return ST_OK;
}

// Access that needs the file open for writing: write data, append, generic
// all, generic write, maximum allowed (the open falls back to reading).
#define DATA_WRITE (0x2u | 0x4u | 0x02000000u | 0x10000000u | 0x40000000u)
// Access a read-only share refuses: those, less maximum allowed, and write
// EA, write attributes, delete.
#define ANY_WRITE (0x2u | 0x4u | 0x10u | 0x100u | 0x10000u | 0x10000000u | 0x40000000u)

static uint32_t do_create(conn_t *c, const request_t *r, int share)
{
    if (!body_fits(r, 56))
        return ST_INVALID_PARAMETER;
    const uint32_t access = rd32(r->body + 24);
    const uint32_t disposition = rd32(r->body + 36);
    const uint32_t options = rd32(r->body + 40);
    const size_t name_n = rd16(r->body + 46);
    const uint8_t *name16 = name_n > 0 ? request_bytes(r, rd16(r->body + 44), name_n) : r->body;
    if (name16 == NULL)
        return ST_INVALID_PARAMETER;
    int slot = -1;
    for (int i = 0; i < MAX_HANDLES && slot < 0; ++i)
    {
        if (!c->handles[i].used)
            slot = i;
    }
    if (slot < 0)
        return ST_UNSUCCESSFUL;
    handle_t *h = &c->handles[slot];
    memset(h, 0, sizeof(*h));
    h->fd = -1;
    h->tree = (int)rd32(r->hdr + 36);

    if (share < 0)
    {
        // IPC$: srvsvc, which lists the shares; nothing else.
        char pipe[64];
        if (utf16_to_utf8(name16, name_n, pipe, sizeof(pipe)) == (size_t)-1)
            return ST_OBJECT_NAME_NOT_FOUND;
        const char *bare = pipe;
        while (*bare == '\\')
            ++bare;
        if (strncasecmp(bare, "PIPE\\", 5) == 0)
            bare += 5;
        if (strcasecmp(bare, "srvsvc") != 0)
            return ST_OBJECT_NAME_NOT_FOUND;
        h->used = 1;
        h->is_pipe = 1;
        c->last_file = (uint64_t)slot + 1;
        const size_t at = add_small(c, 89, 89);
        if (at == (size_t)-1)
            return ST_UNSUCCESSFUL;
        uint8_t *b = c->out.data + at;
        wr32(b + 4, 1); // opened
        wr32(b + 56, 0x80); // NORMAL
        wr64(b + 64, (uint64_t)slot + 1);
        wr64(b + 72, (uint64_t)slot + 1);
        return ST_OK;
    }

    const smb_share_t *where = &c->config->shares[share];
    if (smb_share_path(where->path, name16, name_n, h->path, sizeof(h->path)) == 0)
        return ST_OBJECT_NAME_INVALID;
    match_case(h->path);
    struct stat info;
    const int exists = stat(h->path, &info) == 0;
    const int parent_missing = !exists && errno == ENOTDIR;
    const int is_dir = exists && S_ISDIR(info.st_mode);
    const int changes = disposition != 1 && !(disposition == 3 && exists);
    if (c->config->read_only && (changes || (options & 0x1000) || (access & ANY_WRITE) != 0))
        return ST_ACCESS_DENIED;
    const int writes = !c->config->read_only && (access & DATA_WRITE) != 0;
    if (parent_missing)
        return ST_OBJECT_PATH_NOT_FOUND;
    if (exists && (options & 0x1) && !is_dir)
        return ST_NOT_A_DIRECTORY;
    if (exists && (options & 0x40) && is_dir)
        return ST_FILE_IS_A_DIRECTORY;
    uint32_t action = 1; // opened
    switch (disposition)
    {
    case 1: // OPEN
    case 4: // OVERWRITE
        if (!exists)
        {
            // Is it the file or the folder above it that is missing?
            char parent[MAX_PATH_BYTES];
            snprintf(parent, sizeof(parent), "%s", h->path);
            char *slash = strrchr(parent, '/');
            if (slash != NULL)
                *slash = 0;
            struct stat above;
            return stat(parent, &above) == 0 ? ST_OBJECT_NAME_NOT_FOUND : ST_OBJECT_PATH_NOT_FOUND;
        }
        if (disposition == 4)
            action = 3;
        break;
    case 2: // CREATE
        if (exists)
            return ST_OBJECT_NAME_COLLISION;
        action = 2;
        break;
    case 0: // SUPERSEDE
    case 3: // OPEN_IF
    case 5: // OVERWRITE_IF
        action = exists ? (disposition == 3 ? 1u : disposition == 0 ? 0u : 3u) : 2u;
        break;
    default:
        return ST_INVALID_PARAMETER;
    }
    if (action == 2)
    {
        if (options & 0x1)
        {
            if (mkdir(h->path, 0777) != 0)
                return from_errno(errno);
            h->is_dir = 1;
        }
        else
        {
            h->fd = open(h->path, O_RDWR | O_CREAT | O_EXCL, 0666);
            if (h->fd < 0)
                return from_errno(errno);
        }
    }
    else if (is_dir)
        h->is_dir = 1;
    else
    {
        const int flags = (action == 0 || action == 3) ? O_RDWR | O_TRUNC : writes ? O_RDWR : O_RDONLY;
        h->fd = open(h->path, flags);
        if (h->fd < 0 && flags == O_RDWR && (errno == EACCES || errno == EROFS))
            h->fd = open(h->path, O_RDONLY); // asked for more than it needs, often
        if (h->fd < 0)
            return from_errno(errno);
    }
    if (options & 0x1000) // DELETE_ON_CLOSE
    {
        if (h->is_dir && !folder_is_empty(h->path))
        {
            close_handle(c, h);
            return ST_DIRECTORY_NOT_EMPTY;
        }
        h->delete_pending = 1;
    }
    h->used = 1;
    c->last_file = (uint64_t)slot + 1;
    if (stat_handle(h, &info) != 0)
        memset(&info, 0, sizeof(info));
    facts_t f;
    facts_of(c, &info, base_name(h->path), &f);
    const size_t at = add_small(c, 89, 89);
    if (at == (size_t)-1)
        return ST_UNSUCCESSFUL;
    uint8_t *b = c->out.data + at;
    wr32(b + 4, action);
    write_create_facts(c, at + 8, &f);
    wr64(b + 64, (uint64_t)slot + 1);
    wr64(b + 72, (uint64_t)slot + 1);
    if (action == 2)
        say(c->config, "%s: created %s", c->peer, h->path);
    return ST_OK;
}

static uint32_t do_close(conn_t *c, const request_t *r)
{
    if (!body_fits(r, 24))
        return ST_INVALID_PARAMETER;
    handle_t *h = find_handle(c, r->body + 8, r->related);
    if (h == NULL)
        return ST_INVALID_HANDLE;
    const size_t at = add_small(c, 60, 60);
    if (at == (size_t)-1)
        return ST_UNSUCCESSFUL;
    struct stat info;
    if ((rd16(r->body + 2) & 1) && !h->is_pipe && stat_handle(h, &info) == 0)
    {
        facts_t f;
        facts_of(c, &info, base_name(h->path), &f);
        wr16(c->out.data + at + 2, 1);
        write_create_facts(c, at + 8, &f);
    }
    close_handle(c, h);
    return ST_OK;
}

// ---- srvsvc over the IPC$ pipe: the share list ----

static void ndr32(buf_t *b, uint32_t v)
{
    const size_t at = buf_add(b, 4);
    if (at != (size_t)-1)
        wr32(b->data + at, v);
}

static void ndr_string(buf_t *b, const char *text)
{
    uint8_t wide[256];
    const size_t n = utf8_to_utf16(text, wide, sizeof(wide) - 2);
    wr16(wide + n, 0);
    const uint32_t count = (uint32_t)(n / 2 + 1);
    ndr32(b, count);
    ndr32(b, 0);
    ndr32(b, count);
    buf_put(b, wide, n + 2);
    while (b->size % 4 != 0)
        buf_add(b, 1);
}

static void rpc_answer(conn_t *c, handle_t *h, const uint8_t *in, size_t n)
{
    free(h->pipe_out);
    h->pipe_out = NULL;
    h->pipe_out_size = 0;
    if (n < 16 || in[0] != 5)
        return;
    const uint8_t type = in[2];
    const uint32_t call = rd32(in + 12);
    buf_t out = {NULL, 0, 0};
    buf_add(&out, 16);
    if (type == 11 && n >= 28) // bind
    {
        ndr32(&out, 4280u | 4280u << 16); // max transmit and receive fragments
        ndr32(&out, 0x53f0);             // association group
        static const char kAddress[] = "\\PIPE\\srvsvc";
        const size_t at = buf_add(&out, 2);
        wr16(out.data + at, sizeof(kAddress));
        buf_put(&out, kAddress, sizeof(kAddress));
        while (out.size % 4 != 0)
            buf_add(&out, 1);
        const int contexts = in[24];
        const size_t results = buf_add(&out, 4);
        out.data[results] = (uint8_t)contexts;
        size_t p = 28;
        for (int i = 0; i < contexts && p + 24 <= n; ++i)
        {
            const int transfers = in[p + 2];
            int accepted = 0;
            for (int t = 0; t < transfers && p + 24 + 20 * (size_t)t + 20 <= n; ++t)
            {
                if (memcmp(in + p + 24 + 20 * (size_t)t, kNdr, 20) == 0)
                    accepted = 1;
            }
            const size_t at_result = buf_add(&out, 24);
            if (at_result == (size_t)-1)
                break;
            wr16(out.data + at_result, accepted ? 0 : 2);
            wr16(out.data + at_result + 2, accepted ? 0 : 2);
            if (accepted)
                memcpy(out.data + at_result + 4, kNdr, 20);
            p += 24 + 20 * (size_t)transfers;
        }
        out.data[2] = 12; // bind_ack
    }
    else if (type == 0 && n >= 24) // request
    {
        const uint16_t operation = rd16(in + 22);
        if (operation == 15) // NetShareEnumAll, answered at level 1
        {
            const size_t stub_start = out.size;
            buf_add(&out, 8); // alloc hint, context, cancel count
            const int count = c->config->share_count + 1;
            ndr32(&out, 1);
            ndr32(&out, 1);
            ndr32(&out, 0x00020000);
            ndr32(&out, (uint32_t)count);
            ndr32(&out, 0x00020004);
            ndr32(&out, (uint32_t)count);
            for (int i = 0; i < count; ++i)
            {
                ndr32(&out, 0x00020008u + 8u * (uint32_t)i);
                ndr32(&out, i < c->config->share_count ? 0u : 0x80000003u);
                ndr32(&out, 0x0002000cu + 8u * (uint32_t)i);
            }
            for (int i = 0; i < count; ++i)
            {
                const int disk = i < c->config->share_count;
                ndr_string(&out, disk ? c->config->shares[i].name : "IPC$");
                ndr_string(&out, disk ? c->config->shares[i].path : "IPC Service");
            }
            ndr32(&out, (uint32_t)count); // total entries
            ndr32(&out, 0x00020100);      // the resume handle
            ndr32(&out, 0);
            ndr32(&out, 0); // WERR_OK
            wr32(out.data + stub_start, (uint32_t)(out.size - stub_start - 8));
            out.data[2] = 2; // response
        }
        else
        {
            buf_add(&out, 8);
            ndr32(&out, 0x1c010002); // nca_op_rng_error
            ndr32(&out, 0);
            out.data[2] = 3; // fault
        }
    }
    else
    {
        free(out.data);
        return;
    }
    if (out.data == NULL)
        return;
    out.data[0] = 5;
    out.data[3] = 0x03; // first and last fragment
    wr32(out.data + 4, 0x10);
    wr16(out.data + 8, (uint32_t)out.size);
    wr32(out.data + 12, call);
    h->pipe_out = out.data;
    h->pipe_out_size = out.size;
}

// ---- reading and writing ----

static uint32_t do_read(conn_t *c, const request_t *r)
{
    if (!body_fits(r, 48))
        return ST_INVALID_PARAMETER;
    handle_t *h = find_handle(c, r->body + 16, r->related);
    if (h == NULL)
        return ST_INVALID_HANDLE;
    uint32_t length = rd32(r->body + 4);
    const uint64_t offset = rd64(r->body + 8);
    if (length > MAX_IO)
        length = MAX_IO;
    if (h->is_pipe)
    {
        const size_t n = h->pipe_out_size < length ? h->pipe_out_size : length;
        if (n == 0)
            return ST_END_OF_FILE;
        const size_t at = add_small(c, 17, 16 + n);
        if (at == (size_t)-1)
            return ST_UNSUCCESSFUL;
        uint8_t *b = c->out.data + at;
        b[2] = 0x50;
        wr32(b + 4, (uint32_t)n);
        memcpy(b + 16, h->pipe_out, n);
        free(h->pipe_out);
        h->pipe_out = NULL;
        h->pipe_out_size = 0;
        return ST_OK;
    }
    if (h->is_dir || h->fd < 0)
        return ST_INVALID_DEVICE_REQUEST;
    if (!buf_reserve(&c->out, 16 + (size_t)length + 8))
        return ST_UNSUCCESSFUL;
    const size_t at = add_small(c, 17, 16);
    size_t got = 0;
    while (got < length)
    {
        const ssize_t n = pread(h->fd, c->out.data + at + 16 + got, length - got,
                                (off_t)(offset + got));
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            return from_errno(errno);
        if (n == 0)
            break;
        got += (size_t)n;
    }
    if (got == 0 && length > 0)
    {
        c->out.size = at; // the error body goes here
        return ST_END_OF_FILE;
    }
    c->out.size = at + 16 + got;
    uint8_t *b = c->out.data + at;
    b[2] = 0x50;
    wr32(b + 4, (uint32_t)got);
    return ST_OK;
}

static uint32_t do_write(conn_t *c, const request_t *r)
{
    if (!body_fits(r, 48))
        return ST_INVALID_PARAMETER;
    handle_t *h = find_handle(c, r->body + 16, r->related);
    if (h == NULL)
        return ST_INVALID_HANDLE;
    const uint32_t length = rd32(r->body + 4);
    const uint64_t offset = rd64(r->body + 8);
    const uint8_t *data = request_bytes(r, rd16(r->body + 2), length);
    if (data == NULL)
        return ST_INVALID_PARAMETER;
    if (h->is_pipe)
    {
        rpc_answer(c, h, data, length);
    }
    else
    {
        if (c->config->read_only)
            return ST_ACCESS_DENIED;
        if (h->is_dir || h->fd < 0)
            return ST_INVALID_DEVICE_REQUEST;
        size_t put = 0;
        while (put < length)
        {
            const ssize_t n = pwrite(h->fd, data + put, length - put, (off_t)(offset + put));
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                return from_errno(n < 0 ? errno : ENOSPC);
            put += (size_t)n;
        }
    }
    const size_t at = add_small(c, 17, 17);
    if (at == (size_t)-1)
        return ST_UNSUCCESSFUL;
    wr32(c->out.data + at + 4, length);
    return ST_OK;
}

// ---- listing a folder ----

static int compare_names(const void *a, const void *b)
{
    return strcasecmp(*(char *const *)a, *(char *const *)b);
}

static int list_folder(handle_t *h)
{
    free_listing(h);
    DIR *dir = opendir(h->path);
    if (dir == NULL)
        return 0;
    int cap = 64;
    h->names = (char **)malloc(sizeof(char *) * (size_t)cap);
    if (h->names == NULL)
    {
        closedir(dir);
        return 0;
    }
    h->names[h->name_count++] = strdup(".");
    h->names[h->name_count++] = strdup("..");
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (h->name_count == cap)
        {
            cap *= 2;
            char **more = (char **)realloc(h->names, sizeof(char *) * (size_t)cap);
            if (more == NULL)
                break;
            h->names = more;
        }
        h->names[h->name_count++] = strdup(entry->d_name);
    }
    closedir(dir);
    qsort(h->names + 2, (size_t)(h->name_count - 2), sizeof(char *), compare_names);
    h->listed = 1;
    h->position = 0;
    return 1;
}

static uint32_t do_query_directory(conn_t *c, const request_t *r)
{
    if (!body_fits(r, 32))
        return ST_INVALID_PARAMETER;
    handle_t *h = find_handle(c, r->body + 8, r->related);
    if (h == NULL)
        return ST_INVALID_HANDLE;
    if (!h->is_dir)
        return ST_INVALID_PARAMETER;
    const uint8_t info_class = r->body[2];
    const uint8_t flags = r->body[3];
    const uint8_t *pattern16 = request_bytes(r, rd16(r->body + 24), rd16(r->body + 26));
    uint32_t room = rd32(r->body + 28);
    if (room > MAX_IO)
        room = MAX_IO;
    size_t fixed;
    switch (info_class)
    {
    case 1: // FileDirectoryInformation
        fixed = 64;
        break;
    case 2: // FileFullDirectoryInformation
        fixed = 68;
        break;
    case 3: // FileBothDirectoryInformation
        fixed = 94;
        break;
    case 12: // FileNamesInformation
        fixed = 12;
        break;
    case 37: // FileIdBothDirectoryInformation
        fixed = 104;
        break;
    case 38: // FileIdFullDirectoryInformation
        fixed = 80;
        break;
    default:
        return ST_INVALID_INFO_CLASS;
    }
    if ((flags & 0x11) || !h->listed)
    {
        char pattern[256] = "*";
        if (pattern16 != NULL && rd16(r->body + 26) > 0)
            utf16_to_utf8(pattern16, rd16(r->body + 26), pattern, sizeof(pattern));
        snprintf(h->pattern, sizeof(h->pattern), "%s", pattern);
        if (!list_folder(h))
            return from_errno(errno);
    }
    const int first = h->position == 0;
    const size_t at = add_small(c, 9, 8);
    if (at == (size_t)-1)
        return ST_UNSUCCESSFUL;
    const size_t entries = c->out.size;
    size_t last = (size_t)-1;
    int count = 0;
    while (h->position < h->name_count)
    {
        const char *name = h->names[h->position];
        if (!smb_wildcard_match(h->pattern, name))
        {
            ++h->position;
            continue;
        }
        uint8_t name16[1024];
        const size_t name_n = utf8_to_utf16(name, name16, sizeof(name16));
        const size_t size = fixed + name_n;
        const size_t aligned_start = (c->out.size - entries + 7) & ~(size_t)7;
        if (aligned_start + size > room)
        {
            if (count == 0)
            {
                c->out.size = at;
                return ST_BUFFER_TOO_SMALL;
            }
            break;
        }
        char path[MAX_PATH_BYTES + 260];
        snprintf(path, sizeof(path), "%s/%s", h->path, name);
        struct stat info;
        if (stat(path, &info) != 0)
        {
            ++h->position; // gone since the listing, or a broken link
            continue;
        }
        facts_t f;
        facts_of(c, &info, name, &f);
        const size_t pad = entries + aligned_start - c->out.size;
        if (buf_add(&c->out, pad + size) == (size_t)-1)
            return ST_UNSUCCESSFUL;
        uint8_t *e = c->out.data + entries + aligned_start;
        if (last != (size_t)-1)
            wr32(c->out.data + entries + last, (uint32_t)(aligned_start - last));
        last = aligned_start;
        if (info_class == 12)
        {
            wr32(e + 8, (uint32_t)name_n);
            memcpy(e + 12, name16, name_n);
        }
        else
        {
            wr64(e + 8, f.created);
            wr64(e + 16, f.accessed);
            wr64(e + 24, f.written);
            wr64(e + 32, f.changed);
            wr64(e + 40, f.size);
            wr64(e + 48, f.allocation);
            wr32(e + 56, f.attributes);
            wr32(e + 60, (uint32_t)name_n);
            if (info_class == 37)
                wr64(e + 96, f.id);
            if (info_class == 38)
                wr64(e + 72, f.id);
            memcpy(e + fixed, name16, name_n);
        }
        ++count;
        ++h->position;
        if (flags & 0x2) // RETURN_SINGLE_ENTRY
            break;
    }
    if (count == 0)
    {
        c->out.size = at;
        return first ? ST_NO_SUCH_FILE : ST_NO_MORE_FILES;
    }
    uint8_t *b = c->out.data + at;
    wr16(b + 2, 72);
    wr32(b + 4, (uint32_t)(c->out.size - entries));
    return ST_OK;
}

// ---- asking about a file, a drive or its owner ----

static size_t name_info(const conn_t *c, const handle_t *h, uint8_t *out, size_t cap)
{
    // "\" and the path under the share, with backslashes.
    const smb_share_t *share = NULL;
    const int tree = h->tree - 1;
    if (tree >= 0 && tree < MAX_TREES && c->trees[tree].share >= 0)
        share = &c->config->shares[c->trees[tree].share];
    const char *relative = h->path;
    if (share != NULL && strncmp(h->path, share->path, strlen(share->path)) == 0)
        relative = h->path + strlen(share->path);
    char name[MAX_PATH_BYTES];
    snprintf(name, sizeof(name), "%s%s", relative[0] == '/' ? "" : "\\", relative);
    for (char *p = name; *p != 0; ++p)
    {
        if (*p == '/')
            *p = '\\';
    }
    const size_t n = utf8_to_utf16(name, out + 4, cap - 4);
    wr32(out, (uint32_t)n);
    return 4 + n;
}

static uint32_t file_info(conn_t *c, const handle_t *h, uint8_t info_class, uint8_t *out,
                          size_t cap, size_t *size)
{
    struct stat st;
    memset(&st, 0, sizeof(st));
    if (!h->is_pipe && stat_handle(h, &st) != 0)
        return from_errno(errno);
    facts_t f;
    facts_of(c, &st, base_name(h->path), &f);
    if (h->is_pipe)
        f.attributes = 0x80;
    memset(out, 0, cap);
    switch (info_class)
    {
    case 4: // Basic
        wr64(out, f.created);
        wr64(out + 8, f.accessed);
        wr64(out + 16, f.written);
        wr64(out + 24, f.changed);
        wr32(out + 32, f.attributes);
        *size = 40;
        return ST_OK;
    case 5: // Standard
        wr64(out, f.allocation);
        wr64(out + 8, f.size);
        wr32(out + 16, 1);
        out[20] = (uint8_t)h->delete_pending;
        out[21] = (uint8_t)f.is_dir;
        *size = 24;
        return ST_OK;
    case 6: // Internal
        wr64(out, f.id);
        *size = 8;
        return ST_OK;
    case 7:  // EA
    case 16: // Mode
        *size = 4;
        return ST_OK;
    case 8: // Access
        wr32(out, c->config->read_only ? 0x001200a9u : 0x001f01ffu);
        *size = 4;
        return ST_OK;
    case 9:  // Name
    case 48: // NormalizedName
        *size = name_info(c, h, out, cap);
        return ST_OK;
    case 14: // Position
        *size = 8;
        return ST_OK;
    case 17: // Alignment
        *size = 4;
        return ST_OK;
    case 18: // All: Basic, Standard, Internal, EA, Access, Position, Mode, Alignment, Name
    {
        size_t part;
        file_info(c, h, 4, out, 40, &part);
        file_info(c, h, 5, out + 40, 24, &part);
        wr64(out + 64, f.id);
        wr32(out + 76, c->config->read_only ? 0x001200a9u : 0x001f01ffu);
        *size = 96 + name_info(c, h, out + 96, cap - 96);
        return ST_OK;
    }
    case 22: // Stream: the data stream of a file, none for a folder
        if (f.is_dir)
        {
            *size = 0;
            return ST_OK;
        }
        wr32(out + 4, 14);
        wr64(out + 8, f.size);
        wr64(out + 16, f.allocation);
        *size = 24 + utf8_to_utf16("::$DATA", out + 24, cap - 24);
        return ST_OK;
    case 28: // Compression
        wr64(out, f.size);
        *size = 16;
        return ST_OK;
    case 34: // NetworkOpen
        wr64(out, f.created);
        wr64(out + 8, f.accessed);
        wr64(out + 16, f.written);
        wr64(out + 24, f.changed);
        wr64(out + 32, f.allocation);
        wr64(out + 40, f.size);
        wr32(out + 48, f.attributes);
        *size = 56;
        return ST_OK;
    case 35: // AttributeTag
        wr32(out, f.attributes);
        *size = 8;
        return ST_OK;
    case 59: // Id
        wr64(out, (uint64_t)st.st_dev);
        wr64(out + 8, f.id);
        *size = 24;
        return ST_OK;
    default:
        return ST_INVALID_INFO_CLASS;
    }
}

static uint32_t drive_info(conn_t *c, const handle_t *h, uint8_t info_class, uint8_t *out,
                           size_t cap, size_t *size)
{
    memset(out, 0, cap);
    struct statvfs fs;
    memset(&fs, 0, sizeof(fs));
    if (!h->is_pipe)
        statvfs(h->path, &fs);
    const uint64_t unit = fs.f_frsize != 0 ? (uint64_t)fs.f_frsize : 4096u;
    // Allocation units of 4096 bytes, as 8 sectors of 512.
    const uint64_t total = (uint64_t)fs.f_blocks * unit / 4096u;
    const uint64_t free_units = (uint64_t)fs.f_bavail * unit / 4096u;
    switch (info_class)
    {
    case 1: // Volume
    {
        const int tree = h->tree - 1;
        const char *label = tree >= 0 && tree < MAX_TREES && c->trees[tree].share >= 0
                                ? c->config->shares[c->trees[tree].share].name
                                : "IPC";
        wr64(out, filetime(0));
        wr32(out + 8, 0x50533500u); // a serial number: "PS5"
        const size_t n = utf8_to_utf16(label, out + 18, cap - 18);
        wr32(out + 12, (uint32_t)n);
        *size = 18 + n;
        return ST_OK;
    }
    case 3: // Size
        wr64(out, total);
        wr64(out + 8, free_units);
        wr32(out + 16, 8);
        wr32(out + 20, 512);
        *size = 24;
        return ST_OK;
    case 4: // Device: a disk
        wr32(out, 7);
        wr32(out + 4, 0x20);
        *size = 8;
        return ST_OK;
    case 5: // Attribute: case kept, Unicode names; NTFS, as Windows best understands
        wr32(out, 0x2u | 0x4u);
        wr32(out + 4, 255);
        *size = 12 + utf8_to_utf16("NTFS", out + 12, cap - 12);
        wr32(out + 8, (uint32_t)(*size - 12));
        return ST_OK;
    case 7: // FullSize
        wr64(out, total);
        wr64(out + 8, free_units);
        wr64(out + 16, free_units);
        wr32(out + 24, 8);
        wr32(out + 28, 512);
        *size = 32;
        return ST_OK;
    case 11: // SectorSize
        wr32(out, 512);
        wr32(out + 4, 512);
        wr32(out + 8, 512);
        wr32(out + 12, 512);
        wr32(out + 16, 0x3);
        *size = 28;
        return ST_OK;
    default:
        return ST_INVALID_INFO_CLASS;
    }
}

// Everyone may do everything: a descriptor Windows reads in Properties and
// before a copy ([MS-DTYP] 2.4.6, self-relative).
static size_t security_info(uint8_t *out)
{
    static const uint8_t kEveryone[12] = {1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0};
    memset(out, 0, 72);
    out[0] = 1;
    wr16(out + 2, 0x8004); // self-relative, DACL present
    wr32(out + 4, 20);
    wr32(out + 8, 32);
    wr32(out + 16, 44);
    memcpy(out + 20, kEveryone, 12);
    memcpy(out + 32, kEveryone, 12);
    out[44] = 2; // ACL revision
    wr16(out + 46, 28);
    wr16(out + 48, 1);
    out[52] = 0; // ACCESS_ALLOWED
    out[53] = 3; // inherited by files and folders
    wr16(out + 54, 20);
    wr32(out + 56, 0x001f01ff);
    memcpy(out + 60, kEveryone, 12);
    return 72;
}

static uint32_t do_query_info(conn_t *c, const request_t *r)
{
    if (!body_fits(r, 40))
        return ST_INVALID_PARAMETER;
    handle_t *h = find_handle(c, r->body + 24, r->related);
    if (h == NULL)
        return ST_INVALID_HANDLE;
    const uint8_t type = r->body[2];
    const uint8_t info_class = r->body[3];
    const uint32_t room = rd32(r->body + 4);
    uint8_t info[4096];
    size_t size = 0;
    uint32_t status;
    if (type == 1)
        status = file_info(c, h, info_class, info, sizeof(info), &size);
    else if (type == 2)
        status = drive_info(c, h, info_class, info, sizeof(info), &size);
    else if (type == 3)
    {
        size = security_info(info);
        status = ST_OK;
    }
    else
        status = ST_NOT_SUPPORTED;
    if (status != ST_OK)
        return status;
    if (size > room)
    {
        // Fixed-size answers that do not fit are refused; names are cut.
        if (type == 3)
        {
            // The error body carries the size it needs.
            const size_t at = add_small(c, 9, 12);
            if (at == (size_t)-1)
                return ST_UNSUCCESSFUL;
            wr32(c->out.data + at + 4, 4);
            wr32(c->out.data + at + 8, (uint32_t)size);
            return ST_BUFFER_TOO_SMALL;
        }
        if (!(type == 1 && (info_class == 9 || info_class == 18 || info_class == 48 ||
                            info_class == 22)) &&
            !(type == 2 && (info_class == 1 || info_class == 5)))
            return ST_INFO_LENGTH_MISMATCH;
        size = room;
        status = ST_BUFFER_OVERFLOW;
    }
    const size_t at = add_small(c, 9, 8 + (size > 0 ? size : 1));
    if (at == (size_t)-1)
        return ST_UNSUCCESSFUL;
    uint8_t *b = c->out.data + at;
    wr16(b + 2, 72);
    wr32(b + 4, (uint32_t)size);
    memcpy(b + 8, info, size);
    return status;
}

static uint32_t do_set_info(conn_t *c, const request_t *r)
{
    if (!body_fits(r, 32))
        return ST_INVALID_PARAMETER;
    handle_t *h = find_handle(c, r->body + 16, r->related);
    if (h == NULL)
        return ST_INVALID_HANDLE;
    const uint8_t type = r->body[2];
    const uint8_t info_class = r->body[3];
    const uint32_t length = rd32(r->body + 4);
    const uint8_t *in = request_bytes(r, rd16(r->body + 8), length);
    if (in == NULL)
        return ST_INVALID_PARAMETER;
    if (type != 1 || h->is_pipe)
    {
        add_small(c, 2, 2); // security and drive changes: taken, and left
        return ST_OK;
    }
    if (c->config->read_only)
        return ST_ACCESS_DENIED;
    switch (info_class)
    {
    case 4: // Basic: the times that are given
        if (length >= 40)
        {
            const uint64_t accessed = rd64(in + 8);
            const uint64_t written = rd64(in + 16);
            if ((written != 0 && written != UINT64_MAX) || (accessed != 0 && accessed != UINT64_MAX))
            {
                struct stat st;
                if (stat_handle(h, &st) == 0)
                {
                    struct timeval times[2];
                    times[0].tv_sec = accessed != 0 && accessed != UINT64_MAX ? unix_time(accessed)
                                                                              : st.st_atime;
                    times[0].tv_usec = 0;
                    times[1].tv_sec =
                        written != 0 && written != UINT64_MAX ? unix_time(written) : st.st_mtime;
                    times[1].tv_usec = 0;
                    utimes(h->path, times);
                }
            }
        }
        break;
    case 10: // Rename
    {
        if (length < 20)
            return ST_INVALID_PARAMETER;
        const size_t name_n = rd32(in + 16);
        if (name_n > length - 20)
            return ST_INVALID_PARAMETER;
        const int tree = h->tree - 1;
        if (tree < 0 || tree >= MAX_TREES || c->trees[tree].share < 0)
            return ST_INVALID_PARAMETER;
        char target[MAX_PATH_BYTES];
        if (smb_share_path(c->config->shares[c->trees[tree].share].path, in + 20, name_n, target,
                           sizeof(target)) == 0)
            return ST_OBJECT_NAME_INVALID;
        struct stat st;
        if (strcmp(target, h->path) != 0 && strcasecmp(target, h->path) != 0 &&
            stat(target, &st) == 0)
        {
            if (!in[0])
                return ST_OBJECT_NAME_COLLISION;
        }
        if (rename(h->path, target) != 0)
            return from_errno(errno);
        say(c->config, "%s: renamed %s to %s", c->peer, h->path, target);
        snprintf(h->path, sizeof(h->path), "%s", target);
        break;
    }
    case 13: // Disposition
    case 64: // DispositionEx
    {
        const int remove = info_class == 13 ? (length >= 1 && in[0] != 0)
                                            : (length >= 4 && (rd32(in) & 1u) != 0);
        if (remove && h->is_dir && !folder_is_empty(h->path))
            return ST_DIRECTORY_NOT_EMPTY;
        h->delete_pending = remove;
        break;
    }
    case 19: // Allocation: nothing to reserve
        break;
    case 20: // EndOfFile
        if (length < 8 || h->fd < 0)
            return ST_INVALID_PARAMETER;
        if (ftruncate(h->fd, (off_t)rd64(in)) != 0)
            return from_errno(errno);
        break;
    default:
        return ST_INVALID_INFO_CLASS;
    }
    add_small(c, 2, 2);
    return ST_OK;
}

static uint32_t do_ioctl(conn_t *c, const request_t *r)
{
    if (!body_fits(r, 56))
        return ST_INVALID_PARAMETER;
    const uint32_t code = rd32(r->body + 4);
    if (code == 0x00060194u || code == 0x000601b0u) // DFS referrals: not a DFS server
        return ST_FS_DRIVER_REQUIRED;
    if (code != 0x0011c017u) // only FSCTL_PIPE_TRANSCEIVE
        return ST_NOT_SUPPORTED;
    handle_t *h = find_handle(c, r->body + 8, r->related);
    if (h == NULL || !h->is_pipe)
        return ST_INVALID_HANDLE;
    const uint8_t *in = request_bytes(r, rd32(r->body + 24), rd32(r->body + 28));
    if (in == NULL)
        return ST_INVALID_PARAMETER;
    rpc_answer(c, h, in, rd32(r->body + 28));
    size_t n = h->pipe_out_size;
    const uint32_t most = rd32(r->body + 44);
    if (n > most)
        n = most;
    const size_t at = add_small(c, 49, 48 + (n > 0 ? n : 1));
    if (at == (size_t)-1)
        return ST_UNSUCCESSFUL;
    uint8_t *b = c->out.data + at;
    wr32(b + 4, code);
    memcpy(b + 8, r->body + 8, 16);
    wr32(b + 24, 112);
    wr32(b + 32, 112);
    wr32(b + 36, (uint32_t)n);
    if (n > 0)
        memcpy(b + 48, h->pipe_out, n);
    free(h->pipe_out);
    h->pipe_out = NULL;
    h->pipe_out_size = 0;
    return ST_OK;
}

// ---- one message: its commands, their answers, signed ----

static void sign(const conn_t *c, uint8_t *message, size_t size)
{
    wr32(message + 16, rd32(message + 16) | FLAG_SIGNED);
    memset(message + 48, 0, 16);
    smb_hmac_sha256_t mac;
    smb_hmac_sha256_init(&mac, c->key, 16);
    smb_hmac_sha256_update(&mac, message, size);
    uint8_t digest[32];
    smb_hmac_sha256_final(&mac, digest);
    memcpy(message + 48, digest, 16);
}

static int send_all(int fd, const uint8_t *data, size_t size)
{
    while (size > 0)
    {
#ifdef MSG_NOSIGNAL
        const ssize_t n = send(fd, data, size, MSG_NOSIGNAL);
#else
        const ssize_t n = send(fd, data, size, 0);
#endif
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return 0;
        data += n;
        size -= (size_t)n;
    }
    return 1;
}

static int is_soft(uint32_t status)
{
    // Statuses that come with an ordinary body, not the error one.
    return status == ST_OK || status == ST_MORE_PROCESSING || status == ST_BUFFER_OVERFLOW;
}

// An SMB1 NEGOTIATE (older clients open with one): answered with SMB2's,
// dialect 2.??? so the client asks again in SMB2.
static int answer_smb1(conn_t *c, const uint8_t *message, size_t size)
{
    if (size < 35 || message[4] != 0x72)
        return 0;
    int wildcard = 0;
    int plain = 0;
    for (size_t i = 35; i + 9 <= size; ++i)
    {
        if (memcmp(message + i, "SMB 2.???", 9) == 0)
            wildcard = 1;
        if (memcmp(message + i, "SMB 2.002", 9) == 0)
            plain = 1;
    }
    if (!wildcard && !plain)
        return 0;
    c->out.size = 0;
    buf_add(&c->out, 4 + 64);
    uint8_t blob[96];
    const size_t blob_n = spnego_offer(blob);
    const size_t at = buf_add(&c->out, 64 + blob_n);
    if (at == (size_t)-1)
        return 0;
    uint8_t *h = c->out.data + 4;
    memcpy(h, "\xfeSMB", 4);
    wr16(h + 4, 64);
    wr16(h + 14, 1);
    wr32(h + 16, FLAG_RESPONSE);
    uint8_t *b = c->out.data + at;
    c->dialect = wildcard ? 0x02ff : 0x0202;
    wr16(b, 65);
    wr16(b + 2, 0x03);
    wr16(b + 4, c->dialect);
    random_bytes(b + 8, 16);
    wr32(b + 28, 65536);
    wr32(b + 32, 65536);
    wr32(b + 36, 65536);
    wr64(b + 40, now_filetime());
    wr16(b + 56, 128);
    wr16(b + 58, (uint32_t)blob_n);
    memcpy(b + 64, blob, blob_n);
    const size_t length = c->out.size - 4;
    c->out.data[0] = 0;
    c->out.data[1] = (uint8_t)(length >> 16);
    c->out.data[2] = (uint8_t)(length >> 8);
    c->out.data[3] = (uint8_t)length;
    return send_all(c->fd, c->out.data, c->out.size);
}

static int handle_message(conn_t *c, const uint8_t *message, size_t size)
{
    if (size >= 4 && message[0] == 0xff && memcmp(message + 1, "SMB", 3) == 0)
        return answer_smb1(c, message, size);
    c->out.size = 0;
    if (buf_add(&c->out, 4) == (size_t)-1)
        return 0;
    size_t starts[64];
    int sign_these[64];
    int count = 0;
    size_t position = 0;
    uint32_t chain_status = ST_OK;
    for (;;)
    {
        if (position + 64 > size || memcmp(message + position, "\xfeSMB", 4) != 0)
            return 0;
        const uint8_t *hdr = message + position;
        const uint32_t next = rd32(hdr + 20);
        request_t r;
        r.hdr = hdr;
        r.body = hdr + 64;
        r.size = next != 0 ? next : size - position;
        r.related = (rd32(hdr + 16) & FLAG_RELATED) != 0;
        if (r.size < 64 || r.size > size - position)
            return 0;
        const uint16_t command = rd16(hdr + 12);
        uint64_t session = rd64(hdr + 40);
        uint32_t tree = rd32(hdr + 36);
        if (r.related)
        {
            session = c->last_session;
            tree = c->last_tree;
        }
        if (command == CMD_CANCEL)
        {
            // No answer to a cancel; nothing waits to be cancelled.
            if (next == 0)
                break;
            position += next;
            continue;
        }
        if (count == 64)
            return 0;
        const size_t start = buf_add(&c->out, 64);
        if (start == (size_t)-1)
            return 0;
        starts[count] = start;
        sign_these[count] = 0;
        ++count;
        r.response = start;
        uint16_t grant = rd16(hdr + 14);
        const uint16_t charge = rd16(hdr + 6);
        if (grant < charge)
            grant = charge;
        if (grant == 0)
            grant = 1;
        if (grant > 512)
            grant = 512;
        uint8_t *h = c->out.data + start;
        memcpy(h, "\xfeSMB", 4);
        wr16(h + 4, 64);
        wr16(h + 6, charge);
        wr16(h + 12, command);
        wr16(h + 14, grant);
        wr32(h + 16, FLAG_RESPONSE | (r.related ? FLAG_RELATED : 0));
        wr64(h + 24, rd64(hdr + 24));
        wr32(h + 32, rd32(hdr + 32));
        wr32(h + 36, tree);
        wr64(h + 40, session);

        uint32_t status = ST_OK;
        const int signed_in = c->session_state == 2 && session == c->session_id;
        const int tree_ok = tree >= 1 && tree <= MAX_TREES && c->trees[tree - 1].used;
        const int share = tree_ok ? c->trees[tree - 1].share : -2;
        if (r.related && chain_status != ST_OK)
            status = chain_status;
        else
        {
            switch (command)
            {
            case CMD_NEGOTIATE:
                status = do_negotiate(c, &r);
                break;
            case CMD_SESSION_SETUP:
                status = do_session_setup(c, &r);
                session = c->session_id;
                break;
            case CMD_ECHO:
                add_small(c, 4, 4);
                break;
            default:
                if (!signed_in)
                    status = ST_USER_SESSION_DELETED;
                else
                    switch (command)
                    {
                    case CMD_LOGOFF:
                        close_all(c, -1);
                        memset(c->trees, 0, sizeof(c->trees));
                        add_small(c, 4, 4);
                        c->session_state = 0;
                        sign_these[count - 1] = 1;
                        break;
                    case CMD_TREE_CONNECT:
                        status = do_tree_connect(c, &r, &tree);
                        wr32(c->out.data + start + 36, tree);
                        break;
                    default:
                        if (!tree_ok)
                            status = ST_NETWORK_NAME_DELETED;
                        else
                            switch (command)
                            {
                            case CMD_TREE_DISCONNECT:
                                close_all(c, (int)tree);
                                c->trees[tree - 1].used = 0;
                                add_small(c, 4, 4);
                                break;
                            case CMD_CREATE:
                                status = do_create(c, &r, share);
                                break;
                            case CMD_CLOSE:
                                status = do_close(c, &r);
                                break;
                            case CMD_FLUSH:
                            case CMD_LOCK:
                                add_small(c, 4, 4);
                                break;
                            case CMD_READ:
                                status = do_read(c, &r);
                                break;
                            case CMD_WRITE:
                                status = do_write(c, &r);
                                break;
                            case CMD_IOCTL:
                                status = do_ioctl(c, &r);
                                break;
                            case CMD_QUERY_DIRECTORY:
                                status = do_query_directory(c, &r);
                                break;
                            case CMD_QUERY_INFO:
                                status = do_query_info(c, &r);
                                break;
                            case CMD_SET_INFO:
                                status = do_set_info(c, &r);
                                break;
                            case CMD_CHANGE_NOTIFY:
                            default:
                                status = ST_NOT_SUPPORTED;
                                break;
                            }
                        break;
                    }
                break;
            }
        }
        if (!is_soft(status) && !(status == ST_BUFFER_TOO_SMALL && c->out.size == start + 64 + 12))
        {
            // The error body: 9 bytes, nothing in them.
            c->out.size = start + 64;
            if (buf_add(&c->out, 9) == (size_t)-1)
                return 0;
            c->out.data[start + 64] = 9;
        }
        h = c->out.data + start;
        wr32(h + 8, status);
        wr64(h + 40, session);
        if (status == ST_OK && command == CMD_SESSION_SETUP)
            sign_these[count - 1] = 1;
        if (command == CMD_CREATE && status != ST_OK)
            c->last_file = UINT64_MAX;
        if (!r.related)
            chain_status = ST_OK;
        if (status != ST_OK && status != ST_BUFFER_OVERFLOW)
            chain_status = status;
        c->last_session = session;
        c->last_tree = tree;
        if (command != CMD_NEGOTIATE && command != CMD_SESSION_SETUP && c->session_state == 2 &&
            session == c->session_id)
            sign_these[count - 1] = 1;
        if (next == 0)
            break;
        position += next;
        // The next answer starts 8-byte aligned, and this one says where.
        while ((c->out.size - start) % 8 != 0)
            buf_add(&c->out, 1);
        wr32(c->out.data + start + 20, (uint32_t)(c->out.size - start));
    }
    for (int i = 0; i < count; ++i)
    {
        const size_t end = i + 1 < count ? starts[i + 1] : c->out.size;
        if (sign_these[i] && c->dialect >= 0x0202)
            sign(c, c->out.data + starts[i], end - starts[i]);
    }
    if (count == 0)
        return 1;
    const size_t length = c->out.size - 4;
    c->out.data[0] = 0;
    c->out.data[1] = (uint8_t)(length >> 16);
    c->out.data[2] = (uint8_t)(length >> 8);
    c->out.data[3] = (uint8_t)length;
    return send_all(c->fd, c->out.data, c->out.size);
}

static int receive_all(int fd, uint8_t *data, size_t size)
{
    while (size > 0)
    {
        const ssize_t n = recv(fd, data, size, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return 0;
        data += n;
        size -= (size_t)n;
    }
    return 1;
}

// ---- connections ----

typedef struct
{
    pthread_t thread;
    int fd;
    int used;
    int done;
    const smb_config_t *config;
    char peer[48];
} slot_t;

static void *serve_connection(void *argument)
{
    slot_t *slot = (slot_t *)argument;
    conn_t *c = (conn_t *)calloc(1, sizeof(conn_t));
    if (c != NULL)
    {
        c->handles = (handle_t *)calloc(MAX_HANDLES, sizeof(handle_t));
        c->in = (uint8_t *)malloc(MAX_MESSAGE);
    }
    if (c != NULL && c->handles != NULL && c->in != NULL)
    {
        c->fd = slot->fd;
        c->config = slot->config;
        snprintf(c->peer, sizeof(c->peer), "%s", slot->peer);
        for (int i = 0; i < MAX_HANDLES; ++i)
            c->handles[i].fd = -1;
        buf_reserve(&c->out, MAX_IO + 4096);
        for (;;)
        {
            uint8_t head[4];
            if (!receive_all(c->fd, head, 4))
                break;
            const size_t length = (size_t)head[1] << 16 | (size_t)head[2] << 8 | head[3];
            if (head[0] != 0) // a keep-alive or a NetBIOS request: nothing to answer
            {
                if (length > 0 && length <= MAX_MESSAGE && !receive_all(c->fd, c->in, length))
                    break;
                continue;
            }
            if (length > MAX_MESSAGE || !receive_all(c->fd, c->in, length))
                break;
            if (!handle_message(c, c->in, length))
                break;
        }
        close_all(c, -1);
    }
    say(slot->config, "%s: gone", slot->peer);
    if (c != NULL)
    {
        free(c->handles);
        free(c->in);
        free(c->out.data);
        free(c);
    }
    close(slot->fd);
    slot->done = 1;
    return NULL;
}

int smb_serve(const smb_config_t *config)
{
    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0)
        return errno;
    int on = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)config->port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listener, (const struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(listener, 8) != 0)
    {
        const int error = errno;
        close(listener);
        return error;
    }
    say(config, "sharing %d drive(s) on port %d as \\\\%s", config->share_count, config->port,
        config->computer);
    slot_t slots[MAX_CONNECTIONS];
    memset(slots, 0, sizeof(slots));
    for (;;)
    {
        if (config->keep_going != NULL && !config->keep_going(config->context))
            break;
        for (int i = 0; i < MAX_CONNECTIONS; ++i)
        {
            if (slots[i].used && slots[i].done)
            {
                pthread_join(slots[i].thread, NULL);
                slots[i].used = 0;
            }
        }
        struct pollfd wait = {listener, POLLIN, 0};
        if (poll(&wait, 1, 1000) <= 0)
            continue;
        struct sockaddr_in from;
        socklen_t from_size = sizeof(from);
        const int fd = accept(listener, (struct sockaddr *)&from, &from_size);
        if (fd < 0)
            continue;
        int slot = -1;
        for (int i = 0; i < MAX_CONNECTIONS && slot < 0; ++i)
        {
            if (!slots[i].used)
                slot = i;
        }
        if (slot < 0)
        {
            close(fd);
            continue;
        }
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
        int buffer = 2 << 20;
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffer, sizeof(buffer));
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buffer, sizeof(buffer));
#ifdef SO_NOSIGPIPE
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
        slot_t *s = &slots[slot];
        memset(s, 0, sizeof(*s));
        s->fd = fd;
        s->config = config;
        s->used = 1;
        const uint32_t ip = ntohl(from.sin_addr.s_addr);
        snprintf(s->peer, sizeof(s->peer), "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255,
                 (ip >> 8) & 255, ip & 255);
        say(config, "%s: connected", s->peer);
        if (pthread_create(&s->thread, NULL, serve_connection, s) != 0)
        {
            close(fd);
            s->used = 0;
        }
    }
    close(listener);
    // Stopping: every connection is ended, and waited for.
    for (int i = 0; i < MAX_CONNECTIONS; ++i)
    {
        if (slots[i].used)
        {
            if (!slots[i].done)
                shutdown(slots[i].fd, SHUT_RDWR);
            pthread_join(slots[i].thread, NULL);
        }
    }
    say(config, "stopped sharing");
    return 0;
}
