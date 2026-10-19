// ps5-homebrew-ui - ORION theme files: a whole look as one JSON document.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/theme_file.hpp"

#include "core/save_file.hpp"
#include "library/json.hpp"
#include "orion/look.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace hui::orion
{

namespace
{

using gfx::Color;

constexpr std::size_t kMaxThemeFile = 256u << 10;
constexpr std::size_t kMaxThemes = 128;

constexpr const char *kStyles[] = {"flat",  "soft",    "hard", "neumorphic", "bevel", "gloss",
                                   "glass", "outline", "glow", "pixel",      "sketch"};
constexpr const char *kCorners[] = {"round", "chamfer", "pixel"};
constexpr const char *kFonts[] = {"regular", "semibold", "display", "mono", "pixel", "hand"};
constexpr const char *kSounds[] = {"paper", "glass"};

// The palette keys and the Theme members they fill.
struct PaletteKey
{
    const char *key;
    Color ui::Theme::*member;
};
constexpr PaletteKey kPalette[] = {
    {"surface", &ui::Theme::surface},     {"surface_high", &ui::Theme::surface_high},
    {"text", &ui::Theme::text},           {"text_muted", &ui::Theme::text_muted},
    {"primary", &ui::Theme::primary},     {"on_primary", &ui::Theme::on_primary},
    {"secondary", &ui::Theme::secondary}, {"on_secondary", &ui::Theme::on_secondary},
    {"accent", &ui::Theme::accent},       {"outline", &ui::Theme::outline},
    {"focus", &ui::Theme::focus},         {"shadow", &ui::Theme::shadow},
    {"light", &ui::Theme::light},         {"danger", &ui::Theme::danger},
    {"success", &ui::Theme::success},     {"warning", &ui::Theme::warning},
};

template <std::size_t N> int index_of(const char *const (&names)[N], const std::string &name)
{
    for (std::size_t i = 0; i < N; ++i)
    {
        if (name == names[i])
            return static_cast<int>(i);
    }
    return -1;
}

// "#rrggbb" or "#rrggbbaa".
bool parse_colour(std::string_view text, Color *colour)
{
    if (!text.empty() && text.front() == '#')
        text.remove_prefix(1);
    if (text.size() != 6 && text.size() != 8)
        return false;
    std::uint32_t value = 0;
    for (char c : text)
    {
        value <<= 4;
        if (c >= '0' && c <= '9')
            value |= static_cast<std::uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            value |= static_cast<std::uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            value |= static_cast<std::uint32_t>(c - 'A' + 10);
        else
            return false;
    }
    *colour = text.size() == 6 ? Color::rgb(value)
                               : Color::rgb(value >> 8, static_cast<float>(value & 0xff) / 255.0f);
    return true;
}

std::string colour_text(Color colour)
{
    const auto byte = [](float v)
    { return static_cast<unsigned>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    char text[12];
    if (byte(colour.a) == 255)
        std::snprintf(text, sizeof(text), "\"#%02x%02x%02x\"", byte(colour.r), byte(colour.g),
                      byte(colour.b));
    else
        std::snprintf(text, sizeof(text), "\"#%02x%02x%02x%02x\"", byte(colour.r), byte(colour.g),
                      byte(colour.b), byte(colour.a));
    return text;
}

std::string number_text(float value)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%g", static_cast<double>(value));
    return text;
}

void read_colour(const json::Value &section, const char *key, Color *colour)
{
    Color parsed;
    if (parse_colour(section[key].string_or(""), &parsed))
        *colour = parsed;
}

void read_number(const json::Value &section, const char *key, float lo, float hi, float *value)
{
    if (section[key].is_number())
        *value = std::clamp(static_cast<float>(section[key].number_or(*value)), lo, hi);
}

void read_bool(const json::Value &section, const char *key, bool *value)
{
    *value = section[key].bool_or(*value);
}

bool valid_id(std::string_view id)
{
    return !id.empty() && id.size() <= 40 &&
           std::all_of(id.begin(), id.end(),
                       [](char c) {
                           return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                                  c == '_';
                       });
}

gfx::BackdropMode background_of(const std::string &name, gfx::BackdropMode fallback)
{
    if (name == "none")
        return gfx::BackdropMode::none;
    for (const Background &kind : backgrounds())
    {
        if (name == kind.id)
            return kind.mode;
    }
    // The renderer's own names are accepted too.
    if (name == "stars")
        return gfx::BackdropMode::stars;
    if (name == "grid")
        return gfx::BackdropMode::grid;
    return fallback;
}

const char *background_name(gfx::BackdropMode mode)
{
    return mode == gfx::BackdropMode::none ? "none" : background_id(mode);
}

const ui::Theme &kit_theme(std::string_view id)
{
    for (const ui::Theme &theme : ui::themes())
    {
        if (id == theme.id)
            return theme;
    }
    for (const ui::Theme &theme : ui::themes())
    {
        if (std::strcmp(theme.id, "orion") == 0)
            return theme;
    }
    return ui::themes()[0];
}

} // namespace

void ThemeFile::bind()
{
    theme.id = id.c_str();
    theme.name = name.c_str();
    theme.family = family.c_str();
    theme.summary = description.c_str();
}

ThemeFile from_kit(const ui::Theme &theme)
{
    ThemeFile file;
    file.id = theme.id;
    file.name = theme.name;
    file.family = theme.family != nullptr ? theme.family : "";
    file.description = theme.summary != nullptr ? theme.summary : "";
    file.author = "ORION";
    file.theme = theme;
    file.bind();
    return file;
}

bool parse_theme(std::string_view text, const ThemeResolver &resolve, ThemeFile *out,
                 std::string *error)
{
    const auto fail = [&](std::string why)
    {
        if (error != nullptr)
            *error = std::move(why);
        return false;
    };
    json::Value root;
    std::string json_error;
    if (!json::parse(text, &root, &json_error))
        return fail("not JSON: " + json_error);
    if (!root.is_object())
        return fail("not a JSON object");
    const std::string format = root["format"].string_or("orion-theme");
    if (format != "orion-theme")
        return fail("format is \"" + format + "\", not \"orion-theme\"");
    if (root["version"].number_or(1.0) > 1.0)
        return fail("made for a newer ORION (version " +
                    std::to_string(static_cast<int>(root["version"].number_or(0.0))) + ")");
    const std::string id = root["id"].string_or("");
    if (!valid_id(id))
        return fail("\"id\" must be 1-40 lowercase letters, digits, '-' or '_'");
    const std::string name = root["name"].string_or("");
    if (name.empty() || name.size() > 40)
        return fail("\"name\" must be 1-40 characters");

    // Start from what it extends: a loaded theme, a kit theme, or ORION.
    const std::string extends = root["extends"].string_or("orion");
    ThemeFile file;
    const ThemeFile *base = resolve ? resolve(extends) : nullptr;
    if (base != nullptr)
        file = *base;
    else
        file = from_kit(kit_theme(extends));
    file.id = id;
    file.name = name;
    file.author = root["author"].string_or("");
    file.description = root["description"].string_or(file.description);
    file.family = root["family"].string_or(file.family);
    file.path.clear();
    file.user = false;
    ui::Theme &t = file.theme;

    const json::Value &page = root["page"];
    t.backdrop.mode = background_of(page["background"].string_or(""), t.backdrop.mode);
    for (std::size_t i = 0; i < 4 && i < page["colors"].size(); ++i)
        parse_colour(page["colors"][i].string_or(""), &t.backdrop.colors[i]);
    for (std::size_t i = 0; i < 4 && i < page["params"].size(); ++i)
        t.backdrop.params[i] =
            static_cast<float>(page["params"][i].number_or(t.backdrop.params[i]));
    read_colour(page, "color", &t.page);
    read_colour(page, "text", &t.page_text);
    read_colour(page, "text_muted", &t.page_text_muted);

    const json::Value &palette = root["palette"];
    for (const PaletteKey &key : kPalette)
        read_colour(palette, key.key, &(t.*key.member));

    const json::Value &shape = root["shape"];
    if (const int style = index_of(kStyles, shape["style"].string_or("")); style >= 0)
        t.style = static_cast<ui::SurfaceStyle>(style);
    if (const int corner = index_of(kCorners, shape["corner"].string_or("")); corner >= 0)
        t.corner = static_cast<ui::Corner>(corner);
    read_number(shape, "radius", 0.0f, 200.0f, &t.radius);
    read_number(shape, "radius_card", 0.0f, 120.0f, &t.radius_card);
    read_number(shape, "border", 0.0f, 12.0f, &t.border);
    read_number(shape, "button_border", -1.0f, 12.0f, &t.button_border);
    read_bool(shape, "pill_chips", &t.pill_chips);
    read_bool(shape, "pill_switches", &t.pill_switches);
    read_number(shape, "shadow_offset", 0.0f, 40.0f, &t.shadow_offset);
    read_number(shape, "shadow_blur", 0.0f, 120.0f, &t.shadow_blur);
    read_number(shape, "focus_width", 0.0f, 12.0f, &t.focus_width);
    read_number(shape, "focus_gap", 0.0f, 20.0f, &t.focus_gap);
    read_bool(shape, "underline_fields", &t.underline_fields);

    const json::Value &type = root["type"];
    if (const int heading = index_of(kFonts, type["heading"].string_or("")); heading >= 0)
        t.heading = static_cast<ui::FontRole>(heading);
    if (const int label = index_of(kFonts, type["label"].string_or("")); label >= 0)
        t.label = static_cast<ui::FontRole>(label);
    read_bool(type, "caps", &t.caps);
    read_number(type, "tracking", 0.0f, 12.0f, &t.tracking);

    const json::Value &motion = root["motion"];
    read_number(motion, "omega", 1.0f, 120.0f, &t.omega);
    read_number(motion, "damping", 0.1f, 1.0f, &t.damping);
    if (const int sounds = index_of(kSounds, motion["sounds"].string_or("")); sounds >= 0)
        t.sounds = static_cast<audio::SoundSet>(sounds);
    read_bool(root, "dark", &t.dark);

    const json::Value &orion = root["orion"];
    float clouds = file.clouds * 100.0f;
    read_number(orion, "clouds", 0.0f, 200.0f, &clouds);
    file.clouds = clouds / 100.0f;
    read_bool(orion, "motion", &file.motion);
    read_number(orion, "cover_corners", 0.0f, 48.0f, &file.cover_corners);

    file.bind();
    *out = std::move(file);
    out->bind();
    return true;
}

std::string write_theme(const ThemeFile &file)
{
    const ui::Theme &t = file.theme;
    std::string out = "{\n";
    out += "  \"format\": \"orion-theme\",\n  \"version\": 1,\n";
    out += "  \"id\": " + json::quote(file.id) + ",\n";
    out += "  \"name\": " + json::quote(file.name) + ",\n";
    out += "  \"author\": " + json::quote(file.author) + ",\n";
    out += "  \"family\": " + json::quote(file.family) + ",\n";
    out += "  \"description\": " + json::quote(file.description) + ",\n";

    out += "  \"page\": {\n";
    out += std::string("    \"background\": \"") + background_name(t.backdrop.mode) + "\",\n";
    out += "    \"colors\": [";
    for (int i = 0; i < 4; ++i)
        out += (i ? ", " : "") + colour_text(t.backdrop.colors[i]);
    out += "],\n    \"params\": [";
    for (int i = 0; i < 4; ++i)
        out += (i ? ", " : "") + number_text(t.backdrop.params[i]);
    out += "],\n";
    out += "    \"color\": " + colour_text(t.page) + ",\n";
    out += "    \"text\": " + colour_text(t.page_text) + ",\n";
    out += "    \"text_muted\": " + colour_text(t.page_text_muted) + "\n  },\n";

    out += "  \"palette\": {\n";
    for (std::size_t i = 0; i < std::size(kPalette); ++i)
        out += std::string("    \"") + kPalette[i].key +
               "\": " + colour_text(t.*kPalette[i].member) +
               (i + 1 < std::size(kPalette) ? ",\n" : "\n");
    out += "  },\n";

    const auto flag = [](bool value) { return value ? "true" : "false"; };
    out += "  \"shape\": {\n";
    out += std::string("    \"style\": \"") + kStyles[static_cast<int>(t.style)] + "\",\n";
    out += std::string("    \"corner\": \"") + kCorners[static_cast<int>(t.corner)] + "\",\n";
    out += "    \"radius\": " + number_text(t.radius) + ",\n";
    out += "    \"radius_card\": " + number_text(t.radius_card) + ",\n";
    out += "    \"border\": " + number_text(t.border) + ",\n";
    out += "    \"button_border\": " + number_text(t.button_border) + ",\n";
    out += std::string("    \"pill_chips\": ") + flag(t.pill_chips) + ",\n";
    out += std::string("    \"pill_switches\": ") + flag(t.pill_switches) + ",\n";
    out += "    \"shadow_offset\": " + number_text(t.shadow_offset) + ",\n";
    out += "    \"shadow_blur\": " + number_text(t.shadow_blur) + ",\n";
    out += "    \"focus_width\": " + number_text(t.focus_width) + ",\n";
    out += "    \"focus_gap\": " + number_text(t.focus_gap) + ",\n";
    out += std::string("    \"underline_fields\": ") + flag(t.underline_fields) + "\n  },\n";

    out += "  \"type\": {\n";
    out += std::string("    \"heading\": \"") + kFonts[static_cast<int>(t.heading)] + "\",\n";
    out += std::string("    \"label\": \"") + kFonts[static_cast<int>(t.label)] + "\",\n";
    out += std::string("    \"caps\": ") + flag(t.caps) + ",\n";
    out += "    \"tracking\": " + number_text(t.tracking) + "\n  },\n";

    out += "  \"motion\": {\n";
    out += "    \"omega\": " + number_text(t.omega) + ",\n";
    out += "    \"damping\": " + number_text(t.damping) + ",\n";
    out += std::string("    \"sounds\": \"") + kSounds[std::min(static_cast<int>(t.sounds), 1)] +
           "\"\n  },\n";
    out += std::string("  \"dark\": ") + flag(t.dark) + ",\n";

    out += "  \"orion\": {\n";
    out += "    \"clouds\": " + number_text(std::round(file.clouds * 100.0f)) + ",\n";
    out += std::string("    \"motion\": ") + flag(file.motion) + ",\n";
    out += "    \"cover_corners\": " + number_text(file.cover_corners) + "\n  }\n}\n";
    return out;
}

std::string theme_id_from_name(std::string_view name)
{
    std::string id;
    for (char c : name)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) != 0 && u < 0x80)
            id += static_cast<char>(std::tolower(u));
        else if (!id.empty() && id.back() != '-')
            id += '-';
        if (id.size() >= 40)
            break;
    }
    while (!id.empty() && id.back() == '-')
        id.pop_back();
    return id;
}

