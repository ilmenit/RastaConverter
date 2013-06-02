#include <allegro.h> 
#undef int8_t
#undef uint8_t
#undef int16_t
#undef uint16_t
#undef int32_t
#undef uint32_t
#undef int64_t
#undef uint64_t
#include <stdint.h>
#include "rasta.h"

extern bool quiet;
int screen_color_depth;
int desktop_width;
int desktop_height;

void quit_function(void);
void close_button_procedure();
void switch_in_callback(void);

bool LoadAtariPalette(string filename);
void create_cycles_table();

RastaConverter rasta;

// redraw destination pictures
void switch_in_callback(void)
{
	acquire_screen();
	clear_bitmap(screen);
	release_screen();
	rasta.ShowDestinationBitmap();
	rasta.ShowInputBitmap();
}

int main(int argc, char *argv[])
{	
	//////////////////////////////////////////////////////////////////////////
	allegro_init(); // Initialize Allegro
	install_keyboard();
	set_close_button_callback(quit_function);
	FreeImage_Initialise(TRUE);

	create_cycles_table();

	Configuration cfg;
	cfg.Process(argc, argv);

	if (cfg.continue_processing)
	{
		quiet=true;
		rasta.Resume();
		rasta.cfg.continue_processing=true;
		quiet=false;
	}
	else
		rasta.SetConfig(cfg);

	screen_color_depth = desktop_color_depth();
	get_desktop_resolution(&desktop_width,&desktop_height);
	set_color_depth(screen_color_depth);

	if (!rasta.cfg.preprocess_only)
	{
		if (desktop_width>=320*3)
			set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320*3,480,0,0); 
		else
			set_gfx_mode(GFX_AUTODETECT_WINDOWED, 640, 480,0,0); // Change our graphics mode to 640x480

		set_display_switch_mode(SWITCH_BACKGROUND);
		set_window_close_hook(close_button_procedure);
		set_display_switch_callback(SWITCH_IN, switch_in_callback);

		set_window_title(rasta.cfg.command_line.c_str());
	}
	else
		quiet=true;

	rasta.LoadAtariPalette();

	rasta.LoadInputBitmap();
	if (rasta.ProcessInit())
	{
		rasta.FindBestSolution();
		rasta.SaveBestSolution();
	}
	return 0; // Exit with no errors
}

END_OF_MAIN() // This must be called right after the closing bracket of your MAIN function.
// It is Allegro specific.