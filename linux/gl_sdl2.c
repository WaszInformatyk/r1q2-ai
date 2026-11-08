/* $Id: gl_sdl2.c,v 1.0 2026/11/06 r1q2-tsmod Exp $
 *
 * SDL2 OS-specific refresher code, based on the QuakeForge SDL backend.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include <SDL.h>

#include <GL/gl.h>

#include "../client/client.h"
#include "../ref_gl/gl_local.h"
#include "../linux/glw.h"

#include "../client/keys.h"
#include "../linux/rw_linux.h"

/*****************************************************************************/

#define JOY_AXIS_X			0
#define JOY_AXIS_Y			1
#define JOY_AXIS_Z			2
#define JOY_AXIS_R			3
#define JOY_AXIS_U			4
#define JOY_AXIS_V			5

static qboolean window_active;
static qboolean app_active;
static qboolean input_active;
static qboolean video_initialized;

static SDL_Window *window;
static SDL_GLContext gl_context;

static struct
{
	int key;
	int down;
} keyq[64];
static int keyq_head;
static int keyq_tail;

static Key_Event_fp_t Key_Event_fp;

static unsigned char key_down[SDL_NUM_SCANCODES];
static unsigned char key_codes[SDL_NUM_SCANCODES];
static unsigned char key_refcount[256];

glwstate_t glw_state;

extern cvar_t *vid_display;

/*****************************************************************************/
/* MOUSE                                                                     */
/*****************************************************************************/

static qboolean mouse_avail;
static int mouse_buttonstate;
static int mouse_oldbuttonstate;
static int mouse_x, mouse_y;
static int old_mouse_x, old_mouse_y;
static int mx, my;
static float old_windowed_mouse;
static qboolean mouse_active;
static qboolean mouse_ungrab_requested;

static cvar_t *_windowed_mouse;
static cvar_t *m_filter;
static cvar_t *in_mouse;

static qboolean mlooking;

qboolean have_stencil = false;

static in_state_t *in_state;

cvar_t *sensitivity;
static cvar_t *my_lookstrafe;
cvar_t *m_side;
cvar_t *m_yaw;
cvar_t *m_pitch;
cvar_t *m_forward;
static cvar_t *my_freelook;

#ifdef HAVE_JOYSTICK
static cvar_t *in_joystick;
static cvar_t *j_invert_y;
static qboolean joystick_avail;
static SDL_Joystick *joy;
static int joy_oldbuttonstate;
static int joy_numbuttons;
static int jx, jy, jt;
static int lr_axis, ud_axis, throttle_axis;
#endif

static void ResetMouseDeltas (void)
{
	mx = my = 0;
	mouse_x = mouse_y = 0;
	old_mouse_x = old_mouse_y = 0;
}

static void Keyq_Flush (void)
{
	if (!Key_Event_fp)
		return;

	while (keyq_head != keyq_tail)
	{
		Key_Event_fp (keyq[keyq_tail].key, keyq[keyq_tail].down);
		keyq_tail = (keyq_tail + 1) & 63;
	}
}

static void Keyq_Push (int key, qboolean down)
{
	int next;

	if (!key)
		return;

	next = (keyq_head + 1) & 63;
	if (next == keyq_tail)
	{
		Keyq_Flush ();
		next = (keyq_head + 1) & 63;
		if (next == keyq_tail)
			return;
	}

	keyq[keyq_head].key = key;
	keyq[keyq_head].down = down;
	keyq_head = next;
}

static void ReleaseAllKeys (void)
{
	int i;

	for (i = 0; i < 256; i++)
	{
		if (key_refcount[i])
		{
			Keyq_Push (i, false);
			key_refcount[i] = 0;
		}
	}

	memset (key_down, 0, sizeof(key_down));
	memset (key_codes, 0, sizeof(key_codes));
	mouse_buttonstate = 0;
}

static void UpdateMouseCapture (void)
{
	qboolean want_capture;

	/* Capture gameplay in fullscreen; _windowed_mouse controls windowed play. */
	want_capture = window_active && app_active && input_active && mouse_avail &&
		!mouse_ungrab_requested &&
		((window && (SDL_GetWindowFlags (window) & SDL_WINDOW_FULLSCREEN)) ||
		(_windowed_mouse && FLOAT_NE_ZERO(_windowed_mouse->value)));

	if (want_capture == mouse_active)
		return;

	if (want_capture)
	{
		if (SDL_SetRelativeMouseMode (SDL_TRUE) == 0)
		{
			SDL_SetWindowGrab (window, SDL_TRUE);
			mouse_active = true;
		}
		else
		{
			ri.Con_Printf (PRINT_DEVELOPER, "SDL relative mouse mode failed: %s\n", SDL_GetError());
		}
	}
	else
	{
		if (window)
		{
			SDL_SetRelativeMouseMode (SDL_FALSE);
			SDL_SetWindowGrab (window, SDL_FALSE);
		}
		mouse_active = false;
	}

	ResetMouseDeltas ();
	if (window)
		SDL_GetRelativeMouseState (&mx, &my);
	ResetMouseDeltas ();
}

