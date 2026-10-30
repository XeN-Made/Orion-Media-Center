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
        panel.set_jobs({ActivityJob{"Installing Moon Garden  ·  1.2 GB of 31.8 GB", 0.04f},
                        ActivityJob{"Copying game backgrounds…", -1.0f}});
        panel.update(kFrame);
        const std::size_t before = list_.instances().size();
        auto busy = canvas();
        panel.draw(busy);
        expect_drawn(theme.id);
        EXPECT_GT(list_.instances().size(), before) << theme.id;
    }
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
