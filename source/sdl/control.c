/******************************************************************************/ /*                                                                            */
/*                 CONTROL SUPPORT [KEYBOARD/JOYSTICK/LEDS]                   */
/*                                                                            */
/******************************************************************************/
/*
  How the controls work :

  the game drivers expect a bit to be cleared or set when a specific control
  changes (like a button pressed/released or a movement of the stick).
  So for each bit of the controls to test, we have the index in our array of
  controls to test.

  For example :
   { KB_DEF_P1_B1,        MSG_P1_B1,               0x000001, 0x10, BIT_ACTIVE_0 },

   means : the key for p1_b1, which is called MSG_P1_B1 in the gui should change
   bitmask 0x10 at offset 1 in the controls so that the bit is 0 if the key is
   pressed.

   For the keyboard, KB_DEF_P1_B1 just gives an index in the allegro key array
   to check.
   For the joysticks, we reproduce the same behaviour by updating our own
   rjoy array based on the joystick changes (see update_rjoy_lists)
*/

#include <SDL.h>
#include "deftypes.h"
#include "games.h"
#include "control.h"
#include "demos.h"
#include "ingame.h"
#include "savegame.h"           // Save/Load game stuff
#include "profile.h" // switch_fps_mode
#include "emumain.h" // key_pause_fwd
#include "sdl/display_sdl.h"
#include "blit.h" // SetupScreenBitmap
#include "video/newspr.h" // init_video_core
#include "control_internal.h"
#include "display.h"
#include "bezel.h"
#ifdef HAS_CONSOLE
#include "sdl/console/console.h"
#endif
#include "sdl/dialogs/cheats.h"
#include "sound/sasound.h"

/* The difference in the sdl version :
 * instead of looping for every frame in all the available inputs to the game
 * (which is not so long after all), I tried to do the opposite : when we
 * receive a key up/down event, then find the corresponding input of the game,
 * and update it accordingly. It should be slightly faster, but in fact it's
 * probably impossible to measure. And the key array is still necessary to be
 * able to make an input which was invalid to become valid when another key
 * goes up (see the comments about that in the handling of the KEYUP event.
 *
 * Anyway this method to handle the inputs based on events is much more
 * convinient for the gui inputs at least, so we won something from it for
 * sure ! :) */

SDL_Joystick *joy[MAX_JOY];
char *joy_name[MAX_JOY];
int bad_axes[MAX_JOY*MAX_AXIS];
char analog_name[80]; // analog device saved by name because its index
// can change if it's pluged differently

int analog_num,analog_stick,analog_minx,analog_maxx,analog_miny,
  analog_maxy,analog_normx,analog_normy,
  app_state = SDL_APPMOUSEFOCUS|SDL_APPINPUTFOCUS, pause_on_focus;
// analog_normx & normy are the normalized position of the stick after
// calibration (between -16384 and +16384 inclusive).
Uint8 key[0x300];
UINT8 input_buffer[0x100];
int GameMouse,use_leds;
int use_custom_keys;
int joy_use_custom_keys;

/* Removed led handling for now. In SDL there is no direct support for that
 * and it's obvious that having direct access to the keyboard to be able
 * to manipulate the leds will be a problem in a multitasking OS */

void switch_led(int a, int b) {
}

volatile int *MouseB;
UINT32 p1_trackball_x;
UINT32 p1_trackball_y;
int mouse_x,mouse_y,mouse_b;
// min/max coords for the mouse
static int min_x,max_x,min_y,max_y;

static int mickey_x, mickey_y;

void set_mouse_range(int x1, int y1, int x2, int y2) {
  min_x = x1;
  min_y = y1;
  max_x = x2;
  max_y = y2;
  if (mouse_x > max_x || mouse_x < min_x) mouse_x = min_x;
  if (mouse_y > max_y || mouse_y < min_y) mouse_y = min_y;
}

void my_get_mouse_mickeys(int *mx, int *my) {
  *mx = mickey_x;
  *my = mickey_y;
}

void (*GetMouseMickeys)(int *mx,int *my) = &my_get_mouse_mickeys;

/******************************************************************************/
/*                                                                            */
/*                        DEFAULT GAME KEY SETTINGS                           */
/*                                                                            */
/******************************************************************************/

typedef struct joystick_state {
  int pos_axis[MAX_AXIS];
  UINT8 hat[MAX_HAT];
} joystick_state;

joystick_state jstate[MAX_JOY];

// must be global for the controls dialog
struct DEF_INPUT def_input[KB_DEF_COUNT] =
{
#ifdef RAINE_WIN32
 { SDLK_3,       JOY(1,0,10,0), 0, "Def Coin A",           },      // KB_DEF_COIN1,
#else
 { SDLK_z,       JOY(1,0,10,0), 0, "Def Coin A",           },      // KB_DEF_COIN1,
#endif
 { SDLK_4,       JOY(2,0,10,0), 0, "Def Coin B",           },      // KB_DEF_COIN2,
 { SDLK_7,       JOY(3,0,10,0), 0, "Def Coin C",           },      // KB_DEF_COIN3,
 { SDLK_8,       JOY(4,0,10,0), 0, "Def Coin D",           },      // KB_DEF_COIN4,

 { SDLK_t,       0x00, 0, "Def Tilt",             },      // KB_DEF_TILT,
 { SDLK_y,       0x00, 0, "Def Service",          },      // KB_DEF_SERVICE,
 { SDLK_u,       0x00, 0, "Def Test",             },      // KB_DEF_TEST,

#ifdef RAINE_WIN32
 { SDLK_1,       JOY(1,0,9,0), 0, "Def P1 Start",         },      // KB_DEF_P1_START,
#else
 { SDLK_a,       JOY(1,0,9,0), 0, "Def P1 Start",         },      // KB_DEF_P1_START,
#endif

 { SDLK_UP,      JOY(1,AXIS_LEFT(1),0,0), 0, "Def P1 Up",            },      // KB_DEF_P1_UP,
 { SDLK_DOWN,    JOY(1,AXIS_RIGHT(1),0,0), 0, "Def P1 Down",          },      // KB_DEF_P1_DOWN,
 { SDLK_LEFT,    JOY(1,AXIS_LEFT(0),0,0), 0, "Def P1 Left",          },      // KB_DEF_P1_LEFT,
 { SDLK_RIGHT,   JOY(1,AXIS_RIGHT(0),0,0), 0, "Def P1 Right",         },      // KB_DEF_P1_RIGHT,

 { SDLK_v,       JOY(1,0,1,0), 1, "Def P1 Button 1",      },      // KB_DEF_P1_B1,
 { SDLK_b,       JOY(1,0,2,0), 3, "Def P1 Button 2",      },      // KB_DEF_P1_B2,
 { SDLK_n,       JOY(1,0,3,0), 2, "Def P1 Button 3",      },      // KB_DEF_P1_B3,
 { SDLK_g,       JOY(1,0,4,0), 0, "Def P1 Button 4",      },      // KB_DEF_P1_B4,
 { SDLK_h,       JOY(1,0,5,0), 0, "Def P1 Button 5",      },      // KB_DEF_P1_B5,
 { SDLK_j,       JOY(1,0,6,0), 0, "Def P1 Button 6",      },      // KB_DEF_P1_B6,
 { SDLK_m,       JOY(1,0,7,0), 0, "Def P1 Button 7",      },      // KB_DEF_P1_B7,
 { SDLK_k,       JOY(1,0,8,0), 0, "Def P1 Button 8",      },      // KB_DEF_P1_B8,

 { SDLK_2,       JOY(2,0,9,0), 0, "Def P2 Start",         },      // KB_DEF_P2_START,

 { SDLK_s,       JOY(2,AXIS_LEFT(1),0,0), 0, "Def P2 Up",            },      // KB_DEF_P2_UP,
 { SDLK_x,       JOY(2,AXIS_RIGHT(1),0,0), 0, "Def P2 Down",          },      // KB_DEF_P2_DOWN,
 { SDLK_z,       JOY(2,AXIS_LEFT(0),0,0), 0, "Def P2 Left",          },      // KB_DEF_P2_LEFT,
 { SDLK_c,       JOY(2,AXIS_RIGHT(0),0,0), 0, "Def P2 Right",         },      // KB_DEF_P2_RIGHT,

 { SDLK_q,       JOY(2,0,1,0), 0, "Def P2 Button 1",      },      // KB_DEF_P2_B1,
 { SDLK_w,       JOY(2,0,2,0), 0, "Def P2 Button 2",      },      // KB_DEF_P2_B2,
 { SDLK_e,       JOY(2,0,3,0), 0, "Def P2 Button 3",      },      // KB_DEF_P2_B3,
 { SDLK_r,       JOY(2,0,4,0), 0, "Def P2 Button 4",      },      // KB_DEF_P2_B4,
 { SDLK_a,       JOY(2,0,5,0), 0, "Def P2 Button 5",      },      // KB_DEF_P2_B5,
 { SDLK_d,       JOY(2,0,6,0), 0, "Def P2 Button 6",      },      // KB_DEF_P2_B6,
 { SDLK_f,       JOY(2,0,7,0), 0, "Def P2 Button 7",      },      // KB_DEF_P2_B7,
 { SDLK_g,       JOY(2,0,8,0), 0, "Def P2 Button 8",      },      // KB_DEF_P2_B8,

