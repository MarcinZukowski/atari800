# atari800 extensibility ideas

A while ago I saw this [thread on AtariArea](http://www.atari.org.pl/forum/viewtopic.php?id=17319).

It gave me an idea to add a generic extension mechanism to atari800.
I started playing, and over a course of a few weeks, an hour here, an hour there, I wrote a bunch
of code and extensions for some Atari games.

## Framework capabilities

Framework is composed of two main parts.
Generic functionality, and program-specific extension implementation.

Generic functionality (`ext.c`):

* extension library
* shared key handling (ALT to disable extensions, CTRL to disable acceleration only)
* FPS helpers
* menu helpers
* code-injection / "fake CPU" helpers

Extension-specific (`ext/*.c`) functionality and hooks (see `ext_state`  in `ext.h`):

* inject code _before_ an Atari frame is rendered (`pre_gl_frame`).

  This allows e.g. modifying Atari memory and screen, such that the changes there are reflected.

* inject code _after_ a frame is rendered (`post_gl_frame`).

  This allows e.g. rendering additional content with OpenGL.

* inject code based on an the executed instruction (or actually,
  PC address).

  This allows e.g. detecting when a particular code is executed, and then:

  * executing it in a "fake CPU" mode,
    (where instructions are executed but don't impact Atari state, so are effectively zero-cost).
  * skipping the execution, and instead doing something in C

## Lua scripting

The extension mechanism also supports scripting in Lua.
It can be enabled with `--enable-ext-lua` when running `configure`.
Not all functionality is exposed in Lua, depending on the feedback and demand, more can easily be added.

 atari800 at the startup will look for files matching the
[data/ext/*/init.lua](data/ext) pattern and execute them.
Check out those files for a lot of examples.

Currently exposed basic APIs (see [ext-lua.c](../../src/ext-lua.c) for more details):
* `ext_register(state)` - used to register a new extension, see below for more details
* `a8_memory()` - returns a handle to internal Atari memory. Typical usage is
  ```
  local a8mem = a8_memory()
  local something = a8mem:get(some_address)
  ```
* ANTIC registers: `antic_dlist()`,  `antic_hscrol()`
* Palette conversion: `a8_Colours_GetR(color)`, `a8_Colours_GetG(color)`, `a8_Colours_GetB(color)` -
  get R/G/B components for a given palette color
* "Fake-cpu" functions and constants (can be used inside `CODE_INJECTION_FUNCTION`)
  * `OP_RTS`, `OP_NOP` - constants for the 6502 opcodes
  * `ext_fakecpu_until_pc(pc)` - run CPU until reaching a given address `pc`
  * `ext_fakecpu_until_op(op)` - run CPU until reaching a given opcode `op` (e.g. `OP_RTS`)
* `ext_print_fps(value, color1, color2, x, y)` - detect FPS (a `value` change is considered a new frame),
  and displays it using `color1` and `color2` at position `x,y`. Typically used in `PRE_GL_FRAME`
* `ext_acceleration_disabled()` - check if acceleration is disabled (CTRL key)

OpenGL APIs (see [sdl/video_gl-ext.c](../../src/sdl/video_gl-ext.c) for more details):
* `gl_api()` - returns a handle to an OpenGL API interface, which provides various OpenGL functions.
  Typical usage:
  ```
  local gl = gl_api();
  gl:BindTexture(gl.GL_TEXTURE_2D, some_texture_id)
  ```
  * `gl_api()` result provides a set of constants, e.g. `GL_TEXTURE_2D`, `GL_BLEND`, `GL_DEPTH_TEST` etc - (see [sdl/video_gl-ext.c](../../src/sdl/video_gl-ext.c) for a full list).
  * `gl_api()` result provides a set of functions, e.g. `BindTexture`, `Enable`, `Disable` etc - see (see [sdl/video_gl-ext.c](../../src/sdl/video_gl-ext.c) for a full list).
* `glt_load_rgba(fname, width, height)` - loads an RGBA image and returns a handle to a texture object, supporting the following methods:
  * `get(index)` - return a texture byte
  * `set(index, val)` - set a texture byte
  * `width()` - texture width in pixels
  * `height()` - texture height in pixels
  * `num_pixels()` - total number of pixels
  * `num_bytes()` - total number of bytes
  * `gl_id()` - the OpenGL texture id
  * `finalize()` - needs to be called befure use (this allows changing texture bytes before loading, (see [yoomp/init.lua](yoomp/init.lua) for an example)
* `glo_load(fname)` - loads a Wavefront `.obj` file (and the related `.mtl` file) representing a 3D object, and returns a handle to it. It supports the following methods
  * `render()` - render the object as is in 3D
  * `render_colorized(r,g,b)` - render the object while changing its colors to match the provided R/G/B
* `ext_gl_draw_quad(gl, TL, TR, TT, TB, L, R, T, B, Z)` - draws a quad using given 2D texture and world coordinates.
  Defined in `common.lua`.

### Lua extension state

The input to `ext_register` should be a Lua object with the following fields:
*  (note, all functions accepting `self` get this very object as a parameter, this allows using this object as a state)
* `NAME` - the name of a given extension
* `ENABLE_CHECK_ADDRESS` (integer) and `ENABLE_CHECK_FINGERPRINT` (array of bytes) - used to match an extension
  agains a running program. If the bytes from `ENABLE_CHECK_FINGERPRINT` match the content of Atari memory at
  `ENABLE_CHECK_ADDRESS`, a given extension will be used
* `PRE_GL_FRAME(self)` (optional) - function to be called before we convert the Atari memory to OpenGL
* `POST_GL_FRAME(self)` (optional) - function to be called after we rendered the Atari screen already
* `CODE_INJECTION_LIST` (optional) - an array of addresses for which we'll do code injection
* `CODE_INJECTION_FUNCTION(self, pc, op)` (optional) - function called whenever one of the addresses from
  `CODE_INJECTION_LIST` is met. It is provided the address (`pc`) and the instruction (`op`) at that address.
  It should return the instruction to execute (often `op`, but can also return e.g. `OP_RTS`).
* `MENU` - a dictionary of menu options, where the dictionary key is some helpful identifier, and the value is a dictionary with these fields:
  * `LABEL` - label to be displayed
  * `OPTIONS` - values a given menu option can take. Will be iterated over in the menu
  * `CURRENT` - the current value. Set initially, but also modified by the framework when user changes menu options

## Technicalities

This work was a quick hack, without paying much respect to things like
maintainability, portability etc.

Some notes:
* It was designed to work only with the SDL/OpenGL backend.
  * A lot of functionality had to be added there
* A bunch of small injections had to be made in multiple places.
* I used more modern C functionality, so it might not work on some platforms.
  See `src/ext/helper` for compiler flags I changed.
* Developed, and only tested on MacOSX.
  * A `src/ext/helper` tool exists for simplifying compilation, very specific to my setup
    ```
    src/ext/helper bootstrap
    src/ext/helper install
    ```

# Games extended (in order of creation)

These games are also discussed in [this video on YouTube](https://www.youtube.com/watch?v=075qLp5kIlc).

* Yoomp: [yoomp/init.lua](yoomp/init.lua), [ext-yoomp.c](../../src/ext/ext-yoomp.c)) (old C code now ported to Lua)
  * various 3D balls
  * one high-res background
* Mercenary: [ext-mercenary.c](../../src/ext/ext-mercenary.c), [mercenary.md](mercenary.md)
  * accelerated Atari-like line drawing
  * OpenGL-based line drawing (3 types)
* Zybex: [zybex/init.lua](zybex/init.lua), [zybex.md](zybex/zybex.md), [ext-zybex.c](../../src/ext/ext-zybex.c) (old C code now ported to Lua)
  * scrolling background (grayscale and color modes)
* Behind Jaggi Lines: [bjl/init.lua](bjl/init.lua), [ext-bjl.c](../../src/ext/ext-bjl.c) (old C code now ported to Lua)
  * faster rendering
* Alternate Reality: [altreal/init.lua](altreal/init.lua), [altreal.md](altreal.md), [ext-altreal.c](../../src/ext/ext-altreal.c) (old C code now ported to Lua)
  * faster rendering
* River Raid: [ext-river-raid.c](../../src/ext/ext-river-raid.c), [river-raid.md](river-raid/river-raid.md)
  * 3D rendering
  * custom sounds example

## Reverse-engineering games

The best way to detect where the time is going is to use the
`TRACE` functionality of Atari800:
* start a game
* enter the monitor (`F8`)
* type: `trace file.trace` - this starts recording the trace to `file.trace`.
* type: `cont` - this returns to the game
* play for some time (not too long, a few seconds should be enough)
* enter the monitor again (`F8`)
* type: `trace` - this stops the recording
* type: `quit`

Now, you can use the provided helper tool to analyze the `file.trace`, by running:

    tools/trace-postprocess.py < file.trace

This will show the memory areas where we spend most time (based on how often code is execute there).
