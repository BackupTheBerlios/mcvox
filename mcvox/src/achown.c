/* Chown-advanced command -- for the Midnight Commander
   Copyright (C) 1994, 1995 Radek Doulik

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  
 */

#include <config.h>
/* Needed for the extern declarations of integer parameters */
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_UNISTD_H
#   include <unistd.h>
#endif
#include <string.h>
#include <stdio.h>
#include <errno.h>	/* For errno on SunOS systems	      */

#include "global.h"
#include "tty.h"
#include "win.h"
#include "color.h"
#include "dialog.h"
#include "widget.h"
#include "wtools.h"		/* For init_box_colors() */
#include "key.h"		/* XCTRL and ALT macros */

#include "dir.h"
#include "panel.h"		/* Needed for the externs */
#include "chmod.h"
#include "main.h"
#include "achown.h"

#define BX		5
#define BY		6

#define TX              50
#define TY              2

/* Max nb of buttons (because of a variable number of submit buttons) */
#define BUTTONS		9

/* buttons used as input fields */
#define NB_INPUT_BUTTON 5

#define LABELS		6

#define B_SETALL        B_USER
#define B_SKIP          (B_USER + 1)

#define B_OWN           (B_USER + 3)
#define B_GRP           (B_USER + 4)
#define B_OTH           (B_USER + 5)
#define B_OUSER         (B_USER + 6)
#define B_OGROUP        (B_USER + 7)

#define Y_POS 2
#define X_POS 0

static struct Dlg_head *ch_dlg;

static struct {
    int ret_cmd, flags, y, x;
    char *text;
} chown_advanced_but [BUTTONS] = {
    { B_CANCEL, NORMAL_BUTTON, 4, 53, N_("&Cancel") },
    { B_ENTER,  DEFPUSH_BUTTON,4, 40, N_("&Set") },
    { B_SKIP,   NORMAL_BUTTON, 4, 23, N_("S&kip") },
    { B_SETALL, NORMAL_BUTTON, 4, 0, N_("Set &all")},
    { B_ENTER,  INPUT_BUTTON, 0, 47, "               "},
    { B_ENTER,  INPUT_BUTTON, 0, 29, "               "},
    { B_ENTER,  INPUT_BUTTON, 0, 19, "   "},
    { B_ENTER,  INPUT_BUTTON, 0, 11, "   "},
    { B_ENTER,  INPUT_BUTTON, 0, 3, "   "},
};

static WButton *b_att[3];	/* permission */
static WButton *b_user, *b_group;	/* owner */
static WLabel *l_filename, *l_mode;	

static int files_on_begin;	/* Number of files at startup */
static int flag_pos;
static int x_toggle;
static char ch_flags[11];
static const char ch_perm[] = "rwx";
static umode_t ch_cmode;
static struct stat *sf_stat;
static int need_update;
static int end_chown;
static int current_file;
static int single_set=0;
static char *fname;
static int nb_submit_buttons;
#define NB_BUTTONS (NB_INPUT_BUTTON+nb_submit_buttons)
static callback_fn button_callback=NULL;

static void get_ownership (void)
{				/* set buttons  - ownership */
    char *name_t;

    name_t = name_trunc (get_owner (sf_stat->st_uid), 15);
    memset (b_user->text, ' ', 15);
    strncpy (b_user->text, name_t, strlen (name_t));
    name_t = name_trunc (get_group (sf_stat->st_gid), 15);
    memset (b_group->text, ' ', 15);
    strncpy (b_group->text, name_t, strlen (name_t));
}


static int inc_flag_pos (int f_pos)
{
    if (flag_pos == 10) {
	flag_pos = 0;
	return MSG_NOT_HANDLED;
    }
    flag_pos++;
    if (!(flag_pos % 3) || f_pos > 2)
	return MSG_NOT_HANDLED;
    return MSG_HANDLED;
}

static cb_ret_t dec_flag_pos (int f_pos)
{
    if (!flag_pos) {
	flag_pos = 10;
	return MSG_NOT_HANDLED;
    }
    flag_pos--;
    if (!((flag_pos + 1) % 3) || f_pos > 2)
	return MSG_NOT_HANDLED;
    return MSG_HANDLED;
}

static void set_perm_by_flags (char *s, int f_p)
{
    int i;

    for (i = 0; i < 3; i++) {
	if (ch_flags[f_p + i] == '+')
	    s[i] = ch_perm[i];
	else if (ch_flags[f_p + i] == '-')
	    s[i] = '-';
	else
	    s[i] = (ch_cmode & (1 << (8 - f_p - i))) ? ch_perm[i] : '-';
    }
}

