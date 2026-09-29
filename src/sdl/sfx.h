#ifndef SDL_SFX_H_
#define SDL_SFX_H_

/* sdl/sfx.c - a minimal software mixer for extension sound effects.

   Extensions (see ext.c) load WAV files with SDL_SFX_Load() and play them
   with SDL_SFX_Play(). The samples are converted to the format of the SDL
   audio device and mixed on top of the emulated POKEY output inside the SDL
   audio callback (see sdl/sound.c). Each SDL_SFX_Play() starts one playback
   of the whole sample at full volume; up to SDL_SFX_MAX_VOICES samples play
   at the same time. */

#include <SDL.h>

#define SDL_SFX_MAX_VOICES 16

typedef struct SDL_SFX_sample SDL_SFX_sample;

/* Loads a WAV file. Returns NULL (and logs the reason) on failure. */
SDL_SFX_sample *SDL_SFX_Load(const char *fname);

/* Starts playing SAMPLE once. Silently does nothing if the audio device is
   not open or all voices are busy. Call from the main thread only. */
void SDL_SFX_Play(SDL_SFX_sample *sample);

/* Hooks for sdl/sound.c. */
void SDL_SFX_DeviceOpened(int freq, Uint16 format, Uint8 channels);
void SDL_SFX_DeviceClosed(void);
/* Mixes the playing samples into STREAM; called from the audio callback. */
void SDL_SFX_Mix(Uint8 *stream, int len);

#endif /* SDL_SFX_H_ */
