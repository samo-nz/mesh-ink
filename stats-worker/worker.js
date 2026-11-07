// Optional Cloudflare Worker backed by D1. Deploy separately; GitHub Pages
// cannot persist counters on its own. No user/device identifiers are saved.
const SITE_ORIGIN = "https://samo-nz.github.io";
const pattern = /^\d+\.\d+\.\d+(?:-rc\.\d+)?$/;
const cors = {
  "Access-Control-Allow-Origin": SITE_ORIGIN,
  "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
  "Access-Control-Allow-Headers": "Content-Type",
  "Cache-Control": "no-store",
  "Content-Type": "application/json; charset=utf-8"
};
function reply(body, status = 200) {
  return new Response(JSON.stringify(body), { status, headers: cors });
}
export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (request.method === "OPTIONS") return new Response(null, { status: 204, headers: cors });
    if (url.pathname === "/stats" && request.method === "GET") {
      const result = await env.DB.prepare(
        "SELECT version, mode, event, count FROM flash_counts ORDER BY version DESC"
      ).all();
      return reply({ counts: result.results || [] });
    }
    if (url.pathname === "/event" && request.method === "POST") {
      if (request.headers.get("Origin") !== SITE_ORIGIN) return reply({ error: "origin" }, 403);
      const length = Number(request.headers.get("Content-Length") || 0);
      if (length > 256) return reply({ error: "too large" }, 413);
      let payload;
      try {
        const raw = await request.text();
        if (raw.length > 256) return reply({ error: "too large" }, 413);
        payload = JSON.parse(raw);
      } catch { return reply({ error: "invalid JSON" }, 400); }
      if (!payload || !pattern.test(payload.version || "") ||
          !["update", "wipe"].includes(payload.mode) ||
          !["download", "flash"].includes(payload.event))
        return reply({ error: "invalid event" }, 400);
      await env.DB.prepare(
        "INSERT INTO flash_counts (version, mode, event, count) VALUES (?, ?, ?, 1) " +
        "ON CONFLICT(version, mode, event) DO UPDATE SET count = count + 1"
      ).bind(payload.version, payload.mode, payload.event).run();
      return reply({ ok: true });
    }
    return reply({ error: "not found" }, 404);
  }
};