const ThemeFile *ThemeLibrary::find(std::string_view id) const
{
    for (const auto &file : themes_)
    {
        if (file->id == id)
            return file.get();
    }
    return nullptr;
}

const ThemeFile &ThemeLibrary::fallback() const
{
    if (const ThemeFile *orion = find("orion"))
        return *orion;
    if (!themes_.empty())
        return *themes_.front();
    static const ThemeFile kOrion = from_kit(kit_theme("orion"));
    return kOrion;
}

void ThemeLibrary::add(std::unique_ptr<ThemeFile> file)
{
    file->bind();
    themes_.push_back(std::move(file));
}

void ThemeLibrary::read_dir(const std::string &dir, bool user)
{
    if (dir.empty())
        return;
    const ThemeResolver resolve = [this](std::string_view id) { return find(id); };
    for (const save::DirectoryEntry &entry : save::list_directory(dir))
    {
        const std::string &name = entry.name;
        if (entry.directory || name.front() == '.' || name.size() < 6 ||
            name.compare(name.size() - 5, 5, ".json") != 0)
            continue;
        const std::string path = dir + "/" + name;
        if (themes_.size() >= kMaxThemes)
        {
            problems_.push_back(path + ": skipped, ORION keeps at most 128 themes");
            continue;
        }
        std::string text;
        std::string why;
        auto file = std::make_unique<ThemeFile>();
        if (!save::read_file(path, &text, kMaxThemeFile))
            why = "cannot be read (or is over 256 KiB)";
        else if (parse_theme(text, resolve, file.get(), &why) && find(file->id) != nullptr)
            why = "the id \"" + file->id + "\" is already taken";
        else if (why.empty())
        {
            file->path = path;
            file->user = user;
            add(std::move(file));
            continue;
        }
        problems_.push_back(path + ": " + why);
    }
}

