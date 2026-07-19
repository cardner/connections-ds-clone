#include "image.hpp"
#include "gfx.hpp"

#include <string.h>

namespace {

// grit GRF file header (identical layout to the RIFF container grit emits with
// -ftr). Total size is 0x24 bytes; graphics/map/palette chunks follow.
struct GrfHeader {
	u32 magicRiff;   // 'RIFF'
	u32 fileSize;
	u32 magicGrf;    // 'GRF '
	u32 magicHdr;    // 'HDR '
	u32 hdrSize;     // size of the fields below (0x10)
	u8  gfxAttr;     // bit depth (16 for our bitmaps)
	u8  mapAttr;
	u8  mmapAttr;
	u8  palAttr;
	u8  tileWidth;
	u8  tileHeight;
	u8  metaWidth;
	u8  metaHeight;
	u32 texWidth;
	u32 texHeight;
};

constexpr u32 kGfxId = 0x20584647; // 'GFX ' little-endian

// Locate the 'GFX ' chunk payload and return a pointer to its data + size.
const u8 *findGfxChunk(const u8 *base, u32 len, u32 *outSize) {
	const u8 *p = base + sizeof(GrfHeader);
	const u8 *end = base + len;
	while (p + 8 <= end) {
		u32 id, sz;
		memcpy(&id, p, 4);
		memcpy(&sz, p + 4, 4);
		const u8 *data = p + 8;
		if (data + sz > end) break;
		if (id == kGfxId) {
			*outSize = sz;
			return data;
		}
		p = data + sz;
	}
	return nullptr;
}

// Validate a GRF and return a pointer to its uncompressed 16bpp pixel array,
// plus its dimensions. Returns nullptr for unsupported buffers.
const u16 *grfPixels(const void *grf, u32 len, int *w, int *h) {
	if (!grf || len < sizeof(GrfHeader)) return nullptr;
	const GrfHeader *hdr = (const GrfHeader *)grf;
	if (hdr->gfxAttr != 16) return nullptr;
	int ww = (int)hdr->texWidth, hh = (int)hdr->texHeight;
	if (ww <= 0 || hh <= 0 || ww > 512 || hh > 512) return nullptr;
	u32 gfxSize = 0;
	const u8 *gfx = findGfxChunk((const u8 *)grf, len, &gfxSize);
	if (!gfx || gfxSize < 4) return nullptr;
	u32 comp;
	memcpy(&comp, gfx, 4);
	if ((comp & 0xFF) != 0x00) return nullptr;
	*w = ww;
	*h = hh;
	return (const u16 *)(gfx + 4);
}

} // namespace

bool img::grfSize(const void *grf, u32 len, int *w, int *h) {
	if (!grf || len < sizeof(GrfHeader)) return false;
	const GrfHeader *hdr = (const GrfHeader *)grf;
	if (w) *w = (int)hdr->texWidth;
	if (h) *h = (int)hdr->texHeight;
	return true;
}

bool img::blitGrf(bool top, int x, int y, const void *grf, u32 len) {
	if (!grf || len < sizeof(GrfHeader)) return false;
	const GrfHeader *hdr = (const GrfHeader *)grf;
	if (hdr->gfxAttr != 16) return false;
	int w = (int)hdr->texWidth, h = (int)hdr->texHeight;
	if (w <= 0 || h <= 0 || w > 512 || h > 512) return false;

	u32 gfxSize = 0;
	const u8 *gfx = findGfxChunk((const u8 *)grf, len, &gfxSize);
	if (!gfx || gfxSize < 4) return false;

	// Compression header word: (uncompressedSize << 8) | type. We only emit
	// uncompressed graphics (type 0) from the project's .grit files.
	u32 comp;
	memcpy(&comp, gfx, 4);
	if ((comp & 0xFF) != 0x00) return false;
	const u16 *pix = (const u16 *)(gfx + 4);

	for (int row = 0; row < h; row++) {
		for (int col = 0; col < w; col++) {
			u16 c = pix[row * w + col];
			if (c & 0x8000) gfx::plot(top, x + col, y + row, c);
		}
	}
	return true;
}

bool img::blitGrfCentered(bool top, int cx, int cy, const void *grf, u32 len) {
	int w = 0, h = 0;
	if (!grfSize(grf, len, &w, &h)) return false;
	return blitGrf(top, cx - w / 2, cy - h / 2, grf, len);
}

bool img::blitGrfBlend(bool top, int cx, int cy, const void *grfA, u32 lenA,
                       const void *grfB, u32 lenB, int t256) {
	int wa = 0, ha = 0, wb = 0, hb = 0;
	const u16 *a = grfPixels(grfA, lenA, &wa, &ha);
	const u16 *b = grfPixels(grfB, lenB, &wb, &hb);
	if (!a || !b || wa != wb || ha != hb) return false;
	if (t256 < 0) t256 = 0;
	if (t256 > 256) t256 = 256;

	int x0 = cx - wa / 2, y0 = cy - ha / 2;
	for (int row = 0; row < ha; row++) {
		for (int col = 0; col < wa; col++) {
			u16 ca = a[row * wa + col];
			u16 cb = b[row * wa + col];
			bool oa = (ca & 0x8000) != 0;
			bool ob = (cb & 0x8000) != 0;
			if (!oa && !ob) continue; // both transparent: leave the page
			u16 pa = oa ? ca : gfx::pal::white;
			u16 pb = ob ? cb : gfx::pal::white;
			gfx::plot(top, x0 + col, y0 + row, gfx::blend(pb, pa, t256));
		}
	}
	return true;
}
