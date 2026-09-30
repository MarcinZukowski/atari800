# Alternate Reality: The Dungeon - technical notes

What the extension in this directory knows about the game, and where each
fact comes from. Unless marked otherwise a fact was read from a RAM dump
taken during play (ar.a8s: level 1, the starting corridor, the character
"Eru") and the disassembly of that dump; *measured* facts come from probes
run as code injections or from screenshots. Numbers with `$` are hex, others
decimal. A "row" is a text-mode row of eight scanlines, a "line" a scanline;
scanlines are counted as the emulator does (the first displayed one is 8,
screen line 0).

## The screen

* Display list at `$19BE` while the maze is shown (the extension uses that
  address to know): three blank instructions (20 lines), six mode-2 rows
  from `$0400` with interrupt bits after rows 1-4 and 6, nine mode-4 rows
  from `$04F0` with interrupt bits on rows 3, 4, 6 and 9, then mode-2 rows
  from `$0658` for the inventory and messages.
* The picture is the middle 18 columns (11-28) of the nine mode-4 rows:
  144 colour clocks by 72 lines, scanlines 81-152 (screen lines 73-144), a
  2:1 window that the game draws as if it were square: mode-4 pixels are
  twice as wide as tall. The 11 columns on each side are frame; the right
  side also holds the compass.
* `$1C09` numbers the characters of the picture rows: 0-119 in each group of
  three rows (`$04F0`, `$0568`, `$05E0`). With a different font for each
  group every character cell of the picture has its own eight bytes of font:
  column i of rows 1-3 is character 11+i, 51+i or 91+i of the font at
  `$0800`, rows 4-6 the same characters of `$0C00`, rows 7-9 of `$1000`.
* The interrupt chain, *measured* by scanline (VCOUNT read at each handler,
  so ±1). The vertical blank code at `$2394` resets the counter `$18B8` and
  the vector, `$237E`:
  * `$1B2F` at lines 36, 44, 54, 62: COLPF2 of text rows 2-5 from `$1C19`
    (`$46 $68 $88 $B6`: the coloured bands of the status area)
  * `$1B56` at 80, the blank line before the picture: COLPF0-2 from
    `$18BA`-`$18BC`, COLPF3 from `$18BF`, COLBK from `$18BD`, CHBASE `$08`;
    unless `$7600` is 1 it also places players 2 and 3 (`$90`, `$34`) with
    solid shapes in players 0 and 1
  * `$1BA2` at 104 (end of picture row 3): CHBASE `$0C`
  * `$1BB7` at 112 (end of row 4): waits four lines, then COLBK from
    `$18BE`: the background is the ceiling colour in the upper half of the
    picture and the floor colour in the lower half, split at the middle
  * `$1BD6` at 128 (end of row 6): CHBASE `$10`
  * `$1BEB` at 152 (end of the picture): CHBASE `$14`, COLPF2 and COLBK
    black, COLPF1 `$0E`, players off: the text below
* Mode-4 pixel values in the picture: 0 background, 1 COLPF0, 2 COLPF1,
  3 COLPF2. The picture characters are 0-119, never inverse, so COLPF3 does
  not appear in it.

## Colours

* `$18BA`-`$18BE`: the picture's colours, read only by the interrupts:
  mortar (COLPF0), stones (COLPF1), brick edging (COLPF2), ceiling, floor.
  In the starting corridor: `$00 $0A $24 $28 $06` (black, grey, dark red,
  brown, dark grey). `$18BF` (COLPF3) is `$00`.
* The colours belong to the zone of the dungeon the player is in.
  `$7E6C`-`$7E9F`: a table at `$AF03`/`$AF05` maps where the player is to a
  zone number, kept at `$1912`; the zone times 8 indexes 8-byte records at
  `$AF7E`. Bytes 3-5 of a record are COLPF2, COLPF0, COLPF1 (copied to
  `$194B`-`$1952`, then rotated into place at `$7E8E`-`$7E9D`), byte 6 the
  ceiling, byte 7 the floor. The start zone's record is at `$AF8E`:
  `01 89 05 24 00 0A 28 06`. Other records have a `$22` ceiling over a `$C0`
  floor or `$06` over `$02`, so the same art comes in several colour schemes.
