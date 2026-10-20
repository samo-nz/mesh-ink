import { STATS_ENDPOINT } from "./stats-config.js?v=stats-20261020-2";

const rows = document.getElementById("usage-rows");
const status = document.getElementById("usage-status");
const GITHUB_LATEST = "https://api.github.com/repos/samo-nz/mesh-ink/releases/latest";

function cell(text) {
  const el = document.createElement("td");
  el.textContent = String(text);
  return el;
}
function versionOrder(a, b) {
  const parts = (v) => v.replace(/^v/, "").split(/[.-]/).map((n) => Number(n) || 0);
  const x = parts(a), y = parts(b);
  for (let i = 0; i < Math.max(x.length, y.length); i++) {
    if ((x[i] || 0) !== (y[i] || 0)) return (y[i] || 0) - (x[i] || 0);
  }
  return 0;
}
function onlyFirmwareAssets(release, version) {
  const names = new Set([
    `meshink-${version}-update.bin`,
    `meshink-${version}-full-wipe.bin`
  ]);
  return (release.assets || []).filter((asset) => names.has(asset.name))
    .reduce((sum, asset) => sum + (Number(asset.download_count) || 0), 0);
}
async function getJSON(url) {
  const response = await fetch(url, { cache: "no-store" });
  if (!response.ok) throw new Error(`HTTP ${response.status}`);
  return response.json();
}

export async function showUsageStatistics() {
  const webEnabled = !!STATS_ENDPOINT;
  const [github, web] = await Promise.allSettled([
    getJSON(GITHUB_LATEST),
    webEnabled ? getJSON(STATS_ENDPOINT.replace(/\/$/, "") + "/stats") : Promise.resolve(null)
  ]);

  const totals = new Map();
  // The table is deliberately scoped to the current published stable release.
  // Older events remain in D1 for historical auditing, but are not displayed.
  let currentVersion = null;
  if (github.status === "fulfilled" && github.value?.tag_name) {
    const version = String(github.value.tag_name).replace(/^v/, "");
    if (/^\d+\.\d+\.\d+$/.test(version)) {
      currentVersion = version;
      totals.set(version, {
        downloads: 0,
        flashes: 0,
        github: onlyFirmwareAssets(github.value, version)
      });
    }
  }
  if (web.status === "fulfilled" && web.value && Array.isArray(web.value.counts)) {
    for (const event of web.value.counts) {
      if (!currentVersion || event.version !== currentVersion) continue;
      if (!["download", "flash"].includes(event.event) || !["update", "wipe"].includes(event.mode)) continue;
      if (!Number.isSafeInteger(event.count) || event.count < 0) continue;
      totals.get(event.version)[event.event === "download" ? "downloads" : "flashes"] += event.count;
    }
  }
  rows.replaceChildren();
  for (const version of [...totals.keys()].sort(versionOrder)) {
    const t = totals.get(version);
    const tr = document.createElement("tr");
    tr.append(cell(version), cell(webEnabled && web.status === "fulfilled" ? t.downloads : "—"),
      cell(webEnabled && web.status === "fulfilled" ? t.flashes : "—"),
      cell(t.github === null ? "—" : t.github));
    rows.append(tr);
  }
  if (!totals.size) {
    const tr = document.createElement("tr");
    const td = cell("Counts unavailable");
    td.colSpan = 4;
    tr.append(td);
    rows.append(tr);
  }
  const notes = [];
  if (!webEnabled) notes.push("Web counters not yet enabled; a persistent stats endpoint must be configured.");
  else if (web.status !== "fulfilled") notes.push("Web counters unavailable (statistics service error).");
  else notes.push("Web counts are cumulative browser-reported events since tracking was enabled.");
  if (github.status !== "fulfilled") notes.push("GitHub release download counts unavailable.");
  status.textContent = notes.join(" ");
}
