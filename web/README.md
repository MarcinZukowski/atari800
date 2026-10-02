# atari800 in the browser, with the game extensions

A static web page that runs the emulator and the game extensions of
[data/ext](../data/ext/README.md): the same extension scripts as the native
build, unchanged.

## How it is put together

* **The emulator** is the core compiled to WebAssembly with Emscripten, with
  its own small platform layer, [src/web/web.c](../src/web/web.c): no SDL, no
  built-in user interface. The page calls `web_frame()` once per emulated
  frame and reads the screen, the palette and the sound out of the module's
  memory. [src/web/config.h](../src/web/config.h) is its feature set.
* **The extensions run in the page**, by the browser's own JavaScript engine
  instead of QuickJS. Their two globals are made here:
  * `a8` ([api.js](api.js)): `a8.mem` is a view over the emulated RAM inside
    the module's memory, the registers and the fake CPU are the functions
    `web.c` exports, and `onCodeInjection` is called from the CPU loop through
    one imported function.
  * `gl` ([glshim.js](glshim.js)): the fixed-function calls of the native
    binding (immediate mode, matrix stacks, one texture modulated by the
    colour, blending, fog, scissor, the attribute stack) on WebGL 2, with one
    shader. Each `Begin`/`End` pair is one draw call.
* `a8.host` is `"web"` here, and `a8.panel` is an element beside the menu that the active
  extension may fill with controls of its own.
* `std` and `os`, which a few scripts import for files, are small modules
  ([std.js](std.js), [os.js](os.js)) reached through the page's import map.
  An extension's data files are fetched when its fingerprint is found, before
  it is activated, so the scripts read them without waiting, as natively.
  Files a script writes (Alternate Reality's maps) go to the browser's local
  storage.
* [host.js](host.js) ties it together: the frame loop (paced by the clock at
  the Atari's frame rate, not by the display), the picture, the sound (Web
  Audio, a buffer per frame), the keyboard, loading programs, and the
  extension's menu as a panel beside the picture.

## Building and running

    brew install emscripten        # once
    cd web
    make                           # builds dist/
    make serve                     # http://localhost:8800

`dist/` is the whole site: static files, no server code.

The page starts with the built-in Altirra OS and no program. Drop a file on
it or choose one: a saved state (`.a8s`), a disk image, an executable, or a
cartridge image. The extension whose fingerprint matches the program in
memory is activated by itself.

Address parameters:

| Parameter | Meaning |
|---|---|
| `state=URL` | load this saved state at the start |
| `file=URL` | boot this disk, program or cartridge image |
| `ext=NAME` | only activate the extension with this directory name or part of its name |
| `menu=KEY:2,OTHER:0` | preset entries of the extension's menu |

If `dist/demos.json` exists, its entries are listed as links:

    [{ "title": "Numen: the forest", "state": "numen.a8s" },
     { "title": "Mercenary, lines only", "state": "m1.a8s", "menu": "SCENERY:0" }]

Keys: arrows and right Ctrl are the joystick, F2/F3/F4 Option/Select/Start,
F5 reset (with Shift a cold start), F6 Help, F7 Break. Holding left Ctrl
turns the extensions off for as long as it is held, to compare with the
program as it is.

The page fills the window: the picture, then two bars, one for the page's own
controls and one for the active extension (its name, its menu, with a choice
of two shown as a pair of buttons, and its panel). "Full screen" gives the
picture the whole screen with the bars lying over it; they fade out after
three seconds without the mouse moving or a key other than the Atari's, and
come back when either happens.

## Putting it online

Any static host works: copy `dist/` there, with the programs to show and a
`demos.json` beside `index.html`. For GitHub Pages, publish the contents of
`dist/` (for example on a `gh-pages` branch).

What to publish is a question of rights, not of technique: a saved state
holds the whole memory of the machine, the game and the operating system ROM
it was saved with included. Numen and Yoomp! are free releases; the
commercial games are not.

## Limits

* No emulator menu: machine type and options are fixed (a 320 KB XE with the
  Altirra OS; a saved state brings its own machine and ROMs).
* The `gl` shim covers what the binding offers and the extensions use.
  Lighting is not implemented (no extension turns it on), and a line's width
  is emulated with quads because WebGL draws lines one pixel wide.
* The scripts' `a8.printFps` shows its count beside the title instead of
  inside the Atari's picture, and `a8.recordVideo` does nothing.

## Testing

Tested with headless Chrome on the saved states of all eight extensions
(pictures compared with the native build by eye). For tests the page takes
`frames=N` (run N frames at once before the clock takes over), `trace` (log
the draw calls of the last of those frames) and `report` (send every log
line to the server as a request, so that it shows in the access log). Sound
and the keyboard were not tried by hand.
