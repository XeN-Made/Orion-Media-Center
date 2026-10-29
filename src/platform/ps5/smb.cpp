// ORION Media Center - SMB shares: listing and copying files off a Windows share or a NAS.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/ps5/smb.hpp"
#include "platform/ps5/smb_connection.hpp"

// smb2.h first: libsmb2.h uses its types.
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

// libsmb2 asks for TCP's protocol number by name (for TCP_NODELAY); the
// console's C library has no protocol database. TCP is 6 everywhere.
extern "C" struct protoent *getprotobyname(const char *name)
{
    static char tcp_name[] = "tcp";
    static char *no_aliases[] = {nullptr};
    static protoent tcp = {tcp_name, no_aliases, 6};
    return name != nullptr && std::strcmp(name, "tcp") == 0 ? &tcp : nullptr;
}

namespace hui::sys
{

SmbConnection::~SmbConnection()
{
    if (smb2_ == nullptr)
        return;
    if (connected_)
        smb2_disconnect_share(smb2_);
    smb2_destroy_context(smb2_);
}

bool SmbConnection::open(const SmbLogin &login, std::string *error)
{
    smb2_ = smb2_init_context();
    if (smb2_ == nullptr)
    {
        *error = "could not start SMB";
        return false;
    }
    // Signing where the server wants it; no answer for 20 s is an error, not
    // a wait for ever.
    smb2_set_security_mode(smb2_, SMB2_NEGOTIATE_SIGNING_ENABLED);
    smb2_set_timeout(smb2_, 20);
    if (!login.domain.empty())
        smb2_set_domain(smb2_, login.domain.c_str());
    const std::string user = login.user.empty() ? std::string("guest") : login.user;
    smb2_set_user(smb2_, user.c_str());
    smb2_set_password(smb2_, login.password.c_str());
    if (smb2_connect_share(smb2_, login.server.c_str(), login.share.c_str(), user.c_str()) < 0)
    {
        *error = said();
        return false;
    }
    connected_ = true;
    return true;
}

std::string SmbConnection::said() const
{
    const char *text = smb2_ != nullptr ? smb2_get_error(smb2_) : nullptr;
    return text != nullptr && *text != '\0' ? std::string(text) : std::string("no answer");
}

bool SmbConnection::list(const std::string &folder, const std::string &prefix, int depth,
                         std::size_t most, std::vector<SmbEntry> *entries, std::string *error)
{
    smb2dir *dir = smb2_opendir(smb2_, folder.c_str());
    if (dir == nullptr)
    {
        *error = folder.empty() ? said() : folder + ": " + said();
        return false;
    }
    std::vector<std::string> inner;
    while (const smb2dirent *entry = smb2_readdir(smb2_, dir))
    {
        const std::string name = entry->name != nullptr ? entry->name : "";
        if (name.empty() || name.front() == '.' || entries->size() >= most)
            continue;
        SmbEntry found;
        found.path = prefix.empty() ? name : prefix + "/" + name;
        found.directory = entry->st.smb2_type == SMB2_TYPE_DIRECTORY;
        found.size = entry->st.smb2_size;
        if (found.directory && depth > 0)
            inner.push_back(name);
        entries->push_back(std::move(found));
    }
    smb2_closedir(smb2_, dir);
    for (const std::string &name : inner)
    {
        if (entries->size() >= most)
            break;
        // A folder that cannot be read (no right to it) is left out; the
        // rest still lists.
        std::string ignored;
        list(folder.empty() ? name : folder + "/" + name,
             prefix.empty() ? name : prefix + "/" + name, depth - 1, most, entries, &ignored);
    }
    return true;
}

bool smb_list(const SmbLogin &login, const std::string &folder, int depth, std::size_t most,
              std::vector<SmbEntry> *entries, std::string *error)
{
    entries->clear();
    SmbConnection connection;
    if (!connection.open(login, error))
        return false;
    return connection.list(folder, "", depth, most, entries, error);
}

bool smb_download(const SmbLogin &login, const std::string &path, const std::string &local,
                  std::atomic<std::uint64_t> *done, const std::atomic<bool> *cancel,
                  std::string *error)
{
    if (done != nullptr)
        *done = 0;
    SmbConnection connection;
    if (!connection.open(login, error))
        return false;
    smb2fh *file = smb2_open(connection.get(), path.c_str(), O_RDONLY);
    if (file == nullptr)
    {
        *error = path + ": " + connection.said();
        return false;
    }
    const std::string part = local + ".part";
    const int fd = ::open(part.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
    {
        smb2_close(connection.get(), file);
        *error = "cannot write " + part + " (errno " + std::to_string(errno) + ")";
        return false;
    }
    // As much as the server takes in one read, up to 1 MB.
    std::uint32_t chunk = smb2_get_max_read_size(connection.get());
    if (chunk == 0 || chunk > (1u << 20))
        chunk = 1u << 20;
    std::vector<std::uint8_t> buffer(chunk);
    std::string why;
    std::uint64_t total = 0;
    for (;;)
    {
        if (cancel != nullptr && cancel->load())
        {
            why = "stopped";
            break;
        }
        const int got = smb2_read(connection.get(), file, buffer.data(), chunk);
        if (got < 0)
        {
            why = connection.said();
            break;
        }
        if (got == 0)
            break;
        std::size_t written = 0;
        while (written < static_cast<std::size_t>(got))
        {
            const ssize_t put =
                ::write(fd, buffer.data() + written, static_cast<std::size_t>(got) - written);
            if (put < 0 && errno == EINTR)
                continue;
            if (put <= 0)
            {
                why = "could not write the file (disk full?)";
                break;
            }
            written += static_cast<std::size_t>(put);
        }
        if (!why.empty())
            break;
        total += static_cast<std::uint64_t>(got);
        if (done != nullptr)
            *done = total;
    }
    smb2_close(connection.get(), file);
    const bool closed = ::fsync(fd) == 0 && ::close(fd) == 0;
    if (why.empty() && !closed)
        why = "could not finish the file";
    if (why.empty() && ::rename(part.c_str(), local.c_str()) != 0)
        why = "cannot rename the download (errno " + std::to_string(errno) + ")";
    if (!why.empty())
    {
        ::unlink(part.c_str());
        *error = why;
        return false;
    }
    return true;
}

} // namespace hui::sys