static void HandleAppActive (qboolean active)
{
	if (app_active == active)
		return;

	app_active = active;
	ResetMouseDeltas ();
	UpdateMouseCapture ();

	if (!active)
		ReleaseAllKeys ();

	SDL_ShowCursor (active ? SDL_DISABLE : SDL_ENABLE);
}

static void Force_CenterView_f (void)
{
	in_state->viewangles[PITCH] = 0;
}

static void RW_IN_MLookDown (void)
{
	mlooking = true;
}

static void RW_IN_MLookUp (void)
{
	mlooking = false;
	in_state->IN_CenterView_fp ();
}

void EXPORT RW_IN_Init(in_state_t *in_state_p)
{
	in_state = in_state_p;

	_windowed_mouse = ri.Cvar_Get ("_windowed_mouse", "0", CVAR_ARCHIVE);
	m_filter = ri.Cvar_Get ("m_filter", "0", 0);
	in_mouse = ri.Cvar_Get ("in_mouse", "1", CVAR_ARCHIVE);
#ifdef HAVE_JOYSTICK
	in_joystick = ri.Cvar_Get("in_joystick", "0", CVAR_ARCHIVE);
	j_invert_y = ri.Cvar_Get("j_invert_y", "1", 0);
	lr_axis = (int) ri.Cvar_Get("j_lr_axis", "0", CVAR_ARCHIVE)->value;
	ud_axis = (int) ri.Cvar_Get("j_ud_axis", "1", CVAR_ARCHIVE)->value;
	throttle_axis = (int) ri.Cvar_Get("j_throttle", "3", CVAR_ARCHIVE)->value;
#endif
	my_freelook = ri.Cvar_Get( "freelook", "0", 0);
	my_lookstrafe = ri.Cvar_Get ("lookstrafe", "0", 0);

	sensitivity = ri.Cvar_Get ("sensitivity", "3", 0);
	m_pitch = ri.Cvar_Get ("m_pitch", "0.022", 0);
	m_yaw = ri.Cvar_Get ("m_yaw", "0.022", 0);
	m_forward = ri.Cvar_Get ("m_forward", "1", 0);
	m_side = ri.Cvar_Get ("m_side", "0.8", 0);

	ri.Cmd_AddCommand ("+mlook", RW_IN_MLookDown);
	ri.Cmd_AddCommand ("-mlook", RW_IN_MLookUp);
	ri.Cmd_AddCommand ("force_centerview", Force_CenterView_f);

	old_windowed_mouse = -1;
	mouse_ungrab_requested = false;
	input_active = true;
	mouse_avail = true;
	ResetMouseDeltas ();
}

void EXPORT RW_IN_Shutdown(void)
{
	input_active = false;
	UpdateMouseCapture ();

	if (mouse_avail)
	{
		mouse_avail = false;
		ri.Cmd_RemoveCommand ("+mlook");
		ri.Cmd_RemoveCommand ("-mlook");
		ri.Cmd_RemoveCommand ("force_centerview");
	}

#ifdef HAVE_JOYSTICK
	if (joy)
	{
		SDL_JoystickClose(joy);
		joy = NULL;
	}
#endif
	SDL_ShowCursor (SDL_ENABLE);
}

