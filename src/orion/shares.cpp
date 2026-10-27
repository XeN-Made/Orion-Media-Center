// ORION Media Center - Network drives: SMB shares whose games ORION lists and copies on first play.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/shares.hpp"

#include "core/save_file.hpp"
#include "library/json.hpp"
#include "orion/art_fetcher.hpp"
#include "orion/steamgriddb.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <utility>

namespace hui::orion
{

namespace
{

constexpr std::size_t kMaxShares = 16;
constexpr std::size_t kMaxFiles = 60000; // files a scan keeps, all shares together
constexpr std::size_t kMaxField = 200;   // bytes of a server, share, folder or user

std::string file_of(const std::string &state_dir)
{
    return state_dir + "/shares.json";
}

// Slashes either way, none doubled, none at the ends.
std::string clean_folder(std::string_view text)
{
    std::string out;
    for (char c : text)
    {
        if (c == '\\')
            c = '/';
        if (c == '/' && (out.empty() || out.back() == '/'))
            continue;
        out += c;
    }
    while (!out.empty() && out.back() == '/')
        out.pop_back();
    return out;
}

bool safe(std::string_view text)
{
    return text.size() <= kMaxField && text.find_first_of("\r\n\t") == std::string_view::npos &&
           text.find("..") == std::string_view::npos;
}

std::string last_part(std::string_view path)
{
    const std::size_t slash = path.rfind('/');
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

} // namespace

bool parse_share_address(std::string_view text, Share *share)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
        text.remove_suffix(1);
    std::string lowered(text.substr(0, std::min<std::size_t>(text.size(), 6)));
    for (char &c : lowered)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lowered.rfind("smb://", 0) == 0)
        text.remove_prefix(6);
    const std::string path = clean_folder(text);
    const std::size_t first = path.find('/');
    if (first == std::string::npos || first == 0)
        return false;
    const std::size_t second = path.find('/', first + 1);
    Share parsed = *share;
    parsed.server = path.substr(0, first);
    parsed.share = path.substr(first + 1, second == std::string::npos ? std::string::npos
                                                                      : second - first - 1);
    parsed.folder = second == std::string::npos ? std::string() : path.substr(second + 1);
    if (parsed.server.empty() || parsed.share.empty() || !safe(parsed.server) ||
        !safe(parsed.share) || !safe(parsed.folder))
        return false;
    *share = std::move(parsed);
    return true;
}

std::string share_address(const Share &share)
{
    std::string out = "\\\\" + share.server + "\\" + share.share;
    for (const char c : share.folder.empty() ? std::string() : "/" + share.folder)
        out += c == '/' ? '\\' : c;
    return out;
}

std::string share_password_key(const std::string &id)
{
    return "smb_password_" + id;
}

std::vector<Share> load_shares(const std::string &state_dir)
{
    std::vector<Share> shares;
    std::string text;
    json::Value root;
    if (state_dir.empty() || !save::read_file(file_of(state_dir), &text, 256u << 10) ||
        !json::parse(text, &root))
        return shares;
    for (const json::Value &value : root["shares"].elements())
    {
        Share share;
        share.id = value["id"].string_or("");
        share.server = value["server"].string_or("");
        share.share = value["share"].string_or("");
        share.folder = clean_folder(value["folder"].string_or(""));
        share.user = value["user"].string_or("");
        share.domain = value["domain"].string_or("");
        const bool known = std::any_of(shares.begin(), shares.end(),
                                       [&](const Share &s) { return s.id == share.id; });
        if (share.id.empty() || share.id.size() > 8 || known || share.server.empty() ||
            share.share.empty() || !safe(share.server) || !safe(share.share) ||
            !safe(share.folder) || !safe(share.user) || !safe(share.domain) ||
            shares.size() >= kMaxShares)
            continue;
        shares.push_back(std::move(share));
    }
    return shares;
}

bool save_shares(const std::string &state_dir, const std::vector<Share> &shares)
{
    if (state_dir.empty() || !save::ensure_directory(state_dir))
        return false;
    std::string text = "{\n  \"shares\": [";
    for (std::size_t i = 0; i < shares.size(); ++i)
    {
        const Share &share = shares[i];
        text += std::string(i == 0 ? "\n" : ",\n") + "    {\"id\": " + json::quote(share.id) +
                ", \"server\": " + json::quote(share.server) +
                ", \"share\": " + json::quote(share.share) +
                ", \"folder\": " + json::quote(share.folder) +
                ", \"user\": " + json::quote(share.user) +
                ", \"domain\": " + json::quote(share.domain) + "}";
    }
    text += shares.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return save::write_atomic(file_of(state_dir), text).empty();
}

const Share &add_share(std::vector<Share> &shares, Share share)
{
    int number = 1;
    std::string id;
    do
        id = "s" + std::to_string(number++);
    while (std::any_of(shares.begin(), shares.end(), [&](const Share &s) { return s.id == id; }));
    share.id = id;
    shares.push_back(std::move(share));
    return shares.back();
}

sys::SmbLogin share_login(const Share &share, const std::string &state_dir)
{
    sys::SmbLogin login;
    login.server = share.server;
    login.share = share.share;
    login.user = share.user;
    login.domain = share.domain;
    login.password = read_service_key(state_dir, share_password_key(share.id));
    return login;
}

std::string share_files_text(const std::vector<ShareFile> &files)
{
    std::string text = "# ORION's list of the games on network drives: share, size, path\n";
    for (const ShareFile &file : files)
        text += file.share_id + "\t" + std::to_string(file.size) + "\t" + file.path + "\n";
    return text;
}

