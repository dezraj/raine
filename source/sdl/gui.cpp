#include "raine.h"
#include <SDL_image.h>
#include <dirent.h>
#include "display.h"
#include "dejap.h"
#include "games/games.h"
#include "config.h"
#include "hiscore.h"
#include "history.h"
#include "games/default.h"
#include "video/res.h"
#include "control.h"
#include "dsw.h"
#include "sound/sasound.h"
#include "emumain.h"
#include "ingame.h" // print_ingame
#include "files.h" // get_shared
#include "gui.h" // prototypes
#include "bld.h"
#include "profile.h" // fps

#include "gui/menu.h"
#include "dialogs/about.h"
#include "dialogs/game_selection.h"
#include "dialogs/video_options.h"
#include "dialogs/dlg_dsw.h"
#include "dialogs/messagebox.h"
#include "dialogs/gui_options.h"
#include "dialogs/controls.h"
#include "dialogs/game_options.h"
#include "dialogs/fsel.h"
#include "dialogs/neocd_options.h"
#include "dialogs/cheats.h"
#ifdef HAS_CONSOLE
#include "console/console.h"
#include "console/scripts.h"
#endif
#if SDL == 2
#include "sdl2/compat.h"
#include "sdl2/display_sdl.h"
#else
#include "display_sdl.h"
#endif
#include "arpro.h" // CheatCount

#include "neocd/neocd.h"
#include "blit.h"
#include "newspr.h"
#include "neocd/cdrom.h"
#include "str_opaque.h"
#include "control_internal.h"
#include "version.h"
#include "SDL_gfx/SDL_gfxPrimitives.h"
#include "newmem.h"

static int WantScreen;
static int WantQuit;
static int WantPlay;

static void save_menu_config() {
  raine_set_config_int("GUI", "return_mandatory", return_mandatory);
  raine_set_config_int("GUI", "use_transparency", use_transparency);

  raine_set_config_32bit_hex("GUI", "fg_color", fg_color);
  raine_set_config_32bit_hex("GUI", "bg_color", bg_color);
  raine_set_config_32bit_hex("GUI", "fgframe_color", fgframe_color);
  raine_set_config_32bit_hex("GUI", "bgframe_color", bgframe_color);

  raine_set_config_32bit_hex("GUI", "slider_border", cslider_border);
  raine_set_config_32bit_hex("GUI", "slider_bar", cslider_bar);
  raine_set_config_32bit_hex("GUI", "slider_lift", cslider_lift);

  raine_set_config_32bit_hex("GUI", "bg_dialog_bar", bg_dialog_bar);
  raine_set_config_int("GUI", "keep_vga", keep_vga);
}

static void read_menu_config() {
  return_mandatory = raine_get_config_int("GUI", "return_mandatory", 0);
  use_transparency = raine_get_config_int("GUI", "use_transparency", 1);
  fg_color = raine_get_config_hex("GUI", "fg_color", mymakecol(255,255,255));
  bg_color = raine_get_config_hex("GUI", "bg_color", makecol_alpha(0x11,0x07,0x78,128));
  fgframe_color = raine_get_config_hex("GUI", "fgframe_color", mymakecol(255,255,255));
  bgframe_color = raine_get_config_hex("GUI", "bgframe_color", mymakecol(0,0,128));
  cslider_border = raine_get_config_hex("GUI", "slider_border", mymakecol(0,0,0));
  cslider_bar = raine_get_config_hex("GUI", "slider_bar", mymakecol(0xc0,0xc0,0xc0));
  cslider_lift = raine_get_config_hex("GUI", "slider_lift", mymakecol(0xff,0xff,0xff));
  bg_dialog_bar = raine_get_config_hex("GUI", "bg_dialog_bar", mymakecol(0,0,0));
  int a = bg_dialog_bar & 0xff, b = (bg_dialog_bar >> 8) & 0xff, g = (bg_dialog_bar >> 16) & 0xff, r = (bg_dialog_bar >> 24);
  bg_dialog_bar_gfx = makecol_alpha(a,b,g,r);
  keep_vga = raine_get_config_int("GUI","keep_vga",1);
}