void EXPORT RW_IN_Commands (void)
{
	int i;
#ifdef HAVE_JOYSTICK
	int key_index;
#endif

	if (mouse_avail)
	{
		for (i = 0; i < 3; i++)
		{
			if ( (mouse_buttonstate & (1<<i)) && !(mouse_oldbuttonstate & (1<<i)) )
				in_state->Key_Event_fp (K_MOUSE1 + i, true);

			if ( !(mouse_buttonstate & (1<<i)) && (mouse_oldbuttonstate & (1<<i)) )
				in_state->Key_Event_fp (K_MOUSE1 + i, false);
		}

		if ((mouse_buttonstate & (1<<3)) && !(mouse_oldbuttonstate & (1<<3)))
			in_state->Key_Event_fp(K_MOUSE4, true);
		if (!(mouse_buttonstate & (1<<3)) && (mouse_oldbuttonstate & (1<<3)))
			in_state->Key_Event_fp(K_MOUSE4, false);

		if ((mouse_buttonstate & (1<<4)) && !(mouse_oldbuttonstate & (1<<4)))
			in_state->Key_Event_fp(K_MOUSE5, true);
		if (!(mouse_buttonstate & (1<<4)) && (mouse_oldbuttonstate & (1<<4)))
			in_state->Key_Event_fp(K_MOUSE5, false);

		mouse_oldbuttonstate = mouse_buttonstate;
	}
#ifdef HAVE_JOYSTICK
	if (joystick_avail && joy)
	{
		for (i = 0; i < joy_numbuttons; i++)
		{
			if (SDL_JoystickGetButton(joy, i) && joy_oldbuttonstate != i)
			{
				key_index = (i < 4) ? K_JOY1 : K_AUX1;
				in_state->Key_Event_fp(key_index + i, true);
				joy_oldbuttonstate = i;
			}
			if (!SDL_JoystickGetButton(joy, i) && joy_oldbuttonstate != i)
			{
				key_index = (i < 4) ? K_JOY1 : K_AUX1;
				in_state->Key_Event_fp(key_index + i, false);
				joy_oldbuttonstate = i;
			}
		}
	}
#endif
}

void EXPORT RW_IN_Move (usercmd_t *cmd)
{
	if (mouse_avail && app_active && input_active)
	{
		if (m_filter->value)
		{
			mouse_x = (mx + old_mouse_x) * 0.5;
			mouse_y = (my + old_mouse_y) * 0.5;
		}
		else
		{
			mouse_x = mx;
			mouse_y = my;
		}

		old_mouse_x = mx;
		old_mouse_y = my;

		if (mouse_x || mouse_y)
		{
			mouse_x *= sensitivity->value;
			mouse_y *= sensitivity->value;

			if ( (*in_state->in_strafe_state & 1) ||
				(my_lookstrafe->value && mlooking ))
				cmd->sidemove += m_side->value * mouse_x;
			else
				in_state->viewangles[YAW] -= m_yaw->value * mouse_x;

			if ( (mlooking || my_freelook->value) &&
				!(*in_state->in_strafe_state & 1))
				in_state->viewangles[PITCH] += m_pitch->value * mouse_y;
			else
				cmd->forwardmove -= m_forward->value * mouse_y;
		}
	}
	mx = my = 0;
#ifdef HAVE_JOYSTICK
	if (joystick_avail && joy)
	{
		if ( (*in_state->in_strafe_state & 1) ||
			(my_lookstrafe->value && mlooking ))
			cmd->sidemove += m_side->value * (jx/100);
		else
			in_state->viewangles[YAW] -= m_yaw->value * (jx/100);

		if ((mlooking || my_freelook->value) && !(*in_state->in_strafe_state & 1))
		{
			if (j_invert_y)
				in_state->viewangles[PITCH] -= m_pitch->value * (jy/100);
			else
				in_state->viewangles[PITCH] += m_pitch->value * (jy/100);
			cmd->forwardmove -= m_forward->value * (jt/100);
		}
		else
			cmd->forwardmove -= m_forward->value * (jy/100);
		jt = jx = jy = 0;
	}
#endif
}

void EXPORT RW_IN_Frame (void)
{
}

void EXPORT RW_IN_Activate(qboolean active)
{
	if (input_active == active)
		return;

	input_active = active;
	ResetMouseDeltas ();
	UpdateMouseCapture ();

	if (window_active && app_active)
		SDL_ShowCursor (active ? SDL_DISABLE : SDL_ENABLE);
}

/*****************************************************************************/
/* KEYBOARD                                                                  */
/*****************************************************************************/

