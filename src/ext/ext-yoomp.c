#include "ext-yoomp.h"

#include <math.h>

#include "ext-lua.h"

#include "sdl/video_gl-common.h"
#include "sdl/video_gl-ext.h"

#include "memory.h"
#include "ui.h"

static gl_texture glt_background;

static struct gl_obj* glo_ball;

static int config_lua_script_on = 1;
static int config_background_on = 0;
static int config_ball_on = 0;

static void xx_load_ball()
{
//	const char* filename = "beach-ball.obj";
	const char* filename = "data/ext/yoomp/ball-yoomp.obj";
//	const char* filename = "ball-amiga.obj";
//	const char* filename = "ball-amiga-2.obj";

    glo_ball = gl_obj_load(filename);
}

static void xx_load_background()
{
	glt_background = gl_texture_load_rgba("data/ext/yoomp/rof.rgba", 476, 476);

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

	gl.BindTexture(GL_TEXTURE_2D, glt_background.gl_id);

	gl.Disable(GL_DEPTH_TEST);
	gl.Color4f(1.0f, 1.0f, 1.0f, 1.0f);
	gl.Enable(GL_BLEND);
	gl.BlendFunc(
		GL_SRC_ALPHA,
		GL_ONE_MINUS_SRC_ALPHA
	);

	gl.Begin(GL_QUADS);
	gl.TexCoord2f(TL, TB);
	gl.Vertex3f(L, B, -2.0f);
	gl.TexCoord2f(TR, TB);
	gl.Vertex3f(R, B, -2.0f);
	gl.TexCoord2f(TR, TT);
	gl.Vertex3f(R, T, -2.0f);
	gl.TexCoord2f(TL, TT);
	gl.Vertex3f(L, T, -2.0f);
	gl.End();

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

    gl_obj_render(glo_ball);

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

	if (config_background_on) {
		xx_draw_background();
	}
	if (config_ball_on) {
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

static UI_tMenuItem configs[] = {
	UI_MENU_ACTION(1, "Nicer background:"),
	UI_MENU_ACTION(2, "Nicer ball:"),
	UI_MENU_END
};

static void yoomp_refresh_config(struct UI_tMenuItem *menu)
{
	menu[1].suffix = config_lua_script_on ? "ON" : "OFF";
	menu[2].suffix = config_background_on ? "ON" : "OFF";
	menu[3].suffix = config_ball_on ? "ON" : "OFF";
}

static void yoomp_add_to_config(struct UI_tMenuItem *menu)
{
	static UI_tMenuItem configs[] = {
		UI_MENU_ACTION(1, "(Lua) Script enabled:"),
		UI_MENU_ACTION(2, "(C) Nicer background:"),
		UI_MENU_ACTION(3, "(C) Nicer ball:"),
		UI_MENU_END
	};
	menu[1] = configs[0];
	menu[2] = configs[1];
	menu[3] = configs[2];
	menu[4] = configs[3];
	yoomp_refresh_config(menu);
}

static void yoomp_handle_config(struct UI_tMenuItem *menu, int option)
{
	switch (option) {
		case 1:
			config_lua_script_on ^= 1;
			break;
		case 2:
			config_background_on ^= 1;
			break;
		case 3:
			config_ball_on ^= 1;
			break;
	}
	yoomp_refresh_config(menu);
}

ext_state* ext_register_yoomp(void)
{
	ext_state *s = ext_state_alloc();
	s->name = "Yoomp! Hack by Eru";
	s->initialize = yoomp_init;
	s->code_injection = NULL;
	s->render_frame = xx_draw;

	s->add_to_config = yoomp_add_to_config;
	s->handle_config = yoomp_handle_config;

	return s;
}
