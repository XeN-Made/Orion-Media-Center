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
        ActivityJob waiting;
        waiting.text = "Install Moon Garden Soundtrack.pkg";
        waiting.state = hui::orion::ActivityState::waiting;
        ActivityJob done;
        done.text = "Installed Harbour Lights";
        done.state = hui::orion::ActivityState::done;
        ActivityJob failed;
        failed.text = "Could not install Paper Kites";
        failed.state = hui::orion::ActivityState::failed;
        panel.set_jobs({install, art, waiting, done, failed});
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

// A queue longer than the sheet scrolls, refusing at its ends; Square asks
// for the finished jobs to go, and refuses when there are none.
TEST_F(OrionActivity, ScrollsAQueueAndClearsWhatEnded)
{
    using hui::orion::ActivityState;
    ActivityPanel panel;
    panel.open(feedback_);
    std::vector<ActivityJob> jobs;
    for (int i = 0; i < 20; ++i)
    {
        ActivityJob job;
        job.text = "Install add-on " + std::to_string(i);
        job.state = ActivityState::waiting;
        jobs.push_back(job);
    }
    panel.set_jobs(jobs);
    settle(panel);
    auto first = canvas();
    panel.draw(first); // lays the rows out

    feedback_.cues.clear();
    EXPECT_FALSE(panel.handle(press(Action::west), feedback_));
    EXPECT_FALSE(panel.take_clear());
    EXPECT_TRUE(asked(hui::audio::Cue::error));

    EXPECT_FALSE(panel.handle(nav(hui::Direction::up), feedback_));
    EXPECT_EQ(panel.first_shown(), 0);
    for (int i = 0; i < 40; ++i)
        panel.handle(nav(hui::Direction::down), feedback_);
    const int last = panel.first_shown();
    EXPECT_GT(last, 0);
    EXPECT_LT(last, 20);
    panel.handle(nav(hui::Direction::down), feedback_);
    EXPECT_EQ(panel.first_shown(), last);

    jobs[19].state = ActivityState::done;
    panel.set_jobs(jobs);
    EXPECT_FALSE(panel.handle(press(Action::west), feedback_));
    EXPECT_TRUE(panel.take_clear());
    EXPECT_FALSE(panel.take_clear());

    // Fewer jobs: the top row comes back into the list.
    jobs.resize(3);
    panel.set_jobs(jobs);
    panel.update(kFrame);
    EXPECT_EQ(panel.first_shown(), 0);
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
