#ifndef CODECS_IMAGE_PNG_H_
#define CODECS_IMAGE_PNG_H_

#include "atari.h"
#include "codecs/image.h"

extern IMAGE_CODEC_t Image_Codec_PNG;

#ifdef VIDEO_CODEC_PNG
/* Writes a true-colour picture (width * height * 3 bytes of R, G, B, rows from
   the top) as a PNG into buf. Returns its size, or -1 when it does not fit. */
int PNG_SaveRGBToBuffer(UBYTE *buf, int bufsize, UBYTE *rgb, int width, int height);
#endif

#endif /* CODECS_IMAGE_PNG_H_ */