* `$77EE` copies `$194E`-`$1952` into `$18BA`-`$18BE` (called from `$7720`
  and `$7748`); `$77E5` sets all five to the accumulator (from `$76DA`,
  `$7703`, `$771A`, `$7742`), which is how the picture turns one flat colour
  right against a wall; `$35EA` saves the five at `$3630`, flashes them
  eight times between `$1A` and `$00` with the delay `$2C66`, and restores
  them. Nothing else writes them, so a view that reads `$18BA`-`$18BE`
  every frame follows zones and flashes.

## The map

* The level is 32 x 32 cells of 4 bytes at `$B000 + y*128 + x*4` (`$3531`
  builds the address): byte 0 = north (low nibble) and east (high) walls,
  byte 1 = south (low) and west (high), byte 2 = cell type, byte 3 = flags
  (bit 7: special, low 5 bits an index kept in `$195A`).
* Wall nibbles, by the art table at `$96F1`/`$9701` (below) and the movement
  handlers: 0 nothing; 1-2 arch; 3-4 door; 5-7 drawn as wall, but 5 and 6
  are secret doors: they take the door art of 3 and 4 while `$1957` is `$FF`
  (`$40B5`-`$40BF` sets it while `$638B` is non-zero; what `$638B` counts
  was not established); 8-10 door art, locked (`$3344`-`$334C`: need bit 1
  of `$6388`); 11-15 wall art, solid (`$32DA` clamps the position at the
  wall); 13 is the ordinary wall. The handlers at `$330F`/`$3307` pass
  through 3, 5 and 6 with a message.
* `$6313`/`$6314`/`$6315`: cell x, y, level; `$6312`: facing, 0 decreases
  y, 1 increases x, 2 increases y, 3 decreases x (tables `$3B44`/`$3B54`).
  It is an ordinary map, north up and east right: facing west, north is on
  the right (*measured* against the game's picture; an earlier note here had
  it mirrored).

## Movement

Found while looking into why walking is drawn in a few big steps.

* `$6316`/`$6317`: position inside the cell, x from the west edge and y from
  the north edge, 0-35 (the start state has 17, 17); the perpendicular axis
  is reset on every step. The renderer turns them into `$7F`, the distance
  walked into the cell from the side entered (`$7B0A`-`$7B1B`: `$6317` or
  `$6316` by facing, negated from 35 for facings 0 and 3).
* `$6383`: step size in units, from the character's speed: table `$445F`
  (6..15) indexed by `$6379 >> 4`, halved when `$6398` is negative, at least
  4, and 2 when `$6394 >= $E0` (`$4430`-`$445E`). 36/7 gives the "five steps
  per cell".
* `$30E5`: movement handler: `$2E` bit 0 forward, bit 1 back, bit 2 turn one
  way, otherwise turn the other way (masks `$3B74`-`$3B76`); the step is
  added at `$3140`/`$314A`, and crossing 36 reads the map cell through
  (`$10`) and dispatches on the wall type via `$3B24`/`$3B34`.
* `$7B0D`: the renderer reads the sub-cell position as a plain 0..35 offset,
  so it draws any position; nothing limits the step to the five positions.
* `$262F`: packs the joystick and trigger into `$2E` (1 = pressed, bit 7 =
  trigger); the main loop fetches it through `$1821` and tests `A & $0F`, so
  gating it means changing both `$2E` and A. `$2643`/`$30`/`$31`: keyboard
  command latch (IRQ at `$2653`).
* `$3884`-`$38CE`: monsters at the player's cell -> `$1938`; `$4A69`-`$4A82`:
  per-pass loop over 64 monsters.
