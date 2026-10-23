// ORION Media Center - The languages ORION can be shown in: its own and the player's files.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/i18n.hpp"

#include <memory>
#include <string>
#include <vector>

namespace hui::orion
{

// One row of the Language choice: a code ("" is Automatic) and its name.
struct LanguageChoice
{
    std::string code;
    std::string name;
};

// ORION's languages ship as .lang files in the app (assets/lang); the
// player's go in <state>/lang and are read the same way, so a language can be
// added or corrected without a new ORION (core/i18n.hpp has the format). A
// player's file replaces ORION's of the same code. English is the text in the
// code itself and needs no file.
class LanguageLibrary
{
  public:
    void load(const std::string &builtin_dir, const std::string &user_dir);
    // The language a pref asks for: a code, or "" for the console's own
    // (system, as sys::system_language says it). A code with no file of its
    // own takes one of its family ("pt" finds "pt-BR"). nullptr: English.
    const i18n::Language *pick(const std::string &pref, const std::string &system) const;
    // What the Language row offers: Automatic (naming the language it picks),
    // English, then each language by its own name.
    std::vector<LanguageChoice> choices(const std::string &system) const;
    // Files that were skipped, one line each: "<path>: <why>".
    const std::vector<std::string> &problems() const
    {
        return problems_;
    }

  private:
    void read_dir(const std::string &dir);

    std::vector<std::unique_ptr<i18n::Language>> languages_;
    std::vector<std::string> problems_;
};

} // namespace hui::orion
