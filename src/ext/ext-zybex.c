#include "ext-zybex.h"

#include <assert.h>

#include <SDL.h>

#include "sdl/video_gl-common.h"
#include "sdl/video_gl-ext.h"

#include "antic.h"
#include "cpu.h"
#include "memory.h"
#include "ui.h"
#include "ui_basic.h"
#include "screen.h"

static int config_display_fps = 1;
static int config_show_background = 2;

static gl_texture glt_background_color;
static gl_texture glt_background_gs;

static void load_background()
{
	glt_background_color = gl_texture_load_rgba("data/ext/zybex/bkg1-512x512.rgba", 512, 512);
	gl_texture_finalize(&glt_background_color);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	glt_background_gs = gl_texture_load_rgba("data/ext/zybex/bkg1-gs-512x512.rgba", 512, 512);
	gl_texture_finalize(&glt_background_gs);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

static int init(void)
{
	// Some memory fingerprint from 0x3000
	byte fingerprint_3000[] = {0x18, 0x69, 0x14, 0xA8, 0xC0, 0x50};

	if (memcmp(MEMORY_mem + 0x3000, fingerprint_3000, sizeof(fingerprint_3000))) {
		// No match
		return 0;
	}

	load_background();

	// Match
	return 1;
}

static UI_tMenuItem menu[] = {
	UI_MENU_ACTION(0, "Show background:"),
	UI_MENU_ACTION(1, "Display FPS:"),
	UI_MENU_END
};

static void refresh_config()
{
	const char *show_background_text[3] = { "OFF", "COLOR", "GRAYSCALE" };
	menu[0].suffix = show_background_text[config_show_background];
	menu[1].suffix = config_display_fps ? "ON" : "OFF";
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
			config_show_background = (config_show_background + 1) % 3;
			break;
		case 1:
			config_display_fps ^= 1;
			break;
	}
	refresh_config();
}

static void pre_gl_frame()
{
	if (!config_display_fps) {
		return;
	}

	const char *fps_str = ext_fps_str(ANTIC_dlist);
	Print(0x9f, 0x90, fps_str, 0, -3, 20);
}

static void show_background()
{
	static int last_hscrol = 0;
	static int texture_hscrol = 0;

	// Screen coords
	float L = -0.95;
	float R = -L;
	float T = 0.78;
	float B = -0.65;

	// If hscrol changes, the screen moved, adjust texture
	if (ANTIC_HSCROL != last_hscrol) {
		texture_hscrol++;
		last_hscrol = ANTIC_HSCROL;
	}

	// Texture coords
	float TL = 0.0015 * texture_hscrol;
	float TR = TL + 0.5;
	float TT = 0.70;
	float TB = 0.30;

	int texture_id = config_show_background == 1 ? glt_background_color.gl_id : glt_background_gs.gl_id;
	gl.BindTexture(GL_TEXTURE_2D, texture_id);

	gl.Disable(GL_DEPTH_TEST);
	gl.Color4f(0.2, 0.2, 0.2, 0.5f);
	gl.Enable(GL_BLEND);
	gl.BlendFunc(
		GL_ONE_MINUS_DST_COLOR,
		GL_ONE
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

	gl.Color4f(1.0f, 1.0f, 1.0f, 1.0f);
	gl.Disable(GL_BLEND);
}

static void  post_gl_frame()
{
	if (config_show_background) {
		show_background();
	}
}

ext_state* ext_register_zybex(void)
{
	ext_state *s = ext_state_alloc();
	static char* name  = "ZYBEX HACK by ERU";
	s->initialize = init;
	s->name = name;
	s->code_injection = NULL;
	s->pre_gl_frame = pre_gl_frame;
	s->post_gl_frame = post_gl_frame;

	s->get_config = get_config;
	s->handle_config = handle_config;

	return s;
}
