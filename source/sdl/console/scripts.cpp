#include "raine.h"
#include "games.h"
#include "files.h"
#include "scripts.h"
#include "console.h"

#define MAX_PARAM 10

typedef struct {
  char *title;
  // status : 0 = off, 1 = on (in dialog)
  int status, hidden;
  char **on, **off, **run, **changing;
  int nb_param;
  int value_list[MAX_PARAM];
  char *value_list_label[MAX_PARAM];
} tscript;

tscript *script;
int nb_scripts;

char *get_script_name(int writeable) {
  char base[10];
   if (is_neocd())
       strcpy(base,"neocd");
   else
       strcpy(base,"raine");
  char buf[FILENAME_MAX];
  snprintf(buf,FILENAME_MAX,"scripts%s%s%s%s.txt",SLASH,base,SLASH,current_game->main_name);

  char *s = get_shared(buf);
  if (!exists(s)) {
      snprintf(buf,FILENAME_MAX,"scripts%s%s%s%s.txt",SLASH,base,SLASH,parent_name());
      s = get_shared(buf);
  }
  if (!writeable)
    return s;
  // if it must be writable, force the use of the personnal folder, and
  // create the dirs by the way
  static char shared[1024];
  snprintf(shared,1024, "%sscripts", dir_cfg.exe_path);
  mkdir_rwx(shared);
  snprintf(shared+strlen(shared),1024-strlen(shared),"%s%s",SLASH,base);
  mkdir_rwx(shared);
  snprintf(shared+strlen(shared),1024-strlen(shared),"%s%s.txt",SLASH,current_game->main_name);
  return shared;
}

int atoh(char *s) {
    int val = 0;
    while (*s) {
	val *= 16;
	if (*s >= '0' && *s <= '9')
	    val += (*s-'0');
	else if (*s >= 'A' && *s <= 'F')
	    val += (*s-'A'+10);
	else if (*s >= 'a' && *s <= 'f')
	    val += (*s-'a'+10);
	s++;
    }
    return val;
}

