#ifndef EXT_JS_H
#define EXT_JS_H

/* JavaScript (QuickJS) scripting for extensions, see data/ext/README.md.
   ext_js_init() creates the runtime, installs the "a8" and "gl" globals and
   loads every data/ext/<name>/init.js module. */
void ext_js_init(void);

#endif /* EXT_JS_H */