 { SDLK_5,       JOY(3,0,9,0), 0, "Def P3 Start",         },      // KB_DEF_P3_START,

 { 0,       JOY(3,AXIS_LEFT(1),0,0), 0, "Def P3 Up",            },      // KB_DEF_P2_UP,
 { 0,       JOY(3,AXIS_RIGHT(1),0,0), 0, "Def P3 Down",          },      // KB_DEF_P2_DOWN,
 { 0,       JOY(3,AXIS_LEFT(0),0,0), 0, "Def P3 Left",          },      // KB_DEF_P2_LEFT,
 { 0,       JOY(3,AXIS_RIGHT(0),0,0), 0, "Def P3 Right",         },      // KB_DEF_P2_RIGHT,

 { 0x00,        JOY(3,0,1,0), 0, "Def P3 Button 1",      },      // KB_DEF_P3_B1,
 { 0x00,        JOY(3,0,2,0), 0, "Def P3 Button 2",      },      // KB_DEF_P3_B2,
 { 0x00,        JOY(3,0,3,0), 0, "Def P3 Button 3",      },      // KB_DEF_P3_B3,
 { 0x00,        JOY(3,0,4,0), 0, "Def P3 Button 4",      },      // KB_DEF_P3_B4,
 { 0x00,        JOY(3,0,5,0), 0, "Def P3 Button 5",      },      // KB_DEF_P3_B5,
 { 0x00,        JOY(3,0,6,0), 0, "Def P3 Button 6",      },      // KB_DEF_P3_B6,
 { 0x00,        JOY(3,0,7,0), 0, "Def P3 Button 7",      },      // KB_DEF_P3_B7,
 { 0x00,        JOY(3,0,8,0), 0, "Def P3 Button 8",      },      // KB_DEF_P3_B8,

 { SDLK_6,       JOY(4,0,9,0), 0, "Def P4 Start",         },      // KB_DEF_P4_START,

 { 0,       JOY(4,AXIS_LEFT(1),0,0), 0, "Def P4 Up",            },      // KB_DEF_P2_UP,
 { 0,       JOY(4,AXIS_RIGHT(1),0,0), 0, "Def P4 Down",          },      // KB_DEF_P2_DOWN,
 { 0,       JOY(4,AXIS_LEFT(0),0,0), 0, "Def P4 Left",          },      // KB_DEF_P2_LEFT,
 { 0,       JOY(4,AXIS_RIGHT(0),0,0), 0, "Def P4 Right",         },      // KB_DEF_P2_RIGHT,

 { 0x00,        JOY(4,0,1,0), 0, "Def P4 Button 1",      },      // KB_DEF_P4_B1,
 { 0x00,        JOY(4,0,2,0), 0, "Def P4 Button 2",      },      // KB_DEF_P4_B2,
 { 0x00,        JOY(4,0,3,0), 0, "Def P4 Button 3",      },      // KB_DEF_P4_B3,
 { 0x00,        JOY(4,0,4,0), 0, "Def P4 Button 4",      },      // KB_DEF_P4_B4,
 { 0x00,        JOY(4,0,5,0), 0, "Def P4 Button 5",      },      // KB_DEF_P4_B5,
 { 0x00,        JOY(4,0,6,0), 0, "Def P4 Button 6",      },      // KB_DEF_P4_B6,
 { 0x00,        JOY(4,0,7,0), 0, "Def P4 Button 7",      },      // KB_DEF_P4_B7,
 { 0x00,        JOY(4,0,8,0), 0, "Def P4 Button 8",      },      // KB_DEF_P4_B8,

 { SDLK_LCTRL,0x00, 0, "Def Flipper 1 Left",   },      // KB_DEF_FLIPPER_1_L,
 { SDLK_RCTRL,0x00, 0, "Def Flipper 1 Right",  },      // KB_DEF_FLIPPER_1_R,
 { SDLK_LSHIFT,  0x00, 0, "Def Flipper 2 Left",   },      // KB_DEF_FLIPPER_2_L,
 { SDLK_RSHIFT,  0x00, 0, "Def Flipper 2 Right",  },      // KB_DEF_FLIPPER_2_R,
 { SDLK_BACKSLASH,0x00, 0, "Def Tilt Left",        },      // KB_DEF_TILT_L,
 { SDLK_SLASH,   0x00, 0, "Def Tilt Right",       },      // KB_DEF_TILT_R,
 { SDLK_z,       0x00, 0, "Def Button 1 Left",    },      // KB_DEF_B1_L,
 { SDLK_e,    0x00, 0, "Def Button 1 Right",   },      // KB_DEF_B1_R,

 // Mahjong controls, at least in mahjong quest...
 { SDLK_a,       0x00, 0, "Def P1 A",             }, // KB_DEF_P1_A
 { SDLK_e,       0x00, 0, "Def P1 E",             }, // KB_DEF_P1_E
 { SDLK_i,       0x00, 0, "Def P1 I",             }, // KB_DEF_P1_I
 { SDLK_m,       0x00, 0, "Def P1 M",             }, // KB_DEF_P1_M
 { SDLK_LCTRL,0x00, 0, "Def P1 Kan",           }, // KB_DEF_P1_KAN
 { SDLK_b,       0x00, 0, "Def P1 B",             }, // KB_DEF_P1_B
 { SDLK_f,       0x00, 0, "Def P1 F",             }, // KB_DEF_P1_F
 { SDLK_j,       0x00, 0, "Def P1 J",             }, // KB_DEF_P1_J
 { SDLK_n,       0x00, 0, "Def P1 N",             }, // KB_DEF_P1_N
 { SDLK_LSHIFT,  0x00, 0, "Def P1 Reach",         }, // KB_DEF_P1_REACH
 { SDLK_c,       0x00, 0, "Def P1 C",             }, // KB_DEF_P1_C
 { SDLK_g,       0x00, 0, "Def P1 G",             }, // KB_DEF_P1_G
 { SDLK_k,       0x00, 0, "Def P1 K",             }, // KB_DEF_P1_K
 { SDLK_SPACE,   0x00, 0, "Def P1 Chi",           }, // KB_DEF_P1_CHI
 { SDLK_z,       0x00, 0, "Def P1 Ron",           }, // KB_DEF_P1_RON,
 { SDLK_d,       0x00, 0, "Def P1 D",             }, // KB_DEF_P1_D
 { SDLK_h,       0x00, 0, "Def P1 H",             }, // KB_DEF_P1_H
 { SDLK_l,       0x00, 0, "Def P1 L",             }, // KB_DEF_P1_L
 { SDLK_LALT,     0x00, 0, "Def P1 Pon",           }, // KB_DEF_P1_PON

 { 0,           0, 0, "Def Service A", }, // KB_DEF_SERVICE_A
 { 0,           0, 0, "Def Service B", }, // KB_DEF_SERVICE_B
 { 0,           0, 0, "Def Service C", }, // KB_DEF_SERVICE_C

 { SDLK_r,       0x00, 0, "Def Button 2 Left",    },      // KB_DEF_B2_L,
 { SDLK_t,    0x00, 0, "Def Button 2 Right",   },      // KB_DEF_B2_R,

 { 0,           0, 0, "Player1 B1+B2", }, // p1_b1B2
 { 0,           0, 0, "Player1 B3+B4", },
 { 0,           0, 0, "Player1 B2+B3", },
 { 0,           0, 0, "Player1 B1+B2+B3", },
 { 0,           0, 0, "Player1 B2+B3+B4", },

 { 0,           0, 0, "Player2 B1+B2", }, // p2_b1B2
 { 0,           0, 0, "Player2 B3+B4", },
 { 0,           0, 0, "Player2 B2+B3", },
 { 0,           0, 0, "Player2 B1+B2+B3", },
 { 0,           0, 0, "Player2 B2+B3+B4", },
};

/******************************************************************************/
/*                                                                            */
/*                       DEFAULT EMULATOR KEY SETTINGS                        */
/*                                                                            */
/******************************************************************************/

static void key_save_screen(void)
{
   raine_cfg.req_save_screen = 1;
}

static void key_quit() {
    // Violent quit, equivalent of closing the window
    if (recording)
	end_recording();
    exit(1);
}

static void frame_skip_up(void)
{
   if((display_cfg.frame_skip<9) && (display_cfg.frame_skip)){
      display_cfg.frame_skip++;
      print_ingame(120,"Drawing Every %1d Frames",display_cfg.frame_skip);
   }
}

static void frame_skip_down(void)
{
   if((display_cfg.frame_skip>1) && (display_cfg.frame_skip)){
      display_cfg.frame_skip--;
      if(display_cfg.frame_skip==1)
	 print_ingame(120,"Drawing All Frames");
      else
	 print_ingame(120,"Drawing Every %1d Frames",display_cfg.frame_skip);
   }
}

extern void cpu_speed_up(); // emumain.c
extern void cpu_slow_down(); // emumain.c

