// Minimal grit GRF loader for Connections DS.
//
// Decodes the RIFF/GRF containers produced by grit (`-ftr`) and blits their
// pixels into the existing RGB555 Bmp16 back-buffers via gfx::plot. This keeps
// the toolbar chrome and the Info / How-to-play screens on the same single
// software compositor as gameplay, so gfx::flip and lid-close sleep/wake behave
// exactly as before.
//
// Only the format the project's .grit files emit is supported: a 16bpp bitmap
// (`-gb -gB16`) with uncompressed graphics (`-gz!`). Pixels are DS ARGB16, so a
// clear alpha bit (0x8000) marks the grit transparent color and is skipped.
#pragma once

#include <nds.h>

namespace img {

// Pixel dimensions of a GRF. Returns false if @p grf is not a valid container.
bool grfSize(const void *grf, u32 len, int *w, int *h);

// Blit a GRF at (x, y) on the given screen. Transparent pixels are skipped.
// Returns false if the buffer is not a supported (uncompressed 16bpp) GRF.
bool blitGrf(bool top, int x, int y, const void *grf, u32 len);

// Blit a GRF centered on (cx, cy). Convenience for toolbar icons.
bool blitGrfCentered(bool top, int cx, int cy, const void *grf, u32 len);

// Crossfade two same-size GRFs centered on (cx, cy): @p grfA at t256=0 →
// @p grfB at t256=256. Transparent pixels are treated as the white toolbar page
// so a control's shape fades cleanly in/out. Used to animate button states.
bool blitGrfBlend(bool top, int cx, int cy, const void *grfA, u32 lenA,
                  const void *grfB, u32 lenB, int t256);

} // namespace img