static void save_font_config() {
  raine_set_config_int("GUI", "min_font_size", min_font_size);
  raine_set_config_int("GUI", "max_font_size", max_font_size);
}

static void read_font_config() {
  min_font_size = raine_get_config_int("GUI", "min_font_size", 8);
  max_font_size = raine_get_config_int("GUI", "max_font_size", 30);
}

void read_gui_config() {
#if SDL < 2
  repeat_delay = raine_get_config_int("GUI","repeat_delay",SDL_DEFAULT_REPEAT_DELAY);
  repeat_interval = raine_get_config_int("gui","repeat_interval",SDL_DEFAULT_REPEAT_INTERVAL);
#endif
  read_game_list_config();
#if HAS_NEO
  restore_cdrom_config();
  restore_neocd_config();
#endif
  read_font_config();
  read_menu_config();
  opaque_hud = raine_get_config_int("gui","opaque_hud",0);
  silent_hud = raine_get_config_int("gui","silent_hud",0);
  pause_on_focus = raine_get_config_int("gui","pause_on_focus",0);
}

void write_gui_config() {
#if SDL < 2
  raine_set_config_int("GUI","repeat_delay",repeat_delay);
  raine_set_config_int("GUI","repeat_interval",repeat_interval);
#endif
  save_game_list_config();
#if HAS_NEO
  save_cdrom_config();
  save_neocd_config();
#endif
  save_font_config();
  save_menu_config();
  raine_set_config_int("GUI","opaque_hud",opaque_hud);
  raine_set_config_int("GUI","silent_hud",silent_hud);
  raine_set_config_int("GUI","pause_on_focus",pause_on_focus);
}

#if SDL < 2
static int set_repeat_rate(int sel) {
  SDL_EnableKeyRepeat(repeat_delay,
		      repeat_interval);
  return 0;
}

static menu_item_t gui_options_menu[] =
{
{  _("Key repeat delay"), &set_repeat_rate, &repeat_delay, 11, {0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000 } },
{  _("Key repeat interval"), &set_repeat_rate, &repeat_interval, 10, { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 } },
};

int add_gui_options(menu_item_t *menu) {
  menu[0] = gui_options_menu[0];
  menu[1] = gui_options_menu[1];
  return 2;
}
#else
int add_gui_options(menu_item_t *menu) {
    return 0;
}
#endif

static TDialog *loading_dialog;
static int progress_count;
static int curl_progress;

static menu_item_t load_items[] =
{
  { "", },
  { _("Progress"), NULL, &progress_count, ITEM_PROGRESS_BAR },
  { "...", },
  { NULL }, // curl msg
  { _("Progress"), NULL, &curl_progress, ITEM_PROGRESS_BAR },
  { _("from internet archive"), },
  { NULL, },
};

static int last_tick = 0;

void load_progress(char *rom,int count)
{
  int ticks = SDL_GetTicks();
  if (ticks - last_tick < 16 && count < 100)
    return;
  progress_count = count;
  if (loading_dialog) {
    loading_dialog->update_label(2,rom);
    loading_dialog->redraw_fg_layer();
  }
  last_tick = SDL_GetTicks();
}

void setup_curl_dlg(char *name) {
    load_items[3].label = name;
    if (raine_cfg.no_gui)
	return;
#if SDL==1
    loading_dialog->update_bg_layer(NULL);
#endif
    delete loading_dialog;
    loading_dialog = new TDialog(_("Loading Game"),load_items);
    loading_dialog->draw();
}

void curl_progress_f(int count) {
    if (!loading_dialog) return;
    if (curl_progress != count) {
	curl_progress = count;
	if (loading_dialog)
	    loading_dialog->redraw_fg_layer();
    }
}

void load_message(char *msg) {
  load_items[0].label = msg;
  load_items[2].label = " ";
  if (loading_dialog) {
      loading_dialog->update_label(0,msg);
      loading_dialog->update_label(2," ");
      loading_dialog->redraw_fg_layer();
  }
}

