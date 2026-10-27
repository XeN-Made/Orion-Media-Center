// ORION Media Center - RomM: the games on a RomM server, listed and downloaded.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/romm.hpp"

#include "core/local_host.hpp"
#include "core/save_file.hpp"
#include "library/art_match.hpp"
#include "library/json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include <sys/stat.h>

namespace hui::orion
{

namespace
{

// A page of games comes with all their metadata (summaries, screenshots,
// every source's details): 50 of them stay well under the limit, which a
// page of 200 from a large library passed ("too large").
constexpr std::size_t kMaxAnswer = 24u << 20;
constexpr int kPage = 50;

std::string trimmed(std::string_view text)
{
    std::size_t start = 0;
    std::size_t end = text.size();
    while (start < end && std::isspace(static_cast<unsigned char>(text[start])) != 0)
        ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
        --end;
    return std::string(text.substr(start, end - start));
}

// A field of the catalogue: no tabs or new lines inside.
std::string field(std::string text)
{
    for (char &c : text)
    {
        if (c == '\t' || c == '\n' || c == '\r')
            c = ' ';
    }
    return text;
}

long long as_id(const json::Value &value)
{
    const double number = value.number_or(0.0);
    return number > 0.0 ? static_cast<long long>(number) : 0;
}

// A folder and the ones above it, as needed.
bool make_folders(const std::string &path)
{
    for (std::size_t at = path.find('/', 1); at != std::string::npos; at = path.find('/', at + 1))
        save::ensure_directory(path.substr(0, at));
    return save::ensure_directory(path);
}

bool is_file(const std::string &path)
{
    struct stat info = {};
    return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0;
}

std::string picture_extension(std::string_view url)
{
    const std::size_t query = url.find('?');
    const std::string_view path = url.substr(0, query);
    const std::size_t dot = path.rfind('.');
    std::string extension = dot == std::string_view::npos ? "" : std::string(path.substr(dot));
    for (char &c : extension)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return extension == ".jpg" || extension == ".jpeg" ? ".jpg" : ".png";
}

} // namespace

std::string romm_base(std::string_view typed)
{
    std::string text = trimmed(typed);
    while (!text.empty() && text.back() == '/')
        text.pop_back();
    if (text.empty() || text.find_first_of(" \t\"'<>\\") != std::string::npos)
        return {};
    if (text.rfind("http://", 0) != 0 && text.rfind("https://", 0) != 0)
    {
        const std::string host = net::url_host("http://" + text);
        text = (net::is_local_host(host) ? "http://" : "https://") + text;
    }
    // The address alone: an /api the player typed too goes.
    if (text.size() > 4 && text.compare(text.size() - 4, 4, "/api") == 0)
        text.resize(text.size() - 4);
    return net::url_host(text).empty() ? std::string() : text;
}

std::string romm_platforms_url(const std::string &base)
{
    return base + "/api/platforms";
}

std::string romm_games_url(const std::string &base, long long platform_id, int offset, int limit)
{
    // platform_ids for RomM 4.4 and later, platform_id for the ones before.
    const std::string id = std::to_string(platform_id);
    return base + "/api/roms?platform_ids=" + id + "&platform_id=" + id +
           "&limit=" + std::to_string(limit) + "&offset=" + std::to_string(offset) +
           "&order_by=name&order_dir=asc&with_char_index=false&with_filter_values=false"
           "&with_rom_id_index=false";
}

std::string romm_content_url(const std::string &base, const RommGame &game)
{
    return base + "/api/roms/" + std::to_string(game.id) + "/content/" +
           library::url_encode(game.multi ? game.fs_name + ".zip" : game.fs_name);
}

// An address as RomM writes it, made one curl takes: its cover paths end
// "?ts=2026-10-05 03:40:07", and a space (or a name's own characters) is no
// part of an address until it is escaped. What is escaped already stays.
std::string escaped_url(std::string_view url)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const char c : url)
    {
        const auto byte = static_cast<unsigned char>(c);
        if (byte <= 0x20 || byte >= 0x7f ||
            std::string_view("\"<>\\^`{|}").find(c) != std::string_view::npos)
        {
            out += '%';
            out += kHex[byte >> 4];
            out += kHex[byte & 15];
        }
        else
            out += c;
    }
    return out;
}

std::string romm_cover_url(const std::string &base, const RommGame &game)
{
    if (game.cover.empty())
        return {};
    if (game.cover.rfind("http://", 0) == 0 || game.cover.rfind("https://", 0) == 0)
        return escaped_url(game.cover);
    return base + (game.cover.front() == '/' ? "" : "/") + escaped_url(game.cover);
}

