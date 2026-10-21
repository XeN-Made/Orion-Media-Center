// ORION Media Center - Tests: reading the console library.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// tests/fixtures/library is laid out like a console's /data: invented titles
// in homebrew/ (folders, images and things that are not titles) and
// ShadowMountPlus's icon cache.

#include "core/save_file.hpp"
#include "library/favourites.hpp"
#include "library/library.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

namespace hui
{
namespace
{

using library::Category;
using library::Platform;
using library::Source;
using library::Title;

std::string fixture()
{
    return std::string(HUI_SOURCE_DIR) + "/tests/fixtures/library";
}

std::vector<Title> scan_fixture(std::vector<std::string> exclude = {})
{
    library::ScanOptions options;
    options.roots = {fixture() + "/homebrew"};
    options.icon_cache = fixture() + "/icon-cache";
    options.exclude = std::move(exclude);
    return library::scan(options);
}

const Title *find(const std::vector<Title> &titles, const std::string &id)
{
    for (const Title &title : titles)
    {
        if (title.id == id)
            return &title;
    }
    return nullptr;
}
// The result points into the vector: keep it alive.
const Title *find(std::vector<Title> &&titles, const std::string &id) = delete;

TEST(Library, FindsEveryTitleSortedByName)
{
    const std::vector<Title> titles = scan_fixture();
    ASSERT_EQ(titles.size(), 7u);
    const char *expected[] = {"Ciel Ouvert",     "Glyph Garden", "Lantern Row",   "Pocket Cinema",
                              "Starfall Arcade", "Tidal Echo",   "Velvet Circuit"};
    for (std::size_t i = 0; i < titles.size(); ++i)
        EXPECT_EQ(titles[i].name, expected[i]) << i;
}

TEST(Library, ReadsFolderMetadata)
{
    const std::vector<Title> titles = scan_fixture();
    const Title *title = find(titles, "PPSA90101");
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->version, "01.002.000");
    EXPECT_EQ(title->category, Category::game);
    EXPECT_EQ(title->platform, Platform::ps5);
    // The folder wins over the image of the same title next to it.
    EXPECT_EQ(title->source, Source::folder);
    EXPECT_EQ(title->path, fixture() + "/homebrew/PPSA90101");
    EXPECT_EQ(title->icon, fixture() + "/homebrew/PPSA90101/sce_sys/icon0.png");

    // The folder name does not matter, only its metadata.
    const Title *media = find(titles, "PPSA90102");
    ASSERT_NE(media, nullptr);
    EXPECT_EQ(media->category, Category::media);
    EXPECT_EQ(media->path, fixture() + "/homebrew/Pocket Cinema");
}

TEST(Library, PrefersTheDefaultLanguage)
{
    const std::vector<Title> titles = scan_fixture();
    const Title *title = find(titles, "PPSA90108");
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->name, "Ciel Ouvert");
}

TEST(Library, ReadsPs4ParamSfo)
{
    const std::vector<Title> titles = scan_fixture();
    const Title *title = find(titles, "CUSA90106");
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->name, "Lantern Row");
    EXPECT_EQ(title->version, "01.05");
    EXPECT_EQ(title->platform, Platform::ps4);
    EXPECT_EQ(title->category, Category::game);
    EXPECT_FALSE(title->icon.empty());
}

TEST(Library, NamesImagesAndTakesTheirIconFromTheCache)
{
    const std::vector<Title> titles = scan_fixture();
    const Title *tidal = find(titles, "PPSA90104");
    ASSERT_NE(tidal, nullptr);
    EXPECT_EQ(tidal->name, "Tidal Echo");
    EXPECT_EQ(tidal->source, Source::image);
    // The largest cached icon, even though a smaller one is newer.
    EXPECT_EQ(tidal->icon, fixture() + "/icon-cache/PPSA90104-256-900-1.png");

    const Title *velvet = find(titles, "PPSA90105");
    ASSERT_NE(velvet, nullptr);
    EXPECT_EQ(velvet->name, "Velvet Circuit");
    EXPECT_TRUE(velvet->icon.empty());
}

TEST(Library, FallsBackToTheIconCacheForFolders)
{
    const std::vector<Title> titles = scan_fixture();
    const Title *title = find(titles, "PPSA90103");
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->icon, fixture() + "/icon-cache/PPSA90103-128-1000-1.png");
}

TEST(Library, LeavesOutExcludedTitles)
{
    const std::vector<Title> titles = scan_fixture({"PPSA90101", "PPSA90104"});
    EXPECT_EQ(titles.size(), 5u);
    EXPECT_EQ(find(titles, "PPSA90101"), nullptr);
    EXPECT_EQ(find(titles, "PPSA90104"), nullptr);
}

