// ps5-homebrew-ui - The titles the player starred, kept beside ORION's other state.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "library/favourites.hpp"

#include "core/save_file.hpp"
#include "library/json.hpp"
#include "library/library.hpp"

#include <algorithm>

namespace hui::library
{

namespace
{

std::string file_of(const std::string &state_dir)
{
    return state_dir + "/favourites.json";
}

} // namespace

std::vector<std::string> load_favourites(const std::string &state_dir)
{
    std::vector<std::string> ids;
    std::string text;
    json::Value root;
    if (state_dir.empty() || !save::read_file(file_of(state_dir), &text, 1u << 20) ||
        !json::parse(text, &root))
        return ids;
    for (const json::Value &value : root["favourites"].elements())
    {
        const std::string id = value.string_or("");
        if (is_title_id(id) && std::find(ids.begin(), ids.end(), id) == ids.end())
            ids.push_back(id);
    }
    return ids;
}

bool save_favourites(const std::string &state_dir, const std::vector<std::string> &ids)
{
    if (state_dir.empty() || !save::ensure_directory(state_dir))
        return false;
    // Title IDs are letters and digits only, so nothing needs escaping.
    std::string text = "{\n  \"favourites\": [";
    bool first = true;
    for (const std::string &id : ids)
    {
        if (!is_title_id(id))
            continue;
        text += first ? "\n    \"" : ",\n    \"";
        text += id + "\"";
        first = false;
    }
    text += first ? "]\n}\n" : "\n  ]\n}\n";
    return save::write_atomic(file_of(state_dir), text).empty();
}

} // namespace hui::library