std::string romm_saves_url(const std::string &base)
{
    return base + "/api/saves";
}

std::string romm_states_url(const std::string &base)
{
    return base + "/api/states";
}

bool parse_romm_saves(std::string_view text, bool state, std::vector<RommSave> *saves)
{
    json::Value root;
    if (!json::parse(text, &root))
        return false;
    const json::Value &list = root.is_object() ? root["items"] : root;
    for (const json::Value &item : list.elements())
    {
        RommSave save;
        save.id = as_id(item["id"]);
        save.rom_id = as_id(item["rom_id"]);
        save.state = state;
        save.emulator = item["emulator"].string_or("");
        save.slot = item["slot"].string_or("");
        save.file_name = item["file_name"].string_or("");
        const double size = item["file_size_bytes"].number_or(0.0);
        save.size = size > 0.0 ? static_cast<std::uint64_t>(size) : 0;
        save.updated = item["updated_at"].string_or("");
        if (save.id > 0 && save.rom_id > 0 && !save.file_name.empty())
            saves->push_back(std::move(save));
    }
    return true;
}

std::string romm_saves_text(const std::vector<RommSave> &saves)
{
    std::string text = "# ORION's copy of the player's saves on RomM: id, game, state, size, "
                       "updated, emulator, slot, file\n";
    for (const RommSave &save : saves)
        text += std::to_string(save.id) + "\t" + std::to_string(save.rom_id) + "\t" +
                (save.state ? "1" : "0") + "\t" + std::to_string(save.size) + "\t" +
                field(save.updated) + "\t" + field(save.emulator) + "\t" + field(save.slot) + "\t" +
                field(save.file_name) + "\n";
    return text;
}

std::vector<RommSave> read_romm_saves(std::string_view text)
{
    std::vector<RommSave> saves;
    std::size_t start = 0;
    while (start < text.size())
    {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        const std::string_view line = text.substr(start, end - start);
        start = end + 1;
        if (line.empty() || line.front() == '#')
            continue;
        std::vector<std::string> parts;
        std::size_t at = 0;
        while (parts.size() < 7)
        {
            const std::size_t tab = line.find('\t', at);
            if (tab == std::string_view::npos)
                break;
            parts.emplace_back(line.substr(at, tab - at));
            at = tab + 1;
        }
        if (parts.size() != 7)
            continue;
        parts.emplace_back(line.substr(at));
        RommSave save;
        save.id = std::atoll(parts[0].c_str());
        save.rom_id = std::atoll(parts[1].c_str());
        save.state = parts[2] == "1";
        save.size = std::strtoull(parts[3].c_str(), nullptr, 10);
        save.updated = parts[4];
        save.emulator = parts[5];
        save.slot = parts[6];
        save.file_name = parts[7];
        if (save.id > 0 && save.rom_id > 0 && !save.file_name.empty())
            saves.push_back(std::move(save));
    }
    return saves;
}

std::vector<std::string> romm_headers(const std::string &token)
{
    if (token.empty())
        return {};
    return {"Authorization: Bearer " + token};
}

bool parse_romm_platforms(std::string_view text, std::vector<RommPlatform> *platforms)
{
    json::Value root;
    if (!json::parse(text, &root))
        return false;
    // An array of them; some versions wrap it in "items".
    const json::Value &list = root.is_object() ? root["items"] : root;
    if (list.is_object())
        return false;
    for (const json::Value &item : list.elements())
    {
        RommPlatform platform;
        platform.id = as_id(item["id"]);
        platform.slug = item["slug"].string_or("");
        platform.fs_slug = item["fs_slug"].string_or(platform.slug);
        platform.name = item["display_name"].string_or(item["name"].string_or(platform.slug));
        platform.rom_count = static_cast<int>(item["rom_count"].number_or(0.0));
        if (platform.id > 0 && !platform.fs_slug.empty())
            platforms->push_back(std::move(platform));
    }
    return true;
}