static umode_t get_perm (char *s, int base)
{
    umode_t m;

    m = 0;
    m |= (s [0] == '-') ? 0 :
	((s[0] == '+') ? (1 << (base + 2)) : (1 << (base + 2)) & ch_cmode);

    m |= (s [1] == '-') ? 0 :
	((s[1] == '+') ? (1 << (base + 1)) : (1 << (base + 1)) & ch_cmode);
    
    m |= (s [2] == '-') ? 0 :
	((s[2] == '+') ? (1 << base) : (1 << base) & ch_cmode);

    return m;
}

static umode_t get_mode (void)
{
    umode_t m;

    m = ch_cmode ^ (ch_cmode & 0777);
    m |= get_perm (ch_flags, 6);
    m |= get_perm (ch_flags + 3, 3);
    m |= get_perm (ch_flags + 6, 0);

    return m;
}

static void print_flags (void)
{
    set_perm_by_flags (b_att[0]->text, 0);
    set_perm_by_flags (b_att[1]->text, 3);
    set_perm_by_flags (b_att[2]->text, 6);
}

static void chown_info_update (void);

static void update_mode (Dlg_head * h)
{
    chown_info_update();
    send_message (h->current, WIDGET_FOCUS, 0);
}

static cb_ret_t
chl_callback (Dlg_head *h, dlg_msg_t msg, int parm)
{
    switch (msg) {
    case DLG_KEY:
	switch (parm) {
	case KEY_LEFT:
	case KEY_RIGHT:
	    h->ret_value = parm;
	    dlg_stop (h);
	}

    default:
	return default_dlg_callback (h, msg, parm);
    }
}

static void
do_enter_key (Dlg_head * h, int f_pos)
{
    Dlg_head *chl_dlg;
    WListbox *chl_list;
    struct passwd *chl_pass;
    struct group *chl_grp;
    WLEntry *fe;
    int lxx, lyy, chl_end, b_pos;
    int is_owner;
    char *title;

    do {
	is_owner = (f_pos == 3);
	title = is_owner ? _("owner") : _("group");

	lxx = (COLS - 74) / 2 + (is_owner ? 35 : 53);
	lyy = (LINES - 13) / 2;
	chl_end = 0;

	chl_dlg =
	    create_dlg (lyy, lxx, 13, 17, dialog_colors, chl_callback,
			"[Advanced Chown]", title, DLG_COMPACT | DLG_REVERSE);

	/* get new listboxes */
	chl_list = listbox_new (Y_POS, X_POS, 15, 11, NULL);

	listbox_add_item (chl_list, 0, 0, "<Unknown>", NULL);

	if (is_owner) {
	    /* get and put user names in the listbox */
	    setpwent ();
	    while ((chl_pass = getpwent ()))
		listbox_add_item (chl_list, 0, 0, chl_pass->pw_name, NULL);
	    endpwent ();
	    fe = listbox_search_text (chl_list,
				      get_owner (sf_stat->st_uid));
	} else {
	    /* get and put group names in the listbox */
	    setgrent ();
	    while ((chl_grp = getgrent ())) {
		listbox_add_item (chl_list, 0, 0, chl_grp->gr_name, NULL);
	    }
	    endgrent ();
	    fe = listbox_search_text (chl_list,
				      get_group (sf_stat->st_gid));
	}

	if (fe)
	    listbox_select_entry (chl_list, fe);

	b_pos = chl_list->pos;
	add_widget (chl_dlg, chl_list);

	run_dlg (chl_dlg);

	if (b_pos != chl_list->pos) {
	    int ok = 0;
	    if (is_owner) {
		chl_pass = getpwnam (chl_list->current->text);
		if (chl_pass) {
		    ok = 1;
		    sf_stat->st_uid = chl_pass->pw_uid;
		}
	    } else {
		chl_grp = getgrnam (chl_list->current->text);
		if (chl_grp) {
		    sf_stat->st_gid = chl_grp->gr_gid;
		    ok = 1;
		}
	    }
	    if (ok) {
		ch_flags[f_pos + 6] = '+';
		get_ownership ();
	    }
	    dlg_focus (h);
	    if (ok)
		print_flags ();
	}
	if (chl_dlg->ret_value == KEY_LEFT) {
	    if (!is_owner)
		chl_end = 1;
	    dlg_one_up (ch_dlg);
	    f_pos--;
	} else if (chl_dlg->ret_value == KEY_RIGHT) {
	    if (is_owner)
		chl_end = 1;
	    dlg_one_down (ch_dlg);
	    f_pos++;
	}
	/* Here we used to redraw the window */
	destroy_dlg (chl_dlg);
    } while (chl_end);
}

