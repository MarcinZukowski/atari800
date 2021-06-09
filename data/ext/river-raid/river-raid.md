### River Raid notes

* wide screen, 48 bytes/line
* 160 lines, no double buffering
* screen mostly 55 (land) and ff (water)
* PM2 is the plane (HPOSP2, COLPM2)
* PM3 is the enemies (HPOSP3 etc)

* b00f - helper "move" from (a1) to (a3)
* b223 - drawing missile
* b55c - drawing of enemies, synced with vpos
  * b57d - sets the X position of the enemies
  * b5da - draws the bridge color

* #5D - minimal Y index = 256 - 160 (-3)

* enemies graphics: 0f00..0fff
* 0000 - * to enemy graphics, per line
* 0002 - * to enemy color, per line
* 0005 - (temp) current enemy Y position
* 0006 - (temp) currently drawn enemy
* 0007 - (temp) Y where we started drawing the enemy
* 000c - [] Y starts of enemies
* 0039 - tank X
* 0042 - [] ? flags of enemies? - if 08 set, it's reversed (index + 0x10)
* 004d - initial enemy #
* 0056 - missile Y pos
* 0057 - plane X pos
* 005e - plane shape index  - points to gfx addresses at BACD
         07 -left, 08 - right, 09 - straight,
* 0500 - [] of enemy IDs for texture (can have +0x10 if reversed)
* 050b - [] of enemy IDs for color and height - can be different than 0500
* 0516 - [] X positions of enemies
* 0521 - [] H sizes (width) of enemies - 0 normal, 1 double, 3 quadruple
* 0800 - pmbase
* 0b00 - missiles of helicopters etc
* 0c00 - tank fires PM
* 0d00 - tank PM gfx
* 0e00 - player PM gfx
* 0f00 - enemy PM gfx
* 2000..2eff, 3000.3eff - 2 * 80 wide lines
* 3f00 - dlist
* b4d1 - some graphic positions (2-byte addresses)
* b700 - enemy colors
* b900 - enemy gfx
* ba00 - more gfx (tank, tank's fire)
* bacd - some graphics positions (2-byte addresses) - e.g. plane shape (from 5e)
* bb30 - lo-address of each enemy color, indexed by 050b
* bb40 - lo-address of each enemy gfx (inside b900), indexed by 0500 (+0x10 for reversed)
* bb60 - ? height of each enemy, indexed by 050b

* b91c - 3 * 0x18 bytes of frame of explosion (b91c, b934, b94cq)
* bb40 - explosion indices: 02->4c, 03->34, 04->1c, 05->34, 06->4c

grm b904 01 18 - shows PM

sound
* b384 - fire sound code
  * 7d - fire volume - 0e seems to be the fire start