static void load_game_proc()
{
    LoadDefault();

    load_error = 0;
    load_debug = (char*)malloc(0x10000);

    load_debug[0] = 0;

    current_game = game_list[raine_cfg.req_game_index];
    fps = current_game->video->fps;
    if (fps < 0.1)
	fps = 60.0; // 60 fps (default)
    if (fps > max_fps)
	fps = max_fps;

    // I have to change the depth BEFORE loading.
    // Probably because of the set_color_mapper in the loading function

#if SDL == 1
    if((display_cfg.auto_mode_change && display_cfg.video_mode == 2) || // normal blits
	     display_cfg.keep_ratio) { // && switch_res(current_game->video))){
      switch_res(current_game->video);
    } else {
      print_debug("no resolution switching wanted\n");
      if (current_game->video->flags & VIDEO_NEEDS_8BPP) {
	bestbpp = 8;
	display_cfg.video_mode = 2; // only possibility for 8bpp
      }
      update_stretch();
    }
#endif

    progress_count = 0;
    curl_progress = 0;
    load_items[0].label = _("Applying GFX Layouts and stuff..."); // init
    if (!raine_cfg.no_gui) {
#if SDL == 1
	adjust_gui_resolution();
#endif
	loading_dialog = new TDialog(_("Loading Game"),load_items);
	loading_dialog->draw();
    }

    load_game_rom_info();

    if (!(load_error & LOAD_FATAL_ERROR)) {
	init_inputs();
	init_dsw();
	init_romsw();
	init_sound();
	read_bld();
    }
}

static void do_load_game(void)
{
     char neocd_wm_title[160];

   /*

   satisfy the request

   */

   raine_cfg.req_load_game = 0;

   /*

   close down the current game (if there is one)

   */

   if(current_game){


      ClearDefault();
   }


   load_game_proc(); // load without gui

   // Check if the load was successful, if ok, load game config, if bad clear the mess

   switch(load_error&3){
   case LOAD_WARNING:			// WARNING - IT MIGHT RUN OK

     strcat(load_debug,"\n\nThe game might not run correctly.");
     MessageBox(_("Loading problem"), load_debug);

   case 0x00:				// SUCCESS
     memcpy(&prev_display_cfg, &display_cfg, sizeof(DISPLAY_CFG));

     if (bestbpp > 0 && bestbpp <=32) {
       WantScreen = 1;
       display_cfg.bpp = bestbpp;
       bestbpp = 0;
     }

     if (bestw && besth && display_cfg.video_mode == 2) {
       //display_cfg.screen_type = bestmode;
       display_cfg.screen_x = bestw;
       display_cfg.screen_y = besth;
       WantScreen = 1;
       bestw = besth = 0;
     }

     reset_game_hardware(); // must be done BEFORE load_game_config for scripts

     load_game_config();

     if (loading_dialog) {
	 delete loading_dialog;
	 load_items[3].label = NULL;
	 loading_dialog = NULL;
     }
     if (raine_cfg.save_game_screen_settings)
       WantScreen=1;
     sprintf(neocd_wm_title,"Raine %s - %s",VERSION,current_game->long_name);
     SDL_WM_SetCaption(neocd_wm_title,neocd_wm_title);
     break;
   case LOAD_FATAL_ERROR:			// FATAL ERROR - REMOVE GAME
   case LOAD_FATAL_ERROR|LOAD_WARNING:

     MessageBox(_("Loading error"),load_debug);

     ClearDefault();

     current_game = NULL;

     break;
   }

   free(load_debug);
}

static int play_game(int sel) {
  WantPlay = 1;
  return 1;
}

static int quit_raine(int sel) {
  WantQuit = 1;
  return 1;
}

static int region_code;

static int set_region(int sel) {
  SetLanguageSwitch(region_code);
  return 0;
}

#if HAS_NEO
static int load_neo_game(int sel) {
  char res[1024];
  char *exts[] = { ".zip",
      // iso handled specifically by fsel, if no cue is found but iso or iso.gz
      // is found, then a special list of extensions is used
  // ".iso",
  ".cue", "ipl.txt", "7z", NULL };
  fsel(neocd_dir,exts,res,_("Load NeoCD game"));
  return load_neo_from_name(res);
}
#endif

extern int do_sound_options(int sel);

