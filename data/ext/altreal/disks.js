// Alternate Reality: The Dungeon - the five disk sides served from files, so
// that the game never asks for a disk to be inserted.
//
// The game does its own disk I/O (a serial routine at $2000-$22FF, no OS
// calls): every read goes through the wrapper at $248E, which sends the
// command frame $0230-$0233 (device $31, 'R', sector low, high) and receives
// the 128 bytes into $0100-$017F, leaving 1 in $023D and $0246 on success
// (an error code with bit 7 set otherwise), which the caller tests with the
// N flag and returns in Y. Which side a read belongs to is in the area
// record copied to $1905: bits 3-4 the disk (1-3), bit 2 the side (1-2); a
// loaded area is descrambled and checksummed, and a wrong disk is what
// makes the checksum fail and the "Please insert" prompt appear. See
// altreal.md, "The disks".

import * as std from "std";

const DISK_READ = 0x248E;                 // the read-sector wrapper: intercept here
const DISK_CODE = 0x1905, SECTOR_LO = 0x232, SECTOR_HI = 0x233;
const BUFFER = 0x100, STATUS = 0x23D, STATUS2 = 0x246;
const SECTOR_SIZE = 128, ATR_HEADER = 16;

// The images: side n of the set is "(v1,sn)"; disk 1 has one side, disks 2
// and 3 two each
const DIR = a8.extDir + "/";
function sideFile(disk, side) {
	const n = disk === 1 ? 1 : disk === 2 ? 1 + side : 3 + side;
	return `${DIR}Alternate Reality The Dungeon (v1,s${n}).atr`;
}

const images = new Map();   // "disk,side" -> Uint8Array of the sectors, or null when the file is missing

function image(disk, side) {
	const key = `${disk},${side}`;
	if (images.has(key)) return images.get(key);
	const path = sideFile(disk, side);
	let data = null;
	const f = std.open(path, "rb");
	if (f !== null) {
		f.seek(0, std.SEEK_END);
		const size = f.tell() - ATR_HEADER;
		if (size > 0) {
			data = new Uint8Array(size);
			f.seek(ATR_HEADER, std.SEEK_SET);
			f.read(data.buffer, 0, size);
		}
		f.close();
	}
	console.log(data ? `altreal: disk ${disk} side ${side} from ${path}` : `altreal: no image for disk ${disk} side ${side} (${path}), using the drive`);
	images.set(key, data);
	return data;
}

export const disks = {
	served: 0,          // sectors served so far
	last: "",           // "disk d side s sector n"

	hooks: [DISK_READ],

	// Called for the read wrapper's first instruction: serves the sector from
	// the file and returns RTS to skip the drive; or the opcode to let the
	// game read the drive when the image is missing
	onCodeInjection(pc, op) {
		if (pc !== DISK_READ) return op;
		const mem = a8.mem, code = mem[DISK_CODE];
		const disk = ((code >> 3) & 3) + 1, side = ((code >> 2) & 1) + 1;
		const sector = mem[SECTOR_LO] | mem[SECTOR_HI] << 8;
		const img = image(disk, side);
		if (img === null || sector < 1 || sector * SECTOR_SIZE > img.length) return op;
		mem.set(img.subarray((sector - 1) * SECTOR_SIZE, sector * SECTOR_SIZE), BUFFER);
		mem[STATUS] = 1; mem[STATUS2] = 1;
		a8.cpu.y = 1;                 // what LDY $023D leaves for the caller
		a8.cpu.p &= ~0x82;            // N and Z clear: success
		this.served++;
		this.last = `disk ${disk} side ${side} sector ${sector}`;
		return 0x60;                  // RTS
	},
};
