# Robbo - technical notes

Robbo (Janusz Pelc, LK Avalon, 1989), the Atari version as robbo.xex. Facts
from a RAM dump of the saved state r.a8s (level 1 in play) and its
disassembly; numbers with `$` are hex.

## The program

* robbo.xex loads `$0600`-`$06AC` (a loader stub), the game `$0E00`-`$BA80`,
  and runs at `$A87F` (`RUNAD`); the extension's fingerprint is the code
  there.

## The screen

* Display list at `$36BD`: 21 blank lines, then a mode-4 row with LMS and
  VSCROL (`$64 lo hi`), 19 mode-4 rows with VSCROL (`$24`), a mode-4 row
  with a DLI (`$84`), 12 blank lines, two mode-2 rows from `$36E1` (the
  status line: score, screws, lives, keys, bullets, level) and the jump
  back. DMACTL is `$21`: the narrow playfield, 32 characters a row.
* The 21 playfield rows are a window over the level's screen memory at
  `$1000`, 32 bytes a row; the window's first row is the LMS operand at
  `$36C1`/`$36C2` (set by the scrolling code at `$3440`-`$34B0`, with
  VSCROL), so the game scrolls vertically in lines. The rows start at
  scanline 29 (screen line 21) and the narrow playfield spans colour clocks
  64-192 (screen pixels 40-296).
* The character set is at `$1C00` (CHBAS shadow `$02F4` = `$1C`) for the
  playfield; the DLI after it (`$3335`, a state machine on `$3334`) switches
  to `$2000` for the status line and widens the playfield (DMACTL `$22`),
  and the DLI before it puts the level's colour into the player registers.
* The colours are the OS shadows `$02C4`-`$02C7` (COLPF0-3; level 1: black,
  `$18`, `$0A`, `$72`). Pixel value 0 is not a playfield colour: the four
  players stand at 64, 96, 128 and 160 at quadruple width, 128 clocks in
  all, exactly behind the narrow playfield, with solid shapes and the colour
  from `$02C3` (`$B4`, green, on level 1): the floor. Value 3 is COLPF2, or
  COLPF3 for characters with bit 7 set (the blue walls).

## The level

* 16 x 31 tiles of 2 x 2 characters: the tile at (x, y) is the characters at
  `$1000 + 64 * y + 2 * x`, `+1`, `+32`, `+33`. The four codes are `c`, `c+1`,
  `c+$20`, `c+$21`: the set is laid out as a sheet of tiles. Level 1 uses
  the floor `$40` (all pixels 0), the wall `$80` (inverse: blue, with the
  bevel in COLPF1 and COLPF0), the screw `$5A`, Robbo `$5E`, a bug `$CE`, a
  gun `$9C`, a bomb `$4C`, a bush `$50`, and others. Rows 62-63 (`$42`) lie
  below the level.
* Animation is done in the characters on the screen and in the set, so a
  renderer that reads them every frame follows it.

## What view3d.js does

The level is drawn over the playfield's rectangle with OpenGL: a plane in
the floor's colour, every tile with any pixel set as a block (when it is
at least 90% opaque, like the walls: the art on top, the same art darkened
on the sides) or as a card lying a little above the floor with a soft
shadow; pixel value 0 is transparent. The art is upscaled with Scale2x
twice. The camera looks down at the game's window from a little in front
(a tilt of 30 degrees by default) and follows the game's scrolling in
lines from the LMS and VSCROL. Textures are cached by the tile's four
characters and rebuilt when the set or the colours change.
