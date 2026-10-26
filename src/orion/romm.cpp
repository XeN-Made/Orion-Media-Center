// ORION Media Center - RomM: the games on a RomM server, listed and downloaded.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/romm.hpp"

#include "core/local_host.hpp"
#include "core/save_file.hpp"
#include "library/art_match.hpp"
#include "library/json.hpp"

#include <cctype>
#include <cstdlib>

#include <sys/stat.h>

namespace hui::orion
{

namespace
{

constexpr std::size_t kMaxAnswer = 8u << 20; // a page of games, with their metadata
constexpr std::size_t kMaxCover = 8u << 20;
constexpr int kPage = 200;

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

std::string romm_cover_url(const std::string &base, const RommGame &game)
{
    if (game.cover.empty())
        return {};
    if (game.cover.rfind("http://", 0) == 0 || game.cover.rfind("https://", 0) == 0)
        return game.cover;
    return base + (game.cover.front() == '/' ? "" : "/") + game.cover;
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

RommSync::RommSync(HttpGetWith get, HttpDownloadWith download)
    : get_(std::move(get)), download_(std::move(download))
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

bool RommSync::start(std::string base, std::string token, std::string state_dir)
{
    if (running_.load() || base.empty() || token.empty() || state_dir.empty())
        return false;
    wait();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        progress_ = Progress();
    }
    cancel_ = false;
    running_ = true;
    worker_ = std::thread([this, base = std::move(base), token = std::move(token),
                           state_dir = std::move(state_dir)]() { run(base, token, state_dir); });
    return true;
}

void RommSync::run(std::string base, std::string token, std::string state_dir)
{
    const std::vector<std::string> headers = romm_headers(token);
    const auto fail = [&](const std::string &error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (progress_.error.empty())
            progress_.error = error;
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
        }
        std::vector<RommGame> games;
        for (const RommPlatform &platform : platforms)
        {
            if (platform.rom_count == 0)
                continue;
            for (int offset = 0; !cancel_.load(); offset += kPage)
            {
                long long total = -1;
                const std::size_t before = games.size();
                if (!get_(romm_games_url(base, platform.id, offset, kPage), headers, kMaxAnswer,
                          &body, &error))
                    return fail(error);
                if (!parse_romm_games(body, platforms, &games, &total))
                    return fail("the server's answer is not a list of games");
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    progress_.games = static_cast<int>(games.size());
                }
                // A short page, or as many as the server counts: the last.
                const long long got = static_cast<long long>(games.size() - before);
                if (got < kPage || (total >= 0 && offset + got >= total))
                    break;
            }
        }
        if (cancel_.load())
            return fail("stopped");
        const std::string written =
            save::write_atomic(state_dir + "/romm-games.tsv", romm_catalogue_text(games));
        if (!written.empty())
            return fail(written);
        // Covers last: the games are listed already while they come.
        const std::string covers = state_dir + "/romm/covers";
        save::ensure_directory(state_dir + "/romm");
        save::ensure_directory(covers);
        for (const RommGame &game : games)
        {
            if (cancel_.load())
                break;
            const std::string url = romm_cover_url(base, game);
            if (url.empty())
                continue;
            const std::string path =
                covers + "/" + std::to_string(game.id) + picture_extension(url);
            if (is_file(path))
                continue;
            if (download_(url, headers, path, kMaxCover, &error))
            {
                std::lock_guard<std::mutex> lock(mutex_);
                ++progress_.covers;
            }
        }
    };
    work();
    running_ = false;
    finished_ = true;
}

} // namespace hui::orion