* One redraw costs 20-30 frames of 6502 time, so the game manages 2-3 a
  second while walking; with the drawing skipped the main loop runs at about
  30 passes a second (*measured*).

"Smooth walking" in init.js uses this: with the drawing accelerated, the
joystick is let through so that the character covers 17.5 * (game step / 7)
units a second, the speed the game intended, times the "Walking speed" factor
(1, 1.5, 2 or 3). The step is one unit while that fits in 28 moves a second,
which is what the accelerated loop can redraw, and grows to 2 or 3 units above
that; each move is one redraw. Measured: 124 frames per cell at 1x, 83 at
1.5x, 62 at 2x, 42 at 3x.

## The picture buffer

* `$8F7B`: the picture, 72 rows of 18 bytes, 2 bits per pixel, high bits
  the left pixel; row addresses in the table `$8AFD` (low)/`$8B45` (high).
  `$7888` clears it (six interleaved runs of 216 bytes) before a redraw.
* `$7856`: after the walls are drawn, copies the buffer into the fonts, one
  row at a time with a loop at `$7875` whose operands it patches: byte i of
  row r goes to `$7FC5`/`$800D`[r] + `$8AEB`[i], where `$8AEB` is 0, 8, 16,
  ... 136 (character i of the row) and the row table is `$0858`-`$085F` for
  rows 0-7, `$0998`-`$099F` for 8-15, `$0AD8`-`$0ADF` for 16-23 (font
  `$0800`, characters 11, 51, 91 onwards), then `$0C58`, `$0D98`, `$0ED8`
  and `$1058`, `$1198`, `$12D8` for the other two fonts. 1296 stores per
  redraw; the extension counts the calls for its FPS display and skips the
  loop under HIGH acceleration.

## The renderer

* A wall to draw: `$7B4B` reads the cell byte through (`$7B`),Y, keeps the
  nibble the mask `$7DDE` selects (`$0F` or `$F0`), shifts the high one down
  and stores it at `$8F75`; 0 means nothing to draw (`$7BB7`).
* The projection. A wall's half-height in lines is 35 minus a *depth*; the
  depth of a cell boundary Y cells ahead is `T2[Y] - T1[Y] * $7F / 36`
  (`$7E0D`; `$7E2D` adds instead, for the walls on the other side), with
  T1/T2 an 11-byte table per lateral slot `$7D` (0-10, 5 the player's
  column) at `$8CED + 11*s` and `$8D3B + 11*s`, slots 6-10 reusing 4-0
  through the pointer lists at `$8CD7`/`$8CE2` and `$8D2F`/`$8D35`. For the
  player's column T2 = 0 18 24 27 29 30 31 32 33 34 35 and T1 = 36 18 6 3 2
  1 1 1 1 1 0, so the depth is continuous and linear between cell
  boundaries: distance 0 -> depth 0 (70 lines), one cell -> 18 (34 lines),
  two -> 24 (22), three -> 27 (16), four -> 29 (12), then 10, 8, 6, 4, 2 and
  0 lines at ten cells. No division by the distance: near walls are much
  smaller than a perspective would make them and nothing ever exceeds the
  picture. A frontal wall is as many pixels wide as lines tall (*measured*:
  `$87`, the count the column loop gets, is 2h + 1 for frontal walls); a
  wall seen at an angle has its two ends' heights and the texture runs
  linearly between them (`$87` is then the depth difference of the ends,
  and `$8EE5`/`$8F2D` give the per-column steps).
