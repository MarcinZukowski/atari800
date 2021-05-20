#include "ext-river-raid.h"

#include <math.h>
#include <assert.h>

#include "sdl/video_gl-common.h"
#include "sdl/video_gl-ext.h"

#include "antic.h"
#include "colours.h"
#include "gtia.h"
#include "memory.h"
#include "ui.h"

#define RR_COLOR_LOS 0xBB30
#define RR_COLOR_DATA 0xB700
#define RR_GFX_LOS 0xBB40
#define RR_GFX_DATA 0xB900
#define RR_HEIGHTS 0xBB60
#define RR_WIDTHS 0x0521

#define RR_LINE_COUNT 160

#define Z_NEAR 200
#define Z_FAR ((Z_NEAR) + (RR_LINE_COUNT))

#define Y_ADJUSTMENT 0x5D  // adjustment in the Y positions
#define Y_BLANKS 20   // empty lines on top of the screen

int use_perspective = 0;

/******************************************* OBJECTS *************************************/

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

#define NUM_PLANE_TEXTURES 10
gl_texture plane_textures[NUM_PLANE_TEXTURES];  // left, right, straight

gl_texture missile_texture;

// If color_or_ptr < 256, it's color. Otherwise it's a pointer to a color bitmap
static gl_texture gen_texture(int height, int color_or_ptr, int gfx_ptr)
{
	static const int width = 8;

	gl_texture t = gl_texture_new(width, height);

	for (int y = 0; y < height; y++) {
		// We're upside down
		int idx = height - y - 1;
		// Take color, we're upside down
		byte color = color_or_ptr < 256 ? color_or_ptr : MEMORY_mem[color_or_ptr + idx];
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

static void init_objects()
{
	int i;
	for (i = 0; i < NUM_OBJECTS; i++) {
		objects[i] = gen_object(OBJECTS_OFFSET + i);
	}

	// Generate plane textures based on addresses from BACD pointers
	for (i = 0; i < NUM_PLANE_TEXTURES; i++) {
		plane_textures[i] = gen_texture(14, GTIA_COLPM1, MEMORY_dGetWord(0xBACD + 2 * i));
	}

	// Missile is 8 lines with 0x20.
	// a5b3 has 0x20
	missile_texture = gen_texture(1, GTIA_COLPM1, 0xa5b3);
}

static void render_objects()
{

	int idx = MEMORY_mem[0x004d];
	for (;;) {
		if (idx >= 11) {
			break;
		}
		int y = MEMORY_mem[0x000c + idx] - Y_ADJUSTMENT;
		int gfx_id = MEMORY_mem[0x0500 + idx];
		int color_id = MEMORY_mem[0x050b + idx];
		int flags = MEMORY_mem[0x0042 + idx];
		int h = MEMORY_mem[RR_HEIGHTS + color_id];
		int w = 2 * (8 + 8 * MEMORY_mem[RR_WIDTHS + idx]);
		int x = MEMORY_mem[0x516 + idx];

		// Fix X for now
		// x = 48;

		if (gfx_id >= 2) {
			// Only then render
			EXT_ASSERT(color_id >= OBJECTS_OFFSET && color_id <= 15, "Unexpected color_id=%d, idx=%d", color_id, idx);
			rr_object *o = &objects[color_id - OBJECTS_OFFSET];

			float sy, sh, sw, sx, z;
			if (use_perspective) {
				// In 3D coords
				sy = 0;
				sh = 0.25 * o->normal.height;
				sy += sh / 2;
				sx = 2 * (x - 128);
				sw = 1.0 * w;
				z = - (Z_FAR - y);
			} else {
				y += Y_BLANKS;
				// In 2D screen coords
				sy = 120 - y;
				sh = o->normal.height;
				sw = w;
				sx = 2 * (x - 128);
				z = Z_VALUE_2D;
			}

			gl_texture *t;
			if (gfx_id <= 6) {
				// Explosion
				static int id_to_explosion_index[7] = {-1, -1, 2, 1, 0, 1, 2};
				int explosion_index = id_to_explosion_index[gfx_id];
				assert(explosion_index >= 0 && explosion_index <= 2);
				t = &o->explosions[explosion_index];
			} else {
				o = &objects[gfx_id - OBJECTS_OFFSET];
				t = (flags & 8) ? &o->mirror : &o->normal;
			}
			gl_texture_draw(t,
				0, 1, 0, 1,
				sx, sx + sw, sy, sy - sh,
				z);
		}

		idx++;

		if (y + h > RR_LINE_COUNT) {
			break;
		}
	}

	// Showcase all objects
	for (int t = 0; t < NUM_OBJECTS; t++) {
		float x = -0.8 + t * 0.1;
		gl_texture_draw(&objects[t].normal,
			0, 1, 0, 1,
			x, x + 0.1, -0.8, -1,
			Z_VALUE_2D);
		gl_texture_draw(&objects[t].mirror,
			0, 1, 0, 1,
			x, x + 0.1, -0.6, -0.8,
			Z_VALUE_2D);
	}
}

/******************************************* LINES *************************************/
#define RR_LINE_WIDTH 48

gl_texture lines[RR_LINE_COUNT];  // Ordering the same as in Atari memory, 80 lines from 0x2000 and 80 from 0x3000

int last_active = 0;
int last_line_nr = 0;

static int addr_to_line(int addr)
{
	int line;
	if (addr < 0x3000) {
		line = (addr - 0x2000) / RR_LINE_WIDTH;
	} else {
		line = 80 + (addr - 0x3000) / RR_LINE_WIDTH;
	}
	EXT_ASSERT_BETWEEN(line, 0, 159);
	return line;
}

static int line_to_addr(int line)
{
	EXT_ASSERT_BETWEEN(line, 0, 159);
	int addr ;
	if (line < 80) {
		addr  = 0x2000 + line * RR_LINE_WIDTH;
	} else {
		addr = 0x3000 + (line - 80) * RR_LINE_WIDTH;
	}
	return addr;
}

// Render one atari line to a GL texture
static void render_line(int line_nr)
{
	int addr = line_to_addr(line_nr);
	gl_texture *line = &lines[line_nr];

	int colors[4] = { GTIA_COLBK, GTIA_COLPF0, GTIA_COLPF1, GTIA_COLPF2 };

	for (int xb = 0; xb < RR_LINE_WIDTH; xb++) {
		int byte = MEMORY_mem[addr + xb];
		for (int xp = 0; xp < 4; xp++) {
			int pixel = byte >> (2 * (3 - xp)) & 0x03;
			EXT_ASSERT_BETWEEN(pixel, 0, 3);
			int color = colors[pixel];

			line->data[4 * (4 * xb + xp) + 0] = Colours_GetR(color);
			line->data[4 * (4 * xb + xp) + 1] = Colours_GetG(color);
			line->data[4 * (4 * xb + xp) + 2] = Colours_GetB(color);
			line->data[4 * (4 * xb + xp) + 3] = 0xff;
		}
	}

	gl_texture_finalize(line);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

}

static void print_dl()
{
	int last = -1;
	int cnt = 0;
	printf("DL: ");
	for (int i = 0x3f03; i <= 0x3fa9; i++ ) {
		int b = MEMORY_mem[i];
		if (b != last) {
			if (last >= 0) {
				printf(" %02x", last);
				if (cnt > 1) {
					printf("*%02x", cnt);
				}
			}
			cnt = 0;
			last = b;
		}
		if (i == 0x3fa9) {
			break;
		}
		cnt++;
	}
	printf("\n");
}

static void render_lines()
{
//	print_dl();

	int cur_line_addr = MEMORY_dGetWord(0x3f04);
	int cur_line_nr = addr_to_line(cur_line_addr);
//	printf("  #%d\n", cur_line_nr);

	// Memory at 0x3eff is reset
	int active = MEMORY_mem[0x3eff] != 0;
	if (active && ! last_active) {
		// Re-render all lines
		for (int i = 0; i < RR_LINE_COUNT; i++) {
			render_line(i);
		}
		last_line_nr = cur_line_nr;
	} else {
		while (last_line_nr != cur_line_nr) {
			last_line_nr = (last_line_nr + RR_LINE_COUNT - 1) % RR_LINE_COUNT;
			render_line(last_line_nr);
		}
	}
	last_active = active;

	// Texture is 48 bytes -> 384 atari pixels
	// Screen is really 336 pixels, which is 0.875 of the width
	float margin = (1.0 - 0.875) / 2;

	for (int y = 0; y < RR_LINE_COUNT; y++) {
		int line_nr = (cur_line_nr + y) % RR_LINE_COUNT;
		if (use_perspective) {
			float sy = 0;
			float sh = 2;
			float sx = -192;
			float sw = 384;
			float z = -(Z_FAR - y);
			gl_texture_draw(&lines[line_nr],
				0.0 - (168.0 / 384), 1.0 + (168.0 / 384), 0, 1,
				sx - 168, sx + sw + 168, sy, sy - sh,
				z);
		} else {
			float sy = 120 - (Y_BLANKS + y);
			float sh = 1;
			gl_texture_draw(&lines[line_nr],
				0.0 + margin, 1.0 - margin, 0, 1,
				-168, 168, sy, sy + sh,
				Z_VALUE_2D);
		}
	}

}

static void init_lines()
{
	for (int i = 0; i < RR_LINE_COUNT; i++) {
		lines[i] = gl_texture_new(RR_LINE_WIDTH * 4, 1);
		memset(lines[i].data, 0, RR_LINE_WIDTH);
		gl_texture_finalize(&lines[i]);
	}

	last_active = 0;
}

/******************************************* MAIN *************************************/

static void show_plane_and_missile()
{
	// Show plane
	{
		int plane_idx = MEMORY_mem[0x5e];
		EXT_ASSERT_BETWEEN(plane_idx, 0, 9);
		gl_texture *t = &plane_textures[plane_idx];
		float sy, sh, sw, sx, z;
		sh = 14 / 2;
		sx = 2 * (MEMORY_mem[0x57] - 128);
		sw = 2 * 8;
		if (use_perspective) {
			sy = - 25 - sh;
			z = - Z_NEAR - 34;
			sx = -4;
		} else {
			sy = 120 - 0xAA - 6;
			z = Z_VALUE_2D;

		}
		gl_texture_draw(t, 0, 1, 0, 1,
			sx, sx + sw, sy, sy + sh, z);
	}

	// Show missile
	{
		int missile_y = MEMORY_mem[0x56];
		if (missile_y > 1) {
			float sy, sh, sw, sx, z;
			sh = 1;
			sw = 2 * 8;
			sx = 2 * (MEMORY_mem[0x57] - 128);
			if (use_perspective) {
				sy = - 25 - sh / 2;
				sx = 0;
				z = - (Z_FAR - missile_y);
			} else {
				sx += 4;
				sy = 120 - missile_y;
				z = Z_VALUE_2D;
			}
			gl_texture_draw(&missile_texture, 0, 1, 0, 1,
				sx, sx + sw, sy, sy + sh, z);
		}
	}
}

static void render_subwindow(int x, int y, int with_perspective)
{
	use_perspective = with_perspective;

	gl.Viewport(x, y, 336, 240);
	gl.Scissor(x, y, 336, 240);
	gl.Enable(GL_SCISSOR_TEST);
	gl.ClearColor(0.0f, 0.0, 0.2, 0.0f);
	gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	gl.Disable(GL_SCISSOR_TEST);

	gl.MatrixMode(GL_PROJECTION);
	gl.LoadIdentity();
	if (use_perspective) {
		gl.Frustum(
			// left/right
			-168,+168,
			// bottom/top
			-25, 5,
			// near/far
			Z_NEAR, Z_FAR  + 160);
		gl.MatrixMode(GL_MODELVIEW);
		gl.LoadIdentity();

		float player_x = MEMORY_mem[0x0057];
//		printf("Player X: %d\n", (int)player_x);
		player_x -= 128;
		gl.Translatef(-2.0 * player_x, 0, 0);
		gl.Translatef(0, -25.0f, 0);
//		gl.Rotatef(10, 1, 0, 0);

		render_lines();
		render_objects();

		gl.LoadIdentity();
		show_plane_and_missile();
	} else {
		gl.Ortho(-168, 168, -120, 120, 0, 10);
		render_lines();
		render_objects();
		show_plane_and_missile();
	}
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

//	gl.PushMatrix();
	GLint old_viewport[4];
	gl.GetIntegerv(GL_VIEWPORT, old_viewport);
	gl.MatrixMode(GL_MODELVIEW);
	gl.PushMatrix();
	gl.MatrixMode(GL_PROJECTION);
	gl.PushMatrix();

	render_subwindow(0, 0, 0);
	render_subwindow(336 * 2, 0, 1);

//	printf("%d %d %d %d\n", old_viewport[0], old_viewport[01, old_viewport[2], old_viewport[3]);
	gl.Viewport(old_viewport[0], old_viewport[1], (GLsizei)old_viewport[2], (GLsizei) old_viewport[3]);

//	gl.PopMatrix();
	gl.Color4f(1.0f, 1.0f, 1.0f, 1.0f);
	gl.Disable(GL_BLEND);
	gl.MatrixMode(GL_PROJECTION);
	gl.PopMatrix();
	gl.MatrixMode(GL_MODELVIEW);
	gl.PopMatrix();

}

static int river_raid_init(void)
{
	// Some memory fingerprint
	int address = 0xb55c;
	byte fingerprint[] = {0xA4, 0x4D, 0xA2, 0x5D, 0xD0, 0x03};

	if (memcmp(MEMORY_mem + address, fingerprint, sizeof(fingerprint))) {
		// No match
		printf("RIVER RAID not detected\n");
		return 0;
	}

	printf("RIVER RAID detected\n");

	init_lines();
	init_objects();

	// Match
	return 1;
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
	s->initialize = river_raid_init;
	s->code_injection = NULL;
	s->post_gl_frame = post_gl_frame;

	s->get_config = get_config;
	s->handle_config = handle_config;

	return s;
}
