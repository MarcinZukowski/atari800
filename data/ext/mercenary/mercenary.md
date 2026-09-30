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
