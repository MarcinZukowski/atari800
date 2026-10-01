// Alternate Reality: The Dungeon - the game's own pictures smoothed (shop
// interiors, the picture in the Atari view, monsters in it) with the shared
// smoother over the picture's mode-4 pixels, two hi-res pixels wide and one
// line tall. See ../smooth2d.js.

import { createSmoother } from "../smooth2d.js";

const smoother = createSmoother(2, 1);
export const capture = (region) => smoother.capture(region);
export const draw = (dst, src) => smoother.draw(dst, src);
