// ORION Media Center - The console's own library: what ShadowMountPlus can mount.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ShadowMountPlus registers every child of its scan roots that carries title
// metadata: an unpacked folder (sce_sys/param.json on PS5, param.sfo on PS4)
// or a disk image (.ffpfsc, .ffpkg, ...). Reading the same places gives the
// app the library the home screen shows, without asking the system: a title's
// sandbox cannot open the system's app database, but ShadowMountPlus mounts
// /data and /mnt/ext0 into every title it launches.

#include "library/library.hpp"

#include "core/save_file.hpp"
#include "library/art_match.hpp"
#include "library/json.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unordered_map>

namespace hui::library
{

namespace
{

// Metadata files are small; anything larger is not one.
constexpr std::size_t kMaxMetadata = 256u << 10;

constexpr std::string_view kImageExtensions[] = {".ffpfsc", ".ffpkg", ".ffpfs", ".exfat"};

using Entry = save::DirectoryEntry;

// Children of a directory with hidden names (".upload" temporaries) left out.
std::vector<Entry> visible_entries(const std::string &path, save::ListingStats *stats = nullptr)
{
    std::vector<Entry> entries = save::list_directory(path, stats);
    std::erase_if(entries, [](const Entry &entry) { return entry.name.front() == '.'; });
    return entries;
}

std::int64_t modified_time(const std::string &path)
{
    struct stat info = {};
    return ::stat(path.c_str(), &info) == 0 ? static_cast<std::int64_t>(info.st_mtime) : 0;
}

bool file_exists(const std::string &path)
{
    struct stat info = {};
    return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

bool ends_with(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() &&
           std::equal(suffix.begin(), suffix.end(), text.end() - suffix.size(),
                      [](char a, char b)
                      {
                          return std::tolower(static_cast<unsigned char>(a)) ==
                                 std::tolower(static_cast<unsigned char>(b));
                      });
}

Platform platform_of(std::string_view id)
{
    if (id.substr(0, 4) == "PPSA")
        return Platform::ps5;
    if (id.substr(0, 4) == "CUSA")
        return Platform::ps4;
    return Platform::unknown;
}

std::string trim(std::string_view text)
{
    const auto is_space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    while (!text.empty() && is_space(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && is_space(text.back()))
        text.remove_suffix(1);
    return std::string(text);
}

// A name made of separators only ("-", "_", ".") says nothing.
bool has_letters(std::string_view text)
{
    return std::any_of(text.begin(), text.end(),
                       [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; });
}

std::uint32_t read_u32(std::string_view bytes, std::size_t at)
{
    return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at])) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 1])) << 8 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 2])) << 16 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 3])) << 24;
}

std::uint16_t read_u16(std::string_view bytes, std::size_t at)
{
    return static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[at]) |
                                      static_cast<unsigned char>(bytes[at + 1]) << 8);
}

// The cached icon with the largest size, then the latest stamp, for an ID.
std::string cached_icon(const std::vector<Entry> &cache, const std::string &directory,
                        std::string_view id)
{
    std::string best;
    long best_size = -1;
    long best_stamp = -1;
    for (const Entry &entry : cache)
    {
        const std::string_view name = entry.name;
        if (entry.directory || name.size() < id.size() + 1 || name.substr(0, id.size()) != id ||
            name[id.size()] != '-' || !ends_with(name, ".png"))
            continue;
        const char *fields = entry.name.c_str() + id.size() + 1;
        char *end = nullptr;
        const long size = std::strtol(fields, &end, 10);
        const long stamp = *end == '-' ? std::strtol(end + 1, nullptr, 10) : 0;
        if (size > best_size || (size == best_size && stamp > best_stamp))
        {
            best_size = size;
            best_stamp = stamp;
            best = directory + "/" + entry.name;
        }
    }
    return best;
}

} // namespace

bool is_title_id(std::string_view text)
{
    if (text.size() != 9)
        return false;
    for (std::size_t i = 0; i < 4; ++i)
    {
        if (text[i] < 'A' || text[i] > 'Z')
            return false;
    }
    for (std::size_t i = 4; i < 9; ++i)
    {
        if (text[i] < '0' || text[i] > '9')
            return false;
    }
    return true;
}

