// ORION Media Center - The languages ORION can be shown in: its own and the player's files.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/languages.hpp"

#include "core/save_file.hpp"

#include <algorithm>

namespace hui::orion
{

namespace
{

constexpr std::size_t kMaxLanguageFile = 512u << 10;
constexpr std::size_t kMaxLanguages = 32;

} // namespace

void LanguageLibrary::read_dir(const std::string &dir)
{
    for (const save::DirectoryEntry &entry : save::list_directory(dir))
    {
        const std::string &name = entry.name;
        if (entry.directory || name.size() < 6 || name.compare(name.size() - 5, 5, ".lang") != 0)
            continue;
        const std::string path = dir + "/" + name;
        std::string text;
        std::string why;
        auto language = std::make_unique<i18n::Language>();
        if (!save::read_file(path, &text, kMaxLanguageFile))
            why = "cannot be read (or is over 512 KiB)";
        else if (i18n::parse_language(text, language.get(), &why))
        {
            // The player's file of a code replaces ORION's.
            std::erase_if(languages_,
                          [&](const auto &have) { return have->code == language->code; });
            if (languages_.size() < kMaxLanguages && language->code != "en")
                languages_.push_back(std::move(language));
            continue;
        }
        problems_.push_back(path + ": " + why);
    }
}

void LanguageLibrary::load(const std::string &builtin_dir, const std::string &user_dir)
{
    languages_.clear();
    problems_.clear();
    read_dir(builtin_dir);
    if (!user_dir.empty())
        read_dir(user_dir);
    std::sort(languages_.begin(), languages_.end(),
              [](const auto &a, const auto &b) { return a->name < b->name; });
}

const i18n::Language *LanguageLibrary::pick(const std::string &pref,
                                            const std::string &system) const
{
    const std::string code = pref.empty() ? system : pref;
    if (code.empty() || code == "en")
        return nullptr;
    for (const auto &language : languages_)
    {
        if (language->code == code)
            return language.get();
    }
    // "pt" or "pt-PT": the family's language ("pt-BR").
    const std::string family = code.substr(0, code.find('-'));
    for (const auto &language : languages_)
    {
        if (language->code.substr(0, language->code.find('-')) == family)
            return language.get();
    }
    return nullptr;
}

std::vector<LanguageChoice> LanguageLibrary::choices(const std::string &system) const
{
    const i18n::Language *automatic = pick("", system);
    std::vector<LanguageChoice> choices;
    choices.push_back(
        {"", i18n::trf("Automatic ({0})", {automatic != nullptr ? automatic->name : "English"})});
    choices.push_back({"en", "English"});
    for (const auto &language : languages_)
        choices.push_back({language->code, language->name});
    return choices;
}

} // namespace hui::orion