static menu_item_t main_items[] =
{
{ _("Play game"), &play_game, },
{ _("Game options"), &do_game_options },
{ _("Region"), &set_region, },
{ _("Action replay cheats"), &do_cheats, },
{ _("Dipswitches"), &do_dlg_dsw, },
{ _("Change/Load ROM"), &do_game_sel },
#if HAS_NEO
{ _("Load NeoCD game"), &load_neo_game },
{ _("NeoCD/Neo-Geo options"), &do_neocd_options },
#endif
{ _("Video options"), &do_video_options },
{ _("Sound options"), &do_sound_options },
{ _("Options"), &do_gui_options },
{ _("Inputs"), &do_controls },
{ _("About..."), &do_about,},
{ _("Show command.dat info"), &show_moves },
#ifdef HAS_CONSOLE
{ _("Console"), &do_console, },
#endif
{ _("Quit"), &quit_raine, },
{ NULL, NULL, NULL },
};

int TMain_menu::can_be_displayed(int n) {
    switch(n) {
    case 0: // Play Game
    case 1: // game options
	return current_game != NULL;
    case 2: // Region
	return current_game != NULL && current_game->romsw != NULL;
    case 3: // cheats
	return current_game != NULL && (CheatCount > 0
#ifdef HAS_CONSOLE
		|| nb_scripts > 0
#endif
		);
    case 4: // dsw
	return current_game != NULL && current_game->dsw != NULL;
    default:
	if (strstr(main_items[n].label,"command.dat"))
	    return nb_commands > 0;
	return 1;
    }
}

char* TMain_menu::get_top_string() {
    return EMUNAME " " VERSION;
}

void TMain_menu::draw_top_frame() {
    int w_title,h_title;
    font->dimensions(title,&w_title,&h_title);
#if SDL==1
    boxColor(sdl_screen,0,0,sdl_screen->w,h_title-1,bg_frame);
    font->put_string(HMARGIN,0,get_top_string(),fg_frame,bg_frame);
    font->put_string(sdl_screen->w-w_title,0,title,fg_frame,bg_frame);
#else
    boxColor(rend,0,0,sdl_screen->w,h_title-1,bg_frame_gfx);
    font->put_string(HMARGIN,0,get_top_string(),fg_frame,bg_frame);
    font->put_string(sdl_screen->w-w_title,0,title,fg_frame,bg_frame);
#endif
}

char *TMain_menu::get_bot_frame_text() {
  int size = GetMemoryPoolSize()/1024; // Minimum Kb
  static char game[100];
  snprintf(game,100,"%s",(current_game ? current_game->long_name :_("No game loaded")));
  if (current_game) {
      snprintf(&game[strlen(game)], 100-strlen(game),
	      " (%s)",current_game->main_name);
      if (size < 1024)
	  snprintf(&game[strlen(game)],100-strlen(game),
		  " (%d Kb)",size);
      else
	  snprintf(&game[strlen(game)], 100-strlen(game),
		  " (%d Mb)",size/1024);
  }
  game[99] = 0;
  return game;
}

static void do_main_menu() {
  int old_region;
  // init romsw
  if (current_game && current_game->romsw) {
    main_items[2].values_list_size = LanguageSw.Count;
    main_items[2].value_int = &region_code;
    old_region = region_code = GetLanguageSwitch();
    for (int n=0; n<LanguageSw.Count; n++) {
      main_items[2].values_list[n] = n;
      main_items[2].values_list_label[n] = LanguageSw.data[n].Mode;
    }
  }

  TMain_menu *main_menu = new TMain_menu(_("Main menu"),main_items);
  main_menu->execute();
  if (current_game && current_game->romsw && old_region != region_code)
      reset_game_hardware();
  delete main_menu;
}

void setup_font()
{
  if (ingame_font) {
    return;
  }
  ingame_font = (UINT8*)calloc(1,1792);
  FILE *f = fopen (get_shared("font6x8.bin"), "rb");
  if (!f)
    f = fopen(get_shared("fonts/font6x8.bin"),"rb");
  if (f) {
   fread(ingame_font,1,1792,f);
   fclose(f);
  } else {
    printf("no font, byebye !\n");
    exit(1);
  }
}

#if !defined(SDL) || SDL==1
extern UINT32 videoflags; // display.c
#endif
int goto_debuger = 0;

