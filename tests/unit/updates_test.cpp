// ORION Media Center - Tests: the update check, with a fake GitHub.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/updates.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>

namespace hui
{
namespace
{

// A trimmed answer of GitHub's /releases/latest.
const char *const kLatest = R"json({
  "tag_name": "01.001.000",
  "html_url": "https://github.com/XeN-Made/Orion-Media-Center/releases/tag/01.001.000",
  "name": "ORION Media Center 01.001.000",
  "assets": [
    {"name": "SHA256SUMS", "browser_download_url": "https://example.invalid/SHA256SUMS"},
    {"name": "PPSA99061.zip", "size": 38592417,
     "browser_download_url": "https://example.invalid/PPSA99061.zip"}
  ]
})json";

// Waits for the check's thread, as the library does a frame at a time.
bool finish(orion::UpdateCheck &check)
{
    for (int i = 0; i < 400; ++i)
    {
        if (check.take_finished())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

orion::HttpGet answering(std::string body, bool ok = true)
{
    return [body = std::move(body), ok](const std::string &url, std::size_t, std::string *out,
                                        std::string *error)
    {
        EXPECT_EQ(url, orion::kLatestRelease);
        if (!ok)
        {
            *error = "HTTP 404";
            return false;
        }
        *out = body;
        return true;
    };
}

} // namespace

TEST(OrionUpdates, ReadsTheVersionThePageAndTheZip)
{
    orion::Release release;
    ASSERT_TRUE(orion::parse_release(kLatest, &release));
    EXPECT_EQ(release.version, "01.001.000");
    EXPECT_EQ(release.page,
              "https://github.com/XeN-Made/Orion-Media-Center/releases/tag/01.001.000");
    EXPECT_EQ(release.zip, "https://example.invalid/PPSA99061.zip");
    EXPECT_EQ(release.zip_size, 38592417u); // for the download's progress
    EXPECT_EQ(release.sums, "https://example.invalid/SHA256SUMS");
    // A tag that is not a version is no release ORION can compare.
    EXPECT_FALSE(orion::parse_release(R"({"tag_name": "beta-1"})", &release));
    EXPECT_FALSE(orion::parse_release("not json", &release));
}

TEST(OrionUpdates, ComparesVersionsAsNumbers)
{
    EXPECT_GT(orion::compare_versions("01.001.000", "01.000.010"), 0);
    EXPECT_LT(orion::compare_versions("01.000.009", "01.000.010"), 0);
    EXPECT_EQ(orion::compare_versions("01.000.010", "01.000.010"), 0);
    EXPECT_GT(orion::compare_versions("02.000.000", "01.999.999"), 0);
    // Not a version: never newer than one.
    EXPECT_LT(orion::compare_versions("", "01.000.000"), 0);
    EXPECT_LT(orion::compare_versions("1.0", "01.000.000"), 0);
}

TEST(OrionUpdates, SaysWhetherANewerOrionIsOut)
{
    orion::UpdateCheck newer(answering(kLatest));
    ASSERT_TRUE(newer.start("01.000.010"));
    ASSERT_TRUE(finish(newer));
    EXPECT_EQ(newer.state(), orion::UpdateCheck::State::available);
    EXPECT_EQ(newer.latest().version, "01.001.000");

    orion::UpdateCheck same(answering(kLatest));
    ASSERT_TRUE(same.start("01.001.000"));
    ASSERT_TRUE(finish(same));
    EXPECT_EQ(same.state(), orion::UpdateCheck::State::up_to_date);

    orion::UpdateCheck offline(answering("", false));
    ASSERT_TRUE(offline.start("01.000.010"));
    ASSERT_TRUE(finish(offline));
    EXPECT_EQ(offline.state(), orion::UpdateCheck::State::failed);
    EXPECT_EQ(offline.error(), "HTTP 404");
    EXPECT_FALSE(offline.take_finished()); // reported once
}

} // namespace hui
