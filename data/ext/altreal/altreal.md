


# Top frequencies

  freq  from .. to
 55180  4a69 .. 4a6d
 55180  4a78 .. 4a7f
 18218  0090 .. 00ae
 18218  00b4 .. 00d4
 17239  00af .. 00b3
 13792  3884 .. 388a
 13792  38a6 .. 38a8
  5184  7875 .. 7880
  1856  7f31 .. 7f38
  1690  f9ac .. f9b0
  1521  f9b1 .. f9b3
  1248  7a25 .. 7a2b
  1248  7a33 .. 7a35
   864  788c .. 78a0
   862  1806 .. 1808
   862  1821 .. 1823

# Movement and rendering

Found while looking into why walking is drawn in a few big steps.

* the view is a character-mode picture: 7856 copies 72 precomputed 18-byte
  cell patterns (from 8afd/8b45/7fc5/800d, X = cell) into the font at 0858+;
  0090-00d5 is the zero-page drawing routine that produces them. One redraw
  costs 20-30 frames of 6502 time, so the game only manages 2-3 per second
  while walking; with acceleration the main loop runs at about 30 passes/s
* 6313/6314/6315 - cell x, y, level; 6312 - facing (0-3)
* 6316/6317 - position inside the cell on a 36-unit grid (11 = centre); the
  perpendicular axis is reset to 11 on every step
* 6383 - step size in units, from the character's speed: table 445f (6..15)
  indexed by 6379 >> 4, halved when 6398 is negative, at least 4, and 2 when
  6394 >= e0 (4430-445e). 36/7 gives the "five steps per cell"
* 30e5 - movement handler: 2e bit 0 forward, bit 1 back, bit 2 turn one way,
  otherwise turn the other way (masks 3b74-3b76); the step is added at
  3140/314a, and crossing 36 reads the map cell through (10) and dispatches
  on the wall type via 3b24/3b34
* 7b0d - the renderer reads the sub-cell position as a plain 0..35 offset, so
  it draws any position; nothing limits the step to the five positions
* 262f - packs the joystick and trigger into 2e (1 = pressed, bit 7 = trigger);
  the main loop fetches it through 1821 and tests A & 0f, so gating it means
  changing both 2e and A. 2643/30/31 - keyboard command latch (IRQ at 2653)
* 3884-38ce - monsters at the player's cell -> 1938; 4a69-4a82 - per-pass loop
  over 64 monsters

"Smooth walking" in init.js uses this: with the drawing accelerated, the
joystick is let through so that the character covers 17.5 * (game step / 7)
units a second, the speed the game intended, times the "Walking speed" factor
(1, 1.5, 2 or 3). The step is one unit while that fits in 28 moves a second,
which is what the accelerated loop can redraw, and grows to 2 or 3 units above
that; each move is one redraw. Measured: 124 frames per cell at 1x, 83 at
1.5x, 62 at 2x, 42 at 3x.

# Map and view

* the level is 32 x 32 cells of 4 bytes at b000 + y*128 + x*4 (3531 builds the
  address): byte 0 = north (low nibble) and east (high) walls, byte 1 = south
  (low) and west (high), byte 2 = cell type, byte 3 = flags (bit 7: special,
  low 5 bits an index kept in 195a). Wall nibbles: 0 open, 13 wall, 1 arched
  passage, 3/5/6 doors (the movement handlers at 330f/3307 pass through with a
  message), 8-10 locked doors (3344-334c, need bit 1 of 6388), 11-15 solid
  (32da clamps the position at the wall)
* facing 0 decreases y, 1 increases x, 2 increases y, 3 decreases x (tables
  3b44/3b54). Seen from inside the game the world is mirrored relative to a
  right-handed map: facing 3, what the nibbles call north is on the left
* the picture is the middle 18 text columns of the nine mode-4 rows (display
  list 19be): 144 colour clocks by 72 lines starting at scanline 73, a 2:1
  window that the game draws as if it were square (mode-4 pixels are 2:1).
  The rows use three font banks (0800/0c00/1000, switched by the DLIs at
  1b2f) and the DLIs also change the colours per row group (table 1c19).
  The 11 columns on each side are solid frame; the right side also holds the
  compass
* walls are not textured: 0090 fills them with dither patterns from the
  256-byte table at 8e00, chosen by a366 and the row

view3d.js redraws the maze with OpenGL from this: textured walls, doors,
floor and ceiling in a frustum of 72 x 80 degrees over the picture rectangle,
with fog, side shading and interpolation of steps and turns. It stays out of
the way when the display list is not the maze or a monster is in the cell
(1938), since the game draws monsters into its own picture.

# Capturing the game's wall art

Right against a wall the game paints the whole picture in one flat colour,
so the nearest wall carries no pattern; half a cell back (position 17) the
wall ahead is drawn at full size as cobblestones with red brick edging, and
farther walls shrink. wall-cobble.rgba is that picture, cut to the wall face
and sampled at one texel per mode-4 pixel (58 x 52), taken from the
framebuffer with gl.readPixels(). The "Capture texture" menu entry does the
same in play: stand half a cell from a wall or door, facing it, choose
"wall" or "door", and view3d.js reloads with the file.
