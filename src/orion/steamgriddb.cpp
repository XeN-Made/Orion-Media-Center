// ORION Media Center - SteamGridDB: covers for games the other box art sources lack.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/steamgriddb.hpp"

#include "core/save_file.hpp"
#include "library/art_match.hpp"
#include "library/json.hpp"

#include <cctype>

namespace hui::orion
{

namespace
{

constexpr const char *kApi = "https://www.steamgriddb.com/api/v2";
constexpr std::size_t kMaxAnswer = 512u << 10;
constexpr std::size_t kMaxPicture = 16u << 20;

// A cover's file name ends as its address does: .jpg for a JPEG, else .png
// (the decoder reads either by its contents).
std::string picture_extension(const std::string &url)
{
    const std::size_t dot = url.rfind('.');
    std::string extension = dot == std::string::npos ? "" : url.substr(dot);
    for (char &c : extension)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return extension == ".jpg" || extension == ".jpeg" ? ".jpg" : ".png";
}

// PNG or JPEG by their first bytes: an error page is neither.
bool is_picture(const std::string &bytes)
{
    return (bytes.size() > 8 && bytes.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0) ||
           (bytes.size() > 3 && static_cast<unsigned char>(bytes[0]) == 0xFF &&
            static_cast<unsigned char>(bytes[1]) == 0xD8);
}

std::string trim(std::string_view text)
{
    std::size_t start = 0;
    std::size_t end = text.size();
    while (start < end && std::isspace(static_cast<unsigned char>(text[start])) != 0)
        ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
        --end;
    return std::string(text.substr(start, end - start));
}

bool is_service_line(std::string_view line, std::string_view service, std::string *value)
{
    const std::size_t equals = line.find('=');
    if (line.empty() || line.front() == '#' || equals == std::string_view::npos ||
        trim(line.substr(0, equals)) != service)
        return false;
    if (value != nullptr)
        *value = trim(line.substr(equals + 1));
    return true;
}

} // namespace

std::string read_service_key(const std::string &state_dir, std::string_view service)
{
    std::string text;
    if (state_dir.empty() || !save::read_file(state_dir + "/keys.txt", &text, 64u << 10))
        return {};
    std::size_t start = 0;
    while (start < text.size())
    {
        std::size_t end = text.find('\n', start);
        end = end == std::string::npos ? text.size() : end;
        std::string value;
        if (is_service_line(trim(std::string_view(text).substr(start, end - start)), service,
                            &value))
            return value;
        start = end + 1;
    }
    return {};
}

std::string write_service_key(const std::string &state_dir, std::string_view service,
                              std::string_view key)
{
    if (state_dir.empty())
        return "no folder for keys.txt";
    const std::string value = trim(key);
    if (value.find_first_of("\r\n") != std::string::npos)
        return "a key is one line";
    std::string text;
    save::read_file(state_dir + "/keys.txt", &text, 64u << 10);
    std::string out;
    bool written = false;
    std::size_t start = 0;
    while (start < text.size())
    {
        std::size_t end = text.find('\n', start);
        end = end == std::string::npos ? text.size() : end;
        const std::string line = text.substr(start, end - start);
        start = end + 1;
        if (!is_service_line(trim(line), service, nullptr))
            out += line + "\n";
        else if (!written && !value.empty())
        {
            out += std::string(service) + " = " + value + "\n";
            written = true;
        }
    }
    if (out.empty())
        out = "# ORION's keys for other services, one a line.\n";
    if (!written && !value.empty())
        out += std::string(service) + " = " + value + "\n";
    return save::write_atomic(state_dir + "/keys.txt", out);
}

std::string grid_query(std::string_view name)
{
    std::string out;
    int depth = 0;
    for (const char c : name)
    {
        if (c == '(' || c == '[')
            ++depth;
        else if ((c == ')' || c == ']') && depth > 0)
            --depth;
        else if (depth == 0)
        {
            const bool space = std::isspace(static_cast<unsigned char>(c)) != 0 || c == '_';
            if (!space || (!out.empty() && out.back() != ' '))
                out += space ? ' ' : c;
        }
    }
    return trim(out);
}

std::string grid_search_url(std::string_view name)
{
    return std::string(kApi) + "/search/autocomplete/" + library::url_encode(grid_query(name));
}

std::string grid_covers_url(long long game)
{
    return std::string(kApi) + "/grids/game/" + std::to_string(game) +
           "?dimensions=600x900,342x482,660x930&mimes=image/png,image/jpeg&types=static"
           "&nsfw=false&humor=false";
}

bool parse_grid_search(std::string_view json, std::vector<long long> *games)
{
    json::Value root;
    if (!json::parse(json, &root) || !root["success"].bool_or(false))
        return false;
    const json::Value &data = root["data"];
    for (std::size_t i = 0; i < data.size(); ++i)
    {
        const double id = data[i]["id"].number_or(0.0);
        if (id > 0.0)
            games->push_back(static_cast<long long>(id));
    }
    return true;
}

bool parse_grid_covers(std::string_view json, std::vector<GridCover> *covers)
{
    json::Value root;
    if (!json::parse(json, &root) || !root["success"].bool_or(false))
        return false;
    const json::Value &data = root["data"];
    for (std::size_t i = 0; i < data.size(); ++i)
    {
        const json::Value &item = data[i];
        const std::string mime = item["mime"].string_or("");
        GridCover cover;
        cover.url = item["url"].string_or("");
        cover.thumb = item["thumb"].string_or("");
        if (cover.url.rfind("https://", 0) != 0 || (mime != "image/png" && mime != "image/jpeg"))
            continue;
        if (cover.thumb.rfind("https://", 0) != 0)
            cover.thumb = cover.url;
        covers->push_back(std::move(cover));
    }
    return true;
}

bool find_grid_covers(const HttpGetWith &get, const std::string &key, std::string_view name,
                      std::size_t most, std::vector<GridCover> *covers, std::string *error)
{
    if (key.empty() || grid_query(name).empty())
    {
        *error = key.empty() ? "no SteamGridDB key" : "no name to search for";
        return false;
    }
    const std::vector<std::string> headers = {"Authorization: Bearer " + key};
    std::string body;
    if (!get(grid_search_url(name), headers, kMaxAnswer, &body, error))
        return false;
    std::vector<long long> games;
    if (!parse_grid_search(body, &games))
    {
        *error = "SteamGridDB's answer is not a search";
        return false;
    }
    if (games.empty())
        return true;
    if (!get(grid_covers_url(games.front()), headers, kMaxAnswer, &body, error))
        return false;
    std::vector<GridCover> found;
    if (!parse_grid_covers(body, &found))
    {
        *error = "SteamGridDB's answer has no covers";
        return false;
    }
    for (GridCover &cover : found)
    {
        if (covers->size() >= most)
            break;
        covers->push_back(std::move(cover));
    }
    return true;
}

GridPicker::GridPicker(HttpGetWith get) : get_(std::move(get))
{
}

GridPicker::~GridPicker()
{
    wait();
}

void GridPicker::wait()
{
    if (worker_.joinable())
        worker_.join();
}

void GridPicker::begin(std::function<void()> work)
{
    wait();
    running_ = true;
    worker_ = std::thread(
        [this, work = std::move(work)]
        {
            work();
            std::lock_guard<std::mutex> lock(mutex_);
            finished_ = true;
            running_ = false;
        });
}

bool GridPicker::search(std::string key, std::string name, std::string folder, std::string id)
{
    if (running_.load())
        return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        covers_.clear();
        ready_.clear();
        error_.clear();
        kept_.clear();
        finished_ = false;
    }
    cancelled_ = false;
    begin(
        [this, key = std::move(key), name = std::move(name), folder = std::move(folder),
         id = std::move(id)]
        {
            std::vector<GridCover> covers;
            std::string error;
            const bool asked = find_grid_covers(get_, key, name, kMostCovers, &covers, &error);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                covers_ = covers;
                if (!asked)
                    error_ = error;
            }
            save::ensure_directory(folder);
            for (std::size_t i = 0; i < covers.size() && !cancelled_.load(); ++i)
            {
                std::string bytes;
                if (!get_(covers[i].thumb, {}, kMaxPicture, &bytes, &error) || !is_picture(bytes))
                    continue;
                const std::string path = folder + "/" + id + "-" + std::to_string(i) +
                                         picture_extension(covers[i].thumb);
                if (!save::write_atomic(path, bytes).empty())
                    continue;
                std::lock_guard<std::mutex> lock(mutex_);
                ready_.emplace_back(i, path);
            }
        });
    return true;
}

bool GridPicker::keep(std::size_t index, std::string target_stem)
{
    GridCover cover;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_.load() || index >= covers_.size())
            return false;
        cover = covers_[index];
        error_.clear();
        kept_.clear();
        finished_ = false;
    }
    cancelled_ = false;
    begin(
        [this, cover = std::move(cover), target_stem = std::move(target_stem)]
        {
            std::string bytes;
            std::string error;
            const std::string path = target_stem + picture_extension(cover.url);
            if (!get_(cover.url, {}, kMaxPicture, &bytes, &error) || !is_picture(bytes))
                error = error.empty() ? std::string("not a picture") : error;
            else
                error = save::write_atomic(path, bytes);
            std::lock_guard<std::mutex> lock(mutex_);
            if (error.empty())
                kept_ = path;
            else
                error_ = error;
        });
    return true;
}

std::vector<std::pair<std::size_t, std::string>> GridPicker::take_ready()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(ready_, {});
}

std::size_t GridPicker::count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return covers_.size();
}

bool GridPicker::take_finished()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(finished_, false);
}

std::string GridPicker::error() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

std::string GridPicker::kept() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return kept_;
}

} // namespace hui::orion