static void key_pause_game(void)
{
	raine_cfg.req_pause_game ^= 1;
	if (!raine_cfg.req_pause_game)
	    EndDrawPaused();
}

static void toggle_limit_speed() {
	if(display_cfg.limit_speed){
		print_ingame(60,"No speed limit !");
		display_cfg.limit_speed = 0;
	} else {
		print_ingame(60,"Speed limit %d fps",fps);
		display_cfg.limit_speed = 1;
	}
}

extern void key_stop_emulation_esc(void);
extern void key_stop_emulation_tab(void);

void toggle_fullscreen() {
  if (display_cfg.fullscreen) {
    display_cfg.fullscreen = 0;
    display_cfg.screen_x = display_cfg.winx;
    display_cfg.screen_y = display_cfg.winy;
  } else {
    display_cfg.fullscreen = 1;
    display_cfg.winx = display_cfg.screen_x;
    display_cfg.winy = display_cfg.screen_y;
  }
  resize();
  SetupScreenBitmap();
  if (current_game) {
    init_video_core();
    reset_ingame_timer();
  }
}

#ifdef HAS_CONSOLE
static void call_console() {
  do_console(0);
  reset_ingame_timer();
}
#endif

static void call_cheats() {
  do_cheats(0);
  reset_ingame_timer();
}

int get_console_key() {
  int n;
  int nb = raine_get_emu_nb_ctrl();
  for (n=nb-1; n>=0; n--) {
    if (!strcmp(def_input_emu[n].name,"Console")) {
      int code = def_input_emu[n].scancode;
      return code;
    }
  }
  return 0;
}

static struct DEF_INPUT_EMU *driver_emu_list = NULL;
static int driver_nb_emu_inputs;

// must be global for the controls dialog
struct DEF_INPUT_EMU def_input_emu[] =
{
 { SDLK_s | (KMOD_LCTRL<<16),       0x00,           "Save Screenshot", key_save_screen     },
 { SDLK_RETURN | (KMOD_LCTRL<<16),       0x00,           "Fullscreen", toggle_fullscreen     },
 { SDLK_PAGEUP,    0x00,           "Increase frameskip", frame_skip_up  },
 { SDLK_PAGEDOWN,    0x00,           "Decrease frameskip", frame_skip_down  },
 { SDLK_HOME,    0x00,           "Increase cpu skip",    cpu_speed_up},
 { SDLK_END,     0x00,           "Decrease cpu skip",    cpu_slow_down},
 // You must keep this one the 6th input, see special handling (KEYUP event)
 { SDLK_DELETE,  0x00,           "Toggle limit speed",   toggle_limit_speed},
 { SDLK_F2,      0x00,           "Save game",            GameSave},
 { SDLK_F3,      0x00,           "Switch save slot",     next_save_slot},
 { SDLK_F4,      0x00,           "Load game",            GameLoad},
 { SDLK_F11,     0x00,           "Switch fps display",   switch_fps_mode},
 { SDLK_F1,      0x00,           "Reset game",           reset_game_hardware},
 { SDLK_p,       0x00,           "Pause game",           key_pause_game},
 { SDLK_ESCAPE,     0x00,           "Stop emulation",    key_stop_emulation_esc},
 { SDLK_TAB,     0x00,           "Return to gui",        key_stop_emulation_tab},
#if 0
 { SDLK_UP | (KMOD_CTRL<<16),      0,      "Screen up",  key_pause_scroll_up},
 { SDLK_DOWN | (KMOD_CTRL<<16),    0,    "Screen down",  key_pause_scroll_down},
 { SDLK_LEFT | (KMOD_CTRL<<16),    0,    "Screen left",  key_pause_scroll_left},
 { SDLK_RIGHT | (KMOD_CTRL<<16),   0,   "Screen right",  key_pause_scroll_right},
 // { SDLK_WORLD_18,   0x00,           "Switch Mixer", switch_mixer },
 { SDLK_F2 | (KMOD_CTRL<<16), 0x00, "Save game with name", GameSaveName },
 { SDLK_F4 | (KMOD_CTRL<<16), 0x00, "Load game with name", GameLoadName },
 { SDLK_F2 | (KMOD_SHIFT<<16), 0x00, "Save demo", GameSaveDemo },
 { SDLK_F4 | (KMOD_SHIFT<<16), 0x00, "Load demo", GameLoadDemo },
#endif
 { SDLK_SPACE, 0x00, "Fwd 1 frame in pause", key_pause_fwd},
#ifdef HAS_CONSOLE
 { 31 /* TILDE */, 0x00, "Console", call_console},
#endif
 { SDLK_c | (KMOD_ALT<<16), 0x00, "Cheats", call_cheats},
 { SDLK_F4 | (KMOD_ALT<<16),       0x00,           "Quit w/o saving", key_quit     },
};

struct INPUT InputList[MAX_INPUTS];	// Max 64 control inputs in a game

int InputCount;			// Number of Inputs in InputList

static void set_key_from_default(INPUT *inp)
{

    inp->Key = def_input[inp->default_key & 0xFF].scancode;
    inp->Joy = def_input[inp->default_key & 0xFF].joycode;
    inp->mousebtn = def_input[inp->default_key & 0xFF].mousebtn;

}

static void update_input_buffer(int ta, int input_valid) {
  UINT8 *buffer;

  if(InputList[ta].Address < 0x100)
     buffer = input_buffer;
   else
     buffer = RAM;

  buffer += InputList[ta].Address;

  if(input_valid ^ InputList[ta].high_bit)
    *buffer &= ~ InputList[ta].Bit;
   else
     *buffer |=   InputList[ta].Bit;
}

static int nb_valid_inputs;
// the number of valid inputs at the same time : rather 2 or 3 max, but we'll
// take some margin...
#define MAX_VALID_INPUTS 20
int valid_inputs[MAX_VALID_INPUTS]; // a list
static UINT8 autofire_timer[6];
static UINT8  stick_logic[4];

void init_inputs(void)
{
   const INPUT_INFO *input_src;

   memset(key,0,sizeof(key));
   memset(jstate,0,sizeof(jstate));
   memset(autofire_timer,0,sizeof(autofire_timer));
   memset(stick_logic,0,sizeof(stick_logic));

   nb_valid_inputs = 0;

   InputCount = 0;

   input_src = current_game->input;

   if(input_src){

     while(input_src[InputCount].name){

       InputList[InputCount].default_key = input_src[InputCount].default_key;
       InputList[InputCount].InputName   = input_src[InputCount].name;
       InputList[InputCount].Address     = input_src[InputCount].offset;
       InputList[InputCount].Bit         = input_src[InputCount].bit_mask;
       InputList[InputCount].high_bit    = input_src[InputCount].flags;
       InputList[InputCount].auto_rate   = 0;
       InputList[InputCount].active_time = 0;
       InputList[InputCount].link = 0;

       set_key_from_default(&InputList[InputCount]);

       update_input_buffer(InputCount,0); // say input is not valid for now

       InputCount++;
     }

   }

}

void release_inputs(void)
{
  if (!current_game) return;
  const INPUT_INFO *input_src;
  int ta;
  // just release the inputs when coming back from the gui...

  memset(key,0,sizeof(key));
  memset(jstate,0,sizeof(jstate));
  memset(autofire_timer,0,sizeof(autofire_timer));
  memset(stick_logic,0,sizeof(stick_logic));

  nb_valid_inputs = 0;
  ta = 0;

  input_src = current_game->input;

  if(input_src && RAM){
    // Check for RAM because if loading a game fails, input_src is defined
    // but not ram

    while(input_src[ta].name){

      update_input_buffer(ta,0); // say input is not valid for now

      ta++;
    }

  }
}

void reset_game_keys(void)
{
   int ta;

   for(ta=0;ta<InputCount;ta++) {
     if (InputList[ta].link == 0 || InputList[ta].link > ta)
       set_key_from_default(&InputList[ta]);
   }
}

void no_spaces(char *str)
{
   int ta,tb;

   tb=strlen(str);

   for(ta=0;ta<tb;ta++){
      if(((str[ta]<'A')||(str[ta]>'Z'))&&
         ((str[ta]<'a')||(str[ta]>'z'))&&
         ((str[ta]<'0')||(str[ta]>'9'))){
         str[ta]='_';
      }
   }
}

static void load_emu_keys(char *section, struct DEF_INPUT_EMU *list_emu, int nb) {
  int ta,scan;
  char key_name[64];
   for(ta=0;ta<nb;ta++){
      sprintf(key_name,"%s",list_emu[ta].name);
      no_spaces(key_name);
      scan = raine_get_config_int(section,key_name,list_emu[ta].scancode);
      if (strncmp(key_name,"Screen_",7) || scan < SDLK_LEFT || scan > SDLK_DOWN) {
	// Forces modifiers for scrolling keys (previously in pause only)
	list_emu[ta].scancode = scan;
      }
   }
}

