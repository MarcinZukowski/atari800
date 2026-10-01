# Interesting addresses

* 4dd8 - ??? but takes a while, and is called often
* 4fde - ??? but expensive
* 342c - ditto
* 4e59 - ditto
* 4e12 - ditto (overlaps)

* 4185 - calls the actual line routine

Lines:

* 5230 - line, X right major, Y down minor, ADC delta
* 5222 - loop
* 525d - line, Y down major, X rigth minor, ADC delta
* 524f - loop
* 528A - line, Y down major, X left minor, ADC delta
* 527C - loop for above
* 52b7 - line, X left major, Y down minor, SBC delta
* 52a9 - loop
* 52E4 - line, X left major, Y up minor, SBC  delta
* 52d6 - loop for above
* 5311 - line, Y up major, X left minor, ADC delta
* 5303 - loop for above
* 533e - line, Y up major, X right minor, SBC delta
* 5330 - loop for above
* 536b - line, X right major, Y up minor, ADC delta
* 535d - loop
* 538a - pixel

Also:
* 53b7 - code that sets either ORA or AND inside the line code


Other:
* 570e - two-color fill-the-line
* A/07 - switch point
* 80 - color left
* 81 - color right
* 18 - #lines
* 7b - positive/negative between-line increment
* 06,07 - X switch point lo/hi
* 79,7a - X switch delta lo/hi
* 7b - delta direction, 0-positive, 1-negative (see 5832)
* 5823 - end of drawing loop per single line

* 586f - fill-the-line routine

* line routines: X, Y input X, Y resp.

Memory:
* 2600 - screen lines LO
* 2000 - screen lines HI
* 26a0 - div-by-4 (0,0,0,0,1,1,1,1,2,2,2,2) - X pos to byte
* 23 - screen memory HI (0x80 or 0xE0)
* 04 - current Y
* 06 - X/Y lo
* 64 - X/Y delta
* 68 - start X
* 69 - start Y
* 6a - max X ?
* 6b - max Y ?
* 8010 - screen memory 1
* e010 - screen memory 2

# The 3D pipeline (Atari)

Found by matching the Atari code against the C64 version's analysis at
https://github.com/gamesexplained/gamesexplained (games/c64/mercenary); the
two ports share the engine, with the Atari zero page two bytes lower.

Numbers:
* floats are two bytes: mantissa m, and an exponent byte whose bits 2-7 are a
  signed power of two e and bit 0 the sign: +-(1 + m/256) * 2^e. There is no zero.
* 2200 - 256 * log2(1 + i/256), 2300 - 256 * (2^(i/256) - 1): the multiply and
  divide work by adding and subtracting logs (about 0.4% error)
* 4dd8 - fp_mul, 4e12 - fp_div, 4e59 - fp_add, 4fde - 24-bit int to float
  (A = high, X = mid, 05 = low), 4eed/4eec - sin/cos of a 10-bit angle, 4f45 - float to 8 bits
* positions are 24-bit integers, 65536 units per city square; angles 1024 per turn

View:
* 70/71/72 - eye X (lo/mid/hi), 73/74/75 - eye height, 76/77/78 - eye Y
* 26/27 - roll, 28/29 - pitch (512 = level), 2a/2b - heading (0 = north, toward falling Y)
* 492c - view_trig: sin/cos/tan of the angles as floats (heading sin 3a, cos 3c)
* 4708 - view_matrix: rows 3e/40/42 (x'), 44/46/48 (y'), 4a/4c/4e (depth)
* 8c/8d - projection centre, 1f - focal exponent ($18 = 64), f1 - mirror flag
* a6/a7 - on foot / underground: the heading-only transform at 4cca is used instead

Vertices (all end in floats 50/51 = X, 52/53 = height, 54/55 = Y relative to the eye):
* 49b6 - vertex_rel: vertex 17 of the location from the 24-bit tables
  1d00/1d40/9f40 (X lo/mid/hi), 1d80/1dc0/9f80 (height), 1e00/1e40/9fc0 (Y)
