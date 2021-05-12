#include "ext-yoomp.h"

#include <math.h>

#include "ext-lua.h"

#include "sdl/video_gl-common.h"
#include "sdl/video_gl-ext.h"

#include "antic.h"
#include "colours.h"
#include "memory.h"
#include "ui.h"

static gl_texture glt_background;

#define NUM_BALLS 6
typedef struct yoomp_ball {
	const char *fname;
	const char *name;
	struct gl_obj* glo_ball;
	int colorize;
} yoomp_ball;

static yoomp_ball yoomp_balls[NUM_BALLS] = {
	NULL, "ORIGINAL", NULL, 0,
	"data/ext/yoomp/ball-yoomp-bw.obj", "Yoomp-like-colorized", NULL, 1,
	"data/ext/yoomp/ball-yoomp.obj", "Yoomp-like-green", NULL, 0,
	"data/ext/yoomp/ball-amiga.obj", "Amiga V1", NULL, 0,
	"data/ext/yoomp/ball-amiga-2.obj", "Amiga V2", NULL, 0,
	"data/ext/yoomp/beach-ball.obj", "Beach Ball", NULL, 0,
};

static int config_lua_script_on = 0;
static int config_background_on = 1;
static int config_ball_nr = 1;

static void xx_load_ball()
{
	for (int i = 1; i < NUM_BALLS; i++) {
		yoomp_balls[i].glo_ball = gl_obj_load(yoomp_balls[i].fname);
	}
}

static void xx_load_background()
{
	glt_background = gl_texture_load_rgba("data/ext/yoomp/rof-gray.rgba", 476, 476);

    gl_texture t = glt_background;

	float xc = t.width / 2.0f + 7;
	float yc = t.height / 2.0f;
	float rad = 120.0;
	float dark = 0.8 * rad;

	// Fix alphas
	for (int y = 0; y < t.width; y++) {
		for (int x = 0; x < t.width; x++) {
			int idx = y * t.width + x;
			float r = sqrt((x - xc) * (x - xc) + (y - yc) * (y - yc));
			int alpha = 0;
			if (r <= dark) {
				alpha = 255 - 255.0 * r / dark;
			} else if (r <= rad) {
				alpha = 0;
			} else {
				alpha = 255;
			}
			t.data[4 * idx + 3] = alpha;
		}
	}

	gl_texture_finalize(&glt_background);
}

static void xx_draw_background()
{
	float L = -0.77;
	float R = -L;
	float T = 0.9;
	float B = -0.75;

	float TL = 0.18;
	float TR = 0.84;
	float TT = 0.76;
	float TB = 0.25;

	// 4F60 has background color
	int color = MEMORY_mem[0x4F60] | 0x0F;  // brightest
	float colR = Colours_GetR(color) / 255.0;
	float colG = Colours_GetG(color) / 255.0;
	float colB = Colours_GetB(color) / 255.0;

	gl.Disable(GL_DEPTH_TEST);
	gl.Color4f(colR, colG, colB, 1.0f);
	gl.Enable(GL_BLEND);
	gl.BlendFunc(
		GL_SRC_ALPHA,
		GL_ONE_MINUS_SRC_ALPHA
	);

	gl_texture_draw(&glt_background, TL, TR, TT, TB, L, R, T, B);

	gl.Color4f(1.0f, 1.0f, 1.0f, 1.0f);
	gl.Disable(GL_BLEND);
}

static float xx_last = 0;

