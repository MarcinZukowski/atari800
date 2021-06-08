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

Additionally, I had plans of providing scripting support:

*  There's a skeleton of Lua scripting support, allowing most of this functionality from external scripts.
* Alas, after playing with Lua for a while, I'm not in love with the language nor the embedding mechanisms, and I gave up on it for now.
* I might come back to Lua, or try V8 for example.

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

* Yoomp: [ext-yoomp.c](../../src/ext/ext-yoomp.c)
  * various 3D balls
  * one high-res background
* Mercenary: [ext-mercenary.c](../../src/ext/ext-mercenary.c), [mercenary.md](mercenary.md)
  * accelerated Atari-like line drawing
  * OpenGL-based line drawing (3 types)
* Zybex: [ext-zybex.c](../../src/ext/ext-zybex.c)], [zybex.md](zybex/zybex.md)
  * scrolling background (grayscale and color modes)
* Behind Jaggi Lines: [ext-bjl.c](../../src/ext/ext-bjl.c)]
  * faster rendering
* Alternate Reality: [ext-altreal.c](../../src/ext/ext-altreal.c), [altreal.md](altreal.md)
  * faster rendering
* River Raid: [ext-river-raid.c](../../src/ext/ext-river-raid.c), [river-raid.md](river-raid.md)

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

## Per-game notes

Various per-game notes are stored in `GAME.md` files under `data/ext`, e.g.:
* [Alternate Reality](altreal.md)
* [Mercenary](mercenary.md)
* [Zybex](zybex/zybex.md)

And of course, sources under `src/ext/*.c` have a lot of info.