bool parse_romm_games(std::string_view text, const std::vector<RommPlatform> &platforms,
                      std::vector<RommGame> *games, long long *total)
{
    json::Value root;
    *total = -1;
    if (!json::parse(text, &root))
        return false;
    const json::Value &list = root.is_object() ? root["items"] : root;
    if (root.is_object())
    {
        const double count = root["total"].number_or(-1.0);
        *total = count >= 0.0 ? static_cast<long long>(count) : -1;
    }
    for (const json::Value &item : list.elements())
    {
        RommGame game;
        game.id = as_id(item["id"]);
        game.platform_id = as_id(item["platform_id"]);
        game.fs_slug = item["platform_fs_slug"].string_or("");
        for (const RommPlatform &platform : platforms)
        {
            if (game.fs_slug.empty() && platform.id == game.platform_id)
                game.fs_slug = platform.fs_slug;
        }
        game.fs_name = item["fs_name"].string_or("");
        game.name = item["name"].string_or("");
        if (game.name.empty())
            game.name = item["fs_name_no_ext"].string_or(game.fs_name);
        const double size = item["fs_size_bytes"].number_or(0.0);
        game.size = size > 0.0 ? static_cast<std::uint64_t>(size) : 0;
        game.cover = item["path_cover_large"].string_or(item["path_cover_small"].string_or(""));
        game.multi = item["has_multiple_files"].bool_or(item["multi"].bool_or(false));
        if (game.id > 0 && !game.fs_name.empty() && !game.fs_slug.empty() &&
            game.fs_name.find('/') == std::string::npos && game.fs_name != "." &&
            game.fs_name != "..")
            games->push_back(std::move(game));
    }
    return true;
}

std::string parse_romm_pairing(std::string_view text)
{
    json::Value root;
    if (!json::parse(text, &root) || !root.is_object())
        return {};
    std::string token = root["raw_token"].string_or("");
    return token.find_first_of(" \t\r\n") == std::string::npos ? token : std::string();
}

std::string romm_catalogue_text(const std::vector<RommGame> &games)
{
    std::string text = "# ORION's copy of the RomM server's games: id, system id, system folder, "
                       "size, several files, name, file, cover\n";
    for (const RommGame &game : games)
        text += std::to_string(game.id) + "\t" + std::to_string(game.platform_id) + "\t" +
                field(game.fs_slug) + "\t" + std::to_string(game.size) + "\t" +
                (game.multi ? "1" : "0") + "\t" + field(game.name) + "\t" + field(game.fs_name) +
                "\t" + field(game.cover) + "\n";
    return text;
}

std::vector<RommGame> read_romm_catalogue(std::string_view text)
{
    std::vector<RommGame> games;
    std::size_t start = 0;
    while (start < text.size())
    {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        const std::string_view line = text.substr(start, end - start);
        start = end + 1;
        if (line.empty() || line.front() == '#')
            continue;
        std::vector<std::string> parts;
        std::size_t at = 0;
        while (parts.size() < 7)
        {
            const std::size_t tab = line.find('\t', at);
            if (tab == std::string_view::npos)
                break;
            parts.emplace_back(line.substr(at, tab - at));
            at = tab + 1;
        }
        if (parts.size() != 7)
            continue;
        parts.emplace_back(line.substr(at));
        RommGame game;
        game.id = std::atoll(parts[0].c_str());
        game.platform_id = std::atoll(parts[1].c_str());
        game.fs_slug = parts[2];
        game.size = std::strtoull(parts[3].c_str(), nullptr, 10);
        game.multi = parts[4] == "1";
        game.name = parts[5];
        game.fs_name = parts[6];
        game.cover = parts[7];
        if (game.id > 0 && !game.fs_slug.empty() && !game.fs_name.empty() &&
            game.fs_name.find('/') == std::string::npos &&
            game.fs_slug.find('/') == std::string::npos && game.fs_slug != "." &&
            game.fs_slug != "..")
            games.push_back(std::move(game));
    }
    return games;
}

std::string romm_pair(const HttpPost &post, const std::string &base, const std::string &code,
                      std::string *token)
{
    const std::string cleaned = trimmed(code);
    if (base.empty())
        return "no RomM address";
    if (cleaned.empty() || cleaned.find_first_of("\"\\\r\n") != std::string::npos)
        return "no pairing code";
    std::string answer;
    std::string error;
    if (!post(base + "/api/client-tokens/exchange", {"Content-Type: application/json"},
              "{\"code\": \"" + cleaned + "\"}", 64u << 10, &answer, &error))
        return error.empty() ? "the server did not answer" : error;
    *token = parse_romm_pairing(answer);
    return token->empty() ? "the server did not give a token" : std::string();
}

std::string romm_game_path(const std::string &folder, const RommGame &game)
{
    return folder + "/" + game.fs_slug + "/" + game.fs_name + (game.multi ? ".zip" : "");
}

