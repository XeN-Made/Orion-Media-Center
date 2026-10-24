-- ORION Media Center - The usage counter's tables: when each console was last seen, and on which days.
-- Copyright (C) 2026 BlackBearReloaded
-- SPDX-License-Identifier: GPL-3.0-or-later

-- One row per console: its latest check-in. Answers "active in the last 5
-- minutes, hour, ... 30 days".
CREATE TABLE IF NOT EXISTS seen (
  id TEXT PRIMARY KEY,       -- the console's random ID (32 hex characters)
  last_seen INTEGER NOT NULL, -- Unix time, seconds
  version TEXT NOT NULL       -- ORION's version at that check-in
);
CREATE INDEX IF NOT EXISTS seen_last ON seen (last_seen);

-- One row per console per day it checked in: the daily chart.
CREATE TABLE IF NOT EXISTS checkins (
  day TEXT NOT NULL, -- UTC, "2026-10-24"
  id TEXT NOT NULL,
  PRIMARY KEY (day, id)
);
