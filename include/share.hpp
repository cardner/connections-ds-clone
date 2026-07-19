// Result sharing: emoji grid text, on-screen QR code, and share.txt on SD.
#pragma once

#include "game.hpp"

namespace share {

// Build the human-readable share text (title, id/date, emoji square grid).
void buildText(const Game &g, char *buf, int cap);

// Render the share text as a QR code on the bottom screen. Returns false if
// the text could not be encoded.
bool renderQR(const Game &g);

// Write the share text to SHARE_PATH on the SD card. Returns true on success.
bool writeFile(const Game &g);

} // namespace share
