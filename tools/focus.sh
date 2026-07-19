#!/bin/bash
# Bring melonDS frontmost and give it a throwaway click so subsequent taps land
# (the first click after activation is swallowed by window focus on macOS).
osascript -e 'tell application "melonDS" to activate' >/dev/null 2>&1
sleep 0.3
read -r WX WY < <(osascript -e 'tell application "System Events" to tell process "melonDS" to get value of attribute "AXPosition" of window 1' 2>/dev/null | tr ',' ' ')
# Click an inert spot (top screen area is non-interactive) to consume the focus click.
cliclick c:$(( WX + 128 )),$(( WY + 120 )) >/dev/null 2>&1 || true
sleep 0.1
