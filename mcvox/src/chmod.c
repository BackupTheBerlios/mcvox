/* Chmod command -- for the Midnight Commander
   Copyright (C) 1994 Radek Doulik

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
#include <string.h>
#include <stdio.h>
#include <errno.h>	/* For errno on SunOS systems	      */
/* Needed for the extern declarations of integer parameters */
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_UNISTD_H
#   include <unistd.h>
#endif

#include "global.h"
#include "tty.h"	/* A_REVERSE */
#include "color.h"	/* dialog_colors */
#include "dialog.h"	/* add_widget() */
#include "widget.h"	/* NORMAL_BUTTON */
#include "wtools.h"	/* message() */
#include "panel.h"	/* do_file_mark() */
#include "main.h"	/* update_panels() */
#include "chmod.h"

static int single_set;

#define PX		5
#define PY		2

#define FX		40
#define FY		2

#define BX		6
#define BY		17

#define TX		40
#define TY		12

#define PERMISSIONS	12
#define BUTTONS		6

#define B_MARKED	B_USER
#define B_ALL		(B_USER+1)
#define B_SETMRK        (B_USER+2)
#define B_CLRMRK        (B_USER+3)

#define Y_POS 2
#define X_POS 0

static int mode_change, need_update;
static int c_file, end_chmod;

static umode_t and_mask, or_mask, c_stat;
static char *c_fname, *c_fown, *c_fgrp;

static WLabel *statl;

static struct {
    mode_t mode;
    char *text;
    int selected;
    WCheck *check;
} check_perm[PERMISSIONS] = 
{
    { S_IXOTH, N_("execute/search by others"), 0, 0, },
    { S_IWOTH, N_("write by others"), 0, 0, },
    { S_IROTH, N_("read by others"), 0, 0, },
    { S_IXGRP, N_("execute/search by group"), 0, 0, },
    { S_IWGRP, N_("write by group"), 0, 0, },
    { S_IRGRP, N_("read by group"), 0, 0, },
    { S_IXUSR, N_("execute/search by owner"), 0, 0, },
    { S_IWUSR, N_("write by owner"), 0, 0, },
    { S_IRUSR, N_("read by owner"), 0, 0, },
    { S_ISVTX, N_("sticky bit"), 0, 0, },
    { S_ISGID, N_("set group ID on execution"), 0, 0, },
    { S_ISUID, N_("set user ID on execution"), 0, 0, },
};

static struct {
    int ret_cmd, flags, y, x;
    char *text;
} chmod_but[BUTTONS] = 
{
    { B_CANCEL, NORMAL_BUTTON,  2, 33, N_("&Cancel") },
    { B_ENTER,  DEFPUSH_BUTTON, 2, 17, N_("&Set") },
    { B_CLRMRK, NORMAL_BUTTON,  0, 42, N_("C&lear marked") },
    { B_SETMRK, NORMAL_BUTTON,  0, 27, N_("S&et marked") },
    { B_MARKED, NORMAL_BUTTON,  0, 12, N_("&Marked all") },
    { B_ALL,    NORMAL_BUTTON,  0, 0,  N_("Set &all") },
};

static void chmod_toggle_select (Dlg_head *h, int Id)
{
  attrset (COLOR_NORMAL);
  check_perm[Id].selected ^= 1;
  check_set_mark (check_perm[Id].check, check_perm[Id].selected);
}

static void chmod_toggle_mark (Dlg_head *h, int Id)
{
  attrset (COLOR_NORMAL);
  check_perm[Id].selected ^= 1;
  check_set_mark (check_perm[Id].check, check_perm[Id].selected);
}

static void chmod_refresh (Dlg_head *h)
{
    common_dialog_repaint (h);
}