bool is_item_id(std::string_view text)
{
    if (is_title_id(text))
        return true;
    if (text.size() != 16 || (text.substr(0, 4) != "rom-" && text.substr(0, 4) != "elf-"))
        return false;
    return std::all_of(text.begin() + 4, text.end(),
                       [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool read_param_json(std::string_view text, Title *title)
{
    json::Value root;
    if (!json::parse(text, &root) || !root.is_object())
        return false;
    const std::string id = root["titleId"].string_or("");
    if (!is_title_id(id))
        return false;

    // The display name lives under the default language; fall back to
    // English, then to any language that has one.
    const json::Value &names = root["localizedParameters"];
    std::string name =
        names[names["defaultLanguage"].string_or("en-US")]["titleName"].string_or("");
    if (name.empty())
        name = names["en-US"]["titleName"].string_or("");
    for (const auto &member : names.members())
    {
        if (!name.empty())
            break;
        name = member.second["titleName"].string_or("");
    }

    title->id = id;
    title->name = name.empty() ? id : trim(name);
    title->version = root["contentVersion"].string_or("");
    title->platform = Platform::ps5;
    title->homebrew = root["applicationDrmType"].string_or("") == "free";
    // 0 is the Games area and 65536 Media (docs/platform/CONFIGURATION.md).
    const double type = root["applicationCategoryType"].number_or(0.0);
    title->category = type == 0.0       ? Category::game
                      : type == 65536.0 ? Category::media
                                        : Category::app;
    return true;
}

bool read_param_sfo(std::string_view bytes, Title *title)
{
    // Header: magic "\0PSF", version, key table, data table, entry count;
    // then 16-byte entries: key offset, format, length, capacity, data offset.
    constexpr std::size_t kHeader = 20;
    constexpr std::size_t kEntry = 16;
    if (bytes.size() < kHeader || bytes.substr(0, 4) != std::string_view("\0PSF", 4))
        return false;
    const std::uint32_t keys = read_u32(bytes, 8);
    const std::uint32_t data = read_u32(bytes, 12);
    const std::uint32_t count = read_u32(bytes, 16);
    if (count > 256 || kHeader + std::size_t{count} * kEntry > bytes.size() ||
        keys > bytes.size() || data > bytes.size())
        return false;

    std::string id;
    std::string name;
    std::string category;
    std::string version;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const std::size_t at = kHeader + std::size_t{i} * kEntry;
        const std::size_t key_at = keys + read_u16(bytes, at);
        const std::uint16_t format = read_u16(bytes, at + 2);
        const std::size_t length = read_u32(bytes, at + 4);
        const std::size_t value_at = data + std::size_t{read_u32(bytes, at + 12)};
        if (key_at >= bytes.size() || value_at > bytes.size() || length > bytes.size() - value_at)
            continue;
        const std::string_view key_area = bytes.substr(key_at);
        const std::string_view key = key_area.substr(0, key_area.find('\0'));
        if (format != 0x0204 && format != 0x0004) // UTF-8 strings only
            continue;
        std::string_view value = bytes.substr(value_at, length);
        value = value.substr(0, value.find('\0'));
        if (key == "TITLE_ID")
            id = std::string(value);
        else if (key == "TITLE")
            name = trim(value);
        else if (key == "CATEGORY")
            category = std::string(value);
        else if (key == "APP_VER")
            version = std::string(value);
    }
    if (!is_title_id(id))
        return false;
    title->id = id;
    title->name = name.empty() ? id : name;
    title->version = version;
    title->platform = platform_of(id) == Platform::unknown ? Platform::ps4 : platform_of(id);
    // "gd" is an application in the Games area; everything else (patches,
    // system and media apps) is listed as an app.
    title->category = category == "gd" ? Category::game : Category::app;
    return true;
}

bool parse_image_name(std::string_view file_name, Title *title)
{
    std::string_view stem = file_name;
    bool known = false;
    for (std::string_view extension : kImageExtensions)
    {
        if (ends_with(stem, extension))
        {
            stem.remove_suffix(extension.size());
            known = true;
            break;
        }
    }
    if (!known)
        return false;

    // The ID may sit anywhere in a release name; take the first one.
    std::size_t at = std::string_view::npos;
    for (std::size_t i = 0; i + 9 <= stem.size(); ++i)
    {
        if (is_title_id(stem.substr(i, 9)) &&
            (i == 0 || !std::isalnum(static_cast<unsigned char>(stem[i - 1]))))
        {
            at = i;
            break;
        }
    }
    if (at == std::string_view::npos)
        return false;

    // Release names put the title before the ID; ShadowMountPlus's own naming
    // is "ID - Title". Separators around the name are dropped.
    const auto strip = [](std::string_view text)
    {
        while (!text.empty() && (text.front() == ' ' || text.front() == '-' || text.front() == '_'))
            text.remove_prefix(1);
        while (!text.empty() && (text.back() == ' ' || text.back() == '-' || text.back() == '_'))
            text.remove_suffix(1);
        return text;
    };
    std::string_view name = strip(stem.substr(at + 9));
    if (at > 0 && has_letters(strip(stem.substr(0, at))))
        name = strip(stem.substr(0, at));
    // A release group's tags go ("[SPSX]-", "(EUR)"), and a name written with
    // dots or underscores for spaces ("Planet.Zoo") gets its spaces back.
    const bool spaced = name.find(' ') != std::string_view::npos;
    std::string clean;
    for (std::size_t i = 0; i < name.size(); ++i)
    {
        const char close = name[i] == '[' ? ']' : name[i] == '(' ? ')' : '\0';
        if (const std::size_t end = close != '\0' ? name.find(close, i) : std::string_view::npos;
            end != std::string_view::npos)
        {
            i = end;
            continue;
        }
        const char c = !spaced && (name[i] == '.' || name[i] == '_') ? ' ' : name[i];
        if (c != ' ' || (!clean.empty() && clean.back() != ' '))
            clean += c;
    }
    name = strip(clean);

    title->id = std::string(stem.substr(at, 9));
    title->name = has_letters(name) ? std::string(name) : title->id;
    title->platform = platform_of(title->id);
    title->category = Category::game;
    title->source = Source::image;
    return true;
}

std::string registered_line(const Title &title)
{
    std::string name = title.name;
    for (char &c : name)
    {
        if (c == '\t' || c == '\n' || c == '\r')
            c = ' ';
    }
    return title.id + "\t" + std::to_string(static_cast<int>(title.platform)) + "\t" +
           std::to_string(static_cast<int>(title.category)) + "\t" + (title.homebrew ? "1" : "0") +
           "\t" + name + "\n";
}

int merge_registered(Library &library, std::string_view cache, const std::string &icons,
                     const std::string &skip_id)
{
    int added = 0;
    std::size_t start = 0;
    while (start < cache.size())
    {
        std::size_t end = cache.find('\n', start);
        if (end == std::string_view::npos)
            end = cache.size();
        const std::string_view line = cache.substr(start, end - start);
        start = end + 1;
        std::vector<std::string_view> fields;
        std::size_t at = 0;
        for (int i = 0; i < 4; ++i)
        {
            const std::size_t tab = line.find('\t', at);
            if (tab == std::string_view::npos)
                break;
            fields.push_back(line.substr(at, tab - at));
            at = tab + 1;
        }
        if (fields.size() != 4 || at >= line.size() || !is_title_id(fields[0]) ||
            fields[0] == skip_id)
            continue;
        const std::string id(fields[0]);
        const auto listed = std::find_if(library.titles.begin(), library.titles.end(),
                                         [&](const Title &t) { return t.id == id; });
        if (listed != library.titles.end())
        {
            // An image's name was read from its file name; the system's is
            // the game's own. A name the player gave it stays (apply_names
            // keeps the one it came with in own_name).
            const std::string_view registered = line.substr(at);
            if (listed->source == Source::image && has_letters(registered))
                (listed->own_name.empty() ? listed->name : listed->own_name) =
                    std::string(registered);
            continue;
        }
        const auto number = [](std::string_view text, int top)
        {
            const int value = text.size() == 1 ? text[0] - '0' : -1;
            return value >= 0 && value <= top ? value : -1;
        };
        const int platform = number(fields[1], static_cast<int>(Platform::unknown));
        const int category = number(fields[2], static_cast<int>(Category::app));
        if (platform < 0 || category < 0)
            continue;
        Title title;
        title.id = id;
        title.name = std::string(line.substr(at));
        title.platform = static_cast<Platform>(platform);
        title.category = static_cast<Category>(category);
        title.homebrew = fields[3] == "1";
        title.source = Source::installed;
        const std::string icon = icons + "/" + id + ".png";
        if (file_exists(icon))
            title.icon = icon;
        library.titles.push_back(std::move(title));
        ++added;
    }
    return added;
}

int apply_names(Library &library, const std::vector<std::pair<std::string, std::string>> &names)
{
    int changed = 0;
    for (const auto &[id, name] : names)
    {
        for (Title &title : library.titles)
        {
            if (title.id != id)
                continue;
            if (title.own_name.empty())
                title.own_name = title.name;
            const std::string &wanted = name.empty() ? title.own_name : name;
            if (title.name != wanted)
            {
                title.name = wanted;
                ++changed;
            }
        }
    }
    return changed;
}

std::vector<std::string> apply_custom_art(Library &library, const std::string &folder)
{
    // The folder is read once: each picture by its exact stem and folded.
    std::unordered_map<std::string, std::string> exact;
    std::unordered_map<std::string, std::string> folded;
    for (const save::DirectoryEntry &entry : save::list_directory(folder))
    {
        const std::size_t dot = entry.name.rfind('.');
        if (entry.directory || dot == std::string::npos || dot == 0)
            continue;
        std::string extension = entry.name.substr(dot + 1);
        for (char &c : extension)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (extension != "png" && extension != "jpg" && extension != "jpeg")
            continue;
        const std::string stem = entry.name.substr(0, dot);
        const std::string path = folder + "/" + entry.name;
        exact.emplace(stem, path);
        const std::string key = art_key(stem);
        if (!key.empty())
            folded.emplace(key, path);
    }
    const auto by_key = [&](const std::string &name) -> std::string
    {
        const auto found = folded.find(art_key(name));
        return name.empty() || found == folded.end() ? std::string() : found->second;
    };
    std::vector<std::string> changed;
    for (Title &title : library.titles)
    {
        std::string picture;
        if (const auto found = exact.find(title.id); found != exact.end())
            picture = found->second;
        if (picture.empty())
            picture = by_key(title.name);
        if (picture.empty())
            picture = by_key(title.own_name);
        if (picture.empty() && !title.rom.empty())
        {
            const std::size_t slash = title.rom.rfind('/');
            const std::string file = title.rom.substr(slash == std::string::npos ? 0 : slash + 1);
            picture = by_key(file.substr(0, file.rfind('.')));
        }
        if (!picture.empty() && title.icon != picture)
        {
            if (title.own_icon.empty())
                title.own_icon = title.icon.empty() ? std::string("-") : title.icon;
            title.icon = picture;
            changed.push_back(title.id);
        }
        else if (picture.empty() && !title.own_icon.empty())
        {
            // "-": it had no picture before the player's.
            title.icon = title.own_icon == "-" ? std::string() : title.own_icon;
            title.own_icon.clear();
            changed.push_back(title.id);
        }
    }
    return changed;
}

int prefer_saved_icons(Library &library, const std::string &folder)
{
    int found = 0;
    for (Title &title : library.titles)
    {
        const std::string saved = folder + "/" + title.id + ".png";
        if (title.is_rom() || title.icon == saved || !file_exists(saved))
            continue;
        title.icon = saved;
        ++found;
    }
    return found;
}

ScanOptions console_defaults()
{
    ScanOptions options;
    options.roots = {"/data/homebrew", "/mnt/ext0/homebrew"};
    for (int usb = 0; usb < 8; ++usb)
        options.roots.push_back("/mnt/usb" + std::to_string(usb) + "/homebrew");
    options.icon_cache = "/data/shadowmount/icon-cache";
    return options;
}

std::vector<Title> scan(const ScanOptions &options)
{
    std::vector<Title> titles;
    std::vector<Title> images;
    const auto known = [&](const std::string &id)
    {
        if (std::find(options.exclude.begin(), options.exclude.end(), id) != options.exclude.end())
            return true;
        return std::any_of(titles.begin(), titles.end(),
                           [&](const Title &title) { return title.id == id; });
    };

    for (const std::string &root : options.roots)
    {
        save::ListingStats stats;
        const std::vector<Entry> entries = visible_entries(root, &stats);
        const std::size_t before = titles.size() + images.size();
        for (const Entry &entry : entries)
        {
            const std::string path = root + "/" + entry.name;
            Title title;
            if (entry.directory)
            {
                std::string metadata;
                const bool json =
                    save::read_file(path + "/sce_sys/param.json", &metadata, kMaxMetadata);
                if (!(json
                          ? read_param_json(metadata, &title)
                          : save::read_file(path + "/sce_sys/param.sfo", &metadata, kMaxMetadata) &&
                                read_param_sfo(metadata, &title)))
                    continue;
                if (known(title.id))
                    continue;
                title.source = Source::folder;
                title.path = path;
                title.added = modified_time(path);
                if (file_exists(path + "/sce_sys/icon0.png"))
                    title.icon = path + "/sce_sys/icon0.png";
                titles.push_back(std::move(title));
            }
            else if (parse_image_name(entry.name, &title))
            {
                title.path = path;
                title.added = modified_time(path);
                images.push_back(std::move(title));
            }
        }
        if (options.report != nullptr)
        {
            // Every root gets a line, so a folder that comes back empty says why.
            char line[256];
            if (stats.open_error != 0)
                std::snprintf(line, sizeof(line), "%s: cannot open (errno %d)", root.c_str(),
                              stats.open_error);
            else
                std::snprintf(line, sizeof(line),
                              "%s: %zu entries, %zu titles (reads %d, records %d, empty %d, "
                              "read errno %d, stat failures %d errno %d)",
                              root.c_str(), entries.size(), titles.size() + images.size() - before,
                              stats.reads, stats.records, stats.empty_records, stats.read_error,
                              stats.stat_failures, stats.stat_error);
            options.report->push_back(line);
        }
    }

    // Images last, so a folder of the same title is the one listed.
    const std::vector<Entry> cache = visible_entries(options.icon_cache);
    for (Title &image : images)
    {
        if (known(image.id))
            continue;
        image.icon = cached_icon(cache, options.icon_cache, image.id);
        titles.push_back(std::move(image));
    }
    // Folder titles without an icon of their own may still have a cached one.
    for (Title &title : titles)
    {
        if (title.icon.empty())
            title.icon = cached_icon(cache, options.icon_cache, title.id);
    }

    const auto folded = [](const std::string &text)
    {
        std::string out(text);
        for (char &c : out)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    };
    std::sort(titles.begin(), titles.end(),
              [&](const Title &a, const Title &b)
              {
                  const std::string fa = folded(a.name);
                  const std::string fb = folded(b.name);
                  return fa != fb ? fa < fb : a.id < b.id;
              });
    return titles;
}

std::vector<Title> scan_payloads(const std::vector<std::string> &folders)
{
    std::vector<Title> found;
    for (const std::string &folder : folders)
    {
        const std::vector<save::DirectoryEntry> entries = save::list_directory(folder);
        const auto has = [&](const std::string &name)
        {
            return std::any_of(entries.begin(), entries.end(), [&](const save::DirectoryEntry &e)
                               { return !e.directory && e.name == name; });
        };
        for (const save::DirectoryEntry &entry : entries)
        {
            const std::size_t dot = entry.name.rfind('.');
            if (entry.directory || dot == std::string::npos || dot == 0)
                continue;
            std::string extension = entry.name.substr(dot + 1);
            for (char &c : extension)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (extension != "elf")
                continue;
            Title title;
            title.path = folder + "/" + entry.name;
            // Its ID by its path, as an emulated game's is.
            std::uint64_t hash = 1469598103934665603ull;
            for (const char c : title.path)
                hash = (hash ^ static_cast<unsigned char>(c)) * 1099511628211ull;
            char id[24];
            std::snprintf(id, sizeof(id), "elf-%012llx",
                          static_cast<unsigned long long>(hash & 0xffffffffffffull));
            title.id = id;
            if (std::any_of(found.begin(), found.end(),
                            [&](const Title &t) { return t.id == title.id; }))
                continue;
            const std::string stem = entry.name.substr(0, dot);
            for (const char c : stem)
                title.name += c == '_' ? ' ' : c;
            for (const char *kind : {".png", ".jpg", ".jpeg"})
            {
                if (title.icon.empty() && has(stem + kind))
                    title.icon = folder + "/" + stem + kind;
            }
            title.category = Category::app;
            title.homebrew = true;
            title.source = Source::payload;
            struct stat info = {};
            if (::stat(title.path.c_str(), &info) == 0)
                title.added = static_cast<std::int64_t>(info.st_mtime);
            found.push_back(std::move(title));
        }
    }
    return found;
}

Shelf shelf_of(const Title &title)
{
    if (title.category == Category::media)
        return Shelf::media;
    // Retail titles are PPSA (PS5) or CUSA (PS4). Tools a payload installs
    // as a tile (PSUP00001, PLDM00001, WKAL00001...) often declare no DRM
    // type at all, so their ID is what tells them from games.
    const bool retail_id =
        !is_title_id(title.id) || title.id.rfind("PPSA", 0) == 0 || title.id.rfind("CUSA", 0) == 0;
    return title.homebrew || !retail_id ? Shelf::homebrew : Shelf::games;
}

const char *category_name(Category category)
{
    switch (category)
    {
    case Category::game:
        return "Game";
    case Category::media:
        return "Media";
    case Category::app:
        return "App";
    }
    return "App";
}

const char *platform_name(Platform platform)
{
    switch (platform)
    {
    case Platform::ps5:
        return "PS5";
    case Platform::ps4:
        return "PS4";
    case Platform::unknown:
        return "";
    }
    return "";
}

} // namespace hui::library