void load_game_keys(char *section)
{
   int ta;
   char key_name[64],other_name[64];

   use_custom_keys = raine_get_config_int(section,"use_custom_keys",0);
   // load keys if using custom keys

   if(use_custom_keys){

      for(ta=0;ta<InputCount;ta++){
	int link;
	if (InputList[ta].link && InputList[ta].link < ta)
	  continue;
	sprintf(key_name,"%s",InputList[ta].InputName);
	no_spaces(key_name);
	InputList[ta].Key = raine_get_config_int(section,key_name,InputList[ta].Key);
	sprintf(other_name,"%s_joystick",key_name);
	InputList[ta].Joy = raine_get_config_int(section,other_name,InputList[ta].Joy);
	sprintf(other_name,"%s_mouse",key_name);
	InputList[ta].mousebtn = raine_get_config_int(section,other_name,InputList[ta].mousebtn);
	sprintf(other_name,"%s_auto_rate",key_name);
	InputList[ta].auto_rate = raine_get_config_int(section,other_name,0);
	sprintf(other_name,"%s_link",key_name);
	link = raine_get_config_int(section,other_name,0);
	if (link) {
	  printf("link from %d to %d\n",ta,link);
	  InputList[ta].link = link;
	  InputList[link] = InputList[ta];
	  InputList[link].link = ta;
	  sprintf(other_name,"%s_linked_key",key_name);
	  InputList[link].Key = raine_get_config_int(section,other_name,0);
	  sprintf(other_name,"%s_linked_joy",key_name);
	  InputList[link].Joy = raine_get_config_int(section,other_name,0);
	  sprintf(other_name,"%s_linked_mouse",key_name);
	  InputList[link].mousebtn = raine_get_config_int(section,other_name,0);
	  sprintf(other_name,"%s_linked_auto_rate",key_name);
	  InputList[link].auto_rate = raine_get_config_int(section,other_name,0);
	  InputCount = link+1;
	}
      }
   }
  if (driver_nb_emu_inputs)
    load_emu_keys(section,driver_emu_list,driver_nb_emu_inputs);
}

void save_game_keys(char *section)
{
   int ta;
   char key_name[64],other_name[64];

   // clear the old settings first (keep the file tidy)

   raine_clear_config_section(section);

   // save keys if using custom keys

   if(use_custom_keys){

      raine_set_config_int(section,"use_custom_keys",use_custom_keys);

      for(ta=0;ta<InputCount;ta++){
	 if (InputList[ta].link && InputList[ta].link < ta)
	   continue;
         sprintf(key_name,"%s",InputList[ta].InputName);
         no_spaces(key_name);
	 if (InputList[ta].Key != def_input[InputList[ta].default_key & 0xff].scancode)
	   raine_set_config_int(section,key_name,InputList[ta].Key);
	 if (InputList[ta].Joy != def_input[InputList[ta].default_key & 0xff].joycode) {
	   sprintf(other_name,"%s_joystick",key_name);
	   raine_set_config_int(section,other_name,InputList[ta].Joy);
	 }

	 if (InputList[ta].mousebtn != def_input[InputList[ta].default_key & 0xff].mousebtn) {
	   sprintf(other_name,"%s_mouse",key_name);
	   raine_set_config_int(section,other_name,InputList[ta].mousebtn);
	 }
	 if (InputList[ta].auto_rate) {
	   sprintf(other_name,"%s_auto_rate",key_name);
	   raine_set_config_int(section,other_name,InputList[ta].auto_rate);
	 }
	 if (InputList[ta].link > ta) {
	   int link = InputList[ta].link;
	   sprintf(other_name,"%s_link",key_name);
	   raine_set_config_int(section,other_name,link);
	   if (InputList[link].Key) {
	     sprintf(other_name,"%s_linked_key",key_name);
	     raine_set_config_int(section,other_name,InputList[link].Key);
	   }
	   if (InputList[link].Joy) {
	     sprintf(other_name,"%s_linked_joy",key_name);
	     raine_set_config_int(section,other_name,InputList[link].Joy);
	   }
	   if (InputList[link].mousebtn) {
	     sprintf(other_name,"%s_linked_mouse",key_name);
	     raine_set_config_int(section,other_name,InputList[link].mousebtn);
	   }
	   sprintf(other_name,"%s_linked_auto_rate",key_name);
	   raine_set_config_int(section,other_name,InputList[link].auto_rate);
	 }
      }
   }
   if (driver_nb_emu_inputs) {
     // custom emu keys...
     for(ta=0;ta<driver_nb_emu_inputs;ta++){
       sprintf(key_name,"%s",driver_emu_list[ta].name);
       no_spaces(key_name);
       raine_set_config_int(section,key_name,driver_emu_list[ta].scancode);
     }
     driver_nb_emu_inputs = 0;
     driver_emu_list = NULL;
   }
}

void load_default_keys(char *section)
{
   int ta;
   char key_name[64],other_name[64];

   use_custom_keys = 0;

   strncpy(analog_name,raine_get_config_string(section,"analog_name",""),40);
   analog_name[79] = 0;
   analog_num = -1;
   if (analog_name[0]) {
     int n;
     for (n=0; n<SDL_NumJoysticks(); n++) {
       if (!strcmp(analog_name,joy_name[n])) {
	 printf("Found analog device %s\n",analog_name);
	 analog_num = n;
	 break;
       }
     }
     if (analog_num > -1) {
       analog_stick= raine_get_config_int(section,"analog_stick",0);
       analog_minx = raine_get_config_int(section,"analog_minx",0);
       analog_maxx = raine_get_config_int(section,"analog_maxx",0);
       analog_miny = raine_get_config_int(section,"analog_miny",0);
       analog_maxy = raine_get_config_int(section,"analog_maxy",0);
     }
   }

   for(ta=0;ta<KB_DEF_COUNT;ta++){
      sprintf(key_name,"%s",def_input[ta].name);
      no_spaces(key_name);
      def_input[ta].scancode = raine_get_config_int(section,key_name,def_input[ta].scancode);
      sprintf(other_name,"%s_joystick",key_name);
      def_input[ta].joycode = raine_get_config_int(section,other_name,def_input[ta].joycode);
      sprintf(other_name,"%s_mouse",key_name);
      def_input[ta].mousebtn = raine_get_config_int(section,other_name,def_input[ta].mousebtn);

   }

   for (ta=0; ta<MAX_LAYER_INFO; ta++) {
     sprintf(key_name,"key_layer_%d",ta);
     int code = SDLK_F5+ta;
     if (code == SDLK_F11) code = SDLK_F12; // skip F11, it's used by the fps
     if (code > SDLK_F12) code = SDLK_F12;
     layer_info_list[ta].keycode = raine_get_config_int(section,key_name,code);
   }
}

void save_default_keys(char *section)
{
   int ta;
   char key_name[64],other_name[64];

   if (analog_num >= 0) {
     raine_set_config_string(section,"analog_name",analog_name);
     raine_set_config_int(section,"analog_stick",analog_stick);
     raine_set_config_int(section,"analog_minx",analog_minx);
     raine_set_config_int(section,"analog_maxx",analog_maxx);
     raine_set_config_int(section,"analog_miny",analog_miny);
     raine_set_config_int(section,"analog_maxy",analog_maxy);
   }

   for(ta=0;ta<KB_DEF_COUNT;ta++){
      sprintf(key_name,"%s",def_input[ta].name);
      no_spaces(key_name);
      raine_set_config_int(section,key_name,def_input[ta].scancode);
      sprintf(other_name,"%s_joystick",key_name);
      raine_set_config_int(section,other_name,def_input[ta].joycode);
      sprintf(other_name,"%s_mouse",key_name);
      raine_set_config_int(section,other_name,def_input[ta].mousebtn);
   }

   for (ta=0; ta<MAX_LAYER_INFO; ta++) {
     sprintf(key_name,"key_layer_%d",ta);
     raine_set_config_int(section,key_name,layer_info_list[ta].keycode);
   }
}

int raine_get_emu_nb_ctrl() {
  return sizeof(def_input_emu)/sizeof(DEF_INPUT_EMU);
}

void load_emulator_keys(char *section) {
  int nb = raine_get_emu_nb_ctrl();
  load_emu_keys(section,def_input_emu,nb);
}

void register_driver_emu_keys(struct DEF_INPUT_EMU *list, int nb) {
  driver_emu_list = list;
  driver_nb_emu_inputs = nb;
}

void save_emulator_keys(char *section)
{
   int ta;
   char key_name[64];
   int nb = raine_get_emu_nb_ctrl();

   for(ta=0;ta<nb;ta++){
      sprintf(key_name,"%s",def_input_emu[ta].name);
      no_spaces(key_name);
      raine_set_config_int(section,key_name,def_input_emu[ta].scancode);
   }
}

void load_emulator_joys(char *section)
{
   int ta;
   char joy_name[64];
   int nb = raine_get_emu_nb_ctrl();

   for(ta=0;ta<nb;ta++){
      sprintf(joy_name,"%s",def_input_emu[ta].name);
      no_spaces(joy_name);
      def_input_emu[ta].joycode = raine_get_config_int(section,joy_name,def_input_emu[ta].joycode);
   }
}