* 4a07 - model_vertex_rel: object vertex, 3 signed bytes at (1b),ab in units of 16,
  oriented by 4a95 into 12-bit offsets cf/d0, d1/d2, d3/d4 (+2048), added to the
  object's eye-relative position d5-d7, d8-da, db-dd (lowered by 2048 at 3bfb)
* 4a63 - square_centre_rel: centre of city square A (row*16 + column)

Projection and edges:
* 4b44 - project_vertex into slot 17: flags 9f00,Y (bit 7 behind, bit 0 X off,
  bit 1 Y off), screen x 9e80,Y, screen y 9ec0,Y;
  x = 8c + 64 * x'/z, y = 8d + 128 * y'/z (window 160 x 152)
* 3b49 - projects the location's vertices (count 6e), 3b58 - draws its edges
  (count 6f) from the tables 1e80 (from slot) / 1ec0 (to slot), pen split at 96
* 3b99 - draw_object X: model pointer 6800,X/6840,X; vertices then edges, an
  edge byte n: below $80 n+1 one-byte edges (two 4-bit slots), else (n & $7f)+1
  two-byte edges
* 3e4e - line set-up for the edge between slots X and Y: clipping, octant
  choice, then one of the eight line routines
* 53b7/53bd - switch the line routines between AND (white) and ORA (ground marks)

# The view's picture

* a1-a4 - the colours of the view's four pixel values (shadows of the colour
  registers for the 3D window; the hardware registers hold the dashboard's
  by the end of the frame): a1 value 0, the structures' white; a2 value 1,
  the sky; a3 value 2, the ground; a4 value 3, the ground marks
* Outdoors every frame fills the window row by row: 586f with $55 (sky) for
  the rows above the horizon and $AA (ground) below, 570e for the rows the
  horizon crosses, each with one switch point. On foot the horizon is the
  boundary of rows 74 and 75, a row and a half above where the ground's
  vanishing line projects (76.5)
* The lines' two pens: AND clears a pixel to value 0 (white); ORA sets its low
  bit, which turns ground (2) into mark (3) and leaves sky (1) as it is: a
  mark shows on the ground only, the horizon cuts it without any geometry
* In an interior (a6 not zero) nothing is filled per row: the whole window
  is value 2 and the room's edges are ORA lines, so they show everywhere
* 3b58 - the location's edges, from index 6f down to 0: ORA pen first, AND
  pen from index 96 on, so edges above 96 are marks and the rest structure
  (an interior has 96 above 6f: all marks)
* 538a - plots one white pixel at slot X's projection when its flags are
  zero: a far object is drawn as a dot

# The scene in OpenGL (view3d.js)

With "Line drawing mode: OpenGL" and "GL line type: Line" the 3D window is
drawn as a scene. The vertices are kept in view space as the game projects
them, and a frustum reproduces its projection (x = centre + 64 x'/z,
y = centre + 128 y'/z, a pixel's coordinate being its middle), so everything
else can be real geometry. Options, each on its own:

* Sky and ground: the window is cleared to the sky's colour and the ground is
  a plane at height 0 drawn as rings around the point under the eye, each twice
  as wide as the last, out past the far plane: its edge is the horizon,
  exact at any roll and pitch. An interior is cleared to its one colour.
  Far-object dots are drawn again from the slots
* Line width by distance: an edge is a ribbon facing the eye, 22 world units
  wide, kept between 1.6 and 4.2 window pixels; marks are cut at the eye's
  level, which is the horizon on the screen, as the ORA pen cuts them
* Faces: a location's faces come from its whole model, read from the edge
  and vertex tables when its edge loop starts, not from the edges that
  happen to be drawn; an interior's room is drawn solid in the room's colour
* Textures: one tiling noise texture multiplies the ground at four sizes
  (512 to 2 million units a tile) and the faces at one; far away it blurs
  to its mean and does nothing
* Fog and lighting: exponential fog (toward a paled sky outdoors, toward the
  dark indoors), a haze band over the horizon and a darker zenith, faces lit
  by a fixed sun, floors darker and ceilings lighter than walls

*Measured*: on the five saved states (surface on foot, in flight with roll,
three interiors) the scene lines up with the game's own picture; drawing it
takes 1-2 ms a frame.