void init_scripts() {
  if (nb_scripts) {
    for (int n=0; n<nb_scripts; n++) {
      free(script[n].title);
      if (script[n].run) {
	  for (char **l = script[n].run; l && *l; l++)
	      free(*l);
	  free(script[n].run);
      }
      if (script[n].on) {
	  for (char **l = script[n].on; l && *l; l++)
	      free(*l);
	  free(script[n].on);
      }
      if (script[n].off) {
	  for (char **l = script[n].off; l && *l; l++)
	      free(*l);
	  free(script[n].off);
      }
      if (script[n].changing) {
	  for (char **l = script[n].changing; l && *l; l++)
	      free(*l);
	  free(script[n].changing);
      }
      if (script[n].nb_param && script[n].value_list_label[0]) {
	  for (int x=0; x<script[n].nb_param; x++)
	      free(script[n].value_list_label[x]);
      }
    }
    free(script);
    script = NULL;
    nb_scripts = 0;
  }
  // rb should allow fseek in windoze...
  FILE *f = fopen(get_script_name(0),"rb");
  int nb_alloc = 0;
  if (f) {
      char buff[1024];
      *buff = 0;
    while (!feof(f)) {
	if (strncmp(buff,"script",6))  // if containing a script line then it's just a loop, don't erase it !
	    myfgets(buff,1024,f);
	if (!strncmp(buff,"script \"",8) ||
		!strncmp(buff,"hidden \"",8)) {
	    if (nb_scripts == nb_alloc) {
		nb_alloc += 10;
		script = (tscript *)realloc(script,sizeof(tscript)*nb_alloc);
	    }
	    script[nb_scripts].hidden = !strncmp(buff,"hidden",6);
	    script[nb_scripts].on =
		script[nb_scripts].off =
		script[nb_scripts].run =
		script[nb_scripts].changing = NULL;
	    int argc;
	    char *argv[10];
	    split_command(buff,argv,&argc,10);
	    if (argc < 2) {
		printf("init_scripts: script without title: %s\n",buff);
		fclose(f);
		return;
	    }
	    script[nb_scripts].title = strdup(argv[1]);
	    script[nb_scripts].status = 0;
	    script[nb_scripts].nb_param = 0;
	    if (argc > 2) { // param needed for script
		if (!strncmp(argv[2],"inter=",6)) {
		    char *s = argv[2]+6;
		    for (int n=0; n<=1; n++) {
			char *e = strchr(s,',');
			if (e) {
			    *e = 0;
			    script[nb_scripts].value_list[n] = atoi(s);
			    *e = ','; s=e+1;
			} else {
			    printf("syntax error in inter=\n");
			    fclose(f);
			    return;
			}
		    }
		    script[nb_scripts].value_list[2] = atoi(s);
		    script[nb_scripts].nb_param = 3;
		    script[nb_scripts].value_list_label[0] = NULL;
		} else {
		    // in this case all the remaining arguments are in the format value/description
		    script[nb_scripts].nb_param = argc - 2;
		    for (int n=0; n<argc-2; n++) {
			char *s = strchr(argv[n+2],'/');
			if (!s) {
			    printf("script: argument wrong format, expected value/description\n");
			    fclose(f);
			    return;
			}
			*s = 0;
			int val;
			printf("val %s ",argv[n+2]);
			if (!strncasecmp(argv[n+2],"0x",2)) {
			    val = atoh(argv[n+2]+2);
			} else {
			    val = atoi(argv[n+2]);
			}
			printf("%d\n",val);
			script[nb_scripts].value_list[n] = val;
			script[nb_scripts].value_list_label[n] = strdup(s+1);
		    }
		}
	    }

	    int pos = ftell(f);
	    int nb_on = 0, nb_off = 0, nb_run = 0, nb_changing = 0;
	    bool on = false, off = false, changing = false, run = false;
	    while (!feof(f)) {
		*buff = 0;
		myfgets(buff,1024,f);
		int n=0;
		// skip spaces, tabs, and comments
		while (buff[n] == ' ' || buff[n] == 9)
		    n++;
		if (buff[n] == 0 || !strncmp(&buff[n],"script",6)) // optional empty line to end the script
		    break;
		if (buff[n] == '#')
		    continue; // don't keep comments !
		if (!strcmp(&buff[n],"on:")) {
		    on = true;
		    off = run = changing = false;
		    continue;
		} else if (!strcmp(&buff[n],"off:")) {
		    off = true;
		    on = run = changing = false;
		    continue;
		} else if (!strcmp(&buff[n],"changing:")) {
		    changing = true;
		    on = off = run = false;
		    continue;
		} else if (!strcmp(&buff[n],"run:")) {
		    run = true;
		    on = off = changing = false;
		    continue;
		}

		if (on) nb_on++;
		else if (off) nb_off++;
		else if (run) nb_run++;
		else if (changing) nb_changing++;
	    }
	    fseek(f,pos,SEEK_SET);
	    int l = 0;
	    if (nb_on) {
		script[nb_scripts].on = (char**)malloc(sizeof(char*)*(nb_on+1));
		script[nb_scripts].on[nb_on] = NULL;
	    }
	    if (nb_off) {
		script[nb_scripts].off = (char**)malloc(sizeof(char*)*(nb_off+1));
		script[nb_scripts].off[nb_off] = NULL;
	    }
	    if (nb_run) {
		script[nb_scripts].run = (char**)malloc(sizeof(char*)*(nb_run+1));
		script[nb_scripts].run[nb_run] = NULL;
	    }
	    if (nb_changing) {
		script[nb_scripts].changing = (char**)malloc(sizeof(char*)*(nb_changing+1));
		script[nb_scripts].changing[nb_changing] = NULL;
	    }

	    char **lines;
	    while (!feof(f)) {
		myfgets(buff,1024,f);
		int n=0;
		// skip spaces, tabs, and comments
		while (buff[n] == ' ' || buff[n] == 9)
		    n++;
		if (buff[n] == 0 || !strncmp(&buff[n],"script",6)) // optional empty line to end the script
		    break;
		if (buff[n] == '#')
		    continue; // don't keep comments !
		if (!strcmp(&buff[n],"on:")) {
		    lines = script[nb_scripts].on;
		    l = 0;
		    continue;
		} else if (!strcmp(&buff[n],"off:")) {
		    lines = script[nb_scripts].off;
		    l = 0;
		    continue;
		} else if (!strcmp(&buff[n],"changing:")) {
		    lines = script[nb_scripts].changing;
		    l = 0;
		    continue;
		} else if (!strcmp(&buff[n],"run:")) {
		    lines = script[nb_scripts].run;
		    l = 0;
		    continue;
		}
		lines[l++] = strdup(&buff[n]);
	    }
	    nb_scripts++;
	} else { // script line
	    printf("problem %s\n",buff);
	    exit(0);
	    run_console_command(buff); // pre-init, usually for variables
	}
    } // feof
    fclose(f);
  } // if (f)
}

