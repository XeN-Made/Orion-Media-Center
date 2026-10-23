// ORION Media Center - Translations: English text to the language the player chose.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/i18n.hpp"

#include <vector>

namespace hui::i18n
{

namespace
{

// Set from the main thread between frames; read while drawing on it.
const Language *g_current = nullptr;

std::string trim(std::string_view text)
{
    std::size_t start = 0;
    std::size_t end = text.size();
    while (start < end && (text[start] == ' ' || text[start] == '\t'))
        ++start;
    while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r'))
        --end;
    return std::string(text.substr(start, end - start));
}

// A quoted string at *at ("..." with \" \\ \n); *at moves past it.
bool quoted(std::string_view line, std::size_t *at, std::string *out)
{
    while (*at < line.size() && (line[*at] == ' ' || line[*at] == '\t'))
        ++*at;
    if (*at >= line.size() || line[*at] != '"')
        return false;
    out->clear();
    for (std::size_t i = *at + 1; i < line.size(); ++i)
    {
        const char c = line[i];
        if (c == '"')
        {
            *at = i + 1;
            return true;
        }
        if (c == '\\' && i + 1 < line.size())
        {
            const char next = line[++i];
            *out += next == 'n' ? '\n' : next;
            continue;
        }
        *out += c;
    }
    return false;
}

} // namespace

bool parse_language(std::string_view text, Language *out, std::string *error)
{
    Language language;
    int number = 0;
    std::size_t start = 0;
    while (start <= text.size())
    {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        const std::string line = trim(text.substr(start, end - start));
        start = end + 1;
        ++number;
        const auto fail = [&](const std::string &why)
        {
            if (error != nullptr)
                *error = "line " + std::to_string(number) + ": " + why;
            return false;
        };
        if (line.empty() || line[0] == '#')
            continue;
        if (line[0] == '"')
        {
            std::size_t at = 0;
            std::string english;
            std::string translated;
            if (!quoted(line, &at, &english))
                return fail("an unfinished quote");
            while (at < line.size() && (line[at] == ' ' || line[at] == '\t'))
                ++at;
            if (at >= line.size() || line[at] != '=')
                return fail("expected \"English\" = \"translation\"");
            ++at;
            if (!quoted(line, &at, &translated))
                return fail("the translation is not in quotes");
            if (!english.empty() && !translated.empty())
                language.strings[english] = translated;
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos)
            return fail("expected key = value");
        const std::string key = trim(std::string_view(line).substr(0, equals));
        const std::string value = trim(std::string_view(line).substr(equals + 1));
        if (key == "code")
            language.code = value;
        else if (key == "name")
            language.name = value;
        else if (key == "author")
            language.author = value;
    }
    if (language.code.empty() || language.code.size() > 16)
        return error != nullptr ? (*error = "a language needs a code (code = es)", false) : false;
    if (language.name.empty())
        language.name = language.code;
    *out = std::move(language);
    return true;
}

void use(const Language *language)
{
    g_current = language;
}

const Language *current()
{
    return g_current;
}

std::string tr(std::string_view english)
{
    if (g_current != nullptr)
    {
        const auto found = g_current->strings.find(std::string(english));
        if (found != g_current->strings.end())
            return found->second;
    }
    return std::string(english);
}

std::string fill(std::string_view text, std::initializer_list<std::string_view> args)
{
    const std::vector<std::string_view> values(args);
    std::string out;
    out.reserve(text.size() + 16);
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '{' && i + 2 < text.size() && text[i + 2] == '}' && text[i + 1] >= '0' &&
            text[i + 1] <= '9')
        {
            const std::size_t index = static_cast<std::size_t>(text[i + 1] - '0');
            if (index < values.size())
            {
                out += values[index];
                i += 2;
                continue;
            }
        }
        out += text[i];
    }
    return out;
}

std::string trf(std::string_view english, std::initializer_list<std::string_view> args)
{
    return fill(tr(english), args);
}

} // namespace hui::i18n