std::string romm_folder(const std::string &state_dir)
{
    return state_dir + "/romm";
}

std::vector<library::RemoteGame> romm_remote_games(const std::vector<RommGame> &games,
                                                   const std::string &base,
                                                   const std::string &state_dir)
{
    std::vector<library::RemoteGame> out;
    const std::string folder = romm_folder(state_dir);
    for (const RommGame &game : games)
    {
        library::RemoteGame remote;
        remote.system_folder = game.fs_slug;
        remote.name = game.name;
        remote.path = romm_game_path(folder + "/games", game);
        remote.remote = romm_content_url(base, game);
        const std::string url = romm_cover_url(base, game);
        remote.cover = url;
        // One an older ORION downloaded: used when no emulator has art.
        if (!url.empty())
        {
            const std::string cover =
                folder + "/covers/" + std::to_string(game.id) + picture_extension(url);
            if (is_file(cover))
                remote.icon = cover;
        }
        out.push_back(std::move(remote));
    }
    return out;
}

FileDownload::FileDownload(HttpDownloadWith download) : download_(std::move(download))
{
}

FileDownload::~FileDownload()
{
    wait();
}

void FileDownload::wait()
{
    if (worker_.joinable())
        worker_.join();
}

std::string FileDownload::error() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

std::uint64_t FileDownload::written() const
{
    std::string part;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        part = path_ + ".part";
    }
    struct stat info = {};
    return ::stat(part.c_str(), &info) == 0 ? static_cast<std::uint64_t>(info.st_size) : 0;
}

bool FileDownload::start(std::string url, std::vector<std::string> headers, std::string path)
{
    if (running_.load())
        return false;
    const std::size_t slash = path.rfind('/');
    if (slash == std::string::npos || !make_folders(path.substr(0, slash)))
        return false;
    wait();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        error_.clear();
        path_ = path;
    }
    running_ = true;
    worker_ = std::thread(
        [this, url = std::move(url), headers = std::move(headers), path = std::move(path)]()
        {
            // A game can be several gigabytes (a PS2 disc): the limit is only
            // there so a server answering forever cannot fill the drive.
            constexpr std::size_t kMaxGame = static_cast<std::size_t>(32) << 30;
            std::string error;
            if (!download_(url, headers, path, kMaxGame, &error))
            {
                std::lock_guard<std::mutex> lock(mutex_);
                error_ = error.empty() ? std::string("the download failed") : error;
            }
            running_ = false;
            finished_ = true;
        });
    return true;
}

RommSync::RommSync(HttpGetWith get) : get_(std::move(get))
{
}

RommSync::~RommSync()
{
    cancel_ = true;
    wait();
}

void RommSync::wait()
{
    if (worker_.joinable())
        worker_.join();
}

RommSync::Progress RommSync::progress() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return progress_;
}

bool RommSync::start(std::string base, std::string token, std::string state_dir, bool saves_only)
{
    if (running_.load() || base.empty() || token.empty() || state_dir.empty())
        return false;
    wait();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        progress_ = Progress();
        progress_.saves_only = saves_only;
    }
    cancel_ = false;
    running_ = true;
    worker_ = std::thread([this, base = std::move(base), token = std::move(token),
                           state_dir = std::move(state_dir), saves_only]()
                          { run(base, token, state_dir, saves_only); });
    return true;
}

