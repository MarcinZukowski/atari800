# Numen - technical notes

Numen (Taquart, 2003), a demo with software-rendered 3D scenes, loaded from
the saved states numen.a8s (the forest) and n2.a8s (the maze). Facts from RAM
dumps of those states, the emulator's profile, disassembly of the engine and
its own numbers logged while it ran; numbers with `$` are hex. *Measured*
marks what was checked against the running demo, *read* what comes from the
code alone.

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

## The 3D engine: sectors

The scenes are not polygons on a plane but a sector engine of the Doom kind,
in the bank at `$4000`-`$7FFF`. A level is a set of polygonal sectors, each
with a floor and a ceiling height; where two neighbours differ in height a
wall shows, and sprites stand in the sectors. Each picture is drawn front to
back from the camera's sector through the edges into the neighbours, with
per-column records of what is still open (`$3000` top, `$3050` bottom).

The main loop at `$6B2E`: `JSR $7D97` (the level's own hook), `JSR $6A43`
(which sector the camera is in: the point-in-polygon test at `$69D7`, the
current sector first, then its neighbours, then all), `JSR $6440` (the
sectors), `JSR $68DE` (the sprites), the buffer flip, then the camera script
(`$6B47`-`$6C74`, the script from `$6E01`), which advances by the frames
that passed (`$EB`): the camera moves in real time, and a slow renderer only
samples it less often.

### The level's tables

Per level (*read*, values *measured* in both levels):

| Address | What |
|---|---|
| `$7D80` | number of sectors (forest 24, maze 20) |
| `$7D82` | number of sprites (forest 14, maze 0) |
| `$7D83`-`$7D89` | the camera at the start: x, z (16 bits each), eye height, heading, sector |
| `$7D8A`-`$7D93` | copied to `$D012`-`$D01B`: the nine colour registers and PRIOR |
| `$6E00` | the horizon row at the start, copied into the operand at `$552B` |

Per sector `s`:

| Address | What |
|---|---|
| `$7000,s` | flags; bit 3: open to the sky (no ceiling is drawn, the backdrop shows) |
| `$70C0,s` | its first vertex; the next entry is where its vertices end |
| `$7100,s` | ceiling height |
| `$7140,s` | floor height |
| `$7180,s` | ceiling colour (used by `$5A6B`, sectors not open to the sky) |
| `$71C0,s` | floor colour (used by `$5BC7`) |
| `$7200,s` | flags, bits 0 and 1: not understood |

Per vertex `v`, which also stands for the edge from it to the next vertex.
Every sector has its own vertices; shared corners are stored twice:

| Address | What |
|---|---|
| `$7300,v` `$7400,v` | x, low and high |
| `$7500,v` `$7600,v` | z, low and high |
| `$7700,v` | the next vertex of the outline. An outline can be several loops: the maze's first sector has an outer loop and two holes, its pillars |
| `$7800,v` | the same edge in the neighbour's list (`$FF`: none) |
| `$7900,v` | the sector on the other side (`$FF`: none, an outer edge) |
| `$7A00,v` | the colour of the wall on this edge |

Heights grow downwards: the eye is at 128, a floor at 160, the forest's
"ceiling" (where the sky's walls end) at 88 or 72, raised floors at 136-156.
A sector with floor = ceiling is a solid block (the forest's houses, the
maze's blocks).

The walls on an edge (*read* at `$61ED`-`$6411`, *measured* by redrawing):
no neighbour: one wall from floor to ceiling; a neighbour with a higher floor:
a wall up to that floor; a neighbour with a lower ceiling: a wall down to that
ceiling, unless both sectors are open to the sky. An outer edge of colour 0
gets no wall: its columns are filled with the floor's colour up to the horizon
row and with the backdrop above it, as if the floor went on for ever
(*measured*: the picture's columns there equal the backdrop down to the
horizon row exactly, and the floor's colour below).

### Colours

A colour number indexes two tables of 52 bytes: `$4C40` for even bytes of
the screen and `$4C74` (the same pairs swapped) for odd ones. Numbers 0-15 are
the sixteen plain pixel values (`$00`, `$11`, ... `$FF`), 16-51 are pairs of
two different pixels: the dithers. What a pixel value is depends on the level's
PRIOR: the forest is GTIA mode 10 (`$81`: values 0-3 the player colours, 4-7
the playfields, 8-11 the background), the maze mode 9 (`$41`: sixteen
luminances of the background's hue, `$A0`).

### The camera and the projection

Zero page: x at `$90`/`$91`, z at `$92`/`$93`, eye height `$94`, heading
`$95` (256 to the turn), sector `$96`. The horizon row is the operand at
`$552B` (`ADC #row` in the height projection `$54F1`): the script tilts the
view by changing it (between 16 and 34 while the forest scene opens, 24
afterwards).

The transform at `$52EF` (quarter-square multiplies through the tables set
up for the heading by `$52E1`, sine and cosine at `$4700`/`$4740`), fitted to
140 sprite records logged from the running demo with a mean error of 12 units:

    across = -(x - cx) * sin(heading) + (z - cz) * cos(heading)
    depth  =  (x - cx) * cos(heading) + (z - cz) * sin(heading)

and the projection (*measured* from the same records; the engine's
reciprocals get coarse far away):

    column = 40 + 38 * across / depth                    (of 80)
    row    = horizon + 150 * (height - eye) / depth      (of 48)
    sprite: half its width in columns = 38 * halfwidth / depth,
            its height in rows        = 75 * height / depth

So one unit of height is as large on the screen as four units of ground, and
a sprite's height counts double.

### The sprites

Per sprite `o`: `$7B00` flags (bit 1: the anchor is its middle, not its
foot), `$7B40`/`$7B80` x, `$7BC0`/`$7C00` z, `$7C40` the height it stands at,
`$7C80` its sector (it is drawn only when that sector was), `$7CC0` its type,
`$7D00` half its width, `$7D40` its height. They are sorted by depth
(`$6959`-`$69B5`) and drawn far to near by `$6611`.

Per type `t`: `$7E80`/`$7EA0` the address of a list of column pointers,
`$7EC0` how many columns, `$7EE0` how many rows. A column is one byte per
row: 0 for nothing, else the pixel value *inverted* in both nibbles (the
drawing code at `$3F00`, one unrolled block of 16 bytes per screen row, does
`LDY #row : LDA (col),Y : BEQ skip : EOR screen : ORA mask : EOR (col),Y :
STA screen`, which leaves the complement in the masked nibble). `$3800`
steps down the sprite and writes the row numbers into those `LDY` operands.
The lists of two types can overlap, and columns are shared: the forest's
types 3 and 4 are a figure and its mirror image from the same 15 columns.
The forest: type 0 a pine (20 x 29), 1 and 2 leaf trees (25 x 32, 25 x 28),
3 and 4 the figures (15 x 20).

### The backdrop

A tile 16 bytes (32 pixels) wide and 80 rows tall: sky, clouds, hills. Row
`r` of it starts at the address in `$7DA0,r` (low) and `$7E00,r` (high). The
picture's row `y` shows tile row `y + $2640[horizon]` (the table is
32.67 - 2/3 horizon, rounded down: the backdrop moves two rows for three of
the horizon), and the tile is shifted by `$2500[heading]` bytes (163.5 bytes,
ten tiles, to the turn). `$5846`-`$590A` copies it into the spans left open
above the sky sectors. *Measured*: the picture's columns equal this down to
where the ground begins.

## The 3D scene in OpenGL

world3d.js reads those tables (once, and again when a sum over them
changes: another level) and draws the level with the demo's camera at the
window's resolution: floors and ceilings as triangles (ear clipping, holes
bridged to the outer loop), walls as quads, the floor carried on beyond open
outer edges as a fan from the camera through each edge (the columns the demo
fills), sprites as upright cards facing the camera with their pictures scaled
up with Scale2x, the backdrop as a scrolling layer behind. A dither is drawn
as the mix of its two colours. The projection is the demo's: a frustum with
the eye level at the horizon row, heights scaled by four.

The camera glides: the demo gives a new position a few times a second (some
thirty with the acceleration), and the view moves from each to the next over
the time the last step took, one step behind.

"Shading and fog" adds what the demo has none of: walls lit by how they face
a light from the side and darker at the foot, round shadows under the
sprites, a light fog (the colour of a paled sky outdoors, with a haze over
the far hills; black indoors).

Three more options, each on its own:

* "Whole picture": the view fills the emulator's whole picture instead of
  the demo's 320 x 192 rectangle, at the same scale, so it shows two columns
  more on each side and six rows more above and below (the frustum and the
  backdrop are extended; beyond the backdrop's tile its first and last rows
  repeat).
* "Ground texture": every colour number becomes a texture of 128 x 128, tiled
  every 256 units of ground: a grain (value noise in three sizes, the same on
  every run) over the colour, and for a dither a checker of its two colours
  with squares of 64 units, so the maze's floor and the forest's bridge come
  out tiled as the demo's dithers suggest. Floors and ceilings take it by
  their x and z, walls along their length and height.
* "Smooth edges": multisampling. The emulator asks for a framebuffer with
  four samples when extensions are built in (video_gl.c, with a fallback to
  none), and the option switches `gl.MULTISAMPLE`. *Measured*: `gl.SAMPLES`
  reads 4 on this machine.

*Measured*: drawn from the same camera, the trees, the road, the houses, the
bridge and the figures land where the demo draws them (compared frame by
frame at five points of the forest's path); the maze matches by eye.

## Open questions

* `$7200,s` bits 0 and 1, and `$7000,s` bits other than 3.
* Whether an edge of colour 0 between two sectors of different heights
  occurs, and what the demo draws there (here: colour 0 as it is).
* Whether the demo draws anything over the scene (text, sprites of the
  hardware kind): the OpenGL view would cover it.
* Other levels: only the forest and the maze were seen. The reader checks
  the engine's code and the tables' shape, and falls back to the demo's own
  picture when they do not fit.

## How this was found

* The sprite format from the drawing code: `($D4),Y` is the list of column
  pointers, the unrolled code reads `($D8),Y` per row. Decoded, the pictures
  are trees.
* The camera and projection by logging, at the engine's own routines (code
  injections at `$6611`, `$6722`, `$6143`), each sprite's transformed
  position and screen rectangle for every rendered picture, and fitting.
  Injections do not fire inside accelerated ranges: the acceleration was off
  for that.
* The model by drawing it: the rebuild next to the demo's picture from the
  same camera. A screenshot taken in the frame of the buffer flip still
  shows the previous picture; the one after shows the new.
* The backdrop from its drawing code, then checked column by column
  against screenshots.