int XLateKey(SDL_Keycode keysym)
{
	int key = 0;

	switch(keysym)
	{
		case SDLK_KP_9: key = K_KP_PGUP; break;
		case SDLK_PAGEUP: key = K_PGUP; break;
		case SDLK_KP_3: key = K_KP_PGDN; break;
		case SDLK_PAGEDOWN: key = K_PGDN; break;
		case SDLK_KP_7: key = K_KP_HOME; break;
		case SDLK_HOME: key = K_HOME; break;
		case SDLK_KP_1: key = K_KP_END; break;
		case SDLK_END: key = K_END; break;
		case SDLK_KP_4: key = K_KP_LEFTARROW; break;
		case SDLK_LEFT: key = K_LEFTARROW; break;
		case SDLK_KP_6: key = K_KP_RIGHTARROW; break;
		case SDLK_RIGHT: key = K_RIGHTARROW; break;
		case SDLK_KP_2: key = K_KP_DOWNARROW; break;
		case SDLK_DOWN: key = K_DOWNARROW; break;
		case SDLK_KP_8: key = K_KP_UPARROW; break;
		case SDLK_UP: key = K_UPARROW; break;
		case SDLK_ESCAPE: key = K_ESCAPE; break;
		case SDLK_KP_ENTER: key = K_KP_ENTER; break;
		case SDLK_RETURN: key = K_ENTER; break;
		case SDLK_TAB: key = K_TAB; break;
		case SDLK_F1: key = K_F1; break;
		case SDLK_F2: key = K_F2; break;
		case SDLK_F3: key = K_F3; break;
		case SDLK_F4: key = K_F4; break;
		case SDLK_F5: key = K_F5; break;
		case SDLK_F6: key = K_F6; break;
		case SDLK_F7: key = K_F7; break;
		case SDLK_F8: key = K_F8; break;
		case SDLK_F9: key = K_F9; break;
		case SDLK_F10: key = K_F10; break;
		case SDLK_F11: key = K_F11; break;
		case SDLK_F12: key = K_F12; break;
		case SDLK_BACKSPACE: key = K_BACKSPACE; break;
		case SDLK_KP_PERIOD: key = K_KP_DEL; break;
		case SDLK_DELETE: key = K_DEL; break;
		case SDLK_PAUSE: key = K_PAUSE; break;
		case SDLK_LSHIFT:
		case SDLK_RSHIFT: key = K_SHIFT; break;
		case SDLK_LCTRL:
		case SDLK_RCTRL: key = K_CTRL; break;
		case SDLK_LALT:
		case SDLK_RALT: key = K_ALT; break;
		case SDLK_KP_5: key = K_KP_5; break;
		case SDLK_INSERT: key = K_INS; break;
		case SDLK_KP_0: key = K_KP_INS; break;
		case SDLK_KP_MULTIPLY: key = '*'; break;
		case SDLK_KP_PLUS: key = K_KP_PLUS; break;
		case SDLK_KP_MINUS: key = K_KP_MINUS; break;
		case SDLK_KP_DIVIDE: key = K_KP_SLASH; break;
		case SDLK_NUMLOCKCLEAR: key = K_NUMLOCK; break;
		case SDLK_SCROLLLOCK: key = K_SCROLLLOCK; break;
		case SDLK_CAPSLOCK: key = K_CAPSLOCK; break;
		case SDLK_PRINTSCREEN: key = K_PRTSCR; break;
		case SDLK_LGUI: key = K_LWINKEY; break;
		case SDLK_RGUI: key = K_RWINKEY; break;
		case SDLK_APPLICATION: key = K_APP; break;
		default:
			if (keysym > 0 && keysym < 128)
				key = keysym;
			break;
	}

	return key;
}

static int GetRequestedDisplay (void)
{
	int count;
	int index;

	count = SDL_GetNumVideoDisplays ();
	index = vid_display ? vid_display->intvalue : 0;
	if (count <= 0 || index < 0 || index >= count)
		return 0;
	return index;
}

static int CenterWindowOnDisplay (int display_index)
{
	SDL_Rect bounds;

	if (SDL_GetDisplayBounds (display_index, &bounds) < 0)
	{
		ri.Con_Printf (PRINT_ALL, "SDL display bounds failed: %s\n", SDL_GetError());
		return -1;
	}

	SDL_SetWindowPosition (window,
		bounds.x + (bounds.w - (int)vid.width) / 2,
		bounds.y + (bounds.h - (int)vid.height) / 2);
	return 0;
}

static void ConstrainWindowSize (int display_index)
{
	SDL_Rect bounds;

	if (SDL_GetDisplayUsableBounds (display_index, &bounds) < 0 &&
		SDL_GetDisplayBounds (display_index, &bounds) < 0)
		return;

	if ((int)vid.width > bounds.w)
		vid.width = bounds.w;
	if ((int)vid.height > bounds.h)
		vid.height = bounds.h;

	if (bounds.w >= 800 && vid.width < 800)
		vid.width = 800;
	if (bounds.h >= 600 && vid.height < 600)
		vid.height = 600;
	if (vid.width < 1)
		vid.width = 1;
	if (vid.height < 1)
		vid.height = 1;
}

