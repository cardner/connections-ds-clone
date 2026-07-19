#!/bin/bash
# Capture just the melonDS window to a PNG for verifying rendering/animation.
# Usage: tools/shot.sh [out.png]
# Requires Screen Recording + Accessibility permission for the calling app.
set -e
OUT="${1:-/tmp/melon.png}"

read -r X Y W H < <(osascript -e 'tell application "System Events" to tell process "melonDS" to get {value of attribute "AXPosition" of window 1, value of attribute "AXSize" of window 1}' 2>/dev/null | tr ',' ' ' | awk '{print $1, $2, $3, $4}')

if [ -z "$X" ]; then
  # Fallback to a plain full-screen grab if the window can't be located.
  screencapture -x -o "$OUT"
else
  screencapture -x -o -R"${X},${Y},${W},${H}" "$OUT"
fi
echo "$OUT (${X},${Y} ${W}x${H})"
