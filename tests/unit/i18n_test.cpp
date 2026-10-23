// ORION Media Center - Tests: translations, and the languages ORION ships.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/i18n.hpp"
#include "core/save_file.hpp"
#include "orion/languages.hpp"
#include "orion/playtime.hpp"

#include <gtest/gtest.h>

#ifndef HUI_SOURCE_DIR
#define HUI_SOURCE_DIR "."
#endif

namespace hui
{
namespace
{

// Every test leaves the text in English, whatever it switched to.
struct I18n : ::testing::Test
{
    ~I18n() override
    {
        i18n::use(nullptr);
    }
};

TEST_F(I18n, ReadsALanguageFile)
{
    i18n::Language language;
    std::string error;
    ASSERT_TRUE(i18n::parse_language("# a comment\n"
                                     "code = es\n"
                                     "name = Español\n"
                                     "\"Settings\" = \"Ajustes\"\n"
                                     "\"Say \\\"hi\\\"\\nto {0}\" = \"Di \\\"hola\\\"\\na {0}\"\n"
                                     "\"Untranslated\" = \"\"\n",
                                     &language, &error))
        << error;
    EXPECT_EQ(language.code, "es");
    EXPECT_EQ(language.name, "Español");
    EXPECT_EQ(language.strings.at("Settings"), "Ajustes");
    EXPECT_EQ(language.strings.at("Say \"hi\"\nto {0}"), "Di \"hola\"\na {0}");
    EXPECT_EQ(language.strings.count("Untranslated"), 0u); // empty stays English

    EXPECT_FALSE(i18n::parse_language("name = No code\n", &language, &error));
    EXPECT_FALSE(i18n::parse_language("code = es\n\"Open = \"x\"\n", &language, &error));
    EXPECT_NE(error.find("line 2"), std::string::npos) << error;
}

TEST_F(I18n, TranslatesFillsAndCounts)
{
    EXPECT_EQ(i18n::tr("Settings"), "Settings");
    EXPECT_EQ(i18n::trf("Renamed to {0}", {"Zed"}), "Renamed to Zed");
    EXPECT_EQ(i18n::plural(1, "{0} game", "{0} games"), "1 game");
    EXPECT_EQ(i18n::plural(3, "{0} game", "{0} games"), "3 games");

    i18n::Language language;
    language.code = "xx";
    language.strings["Settings"] = "Ajustes";
    language.strings["{0} of {1}"] = "{1}: {0}"; // the order may change
    language.strings["{0} games"] = "{0} juegos";
    i18n::use(&language);
    EXPECT_EQ(i18n::tr("Settings"), "Ajustes");
    EXPECT_EQ(i18n::tr("Not in the file"), "Not in the file");
    EXPECT_EQ(i18n::trf("{0} of {1}", {"3", "9"}), "9: 3");
    EXPECT_EQ(i18n::plural(2, "{0} game", "{0} games"), "2 juegos");
    EXPECT_EQ(i18n::fill("{0} and {2}", {"a"}), "a and {2}");
}

TEST_F(I18n, PicksTheConsolesLanguageOrTheChosenOne)
{
    orion::LanguageLibrary languages;
    languages.load(std::string(HUI_SOURCE_DIR) + "/assets/lang", "");
    EXPECT_TRUE(languages.problems().empty());
    ASSERT_NE(languages.pick("es", ""), nullptr);
    EXPECT_EQ(languages.pick("es", "")->name, "Español");
    ASSERT_NE(languages.pick("", "pt-BR"), nullptr);
    EXPECT_EQ(languages.pick("", "pt")->code, "pt-BR"); // its family
    EXPECT_EQ(languages.pick("", "en-US"), nullptr);
    EXPECT_EQ(languages.pick("en", "es"), nullptr); // English chosen over the console's

    const std::vector<orion::LanguageChoice> choices = languages.choices("es");
    ASSERT_GE(choices.size(), 4u);
    EXPECT_EQ(choices[0].code, "");
    EXPECT_EQ(choices[1].name, "English");
}

TEST_F(I18n, TheBuiltInLanguagesCoverTheTemplate)
{
    // docs/lang/template.lang lists every text (tools/gen-lang.py --check
    // keeps it current); each shipped language translates all of it.
    std::string text;
    ASSERT_TRUE(
        save::read_file(std::string(HUI_SOURCE_DIR) + "/docs/lang/template.lang", &text, 1u << 20));
    i18n::Language keys;
    std::string error;
    // The template's translations are empty, so read its keys line by line.
    std::vector<std::string> english;
    std::size_t start = 0;
    while (start < text.size())
    {
        std::size_t end = text.find('\n', start);
        end = end == std::string::npos ? text.size() : end;
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (line.empty() || line[0] != '"')
            continue;
        const std::size_t equals = line.rfind(" = \"\"");
        ASSERT_NE(equals, std::string::npos) << line;
        line.replace(equals, std::string::npos, " = \"x\"");
        ASSERT_TRUE(i18n::parse_language("code = t\n" + line + "\n", &keys, &error)) << error;
        english.push_back(keys.strings.begin()->first);
    }
    ASSERT_GT(english.size(), 400u);

    orion::LanguageLibrary languages;
    languages.load(std::string(HUI_SOURCE_DIR) + "/assets/lang", "");
    for (const char *code : {"es", "pt-BR"})
    {
        const i18n::Language *language = languages.pick(code, "");
        ASSERT_NE(language, nullptr) << code;
        for (const std::string &key : english)
            EXPECT_EQ(language->strings.count(key), 1u) << code << ": " << key;
    }
}

TEST_F(I18n, PlayTimesReadInTheLanguage)
{
    orion::LanguageLibrary languages;
    languages.load(std::string(HUI_SOURCE_DIR) + "/assets/lang", "");
    i18n::use(languages.pick("es", ""));
    EXPECT_EQ(orion::play_time_text(2 * 3600 + 5 * 60), "2 h 5 min");
    EXPECT_EQ(orion::ago_text(0, 3 * 86400), "hace 3 días");
    i18n::use(nullptr);
    EXPECT_EQ(orion::ago_text(0, 86400), "yesterday");
}

} // namespace
} // namespace hui
