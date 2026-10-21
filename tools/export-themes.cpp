// ORION Media Center - Writes every built-in theme as an ORION theme file.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// usage: export-themes <output dir>
//
// The kit's themes are C++ (src/ui/theme.cpp); ORION reads its themes from
// files, the built-in ones from assets/themes. This writes one complete file
// per kit theme, so the app's own themes are exactly what a player's theme
// file is. Run through tools/export-themes.sh.

#include "core/save_file.hpp"
#include "orion/theme_file.hpp"
#include "ui/theme.hpp"

#include <cstdio>
#include <string>

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: %s <output dir>\n", argv[0]);
        return 2;
    }
    const std::string dir = argv[1];
    if (!hui::save::ensure_directory(dir))
    {
        std::fprintf(stderr, "cannot create %s\n", dir.c_str());
        return 1;
    }
    int written = 0;
    for (const hui::ui::Theme &theme : hui::ui::themes())
    {
        const hui::orion::ThemeFile file = hui::orion::from_kit(theme);
        const std::string path = dir + "/" + file.id + ".json";
        const std::string error = hui::save::write_atomic(path, hui::orion::write_theme(file));
        if (!error.empty())
        {
            std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
            return 1;
        }
        ++written;
    }
    std::printf("%d themes written to %s\n", written, dir.c_str());
    return 0;
}