static void chown_refresh (void)
{
    common_dialog_repaint (ch_dlg);


/*     if (!single_set){ */
/* 	dlg_move (ch_dlg, 3, 54); */
/* 	printw (_("%6d of %d"), files_on_begin - (current_panel->marked) + 1, */
/* 		   files_on_begin); */
/*     } */

/*     print_flags (); */

}

static void chown_info_update (void)
{
    char  buffer [BUF_SMALL];
    char  buffer2 [BUF_TINY];
    print_flags ();
    g_snprintf (buffer, sizeof(buffer), "%s: %o", _("Mode"), get_mode ());
    label_set_text (l_mode, buffer);

    if (fname) {
      *buffer2=0;

      if (!single_set) {
	g_snprintf (buffer2, sizeof (buffer), _("(%d of %d)"),  
		    files_on_begin - (current_panel->marked) + 1,
		    files_on_begin);
      }

      g_snprintf (buffer, sizeof (buffer), "%s: %s %s",
		  _("On"), name_trunc (fname, 45),
		  buffer2
		  );

      label_set_text (l_filename, buffer);
    }
}

static void b_setpos (int f_pos) {
	b_att[0]->hotpos=-1;
	b_att[1]->hotpos=-1;
	b_att[2]->hotpos=-1;
	b_att[f_pos]->hotpos = (flag_pos % 3);
}

static cb_ret_t
advanced_chown_callback (Dlg_head *h, dlg_msg_t msg, int parm)
{
    int i = 0, f_pos = NB_BUTTONS + LABELS - h->current->dlg_id - 1;

    if (msg==DLG_DRAW)
      {
	chown_refresh ();
	chown_info_update ();
	return MSG_HANDLED;
      }

    /* 
       dlg_id=0 is the first submit button
       there are 2 or 4 submit buttons.

       dlg_id=nb_submit_buttons is a label (Mode)
       then a set of five couples (narrow button+label) 
       then a label (Filename)
     */
    if (h->current->callback == button_callback)
      {
	if (h->current->dlg_id > nb_submit_buttons-1) { 
	  /* Compute the button pos without the labels */
	  f_pos = (f_pos-1)/2;
	}
    }
    else {
	return MSG_NOT_HANDLED;
    }


/*     if (h->current->dlg_id > nb_submit_buttons) {  */
/*       /\* Either a label or a button *\/ */
/*       /\* Labels have even indexes, except the first one *\/ */
/*       if (h->current->dlg_id % 2 */
/* 	  || (h->current->dlg_id >= nb_submit_buttons + 2*(NARROW_BUTTONS))) { */
/* 	return MSG_NOT_HANDLED; */
/*       } */
/*       else {  */
/* 	/\* Compute the button pos without the labels *\/ */
/* 	f_pos = f_pos/2; */
/*       } */
/*     } */
      
    switch (msg) {
    case DLG_POST_KEY:
	if (f_pos < 3)
	    b_setpos (f_pos);
	return MSG_HANDLED;

    case DLG_FOCUS:
      if (f_pos < 3) { /* access rights */
	    if ((flag_pos / 3) != f_pos)
		flag_pos = f_pos * 3;
	    b_setpos (f_pos);
      } else if (f_pos < 5) /* owner and group names */
	    flag_pos = f_pos + 6;
	return MSG_HANDLED;

    case DLG_KEY:
	switch (parm) {

	case XCTRL ('b'):
	case KEY_LEFT:
	    if (f_pos < 5)
		return (dec_flag_pos (f_pos));
	    break;

	case XCTRL ('f'):
	case KEY_RIGHT:
	    if (f_pos < 5)
		return (inc_flag_pos (f_pos));
	    break;

	case ' ':
	    if (f_pos < 3)
		return MSG_HANDLED;
	    break;

	case '\n':
	case KEY_ENTER:
	    if (f_pos <= 2 || f_pos >= 5)
		break;
	    do_enter_key (h, f_pos);
	    return MSG_HANDLED;

	case ALT ('x'):
	    i++;

	case ALT ('w'):
	    i++;

	case ALT ('r'):
	    parm = i + 3;
	    for (i = 0; i < 3; i++)
		ch_flags[i * 3 + parm - 3] =
		    (x_toggle & (1 << parm)) ? '-' : '+';
	    x_toggle ^= (1 << parm);
	    update_mode (h);
	    dlg_broadcast_msg (h, WIDGET_DRAW, 0);
	    send_message (h->current, WIDGET_FOCUS, 0);
	    break;

	case XCTRL ('x'):
	    i++;

	case XCTRL ('w'):
	    i++;

	case XCTRL ('r'):
	    parm = i;
	    for (i = 0; i < 3; i++)
		ch_flags[i * 3 + parm] =
		    (x_toggle & (1 << parm)) ? '-' : '+';
	    x_toggle ^= (1 << parm);
	    update_mode (h);
	    dlg_broadcast_msg (h, WIDGET_DRAW, 0);
	    send_message (h->current, WIDGET_FOCUS, 0);
	    break;

	case 'x':
	    i++;

	case 'w':
	    i++;

	case 'r':
	    if (f_pos > 2)
		break;
	    flag_pos = f_pos * 3 + i;	/* (strchr(ch_perm,parm)-ch_perm); */
	    if (((WButton *) h->current)->text[(flag_pos % 3)] ==
		'-')
		ch_flags[flag_pos] = '+';
	    else
		ch_flags[flag_pos] = '-';
	    update_mode (h);
	    break;

	case '4':
	    i++;

	case '2':
	    i++;

	case '1':
	    if (f_pos > 2)
		break;
	    flag_pos = i + f_pos * 3;
	    ch_flags[flag_pos] = '=';
	    update_mode (h);
	    break;

	case '-':
	    if (f_pos > 2)
		break;

	case '*':
	    if (parm == '*')
		parm = '=';

	case '=':
	case '+':
	    if (f_pos > 4)
		break;
	    ch_flags[flag_pos] = parm;
	    update_mode (h);
	    advanced_chown_callback (h, KEY_RIGHT, DLG_KEY);
	    if (flag_pos > 8 || !(flag_pos % 3))
		dlg_one_down (h);

	    break;
	}
	return MSG_NOT_HANDLED;

    default:
	return default_dlg_callback (h, msg, parm);
    }
}

