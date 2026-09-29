#ifndef SDL_VIDEO_GL_JS_H
#define SDL_VIDEO_GL_JS_H

#include <quickjs/quickjs.h>

/* Installs the global "gl" object: a subset of the legacy OpenGL API, GL_*
   constants (without the prefix), and the Texture and Obj classes.
   See data/ext/README.md for the JavaScript view of it. */
void SDL_VIDEO_GL_JS_Init(JSContext *ctx, JSValueConst global);

#endif /* SDL_VIDEO_GL_JS_H */
