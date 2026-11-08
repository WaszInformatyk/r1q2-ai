#include <SDL.h>

#include "../client/client.h"
#include "../client/qmenu.h"

#define REF_OPENGL	0
#define VIDEO_MODE_CUSTOM	-1
#define VIDEO_MODE_DESKTOP	-2
#define MAX_SDL_DISPLAYS	8

extern cvar_t *vid_ref;
extern cvar_t *vid_fullscreen;
extern cvar_t *vid_display;
extern cvar_t *vid_width;
extern cvar_t *vid_height;
extern cvar_t *vid_gamma;
extern cvar_t *scr_viewsize;

static cvar_t *gl_mode;
static cvar_t *gl_driver;
static cvar_t *gl_picmip;
static cvar_t *gl_ext_palettedtexture;

static cvar_t *sw_mode;
static cvar_t *sw_stipplealpha;

static cvar_t *_windowed_mouse;

extern void M_ForceMenuOff( void );

/*
====================================================================

MENU INTERACTION

====================================================================
*/
#define SOFTWARE_MENU 0
#define OPENGL_MENU   1

static menuframework_s  s_software_menu;
static menuframework_s	s_opengl_menu;
static menuframework_s *s_current_menu;
static int				s_current_menu_index;

static menulist_s		s_mode_list[2];
static menulist_s		s_ref_list[2];
static menulist_s		s_display_list[2];
static menufield_s		s_width_field;
static menufield_s		s_height_field;
static menuslider_s		s_tq_slider;
static menuslider_s		s_screensize_slider[2];
static menuslider_s		s_brightness_slider[2];
static menulist_s  		s_fs_box[2];
static menulist_s  		s_stipple_box;
static menulist_s  		s_paletted_texture_box;
static menulist_s  		s_windowed_mouse;
static menuaction_s		s_apply_action[2];
static menuaction_s		s_defaults_action[2];

static char s_display_names[MAX_SDL_DISPLAYS][80];
static const char *s_display_name_ptrs[MAX_SDL_DISPLAYS + 1];

static void DriverCallback( void *unused )
{
	s_ref_list[0].curvalue = s_ref_list[1].curvalue = REF_OPENGL;
	s_current_menu = &s_opengl_menu;
	s_current_menu_index = OPENGL_MENU;
}

static void ScreenSizeCallback( void *s )
{
	menuslider_s *slider = ( menuslider_s * ) s;

	Cvar_SetValue( "viewsize", slider->curvalue * 10 );
}

static void BrightnessCallback( void *s )
{
	menuslider_s *slider = ( menuslider_s * ) s;

	if ( s_current_menu_index == 0)
		s_brightness_slider[1].curvalue = s_brightness_slider[0].curvalue;
	else
		s_brightness_slider[0].curvalue = s_brightness_slider[1].curvalue;

	if ( Q_stricmp( vid_ref->string, "soft" ) == 0 ||
		 Q_stricmp( vid_ref->string, "softx" ) == 0 )
	{
		float gamma = ( 0.8 - ( slider->curvalue/10.0 - 0.5 ) ) + 0.5;

		Cvar_SetValue( "vid_gamma", gamma );
	}
}

static void ResetDefaults( void *unused )
{
	VID_MenuInit();
}

static void ApplyChanges( void *unused )
{
	float gamma;
	int mode_selection;

	/*
	** make values consistent
	*/
	s_fs_box[!s_current_menu_index].curvalue = s_fs_box[s_current_menu_index].curvalue;
	s_brightness_slider[!s_current_menu_index].curvalue = s_brightness_slider[s_current_menu_index].curvalue;
	s_ref_list[!s_current_menu_index].curvalue = s_ref_list[s_current_menu_index].curvalue;
	s_display_list[!s_current_menu_index].curvalue = s_display_list[s_current_menu_index].curvalue;

	/*
	** invert sense so greater = brighter, and scale to a range of 0.5 to 1.3
	*/
	gamma = ( 0.8 - ( s_brightness_slider[s_current_menu_index].curvalue/10.0 - 0.5 ) ) + 0.5;
	mode_selection = s_mode_list[OPENGL_MENU].curvalue;

	Cvar_SetValue( "vid_gamma", gamma );
	Cvar_SetValue( "sw_stipplealpha", s_stipple_box.curvalue );
	Cvar_SetValue( "gl_picmip", 3 - s_tq_slider.curvalue );
	Cvar_SetValue( "vid_fullscreen", s_fs_box[s_current_menu_index].curvalue );
	Cvar_SetValue( "vid_display", s_display_list[s_current_menu_index].curvalue );
	Cvar_SetValue( "gl_ext_palettedtexture", s_paletted_texture_box.curvalue );

	if (mode_selection == 13)
		Cvar_SetValue( "gl_mode", VIDEO_MODE_DESKTOP );
	else if (mode_selection == 14)
		Cvar_SetValue( "gl_mode", VIDEO_MODE_CUSTOM );
	else
		Cvar_SetValue( "gl_mode", mode_selection + 4 );

	if (mode_selection == 14)
	{
		int width = atoi (s_width_field.buffer);
		int height = atoi (s_height_field.buffer);

		if (width < 800)
			width = 800;
		if (height < 600)
			height = 600;
		if (width > 16384)
			width = 16384;
		if (height > 16384)
			height = 16384;
		Cvar_SetValue( "vid_width", width );
		Cvar_SetValue( "vid_height", height );
	}
	Cvar_SetValue( "_windowed_mouse", s_windowed_mouse.curvalue);

	Cvar_Set( "vid_ref", "gl" );
	Cvar_Set( "gl_driver", "opengl32" );

	M_ForceMenuOff();
}