static void
init_chown_advanced (void)
{
    int i;
    char  buffer [BUF_SMALL];

    sf_stat = g_new (struct stat, 1);
    do_refresh ();
    end_chown = need_update = current_file = 0;

    if (current_panel->marked < 2)
      {
	nb_submit_buttons=2;
	single_set=1;
      }
    else
      {
	nb_submit_buttons=4;
	single_set=0;
      }

    memset (ch_flags, '=', 11);
    flag_pos = 0;
    x_toggle = 070;

    ch_dlg =
	create_dlg (0, 0, 13, 74, dialog_colors, advanced_chown_callback,
		    "[Advanced Chown]", _(" Chown advanced command "),
		    DLG_CENTER | DLG_REVERSE);

#define XTRACT(i) Y_POS, X_POS, \
	chown_advanced_but[i].ret_cmd, chown_advanced_but[i].flags, \
	(chown_advanced_but[i].text), 0

    for (i = 0; i < nb_submit_buttons; i++)
      add_widget (ch_dlg, button_new (XTRACT (i)));

    b_att[0] = button_new (XTRACT (8));
    b_att[1] = button_new (XTRACT (7));
    b_att[2] = button_new (XTRACT (6));
    b_user = button_new (XTRACT (5));
    b_group = button_new (XTRACT (4));

    b_user->hotpos = 0;
    b_group->hotpos = 0;

    button_callback=b_group->widget.callback;

    l_mode=label_new (Y_POS, X_POS, " ");
    add_widget (ch_dlg, l_mode);

    add_widget (ch_dlg, b_group);
    g_snprintf (buffer, sizeof (buffer), "5. %s", 
		_("group"));
    add_widget (ch_dlg, label_new (Y_POS, X_POS, buffer));

    add_widget (ch_dlg, b_user);
    g_snprintf (buffer, sizeof (buffer), "4. %s", 
		_("owner"));
    add_widget (ch_dlg, label_new (Y_POS, X_POS, buffer));


    add_widget (ch_dlg, b_att[2]);
    g_snprintf (buffer, sizeof (buffer), "3. %s", 
		_("other"));
    add_widget (ch_dlg, label_new (Y_POS, X_POS, buffer));


    add_widget (ch_dlg, b_att[1]);
    g_snprintf (buffer, sizeof (buffer), "2. %s", 
		_("group"));
    add_widget (ch_dlg, label_new (Y_POS, X_POS, buffer));


    add_widget (ch_dlg, b_att[0]);
    g_snprintf (buffer, sizeof (buffer), "1. %s", 
		_("owner"));
    add_widget (ch_dlg, label_new (Y_POS, X_POS, buffer));

    /* filename */
    l_filename = label_new (Y_POS, X_POS, " ");
    add_widget (ch_dlg, l_filename);

/*     if (!single_set){ */
/* 	dlg_move (ch_dlg, 3, 54); */
/* 	printw (_("%6d of %d"), files_on_begin - (current_panel->marked) + 1, */
/*     g_snprintf (buffer, sizeof (buffer),  */
/* 		"%s: %s",  */
/* 		_("On"), text); */
/*     add_widget (ch_dlg, label_new (Y_POS, X_POS, buffer)); */

}

