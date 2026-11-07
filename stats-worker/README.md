# Optional MeshInk web-flasher counters

GitHub Pages cannot store shared counters, so this small Cloudflare Worker plus
D1 database is an **optional** first-party aggregation endpoint. The Serial
Console table also displays public GitHub Release asset-download totals without
this service. Firmware remains unchanged.

## Setup

1. Create a Cloudflare Worker with the source in `worker.js`.
2. Create a D1 database and run `schema.sql` on it.
3. Bind the D1 database to the Worker as **DB**.
4. Deploy the Worker at your own HTTPS URL.
5. Put that URL (without a trailing slash) into
   `web/stats-config.js` as `STATS_ENDPOINT`; commit the change to `main`
   to redeploy GitHub Pages.

The flasher sends a `download` event only after the release firmware has been
downloaded and its SHA-256 verified; it sends a `flash` event only after
`writeFlash()` succeeds. Update/full-wipe modes are counted separately. Custom
local BIN files are excluded because their version cannot be verified.

The console reads the aggregated counts from `GET /stats`. No node number,
serial output, firmware contents, filename, persistent browser identifier or
message is included in event bodies. The hosting provider may process network
metadata such as IP addresses as part of normal HTTP operations.

**Limitations:** Events are best-effort. Browser/network interruptions, privacy
filters and offline use can undercount. Anyone able to forge requests may inflate
browser-reported counts; neither counts nor GitHub asset downloads prove distinct
devices or successful boots. Origin checks deter ordinary cross-site browser
submissions but do not authenticate callers. For abuse resistance add provider
rate-limiting / bot controls before enabling public reporting. The table labels
the counts as reported events, not unique installs.

No historical web-flasher counters can be reconstructed before activation.
