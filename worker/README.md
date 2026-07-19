# Connections DS puzzle proxy (Cloudflare Worker)

This Worker fetches the daily NYT Connections puzzle over HTTPS and returns a
tiny, line-based payload that the Nintendo DS can parse without a JSON library
or a TLS stack.

## Why a proxy?

- The NYT endpoint (`https://www.nytimes.com/svc/connections/v2/YYYY-MM-DD.json`)
  is **HTTPS-only**.
- The DS has no practical TLS stack. The Worker does the HTTPS call at the edge
  and serves the DS over **plain HTTP on port 80**.

## Endpoints

```
GET /connections/latest        # today's puzzle (US Eastern)
GET /connections/YYYY-MM-DD     # a specific date
```

### Response format

```
DATE 2024-12-25
ID 595
CAT 0 CELESTIAL OBJECTS
CAT 1 ARCHERS
CAT 2 FEMALE ANIMALS
CAT 3 "S.N.L." CAST MEMBERS
CARD 0 2 QUEEN
CARD 1 0 STAR
...                            # 16 CARD lines, ordered by board position 0..15
END
```

- `CAT <index> <title>` - category index 0 (easiest/yellow) to 3 (hardest/purple).
- `CARD <position> <category> <word>` - `word` runs to end of line and may
  contain spaces (e.g. `ROBIN HOOD`). Text is normalized to ASCII.

## Develop and deploy

```sh
npm install
npm run dev        # wrangler dev, then: curl http://localhost:8787/connections/latest
npm run deploy     # wrangler deploy
```

## Serving over plain HTTP (required for the DS)

`*.workers.dev` is HTTPS-only, so the DS cannot use it. You must run the Worker
on a **proxied (orange-cloud) custom domain** and disable HTTPS enforcement:

1. Add a route in [wrangler.jsonc](wrangler.jsonc) (uncomment the `routes`
   block) for e.g. `connections-ds.example.com/connections/*`, or add a Custom
   Domain for the Worker in the dashboard. Ensure the hostname has a proxied DNS
   record.
2. In the Cloudflare dashboard for that zone, under **SSL/TLS -> Edge
   Certificates**:
   - Turn **Always Use HTTPS** = Off.
   - Turn **HSTS** = Off (and make sure no prior HSTS header is still cached).
3. Add a **Configuration Rule** (or legacy Page Rule) for
   `connections-ds.example.com/*` that sets **SSL = Off**, so the edge answers
   plain HTTP on port 80 instead of redirecting to HTTPS.
4. Verify (note `http://`, no redirect):

   ```sh
   curl -v http://connections-ds.example.com/connections/latest
   ```

5. Point the ROM at it by editing [`../include/config.hpp`](../include/config.hpp)
   and rebuilding (`make`). Set your hostname (no scheme, no trailing slash):

   ```c
   #define PROXY_HOST "connections-ds.example.com"
   #define PROXY_PORT 80
   ```

## Notes

- NYT puzzle content is copyrighted; this is for personal use.
- A short edge cache (5 min) keeps repeated same-day syncs cheap.