static void
chown_advanced_done (void)
{
    g_free (sf_stat);
    if (need_update)
	update_panels (UP_OPTIMIZE, UP_KEEPSEL);
    repaint_screen ();
}

#if 0
static inline void do_chown (uid_t u, gid_t g)
{
    chown (current_panel->dir.list[current_file].fname, u, g);
    file_mark (current_panel, current_file, 0);
}
#endif

static char *next_file (void)
{
    while (!current_panel->dir.list[current_file].f.marked)
	current_file++;

    return current_panel->dir.list[current_file].fname;
}

static void apply_advanced_chowns (struct stat *sf)
{
    char *fname;
    gid_t a_gid = sf->st_gid;
    uid_t a_uid = sf->st_uid;

    fname = current_panel->dir.list[current_file].fname;
    need_update = end_chown = 1;
    if (mc_chmod (fname, get_mode ()) == -1)
	message (1, MSG_ERROR, _(" Cannot chmod \"%s\" \n %s "),
		 fname, unix_error_string (errno));
    /* call mc_chown only, if mc_chmod didn't fail */
    else if (mc_chown (fname, (ch_flags[9] == '+') ? sf->st_uid : -1,
		       (ch_flags[10] == '+') ? sf->st_gid : -1) == -1)
	message (1, MSG_ERROR, _(" Cannot chown \"%s\" \n %s "),
		 fname, unix_error_string (errno));
    do_file_mark (current_panel, current_file, 0);

    do {
	fname = next_file ();

	if (mc_stat (fname, sf) != 0)
	    break;
	ch_cmode = sf->st_mode;
	if (mc_chmod (fname, get_mode ()) == -1)
	    message (1, MSG_ERROR, _(" Cannot chmod \"%s\" \n %s "),
		     fname, unix_error_string (errno));
	/* call mc_chown only, if mc_chmod didn't fail */
	else if (mc_chown (fname, (ch_flags[9] == '+') ? a_uid : -1, (ch_flags[10] == '+') ? a_gid : -1) == -1)
	    message (1, MSG_ERROR, _(" Cannot chown \"%s\" \n %s "),
		     fname, unix_error_string (errno));

	do_file_mark (current_panel, current_file, 0);
    } while (current_panel->marked);
}

void
chown_advanced_cmd (void)
{

    files_on_begin = current_panel->marked;

    do {			/* do while any files remaining */
	init_chown_advanced ();

	if (current_panel->marked)
	    fname = next_file ();	/* next marked file */
	else
	    fname = selection (current_panel)->fname;	/* single file */

	if (mc_stat (fname, sf_stat) != 0) {	/* get status of file */
	    destroy_dlg (ch_dlg);
	    break;
	}
	ch_cmode = sf_stat->st_mode;

	chown_refresh ();
	
	get_ownership ();

	/* game can begin */
	run_dlg (ch_dlg);

	switch (ch_dlg->ret_value) {
	case B_CANCEL:
	    end_chown = 1;
	    break;

	case B_ENTER:
	    need_update = 1;
	    if (mc_chmod (fname, get_mode ()) == -1)
		message (1, MSG_ERROR, _(" Cannot chmod \"%s\" \n %s "),
			 fname, unix_error_string (errno));
	    /* call mc_chown only, if mc_chmod didn't fail */
	    else if (mc_chown (fname, (ch_flags[9] == '+') ? sf_stat->st_uid : -1, (ch_flags[10] == '+') ? sf_stat->st_gid : -1) == -1)
		message (1, MSG_ERROR, _(" Cannot chown \"%s\" \n %s "),
			 fname, unix_error_string (errno));
	    break;
	case B_SETALL:
	    apply_advanced_chowns (sf_stat);
	    break;

	case B_SKIP:
	    break;

	}

	if (current_panel->marked && ch_dlg->ret_value != B_CANCEL) {
	    do_file_mark (current_panel, current_file, 0);
	    need_update = 1;
	}
	destroy_dlg (ch_dlg);
    } while (current_panel->marked && !end_chown);

    chown_advanced_done ();
}
