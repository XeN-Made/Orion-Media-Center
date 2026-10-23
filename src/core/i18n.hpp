// ORION Media Center - Translations: English text to the language the player chose.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>

namespace hui::i18n
{

// A language: what each English text reads as in it. The English is the key,
// so text without a translation stays readable, in English. Written by hand
// as a .lang file:
//
//   # Español
//   code = es
//   name = Español
//   "Settings" = "Ajustes"
//   "Renamed to {0}" = "Renombrado como {0}"
//
// Lines starting with # are comments. Text is in double quotes, with \" for a
// quote, \\ for a backslash and \n for a new line. {0}, {1}... stand for what
// the sentence is built around (a name, a count) and may move in the
// translation; each must be kept.
struct Language
{
    std::string code; // "es", "pt-BR"
    std::string name; // in the language itself: "Español"
    std::string author;
    std::unordered_map<std::string, std::string> strings;
};

// False, with *error naming the line, for a file that is not a language.
bool parse_language(std::string_view text, Language *out, std::string *error);

// The language text is shown in from now on; nullptr is English. The
// language must outlive its use.
void use(const Language *language);
const Language *current();

// Text kept in a table to be translated where it is shown (tr(kNames[i])):
// unchanged here, marked for the tools that list what needs translating.
constexpr const char *marked(const char *english)
{
    return english;
}

// The text in the current language (the English when it has none).
std::string tr(std::string_view english);
// The same for a sentence with {0}, {1}... in it, filled with args.
std::string trf(std::string_view english, std::initializer_list<std::string_view> args);
// Fills {0}, {1}... of text with args; what is past the last arg stays.
std::string fill(std::string_view text, std::initializer_list<std::string_view> args);

} // namespace hui::i18n
