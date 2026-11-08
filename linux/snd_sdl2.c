/*
 * SDL2 audio backend for the Quake II DMA mixer.
 *
 * The callback only copies samples from the shared ring buffer. The
 * engine is always on the other side of SDL_LockAudioDevice().
 */

#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "../client/client.h"
#include "../client/snd_loc.h"

static SDL_AudioDeviceID sdl_device;
static qboolean audio_subsystem_initialized;
static qboolean snd_inited;
static qboolean start_pending;

static int paint_lock_depth;
static int play_byte;
static int dma_bytes;

static int NextPowerOfTwo (int value)
{
	int result = 1;

	while (result < value)
		result <<= 1;

	return result;
}

static int SDL2_SoundRate (void)
{
	switch (s_khz->intvalue)
	{
	case 48:
		return 48000;
	case 44:
		return 44100;
	case 22:
		return 22050;
	case 11:
		return 11025;
	default:
		return 22050;
	}
}

static void SDLCALL SDL2_AudioCallback (void *userdata, Uint8 *stream, int len)
{
	(void)userdata;

	if (!snd_inited || !dma.buffer || dma_bytes <= 0)
	{
		memset (stream, 0, len);
		return;
	}

	while (len > 0)
	{
		int chunk;

		if (play_byte >= dma_bytes)
			play_byte = 0;

		chunk = dma_bytes - play_byte;
		if (chunk > len)
			chunk = len;

		memcpy (stream, dma.buffer + play_byte, chunk);

		stream += chunk;
		len -= chunk;
		play_byte += chunk;

		if (play_byte == dma_bytes)
			play_byte = 0;
	}
}

qboolean SNDDMA_Init (int fullInit)
{
	SDL_AudioSpec desired;
	SDL_AudioSpec obtained;
	const char *driver;
	int rate;
	int ring_frames;

	(void)fullInit;

	if (snd_inited)
		return true;

	memset (&dma, 0, sizeof(dma));
	play_byte = 0;
	dma_bytes = 0;
	paint_lock_depth = 0;
	start_pending = false;
	audio_subsystem_initialized = false;

	rate = SDL2_SoundRate ();

	if (!(SDL_WasInit (SDL_INIT_AUDIO) & SDL_INIT_AUDIO))
	{
		if (SDL_InitSubSystem (SDL_INIT_AUDIO) < 0)
		{
			Com_Printf ("SDL audio initialization failed: %s\n",
				LOG_CLIENT, SDL_GetError());
			return false;
		}
		audio_subsystem_initialized = true;
	}

	memset (&desired, 0, sizeof(desired));
	memset (&obtained, 0, sizeof(obtained));

	desired.freq = rate;
	desired.format = AUDIO_S16SYS;
	desired.channels = 2;
	desired.samples = rate <= 11025 ? 256 : rate <= 22050 ? 512 : 1024;
	desired.callback = SDL2_AudioCallback;
	desired.userdata = NULL;

	/* SDL converts to the hardware format without changing our callback format. */
	sdl_device = SDL_OpenAudioDevice (NULL, 0, &desired, &obtained, 0);
	if (!sdl_device)
	{
		Com_Printf ("SDL_OpenAudioDevice failed: %s\n",
			LOG_CLIENT, SDL_GetError());
		SNDDMA_Shutdown ();
		return false;
	}

	if (obtained.format != AUDIO_S16SYS || obtained.channels != 2 ||
		obtained.freq <= 0 || obtained.samples == 0)
	{
		Com_Printf ("SDL audio returned an unsupported format\n", LOG_CLIENT);
		SNDDMA_Shutdown ();
		return false;
	}

	/* Keep one to two seconds in the ring, always at a power of two frames. */
	ring_frames = NextPowerOfTwo (obtained.freq);
	if (ring_frames < 4 * obtained.samples)
		ring_frames = NextPowerOfTwo (4 * obtained.samples);

	dma.channels = 2;
	dma.samplebits = 16;
	dma.speed = obtained.freq;
	dma.samples = ring_frames * dma.channels;
	dma.submission_chunk = 1;
	dma.samplepos = 0;

	dma_bytes = dma.samples * (dma.samplebits / 8);
	dma.buffer = calloc ((size_t)dma.samples, (size_t)(dma.samplebits / 8));
	if (!dma.buffer)
	{
		Com_Printf ("SDL audio buffer allocation failed\n", LOG_CLIENT);
		SNDDMA_Shutdown ();
		return false;
	}

	play_byte = 0;
	start_pending = true;
	snd_inited = true;

	driver = SDL_GetCurrentAudioDriver ();
	Com_Printf ("SDL audio driver: %s, %d Hz, 16-bit stereo, %d-frame ring\n",
		LOG_CLIENT, driver ? driver : "unknown", dma.speed, ring_frames);

	return true;
}

int SNDDMA_GetDMAPos (void)
{
	qboolean lock_here;
	int pos;

	if (!snd_inited)
		return 0;

	lock_here = (paint_lock_depth == 0);
	if (lock_here)
		SDL_LockAudioDevice (sdl_device);

	pos = (play_byte / (dma.samplebits / 8)) & (dma.samples - 1);

	if (lock_here)
		SDL_UnlockAudioDevice (sdl_device);

	dma.samplepos = pos;
	return pos;
}

void SNDDMA_BeginPainting (void)
{
	if (!snd_inited || !dma.buffer)
		return;

	if (paint_lock_depth++ == 0)
		SDL_LockAudioDevice (sdl_device);
}

void SNDDMA_Submit (void)
{
	if (!snd_inited || !sdl_device || paint_lock_depth <= 0)
		return;

	paint_lock_depth--;
	if (paint_lock_depth != 0)
		return;

	SDL_UnlockAudioDevice (sdl_device);

	if (start_pending)
	{
		start_pending = false;
		SDL_PauseAudioDevice (sdl_device, 0);
	}
}

void SNDDMA_Shutdown (void)
{
	snd_inited = false;
	start_pending = false;

	if (sdl_device && paint_lock_depth > 0)
		SDL_UnlockAudioDevice (sdl_device);
	paint_lock_depth = 0;

	if (sdl_device)
	{
		SDL_PauseAudioDevice (sdl_device, 1);
		SDL_CloseAudioDevice (sdl_device);
		sdl_device = 0;
	}

	/* SDL_CloseAudioDevice waits for the callback before we free the ring. */
	free (dma.buffer);
	dma.buffer = NULL;

	memset (&dma, 0, sizeof(dma));
	play_byte = 0;
	dma_bytes = 0;

	if (audio_subsystem_initialized)
	{
		SDL_QuitSubSystem (SDL_INIT_AUDIO);
		audio_subsystem_initialized = false;
	}
}