void ThemeLibrary::load(const std::string &builtin_dir, const std::string &user_dir)
{
    themes_.clear();
    problems_.clear();
    user_dir_ = user_dir;
    read_dir(builtin_dir, false);
    if (themes_.empty())
    {
        for (const ui::Theme &theme : ui::themes())
            add(std::make_unique<ThemeFile>(from_kit(theme)));
    }
    // ORION leads, whatever its file is called.
    const auto orion = std::find_if(themes_.begin(), themes_.end(),
                                    [](const auto &file) { return file->id == "orion"; });
    if (orion != themes_.end())
        std::rotate(themes_.begin(), orion, orion + 1);
    read_dir(user_dir, true);
}

bool ThemeLibrary::save_user_theme(ThemeFile file, std::string *error)
{
    const auto fail = [&](std::string why)
    {
        if (error != nullptr)
            *error = std::move(why);
        return false;
    };
    if (!valid_id(file.id))
        return fail("the theme needs a name made of letters or digits");
    const ThemeFile *existing = find(file.id);
    if (existing != nullptr && !existing->user)
        return fail("\"" + file.id + "\" is a built-in theme's id");
    if (user_dir_.empty() || !save::ensure_directory(user_dir_))
        return fail("cannot create " + user_dir_);
    const std::string path = user_dir_ + "/" + file.id + ".json";
    const std::string written = save::write_atomic(path, write_theme(file));
    if (!written.empty())
        return fail(written);
    file.path = path;
    file.user = true;
    for (auto &slot : themes_)
    {
        if (slot->id == file.id)
        {
            *slot = std::move(file);
            slot->bind();
            return true;
        }
    }
    add(std::make_unique<ThemeFile>(std::move(file)));
    return true;
}

} // namespace hui::orion