* Height: `$67` is half the wall's height in lines (0-35), `$64` = 35 - `$67`
  the first line, `$62` = 2 * (`$67` + 1) the lines to fill (`$797A`-`$7987`).
  Two tables indexed by `$67`: `$8EC1` picks the copy of the art (2 = the
  18 x 18 for heights up to 18 lines, 1 = the 36 x 36 up to 36, 0 = the
  72 x 72 above), so a texture is only ever shrunk, never enlarged; and
  `$8E41` (high)/`$8E81` (low) is the texture rows per line as 8.8 fixed
  point, size / lines (9.0 for two lines of the 18 copy, 1.0 for 18, 1.8 for
  20 lines of the 36 copy, 1.0 for 72). The copy goes to `$8F76`, the step
  to `$76`/`$77` (`$798D`-`$799A`).
* Texture address: `$7917`-`$793C`: `$70`/`$71` = `$96F1`/`$9701`[`$8F75`]
  + `$48` (past the header; with the secret-door adjustment of `$8F75`
  first). `$79CB`-`$79ED`: `$72`/`$73` = `$70`/`$71` + `$8B95`/`$8B98`
  [`$8F76`] (0, `$510`, `$654`: the offsets of the three copies) + the
  column's byte within the row, an immediate at `$79E0` that `$79C8` patches
  from the texture column / 4.
* The column filler, `$0090`-`$00D7`, lives in zero page and modifies
  itself. Per line: it loads the texture byte with the LDA at `$009C`, whose
  operand (`$9D`/`$9E`) it recomputed on the previous line as `$72`/`$73` +
  the row offset table entry for the integer part of the row accumulator
  `$6E`/`$6F`. There is a row offset table per copy, 18-, 9- and 5-byte
  strides: `$8BA7`/`$8BEF` (72 entries), `$8C37`/`$8C5B` (36) and
  `$8C7F`/`$8C91` (18); `$799D`-`$79AF` patches the chosen one's address into
  the two loads at `$00C1` and `$00C8` through the list at `$8B9B`. The
  master copy of the filler at `$78A2` has `$FF` where the operands go. It
  masks
  the pixel (`$8B8D`: `$C0 $30 $0C $03`), looks the byte up in the page at
  `$8E00`, whose entries at `$00/$40/$80/$C0`, `$10/$20/$30`, `$04/$08/$0C`
  and `$01/$02/$03` hold the pixel value spread to all four positions
  (`$00 $55 $AA $FF`; the rest of the page holds the tables above), masks
  that to the destination position and ORs it into the picture byte
  (`$B2`/`$B3` = the row from `$8AFD`/`$8B45`), then adds `$76`/`$77` to the
  accumulator. The OR only happens if the destination pixel is still 0
  (`BIT $7E`, the position mask): the walls are drawn near to far and the
  first wall to reach a pixel keeps it. Art pixels of value 0 leave the pixel
  to farther walls or the background, which is how the arch's opening shows
  what lies beyond it, and why the wall right against the player, drawn
  first, hides everything.
* `$79F5`-`$7A1E`: the next column: `$66`/`$67` += `$6A`/`$6B` (the height
  changes along a wall seen at an angle) and `$6C`/`$6D` += `$68`/`$69`, until
  `$6D` reaches 72, then back to `$793E`.
* `$7A1F`-`$7A36`: 8 x 8 multiply, `$79` * `$78` -> A:`$7A`.
* `$8CC1`/`$8CCC` (15, 28, 36, 51, 85, 256 and back), indexed by the slot,
  scales the height of the walls of the side columns; how those are placed
  across the picture was not worked out.
* The frame: the two code blocks at `$7B43`-`$7BB7` and `$7BD2`-`$7C4A` walk
  the cells ahead (`$80`, addresses from `$8CA3`/`$8CAD`) for the walls
  parallel to the view on each side, `$7C4C` onwards draws the wall facing
  the player; the picture is drawn near to far.

## The wall art

* `$96F1` (low)/`$9701` (high): 16 pointers indexed by the wall nibble:
  `0000 A4FD A4FD 9E07 9E07 9711 9711 9711 9E07 9E07 9E07 9711 9711 9711
  9711 9711`. Three pictures: `$9711` the wall (cobblestones with brick
  edging), `$9E07` the door (a frame of light stones, a barred window, a
  handle), `$A4FD` the arch (light stones around an opening).
