# Numen - technical notes

Numen (Taquart, 2003), a demo with software-rendered 3D scenes, loaded from
the saved state numen.a8s (the forest scene). Facts from a RAM dump of that
state and the emulator's profile; numbers with `$` are hex.

## The screen

* Display list at `$1F80`: 24 blank lines with a DLI, then a mode-F row with
  LMS (`$6F lo hi`) and VSCROL, followed by pairs `$8F $2F`: mode F with a
  DLI every other line and the VSCROL bit on the others. The DLI at
  `$1FBB` does `WSYNC`, VSCROL = `$0D`, VSCROL = 3 and returns: the
  vertical-scroll trick that repeats lines, the demo's way of getting its
  colour depth out of mode F.
* Two frame buffers: the LMS operand at `$1F84`/`$1F85` flips between `$1000`
  and `$1800`. Counting the flips gives the rendered frame rate: about 3 a
  second as the demo stands.
* The OS ROM is off; the NMI vector points at `$FF83` (RAM under the OS),
  the extension's fingerprint. The machine has extended memory, and the
  demo banks it in at `$4000`-`$7FFF`.

## The profile

Over 400 frames of the forest scene the profile (`a8.profile()`) puts 40%
of all cycles in three ranges and 75% in ten: `$4D04`-`$4EEF` (15%),
`$5787`-`$5916` (13%), `$7765`-`$77BB` (12%, a loop that polls: it waits),
`$1FB8`-`$1FC9` (7%, the DLI handler), `$54C5`-`$5604`, `$5CEC`-`$6067`,
`$52E1`-`$53D7`, `$66BD`-`$6A46`, `$5966`-`$5A66`, and unrolled code at
`$3B00`-`$3DC0` and `$3F00`-`$41C0`. Most of it lies in the banked window,
and some of it is generated at run time (a dump taken later shows zeros
where `$7765` ran), so what the hot code is was not read; the accelerator
does not need to know.

## The acceleration

init.js uses `createAccelerator()` from common.js: it profiles 150 frames,
takes the hottest ranges (at least 0.1% of the cycles each, up to 48),
leaves out ranges with an executed RTI or a WSYNC store, and from then on
runs any of them in no emulated time whenever execution enters it, with a
budget of two million instructions after which a range is dropped for good
(the polling loop is dropped this way at once). The profile is repeated
every 1500 frames. *Measured*: the forest scene renders 24-33 frames a
second instead of 3 (11-18 with the 16 hottest ranges only), and since the
demo advances by rendered frames, its parts go by faster too. "Log frame rate" in the menu prints the rate.

## The picture, smoothed

PRIOR is `$81`: GTIA mode 10, nine colours, pixels two colour clocks (four
hi-res pixels) wide; the display list's VSCROL trick stretches each of the 48
mode-F rows to four lines, so the scene is 80 x 48 pixels of 4 x 4 from
screen line 24 (*measured*: every 4-line block is uniform, horizontal runs
are multiples of four pixels). The shared smoother (../smooth2d.js) reads
that grid back from the framebuffer, upscales it with Scale2x twice and draws
it over the scene while the display list is `$1F80` with a buffer at `$1000`
or `$1800`. "Smooth picture" in the menu.
