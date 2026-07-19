// Build-time configuration for Connections DS.
#pragma once

// -------------------------------------------------------------------------
// Puzzle proxy (Cloudflare Worker).
//
// The DS speaks plain HTTP (no TLS). Point this at your Worker's custom
// domain, which must be configured to serve plain HTTP on port 80 (see
// worker/README.md). Do NOT include "http://" or a trailing slash.
// -------------------------------------------------------------------------
#define PROXY_HOST "connections-ds-proxy.driffterillustrations.workers.dev"
#define PROXY_PORT 80
#define PROXY_PATH_LATEST "/connections/latest"

// -------------------------------------------------------------------------
// SD card paths (created on demand under the standard homebrew data dir).
// -------------------------------------------------------------------------
#define APP_DIR      "/_nds/ConnectionsDS"
#define SAVE_PATH    APP_DIR "/progress.sav"
#define STATS_PATH   APP_DIR "/stats.sav"
#define SHARE_PATH   APP_DIR "/share.txt"

// Max HTTP response we will buffer (the compact puzzle is well under 1 KB).
#define NET_BUFFER_SIZE 4096
