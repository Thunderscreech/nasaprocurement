# nasaprocurement

A bot that watches [SAM.gov](https://sam.gov) for new NASA contract opportunities and awards,
renders each notice as a memo-style image, and posts it to Mastodon and Bluesky. A second job
follows selected contracts on [USAspending.gov](https://www.usaspending.gov) and alerts on new
task orders and spending changes.

## How it works

| Script | Schedule | What it does |
|---|---|---|
| `nasasamscraper.php` | every 20 min | Runs each search in `saved_searches` against the SAM.gov v2 API (last 10 days) and upserts results into `opportunities`. Fetches each notice's full description via `get_description.php`. |
| `procurement_check.php` | every 10 min | Takes up to 5 unposted NASA notices, renders them (`render_opportunity.php`, plus `render_addl_pages.php` for long notices), and posts to Mastodon (`nasatoot.sh`) and Bluesky (`bluesky_lib.inc`). Each platform is tracked separately, so a retry never double-posts. |
| `contract_monitor.php` | hourly | For contracts in `hv_contracts`, finds new child task orders and new modifications on USAspending and sends alerts. |
| `turnon.php` / `turnoff.php` | manual | Pause or resume posting (`control_data.active`). |

Shared code: `common.inc` (DB connection, helpers, `db_query()` prepared statements),
`config.inc` (settings loader), `sam_http.inc` (SAM.gov HTTP with timeouts, retries and health
alerts), `render_lib.inc` (pixel-accurate text wrapping and fitting for the images).

`opportunities.processed` codes: `0` queued, `1` posted everywhere, `2` skipped (old backlog),
`3` gave up after 3 failed attempts (an alert is sent).

## Requirements

- PHP 8.2+ CLI with `mysqli`, `gd` (FreeType), `curl`, `mbstring`, `iconv`
- MariaDB or MySQL
- `bash`, `curl`, `jq`, `file`, `flock` (for `nasatoot.sh` and cron)
- A [SAM.gov public API key](https://open.gsa.gov/api/get-opportunities-public-api/) (expires every 90 days)
- A Mastodon access token and/or a Bluesky app password
- Fonts (not included, see below)

## Setup

```bash
git clone https://github.com/Thunderscreech/nasaprocurement.git /opt/nasaprocurement
cd /opt/nasaprocurement
cp config.example.ini config.ini && chmod 600 config.ini   # then fill it in
mysql -e "CREATE DATABASE nasaprocurement"
mysql nasaprocurement < schema.sql
composer install          # TwitterOAuth (legacy), autoloaded by common.inc
mkdir -p rendered_opportunities sam_responses fonts
```

Then copy the fonts into `fonts/`, create the database user named in `config.ini`, and add the
lines from `crontab.example` to root's crontab. Run `php procurement_check.php test` to see what
would be posted without posting anything.

Alerts (scraper outages, expired API key, posts that gave up) go to a Discord bot script named
by `DISCORD_SCRIPT` in `config.ini`. Leave it empty to just print them to the log.

### Fonts

The renderer expects these files in `fonts/`. They are not redistributable, so they are not in
this repository:

- `Product_Sans_Regular.ttf`, `Product_Sans_Bold.ttf`
- `coolvetica_rg.ttf` ([Typodermic](https://typodermicfonts.com/coolvetica/))

Any TrueType font can be substituted by changing the file names in `render_opportunity.php` and
`render_addl_pages.php`.

## Secrets

All credentials live in `config.ini`, which is excluded by `.gitignore` (the ignore file is an
allowlist, so new files stay untracked until they're added to it on purpose). Bluesky session
tokens are cached in `bluesky_session.json` (mode 600, also ignored).

## License

Code: MIT, see [LICENSE](LICENSE).

The NASA logotype in `templates/` is a NASA insignia and is **not** covered by the MIT license.
NASA's insignia are protected under 14 CFR Part 1221 and are subject to
[NASA's media usage guidelines](https://www.nasa.gov/nasa-brand-center/images-and-media/).
This project is not affiliated with or endorsed by NASA.