static int ApplyFullscreen (int fullscreen_mode)
{
	int display_index;
	SDL_DisplayMode display_mode;
	SDL_Rect bounds;

	if (!window)
		return -1;

	display_index = GetRequestedDisplay ();

	if (fullscreen_mode == 2)
	{
		if (SDL_GetDisplayBounds (display_index, &bounds) < 0)
		{
			ri.Con_Printf (PRINT_ALL, "SDL desktop bounds failed: %s\n", SDL_GetError());
			return -1;
		}

		vid.width = bounds.w;
		vid.height = bounds.h;
		SDL_SetWindowPosition (window, bounds.x, bounds.y);
		SDL_SetWindowSize (window, bounds.w, bounds.h);
		if (SDL_SetWindowDisplayMode (window, NULL) < 0 ||
			SDL_SetWindowFullscreen (window, SDL_WINDOW_FULLSCREEN_DESKTOP) < 0)
		{
			ri.Con_Printf (PRINT_ALL, "SDL fullscreen desktop failed: %s\n", SDL_GetError());
			return -1;
		}
	}
	else if (fullscreen_mode == 1)
	{
		if (CenterWindowOnDisplay (display_index) < 0 ||
			SDL_GetCurrentDisplayMode (display_index, &display_mode) < 0)
		{
			ri.Con_Printf (PRINT_ALL, "SDL display mode query failed: %s\n", SDL_GetError());
			return -1;
		}

		display_mode.w = vid.width;
		display_mode.h = vid.height;
		if (SDL_SetWindowDisplayMode (window, &display_mode) < 0 ||
			SDL_SetWindowFullscreen (window, SDL_WINDOW_FULLSCREEN) < 0)
		{
			ri.Con_Printf (PRINT_ALL, "SDL fullscreen failed: %s\n", SDL_GetError());
			return -1;
		}
	}
	else
	{
		if (SDL_SetWindowFullscreen (window, 0) < 0)
		{
			ri.Con_Printf (PRINT_ALL, "SDL windowed mode failed: %s\n", SDL_GetError());
			return -1;
		}
		SDL_SetWindowSize (window, vid.width, vid.height);
		if (CenterWindowOnDisplay (display_index) < 0)
			return -1;
	}

	return 0;
}

static void ToggleFullscreen (void)
{
	cvar_t *fullscreen;
	int target;

	if (!window)
		return;

	target = (SDL_GetWindowFlags (window) & SDL_WINDOW_FULLSCREEN) ? 0 : 2;
	if (ApplyFullscreen (target) < 0)
		return;

	fullscreen = ri.Cvar_Get ("vid_fullscreen", "0", 0);
	ri.Cvar_SetValue ("vid_fullscreen", target);
	fullscreen->modified = false;
}

