# Game extensions

A generic extension mechanism for atari800: scripts that hook into the emulated machine to
change how a particular program looks and runs, for example by drawing a game's 3D scene again
with OpenGL from the game's own data, or by doing a slow routine's work in no emulated time.
The idea came from this [thread on AtariArea](http://www.atari.org.pl/forum/viewtopic.php?id=17319).

This directory holds the framework's documentation and one extension, the
[self-test](#the-self-test-extension). Extensions for actual games are kept apart, in
[a8ext](https://github.com/MarcinZukowski/a8ext).

## How it works

Extensions are JavaScript modules, one per game in `<ext-dir>/<name>/init.js`, run by an
embedded [QuickJS](https://bellard.org/quickjs/) engine. The emulator side (`src/ext.c`) is
small: it keeps the list of extensions, activates the one whose memory fingerprint matches
the running program (TAB opens the extensions menu, or see `A8_EXT_SELECT` under Testing),
handles the ALT (extensions off) and CTRL (acceleration off) keys, and calls the extension's
hooks from a few places in the emulator:

* before an Atari frame is converted for OpenGL (`onPreGlFrame`), e.g. to write onto the Atari screen
* after the Atari frame was drawn (`onPostGlFrame`), e.g. to render extra content with OpenGL
* once per frame regardless of the video output (`onFrame`)
* when the CPU is about to execute one of the addresses the extension asked for (`onCodeInjection`).
  The hook can let the instruction run, run the routine on a "fake CPU" so that it costs no
  emulated time, or skip it and do the work itself in JavaScript.

The scripting side (`src/ext-js.c`, `src/sdl/video_gl-js.c`) exposes the `a8` and `gl` globals
described below; `src/sdl/sfx.c` mixes the extensions' sound effects into the emulator's audio.

## JavaScript scripting

Extensions are enabled with `--with-ext` when running `configure`. QuickJS needs to be
installed (e.g. `brew install quickjs`) with `CPPFLAGS`/`LDFLAGS` pointing at it.
Not all emulator functionality is exposed; more can easily be added.

At startup atari800 looks for files matching `<ext-dir>/*/init.js`
(e.g. [data/ext/selftest/init.js](selftest/init.js)), evaluates each as an ES module and
registers its default export as an extension. The directory is `data/ext` unless the
`-ext-dir <path>` option or `EXT_DIR=<path>` in the configuration file says otherwise; when it
does not exist there are no extensions. A module imports others the usual way, relative to
itself:

```js
import { drawQuad } from "../common.js";
```

The QuickJS `std` and `os` modules and `console.log()` are available too.
A script error prints the exception with its stack trace and exits the emulator.

Two globals form the API (see [ext-js.c](../../src/ext-js.c) and
[sdl/video_gl-js.c](../../src/sdl/video_gl-js.c) for details):

### `a8` - the emulator

* `a8.host` - where the scripts run: `"native"` in the emulator, `"web"` in the
  [web build](#in-the-browser), where an extension is an ordinary module of the page and may
  use what a page has. `a8.panel` is then an element of the page that belongs to the active
  extension, empty when it is activated, for controls of its own (a file picker, a slider, a
  map), and `a8.overlay` an element over the picture for things to show on it: an element of
  class `pill` in it is a small button like the page's own (class `on` lights it), one of class
  `box` a panel, each placed with its own `top`/`left`/`right`/`bottom`. Natively both are `null`.
  Anything that needs the page must be an addition: the [menu](#the-extension-object) is the
  user interface that works everywhere.
  ```js
  if (a8.host === "web") { const button = document.createElement("button"); a8.panel.append(button); }
  ```
* `a8.extDir` - the directory of the extension being loaded or activated, for the files it
  ships; a script reads it at the top of its modules:
  ```js
  const DIR = a8.extDir;
  const picture = gl.loadTextureRGBA(`${DIR}/picture.rgba`, 512, 512);
  ```
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
* `a8.antic.dlist`, `a8.antic.hscrol`, `a8.antic.vscrol`, `a8.antic.chbase` - ANTIC registers
* `a8.profile(what)` - the monitor's profile as a `Float64Array` of 65536 entries: how many times
  the instruction at each address ran (`"count"`, the default) or the cycles it took (`"cycles"`),
  since the start or the last `a8.profileReset()`; `null` when the emulator was built without
  `--enable-monitorprofile`. For finding the routines worth accelerating with `a8.fakeCpuUntil*`
* `a8.xeBank(n)` - bank `n` of the extended (XE) memory as a 16 KB `Uint8Array` (zero copy), or
  `null` when the machine has none. A program that does not use extended memory leaves the banks
  free, and they are saved in state files, so data kept there follows the game's save and load
* `a8.antic.pmbase`, `a8.antic.dmactl` - player/missile base and DMA control
* `a8.gtia.colbk`, `a8.gtia.colpf0`..`colpf3`, `a8.gtia.colpm0`..`colpm3` - GTIA colour registers
* `a8.gtia.hposp0`..`hposp3`, `sizep0`..`sizep3`, `grafp0`..`grafp3`, `prior`, `gractl` - GTIA
  player registers (as last written; a game's interrupts may change them within a frame)
* "Fake CPU" functions and constants for use inside `onCodeInjection`:
  * `a8.OP_RTS`, `a8.OP_NOP` - 6502 opcodes
  * `a8.fakeCpuUntilPc(pc)` - run the CPU (without side effects on the machine) until reaching address `pc`
  * `a8.fakeCpuUntilOp(op)` - run the CPU until reaching opcode `op` (e.g. `a8.OP_RTS`)
  * `a8.fakeCpuUntilAfterOp(op)` - the same, but also execute that opcode (e.g. return from the routine)
  * `a8.fakeCpuWhileIn(lo, hi, maxInstructions = 1000000)` - runs the current instruction and the
    following ones in no emulated time while the PC stays in `lo..hi`, up to the budget; returns the
    number of instructions run, negative when the budget ran out (a loop waiting for an interrupt
    or VCOUNT cannot end this way)
  * `a8.setCodeInjections([addresses])` - replaces, at run time, the addresses `onCodeInjection` is
    called for; with `a8.profile()` this lets a script find a program's hottest code and run
    it in no emulated time without knowing the program (a8ext's `createAccelerator()` does)
* `a8.printFps(value, fg, bg, x, y)` - counts frames (a change of `value` is a new frame)
  and prints the rate on the Atari screen at `x, y`. Typically called from `onPreGlFrame`
* `a8.accelerationDisabled()` - true while CTRL is held
* `a8.recordVideo(path)` starts a video recording into that `.avi` file, as the emulator's "Record
  video" does, and returns whether it started; `a8.stopRecording()` ends it. With the OpenGL display
  and an extension active the video is the display's own picture in true colour, with everything the
  extension draws (see "Recording" below). In the web page the recording is a WebM file through the
  browser's MediaRecorder, saved as a download named after the path when it stops
* `a8.loadSound(path)` - loads a WAV file; the result has a `play()` method.
  The sound is mixed on top of the POKEY output

### `gl` - OpenGL

* Legacy OpenGL calls without the `gl` prefix: `gl.Enable`, `gl.Disable`, `gl.Begin`, `gl.End`,
  `gl.Color4f`, `gl.TexCoord2f`, `gl.Vertex3f`, `gl.Normal3f`, `gl.BlendFunc`, `gl.BindTexture`,
  `gl.TexParameteri`, `gl.MatrixMode`, `gl.PushMatrix`, `gl.PopMatrix`, `gl.LoadIdentity`,
  `gl.Translatef`, `gl.Scalef`, `gl.Rotatef`, `gl.Ortho`, `gl.Frustum`, `gl.Viewport`, `gl.Scissor`,
  `gl.Clear`, `gl.ClearColor`, `gl.Fogf`, `gl.Fogfv(pname, [values])`, `gl.Lightfv(light, pname, [values])`,
  `gl.LineWidth`, `gl.PolygonMode`, `gl.CullFace`, `gl.FrontFace`, `gl.PushAttrib`, `gl.PopAttrib`, `gl.GetIntegerv(pname)` (returns an array)
* Constants without the `GL_` prefix, like WebGL: `gl.TEXTURE_2D`, `gl.BLEND`, `gl.DEPTH_TEST`,
  `gl.QUADS`, `gl.SRC_ALPHA`, `gl.VIEWPORT`, ... (see the `C(...)` list in `video_gl-js.c`)
* The framebuffer is multisampled where the system has it (four samples), so polygon edges are
  smooth; `gl.Disable(gl.MULTISAMPLE)` turns that off, `gl.GetIntegerv(gl.SAMPLES)` tells how many
* Recording: the emulator's video recording used to hold the Atari's screen only (8 bits a pixel
  with the Atari palette), so nothing an extension draws. Now the source can be the display:
  `-vsource auto|atari|display` (`VIDEO_SOURCE` in the configuration file). `display` records
  what the OpenGL display shows, read back after the extensions have drawn, in true colour at the
  size the picture has in the window as the recording starts; `auto`, the default, does so when
  an extension is active at that moment and records the Atari screen otherwise. True colour is
  encoded as Motion-PNG whatever codec is set (the others are 8-bit), with fast compression: about
  8 MB a second at 672 x 480, in real time. The picture lags the sound by one frame
* `gl.createTexture(width, height)` and `gl.loadTextureRGBA(path, width, height)` return a `Texture`:
  * `pixels` - a `Uint8Array` that *is* the RGBA texture memory (4 bytes per pixel)
  * `width`, `height`, `id` (the OpenGL texture name)
  * `finalize()` - uploads `pixels` to OpenGL; call it before drawing and after every change.
    For mipmaps, bind the texture and set `gl.GENERATE_MIPMAP` to `gl.TRUE` before it, then
    choose a `*_MIPMAP_*` minification filter
  * `draw(texL, texR, texT, texB, scrL, scrR, scrT, scrB, z = -2)` - draws the texture on a quad
* `gl.drawTriangles(positions, normals)` - draws `GL_TRIANGLES` from flat `Float32Array`s (x, y, z
  per vertex; `normals` may be omitted) in a single call, for models
* `gl.readPixels(x, y, width, height)` - the framebuffer as a `Uint8Array` of RGBA bytes, rows
  bottom-up, in window pixels; for test scripts that want to look at what was drawn
* `gl.drawScreen(x0, y0, x1, y1, left, right, top, bottom, z = -2)` - draws that region of the
  emulated screen (pixels of the displayed area, y down; `gl.screenSize()` gives its size) onto
  a rectangle in GL coordinates, with linear filtering: for rearranging the game's screen

### The extension object

Each `init.js` exports the extension object as its default export (`export default { ... }`)
with these properties. Hook methods are called with `this` bound to that object, so it
doubles as the extension's state:

* `name` - shown in the extensions menu
* `fingerprint: { address, bytes }` - the extension is activated when the bytes at `address`
  in Atari memory equal `bytes`. It stays active while they do: once they have been gone for
  50 frames (a program may hide them for a moment, a bank switched out), the extension is
  deactivated, and comes back by itself when its program does
* `onActivate()` (optional) - called once the fingerprint matched, with the program in memory
* `onFrame()` (optional) - called once per Atari frame, with or without OpenGL (headless runs too)
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

    A8_EXT_SELECT=SELF-TEST build/src/atari800 -nobasic data/ext/selftest/selftest.xex

Input can be scripted with atari800's `-playback file` (`-playbacknoexit` keeps running at
the end). The file is plain text: the line `Atari800 event recording, version: 1`, one line
with the POKEY random seed (`0`), then per frame eight lines: `key shift consol` (`-1 0 7`
for nothing), the ports 0/1 and 2/3 joystick bytes (`255` centred; stick 0 forward is `254`,
right `247`), four trigger lines (`1` = released) and a screen checksum (`00000000`, the
mismatch is only logged). This is how walks through a game can be replayed exactly.

Tests that do not need the OpenGL view can run without a window: SDL's dummy drivers
(`SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`) with `-no-video-accel` give a software video mode
and no display at all. Only `onFrame` and `onCodeInjection` run then (the GL hooks hang off the
OpenGL frame), and a script can still read the screen memory through the display list to check
what the game shows.

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

## In the browser

The same scripts run in a web page: [web/](../../web/README.md) builds the emulator core as a
WebAssembly module and a page that hosts the extensions, with `a8` made of the module's memory
and `gl` implemented on WebGL 2. An extension needs nothing special for it, as long as it keeps
to the API above: the files it reads must be in its own directory (they are fetched when it is
activated), and what it draws must go through `gl`.

## The self-test extension

[selftest/](selftest/init.js) is an extension for a 22-byte Atari program of its own
(`selftest.xex`: a loop that calls a routine filling a page of memory). It uses nearly every
call of the API on that program, checks what comes back, prints a line per check on the
console and shows the result on the screen:

    A8_EXT_SELECT=SELF-TEST build/src/atari800 -nobasic data/ext/selftest/selftest.xex

It checks the memory view, the hardware-aware peek and poke, the register getters, the palette,
the extension's own files, code injections with every kind of answer (let the code run, skip
it, run it on the fake CPU in each of its four ways), changing the injections at run time, and
then the drawing: quads, textures from pixels and from a file, blending, the matrix stacks,
fog, scissor and the attribute stack, `drawTriangles`, `drawScreen` and lines, each by reading
the framebuffer back. Without an OpenGL display (the dummy drivers above) the drawing checks
are skipped and said so. The same extension runs in the web build, where the page offers it,
so it tests the browser's `a8` and `gl` as well.

It is also the example to start from: a fingerprint, a menu, all the hooks, files found
through `a8.extDir`.