* Each is 1782 bytes: a 72-byte header (all `$FF` for the wall and the door;
  the arch's is 8 x `$FF`, 57 x `$00`, 7 x `$FF`; what it is for was not
  established), then the picture 72 x 72 at 18 bytes a row (1296 bytes),
  then 36 x 36 at 9 a row (324) and 18 x 18 at 5 a row (90), the smaller
  copies drawn by hand rather than scaled. 2 bits per pixel, the high bits
  the left pixel, rows top-down.
* Pixel values: 1 mortar (COLPF0), 2 stones (COLPF1), 3 brick edging
  (COLPF2), 0 the background: only in the arch's opening. The wall picture
  has 2015, 2416 and 753 pixels of values 1, 2 and 3 and none of 0.
* The layout was confirmed by decoding the three pictures at every plausible
  stride and offset to PNG and looking: the 18-byte stride from the header's
  end gives the wall, door and arch; the "+7" seen in `$72`/`$73` in the dump
  was the column byte of the wall being drawn, not part of the address.

## What view3d.js does with this

The maze is redrawn with OpenGL from the map, with the game's own projection
so that both pictures agree: the wall corners are projected with the depth
law above (the scale at a distance is the half-height over 18 units, across
as well as up), drawn in the 72 x 72 picture space that the viewport shows
2:1, with the distance in the depth buffer and the fog. Walls seen at an
angle keep a height per end and a linear texture, as in the game. The eye
takes the game's position along the facing but sits in the middle of the
cell across it, as the game's renderer ignores that coordinate; a turn
slides it there. Added are fog, side shading and interpolation of steps and
turns. The textures are the
game's 72 x 72 art
decoded from memory through the art table, in the colours at `$18BA`-`$18BE`
looked up in the emulator's palette, so every wall type looks as the game
draws it, secret doors show when the game shows them, and the picture flashes
and changes colours with the game. Each cell has its own four walls, so the
wall between two cells is two records, possibly of different types; only the
side facing the player is drawn, as the game reads only the player's cell.
Art pixels of value 0 are transparent, so arches open onto the corridor
beyond, as in the game. The "Textures" menu entry chooses between the art
as it is, sampled nearest like the game's own scaling, and a 4x version made
by applying Scale2x twice to the pixel values (288 x 288, drawn with mipmaps
and linear filtering): the stairs of the stones' edges are rounded, the
colours stay the game's. The "Layout" entry's Wide mode draws the view over
the whole screen width, still 2:1, and shrinks the game's text rows (lines
20-72 and 146-200 of the screen), centred, and the compass into the 36-line
bands above and below through `gl.drawScreen()`; when the view is not drawn the game's
own picture is enlarged into the same place. The floor and ceiling,
which the game only colours, are grey patterns tinted with the ceiling and
floor colours. A key over the colours, the table and the art bytes tells
when the textures must be rebuilt. The view stays out of the way when the
display list is not the maze or a monster is in the cell (`$1938`), since
the game draws monsters into its own picture.

An earlier version captured the wall from the framebuffer; the art in
memory made that unnecessary.

## The disks

