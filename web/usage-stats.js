import { STATS_ENDPOINT } from "./stats-config.js?v=stats-20261020-2";

const rows = document.getElementById("usage-rows");
const status = document.getElementById("usage-status");
const GITHUB_RELEASES = "https://api.github.com/repos/samo-nz/mesh-ink/releases?per_page=100";
const FIRST_TRACKED = [3, 1, 0];

function eligible(version) {
  if (!/^\d+\.\d+\.\d+$/.test(version)) return false;
  const n = version.split(".").map(Number);
  for (let i = 0; i < 3; i++) {
    if (n[i] !== FIRST_TRACKED[i]) return n[i] > FIRST_TRACKED[i];
  }
  return true;
}
function compareVersions(a, b) {
  const x = a.split(".").map(Number), y = b.split(".").map(Number);
  for (let i = 0; i < 3; i++) if (x[i] !== y[i]) return y[i] - x[i];
  return 0;
}
function cell(value) {
  const td = document.createElement("td");
  td.textContent = String(value);
  return td;
}
async function getJSON(url) {
  const response = await fetch(url, { cache: "no-store" });
  if (!response.ok) throw new Error(`HTTP ${response.status}`);
  return response.json();
}
function emptyTotals() {
  return { updateDownloads: 0, wipeDownloads: 0, updateFlashes: 0, wipeFlashes: 0,
    githubUpdate: 0, githubWipe: 0 };
}

export async function showUsageStatistics() {
  const webEnabled = Boolean(STATS_ENDPOINT);
  const [github, web] = await Promise.allSettled([
    getJSON(GITHUB_RELEASES),
    webEnabled ? getJSON(STATS_ENDPOINT.replace(/\/$/, "") + "/stats") : Promise.resolve(null)
  ]);

  const totals = new Map();
  if (github.status === "fulfilled" && Array.isArray(github.value)) {
    for (const release of github.value) {
      if (release.draft || release.prerelease) continue;
      const version = String(release.tag_name || "").replace(/^v/, "");
      if (!eligible(version)) continue;
      if (!totals.has(version)) totals.set(version, emptyTotals());
      const t = totals.get(version);
      for (const asset of release.assets || []) {
        if (asset.name === `meshink-${version}-update.bin`) t.githubUpdate += Number(asset.download_count) || 0;
        if (asset.name === `meshink-${version}-full-wipe.bin`) t.githubWipe += Number(asset.download_count) || 0;
      }
    }
  }
  if (web.status === "fulfilled" && Array.isArray(web.value?.counts)) {
    for (const event of web.value.counts) {
      if (!eligible(event.version || "")) continue;
      if (!["download", "flash"].includes(event.event) || !["update", "wipe"].includes(event.mode)) continue;
      if (!Number.isSafeInteger(event.count) || event.count < 0) continue;
      // Only display published stable releases; do not expose arbitrary reported version strings.
      if (!totals.has(event.version)) continue;
      const t = totals.get(event.version);
      const key = event.mode + (event.event === "download" ? "Downloads" : "Flashes");
      t[key] += event.count;
    }
  }

  rows.replaceChildren();
  for (const version of [...totals.keys()].sort(compareVersions)) {
    const t = totals.get(version);
    const webValue = (value) => webEnabled && web.status === "fulfilled" ? value : "—";
    const tr = document.createElement("tr");
    tr.append(cell(version), cell(webValue(t.updateDownloads)), cell(webValue(t.wipeDownloads)),
      cell(webValue(t.updateFlashes)), cell(webValue(t.wipeFlashes)),
      cell(t.githubUpdate), cell(t.githubWipe));
    rows.append(tr);
  }
  if (!totals.size) {
    const tr = document.createElement("tr");
    const td = cell("Release counts unavailable");
    td.colSpan = 7;
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
