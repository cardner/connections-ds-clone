---
name: figma-to-ds-assets
description: >-
  Converts Figma icons/glyphs or local BMP image assets into Connections DS
  rendering — procedural gfx draws for line icons, or 8-bit BMP embeds for
  logos/illustrations. Use when the user pastes a Figma URL/node, points at a
  local .bmp, asks to port toolbar/UI icons from design or bitmap files,
  convert BMP icons into procedural draw code, or choose between procedural
  drawing and BMP embeds for in-game graphics.
---

# Design / BMP → Connections DS assets

This app draws UI on dual RGB555 bitmaps via `gfx::` (no grit sprites, no SVG
runtime). Prefer **procedural** icons; keep a **BMP embed** only for filled
illustrations / logos that cannot be described in a few primitives.

Sources this skill accepts:

1. **Figma** node / URL
2. **Local BMP** files (repo `data/`, project root, or a path the user gives)

## Decision: procedural vs keep-as-BMP

| Asset | Path |
|-------|------|
| Toolbar / control glyphs (info, sync, shuffle, ✕, ✓) | Procedural |
| Line icons needing tint (enabled / disabled / white-on-fill) | Procedural |
| Local BMP that is clearly a simple glyph (strokes, rings, X, check) | Procedural (replace raster) |
| Logo, wordmark art, complex filled illustration | Keep/embed BMP in `data/` |
| ROM banner | Root `icon.bmp` only (Makefile syncs `data/logo.bmp`) |

**Rule of thumb:** ≤5 primitives (ring, stroke, triangle, round rect, disk) →
procedural. Otherwise → 8-bit BMP embed.

Never paste SVG path `d=` into C++. Never add an SVG parser.

## Scale

- **Figma:** artboards are often **1024px wide → DS 256px** → `ds = figma * 0.25`.
- **Local BMP:** measure the glyph’s ink bounds in the file; target toolbar
  glyph size already used in-app (~14–16px / `r` ≈ 5–8), not the raw pixel
  width if the asset is exported large.

Confirm against the artboard or BMP. Existing toolbar constants assume the
Figma scale (`BTN_PITCH`, `FRAME_W/H`, glyph radii in `source/ui_bottom.cpp`).

- Layout sizes / radii: integers after scale
- Stroke pens: keep floats (`~1.35f`–`1.5f`) for AA via `strokeLine` / `plotDisk`

## Workflow (procedural icons)

Copy and track:

```text
- [ ] Load source (Figma node OR local BMP)
- [ ] Map colors → gfx::pal
- [ ] Write geometry recipe (≤5 primitives)
- [ ] Implement draw*Icon(cx, cy, r, color)
- [ ] Wire into bare/framed toolbar helpers if needed
- [ ] If replacing a BMP embed, remove unused data/ include once verified
- [ ] Verify vs source (Figma screenshot or original BMP) on emulator
```

### 1a. Read Figma

With a Figma URL or selected node, use the Figma MCP:

1. Load `/figma-design-to-code` (or project Figma skills) before design reads.
2. Call `get_design_context` + `get_screenshot` on the **icon / strip node**.
3. Optionally `get_variable_defs` for hex tokens.

**Ignore** React/Tailwind from design-context. Extract only: bbox, stroke
weight, corner radius, arc/line geometry, and hex colors.

### 1b. Read local BMP

When the user points at a `.bmp` (or one exists under `data/` / project root):

1. **Read the image** with the Read tool (supports viewing BMP/PNG visually).
2. Note width/height, transparent treatment (pure white in this app), and
   whether the mark is stroke-like or a filled blob.
3. Mentally (or with a quick overlay) find center, outer radius, stroke
   thickness in source pixels, and major segments (lines, arcs, triangles).
4. Decide:
   - **Convert to procedural** if it fits the ≤5-primitive rule (typical for
     toolbar icons the user exported from Figma/other tools as BMP).
   - **Keep as embed** if it is a logo or dense art (see “Keep as BMP embed”).

Do not re-embed a glyph BMP “as-is” when the user asked for procedural UI —
trace it into `draw*Icon` instead.

Tracing tips for BMPs:

- Treat white / near-white as empty; ink is the dark (or colored) pixels.
- Estimate pen radius as ~half the stroke thickness in DS pixels after scale.
- Prefer geometric ideals (true circle, 45° X, symmetric check) over
  pixel-perfect jaggy edges from a small raster.
- If multiple sizes of the same icon exist, trace the clearest (usually largest)
  then scale `r` down for the toolbar.

