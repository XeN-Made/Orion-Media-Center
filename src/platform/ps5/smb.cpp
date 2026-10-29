// ORION Media Center - SMB shares: listing and copying files off a Windows share or a NAS.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/ps5/smb.hpp"
#include "platform/ps5/smb_connection.hpp"

// smb2.h first: libsmb2.h uses its types.
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <smb2/libsmb2-raw.h>
#include <smb2/libsmb2-dcerpc.h>
#include <smb2/libsmb2-dcerpc-srvsvc.h>

#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
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

namespace
{

// A NetBIOS name query's 50 bytes: `name` in its 16-byte form (padded with
// `pad`, then `suffix`), for records of `type`.
void netbios_question(unsigned char *packet, std::uint16_t id, const std::string &name,
                      unsigned char pad, unsigned char suffix, unsigned char type, bool broadcast)
{
    std::memset(packet, 0, 50);
    packet[0] = static_cast<unsigned char>(id >> 8);
    packet[1] = static_cast<unsigned char>(id & 0xff);
    packet[2] = broadcast ? 0x01 : 0x00;
    packet[3] = broadcast ? 0x10 : 0x00;
    packet[5] = 1;
    packet[12] = 0x20;
    for (int i = 0; i < 16; ++i)
    {
        const unsigned char c =
            i == 15 ? suffix
            : i < static_cast<int>(name.size())
                ? static_cast<unsigned char>(
                      std::toupper(static_cast<unsigned char>(name[static_cast<std::size_t>(i)])))
                : pad;
        packet[13 + 2 * i] = static_cast<unsigned char>('A' + (c >> 4));
        packet[14 + 2 * i] = static_cast<unsigned char>('A' + (c & 0x0f));
    }
    packet[47] = type;
    packet[49] = 0x01;
}

// A Windows PC by its name ("MEDIA-PC"). The console's resolver knows only
// DNS, which has no such name on a home network: it is asked the way
// Windows asks, a NetBIOS name query broadcast on the local network (RFC
// 1002), for the file server's name, which a PC sharing folders answers.
bool netbios_address(const std::string &name, std::string *address)
{
    if (name.empty() || name.size() > 15)
        return false;
    unsigned char packet[50];
    netbios_question(packet, static_cast<std::uint16_t>(::getpid() ^ 0x4f52), name, ' ', 0x20, 0x20,
                     true);
    const int s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
        return false;
    int on = 1;
    ::setsockopt(s, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(137);
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    bool found = false;
    for (int attempt = 0; attempt < 3 && !found; ++attempt)
    {
        ::sendto(s, packet, sizeof(packet), 0, reinterpret_cast<const sockaddr *>(&to), sizeof(to));
        pollfd wait = {s, POLLIN, 0};
        while (!found && ::poll(&wait, 1, 700) > 0)
        {
            unsigned char answer[576];
            const ssize_t got = ::recv(s, answer, sizeof(answer), 0);
            // Ours, an answer, no error; then the name again (34 bytes),
            // type, class, time to live, length, flags: the address at 58.
            if (got < 62 || answer[0] != packet[0] || answer[1] != packet[1] ||
                (answer[2] & 0x80) == 0 || (answer[3] & 0x0f) != 0)
                continue;
            char text[INET_ADDRSTRLEN] = {};
            if (::inet_ntop(AF_INET, answer + 58, text, sizeof(text)) != nullptr)
            {
                *address = text;
                found = true;
            }
        }
    }
    ::close(s);
    return found;
}

// The server as libsmb2 can reach it: an address as it is, a name DNS
// knows as it is, else a Windows PC's name made its address.
bool reachable(const std::string &server, std::string *host, std::string *error)
{
    *host = server;
    in_addr ip = {};
    if (::inet_pton(AF_INET, server.c_str(), &ip) == 1 || server.find(':') != std::string::npos)
        return true;
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    addrinfo *found = nullptr;
    if (::getaddrinfo(server.c_str(), nullptr, &hints, &found) == 0)
    {
        ::freeaddrinfo(found);
        return true;
    }
    if (netbios_address(server, host))
        return true;
    *error = "could not find " + server +
             " on the network: use its IP address instead, like "
             "192.168.1.20 (on Windows: Settings, Network, your connection, IPv4 address)";
    return false;
}

} // namespace

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
    std::string host;
    if (!reachable(login.server, &host, error))
        return false;
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
    if (smb2_connect_share(smb2_, host.c_str(), login.share.c_str(), user.c_str()) < 0)
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

namespace
{

using Clock = std::chrono::steady_clock;

int ms_left(Clock::time_point until)
{
    const auto left =
        std::chrono::duration_cast<std::chrono::milliseconds>(until - Clock::now()).count();
    return left > 0 ? static_cast<int>(left) : 0;
}

// A socket that never waits: fcntl first, FIONBIO if that did not take.
bool never_waits(int s)
{
    ::fcntl(s, F_SETFL, ::fcntl(s, F_GETFL, 0) | O_NONBLOCK);
    if ((::fcntl(s, F_GETFL, 0) & O_NONBLOCK) != 0)
        return true;
    int on = 1;
    return ::ioctl(s, FIONBIO, &on) == 0;
}

std::string text_of(std::uint32_t address)
{
    in_addr ip = {};
    ip.s_addr = htonl(address);
    char text[INET_ADDRSTRLEN] = {};
    ::inet_ntop(AF_INET, &ip, text, sizeof(text));
    return text;
}

void add_server(std::vector<SmbServer> *servers, const std::string &address)
{
    for (const SmbServer &server : *servers)
    {
        if (server.address == address)
            return;
    }
    servers->push_back({address, ""});
}

} // namespace

std::vector<SmbServer> smb_find_servers(const std::string &own, std::string *trace)
{
    std::vector<SmbServer> servers;
    trace->clear();
    in_addr self = {};
    if (::inet_pton(AF_INET, own.c_str(), &self) != 1)
    {
        *trace = "no address of its own";
        return servers;
    }
    const auto started = Clock::now();
    // The whole search ends by then, whatever the network does.
    const auto deadline = started + std::chrono::seconds(8);
    const std::uint32_t me = ntohl(self.s_addr);
    const std::uint32_t base = me & 0xffffff00u;

    // Windows PCs: a broadcast NetBIOS query for the workgroup most are in,
    // answered by each of them with its address.
    int workgroup = 0;
    const int u = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (u >= 0 && never_waits(u))
    {
        int on = 1;
        ::setsockopt(u, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
        unsigned char packet[50];
        netbios_question(packet, 0x5157, "WORKGROUP", ' ', 0x00, 0x20, true);
        sockaddr_in to = {};
        to.sin_family = AF_INET;
        to.sin_port = htons(137);
        to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        ::sendto(u, packet, sizeof(packet), 0, reinterpret_cast<const sockaddr *>(&to), sizeof(to));
        const auto until = std::min(deadline, Clock::now() + std::chrono::milliseconds(900));
        for (int reads = 0; reads < 64; ++reads)
        {
            pollfd wait = {u, POLLIN, 0};
            if (::poll(&wait, 1, ms_left(until)) <= 0)
                break;
            unsigned char answer[576];
            const ssize_t got = ::recv(u, answer, sizeof(answer), 0);
            if (got < 0)
                break;
            // Ours and an answer: the name again, type, class, time to live,
            // length at 54, then 6 bytes an address (2 of flags, 4 of address).
            if (got < 62 || answer[0] != 0x51 || answer[1] != 0x57 || (answer[2] & 0x80) == 0)
                continue;
            const int length = (answer[54] << 8) | answer[55];
            for (int at = 56; at + 6 <= 56 + length && at + 6 <= got; at += 6)
            {
                const std::uint32_t found = (static_cast<std::uint32_t>(answer[at + 2]) << 24) |
                                            (static_cast<std::uint32_t>(answer[at + 3]) << 16) |
                                            (static_cast<std::uint32_t>(answer[at + 4]) << 8) |
                                            answer[at + 5];
                if (found != me && found != 0)
                {
                    add_server(&servers, text_of(found));
                    ++workgroup;
                }
            }
        }
    }

    // Anything taking SMB on the 256 addresses around: port 445 knocked on
    // a batch at a time; an open one completes its connection.
    int knocked = 0;
    int open = 0;
    constexpr int kBatch = 64;
    for (int first = 1; first < 255 && ms_left(deadline) > 0; first += kBatch)
    {
        std::vector<std::pair<int, std::uint32_t>> trying;
        for (int host = first; host < std::min(255, first + kBatch); ++host)
        {
            const std::uint32_t address = base | static_cast<std::uint32_t>(host);
            if (address == me)
                continue;
            const int s = ::socket(AF_INET, SOCK_STREAM, 0);
            if (s < 0)
                continue;
            if (!never_waits(s))
            {
                ::close(s);
                continue;
            }
            sockaddr_in to = {};
            to.sin_family = AF_INET;
            to.sin_port = htons(445);
            to.sin_addr.s_addr = htonl(address);
            ++knocked;
            if (::connect(s, reinterpret_cast<const sockaddr *>(&to), sizeof(to)) == 0 ||
                errno == EINPROGRESS)
                trying.emplace_back(s, address);
            else
                ::close(s);
        }
        const auto until = std::min(deadline, Clock::now() + std::chrono::milliseconds(700));
        while (!trying.empty() && ms_left(until) > 0)
        {
            std::vector<pollfd> waits;
            for (const auto &[s, address] : trying)
                waits.push_back({s, POLLOUT, 0});
            if (::poll(waits.data(), static_cast<nfds_t>(waits.size()), ms_left(until)) <= 0)
                break;
            for (std::size_t i = waits.size(); i-- > 0;)
            {
                if (waits[i].revents == 0)
                    continue;
                int problem = 0;
                socklen_t size = sizeof(problem);
                ::getsockopt(trying[i].first, SOL_SOCKET, SO_ERROR, &problem, &size);
                if (problem == 0 && (waits[i].revents & POLLOUT) != 0)
                {
                    add_server(&servers, text_of(trying[i].second));
                    ++open;
                }
                ::close(trying[i].first);
                trying.erase(trying.begin() + static_cast<std::ptrdiff_t>(i));
            }
        }
        for (const auto &[s, address] : trying)
            ::close(s);
    }

    // Each one's name: a NetBIOS node status query ("*"), answered with the
    // names it holds; the first unique workstation name is the machine's.
    int named = 0;
    if (u >= 0)
    {
        for (std::size_t i = 0; i < servers.size() && i < 256; ++i)
        {
            unsigned char packet[50];
            netbios_question(packet, static_cast<std::uint16_t>(0x5200 + i), "*", 0x00, 0x00, 0x21,
                             false);
            sockaddr_in to = {};
            to.sin_family = AF_INET;
            to.sin_port = htons(137);
            ::inet_pton(AF_INET, servers[i].address.c_str(), &to.sin_addr);
            ::sendto(u, packet, sizeof(packet), 0, reinterpret_cast<const sockaddr *>(&to),
                     sizeof(to));
        }
        const auto until = std::min(deadline + std::chrono::seconds(1),
                                    Clock::now() + std::chrono::milliseconds(900));
        for (int reads = 0; reads < 256 && !servers.empty(); ++reads)
        {
            pollfd wait = {u, POLLIN, 0};
            if (::poll(&wait, 1, ms_left(until)) <= 0)
                break;
            unsigned char answer[1024];
            const ssize_t got = ::recv(u, answer, sizeof(answer), 0);
            if (got < 0)
                break;
            // The header, the name (34 bytes), type, class, time to live and
            // length: the count of names at 56, then 18 bytes a name.
            const std::size_t which = static_cast<std::size_t>(answer[1]);
            if (got < 57 || (answer[2] & 0x80) == 0 || answer[0] != 0x52 || which >= servers.size())
                continue;
            const int count = answer[56];
            for (int n = 0; n < count && 57 + 18 * (n + 1) <= got; ++n)
            {
                const unsigned char *entry = answer + 57 + 18 * n;
                if (entry[15] != 0x00 || (entry[16] & 0x80) != 0)
                    continue;
                std::string name(reinterpret_cast<const char *>(entry), 15);
                while (!name.empty() && (name.back() == ' ' || name.back() == '\0'))
                    name.pop_back();
                if (servers[which].name.empty())
                    ++named;
                servers[which].name = name;
                break;
            }
        }
    }
    if (u >= 0)
        ::close(u);
    const auto took =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
    *trace = "around " + own + ": workgroup answers " + std::to_string(workgroup) +
             ", port 445 knocked on " + std::to_string(knocked) + " addresses, open on " +
             std::to_string(open) + ", named " + std::to_string(named) + ", " +
             std::to_string(took) + " ms";
    return servers;
}

bool smb_shares(const SmbLogin &login, std::vector<std::string> *shares, std::string *error)
{
    shares->clear();
    // The list is asked of the server's IPC$ share.
    SmbLogin ipc = login;
    ipc.share = "IPC$";
    SmbConnection connection;
    if (!connection.open(ipc, error))
        return false;
    struct Answer
    {
        bool done = false;
        int status = 0;
        srvsvc_NetrShareEnum_rep *rep = nullptr;
    } answer;
    const smb2_command_cb done = [](smb2_context *, int status, void *data, void *mine)
    {
        Answer *answer = static_cast<Answer *>(mine);
        answer->done = true;
        answer->status = status;
        answer->rep = static_cast<srvsvc_NetrShareEnum_rep *>(data);
    };
    if (smb2_share_enum_async(connection.get(), SHARE_INFO_1, done, &answer) < 0)
    {
        *error = connection.said();
        return false;
    }
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!answer.done && std::chrono::steady_clock::now() < until)
    {
        pollfd wait = {smb2_get_fd(connection.get()),
                       static_cast<short>(smb2_which_events(connection.get())), 0};
        if (::poll(&wait, 1, 1000) < 0 || smb2_service(connection.get(), wait.revents) < 0)
            break;
    }
    if (!answer.done || answer.status != 0 || answer.rep == nullptr)
    {
        *error = answer.done ? connection.said() : std::string("no answer");
        if (answer.rep != nullptr)
            smb2_free_data(connection.get(), answer.rep);
        return false;
    }
    const srvsvc_SHARE_INFO_1_CONTAINER &list = answer.rep->ses.ShareInfo.Level1;
    for (std::uint32_t i = 0; list.Buffer != nullptr && i < list.EntriesRead; ++i)
    {
        const srvsvc_SHARE_INFO_1 &info = list.Buffer->share_info_1[i];
        if ((info.type & 0x0fffffffu) != SHARE_TYPE_DISKTREE || info.netname.utf16 == nullptr)
            continue;
        const char *name = smb2_utf16_to_utf8(info.netname.utf16->val,
                                              static_cast<std::size_t>(info.netname.utf16->len));
        if (name == nullptr)
            continue;
        std::string text = name;
        std::free(const_cast<char *>(name));
        if (!text.empty() && text.back() != '$')
            shares->push_back(text);
    }
    smb2_free_data(connection.get(), answer.rep);
    std::sort(shares->begin(), shares->end());
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
