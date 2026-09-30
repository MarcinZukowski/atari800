# atari800 extensibility ideas

A while ago I saw this [thread on AtariArea](http://www.atari.org.pl/forum/viewtopic.php?id=17319).

It gave me an idea to add a generic extension mechanism to atari800.
I started playing, and over a course of a few weeks, an hour here, an hour there, I wrote a bunch
of code and extensions for some Atari games.

## How it works

Extensions are JavaScript modules, one per game in `data/ext/<name>/init.js`, run by an
embedded [QuickJS](https://bellard.org/quickjs/) engine. The emulator side (`src/ext.c`) is
small: it keeps the list of extensions, activates the one whose memory fingerprint matches
the running program (TAB opens the extensions menu, or see `A8_EXT_SELECT` under Testing),
handles the ALT (extensions off) and CTRL (acceleration off) keys, and calls the extension's
hooks from a few places in the emulator:

* before an Atari frame is converted for OpenGL (`onPreGlFrame`), e.g. to write onto the Atari screen
* after the Atari frame was drawn (`onPostGlFrame`), e.g. to render extra content with OpenGL
* when the CPU is about to execute one of the addresses the extension asked for (`onCodeInjection`).
  The hook can let the instruction run, run the routine on a "fake CPU" so that it costs no
  emulated time, or skip it and do the work itself in JavaScript.

The scripting side (`src/ext-js.c`, `src/sdl/video_gl-js.c`) exposes the `a8` and `gl` globals
described below; `src/sdl/sfx.c` mixes the extensions' sound effects into the emulator's audio.

## JavaScript scripting

Extensions are enabled with `--with-ext` when running `configure`. QuickJS needs to be
installed (e.g. `brew install quickjs`) with `CPPFLAGS`/`LDFLAGS` pointing at it.
Not all emulator functionality is exposed; more can easily be added.

At startup atari800 looks for files matching `data/ext/*/init.js`
(e.g. [data/ext/zybex/init.js](zybex/init.js)), evaluates each as an ES module and
registers its default export as an extension. Shared helpers live in
[common.js](common.js) and are imported the usual way:

```js
import { drawQuad, word, rgb } from "../common.js";
```

The QuickJS `std` and `os` modules and `console.log()` are available too.
A script error prints the exception with its stack trace and exits the emulator.

Two globals form the API (see [ext-js.c](../../src/ext-js.c) and
[sdl/video_gl-js.c](../../src/sdl/video_gl-js.c) for details):

### `a8` - the emulator

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
  * `finalize()` - uploads `pixels` to OpenGL; call it before drawing and after every change.
    For mipmaps, bind the texture and set `gl.GENERATE_MIPMAP` to `gl.TRUE` before it, then
    choose a `*_MIPMAP_*` minification filter ([altreal/view3d.js](altreal/view3d.js) does)
  * `draw(texL, texR, texT, texB, scrL, scrR, scrT, scrB, z = -2)` - draws the texture on a quad
* `gl.drawTriangles(positions, normals)` - draws `GL_TRIANGLES` from flat `Float32Array`s (x, y, z
  per vertex; `normals` may be omitted) in a single call. [yoomp/obj.js](yoomp/obj.js) loads
  Wavefront `.obj`/`.mtl` models into that form
* `gl.readPixels(x, y, width, height)` - the framebuffer as a `Uint8Array` of RGBA bytes, rows
  bottom-up, in window pixels; for test scripts that want to look at what was drawn
* `gl.drawScreen(x0, y0, x1, y1, left, right, top, bottom, z = -2)` - draws that region of the
  emulated screen (pixels of the displayed area, y down; `gl.screenSize()` gives its size) onto
  a rectangle in GL coordinates, with linear filtering: for rearranging the game's screen, like
  the wide layout of Alternate Reality

### The extension object

Each `init.js` exports the extension object as its default export (`export default { ... }`)
with these properties. Hook methods are called with `this` bound to that object, so it
doubles as the extension's state:

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

Input can be scripted with atari800's `-playback file` (`-playbacknoexit` keeps running at
the end). The file is plain text: the line `Atari800 event recording, version: 1`, one line
with the POKEY random seed (`0`), then per frame eight lines: `key shift consol` (`-1 0 7`
for nothing), the ports 0/1 and 2/3 joystick bytes (`255` centred; stick 0 forward is `254`,
right `247`), four trigger lines (`1` = released) and a screen checksum (`00000000`, the
mismatch is only logged). This is how the walks in the Alternate Reality notes were measured.

## Technicalities

This work was a quick hack, without paying much respect to things like
maintainability, portability etc.

Some notes:
* It was designed to work only with the SDL 1.2/OpenGL backend.
  * A lot of functionality had to be added there
* A bunch of small injections had to be made in multiple places.
* The extension code is C99, so `--with-ext` builds drop upstream's `-ansi -pedantic` flags.
* Developed, and only tested on MacOSX.
  * A `tools/ext-helper` script exists for simplifying compilation, very specific to my setup
    ```
    tools/ext-helper bootstrap
    tools/ext-helper install
    ```

# Games extended (in order of creation)

These games are also discussed in [this video on YouTube](https://www.youtube.com/watch?v=075qLp5kIlc).

* Yoomp: [yoomp/init.js](yoomp/init.js) (originally in C, now JavaScript)
  * various 3D balls
  * one high-res background
* Mercenary: [mercenary/init.js](mercenary/init.js), [mercenary.md](mercenary/mercenary.md) (originally in C, now JavaScript)
  * accelerated Atari-like line drawing
  * the 3D scene redrawn with OpenGL from the game's own geometry: exact vertex positions
    and view angles are read as the game projects them, the transform is redone in floating
    point, and edges are drawn between sub-pixel end points (3 line styles)
  * faces found in each model's edge graph (coplanar chordless cycles) and drawn as translucent
    "glass" or shaded polygons under the lines
* Zybex: [zybex/init.js](zybex/init.js), [zybex.md](zybex/zybex.md) (originally in C, now JavaScript)
  * scrolling background (grayscale and color modes)
* Behind Jaggi Lines: [bjl/init.js](bjl/init.js) (originally in C, now JavaScript)
  * faster rendering
* Alternate Reality: [altreal/init.js](altreal/init.js), [altreal.md](altreal/altreal.md) (originally in C, now JavaScript)
  * faster rendering
  * smooth walking: small steps instead of five big ones per cell, at the game's own speed or
    1.5, 2 or 3 times it (needs the HIGH acceleration, see the notes for how the engine moves)
  * the maze redrawn with OpenGL ([altreal/view3d.js](altreal/view3d.js)): the level map is read
    from memory and drawn with the game's own projection (so both views agree), plus fog,
    shading and interpolated steps and turns; the walls, doors
    and arches carry the game's own art, decoded from its memory in the game's current colours
    (so the picture flashes when the game flashes it), optionally upscaled 4x with Scale2x, and
    arches open onto what lies beyond. A wide layout puts the view over the whole width with the
    game's texts and compass shrunk above and below it.
    The notes document the engine: map, movement, picture buffer, art and renderer
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