static cb_ret_t
chmod_callback (Dlg_head *h, dlg_msg_t msg, int parm)
{
    char buffer[BUF_TINY];
    int id = h->current->dlg_id - BUTTONS + single_set * 2;

    switch (msg) {
    case DLG_ACTION:
	if (id >= 0) {
	    c_stat ^= check_perm[id].mode;
	    g_snprintf (buffer, sizeof (buffer), "%o", c_stat);
	    label_set_text (statl, buffer);
 	    chmod_toggle_select (h, id);
	    mode_change = 1;
	}
	return MSG_HANDLED;

    case DLG_KEY:
	if ((parm == 'T' || parm == 't' || parm == KEY_IC) && id > 0) {
	    chmod_toggle_mark (h, id);
/* 	    if (parm == KEY_IC) */
/* 		dlg_one_down (h); */
	    return MSG_HANDLED;
	}
	return MSG_NOT_HANDLED;

    case DLG_DRAW:
	chmod_refresh (h);
	return MSG_HANDLED;

    default:
	return default_dlg_callback (h, msg, parm);
    }
}

static Dlg_head *
init_chmod (void)
{
    int i;
    Dlg_head *ch_dlg;
    char buffer [BUF_TINY];

    do_refresh ();
    end_chmod = c_file = need_update = 0;
    single_set = (current_panel->marked < 2) ? 2 : 0;

    ch_dlg =
	create_dlg (0, 0, 22 - single_set, 70, dialog_colors,
		    chmod_callback, "[Chmod]", _("Chmod command"),
		    DLG_CENTER | DLG_REVERSE);

    for (i = 0; i < BUTTONS; i++) {
	if (i == 2 && single_set)
	    break;
	else
	    add_widget (ch_dlg,
			button_new (Y_POS,
				    X_POS,
				    chmod_but[i].ret_cmd,
				    chmod_but[i].flags,
				    _(chmod_but[i].text), 0));
    }

    for (i = 0; i < PERMISSIONS; i++) {
	check_perm[i].check =
	    check_new (Y_POS, X_POS, 0,
		       _(check_perm[i].text));
	add_widget (ch_dlg, check_perm[i].check);
    }

    g_snprintf (buffer, sizeof (buffer), "2. %s", 
		_(" Permission "));
    add_widget (ch_dlg, label_new (Y_POS, X_POS, buffer));

    return ch_dlg;
}

static void chmod_done (void)
{
    if (need_update)
	update_panels (UP_OPTIMIZE, UP_KEEPSEL);
    repaint_screen ();
}

static char *next_file (void)
{
    while (!current_panel->dir.list[c_file].f.marked)
	c_file++;

    return current_panel->dir.list[c_file].fname;
}

static void do_chmod (struct stat *sf)
{
    sf->st_mode &= and_mask;
    sf->st_mode |= or_mask;
    if (mc_chmod (current_panel->dir.list [c_file].fname, sf->st_mode) == -1)
	message (1, MSG_ERROR, _(" Cannot chmod \"%s\" \n %s "),
	     current_panel->dir.list [c_file].fname, unix_error_string (errno));

    do_file_mark (current_panel, c_file, 0);
}

static void apply_mask (struct stat *sf)
{
    char *fname;

    need_update = end_chmod = 1;
    do_chmod (sf);

    do {
	fname = next_file ();
	if (mc_stat (fname, sf) != 0)
	    return;
	c_stat = sf->st_mode;

	do_chmod (sf);
    } while (current_panel->marked);
}