#if SDL == 2
static int my_resize(int sx, int sy) {
    return resize(1,sx,sy);
}
#endif

void StartGUI(void)
{
#if SDL == 2
    resize_hook = &my_resize;
#else
    setup_mouse_cursor(IMG_Load("bitmaps/cursor.png"));
#endif
    get_shared_hook = &get_shared;

#ifdef RAINE_DEBUG
    print_debug("StartGUI(): START\n");
#endif

    init_display();

    setup_font(); // Usefull even without gui for the messages on screen

    WantScreen=0;
    WantQuit=0;
    WantPlay=0;

#ifdef RAINE_DEBUG
    print_debug("StartGUI(): prepare international\n");
#endif

#if SDL < 2
   // We need to know if we have hw overlay support, and the only way to know
   // is to try to create one...
    if (display_cfg.video_mode == 1 && !sdl_overlay && display_cfg.bpp >= 16) {
	sdl_create_overlay(display_cfg.screen_x,display_cfg.screen_y);
    }
#endif

   while(!WantQuit){		// ----- Main Loop ------

       /*

	  check for load game request (from the command line)

*/

       if(raine_cfg.req_load_game)

	   do_load_game();

       if(!raine_cfg.no_gui)	// GUI MODE
       {
#ifdef HAS_CONSOLE
	   if (goto_debuger) {
	       do_console(0);
	       WantPlay = 1;
	   } else
#endif
	   {
	       do_main_menu();
	       if (!WantQuit && !WantPlay && !raine_cfg.req_load_game && current_game)
		   WantPlay = 1;
	   }
       } else {

	   WantQuit=1;
	   ScreenChange();

	   if(current_game)

	       WantPlay = 1;

	   else

	       WantPlay = 0;
       }

       if(current_game && (display_cfg.auto_mode_change && display_cfg.video_mode == 2)) {
	   switch_res(current_game->video);
	   if (bestw) {
	       display_cfg.screen_x = bestw;
	       display_cfg.screen_y = besth;
	       bestw = besth = 0;
	   }
       }

       /*
	* ScreenChange is now called at the end of execute when exiting from the gui
       if(!WantQuit &&
	       (WantScreen || display_cfg.screen_x != sdl_screen->w ||
	       display_cfg.screen_y != sdl_screen->h ||
	       (display_cfg.bpp != sdl_screen->format->BitsPerPixel && !sdl_overlay) ||
	       ((videoflags & SDL_DOUBLEBUF) && !(sdl_screen->flags & SDL_DOUBLEBUF)))){
	   // Are we here for a screenchange?
	   WantScreen=0;
	   print_debug("calling ScreenChange from StartGUI, WantScreen=0\n");
	   ScreenChange();
       }*/

       clear_raine_screen();

       if(WantPlay){		// Are we able to and wanting to play?
	   WantPlay = 0;
	   SDL_ShowCursor(0);
#ifndef RAINE_DEBUG
	   if (GameMouse)
	       SDL_WM_GrabInput(SDL_GRAB_ON);
#endif
#if SDL == 2
	   if (GameScreen.xview) {
	       if (display_cfg.video_mode > 0)
		   SDL_RenderSetLogicalSize(rend, GameScreen.xview, GameScreen.yview);
	       else if (display_cfg.video_mode == 0)
		   ScreenChange();
	   }
#endif
	   if(run_game_emulation()){ // In no gui mode, tab will reactivate the gui (req. by bubbles)
	       raine_cfg.no_gui = 0;
	       WantQuit = 0;
	   }
#if SDL == 2
	   if (display_cfg.video_mode > 0)
	       SDL_RenderSetLogicalSize(rend, 0,0);
#else
	   if (!(sdl_screen->flags & SDL_DOUBLEBUF) && !emulate_mouse_cursor)
#endif
	       SDL_ShowCursor(1);
	   if (GameMouse)
	       SDL_WM_GrabInput(SDL_GRAB_OFF);
       }

   }			// ----- Main Loop -----

   /*

   close down the current game (if there is one)

   */

   if(current_game){

     ClearDefault();

     current_game = NULL;
   }

}

void *old_draw = NULL;

void switch_mixer() {
  print_ingame(60,_("no mixer yet"));
}