static void GetEvent(SDL_Event *event)
{
	SDL_Scancode scancode;
	int key;
	int steps;
	int i;

	switch(event->type)
	{
	case SDL_MOUSEWHEEL:
		if (event->wheel.windowID != SDL_GetWindowID(window) || !app_active)
			break;
		steps = event->wheel.y;
		if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
			steps = -steps;
		if (steps > 8)
			steps = 8;
		else if (steps < -8)
			steps = -8;
		for (i = 0; i < (steps < 0 ? -steps : steps); i++)
		{
			Keyq_Push (steps > 0 ? K_MWHEELUP : K_MWHEELDOWN, true);
			Keyq_Push (steps > 0 ? K_MWHEELUP : K_MWHEELDOWN, false);
		}
		break;
#ifdef HAVE_JOYSTICK
	case SDL_JOYBUTTONDOWN:
		key = ((((SDL_JoyButtonEvent*)event)->button < 4) ? K_JOY1 : K_AUX1) +
			((SDL_JoyButtonEvent*)event)->button;
		Keyq_Push (key, true);
		break;
	case SDL_JOYBUTTONUP:
		key = ((((SDL_JoyButtonEvent*)event)->button < 4) ? K_JOY1 : K_AUX1) +
			((SDL_JoyButtonEvent*)event)->button;
		Keyq_Push (key, false);
		break;
#endif
	case SDL_KEYDOWN:
		if (event->key.windowID != SDL_GetWindowID(window) || !app_active)
			break;
		scancode = event->key.keysym.scancode;
		if (scancode < 0 || scancode >= SDL_NUM_SCANCODES)
			break;

		if (!event->key.repeat)
		{
			if (key_down[SDL_SCANCODE_LALT] || key_down[SDL_SCANCODE_RALT])
			{
				if (event->key.keysym.sym == SDLK_RETURN)
				{
					ToggleFullscreen ();
					break;
				}
			}

			if ((key_down[SDL_SCANCODE_LCTRL] || key_down[SDL_SCANCODE_RCTRL]) &&
				event->key.keysym.sym == SDLK_g)
			{
				mouse_ungrab_requested = SDL_GetRelativeMouseMode() ? true : false;
				ri.Cvar_SetValue ("_windowed_mouse", mouse_ungrab_requested ? 0 : 1);
				old_windowed_mouse = _windowed_mouse->value;
				UpdateMouseCapture ();
				break;
			}
		}

		if (!key_down[scancode])
		{
			key = XLateKey (event->key.keysym.sym);
			key_down[scancode] = true;
			key_codes[scancode] = key;
			if (key && key_refcount[key] < 255)
			{
				if (!key_refcount[key])
					Keyq_Push (key, true);
				key_refcount[key]++;
			}
		}
		else if (event->key.repeat && key_codes[scancode])
		{
			Keyq_Push (key_codes[scancode], true);
		}
		break;
	case SDL_KEYUP:
		if (event->key.windowID != SDL_GetWindowID(window))
			break;
		scancode = event->key.keysym.scancode;
		if (scancode < 0 || scancode >= SDL_NUM_SCANCODES || !key_down[scancode])
			break;

		key = key_codes[scancode];
		key_down[scancode] = false;
		key_codes[scancode] = 0;
		if (key && key_refcount[key])
		{
			key_refcount[key]--;
			if (!key_refcount[key])
				Keyq_Push (key, false);
		}
		break;
	case SDL_WINDOWEVENT:
		if (event->window.windowID != SDL_GetWindowID(window))
			break;
		switch (event->window.event)
		{
		case SDL_WINDOWEVENT_FOCUS_GAINED:
			HandleAppActive (true);
			break;
		case SDL_WINDOWEVENT_FOCUS_LOST:
		case SDL_WINDOWEVENT_MINIMIZED:
			HandleAppActive (false);
			break;
		case SDL_WINDOWEVENT_CLOSE:
			ri.Cmd_ExecuteText(EXEC_NOW, "quit");
			break;
		}
		break;
	case SDL_QUIT:
		ri.Cmd_ExecuteText(EXEC_NOW, "quit");
		break;
	}
}

void init_joystick(void)
{
#ifdef HAVE_JOYSTICK
	int num_joysticks, i;

	joy = NULL;
	if (!(SDL_INIT_JOYSTICK & SDL_WasInit(SDL_INIT_JOYSTICK)))
		SDL_InitSubSystem(SDL_INIT_JOYSTICK);

	if (in_joystick && in_joystick->value)
	{
		ri.Con_Printf(PRINT_ALL, "Trying to start-up joystick...\n");
		num_joysticks = SDL_NumJoysticks();
		for (i = 0; i < num_joysticks; i++)
		{
			ri.Con_Printf(PRINT_ALL, "Trying joystick [%s]\n", SDL_JoystickNameForIndex(i));
			joy = SDL_JoystickOpen(i);
			if (joy)
			{
				ri.Con_Printf(PRINT_ALL, "Joystick activated.\n");
				joystick_avail = true;
				joy_numbuttons = SDL_JoystickNumButtons(joy);
				break;
			}
		}
		if (!joy)
		{
			ri.Con_Printf(PRINT_ALL, "Failed to open any joysticks\n");
			joystick_avail = false;
		}
	}
	else
	{
		ri.Con_Printf(PRINT_ALL, "Joystick Inactive\n");
		joystick_avail = false;
	}
#endif
}

/*****************************************************************************/
/* VIDEO                                                                     */
/*****************************************************************************/

int SWimp_Init( void *hInstance, void *wndProc )
{
	if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
	{
		ri.Con_Printf (PRINT_ALL, "SDL video initialization failed: %s\n", SDL_GetError());
		return false;
	}
	video_initialized = true;

#ifdef HAVE_JOYSTICK
	init_joystick();
#endif
	return true;
}

void *GLimp_GetProcAddress(const char *func)
{
	return SDL_GL_GetProcAddress(func);
}

int GLimp_Init( void *hInstance, void *wndProc )
{
	return SWimp_Init(hInstance, wndProc);
}

