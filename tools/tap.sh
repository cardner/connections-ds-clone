#!/bin/bash
# Tap a point on the melonDS *bottom* (touch) screen, given DS pixel coords.
# Usage: tools/tap.sh <dsX 0-255> <dsY 0-191>
# Maps DS bottom-screen pixels to absolute screen points using the live window
# position. The bottom screen sits BOTTOM_OFF points below the window top.
set -e
DSX="$1"; DSY="$2"
BOTTOM_OFF=256   # window-relative points to the top of the bottom DS screen

read -r WX WY < <(osascript -e 'tell application "System Events" to tell process "melonDS" to get value of attribute "AXPosition" of window 1' 2>/dev/null | tr ',' ' ')
if [ -z "$WX" ]; then echo "melonDS window not found"; exit 1; fi

SX=$(( WX + DSX ))
SY=$(( WY + BOTTOM_OFF + DSY ))
# Caller must ensure melonDS is frontmost first (tools/focus.sh) so the click
# isn't swallowed by window activation. Use a held press (down, wait, up) so the
# emulator samples the touch as down for several frames.
cliclick m:${SX},${SY} dd:${SX},${SY} w:120 du:${SX},${SY} >/dev/null
echo "tap DS(${DSX},${DSY}) -> screen(${SX},${SY})"
