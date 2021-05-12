#include "ext-yoomp.h"

#include <math.h>
#include <assert.h>

#include "sdl/video_gl-common.h"
#include "sdl/video_gl-ext.h"

#include "antic.h"
#include "colours.h"
#include "memory.h"
#include "ui.h"

typedef struct rr_object {
	gl_texture normal;
	gl_texture mirror;
	// explosion textures (from memory 1c, 34, 4c resp)
	gl_texture explosions[3];
} rr_object;

// Object IDs:
// 0, 1 - nothing
// 2..6 - explosions
// 7..15 - 9 actual objects
#define OBJECTS_OFFSET 7
#define NUM_OBJECTS 9
rr_object objects[NUM_OBJECTS];

#define RR_COLOR_LOS 0xBB30
#define RR_COLOR_DATA 0xB700
#define RR_GFX_LOS 0xBB40
#define RR_GFX_DATA 0xB900
#define RR_HEIGHTS 0xBB60
#define RR_WIDTHS 0x0521

#define Y_ADJUSTMENT 0x5D  // adjustment in the Y positions
#define Y_BLANKS 20   // empty lines on top of the screen

static gl_texture gen_texture(int height, int color_ptr, int gfx_ptr)
{
	static const int width = 8;

	gl_texture t = gl_texture_new(width, height);

	for (int y = 0; y < height; y++) {
		// We're upside down
		int idx = height - y - 1;
		// Take color, we're upside down
		byte color = MEMORY_mem[color_ptr + idx];
		byte colR = Colours_GetR(color);
		byte colG = Colours_GetG(color);
		byte colB = Colours_GetB(color);
		// Take bitmap
		byte gfx_byte = MEMORY_mem[gfx_ptr + idx];
		for (int x = 0; x < width; x++) {
			int mul = (gfx_byte & (1 << (7 - x))) != 0;
			t.data[4 * (y * width + x) + 0] = mul * colR;
			t.data[4 * (y * width + x) + 1] = mul * colG;
			t.data[4 * (y * width + x) + 2] = mul * colB;
			t.data[4 * (y * width + x) + 3] = mul * 255;
		}
	}

	gl_texture_finalize(&t);
	return t;
}

static rr_object gen_object(int object_id)
{
	int height = 1 + MEMORY_mem[RR_HEIGHTS + object_id];

	int colors = RR_COLOR_DATA | MEMORY_mem[RR_COLOR_LOS + (object_id % 16)];

	int gfx_normal = RR_GFX_DATA | MEMORY_mem[RR_GFX_LOS + object_id];
	int gfx_mirror = RR_GFX_DATA | MEMORY_mem[RR_GFX_LOS + object_id + 0x10];

	printf("Constructing object #%02x: height: %02x colors: %04x  normal: %04x  mirror: %04x\n",
			object_id, height, colors, gfx_normal, gfx_mirror);

	rr_object o;
	o.normal = gen_texture(height, colors, gfx_normal);
	o.mirror = gen_texture(height, colors, gfx_mirror);
	o.explosions[0] = gen_texture(height, colors, 0xb91c);
	o.explosions[1] = gen_texture(height, colors, 0xb934);
	o.explosions[2] = gen_texture(height, colors, 0xb94c);

	return o;
}

static void init()
{
	for (int i = 0; i < NUM_OBJECTS; i++) {
		objects[i] = gen_object(OBJECTS_OFFSET + i);
	}
}

static int river_raid_init(void)
{
	// Some memory fingerprint
	int address = 0xb55c;
	byte fingerprint[] = {0xA4, 0x4D, 0xA2, 0x5D, 0xD0, 0x03};

	if (memcmp(MEMORY_mem + address, fingerprint, sizeof(fingerprint))) {
		// No match
		return 0;
	}

	// Match
	return 1;
}

static void post_gl_frame()
{
	gl.Disable(GL_DEPTH_TEST);
	gl.Color4f(1.0f, 1.0f, 1.0f, 1.0f);
	gl.Enable(GL_BLEND);
	gl.BlendFunc(
		GL_SRC_ALPHA,
		GL_ONE_MINUS_SRC_ALPHA
	);

	int idx = MEMORY_mem[0x004d];
	for (;;) {
		int y = MEMORY_mem[0x000c + idx] - Y_ADJUSTMENT + Y_BLANKS;
		int gfx_id = MEMORY_mem[0x0500 + idx];
		int color_id = MEMORY_mem[0x050b + idx];
		int flags = MEMORY_mem[0x0042 + idx];
		int h = MEMORY_mem[RR_HEIGHTS + color_id];
		int w = 2 * (8 + 8 * MEMORY_mem[RR_WIDTHS + idx]);

		if (gfx_id >= 2) {
			// Only then render

			assert(color_id >= OBJECTS_OFFSET && color_id <= 15);
			rr_object *o = &objects[color_id - OBJECTS_OFFSET];

			float sy = 1.0 - 2 * (y / 240.0);
			float sh = 2.0 * o->normal.height / 240.0;
			float sw = 2.0 * w / 336.0;

			gl_texture *t;

			if (gfx_id <= 6) {
				// Explosion
				static int id_to_explosion_index[7] = {-1, -1, 2, 1, 0, 1, 2};
				int explosion_index = id_to_explosion_index[gfx_id];
				assert(explosion_index >= 0 && explosion_index <= 2);
				t = &o->explosions[explosion_index];
			} else if (flags & 8) {
				// Mirror
				t = &o->mirror;
			} else {
				// Normal
				t = &o->normal;
			}
			gl_texture_draw(t,
				0, 1, 0, 1,
				-0.9, -0.9 + sw, sy, sy - sh);
		}

		if (y + h > 160) {
			break;
		}
		idx++;
	}

	for (int t = 0; t < NUM_OBJECTS; t++) {
		float x = -0.8 + t * 0.1;
		gl_texture_draw(&objects[t].normal,
			0, 1, 0, 1,
			x, x + 0.1, -0.8, -1);
		gl_texture_draw(&objects[t].mirror,
			0, 1, 0, 1,
			x, x + 0.1, -0.6, -0.8);
	}

	gl.Color4f(1.0f, 1.0f, 1.0f, 1.0f);
	gl.Disable(GL_BLEND);

}

static UI_tMenuItem menu[] = {
	UI_MENU_END
};

static void refresh_config()
{
}

static struct UI_tMenuItem* get_config()
{
	refresh_config();
	return menu;
}

static void handle_config(int option)
{
	switch (option) {
	}
	refresh_config();
}

ext_state* ext_register_river_raid(void)
{
	ext_state *s = ext_state_alloc();
	s->name = "River Raid Hack by Eru";
	s->initialize = init;
	s->code_injection = NULL;
	s->post_gl_frame = post_gl_frame;

	s->get_config = get_config;
	s->handle_config = handle_config;

	return s;
}