static void SetSDLIcon(void)
{
#include "q2icon.xbm"
	SDL_Surface *icon;
	SDL_Color colors[2];
	int i, mask, pixel;

	icon = SDL_CreateRGBSurface(0, q2icon_width, q2icon_height, 8, 0, 0, 0, 0);
	if (!icon)
		return;

	colors[0].r = colors[0].g = colors[0].b = 255;
	colors[0].a = SDL_ALPHA_OPAQUE;
	colors[1].r = 0;
	colors[1].g = 16;
	colors[1].b = 0;
	colors[1].a = SDL_ALPHA_OPAQUE;
	SDL_SetPaletteColors(icon->format->palette, colors, 0, 2);
	SDL_SetColorKey(icon, SDL_TRUE, 0);

	pixel = 0;
	for (i = 0; i < (int)sizeof(q2icon_bits); i++)
	{
		for (mask = 1; mask != 0x100; mask <<= 1)
		{
			*((Uint8 *)icon->pixels + (pixel / q2icon_width) * icon->pitch +
				(pixel % q2icon_width)) = (q2icon_bits[i] & mask) ? 1 : 0;
			pixel++;
		}
	}

	SDL_SetWindowIcon(window, icon);
	SDL_FreeSurface(icon);
}

static void DestroyGraphicsResources (void)
{
	app_active = false;
	UpdateMouseCapture ();
	ReleaseAllKeys ();

	if (gl_context)
	{
		SDL_GL_DeleteContext (gl_context);
		gl_context = NULL;
	}
	if (window)
	{
		SDL_DestroyWindow (window);
		window = NULL;
	}
	window_active = false;
	SDL_ShowCursor (SDL_ENABLE);
}

static void UpdateDrawableSize (void)
{
	int width, height;

	SDL_GL_GetDrawableSize (window, &width, &height);
	if (width > 0 && height > 0)
	{
		vid.width = width;
		vid.height = height;
	}
	ri.Vid_NewWindow (vid.width, vid.height);
	ri.Con_Printf (PRINT_ALL, "SDL display %d, drawable size %d %d\n",
		GetRequestedDisplay (), vid.width, vid.height);
}

static qboolean GLimp_InitGraphics( int fullscreen_mode )
{
	Uint32 flags;
	Uint32 window_flags;
	int display_index;

	display_index = GetRequestedDisplay ();
	if (!fullscreen_mode)
		ConstrainWindowSize (display_index);

	if (window)
	{
		int width, height;

		SDL_GetWindowSize (window, &width, &height);
		if (width == (int)vid.width && height == (int)vid.height)
		{
			if (ApplyFullscreen (fullscreen_mode) == 0)
			{
				window_active = true;
				app_active = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
				UpdateMouseCapture ();
				UpdateDrawableSize ();
				return true;
			}
		}
		DestroyGraphicsResources ();
	}

	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 5);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 5);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 5);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);

	flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;
	window = SDL_CreateWindow ("Quake II", SDL_WINDOWPOS_CENTERED_DISPLAY (display_index),
		SDL_WINDOWPOS_CENTERED_DISPLAY (display_index), vid.width, vid.height, flags);
	if (!window)
	{
		ri.Con_Printf (PRINT_ALL, "SDL window creation failed: %s\n", SDL_GetError());
		return false;
	}

	SetSDLIcon ();
	gl_context = SDL_GL_CreateContext (window);
	if (!gl_context)
	{
		ri.Con_Printf (PRINT_ALL, "SDL OpenGL context creation failed: %s\n", SDL_GetError());
		DestroyGraphicsResources ();
		return false;
	}

	if (SDL_GL_MakeCurrent (window, gl_context) < 0)
	{
		ri.Con_Printf (PRINT_ALL, "SDL OpenGL activation failed: %s\n", SDL_GetError());
		DestroyGraphicsResources ();
		return false;
	}

	if (ApplyFullscreen (fullscreen_mode) < 0)
	{
		DestroyGraphicsResources ();
		return false;
	}

	window_active = true;
	window_flags = SDL_GetWindowFlags (window);
	app_active = (window_flags & SDL_WINDOW_INPUT_FOCUS) != 0;
	UpdateMouseCapture ();
	SDL_ShowCursor (app_active ? SDL_DISABLE : SDL_ENABLE);
	UpdateDrawableSize ();
	return true;
}

static int last_swap_interval = -2;

static void UpdateSwapInterval (void)
{
	int interval;

	if (!gl_context || !gl_swapinterval)
		return;

	interval = Q_ftol (gl_swapinterval->value);
	if (interval < 0)
		interval = 1;
	if (interval == last_swap_interval)
		return;

	if (SDL_GL_SetSwapInterval (interval) < 0)
		ri.Con_Printf (PRINT_DEVELOPER, "SDL swap interval %d failed: %s\n", interval, SDL_GetError());
	last_swap_interval = interval;
}

