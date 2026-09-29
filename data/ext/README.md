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

## JavaScript scripting

The extension mechanism also supports scripting in JavaScript, using the
[QuickJS](https://bellard.org/quickjs/) engine. It is enabled with
`--with-ext-js` when running `configure` (QuickJS needs to be installed,
e.g. `brew install quickjs`, and `CPPFLAGS`/`LDFLAGS` need to point at it).
Not all functionality is exposed; more can easily be added.

At startup atari800 looks for files matching `data/ext/*/init.js`
(e.g. [data/ext/zybex/init.js](zybex/init.js)) and evaluates each as an ES module.
Shared helpers live in [common.js](common.js) and are imported the usual way:

```js
import { drawQuad, word, rgb } from "../common.js";
```

The QuickJS `std` and `os` modules and `console.log()` are available too.
A script error prints the exception with its stack trace and exits the emulator.

Two globals form the API (see [ext-js.c](../../src/ext-js.c) and
[sdl/video_gl-js.c](../../src/sdl/video_gl-js.c) for details):

### `a8` - the emulator

* `a8.register(extension)` - registers an extension, see below
* `a8.mem` - a `Uint8Array` over the 64 KB of Atari memory, read/write, no copy:
  ```js
  const lives = a8.mem[0x00C0];
  a8.mem[0x00C0] = 9;                 // stores wrap to a byte, like a C uint8_t
  a8.mem.fill(0xEA, 0xB3B0, 0xB3B6);  // NOP out six bytes
  ```
  Intermediate values are plain numbers, so mask them yourself: `(a + b) & 0xFF`.
* `a8.palette` - an `Int32Array` over the current palette, `0x00RRGGBB` per Atari colour;
  `a8.rgb(colour)` returns `[r, g, b]` in 0..255
* `a8.peek(addr)`, `a8.poke(addr, value)` - memory access that honours bank switching, ROM and
  hardware registers; use them instead of `a8.mem` for screen memory under the OS ROM or for I/O
* `a8.cpu.a`, `a8.cpu.x`, `a8.cpu.y`, `a8.cpu.s`, `a8.cpu.p`, `a8.cpu.pc` - the 6502 registers,
  read/write; they are current inside `onCodeInjection`, where changes take effect on return
* `a8.antic.dlist`, `a8.antic.hscrol` - ANTIC registers
* `a8.gtia.colbk`, `a8.gtia.colpf0`..`colpf3`, `a8.gtia.colpm0`..`colpm3` - GTIA colour registers
* "Fake CPU" functions and constants for use inside `onCodeInjection`:
  * `a8.OP_RTS`, `a8.OP_NOP` - 6502 opcodes
  * `a8.fakeCpuUntilPc(pc)` - run the CPU (without side effects on the machine) until reaching address `pc`
  * `a8.fakeCpuUntilOp(op)` - run the CPU until reaching opcode `op` (e.g. `a8.OP_RTS`)
  * `a8.fakeCpuUntilAfterOp(op)` - the same, but also execute that opcode (e.g. return from the routine)
* `a8.printFps(value, fg, bg, x, y)` - counts frames (a change of `value` is a new frame)
  and prints the rate on the Atari screen at `x, y`. Typically called from `onPreGlFrame`
* `a8.accelerationDisabled()` - true while CTRL is held
* `a8.loadSound(path)` - loads a WAV file; the result has a `play()` method.
  The sound is mixed on top of the POKEY output

### `gl` - OpenGL

* Legacy OpenGL calls without the `gl` prefix: `gl.Enable`, `gl.Disable`, `gl.Begin`, `gl.End`,
  `gl.Color4f`, `gl.TexCoord2f`, `gl.Vertex3f`, `gl.Normal3f`, `gl.BlendFunc`, `gl.BindTexture`,
  `gl.TexParameteri`, `gl.MatrixMode`, `gl.PushMatrix`, `gl.PopMatrix`, `gl.LoadIdentity`,
  `gl.Translatef`, `gl.Scalef`, `gl.Rotatef`, `gl.Ortho`, `gl.Frustum`, `gl.Viewport`, `gl.Scissor`,
  `gl.Clear`, `gl.ClearColor`, `gl.Fogf`, `gl.Fogfv(pname, [values])`, `gl.Lightfv(light, pname, [values])`,
  `gl.LineWidth`, `gl.PolygonMode`, `gl.PushAttrib`, `gl.PopAttrib`, `gl.GetIntegerv(pname)` (returns an array)
* Constants without the `GL_` prefix, like WebGL: `gl.TEXTURE_2D`, `gl.BLEND`, `gl.DEPTH_TEST`,
  `gl.QUADS`, `gl.SRC_ALPHA`, `gl.VIEWPORT`, ... (see the `C(...)` list in `video_gl-js.c`)
* `gl.createTexture(width, height)` and `gl.loadTextureRGBA(path, width, height)` return a `Texture`:
  * `pixels` - a `Uint8Array` that *is* the RGBA texture memory (4 bytes per pixel)
  * `width`, `height`, `id` (the OpenGL texture name)
  * `finalize()` - uploads `pixels` to OpenGL; call it before drawing and after every change
  * `draw(texL, texR, texT, texB, scrL, scrR, scrT, scrB, z = -2)` - draws the texture on a quad
* `gl.loadObj(path)` - loads a Wavefront `.obj` file (and its `.mtl`) and returns an `Obj` with
  `render()` and `renderColorized(r, g, b)`

### The extension object

`a8.register()` takes an object with these properties. Hook methods are called with
`this` bound to that object, so it doubles as the extension's state:

* `name` - shown in the extensions menu
* `fingerprint: { address, bytes }` - the extension is activated when the bytes at `address`
  in Atari memory equal `bytes`
* `onActivate()` (optional) - called once the fingerprint matched, with the program in memory
* `onPreGlFrame()` (optional) - called before the Atari screen is converted for OpenGL
* `onPostGlFrame()` (optional) - called after the Atari screen was drawn; draw extra things here
* `codeInjections: [addresses]` with `onCodeInjection(pc, op)` (optional, together) - called
  whenever the CPU is about to execute one of the addresses. Return the opcode to execute:
  usually `op`, or e.g. `a8.fakeCpuUntilOp(a8.OP_RTS)` to skip a routine
* `menu: { KEY: { label, options, current }, ... }` (optional) - entries of the extension's menu
  (TAB in the emulator). `options` is an array of strings and `current` the 0-based index
  of the selected one; the framework updates `current` when the user cycles through the options

### Testing

`A8_EXT_SELECT=<part of the name>` in the environment activates the matching extension as
soon as its fingerprint matches, without going through the TAB menu:

    A8_EXT_SELECT=ZYBEX build/src/atari800 -state zyb.a8s

## Technicalities

This work was a quick hack, without paying much respect to things like
maintainability, portability etc.

Some notes:
* It was designed to work only with the SDL 1.2/OpenGL backend.
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

* Yoomp: [yoomp/init.js](yoomp/init.js), [ext-yoomp.c](../../src/ext/ext-yoomp.c) (old C code, now ported to JavaScript)
  * various 3D balls
  * one high-res background
* Mercenary: [mercenary/init.js](mercenary/init.js), [mercenary.md](mercenary/mercenary.md) (originally in C, now JavaScript)
  * accelerated Atari-like line drawing
  * OpenGL-based line drawing (3 types)
* Zybex: [zybex/init.js](zybex/init.js), [zybex.md](zybex/zybex.md), [ext-zybex.c](../../src/ext/ext-zybex.c) (old C code, now ported to JavaScript)
  * scrolling background (grayscale and color modes)
* Behind Jaggi Lines: [bjl/init.js](bjl/init.js), [ext-bjl.c](../../src/ext/ext-bjl.c) (old C code, now ported to JavaScript)
  * faster rendering
* Alternate Reality: [altreal/init.js](altreal/init.js), [altreal.md](altreal.md), [ext-altreal.c](../../src/ext/ext-altreal.c) (old C code, now ported to JavaScript)
  * faster rendering
* River Raid: [river-raid/init.js](river-raid/init.js), [river-raid.md](river-raid/river-raid.md) (originally in C, now JavaScript)
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