std::vector<ShareFile> read_share_files(std::string_view text)
{
    std::vector<ShareFile> files;
    std::size_t start = 0;
    while (start < text.size() && files.size() < kMaxFiles)
    {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        const std::string_view line = text.substr(start, end - start);
        start = end + 1;
        if (line.empty() || line.front() == '#')
            continue;
        const std::size_t tab = line.find('\t');
        const std::size_t tab2 =
            tab == std::string_view::npos ? std::string_view::npos : line.find('\t', tab + 1);
        if (tab2 == std::string_view::npos)
            continue;
        ShareFile file;
        file.share_id = std::string(line.substr(0, tab));
        file.size =
            std::strtoull(std::string(line.substr(tab + 1, tab2 - tab - 1)).c_str(), nullptr, 10);
        file.path = std::string(line.substr(tab2 + 1));
        if (!file.share_id.empty() && !file.path.empty() && safe(file.path))
            files.push_back(std::move(file));
    }
    return files;
}

std::vector<library::RemoteGame> share_remote_games(const std::vector<ShareFile> &files,
                                                    const std::vector<Share> &shares,
                                                    const std::string &state_dir)
{
    std::vector<library::RemoteGame> out;
    for (const ShareFile &file : files)
    {
        const auto share = std::find_if(shares.begin(), shares.end(),
                                        [&](const Share &s) { return s.id == file.share_id; });
        if (share == shares.end())
            continue;
        // Its folder names its system: the one it is in, or, for a file at
        // the top of the share's folder, that folder.
        const std::size_t slash = file.path.rfind('/');
        std::string folder =
            slash == std::string::npos ? std::string() : file.path.substr(0, slash);
        const std::string system_folder =
            last_part(folder.empty() ? std::string_view(share->folder) : std::string_view(folder));
        if (system_folder.empty())
            continue;
        const std::string name = last_part(file.path);
        library::RemoteGame game;
        game.system_folder = system_folder;
        game.name = library::rom_display_name(name);
        game.path = state_dir + "/shares/" + share->id + "/" + system_folder + "/" + name;
        game.remote = std::string(kShareScheme) + share->id + "/" +
                      (share->folder.empty() ? file.path : share->folder + "/" + file.path);
        game.filtered = true;
        out.push_back(std::move(game));
    }
    return out;
}

bool download_share_file(const std::string &state_dir, const std::string &url,
                         const std::string &path, std::string *error)
{
    if (url.rfind(kShareScheme, 0) != 0)
    {
        *error = "not a network drive's file";
        return false;
    }
    const std::string rest = url.substr(kShareScheme.size());
    const std::size_t slash = rest.find('/');
    const std::string id = rest.substr(0, slash);
    const std::vector<Share> shares = load_shares(state_dir);
    const auto share =
        std::find_if(shares.begin(), shares.end(), [&](const Share &s) { return s.id == id; });
    if (slash == std::string::npos || share == shares.end())
    {
        *error = "that network drive is no longer set up";
        return false;
    }
    const std::size_t folder = path.rfind('/');
    if (folder == std::string::npos || !make_directories(path.substr(0, folder)))
    {
        *error = "cannot make the folder for " + path;
        return false;
    }
    return sys::smb_download(share_login(*share, state_dir), rest.substr(slash + 1), path, nullptr,
                             nullptr, error);
}

ShareScan::ShareScan(SmbLister list) : list_(std::move(list))
{
}

ShareScan::~ShareScan()
{
    wait();
}

void ShareScan::wait()
{
    if (worker_.joinable())
        worker_.join();
}

bool ShareScan::start(std::vector<Share> shares, std::string state_dir)
{
    if (running_.load())
        return false;
    wait();
    running_ = true;
    worker_ = std::thread(
        [this, shares = std::move(shares), state_dir = std::move(state_dir)]()
        {
            Result result;
            std::vector<ShareFile> files;
            for (const Share &share : shares)
            {
                std::vector<sys::SmbEntry> entries;
                std::string error;
                if (!list_(share_login(share, state_dir), share.folder, 1, kMaxFiles, &entries,
                           &error))
                {
                    if (result.error.empty())
                        result.error = share_address(share) + ": " + error;
                    continue;
                }
                ++result.shares;
                for (const sys::SmbEntry &entry : entries)
                {
                    if (!entry.directory && files.size() < kMaxFiles)
                        files.push_back({share.id, entry.path, entry.size});
                }
            }
            result.files = static_cast<int>(files.size());
            // A share that could not be read keeps what an earlier scan found
            // of it: a NAS asleep does not empty the library.
            if (result.shares < static_cast<int>(shares.size()))
            {
                std::string text;
                save::read_file(state_dir + "/share-games.tsv", &text, 32u << 20);
                for (ShareFile &old : read_share_files(text))
                {
                    const bool listed =
                        std::any_of(files.begin(), files.end(),
                                    [&](const ShareFile &f) { return f.share_id == old.share_id; });
                    const bool still = std::any_of(shares.begin(), shares.end(), [&](const Share &s)
                                                   { return s.id == old.share_id; });
                    if (!listed && still && files.size() < kMaxFiles)
                        files.push_back(std::move(old));
                }
            }
            save::write_atomic(state_dir + "/share-games.tsv", share_files_text(files));
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                result_ = std::move(result);
                finished_ = true;
            }
            running_ = false;
        });
    return true;
}

bool ShareScan::take_finished(Result *result)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!finished_)
        return false;
    finished_ = false;
    *result = result_;
    return true;
}

} // namespace hui::orion