void GLimp_BeginFrame(void)
{
	UpdateSwapInterval ();
}

void GLimp_EndFrame (void)
{
	if (window)
		SDL_GL_SwapWindow(window);
}

int GLimp_SetMode( unsigned int *pwidth, unsigned int *pheight, int mode, int fullscreen )
{
	ri.Con_Printf (PRINT_ALL, "setting mode %d:", mode );

	if ( !ri.Vid_GetModeInfo( pwidth, pheight, mode ) )
	{
		ri.Con_Printf( PRINT_ALL, " invalid mode\n" );
		return VID_ERR_INVALID_MODE;
	}

	ri.Con_Printf( PRINT_ALL, " %d %d\n", *pwidth, *pheight);

	if ( !GLimp_InitGraphics( fullscreen ) )
		return fullscreen ? VID_ERR_FULLSCREEN_FAILED : VID_ERR_FAIL;

	last_swap_interval = -2;
	UpdateSwapInterval ();
	return VID_ERR_NONE;
}

void SWimp_Shutdown( void )
{
	DestroyGraphicsResources ();
	if (video_initialized)
	{
		SDL_QuitSubSystem (SDL_INIT_VIDEO);
		video_initialized = false;
	}
}

void GLimp_Shutdown( void )
{
	SWimp_Shutdown();
}

void GLimp_AppActivate( qboolean active )
{
	HandleAppActive (active);
}

void Sys_MakeCodeWriteable (unsigned long startaddr, unsigned long length)
{
	int r;
	unsigned long addr;
	int psize = getpagesize();

	addr = (startaddr & ~(psize-1)) - psize;
	r = mprotect((char*)addr, length + startaddr - addr + psize, 7);
	if (r < 0)
		Sys_Error("Protection change failed\n");
}

/*****************************************************************************/
/* KEYBOARD PUBLIC INTERFACE                                                 */
/*****************************************************************************/

void EXPORT KBD_Init(Key_Event_fp_t fp)
{
	Key_Event_fp = fp;
}

void EXPORT KBD_Update(void)
{
	SDL_Event event;
	static int KBD_Update_Flag;
	int bstate;

	if (KBD_Update_Flag == 1)
		return;
	KBD_Update_Flag = 1;

	if (window_active)
	{
		while (SDL_PollEvent(&event))
			GetEvent(&event);

		if (app_active && mouse_avail)
		{
			if (!mx && !my)
				SDL_GetRelativeMouseState(&mx, &my);
		}
		else
		{
			SDL_GetRelativeMouseState(&mx, &my);
			ResetMouseDeltas ();
		}

#ifdef HAVE_JOYSTICK
		if (joystick_avail && joy)
		{
			jx = SDL_JoystickGetAxis(joy, lr_axis);
			jy = SDL_JoystickGetAxis(joy, ud_axis);
			jt = SDL_JoystickGetAxis(joy, throttle_axis);
		}
#endif
		if (app_active)
			bstate = SDL_GetMouseState(NULL, NULL);
		else
			bstate = 0;

		mouse_buttonstate = 0;
		if (SDL_BUTTON(SDL_BUTTON_LEFT) & bstate)
			mouse_buttonstate |= (1 << 0);
		if (SDL_BUTTON(SDL_BUTTON_RIGHT) & bstate)
			mouse_buttonstate |= (1 << 1);
		if (SDL_BUTTON(SDL_BUTTON_MIDDLE) & bstate)
			mouse_buttonstate |= (1 << 2);
		if (SDL_BUTTON(SDL_BUTTON_X1) & bstate)
			mouse_buttonstate |= (1 << 3);
		if (SDL_BUTTON(SDL_BUTTON_X2) & bstate)
			mouse_buttonstate |= (1 << 4);

		if (old_windowed_mouse != _windowed_mouse->value)
		{
			old_windowed_mouse = _windowed_mouse->value;
			mouse_ungrab_requested = false;
			UpdateMouseCapture ();
		}

		Keyq_Flush ();
	}

	KBD_Update_Flag = 0;
}

void EXPORT KBD_Close(void)
{
	keyq_head = keyq_tail = 0;
	memset(keyq, 0, sizeof(keyq));
	memset(key_down, 0, sizeof(key_down));
	memset(key_codes, 0, sizeof(key_codes));
	memset(key_refcount, 0, sizeof(key_refcount));
	mouse_buttonstate = mouse_oldbuttonstate = 0;
	ResetMouseDeltas ();
}
