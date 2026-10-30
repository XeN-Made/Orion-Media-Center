// ORION Media Center - Tests: the Activity sheet, its jobs and how it closes.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "component_fixture.hpp"
#include "orion/activity_panel.hpp"
#include "ui/theme.hpp"

#include <gtest/gtest.h>

namespace
{

using hui::Action;
using hui::orion::ActivityJob;
using hui::orion::ActivityPanel;

class OrionActivity : public hui::testing::ComponentFixture
{
  protected:
    void settle(ActivityPanel &panel)
    {
        for (int i = 0; i < 30; ++i)
            panel.update(kFrame);
    }
};

// Jobs with a known end and without one draw in every theme; with none it
// says nothing is running.
TEST_F(OrionActivity, DrawsItsJobsInEveryTheme)
{
    for (const hui::ui::Theme &theme : hui::ui::themes())
    {
        ActivityPanel panel;
        panel.set_theme(theme, false);
        panel.open(feedback_);
        settle(panel);
        auto empty = canvas();
        panel.draw(empty);
        expect_drawn(theme.id);
        ActivityJob install;
        install.text = "Installing Moon Garden  ·  1.2 GB of 31.8 GB";
        install.progress = 0.04f;
        install.detail = "24.3 MB/s  ·  about 21 min left";
        ActivityJob art;
        art.text = "Copying game backgrounds…";
        panel.set_jobs({install, art});
        panel.update(kFrame);
        const std::size_t before = list_.instances().size();
        auto busy = canvas();
        panel.draw(busy);
        expect_drawn(theme.id);
        EXPECT_GT(list_.instances().size(), before) << theme.id;
    }
}

// The average since it started, and the time that leaves; nothing in the
// first seconds or before a byte has come.
TEST(OrionActivitySpeed, SaysTheAverageAndTheTimeLeft)
{
    using hui::orion::transfer_speed;
    constexpr std::uint64_t kMb = 1024u * 1024u;
    EXPECT_EQ(transfer_speed(10 * kMb, 100 * kMb, 1.0), "");
    EXPECT_EQ(transfer_speed(0, 100 * kMb, 30.0), "");
    EXPECT_EQ(transfer_speed(240 * kMb, 0, 10.0), "24.0 MB/s");
    EXPECT_EQ(transfer_speed(240 * kMb, 240 * kMb, 10.0), "24.0 MB/s");
    EXPECT_EQ(transfer_speed(240 * kMb, 241 * kMb, 10.0), "24.0 MB/s  ·  about a minute left");
    // 24 MB/s with 36,000 MB to go: 25 minutes.
    EXPECT_EQ(transfer_speed(240 * kMb, 36240 * kMb, 10.0), "24.0 MB/s  ·  about 25 min left");
    // 10 MB/s with 50,000 MB to go: 84 minutes (83.3, rounded up).
    EXPECT_EQ(transfer_speed(100 * kMb, 50100 * kMb, 10.0), "10.0 MB/s  ·  about 1 h 24 min left");
}

// Circle closes it, and so does L3, the button that opened it.
TEST_F(OrionActivity, CircleOrL3Closes)
{
    ActivityPanel panel;
    EXPECT_FALSE(panel.handle(press(Action::back), feedback_)); // closed: nothing to do
    panel.open(feedback_);
    settle(panel);
    EXPECT_TRUE(panel.is_open());
    EXPECT_FALSE(panel.handle(press(Action::down), feedback_));
    EXPECT_TRUE(panel.handle(press(Action::l3), feedback_));
    EXPECT_TRUE(panel.handle(press(Action::back), feedback_));
    panel.close(feedback_);
    settle(panel);
    EXPECT_FALSE(panel.is_open());
}

} // namespace