void save_emulator_joys(char *section)
{
   int ta;
   char joy_name[64];
   int nb = raine_get_emu_nb_ctrl();

   for(ta=0;ta<nb;ta++){
      sprintf(joy_name,"%s",def_input_emu[ta].name);
      no_spaces(joy_name);
      raine_set_config_int(section,joy_name,def_input_emu[ta].joycode);
   }
}

void update_rjoy_list(void)
{
}

static int pulse_time;

void set_pulse_time(int time)
{
  pulse_time = time;
}

static int find_input_from_keysym(int sym, int start) {
  int ta;
  for (ta=start; ta<InputCount; ta++) {
    if (InputList[ta].Key == sym) {
      return ta;
    }
  }


  return -1;
}

static int find_input_from_joy(int event, int start) {
  int ta;
  for (ta=start; ta<InputCount; ta++) {
    if (InputList[ta].Joy == event) {
      return ta;
    }
  }

  return -1;
}

static int find_input_from_mbtn(int btn, int start) {
  int ta;
  for (ta=start; ta<InputCount; ta++) {
    if (InputList[ta].mousebtn == btn) {
      return ta;
    }
  }


  return -1;
}


static int is_input_valid(int ta) {
  int    input_valid;

  // Increment active timer
  InputList[ta].active_time ++;

  // Assume Input is valid

  input_valid=1;

  // Toggle Autofire settings

  if(InputList[ta].auto_rate){
    if(autofire_timer[InputList[ta].auto_rate] >= InputList[ta].auto_rate) {
      return 0; // no need to go further
    }
  }

  // Disable the following situations:
  // 1) Impossible joystick inputs (joystick can be up or down, but not both)
  // 2) Coin inputs must last approx 250ms (prevent taito coin error)

  /* I am not sure stick_logic is very usefull, but if Antiriad bothered to
   * write this, then it probably means that some games insist of having this
   * logic (and anyway it's long to write, but fast to execute !) */
  switch(InputList[ta].default_key){
  case KB_DEF_P1_UP:
    stick_logic[0] |= 0x01;
    if((stick_logic[0]&0x02)) input_valid=0;
    break;
  case KB_DEF_P1_DOWN:
    stick_logic[0] |= 0x02;
    if((stick_logic[0]&0x01)) input_valid=0;
    break;
  case KB_DEF_P1_LEFT:
    stick_logic[0] |= 0x04;
    if((stick_logic[0]&0x08)) input_valid=0;
    break;
  case KB_DEF_P1_RIGHT:
    stick_logic[0] |= 0x08;
    if((stick_logic[0]&0x04)) input_valid=0;
    break;
  case KB_DEF_P2_UP:
    stick_logic[1] |= 0x01;
    if((stick_logic[1]&0x02)) input_valid=0;
    break;
  case KB_DEF_P2_DOWN:
    stick_logic[1] |= 0x02;
    if((stick_logic[1]&0x01)) input_valid=0;
    break;
  case KB_DEF_P2_LEFT:
    stick_logic[1] |= 0x04;
    if((stick_logic[1]&0x08)) input_valid=0;
    break;
  case KB_DEF_P2_RIGHT:
    stick_logic[1] |= 0x08;
    if((stick_logic[1]&0x04)) input_valid=0;
    break;
  case KB_DEF_P3_UP:
    stick_logic[2] |= 0x01;
    if((stick_logic[2]&0x02)) input_valid=0;
    break;
  case KB_DEF_P3_DOWN:
    stick_logic[2] |= 0x02;
    if((stick_logic[2]&0x01)) input_valid=0;
    break;
  case KB_DEF_P3_LEFT:
    stick_logic[2] |= 0x04;
    if((stick_logic[2]&0x08)) input_valid=0;
    break;
  case KB_DEF_P3_RIGHT:
    stick_logic[2] |= 0x08;
    if((stick_logic[2]&0x04)) input_valid=0;
    break;
  case KB_DEF_P4_UP:
    stick_logic[3] |= 0x01;
    if((stick_logic[3]&0x02)) input_valid=0;
    break;
  case KB_DEF_P4_DOWN:
    stick_logic[3] |= 0x02;
    if((stick_logic[3]&0x01)) input_valid=0;
    break;
  case KB_DEF_P4_LEFT:
    stick_logic[3] |= 0x04;
    if((stick_logic[3]&0x08)) input_valid=0;
    break;
  case KB_DEF_P4_RIGHT:
    stick_logic[3] |= 0x08;
    if((stick_logic[3]&0x04)) input_valid=0;
    break;
  case KB_DEF_COIN1:
  case KB_DEF_COIN2:
  case KB_DEF_COIN3:
  case KB_DEF_COIN4:
    if(InputList[ta].active_time > pulse_time) input_valid=0;
    break;
  default:
    break;
  }
  return input_valid;
}

static void key_up(int ta) {
  switch(InputList[ta].default_key){
  case KB_DEF_P1_UP:
    stick_logic[0] &= ~0x01;
    break;
  case KB_DEF_P1_DOWN:
    stick_logic[0] &= ~0x02;
    break;
  case KB_DEF_P1_LEFT:
    stick_logic[0] &= ~0x04;
    break;
  case KB_DEF_P1_RIGHT:
    stick_logic[0] &= ~0x08;
    break;
  case KB_DEF_P2_UP:
    stick_logic[1] &= ~0x01;
    break;
  case KB_DEF_P2_DOWN:
    stick_logic[1] &= ~0x02;
    break;
  case KB_DEF_P2_LEFT:
    stick_logic[1] &= ~0x04;
    break;
  case KB_DEF_P2_RIGHT:
    stick_logic[1] &= ~0x08;
    break;
  case KB_DEF_P3_UP:
    stick_logic[2] &= ~0x01;
    break;
  case KB_DEF_P3_DOWN:
    stick_logic[2] &= ~0x02;
    break;
  case KB_DEF_P3_LEFT:
    stick_logic[2] &= ~0x04;
    break;
  case KB_DEF_P3_RIGHT:
    stick_logic[2] &= ~0x08;
    break;
  case KB_DEF_P4_UP:
    stick_logic[3] &= ~0x01;
    break;
  case KB_DEF_P4_DOWN:
    stick_logic[3] &= ~0x02;
    break;
  case KB_DEF_P4_LEFT:
    stick_logic[3] &= ~0x04;
    break;
  case KB_DEF_P4_RIGHT:
    stick_logic[3] &= ~0x08;
    break;
  }
}

// update_inputs():
// Goes through the input list setting/clearing the mapped RAM[] bits

static void remove_valid_input(int ta) {
  // remove an input from the list of valid input when the control is released
  int n;
  for (n=0; n<nb_valid_inputs; n++) {
    if (valid_inputs[n] == ta) {
      if (n < nb_valid_inputs-1)
	memmove(&valid_inputs[n],&valid_inputs[n+1],(nb_valid_inputs-1-n)*sizeof(int));
      nb_valid_inputs--;
      break;
    }
  }
  InputList[ta].active_time = 0;
}

static void add_joy_event(int event) {
  int ta = -1;
  /* We allow that 1 key is mapped to more than 1 control (loop)
   * it's because there are so many controls that you can very easily
   * map a key which is already used by another unused input and so
   * it's better to trigger them together in this case */
  do {
    ta = find_input_from_joy(event,ta+1);
    if (ta >= 0) {
      autofire_timer[InputList[ta].auto_rate] = 0;
      int input_valid = is_input_valid(ta);
      update_input_buffer(ta,input_valid);
      if (input_valid) {
	if (nb_valid_inputs == MAX_VALID_INPUTS) {
	  fprintf(stderr,"too many valid inputs\n");
	  exit(1);
	}
	valid_inputs[nb_valid_inputs++] = ta;
      }
    }
  } while (ta >= 0);
}

static void remove_joy_event(int event) {
  int ta = -1;
  if (event == def_input_emu[6].joycode) {
    // special case for the turbo key this one is a toggle
    def_input_emu[6].proc();
    return;
  }
  do {
    ta = find_input_from_joy(event,ta+1);
    if (ta >= 0) {
      key_up(ta);
      update_input_buffer(ta,0);
      remove_valid_input(ta);
    }
  } while (ta >= 0);
}

static void check_emu_joy_event(int jevent) {
  DEF_INPUT_EMU *emu = &def_input_emu[0];
  int nb = raine_get_emu_nb_ctrl();
  while (nb--) {
    if (emu->joycode == jevent) {
      emu->proc();
      break;
    }
    emu++;
  }
}

int get_axis_from_hat(int which, int hat) {
  return SDL_JoystickNumAxes(joy[which]) + (hat)*2;
}

static int check_layer_key(int input)
{
  int ta;
  int ret = 0;

  for(ta=0; ta<layer_info_count; ta++){

    if( layer_info_list[ta].keycode == input ){

      layer_info_list[ta].flip = 1;
      layer_info_list[ta].enabled ^= 1;
      print_ingame(60, "%s: %01d", layer_info_list[ta].name, layer_info_list[ta].enabled);
      ret = 1;
    }
  }
  return ret;
}

