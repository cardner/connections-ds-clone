/**
 * Connections DS - puzzle proxy (Cloudflare Worker)
 *
 * The Nintendo DS has no practical TLS stack, but the NYT Connections endpoint
 * is HTTPS-only. This Worker performs the HTTPS fetch at Cloudflare's edge and
 * returns a tiny, line-based payload the DS can parse without a JSON library.
 *
 * Routes:
 *   GET /connections/latest          -> today's puzzle (US Eastern date)
 *   GET /connections/YYYY-MM-DD      -> a specific date's puzzle
 *
 * IMPORTANT: to be reachable by the DS this Worker must be served over plain
 * HTTP on port 80. See worker/README.md for the required zone settings
 * (custom domain, "Always Use HTTPS" off, HSTS off, SSL "Off" rule).
 */

const NYT_BASE = "https://www.nytimes.com/svc/connections/v2";

interface NytCard {
  content: string;
  position: number;
}

interface NytCategory {
  title: string;
  cards: NytCard[];
}

interface NytPuzzle {
  status: string;
  id: number;
  print_date: string;
  editor?: string;
  categories: NytCategory[];
}

/** Today's date in US Eastern time (matches NYT's puzzle rollover). */
function easternDate(now: Date): string {
  // en-CA gives an ISO-like YYYY-MM-DD format.
  return new Intl.DateTimeFormat("en-CA", {
    timeZone: "America/New_York",
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
  }).format(now);
}

const DATE_RE = /^\d{4}-\d{2}-\d{2}$/;

/**
 * Normalize NYT text to plain ASCII the DS bitmap font can render:
 * smart quotes -> straight, dashes -> hyphen, drop other non-ASCII.
 */
function toAscii(input: string): string {
  return input
    .replace(/[\u2018\u2019\u201A\u201B]/g, "'")
    .replace(/[\u201C\u201D\u201E\u201F]/g, '"')
    .replace(/[\u2013\u2014\u2212]/g, "-")
    .replace(/\u2026/g, "...")
    .replace(/[^\x20-\x7E]/g, "")
    .replace(/\s+/g, " ")
    .trim();
}

/** Convert the NYT JSON into the compact line-based format for the DS. */
function toCompact(puzzle: NytPuzzle): string {
  const lines: string[] = [];
  lines.push(`DATE ${puzzle.print_date}`);
  lines.push(`ID ${puzzle.id}`);

  // Category 0 is the easiest (yellow) ... 3 is the hardest (purple).
  puzzle.categories.forEach((cat, index) => {
    lines.push(`CAT ${index} ${toAscii(cat.title)}`);
  });

  // Emit one CARD line per board slot, ordered by position 0..15.
  const slots: { position: number; category: number; word: string }[] = [];
  puzzle.categories.forEach((cat, index) => {
    for (const card of cat.cards) {
      slots.push({
        position: card.position,
        category: index,
        word: toAscii(card.content),
      });
    }
  });
  slots.sort((a, b) => a.position - b.position);
  for (const slot of slots) {
    lines.push(`CARD ${slot.position} ${slot.category} ${slot.word}`);
  }

  lines.push("END");
  return lines.join("\n") + "\n";
}

function textResponse(body: string, status = 200): Response {
  return new Response(body, {
    status,
    headers: {
      "content-type": "text/plain; charset=utf-8",
      // Small cache so repeated syncs on the same day are cheap.
      "cache-control": "public, max-age=300",
    },
  });
}

async function fetchPuzzle(date: string): Promise<Response> {
  const res = await fetch(`${NYT_BASE}/${date}.json`, {
    headers: { accept: "application/json" },
    cf: { cacheTtl: 300, cacheEverything: true },
  });

  if (!res.ok) {
    return textResponse(`ERR upstream ${res.status}\n`, 502);
  }

  const puzzle = (await res.json()) as NytPuzzle;
  if (puzzle.status !== "OK" || !Array.isArray(puzzle.categories) || puzzle.categories.length !== 4) {
    return textResponse("ERR malformed upstream\n", 502);
  }

  return textResponse(toCompact(puzzle));
}

export default {
  async fetch(request: Request): Promise<Response> {
    const url = new URL(request.url);

    if (request.method !== "GET") {
      return textResponse("ERR method not allowed\n", 405);
    }

    const match = url.pathname.match(/^\/connections\/([^/]+)\/?$/);
    if (!match) {
      return textResponse(
        "Connections DS proxy\n" +
          "GET /connections/latest\n" +
          "GET /connections/YYYY-MM-DD\n",
      );
    }

    const arg = match[1];
    const date = arg === "latest" ? easternDate(new Date()) : arg;

    if (!DATE_RE.test(date)) {
      return textResponse("ERR bad date\n", 400);
    }

    try {
      return await fetchPuzzle(date);
    } catch (err) {
      return textResponse(`ERR ${(err as Error).message}\n`, 502);
    }
  },
} satisfies ExportedHandler;
