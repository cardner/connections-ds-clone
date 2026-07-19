#include "sprite.hpp"

#include <string.h>

namespace {

constexpr int MAX_SPR = 32; // per engine (OAM holds 128; we need far fewer)

struct Sprite {
	u16 *gfx;        // VRAM graphics pointer (nullptr = free slot)
	SpriteSize size;
	int allocW, allocH;
	// Desired state for this frame (flushed by update()).
	bool visible;
	bool scaled;
	int x, y;        // top-left (unscaled) or center (scaled)
	int num, den;    // scale factor for scaled sprites
	int alpha;       // 1..15
};

struct Engine {
	OamState *oam;
	Sprite s[MAX_SPR];
	int count;
};

Engine gEng[2]; // [0] = bottom/sub, [1] = top/main

Engine &eng(bool top) { return gEng[top ? 1 : 0]; }

// Smallest bitmap sprite size that covers w x h, with its pixel dimensions.
SpriteSize chooseSize(int w, int h, int *aw, int *ah) {
	struct Opt { SpriteSize sz; int w, h; };
	static const Opt opts[] = {
		{SpriteSize_8x8, 8, 8},     {SpriteSize_16x16, 16, 16},
		{SpriteSize_16x8, 16, 8},   {SpriteSize_8x16, 8, 16},
		{SpriteSize_32x8, 32, 8},   {SpriteSize_32x16, 32, 16},
		{SpriteSize_32x32, 32, 32}, {SpriteSize_16x32, 16, 32},
		{SpriteSize_8x32, 8, 32},   {SpriteSize_64x32, 64, 32},
		{SpriteSize_32x64, 32, 64}, {SpriteSize_64x64, 64, 64},
	};
	const Opt *best = nullptr;
	for (const Opt &o : opts) {
		if (o.w < w || o.h < h) continue;
		if (!best || o.w * o.h < best->w * best->h) best = &o;
	}
	if (!best) best = &opts[11]; // 64x64 fallback (largest)
	*aw = best->w;
	*ah = best->h;
	return best->sz;
}

} // namespace

void spr::init() {
	// Sprite VRAM: bank B for the main engine (top), bank D for the sub (bottom).
	vramSetBankB(VRAM_B_MAIN_SPRITE);
	vramSetBankD(VRAM_D_SUB_SPRITE);

	oamInit(&oamMain, SpriteMapping_Bmp_1D_128, false);
	oamInit(&oamSub, SpriteMapping_Bmp_1D_128, false);

	// Enable OBJ alpha blending against the (single) BG3 bitmap layer + backdrop
	// on both engines. For bitmap sprites the per-object alpha (OAM, 0-15) is the
	// blend coefficient, so this is what makes crossfades / fades work at all.
	u16 bld = BLEND_ALPHA | BLEND_SRC_SPRITE | BLEND_DST_BG3 | BLEND_DST_BACKDROP;
	REG_BLDCNT = bld;
	REG_BLDCNT_SUB = bld;

	gEng[0].oam = &oamSub;
	gEng[1].oam = &oamMain;
	for (int e = 0; e < 2; e++) {
		gEng[e].count = 0;
		memset(gEng[e].s, 0, sizeof(gEng[e].s));
	}
	hideAll();
}

void spr::reset(bool top) {
	Engine &en = eng(top);
	for (int i = 0; i < en.count; i++) {
		if (en.s[i].gfx) oamFreeGfx(en.oam, en.s[i].gfx);
		en.s[i].gfx = nullptr;
		en.s[i].visible = false;
	}
	en.count = 0;
	// Hide any stale hardware entries; flushed on the next spr::update().
	oamClear(en.oam, 0, 0);
}

int spr::create(bool top, int w, int h, u16 **gfx, int *stride, int *allocW, int *allocH) {
	Engine &en = eng(top);
	if (en.count >= MAX_SPR) return -1;
	int aw = 0, ah = 0;
	SpriteSize sz = chooseSize(w, h, &aw, &ah);
	u16 *g = oamAllocateGfx(en.oam, sz, SpriteColorFormat_Bmp);
	if (!g) return -1;
	memset(g, 0, (u32)aw * ah * sizeof(u16)); // clear to transparent

	int id = en.count++;
	Sprite &sp = en.s[id];
	sp.gfx = g;
	sp.size = sz;
	sp.allocW = aw;
	sp.allocH = ah;
	sp.visible = false;
	sp.scaled = false;
	sp.x = sp.y = 0;
	sp.num = sp.den = 1;
	sp.alpha = 15;

	if (gfx) *gfx = g;
	if (stride) *stride = aw;
	if (allocW) *allocW = aw;
	if (allocH) *allocH = ah;
	return id;
}

void spr::place(bool top, int handle, int x, int y, int alpha) {
	Engine &en = eng(top);
	if (handle < 0 || handle >= en.count || !en.s[handle].gfx) return;
	Sprite &sp = en.s[handle];
	sp.visible = true;
	sp.scaled = false;
	sp.x = x;
	sp.y = y;
	sp.alpha = alpha < 1 ? 1 : (alpha > 15 ? 15 : alpha);
}

void spr::placeScaled(bool top, int handle, int cx, int cy, int num, int den, int alpha) {
	Engine &en = eng(top);
	if (handle < 0 || handle >= en.count || !en.s[handle].gfx) return;
	Sprite &sp = en.s[handle];
	sp.visible = true;
	sp.scaled = true;
	sp.x = cx;
	sp.y = cy;
	sp.num = num;
	sp.den = den;
	sp.alpha = alpha < 1 ? 1 : (alpha > 15 ? 15 : alpha);
}

void spr::hide(bool top, int handle) {
	Engine &en = eng(top);
	if (handle < 0 || handle >= en.count) return;
	en.s[handle].visible = false;
}

void spr::update() {
	for (int e = 0; e < 2; e++) {
		Engine &en = gEng[e];
		int affine = 0;
		for (int i = 0; i < en.count; i++) {
			Sprite &sp = en.s[i];
			if (!sp.gfx || !sp.visible) {
				oamSet(en.oam, i, 0, 192, 0, sp.alpha, sp.size,
				       SpriteColorFormat_Bmp, sp.gfx, -1, false, true,
				       false, false, false);
				continue;
			}
			if (sp.scaled) {
				int rot = affine++;
				// Inverse scale in 1.8 fixed: 256 = 1.0. sizeDouble keeps the
				// enlarged sprite from clipping to its base box.
				int inv = sp.den * 256 / (sp.num > 0 ? sp.num : 1);
				oamRotateScale(en.oam, rot, 0, inv, inv);
				int boxW = sp.allocW * 2, boxH = sp.allocH * 2; // doubled box
				int x = sp.x - boxW / 2;
				int y = sp.y - boxH / 2;
				oamSet(en.oam, i, x, y, 0, sp.alpha, sp.size,
				       SpriteColorFormat_Bmp, sp.gfx, rot, true, false,
				       false, false, false);
			} else {
				oamSet(en.oam, i, sp.x, sp.y, 0, sp.alpha, sp.size,
				       SpriteColorFormat_Bmp, sp.gfx, -1, false, false,
				       false, false, false);
			}
		}
	}
	oamUpdate(&oamMain);
	oamUpdate(&oamSub);
}

void spr::hideAll() {
	for (int e = 0; e < 2; e++) {
		Engine &en = gEng[e];
		for (int i = 0; i < en.count; i++) en.s[i].visible = false;
	}
	oamClear(&oamMain, 0, 0);
	oamClear(&oamSub, 0, 0);
	oamUpdate(&oamMain);
	oamUpdate(&oamSub);
}