static int activate_cheat(int n) {
    if (script[n].nb_param) {
	printf("j'ai le status %d\n",script[n].status);
	set_script_param(script[n].status);
    }
    if (!script[n].status) {
	char **l = script[n].off;
	for (; l && *l; l++)
	    run_console_command(*l);
	return 0;
    }
    char **l = script[n].on;
    for (; l && *l; l++)
	run_console_command(*l);
    return 0;
}

void add_scripts(menu_item_t *menu) {
  for (int n=0; n<nb_scripts; n++) {
      if (script[n].hidden) continue;
      menu->label = script[n].title;
      menu->value_int = &script[n].status;
      menu->menu_func = &activate_cheat;
      if (script[n].nb_param == 3 && !script[n].value_list_label[0]) { // interval
	  menu->values_list_size = 3;
	  menu->values_list[0] = script[n].value_list[0];
	  menu->values_list[1] = script[n].value_list[1];
	  menu->values_list[2] = script[n].value_list[2];
	  menu->values_list_label[0] = NULL;
      } else if (script[n].nb_param) {
	  menu->values_list_size = script[n].nb_param;
	  for (int x=0; x<menu->values_list_size; x++) {
	      printf("add_scripts %d:%s:%d\n",x,script[n].value_list_label[x],script[n].value_list[x]);
	      menu->values_list[x] = script[n].value_list[x];
	      menu->values_list_label[x] = script[n].value_list_label[x];
	  }
      } else {
	  menu->values_list_size = 2;
	  menu->values_list[0] = 0;
	  menu->values_list[1] = 1;
	  menu->values_list_label[0] = _("Off");
	  menu->values_list_label[1] = _("On");
      }
      menu++;
  }
}

void update_scripts() {
    if (!nb_scripts) return;
    int n;
    for (n=0; n<nb_scripts; n++) {
	if (script[n].status) {
	    printf("exec script %d\n",n);
	    for (char **l = script[n].run; l && *l; l++)
		run_console_command(*l);
	}
    }
}

static FILE *fscript;

static void get_script_commands(char *field) {
  if (strcmp(field,".")) {
    fprintf(fscript,"  %s\n",field);
    cons->print("  %s",field);
    *field = 0;
  } else {
    fprintf(fscript,"\n");
    fclose(fscript);
    cons->print("your script has been recorded");
    cons->end_interactive();
  }
}

static void get_script_mode(char *field) {
  if (!*field) {
    cons->print("executed only once");
  } else {
    cons->print("looping script");
    fprintf(fscript," always");
  }
  *field = 0;
  fprintf(fscript,"\n");
  cons->print("now type your commands, a . to finish...");
  cons->set_interactive(&get_script_commands);
}

static void get_script_name(char *field) {
  fprintf(fscript,"script \"%s\"",field);
  cons->print("  %s",field);
  *field = 0;
  cons->print("Type return for a script which is executed only once, or type any text + return for a script which loops until manually stopped");
  cons->set_interactive(&get_script_mode);
}

void do_start_script(int argc, char **argv) {
    if (argc != 2)
	throw "Syntax : start_script name\n";
    for (int n=0; n<nb_scripts; n++) {
	if (!strcmp(script[n].title,argv[1])) {
	    script[n].status = 1;
	    return;
	}
    }
    cons->print("Didn't find script %s",argv[1]);
}

void do_script(int argc, char **argv) {
  char *s = get_script_name(1);
  fscript = fopen(s,"a");
  if (!fscript) {
    cons->print("can't create file %s",s);
    return;
  }
  if (argc == 1) {
    cons->print("Please type the script description (spaces allowed)");
    cons->set_interactive(&get_script_name);
  } else {
    fprintf(fscript,"script \"%s\"",argv[1]);
    if (argc == 3) {
      get_script_mode(argv[2]);
    } else {
      get_script_mode("");
    }
  }
}

