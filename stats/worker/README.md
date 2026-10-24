# The usage counter

ORION's anonymous check-ins (`src/orion/usage.hpp`) go to this Cloudflare
Worker: one when ORION opens, then one every five minutes while it stays open.
The Worker answers the dashboard with how many consoles were active in the
last 5 minutes (ORION open now), hour, 6 and 12 hours, day, week and month,
consoles per day and the versions in use.

It keeps, per console, its random ID, ORION's version and when it last checked
in, plus the days it checked in (UTC). It does not keep IP addresses or
anything else about the request, and it deletes rows older than 400 days.

**Free plan limits.** Cloudflare's free plan allows 100,000 Worker requests
and 100,000 database rows written a day. A check-in writes one row, so with
ORION open about two hours a day per console (24 check-ins), the free plan
counts about 4,000 consoles a day. Past that, the Workers Paid plan ($5 a
month) raises both limits far beyond it.

## Set it up from Cloudflare's dashboard (no tools to install)

1. Sign up at [dash.cloudflare.com](https://dash.cloudflare.com) (free).
2. **Make the database.** Storage & Databases, D1 SQL Database, **Create**.
   Name it `orion-usage`. Open it, then **Console**, paste the contents of
   [schema.sql](schema.sql) and **Execute**.
3. **Make the Worker.** Compute (Workers), **Create**, *Start with Hello
   World*. Name it `orion-usage`, **Deploy**, then **Edit code**. Replace
   everything in the editor with [index.js](index.js) and **Deploy** again.
4. **Connect the two.** On the Worker's page: Settings (or Bindings), **Add
   binding**, D1 database. Variable name `DB`, database `orion-usage`,
   **Add/Deploy**.
5. **Nightly clean-up.** Settings, Trigger Events (Triggers), **Add**, Cron
   Triggers, `17 3 * * *`.
6. Open `https://orion-usage.<your subdomain>.workers.dev/stats` in a browser.
   It should answer with zeros: `{"active":{"now":0, ...}, "ever":0, ...}`.

Then send that address (without `/stats`) to whoever builds ORION: it goes
in `kUsageUrl` (`src/orion/usage.hpp`) as `<address>/ping`, and check-ins
start with the next release.

## Or with Wrangler

With Node.js installed:

```bash
npx wrangler login
```

```bash
npx wrangler d1 create orion-usage
```

Put the printed `database_id` in [wrangler.toml](wrangler.toml), then:

```bash
npx wrangler d1 execute orion-usage --remote --file schema.sql
```

```bash
npx wrangler deploy
```

## Try it

```bash
curl -X POST https://orion-usage.<your subdomain>.workers.dev/ping -H "content-type: application/json" -d "{\"id\":\"0123456789abcdef0123456789abcdef\",\"version\":\"01.103.000\"}"
```

`/stats` then shows `"now": 1`. That test console can be removed in the
database's Console with
`DELETE FROM seen WHERE id = '0123456789abcdef0123456789abcdef'; DELETE FROM checkins WHERE id = '0123456789abcdef0123456789abcdef';`.