void RommSync::run(std::string base, std::string token, std::string state_dir, bool saves_only)
{
    const std::vector<std::string> headers = romm_headers(token);
    const auto fail = [&](const std::string &error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (progress_.error.empty())
            progress_.error = error;
    };
    // The player's saves and save states: a short list, read whole each time.
    const auto read_saves = [&]()
    {
        std::string body;
        std::string error;
        std::vector<RommSave> saves;
        const bool read = get_(romm_saves_url(base), headers, kMaxAnswer, &body, &error) &&
                          parse_romm_saves(body, false, &saves) &&
                          get_(romm_states_url(base), headers, kMaxAnswer, &body, &error) &&
                          parse_romm_saves(body, true, &saves);
        if (!read)
            return fail("saves: " + (error.empty() ? std::string("not a list") : error));
        save::write_atomic(state_dir + "/romm-saves.tsv", romm_saves_text(saves));
        std::lock_guard<std::mutex> lock(mutex_);
        progress_.saves = static_cast<int>(saves.size());
    };
    const auto work = [&]()
    {
        std::string body;
        std::string error;
        std::vector<RommPlatform> platforms;
        if (!get_(romm_platforms_url(base), headers, kMaxAnswer, &body, &error))
            return fail(error);
        if (!parse_romm_platforms(body, &platforms))
            return fail("the server's answer is not a list of systems");
        {
            std::lock_guard<std::mutex> lock(mutex_);
            progress_.platforms = static_cast<int>(platforms.size());
            progress_.total = 0;
            for (const RommPlatform &platform : platforms)
                progress_.total += platform.rom_count;
        }
        std::vector<RommGame> games;
        // A system that cannot be read keeps what the last sync had of it;
        // the others go on.
        std::string old_text;
        save::read_file(state_dir + "/romm-games.tsv", &old_text, 32u << 20);
        const std::vector<RommGame> old = read_romm_catalogue(old_text);
        // A sync that stopped part way (ORION closed, the server went away)
        // goes on from the systems it had read: a large library is not read
        // again from the start. Its progress is the systems done and their
        // games, kept for two days and for this server only.
        const std::string done_path = state_dir + "/romm-sync-done.txt";
        const std::string part_path = state_dir + "/romm-sync-games.tsv";
        std::vector<long long> done;
        {
            std::string text;
            struct stat info = {};
            if (save::read_file(done_path, &text, 1u << 20) &&
                ::stat(done_path.c_str(), &info) == 0 &&
                std::time(nullptr) - info.st_mtime < 2 * 24 * 3600 &&
                text.rfind(base + "\n", 0) == 0)
            {
                for (std::size_t at = text.find('\n') + 1; at < text.size();)
                {
                    std::size_t end = text.find('\n', at);
                    if (end == std::string::npos)
                        end = text.size();
                    const long long id = std::atoll(text.substr(at, end - at).c_str());
                    if (id > 0)
                        done.push_back(id);
                    at = end + 1;
                }
                std::string part;
                save::read_file(part_path, &part, 32u << 20);
                for (RommGame &game : read_romm_catalogue(part))
                {
                    if (std::find(done.begin(), done.end(), game.platform_id) != done.end())
                        games.push_back(std::move(game));
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            progress_.resumed = static_cast<int>(done.size());
            progress_.games = static_cast<int>(games.size());
        }
        const auto keep_progress = [&]()
        {
            std::string text = base + "\n";
            for (const long long id : done)
                text += std::to_string(id) + "\n";
            save::write_atomic(part_path, romm_catalogue_text(games));
            save::write_atomic(done_path, text);
        };
        for (const RommPlatform &platform : platforms)
        {
            if (platform.rom_count == 0 ||
                std::find(done.begin(), done.end(), platform.id) != done.end())
                continue;
            const std::size_t first = games.size();
            bool read = true;
            for (int offset = 0; !cancel_.load(); offset += kPage)
            {
                long long total = -1;
                const std::size_t before = games.size();
                const bool got_page = get_(romm_games_url(base, platform.id, offset, kPage),
                                           headers, kMaxAnswer, &body, &error);
                if (!got_page || !parse_romm_games(body, platforms, &games, &total))
                {
                    fail(platform.name + ": " +
                         (got_page ? std::string("the answer is not a list of games") : error));
                    games.resize(first);
                    for (const RommGame &game : old)
                    {
                        if (game.platform_id == platform.id)
                            games.push_back(game);
                    }
                    read = false;
                    break;
                }
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    progress_.games = static_cast<int>(games.size());
                }
                // A short page, or as many as the server counts: the last.
                const long long got = static_cast<long long>(games.size() - before);
                if (got < kPage || (total >= 0 && offset + got >= total))
                    break;
            }
            if (read && !cancel_.load())
            {
                done.push_back(platform.id);
                keep_progress();
            }
        }
        if (cancel_.load())
            return fail("stopped");
        // Whole: the progress is not needed any more.
        std::remove(done_path.c_str());
        std::remove(part_path.c_str());
        // Without the saves the games are listed all the same.
        read_saves();
        const std::string written =
            save::write_atomic(state_dir + "/romm-games.tsv", romm_catalogue_text(games));
        if (!written.empty())
            return fail(written);
        // No covers here: most of a large library's games are on the
        // console already, with their emulators' art. The box art downloader
        // fetches RomM's cover for a game listed without any (ArtJob::first_url).
    };
    if (saves_only)
        read_saves();
    else
        work();
    running_ = false;
    finished_ = true;
}

} // namespace hui::orion