static int check_emu_inputs(DEF_INPUT_EMU *emu_input, int nb, int input, int modifier) {
  while (nb--) {
    if ((emu_input->scancode & 0xffff) == input) {
      int kmod = emu_input->scancode >> 16;
      if ((modifier == 0 && kmod == 0) ||
	  (modifier && (kmod & modifier) == (modifier & (KMOD_CTRL|KMOD_ALT|KMOD_SHIFT)))) {
	emu_input->proc();
	return 1;
      }
    }
    emu_input++;
  }
  return 0;
}

static void handle_event(SDL_Event *event) {
  int input,which,axis,value,modifier,jevent, hat, ta;
  DEF_INPUT_EMU *emu_input;
  int input_valid,nb,changed,base_axis;

  switch (event->type) {
    case SDL_KEYDOWN:
      input = event->key.keysym.sym; // | ((event->key.keysym.mod & 0x4fc0)<<16);
      if (!input) { // special encoding for scancodes (unknown keys)
	input = event->key.keysym.scancode | 0x200;
      }
      key[input] = 1;
      ta = -1;
      /* We allow that 1 key is mapped to more than 1 control (loop)
       * it's because there are so many controls that you can very easily
       * map a key which is already used by another unused input and so
       * it's better to trigger them together in this case */
      do {
	ta = find_input_from_keysym(input,ta+1);
	if (ta >= 0) {
	  autofire_timer[InputList[ta].auto_rate] = 0;
	  input_valid = is_input_valid(ta);
	  if (input_valid)
	    update_input_buffer(ta,input_valid);
	  valid_inputs[nb_valid_inputs++] = ta;
	}
      } while (ta >= 0);

      // Now check the gui inputs, the logic is slightly different since
      // we check for the keysym + modifiers here

      modifier = (event->key.keysym.mod & 0x4fc3);
      emu_input = &def_input_emu[0];
      nb = raine_get_emu_nb_ctrl();
      int handled=0,paused = raine_cfg.req_pause_game;
      if (!check_emu_inputs(emu_input,nb,input,modifier) && driver_nb_emu_inputs)
	handled = check_emu_inputs(driver_emu_list,driver_nb_emu_inputs,input,modifier);
      if (!handled)
	handled = check_layer_key(input);
      if (handled && paused && raine_cfg.req_pause_game) {
	current_game->video->draw_game(); // update game bitmap in pause
	EndDrawPaused();
	InitDrawPaused();
      }
      break;
    case SDL_KEYUP:
      input = event->key.keysym.sym; // | ((event->key.keysym.mod & 0x4fc0)<<16);
      if (!input) { // special encoding for scancodes (unknown keys)
	input = event->key.keysym.scancode | 0x200;
      }
      key[input] = 0;
      ta = -1;
      if (input == def_input_emu[6].scancode) {
	// special case for the turbo key this one is a toggle
	def_input_emu[6].proc();
	break;
      }

      do {
	ta = find_input_from_keysym(input,ta+1);
	if (ta >= 0) {
	  key_up(ta);
	  update_input_buffer(ta,0);
	  remove_valid_input(ta);

	  /* Now a particular case :
	   * sometimes when pressing rapidly 2 keys, we receive the 2nd keydown
	   * message before the key up for the 1st key. It can be critical in some
	   * action games !
	   * So the workaround : once a key has gone up, check the other keys
	   * to see if another input wouldn't become valid now ! */
	  int x;

	  for(x=0;x<InputCount;x++){
	    if((key[InputList[x].Key])){
	      int found_valid = 0;
	      int n;
	      for (n=0; n<nb_valid_inputs; n++) {
		if (valid_inputs[n] == x) {
		  found_valid = 1;
		  break;
		}
	      }
	      if (!found_valid) {
		input_valid = is_input_valid(x);
		if (input_valid) {
		  // We found a key down which now trigers a valid input !
		  update_input_buffer(x,input_valid);
		  valid_inputs[nb_valid_inputs++] = x;
		}
	      }
	    }
	  } // for
	} // if ta >= 0
      } while (ta >= 0);
      // printf("key up %d stick logic %d\n",ta,stick_logic[0]);

      break;
    case SDL_MOUSEMOTION:
      mickey_x = event->motion.xrel;
      mickey_y = event->motion.yrel;
      mouse_x += mickey_x;
      if (mouse_x > max_x) mouse_x = max_x;
      else if (mouse_x < min_x) mouse_x = min_x;
      mouse_y += mickey_y;
      if (mouse_y > max_y) mouse_y = max_y;
      else if (mouse_y < min_y) mouse_y = min_y;
      break;
    case SDL_MOUSEBUTTONDOWN:
      ta = find_input_from_mbtn(event->button.button,0);
      if (ta >= 0) {
	autofire_timer[InputList[ta].auto_rate] = 0;
	input_valid = is_input_valid(ta);
	update_input_buffer(ta,input_valid);
	if (input_valid)
	  valid_inputs[nb_valid_inputs++] = ta;
      }
      break;
    case SDL_MOUSEBUTTONUP:
      ta = find_input_from_mbtn(event->button.button,0);
      if (ta >= 0) {
	update_input_buffer(ta,0);
	remove_valid_input(ta);
      }
      break;
    case SDL_VIDEORESIZE:
      display_cfg.screen_x = event->resize.w;
      display_cfg.screen_y = event->resize.h;
      resize();
      SetupScreenBitmap();
      init_video_core();
      reset_ingame_timer();

      break;
    case SDL_VIDEOEXPOSE:
      display_bezel();
      break;
    case SDL_JOYHATMOTION:
      /* Hats are a windows speciality. In linux, they are seen as 2 separate
       * axis. Since axis simplify the way to handle them, I don't really see
       * any good reasons to handle hats separately, but it's windows, and
       * they love to make things complicated... and this code is not trivial
       * because a hat sends only the position it is in (8 possible positions
       * + center), so to emulate a real joystick we have to emulate more
       * than 1 event for 1 position change of the hat */
      which = event->jhat.which+1;
      hat = event->jhat.hat+1;
      value = event->jhat.value;
      changed = jstate[which].hat[hat] & (~value);
      // Emulate the hat like a virtual stick
      base_axis = get_axis_from_hat(which-1,hat-1);
      // changed contains the bitmask of the axis which need to be centered !
      event->jaxis.which = which-1;
      event->type = SDL_JOYAXISMOTION;

      event->jaxis.value = 0;
      if (changed & (SDL_HAT_LEFT | SDL_HAT_RIGHT)) {
	event->jaxis.axis = base_axis;
	handle_event(event);
      }
      if (changed & (SDL_HAT_UP | SDL_HAT_DOWN)) {
	event->jaxis.axis = base_axis+1;
	handle_event(event);
      }

      // Now send the events corresponding to the current position
      if (value & SDL_HAT_LEFT) {
	event->jaxis.axis = base_axis + 0;
	event->jaxis.value = -32700;
	handle_event(event);
      }
      if (value & SDL_HAT_RIGHT) {
	event->jaxis.axis = base_axis + 0;
	event->jaxis.value = 32700;
	handle_event(event);
      }
      if (value & SDL_HAT_UP) {
	event->jaxis.axis = base_axis + 1;
	event->jaxis.value = -32700;
	handle_event(event);
      }
      if (value & SDL_HAT_DOWN) {
	event->jaxis.axis = base_axis + 1;
	event->jaxis.value = 32000;
	handle_event(event);
      }
      jstate[which].hat[hat] = event->jhat.value;
      break;
    case SDL_JOYAXISMOTION:
      which = event->jaxis.which+1;
      axis = event->jaxis.axis;
      value = event->jaxis.value;
      if (which >= MAX_JOY || axis >= MAX_AXIS) {
	return;
      } else if (which == analog_num+1 && axis/2 == analog_stick) {
	// Normalized position for analog input...
	if (axis == analog_stick*2) {
	  if (value < 0) {
	    if (value < analog_minx) {
	      analog_minx = value;
	      analog_normx = -16384;
	    } else
	      analog_normx = value*-16384/analog_minx;
	  } else if (value > 0) {
	    if (value > analog_maxx) {
	      analog_maxx = value;
	      analog_normx = 16384;
	    } else
	      analog_normx = value*16384/analog_maxx;
	  }
	} else { // vertical axis
	  if (value < 0) {
	    if (value < analog_miny) {
	      analog_miny = value;
	      analog_normy = -16384;
	    } else
	      analog_normy = value*-16384/analog_miny;
	  } else if (value > 0) {
	    if (value > analog_maxy) {
	      analog_maxy = value;
	      analog_normy = 16384;
	    } else
	      analog_normy = value*16384/analog_maxy;
	  }
	}
      }
      jevent = 0;
      if (value < -16000 && jstate[which].pos_axis[axis] > -1) {
	if (jstate[which].pos_axis[axis] == 1) {
	  /* Special case for joysticks : sometimes they move so fast from one
	   * side to the other that we get nothing for the central position.
	   * In this case raine rejects the opposite control so we'd better
	   * avoid this here... */
	  remove_joy_event(JOY(which,AXIS_RIGHT(axis),0,0));
	}
	jstate[which].pos_axis[axis] = -1;
	add_joy_event((jevent = JOY(which,AXIS_LEFT(axis),0,0)));
      } else if (value > 16000 && jstate[which].pos_axis[axis] < 1) {
	if (jstate[which].pos_axis[axis] == -1)
	  remove_joy_event(JOY(which,AXIS_LEFT(axis),0,0));
	jstate[which].pos_axis[axis] = 1;
	add_joy_event((jevent = JOY(which,AXIS_RIGHT(axis),0,0)));
      } else if ((jstate[which].pos_axis[axis] == -1 && value > -16000) ||
	  (jstate[which].pos_axis[axis] == 1 && value < 16000)) {
	/* With my choice to encode joystick events, there is no way to know
	 * if this is the end of a right or left movement. So I end both of
	 * them !
	 * It shouldn't matter all that much, this remains very fast */
	remove_joy_event(JOY(which, AXIS_LEFT(axis),0,0));
	remove_joy_event(JOY(which, AXIS_RIGHT(axis),0,0));
	jstate[which].pos_axis[axis] = 0;
      }
      if (jevent)
	check_emu_joy_event(jevent);
      break;
    case SDL_JOYBUTTONDOWN:
      add_joy_event((jevent = JOY(event->jbutton.which+1,0,event->jbutton.button+1,0)));
      check_emu_joy_event(jevent);
      break;
    case SDL_JOYBUTTONUP:
      remove_joy_event(JOY(event->jbutton.which+1,0,event->jbutton.button+1,0));
      break;
    case SDL_QUIT:
      exit(0);
    case SDL_ACTIVEEVENT:
      if (event->active.gain)
	      app_state |= event->active.state;
      else
	      app_state &= ~event->active.state;
      if (! raine_cfg.req_pause_game && !(app_state & SDL_APPINPUTFOCUS) &&
			  pause_on_focus)
		  // lost input -> go to pause
	      raine_cfg.req_pause_game = 1;
      break;
  }
}