### 2. Map to project primitives

| Source shape | Code |
|--------------|------|
| Straight stroke | `gfx::strokeLine(screen, x0, y0, x1, y1, penRadius, c)` |
| Circle fill | `gfx::fillCircle` / `gfx::plotDisk` |
| Ring / donut | `gfx::fillRing(screen, cx, cy, outerR, innerR, c)` |
| Triangle / arrowhead | `gfx::fillTriangle` |
| Rounded chip | `gfx::fillRoundRect` / `gfx::roundPanel` |
| Hex fill/stroke | `gfx::pal::*` or `gfx::color(r,g,b)` |

Key files:

- Primitives / palette: `include/gfx.hpp`, `source/gfx.cpp`
- Toolbar icons: `source/ui_bottom.cpp` (`drawInfoIcon`, `drawRefreshIcon`, …)
- Layout / hit targets: `include/ui.hpp`, `buttonRect`

### 3. Recipe then code

Before coding, write a one-line recipe:

```text
Info: ring(r=8, stroke≈2) + top dot + vertical bar
Sync: two ~130° arcs + two arrowhead triangles
```

Implement:

```cpp
static void drawFooIcon(bool bottom, int cx, int cy, int r, u16 c);
```

Requirements:

- **Center + radius + color** — never bake enabled/disabled colors into geometry
- Match existing helpers in `ui_bottom.cpp` (bare vs framed chrome stays in
  `drawBareTool` / `drawFramedTool`)
- Pass `u16 c` from callers (`toolIcon`, `disText`, `white`, etc.)

### 4. Colors

Reuse palette entries in `gfx::pal` when hexes match. Common toolbar tokens:

- `#1C1C1C` → `toolIcon`
- `#787878` → `disText` / `border`
- `#E4E4E4` → `disabled`
- Submit active green → `submit`
- Focus ring → `focus`

From a BMP, sample the dominant ink color and map to the closest `pal` entry
(usually `toolIcon`); do not introduce a one-off color unless it is thematic.

Add a new `pal` constant only when the hex is reused or part of the theme.

### 5. Verify

1. Keep the Figma screenshot **or** the source BMP as ground truth.
2. Build/run on emulator; compare at DS resolution.
3. Tune pen radius, arc degrees, and triangle tips first — usual mismatch
   points. Do not “fix” a failed trace by re-embedding the BMP unless the
   asset fails the ≤5-primitive rule.

## Workflow (keep as BMP embed)

Use when procedural is a bad fit (logo, dense art), including local BMPs the
user wants shipped as raster.

### Format constraints

`gfx::drawBmp` / `drawBmpSized` accept **8-bit Windows BMP, BI_RGB only**.

- Pure **white** pixels are skipped (transparent).
- Nearest-neighbor scale only.
- No runtime tint — export separate variants if you need gray/white versions,
  or prefer procedural for tintable glyphs.

### Embed pattern

1. Place file under `data/` (e.g. `data/my_icon.bmp`).
2. Makefile `bin2o` produces `my_icon_bmp.h` with `my_icon_bmp` / `my_icon_bmp_size`.
3. Include and draw:

```cpp
#include "my_icon_bmp.h"
gfx::drawBmpSized(false, x, y, my_icon_bmp, my_icon_bmp_size, destW, destH);
```

Reference: logo draw in `source/ui_top.cpp`.

**Do not** put in-game UI icons in root `icon.bmp` — that is the ROM banner
(and source for `data/logo.bmp`).

### Export / prep tips

- Flatten to a square/rect at final or 2× DS pixels, then quantize to 8-bit.
- Use pure white `(255,255,255)` for transparent areas.
- Prefer indexed color with a simple palette; avoid 24/32-bit BMP.
- If converting a 24-bit export for embed, quantize first; if converting that
  same file to procedural, skip quantization and trace geometry instead.

## Anti-patterns

- SVG in the ROM or an SVG→path interpreter
- Grit/PNG sprite sheets for toolbar glyphs (GRAPHICS is unused on purpose)
- Hard-coded icon colors that break disabled / active chip states
- Measuring at Figma 1:1 without applying the DS scale
- Leaving a simple glyph as an embedded BMP when the user asked for procedural
- Pixel-tracing jaggy BMP edges instead of ideal geometry

## When implementing

Follow existing style in `ui_bottom.cpp` / `ui_top.cpp`. Touch only the draw
path needed for the asset. After changes, rebuild the NDS target the project
already uses (`make`) when the user expects a visual check.
