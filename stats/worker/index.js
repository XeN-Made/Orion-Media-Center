// ORION Media Center - The usage counter: how many consoles have used ORION, and how recently.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

// A Cloudflare Worker with one D1 database bound as DB (schema.sql).
//
//   POST /ping   {"id": "<32 hex>", "version": "01.104.000"}  from ORION, every
//                5 minutes while it is open
//   GET  /stats  the totals, for the dashboard: no IDs, only counts
//
// What is kept: each console's random ID, ORION's version, when it last
// checked in and the days it did (UTC). Not kept: the IP address, headers or
// anything else about the request. Rows older than 400 days are deleted every
// night.

const ID = /^[0-9a-f]{32}$/;
const VERSION = /^\d{2}\.\d{3}\.\d{3}$/;
const KEEP_DAYS = 400;
// ORION checks in every 5 minutes, so "now" is the last 5 minutes and a bit.
const WINDOWS = [
  ["now", 6 * 60],
  ["hour", 3600],
  ["hours_6", 6 * 3600],
  ["hours_12", 12 * 3600],
  ["day", 86400],
  ["week", 7 * 86400],
  ["month", 30 * 86400],
];

const json = (value, status = 200) =>
  new Response(JSON.stringify(value), {
    status,
    headers: {
      "content-type": "application/json",
      // The dashboard is a local page: it may read the totals.
      "access-control-allow-origin": "*",
      "cache-control": "no-store",
    },
  });

const seconds = () => Math.floor(Date.now() / 1000);
const dayOf = (time) => new Date(time * 1000).toISOString().slice(0, 10);

async function ping(request, env) {
  if (Number(request.headers.get("content-length") || 0) > 1024)
    return json({ error: "too large" }, 413);
  let body;
  try {
    body = await request.json();
  } catch {
    return json({ error: "not JSON" }, 400);
  }
  if (!body || !ID.test(body.id) || !VERSION.test(body.version))
    return json({ error: "expected an id and a version" }, 400);
  const now = seconds();
  await env.DB.batch([
    env.DB.prepare(
      `INSERT INTO seen (id, last_seen, version) VALUES (?1, ?2, ?3)
       ON CONFLICT (id) DO UPDATE SET last_seen = excluded.last_seen, version = excluded.version`)
      .bind(body.id, now, body.version),
    // A second check-in the same day writes nothing.
    env.DB.prepare("INSERT OR IGNORE INTO checkins (day, id) VALUES (?1, ?2)").bind(dayOf(now), body.id),
  ]);
  return new Response(null, { status: 204 });
}

async function stats(env) {
  const now = seconds();
  const counts = await env.DB.prepare(
    `SELECT ${WINDOWS.map(([name], i) => `SUM(last_seen >= ?${i + 1}) AS ${name}`).join(", ")},
            COUNT(*) AS ever FROM seen`)
    .bind(...WINDOWS.map(([, span]) => now - span))
    .first();
  const days = await env.DB.prepare(
    "SELECT day, COUNT(*) AS consoles FROM checkins WHERE day >= ?1 GROUP BY day ORDER BY day")
    .bind(dayOf(now - 89 * 86400))
    .all();
  const versions = await env.DB.prepare(
    "SELECT version, COUNT(*) AS consoles FROM seen WHERE last_seen >= ?1 GROUP BY version ORDER BY version DESC")
    .bind(now - 7 * 86400)
    .all();
  const active = {};
  for (const [name] of WINDOWS) active[name] = counts[name] || 0;
  return json({
    updated: new Date(now * 1000).toISOString(),
    active,
    ever: counts.ever || 0,
    days: days.results,
    versions: versions.results,
  });
}

export default {
  async fetch(request, env) {
    const { pathname } = new URL(request.url);
    if (request.method === "POST" && pathname === "/ping") return ping(request, env);
    if (request.method === "GET" && pathname === "/stats") return stats(env);
    return json({ error: "not found" }, 404);
  },

  async scheduled(event, env) {
    const before = seconds() - KEEP_DAYS * 86400;
    await env.DB.batch([
      env.DB.prepare("DELETE FROM seen WHERE last_seen < ?1").bind(before),
      env.DB.prepare("DELETE FROM checkins WHERE day < ?1").bind(dayOf(before)),
    ]);
  },
};