void update_inputs(void)
{
  SDL_Event event;

  int    ta,n;
  int    input_valid;

  if (reading_demo) {
    write_demo_inputs();
    return;
  }

  // in case nothing moves, reset the mouse mickeys to 0 !
  mickey_x = mickey_y = 0;

  /* update valid inputs : this is not an option, it allows auto fire to work
   * and certain games insist on some inputs to be valid only if they last
   * only for a certain amount of time (coins in spf2t for example) */
  if (nb_valid_inputs) {
    // Autofire timer emulation

    for(ta=1;ta<6;ta++){
      autofire_timer[ta] ++;
      if(autofire_timer[ta] >= (ta<<1))
				autofire_timer[ta] = 0;
    }
    for (n=0; n<nb_valid_inputs; n++) {
      input_valid = is_input_valid(valid_inputs[n]);
      update_input_buffer(valid_inputs[n],input_valid);
      /* Notice : even if an input becomes invalid in this loop, it can be
       * removed from this list/array only by releasing the corresponding
       * input, otherwise the auto fire wouldn't work */
    }
  }

  while (SDL_PollEvent(&event)) {
    handle_event(&event);
  }
  if (recording_demo)
    save_demo_inputs();
}

/******************************************************************************/
/*                                                                            */
/*                              GLOBAL STRINGS                                */
/*                                                                            */
/******************************************************************************/

char MSG_COIN1[]        = "Coin A";
char MSG_COIN2[]        = "Coin B";
char MSG_COIN3[]        = "Coin C";
char MSG_COIN4[]        = "Coin D";

char MSG_TILT[]         = "Tilt";
char MSG_SERVICE[]      = "Service";
char MSG_TEST[]         = "Test";
char MSG_UNKNOWN[]      = "Unknown";
char MSG_YES[] = "Yes";
char MSG_NO[] = "No";
char MSG_FREE_PLAY[] = "Free Play";
char MSG_UNUSED[] = "Unused";
char MSG_COINAGE[] = "Coinage";

char MSG_P1_START[]     = "Player1 Start";

char MSG_P1_UP[]        = "Player1 Up";
char MSG_P1_DOWN[]      = "Player1 Down";
char MSG_P1_LEFT[]      = "Player1 Left";
char MSG_P1_RIGHT[]     = "Player1 Right";

char MSG_P1_B1[]        = "Player1 Button1";
char MSG_P1_B2[]        = "Player1 Button2";
char MSG_P1_B3[]        = "Player1 Button3";
char MSG_P1_B4[]        = "Player1 Button4";
char MSG_P1_B5[]        = "Player1 Button5";
char MSG_P1_B6[]        = "Player1 Button6";
char MSG_P1_B7[]        = "Player1 Button7";
char MSG_P1_B8[]        = "Player1 Button8";

char MSG_P2_START[]     = "Player2 Start";

char MSG_P2_UP[]        = "Player2 Up";
char MSG_P2_DOWN[]      = "Player2 Down";
char MSG_P2_LEFT[]      = "Player2 Left";
char MSG_P2_RIGHT[]     = "Player2 Right";

char MSG_P2_B1[]        = "Player2 Button1";
char MSG_P2_B2[]        = "Player2 Button2";
char MSG_P2_B3[]        = "Player2 Button3";
char MSG_P2_B4[]        = "Player2 Button4";
char MSG_P2_B5[]        = "Player2 Button5";
char MSG_P2_B6[]        = "Player2 Button6";
char MSG_P2_B7[]        = "Player2 Button7";
char MSG_P2_B8[]        = "Player2 Button8";

char MSG_P3_START[]     = "Player3 Start";

char MSG_P3_UP[]        = "Player3 Up";
char MSG_P3_DOWN[]      = "Player3 Down";
char MSG_P3_LEFT[]      = "Player3 Left";
char MSG_P3_RIGHT[]     = "Player3 Right";

char MSG_P3_B1[]        = "Player3 Button1";
char MSG_P3_B2[]        = "Player3 Button2";
char MSG_P3_B3[]        = "Player3 Button3";
char MSG_P3_B4[]        = "Player3 Button4";
char MSG_P3_B5[]        = "Player3 Button5";
char MSG_P3_B6[]        = "Player3 Button6";
char MSG_P3_B7[]        = "Player3 Button7";
char MSG_P3_B8[]        = "Player3 Button8";

char MSG_P4_START[]     = "Player4 Start";

char MSG_P4_UP[]        = "Player4 Up";
char MSG_P4_DOWN[]      = "Player4 Down";
char MSG_P4_LEFT[]      = "Player4 Left";
char MSG_P4_RIGHT[]     = "Player4 Right";

char MSG_P4_B1[]        = "Player4 Button1";
char MSG_P4_B2[]        = "Player4 Button2";
char MSG_P4_B3[]        = "Player4 Button3";
char MSG_P4_B4[]        = "Player4 Button4";
char MSG_P4_B5[]        = "Player4 Button5";
char MSG_P4_B6[]        = "Player4 Button6";
char MSG_P4_B7[]        = "Player4 Button7";
char MSG_P4_B8[]        = "Player4 Button8";

char MSG_FLIPPER_1_L[]  = "Flipper 1 Left";
char MSG_FLIPPER_1_R[]  = "Flipper 1 Right";
char MSG_FLIPPER_2_L[]  = "Flipper 2 Left";
char MSG_FLIPPER_2_R[]  = "Flipper 2 Right";
char MSG_TILT_L[]       = "Tilt Left";
char MSG_TILT_R[]       = "Tilt Right";
char MSG_B1_L[]         = "Button 1 Left";
char MSG_B1_R[]         = "Button 1 Right";
char MSG_B2_L[]         = "Button 2 Left";
char MSG_B2_R[]         = "Button 2 Right";

char MSG_P1_A[]         = "P1 A";    // Mahjong controls[]; at least in mahjong quest...
char MSG_P1_E[]         = "P1 E";
char MSG_P1_I[]         = "P1 I";
char MSG_P1_M[]         = "P1 M";
char MSG_P1_KAN[]       = "P1 Kan";

char MSG_P1_B[]         = "P1 B";
char MSG_P1_F[]         = "P1 F";
char MSG_P1_J[]         = "P1 J";
char MSG_P1_N[]         = "P1 N";
char MSG_P1_REACH[]     = "P1 Reach";

char MSG_P1_C[]         = "P1 C";
char MSG_P1_G[]         = "P1 G";
char MSG_P1_K[]         = "P1 K";
char MSG_P1_CHI[]       = "P1 Chi";
char MSG_P1_RON[]       = "P1 Ron";

char MSG_P1_D[]         = "P1 D";
char MSG_P1_H[]         = "P1 H";
char MSG_P1_L[]         = "P1 L";
char MSG_P1_PON[]       = "P1 Pon";

char *MSG_P1_SERVICE_A  = "Service A";
char *MSG_P1_SERVICE_B  = "Service B";
char *MSG_P1_SERVICE_C  = "Service C";

/* DSW SECTION */

