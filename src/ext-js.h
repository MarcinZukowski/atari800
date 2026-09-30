#ifndef EXT_JS_H
#define EXT_JS_H

/* The scripting side of the extensions: embeds QuickJS, exposes the "a8"
   global (the "gl" global lives in sdl/video_gl-js.c), loads every
   data/ext/<name>/init.js module and registers its default export as an
   extension. ext.c drives the extensions through the functions below. */

#include <quickjs/quickjs.h>

#include "ext.h"
#include "ui.h"

/* Optional hook methods of an extension object */
enum ext_hook {
	EXT_HOOK_ACTIVATE,        /* onActivate(): the fingerprint matched */
	EXT_HOOK_PRE_GL_FRAME,    /* onPreGlFrame(): before the Atari screen is converted */
	EXT_HOOK_POST_GL_FRAME,   /* onPostGlFrame(): after it was drawn */
	EXT_HOOK_CODE_INJECTION,  /* onCodeInjection(pc, op): at one of the listed addresses */
	EXT_HOOK_FRAME,           /* onFrame(): once per Atari frame, with or without OpenGL */
	EXT_HOOK_COUNT
};

/* One extension: the default export of its init.js module */
typedef struct ext_extension {
	JSValue self;              /* the exported object; hooks are called with this = self */
	char *name;

	/* Detection: the bytes that must be found at fp_address in Atari memory */
	int fp_address;
	byte *fp_bytes;
	int fp_size;

	/* Addresses for onCodeInjection, -1 terminated, or NULL */
	int *injection_list;

	/* Hook functions, JS_UNDEFINED where the script has none */
	JSValue hooks[EXT_HOOK_COUNT];

	/* Menu: the "menu" object, its keys in definition order and the UI items
	   built from it (label and suffix strings are owned here) */
	JSValue menu;
	int menu_count;
	char **menu_keys;
	char **menu_labels;
	char **menu_suffixes;
	UI_tMenuItem *ui_items;
} ext_extension;

/* Creates the runtime and loads all extensions. */
void ext_js_init(void);

#define ext_js_has_hook(ext, hook) (!JS_IsUndefined((ext)->hooks[hook]))

/* Calls a no-argument hook. A script error is fatal. */
void ext_js_call_hook(ext_extension *ext, enum ext_hook hook);

/* Calls onCodeInjection(pc, op) and returns the opcode it chose. */
int ext_js_call_code_injection(ext_extension *ext, int pc, int op);

/* Menu items with the current option texts, and cycling one option. */
UI_tMenuItem *ext_js_menu_items(ext_extension *ext);
void ext_js_menu_cycle(ext_extension *ext, int option);

#endif /* EXT_JS_H */