TEST(Library, MissingRootsGiveAnEmptyLibrary)
{
    library::ScanOptions options;
    options.roots = {fixture() + "/does-not-exist"};
    options.icon_cache = fixture() + "/also-missing";
    EXPECT_TRUE(library::scan(options).empty());
}

TEST(Library, ParsesImageNames)
{
    Title title;
    ASSERT_TRUE(library::parse_image_name("PPSA01234 - Some Title.ffpfsc", &title));
    EXPECT_EQ(title.id, "PPSA01234");
    EXPECT_EQ(title.name, "Some Title");
    EXPECT_EQ(title.source, Source::image);

    ASSERT_TRUE(library::parse_image_name("PPSA01234.FFPKG", &title));
    EXPECT_EQ(title.name, "PPSA01234");

    ASSERT_TRUE(library::parse_image_name("Some Title-CUSA01234-USA.exfat", &title));
    EXPECT_EQ(title.id, "CUSA01234");
    EXPECT_EQ(title.name, "Some Title");
    EXPECT_EQ(title.platform, Platform::ps4);

    EXPECT_FALSE(library::parse_image_name("PPSA01234 - Some Title.zip", &title));
    EXPECT_FALSE(library::parse_image_name("no id here.ffpfsc", &title));
    EXPECT_FALSE(library::parse_image_name("XPPSA01234.ffpfsc", &title));
}

TEST(Library, RefusesMetadataWithoutAValidId)
{
    Title title;
    EXPECT_FALSE(library::read_param_json(R"({"titleId": "ppsa01234"})", &title));
    EXPECT_FALSE(library::read_param_json(R"(["PPSA01234"])", &title));
    EXPECT_FALSE(library::read_param_json("{", &title));
    EXPECT_FALSE(library::read_param_sfo("\0PSF", &title));
    EXPECT_FALSE(library::read_param_sfo(std::string(64, '\0'), &title));

    // A title without any name shows its ID.
    ASSERT_TRUE(library::read_param_json(R"({"titleId": "PPSA01234"})", &title));
    EXPECT_EQ(title.name, "PPSA01234");
}

TEST(Library, ShelvesFollowTheMetadata)
{
    const std::vector<Title> titles = scan_fixture();
    const auto shelf = [&](const char *id) { return library::shelf_of(*find(titles, id)); };
    EXPECT_EQ(shelf("PPSA90101"), library::Shelf::games);    // DRM "standard"
    EXPECT_EQ(shelf("PPSA90108"), library::Shelf::homebrew); // DRM "free"
    EXPECT_EQ(shelf("PPSA90103"), library::Shelf::homebrew);
    EXPECT_EQ(shelf("PPSA90102"), library::Shelf::media); // the Media area wins
    EXPECT_EQ(shelf("CUSA90106"), library::Shelf::games); // PS4 titles are games
    EXPECT_EQ(shelf("PPSA90104"), library::Shelf::games); // so are images
    EXPECT_FALSE(find(titles, "PPSA90101")->homebrew);
    EXPECT_TRUE(find(titles, "PPSA90108")->homebrew);
    for (const Title &title : titles)
        EXPECT_GT(title.added, 0) << title.id;
}

TEST(Library, FavouritesRoundTripAndIgnoreJunk)
{
    const std::string dir = ::testing::TempDir() + "orion-favourites";
    ASSERT_TRUE(save::ensure_directory(dir));
    std::remove((dir + "/favourites.json").c_str());
    EXPECT_TRUE(library::load_favourites(dir).empty());

    ASSERT_TRUE(library::save_favourites(dir, {"PPSA90101", "CUSA90106", "not an id"}));
    EXPECT_EQ(library::load_favourites(dir), (std::vector<std::string>{"PPSA90101", "CUSA90106"}));

    // A hand-edited file: duplicates, other keys and wrong types are ignored.
    ASSERT_TRUE(
        save::write_atomic(dir + "/favourites.json",
                           R"({"note": 1, "favourites": ["PPSA90101", 7, "PPSA90101", "x"]})")
            .empty());
    EXPECT_EQ(library::load_favourites(dir), (std::vector<std::string>{"PPSA90101"}));

    ASSERT_TRUE(save::write_atomic(dir + "/favourites.json", "{broken").empty());
    EXPECT_TRUE(library::load_favourites(dir).empty());
    EXPECT_FALSE(library::save_favourites("", {"PPSA90101"}));
}

TEST(Library, RecognisesTitleIds)
{
    EXPECT_TRUE(library::is_title_id("PPSA01234"));
    EXPECT_TRUE(library::is_title_id("CUSA99999"));
    EXPECT_FALSE(library::is_title_id("PPSA0123"));
    EXPECT_FALSE(library::is_title_id("PPSA012345"));
    EXPECT_FALSE(library::is_title_id("ppsa01234"));
    EXPECT_FALSE(library::is_title_id("PPS101234"));
}

} // namespace
} // namespace hui
