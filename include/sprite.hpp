// Hardware OAM sprite overlay for Connections DS animations.
//
// The whole UI is a software RGB555 compositor (see gfx.cpp): the background
// holds the settled state and is only repainted on input. Transitions are done
// the DS-native way instead of re-rasterizing pixels every frame: the animating
// element is captured once into a hardware *bitmap sprite* (direct RGB555, same
// format as our back-buffers) and then moved / scaled / alpha-blended purely via
// OAM register writes each frame. The hardware composites it during scanout, so
// animation costs a few register writes rather than a full software repaint.
//
// Engine map: top screen = main engine (VRAM_B sprites), bottom = sub engine
// (VRAM_D sprites). `top` selects the engine, matching gfx.hpp.
#pragma once

#include <nds.h>

namespace spr {

// Set up OAM on both engines and map sprite VRAM. Call once, after gfx::init().
void init();

// Free every sprite handle + graphics on an engine (call before building the
// sprite set for a new transition). Cheap; safe to call every frame if idle.
void reset(bool top);

// Allocate a bitmap sprite big enough for @p w x @p h. Returns a handle (>=0) or
// -1 on failure. *gfx points at a contiguous @p *stride-by-*allocH ARGB16 buffer
// to fill (write 0x8000|rgb555 for opaque pixels, 0x0000 for transparent). The
// allocated hardware size may exceed the request; extra pixels should be left 0.
int create(bool top, int w, int h, u16 **gfx, int *stride, int *allocW, int *allocH);

// Per-frame placement (values apply on the next spr::update). Alpha is 1..15
// (0 hides). place() is 1:1; placeScaled() scales about the sprite center by
// num/den using an affine matrix.
void place(bool top, int handle, int x, int y, int alpha);
void placeScaled(bool top, int handle, int cx, int cy, int num, int den, int alpha);

// Hide a single sprite for this frame.
void hide(bool top, int handle);

// Flush the shadow OAM to hardware (call once per frame, after placements).
// Any handle not placed this frame is hidden.
void update();

// Hide all sprites on both engines and flush immediately.
void hideAll();

} // namespace spr