/*
** VID_MenuInit
*/
void VID_MenuInit( void )
{
	static const char *resolutions[] = 
	{
		"[800 600   ]",
		"[960 720   ]",
		"[1024 768  ]",
		"[1152 864  ]",
		"[1280 1024 ]",
		"[1600 1200 ]",
		"[1280 720  ]",
		"[1366 768  ]",
		"[1600 900  ]",
		"[1920 1080 ]",
		"[2560 1440 ]",
		"[3440 1440 ]",
		"[3840 2160 ]",
		"[desktop    ]",
		"[custom     ]",
		0
	};
	static const char *refs[] =
	{
		"[default OpenGL]",
		0
	};
	static const char *yesno_names[] =
	{
		"no",
		"yes",
		0
	};
	static const char *fullscreen_names[] =
	{
		"windowed",
		"fullscreen",
		"fullscreen desktop",
		0
	};
	SDL_DisplayMode display_mode;
	const char *display_name;
	int i, display_count;

	if ( !gl_driver )
		gl_driver = Cvar_Get( "gl_driver", "opengl32", 0 );
	if ( !gl_picmip )
		gl_picmip = Cvar_Get( "gl_picmip", "0", 0 );
	if ( !gl_mode )
		gl_mode = Cvar_Get( "gl_mode", "4", 0 );
	if ( !sw_mode )
		sw_mode = Cvar_Get( "sw_mode", "0", 0 );
	if ( !gl_ext_palettedtexture )
		gl_ext_palettedtexture = Cvar_Get( "gl_ext_palettedtexture", "1", CVAR_ARCHIVE );

	if ( !sw_stipplealpha )
		sw_stipplealpha = Cvar_Get( "sw_stipplealpha", "0", CVAR_ARCHIVE );

	if ( !_windowed_mouse)
        _windowed_mouse = Cvar_Get( "_windowed_mouse", "0", CVAR_ARCHIVE );

	s_mode_list[SOFTWARE_MENU].curvalue = sw_mode->value;
	if (gl_mode->intvalue == VIDEO_MODE_DESKTOP)
		s_mode_list[OPENGL_MENU].curvalue = 13;
	else if (gl_mode->intvalue == VIDEO_MODE_CUSTOM)
		s_mode_list[OPENGL_MENU].curvalue = 14;
	else
	{
		s_mode_list[OPENGL_MENU].curvalue = gl_mode->intvalue - 4;
		if ( s_mode_list[OPENGL_MENU].curvalue < 0 )
			s_mode_list[OPENGL_MENU].curvalue = 0;
		else if ( s_mode_list[OPENGL_MENU].curvalue > 12 )
			s_mode_list[OPENGL_MENU].curvalue = 12;
	}

	display_count = SDL_GetNumVideoDisplays ();
	if (display_count < 1)
		display_count = 1;
	else if (display_count > MAX_SDL_DISPLAYS)
		display_count = MAX_SDL_DISPLAYS;

	for (i = 0; i < display_count; i++)
	{
		display_name = SDL_GetDisplayName (i);
		if (!display_name)
			display_name = "unknown";
		if (SDL_GetCurrentDisplayMode (i, &display_mode) < 0)
			display_mode.w = display_mode.h = 0;
		Com_sprintf (s_display_names[i], sizeof(s_display_names[i]),
			"%d: %.32s %dx%d", i, display_name, display_mode.w, display_mode.h);
		s_display_name_ptrs[i] = s_display_names[i];
	}
	s_display_name_ptrs[display_count] = NULL;
	s_display_list[OPENGL_MENU].curvalue = vid_display->intvalue;
	if (s_display_list[OPENGL_MENU].curvalue < 0)
		s_display_list[OPENGL_MENU].curvalue = 0;
	else if (s_display_list[OPENGL_MENU].curvalue >= display_count)
		s_display_list[OPENGL_MENU].curvalue = display_count - 1;

	if ( !scr_viewsize )
		scr_viewsize = Cvar_Get ("viewsize", "100", CVAR_ARCHIVE);

	s_screensize_slider[SOFTWARE_MENU].curvalue = scr_viewsize->value/10;
	s_screensize_slider[OPENGL_MENU].curvalue = scr_viewsize->value/10;

	s_current_menu = &s_opengl_menu;
	s_current_menu_index = OPENGL_MENU;
	s_ref_list[0].curvalue = s_ref_list[1].curvalue = REF_OPENGL;

	s_software_menu.x = viddef.width * 0.50;
	s_software_menu.nitems = 0;
	s_opengl_menu.x = viddef.width * 0.50;
	s_opengl_menu.nitems = 0;

	for ( i = 0; i < 2; i++ )
	{
		s_ref_list[i].generic.type = MTYPE_SPINCONTROL;
		s_ref_list[i].generic.name = "driver";
		s_ref_list[i].generic.x = 0;
		s_ref_list[i].generic.y = 0;
		s_ref_list[i].generic.callback = DriverCallback;
		s_ref_list[i].itemnames = refs;

		s_mode_list[i].generic.type = MTYPE_SPINCONTROL;
		s_mode_list[i].generic.name = "video mode";
		s_mode_list[i].generic.x = 0;
		s_mode_list[i].generic.y = 10;
		s_mode_list[i].itemnames = resolutions;

		s_display_list[i].generic.type = MTYPE_SPINCONTROL;
		s_display_list[i].generic.name = "display";
		s_display_list[i].generic.x = 0;
		s_display_list[i].generic.y = 20;
		s_display_list[i].itemnames = s_display_name_ptrs;
		if (i == SOFTWARE_MENU)
			s_display_list[i].curvalue = 0;
		else
			s_display_list[i].curvalue = s_display_list[OPENGL_MENU].curvalue;

		s_screensize_slider[i].generic.type	= MTYPE_SLIDER;
		s_screensize_slider[i].generic.x		= 0;
		s_screensize_slider[i].generic.y		= 30;
		s_screensize_slider[i].generic.name	= "screen size";
		s_screensize_slider[i].minvalue = 3;
		s_screensize_slider[i].maxvalue = 12;
		s_screensize_slider[i].generic.callback = ScreenSizeCallback;

		s_brightness_slider[i].generic.type	= MTYPE_SLIDER;
		s_brightness_slider[i].generic.x	= 0;
		s_brightness_slider[i].generic.y	= 40;
		s_brightness_slider[i].generic.name	= "brightness";
		s_brightness_slider[i].generic.callback = BrightnessCallback;
		s_brightness_slider[i].minvalue = 5;
		s_brightness_slider[i].maxvalue = 13;
		s_brightness_slider[i].curvalue = ( 1.3 - vid_gamma->value + 0.5 ) * 10;

		s_fs_box[i].generic.type = MTYPE_SPINCONTROL;
		s_fs_box[i].generic.x	= 0;
		s_fs_box[i].generic.y	= 50;
		s_fs_box[i].generic.name	= "fullscreen";
		s_fs_box[i].itemnames = fullscreen_names;
		s_fs_box[i].curvalue = vid_fullscreen->intvalue;
		if (s_fs_box[i].curvalue < 0)
			s_fs_box[i].curvalue = 0;
		else if (s_fs_box[i].curvalue > 2)
			s_fs_box[i].curvalue = 2;

		s_defaults_action[i].generic.type = MTYPE_ACTION;
		s_defaults_action[i].generic.name = "reset to default";
		s_defaults_action[i].generic.x    = 0;
		s_defaults_action[i].generic.y    = 110;
		s_defaults_action[i].generic.callback = ResetDefaults;

		s_apply_action[i].generic.type = MTYPE_ACTION;
		s_apply_action[i].generic.name = "apply";
		s_apply_action[i].generic.x    = 0;
		s_apply_action[i].generic.y    = 120;
		s_apply_action[i].generic.callback = ApplyChanges;
	}

	s_stipple_box.generic.type = MTYPE_SPINCONTROL;
	s_stipple_box.generic.x	= 0;
	s_stipple_box.generic.y	= 60;
	s_stipple_box.generic.name	= "stipple alpha";
	s_stipple_box.curvalue = sw_stipplealpha->value;
	s_stipple_box.itemnames = yesno_names;

	s_windowed_mouse.generic.type = MTYPE_SPINCONTROL;
	s_windowed_mouse.generic.x  = 0;
	s_windowed_mouse.generic.y  = 72;
	s_windowed_mouse.generic.name   = "windowed mouse";
	s_windowed_mouse.curvalue = _windowed_mouse->value;
	s_windowed_mouse.itemnames = yesno_names;

	s_width_field.generic.type = MTYPE_FIELD;
	s_width_field.generic.name = "custom width";
	s_width_field.generic.flags = QMF_NUMBERSONLY;
	s_width_field.generic.x = 0;
	s_width_field.generic.y = 60;
	s_width_field.length = 5;
	s_width_field.visible_length = 10;
	Q_strncpy (s_width_field.buffer, Cvar_VariableString ("vid_width"), sizeof(s_width_field.buffer)-1);

	s_height_field.generic.type = MTYPE_FIELD;
	s_height_field.generic.name = "custom height";
	s_height_field.generic.flags = QMF_NUMBERSONLY;
	s_height_field.generic.x = 0;
	s_height_field.generic.y = 70;
	s_height_field.length = 5;
	s_height_field.visible_length = 10;
	Q_strncpy (s_height_field.buffer, Cvar_VariableString ("vid_height"), sizeof(s_height_field.buffer)-1);

	s_tq_slider.generic.type	= MTYPE_SLIDER;
	s_tq_slider.generic.x		= 0;
	s_tq_slider.generic.y		= 80;
	s_tq_slider.generic.name	= "texture quality";
	s_tq_slider.minvalue = 0;
	s_tq_slider.maxvalue = 3;
	s_tq_slider.curvalue = 3-gl_picmip->value;

	s_paletted_texture_box.generic.type = MTYPE_SPINCONTROL;
	s_paletted_texture_box.generic.x	= 0;
	s_paletted_texture_box.generic.y	= 90;
	s_paletted_texture_box.generic.name	= "8-bit textures";
	s_paletted_texture_box.itemnames = yesno_names;
	s_paletted_texture_box.curvalue = gl_ext_palettedtexture->value;

	Menu_AddItem( &s_software_menu, ( void * ) &s_ref_list[SOFTWARE_MENU] );
	Menu_AddItem( &s_software_menu, ( void * ) &s_mode_list[SOFTWARE_MENU] );
	Menu_AddItem( &s_software_menu, ( void * ) &s_screensize_slider[SOFTWARE_MENU] );
	Menu_AddItem( &s_software_menu, ( void * ) &s_brightness_slider[SOFTWARE_MENU] );
	Menu_AddItem( &s_software_menu, ( void * ) &s_fs_box[SOFTWARE_MENU] );
	Menu_AddItem( &s_software_menu, ( void * ) &s_stipple_box );
	Menu_AddItem( &s_software_menu, ( void * ) &s_windowed_mouse );

	Menu_AddItem( &s_opengl_menu, ( void * ) &s_ref_list[OPENGL_MENU] );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_mode_list[OPENGL_MENU] );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_display_list[OPENGL_MENU] );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_screensize_slider[OPENGL_MENU] );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_brightness_slider[OPENGL_MENU] );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_fs_box[OPENGL_MENU] );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_width_field );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_height_field );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_tq_slider );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_paletted_texture_box );

	Menu_AddItem( &s_software_menu, ( void * ) &s_defaults_action[SOFTWARE_MENU] );
	Menu_AddItem( &s_software_menu, ( void * ) &s_apply_action[SOFTWARE_MENU] );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_defaults_action[OPENGL_MENU] );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_apply_action[OPENGL_MENU] );

	Menu_Center( &s_software_menu );
	Menu_Center( &s_opengl_menu );
	s_opengl_menu.x -= 8;
	s_software_menu.x -= 8;
}