static void xx_render_ball()
{
	gl.Disable(GL_TEXTURE_2D);

	gl.MatrixMode(GL_MODELVIEW);
	gl.PushMatrix();
	gl.LoadIdentity();

/*
	gl.Enable(GL_LIGHTING);
	gl.Enable(GL_LIGHT0);
	gl.Enable(GL_COLOR_MATERIAL);
	GLfloat light_pos[] = {0, 0.2, 2.5, 0};
	gl.Lightfv(GL_LIGHT0, GL_POSITION, light_pos);
*/
	xx_last++;

	const int EQU_BALL_X = 0x0030;
	const int EQU_BALL_VX = 0x0031;
	const int EQU_BALL_VY = 0x0032;

	float ball_angle = MEMORY_mem[EQU_BALL_X];
	float ball_vx = MEMORY_mem[EQU_BALL_VX];
	float ball_vy = MEMORY_mem[EQU_BALL_VY];

	gl.Translatef(
		(ball_vx - 128 + 4) / 84,
		- (ball_vy - 112 - 8) / 120,
		0);
	gl.Scalef(0.05, 0.07, 0.07);
	gl.Rotatef(ball_angle / 256.0 * 360.0, 0, 0, 1);
	gl.Rotatef(11 * xx_last, 1, 0, 0);

	float colR, colG, colB;
	if (yoomp_balls[config_ball_nr].colorize) {
		// 4F5c has ball color
		int color = MEMORY_mem[0x4F5c] | 0x0c;  // brightness
		colR = Colours_GetR(color) / 255.0;
		colG = Colours_GetG(color) / 255.0;
		colB = Colours_GetB(color) / 255.0;
	} else {
		colR = colG = colB = 1.0;
	}

    gl_obj_render_colorized(yoomp_balls[config_ball_nr].glo_ball, colR, colG, colB);

	gl.PopMatrix();

	gl.Enable(GL_TEXTURE_2D);
	gl.Color4f(1, 1, 1, 1);
	gl.Disable(GL_LIGHTING);
	gl.Disable(GL_LIGHT0);
}

static void xx_init()
{
    xx_load_background();
	xx_load_ball();
}

static void lua_draw_background()
{
    struct lua_State *L = lua_ext_get_state();
	if (!L) {
		printf("%s: Lua not ready, waiting\n", __FUNCTION__);
		return;
	}

	ext_lua_run_str("yoomp_render_frame()");
}

static void xx_draw()
{

	if (config_lua_script_on) {
		lua_draw_background();
	}

	if (ANTIC_dlist != 0xCA00) {
		// Not in-game
		return;
	}

	if (config_background_on) {
		xx_draw_background();
	}
	if (config_ball_nr > 0) {
		xx_render_ball();
	}
}

static int yoomp_init(void)
{
	// Some memory fingerprint from 0x4000
	byte fingerprint_3600[] = {0x20, 0x00, 0xB0, 0x20, 0xBC, 0x3D};

	if (memcmp(MEMORY_mem + 0x3600, fingerprint_3600, sizeof(fingerprint_3600))) {
		// No match
		return 0;
	}

	// Match
	xx_init();
	return 1;
}

static UI_tMenuItem menu[] = {
	UI_MENU_ACTION(0, "(Lua) Script enabled:"),
	UI_MENU_ACTION(1, "Nicer background:"),
	UI_MENU_ACTION(2, "Ball type:"),
	UI_MENU_END
};

static void refresh_config()
{
	menu[0].suffix = config_lua_script_on ? "ON" : "OFF";
	menu[1].suffix = config_background_on ? "ON" : "OFF";
	menu[2].suffix = yoomp_balls[config_ball_nr].name;
}

static struct UI_tMenuItem* get_config()
{
	refresh_config();
	return menu;
}

static void handle_config(int option)
{
	switch (option) {
		case 0:
			config_lua_script_on ^= 1;
			break;
		case 1:
			config_background_on ^= 1;
			break;
		case 2:
			config_ball_nr = (config_ball_nr + 1) % NUM_BALLS;
			break;
	}
	refresh_config();
}

ext_state* ext_register_yoomp(void)
{
	ext_state *s = ext_state_alloc();
	s->name = "Yoomp! Hack by Eru";
	s->initialize = yoomp_init;
	s->code_injection = NULL;
	s->post_gl_frame = xx_draw;

	s->get_config = get_config;
	s->handle_config = handle_config;

	return s;
}
