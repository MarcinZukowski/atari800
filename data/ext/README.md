# atari800 extensibility ideas

A while ago




# River Raid notes

* wide screen, 48 bytes/line
* 160 lines, no double buffering
* screen mostly 55 (land) and ff (water)
* PM2 is the plane (HPOSP2, COLPM2)
* PM3 is the enemies (HPOSP3 etc)

* b00f - helper "move" from (a1) to (a3)
* b55c - drawing of enemies, synced with vpos
* b57d - sets the X position of the enemies

* #5D - minimal Y index = 256 - 160 (-3)

* enemies graphics: 0f00..0fff
* 0000 - * to enemy graphics, per line
* 0002 - * to enemy color, per line
* 0005 - (temp) current enemy Y position
* 0006 - currently drawn enemy
* 0007 - (temp) Y where we started drawing the enemy
* 000c - [] Y starts of enemies
* 0039 - tank X
* 0042 - [] ? flags of enemies? - if 08 set, it's reversed (index + 0x10)
* 004d - initial enemy #
* 0057 - plane X pos
* 0500 - [] of enemy IDs (can have +0x10 if reversed ?
* 050b - [] of enemy color ids - same (similar?) to 0500
* 0516 - [] X positions of enemies
* 0521 - [] H sizes of enemies
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
* bacd - some graphics positions (2-byte addresses)
* bb30 - lo-address of each enemy color, indexed by 050b
* bb40 - lo-address of each enemy gfx, indexed by 0500
* bb60 - ? height of each enemy, indexed by 050b