void chmod_cmd (void)
{
    char buffer [BUF_SMALL];
    char *fname;
    int i;
    struct stat sf_stat;
    Dlg_head *ch_dlg;

    do {			/* do while any files remaining */
	ch_dlg = init_chmod ();
	if (current_panel->marked)
	    fname = next_file ();	/* next marked file */
	else
	    fname = selection (current_panel)->fname;	/* single file */

	if (mc_stat (fname, &sf_stat) != 0) {	/* get status of file */
	    destroy_dlg (ch_dlg);
	    break;
	}
	
	c_stat = sf_stat.st_mode;
	mode_change = 0;	/* clear changes flag */

	/* set check buttons */
	for (i = 0; i < PERMISSIONS; i++){
	    check_perm[i].check->state = (c_stat & check_perm[i].mode) ? 1 : 0;
	    check_perm[i].selected = 0;
	}

	/* Set the labels */

	c_fgrp = name_trunc (get_group (sf_stat.st_gid), 21);
	g_snprintf (buffer, sizeof (buffer), "%s: %s", 
		    _("Group name"), c_fgrp);
	add_widget (ch_dlg, label_new (Y_POS, X_POS, buffer));

	c_fown = name_trunc (get_owner (sf_stat.st_uid), 21);
	g_snprintf (buffer, sizeof (buffer), "%s: %s", 
		    _("Owner name"), c_fown);
	add_widget (ch_dlg, label_new (Y_POS, X_POS, buffer));

	g_snprintf (buffer, sizeof (buffer), "%s: %o", 
		    _("Permissions (Octal)"), c_stat);
	statl = label_new (Y_POS, X_POS, buffer);
	add_widget (ch_dlg, statl);

	c_fname = name_trunc (fname, 21);
	g_snprintf (buffer, sizeof (buffer), "%s: %s", 
		    _("Name"), fname);
	add_widget (ch_dlg, label_new (Y_POS, X_POS, buffer));


	g_snprintf (buffer, sizeof (buffer), "1. %s", 
		    _(" File "));
	add_widget (ch_dlg, label_new (Y_POS, X_POS, buffer));

	g_snprintf (buffer, sizeof (buffer), "%s %s", 
		    _("to move between options"),
		    _("and T or INS to mark")
		    );
	add_widget (ch_dlg, label_new (Y_POS, X_POS, buffer));

	g_snprintf (buffer, sizeof (buffer), "%s %s", 
		    _("Use SPACE to change"),
		    _("an option, ARROW KEYS")
		    );
	add_widget (ch_dlg, label_new (Y_POS, X_POS, buffer));
	run_dlg (ch_dlg);	/* retrieve an action */
	
	/* do action */
	switch (ch_dlg->ret_value){
	case B_ENTER:
	    if (mode_change)
		if (mc_chmod (fname, c_stat) == -1)
		    message (1, MSG_ERROR, _(" Cannot chmod \"%s\" \n %s "),
	 		 fname, unix_error_string (errno));
	    need_update = 1;
	    break;
	    
	case B_CANCEL:
	    end_chmod = 1;
	    break;
	    
	case B_ALL:
	case B_MARKED:
	    and_mask = or_mask = 0;
	    and_mask = ~and_mask;

	    for (i = 0; i < PERMISSIONS; i++) {
		if (check_perm[i].selected || ch_dlg->ret_value == B_ALL) {
		    if (check_perm[i].check->state & C_BOOL)
			or_mask |= check_perm[i].mode;
		    else
			and_mask &= ~check_perm[i].mode;
		}
	    }

	    apply_mask (&sf_stat);
	    break;
	    
	case B_SETMRK:
	    and_mask = or_mask = 0;
	    and_mask = ~and_mask;

	    for (i = 0; i < PERMISSIONS; i++) {
		if (check_perm[i].selected)
		    or_mask |= check_perm[i].mode;
	    }

	    apply_mask (&sf_stat);
	    break;
	case B_CLRMRK:
	    and_mask = or_mask = 0;
	    and_mask = ~and_mask;

	    for (i = 0; i < PERMISSIONS; i++) {
		if (check_perm[i].selected)
		    and_mask &= ~check_perm[i].mode;
	    }

	    apply_mask (&sf_stat);
	    break;
	}

	if (current_panel->marked && ch_dlg->ret_value!=B_CANCEL) {
	    do_file_mark (current_panel, c_file, 0);
    	    need_update = 1;
	}
	destroy_dlg (ch_dlg);
    } while (current_panel->marked && !end_chmod);
    chmod_done ();
}