/*
================
VID_MenuDraw
================
*/
void VID_MenuDraw (void)
{
	int w, h;

	s_current_menu_index = OPENGL_MENU;
	s_current_menu = &s_opengl_menu;

	/*
	** draw the banner
	*/
	re.DrawGetPicSize( &w, &h, "m_banner_video" );
	re.DrawPic( viddef.width / 2 - w / 2, viddef.height /2 - 110, "m_banner_video" );

	/*
	** move cursor to a reasonable starting position
	*/
	Menu_AdjustCursor( s_current_menu, 1 );

	/*
	** draw the menu
	*/
	Menu_Draw( s_current_menu );
}

/*
================
VID_MenuKey
================
*/
const char *VID_MenuKey( int key )
{
	extern void M_PopMenu( void );

	menuframework_s *m = s_current_menu;
	static const char *sound = "misc/menu1.wav";

	switch ( key )
	{
	case K_ESCAPE:
		M_PopMenu();
		return NULL;
	case K_UPARROW:
		m->cursor--;
		Menu_AdjustCursor( m, -1 );
		break;
	case K_DOWNARROW:
		m->cursor++;
		Menu_AdjustCursor( m, 1 );
		break;
	case K_LEFTARROW:
		Menu_SlideItem( m, -1 );
		break;
	case K_RIGHTARROW:
		Menu_SlideItem( m, 1 );
		break;
	case K_ENTER:
		Menu_SelectItem( m );
		break;
	}

	return sound;
}


