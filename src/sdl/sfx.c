/*
 * sdl/sfx.c - a minimal software mixer for extension sound effects
 *
 * This file is part of the Atari800 emulator project which emulates
 * the Atari 400, 800, 800XL, 130XE, and 5200 8-bit computers.
 *
 * Atari800 is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Atari800 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Atari800; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <SDL.h>
#include <stdlib.h>
#include <string.h>

#include "atari.h"
#include "log.h"
#include "sdl/sfx.h"

struct SDL_SFX_sample {
	char *name;
	/* Raw data as loaded from the WAV file. */
	Uint8 *wav_data;
	Uint32 wav_len;
	SDL_AudioSpec wav_spec;
	/* Data in the device format. Points at wav_data when no conversion
	   was needed. NULL when the sample cannot be played. */
	Uint8 *data;
	Uint32 len;
	/* Device generation DATA was converted for; -1 = never. */
	int generation;
};

typedef struct {
	SDL_SFX_sample *sample; /* NULL = voice is free */
	Uint32 pos;             /* byte offset into sample->data */
} voice_t;

/* Voices are shared between the main thread and the SDL audio thread.
   The main thread only touches them with the audio lock held. */
static voice_t voices[SDL_SFX_MAX_VOICES];

/* Format of the currently open audio device. */
static int device_freq;
static Uint16 device_format;
static Uint8 device_channels;
static int device_open = FALSE;
/* Bumped every time the device is (re)opened, so that samples converted
   for an old device format get converted again. */
static int device_generation = 0;

void SDL_SFX_DeviceOpened(int freq, Uint16 format, Uint8 channels)
{
	SDL_LockAudio();
	memset(voices, 0, sizeof(voices));
	SDL_UnlockAudio();
	device_freq = freq;
	device_format = format;
	device_channels = channels;
	device_generation++;
	device_open = TRUE;
}

void SDL_SFX_DeviceClosed(void)
{
	if (!device_open)
		return;
	SDL_LockAudio();
	memset(voices, 0, sizeof(voices));
	SDL_UnlockAudio();
	device_open = FALSE;
}

SDL_SFX_sample *SDL_SFX_Load(const char *fname)
{
	SDL_SFX_sample *s = (SDL_SFX_sample *) calloc(1, sizeof(SDL_SFX_sample));
	if (s == NULL)
		return NULL;
	if (SDL_LoadWAV(fname, &s->wav_spec, &s->wav_data, &s->wav_len) == NULL) {
		Log_print("sfx: cannot load %s: %s", fname, SDL_GetError());
		free(s);
		return NULL;
	}
	s->name = (char *) malloc(strlen(fname) + 1);
	if (s->name != NULL)
		strcpy(s->name, fname);
	s->generation = -1;
	Log_print("sfx: loaded %s (%d Hz, format 0x%x, %d channel(s), %u bytes)",
	          fname, s->wav_spec.freq, s->wav_spec.format, s->wav_spec.channels,
	          (unsigned int) s->wav_len);
	return s;
}

/* Makes sure S->data is in the current device format. Returns TRUE if the
   sample can be played. Must not be called while a voice plays S, which
   holds because voices are cleared whenever the device format changes. */
static int Convert(SDL_SFX_sample *s)
{
	SDL_AudioCVT cvt;
	int rc;

	if (s->generation == device_generation)
		return s->data != NULL;

	if (s->data != NULL && s->data != s->wav_data)
		free(s->data);
	s->data = NULL;
	s->len = 0;
	s->generation = device_generation;

	rc = SDL_BuildAudioCVT(&cvt, s->wav_spec.format, s->wav_spec.channels, s->wav_spec.freq,
	                       device_format, device_channels, device_freq);
	if (rc < 0) {
		Log_print("sfx: cannot convert %s to the device format: %s", s->name, SDL_GetError());
		return FALSE;
	}
	if (rc == 0) {
		s->data = s->wav_data;
		s->len = s->wav_len;
		return TRUE;
	}
	cvt.len = (int) s->wav_len;
	cvt.buf = (Uint8 *) malloc((size_t) cvt.len * cvt.len_mult);
	if (cvt.buf == NULL)
		return FALSE;
	memcpy(cvt.buf, s->wav_data, s->wav_len);
	if (SDL_ConvertAudio(&cvt) != 0) {
		Log_print("sfx: cannot convert %s: %s", s->name, SDL_GetError());
		free(cvt.buf);
		return FALSE;
	}
	s->data = cvt.buf;
	s->len = (Uint32) cvt.len_cvt;
	return TRUE;
}

void SDL_SFX_Play(SDL_SFX_sample *s)
{
	int i;

	if (s == NULL || !device_open)
		return;
	if (!Convert(s))
		return;

	SDL_LockAudio();
	for (i = 0; i < SDL_SFX_MAX_VOICES; i++) {
		if (voices[i].sample == NULL) {
			voices[i].sample = s;
			voices[i].pos = 0;
			break;
		}
	}
	SDL_UnlockAudio();
}

/* Adds N bytes of SRC to DST with clipping, in the device sample format.
   sdl/sound.c only ever opens the device as AUDIO_U8 or AUDIO_S16SYS. */
static void MixInto(Uint8 *dst, const Uint8 *src, Uint32 n)
{
	Uint32 i;
	if (device_format == AUDIO_U8) {
		for (i = 0; i < n; i++) {
			int v = (int) dst[i] + (int) src[i] - 128;
			dst[i] = (Uint8) (v < 0 ? 0 : v > 255 ? 255 : v);
		}
	}
	else {
		Sint16 *d = (Sint16 *) dst;
		const Sint16 *s = (const Sint16 *) src;
		n /= 2;
		for (i = 0; i < n; i++) {
			int v = (int) d[i] + (int) s[i];
			d[i] = (Sint16) (v < -32768 ? -32768 : v > 32767 ? 32767 : v);
		}
	}
}

void SDL_SFX_Mix(Uint8 *stream, int len)
{
	int i;
	for (i = 0; i < SDL_SFX_MAX_VOICES; i++) {
		voice_t *v = &voices[i];
		Uint32 n;
		if (v->sample == NULL)
			continue;
		n = v->sample->len - v->pos;
		if (n > (Uint32) len)
			n = (Uint32) len;
		MixInto(stream, v->sample->data + v->pos, n);
		v->pos += n;
		if (v->pos >= v->sample->len)
			v->sample = NULL;
	}
}