char MSG_DSWA_BIT1[]    = "DSW A Bit 1";        // Since most dsw info sheets
char MSG_DSWA_BIT2[]    = "DSW A Bit 2";        // number the bits 1-8, we will
char MSG_DSWA_BIT3[]    = "DSW A Bit 3";        // too, although 0-7 is a more
char MSG_DSWA_BIT4[]    = "DSW A Bit 4";        // correct syntax for progammers.
char MSG_DSWA_BIT5[]    = "DSW A Bit 5";
char MSG_DSWA_BIT6[]    = "DSW A Bit 6";
char MSG_DSWA_BIT7[]    = "DSW A Bit 7";
char MSG_DSWA_BIT8[]    = "DSW A Bit 8";

char MSG_DSWB_BIT1[]    = "DSW B Bit 1";
char MSG_DSWB_BIT2[]    = "DSW B Bit 2";
char MSG_DSWB_BIT3[]    = "DSW B Bit 3";
char MSG_DSWB_BIT4[]    = "DSW B Bit 4";
char MSG_DSWB_BIT5[]    = "DSW B Bit 5";
char MSG_DSWB_BIT6[]    = "DSW B Bit 6";
char MSG_DSWB_BIT7[]    = "DSW B Bit 7";
char MSG_DSWB_BIT8[]    = "DSW B Bit 8";

char MSG_DSWC_BIT1[]    = "DSW C Bit 1";
char MSG_DSWC_BIT2[]    = "DSW C Bit 2";
char MSG_DSWC_BIT3[]    = "DSW C Bit 3";
char MSG_DSWC_BIT4[]    = "DSW C Bit 4";
char MSG_DSWC_BIT5[]    = "DSW C Bit 5";
char MSG_DSWC_BIT6[]    = "DSW C Bit 6";
char MSG_DSWC_BIT7[]    = "DSW C Bit 7";
char MSG_DSWC_BIT8[]    = "DSW C Bit 8";

char MSG_COIN_SLOTS[]   = "Coin Slots";

char MSG_1COIN_1PLAY[]  = "1 Coin/1 Credit";
char MSG_1COIN_2PLAY[]  = "1 Coin/2 Credits";
char MSG_1COIN_3PLAY[]  = "1 Coin/3 Credits";
char MSG_1COIN_4PLAY[]  = "1 Coin/4 Credits";
char MSG_1COIN_5PLAY[]  = "1 Coin/5 Credits";
char MSG_1COIN_6PLAY[]  = "1 Coin/6 Credits";
char MSG_1COIN_7PLAY[]  = "1 Coin/7 Credits";
char MSG_1COIN_8PLAY[]  = "1 Coin/8 Credits";
char MSG_1COIN_9PLAY[]  = "1 Coin/9 Credits";

char MSG_2COIN_1PLAY[]  = "2 Coins/1 Credit";
char MSG_2COIN_2PLAY[]  = "2 Coins/2 Credits";
char MSG_2COIN_3PLAY[]  = "2 Coins/3 Credits";
char MSG_2COIN_4PLAY[]  = "2 Coins/4 Credits";
char MSG_2COIN_5PLAY[]  = "2 Coins/5 Credits";
char MSG_2COIN_6PLAY[]  = "2 Coins/6 Credits";
char MSG_2COIN_7PLAY[]  = "2 Coins/7 Credits";
char MSG_2COIN_8PLAY[]  = "2 Coins/8 Credits";

char MSG_3COIN_1PLAY[]  = "3 Coins/1 Credit";
char MSG_3COIN_2PLAY[]  = "3 Coins/2 Credits";
char MSG_3COIN_3PLAY[]  = "3 Coins/3 Credits";
char MSG_3COIN_4PLAY[]  = "3 Coins/4 Credits";
char MSG_3COIN_5PLAY[]  = "3 Coins/5 Credits";
char MSG_3COIN_6PLAY[]  = "3 Coins/6 Credits";
char MSG_3COIN_7PLAY[]  = "3 Coins/7 Credits";
char MSG_3COIN_8PLAY[]  = "3 Coins/8 Credits";

char MSG_4COIN_1PLAY[]  = "4 Coins/1 Credit";
char MSG_4COIN_2PLAY[]  = "4 Coins/2 Credits";
char MSG_4COIN_3PLAY[]  = "4 Coins/3 Credits";
char MSG_4COIN_4PLAY[]  = "4 Coins/4 Credits";
char MSG_4COIN_5PLAY[]  = "4 Coins/5 Credits";
char MSG_4COIN_6PLAY[]  = "4 Coins/6 Credits";
char MSG_4COIN_7PLAY[]  = "4 Coins/7 Credits";
char MSG_4COIN_8PLAY[]  = "4 Coins/8 Credits";

char MSG_5COIN_1PLAY[]  = "5 Coins/1 Credit";
char MSG_5COIN_2PLAY[]  = "5 Coins/2 Credits";
char MSG_5COIN_3PLAY[]  = "5 Coins/3 Credits";
char MSG_5COIN_4PLAY[]  = "5 Coins/4 Credits";

char MSG_6COIN_1PLAY[]  = "6 Coins/1 Credit";
char MSG_6COIN_2PLAY[]  = "6 Coins/2 Credits";
char MSG_6COIN_3PLAY[]  = "6 Coins/3 Credits";
char MSG_6COIN_4PLAY[]  = "6 Coins/4 Credits";

char MSG_7COIN_1PLAY[]  = "7 Coins/1 Credit";
char MSG_7COIN_2PLAY[]  = "7 Coins/2 Credits";
char MSG_7COIN_3PLAY[]  = "7 Coins/3 Credits";
char MSG_7COIN_4PLAY[]  = "7 Coins/4 Credits";

char MSG_8COIN_1PLAY[]  = "8 Coins/1 Credit";
char MSG_8COIN_2PLAY[]  = "8 Coins/2 Credits";
char MSG_8COIN_3PLAY[]  = "8 Coins/3 Credits";
char MSG_8COIN_4PLAY[]  = "8 Coins/4 Credits";

char MSG_9COIN_1PLAY[] = "9 Coins/1 Credit";

char MSG_OFF[]          = "Off";
char MSG_ON[]           = "On";

char MSG_SCREEN[]       = "Flip Screen";
char MSG_NORMAL[]       = "Normal";
char MSG_INVERT[]       = "Invert";

char MSG_TEST_MODE[]    = "Test Mode";

char MSG_DEMO_SOUND[]   = "Demo Sound";

char MSG_CONTINUE_PLAY[]= "Continue Play";
char MSG_EXTRA_LIFE[]   = "Extra Life";
char MSG_LIVES[]        = "Lives";

char MSG_CHEAT[]        = "Cheat";

char MSG_DIFFICULTY[]   = "Difficulty";
char MSG_EASY[]         = "Easy";
char MSG_HARD[]         = "Hard";
char MSG_VERY_HARD[]         = "Very Hard";
char MSG_HARDEST[]      = "Hardest";
char MSG_MEDIUM[]       = "Medium";

char MSG_CABINET[]      = "Cabinet";
char MSG_UPRIGHT[]      = "Upright";
char MSG_TABLE[]        = "Table";
char MSG_ALT[]        = "Alternate";

/******************************************************************************/

#ifndef SDL
static void key_pause_scroll_up(void)
{
   raine_cfg.req_pause_scroll |= 1;
}

static void key_pause_scroll_down(void)
{
   raine_cfg.req_pause_scroll |= 2;
}

static void key_pause_scroll_left(void)
{
   raine_cfg.req_pause_scroll |= 4;
}

static void key_pause_scroll_right(void)
{
   raine_cfg.req_pause_scroll |= 8;
}
#endif
void inputs_preinit() {
  int n;
  SDL_Event event;
  int handled;
  memset(bad_axes,0,sizeof(bad_axes));
  for (n=0; n<SDL_NumJoysticks(); n++) {
    joy[n] = SDL_JoystickOpen(n);
    // Memorize the joystick name because calls to this function are very
    // slow for some reason... !
    joy_name[n] = strdup(SDL_JoystickName(n));
    printf("joy %d opened (%s), numaxes %d\n",n,joy_name[n],SDL_JoystickNumAxes(joy[n]));
  }

  // Some peripherals like a certain microsoft keyboard is recognized as a
  // joystick when pluged in usb, and they send a few faulty events at start
  // this loop should get rid of them...
  int ticks = SDL_GetTicks();
  event.type = 0;
  while (!SDL_PollEvent(&event) && SDL_GetTicks()-ticks < 100);
  if (event.type) SDL_PushEvent(&event);
  do {
      handled = 0;
      if (SDL_PollEvent(&event)) {
	  switch(event.type) {
	  case SDL_JOYAXISMOTION:
	  case SDL_JOYBALLMOTION:
	  case SDL_JOYHATMOTION:
	  case SDL_JOYBUTTONDOWN:
	  case SDL_JOYBUTTONUP:
	      handled = 1;
	      break;
	  }
      }
  } while (handled);
}

void inputs_done() {
  int n;
  for (n=0; n<SDL_NumJoysticks(); n++)
    if (joy[n]) {
	SDL_JoystickClose(joy[n]);
	free(joy_name[n]);
    }
}