The game came on five disk sides and asks for them by name ("Please insert
The Dungeon Disk 3 Side 2"). It does its own disk I/O and never writes:

* `$2000`-`$22FF`: a serial-port SIO routine (POKEY and PIA directly, no OS
  call anywhere in the game). Its control block is at `$0234` onwards:
  `$0234`/`$0235` the transfer length, `$023D` the status (1 = success, `$8A`,
  `$8C`, `$8E`, `$8F` errors), `$023E`/`$023F` retry counters, `$0246` a copy
  of the status. `$204E` is the entry with retries; the command frame at
  `$0230`-`$0233` (device `$31`, command, sector low, high) is copied to
  `$0266`-`$0269` and sent; data arrives at `$0100`-`$017F`.
* `$248E`: read a sector ('R', 128 bytes, sector from `$0232`/`$0233`);
  `$24A3`: the status command ('S', 4 bytes). `$2986`-`$299D` reads the next
  sector with two retries and steps `$0232`/`$0233`; `$298D`: `BPL` on the
  status, and the status is returned in A. `$2785`-`$2798` steps the device
  byte `$0230` through `$31`-`$34` after a failure: "into any drive". No
  write command exists anywhere in the code seen; the only commands are
  'R' and 'S'.
* `$1821`-`$1841`: a jump table into the low-level routines: `$1821` the
  joystick fetch (`$262F`), `$1824` -> `$275B`, `$1827` the SIO set-up
  (`$245D`), `$182A` the area record (`$28A1`), `$182D` read a sector
  (`$248E`), `$1830` -> `$24C6`, `$1833` the SIO command (`$2494`), `$1836` ->
  `$3C61`, `$1839` -> `$2A41`, `$183C` -> `$2BB0`, `$183F` -> `$2BA5`.
* `$0280` + 4 * area: the area table, a record per area `$1909` (`$28A1`
  copies it to `$1905`-`$1908`): byte 0 bits 3-4 = disk - 1, bit 2 = side - 1,
  bits 0-1 = start sector high; byte 1 = start sector low; bytes 2-3 the
  length (`$1907` extra bytes, `$1908` pages). The five sides in use are
  disk 1 side 1, disk 2 sides 1 and 2, disk 3 sides 1 and 2, in that order
  the images "(v1,s1)" to "(v1,s5)". Area 29, the Damon & Pythias shop, is
  disk 3 side 2 from sector 262, 32 pages.
* `$2827`-`$2896`: the loaded area is descrambled (each byte rotated right
  once and XORed with the 128-byte key at `$0100`) and summed over its pages;
  the sum is compared with `$0184`/`$0185`. A mismatch, which is what a wrong
  disk produces, sets the carry, and `$2CC2`-`$2CF6` then shows the prompt
  (`$28D7`, template at `$29A4` with print codes for `$1911` and `$1910`, the
  disk and side numbers) and retries the load after SPACE.

disks.js intercepts `$248E`: it takes the disk and side from `$1905` and the
sector from `$0232`/`$0233`, copies the 128 bytes from the matching image in
this directory into `$0100`, sets the status registers and Y to 1 and clears
N and Z, and returns RTS in place of the wrapper's first instruction. The
checksum then always passes and the prompt never appears. Images that are
missing fall through to the real drive.

## Hot spots

Instruction frequencies over a walk, from the monitor's profile, which is
where the accelerations in init.js come from (`$0090`: the column filler;
`$7875`: the font copy; `$4A69`/`$3884`: the monster loops; `$7A25`: the
multiply).

```
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
```

## Open questions

* The 72-byte header of each art set.
* What `$638B` is, which reveals the secret doors through `$1957`.
* What triggers the colour flash at `$35EA`.
* Where the checksum the loader compares with comes from (`$0184`/`$0185`).
* How the walls of the side columns (slots other than 5) are placed across
  the picture; view3d.js places everything with the centre column's law.
* `$7600` and the players placed by the `$1B56` interrupt.

## Methods

* A RAM dump from JavaScript: `std.open(path, "wb").write(a8.mem.buffer, 0,
  65536)` in a hook, then a 6502 disassembler over it.
* Probes as `codeInjections` that log registers, memory and `a8.peek(0xD40B)`
  (VCOUNT) when the game reaches an address; the interrupt scanlines above
  came from injections at the handlers.
* Scripted input with `-playback` files (see the README) for repeatable
  walks, `A8_EXT_SELECT` to activate the extension without the menu, and
  `gl.readPixels()` for screenshots of the OpenGL view.
* Decoding candidate memory layouts to PNG (grey per pixel value) and
  looking at them beats reasoning about strides.
