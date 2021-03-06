/*
 * Single File fileSystem
 *
 * Copyright 1998 Pavel Machek, distribute under GPL
 *
 * This defines whole class of filesystems which contain single file
 * inside. It is somehow similar to extfs, except that extfs makes
 * whole virtual trees and we do only single virtual files. 
 *
 * If you want to gunzip something, you should open it with #ugz
 * suffix, DON'T try to gunzip it yourself.
 *
 * Namespace: exports vfs_sfs_ops */

#include <config.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include "utilvfs.h"

#include "vfs.h"
#include "gc.h"		/* vfs_stamp_create */
#include "local.h"
#include "../src/execute.h"	/* EXECUTE_AS_SHELL */

struct cachedfile {
    char *name, *cache;
    struct cachedfile *next;
};

static struct cachedfile *head;
static struct vfs_class vfs_sfs_ops;

#define MAXFS 32
static int sfs_no = 0;
static char *sfs_prefix[ MAXFS ];
static char *sfs_command[ MAXFS ];
static int sfs_flags[ MAXFS ];
#define F_1 1
#define F_2 2
#define F_NOLOCALCOPY 4
#define F_FULLMATCH 8

static int
sfs_uptodate (char *name, char *cache)
{
    return 1;
}

static int
sfs_vfmake (struct vfs_class *me, const char *name, char *cache)
{
    char *inpath, *op;
    int w;
    char pad[10240];
    char *s, *t = pad;
    int was_percent = 0;
    char *pname;	/* name of parent archive */
    char *pqname;	/* name of parent archive, quoted */

    pname = g_strdup (name);
    vfs_split (pname, &inpath, &op);
    if ((w = (*me->which) (me, op)) == -1)
	vfs_die ("This cannot happen... Hopefully.\n");

    if (!(sfs_flags[w] & F_1) && strcmp (pname, "/")) {
	g_free (pname);
	return -1;
    }

    /*    if ((sfs_flags[w] & F_2) || (!inpath) || (!*inpath)); else return -1; */
    if (!(sfs_flags[w] & F_NOLOCALCOPY)) {
	s = mc_getlocalcopy (pname);
	if (!s) {
	    g_free (pname);
	    return -1;
	}
	pqname = name_quote (s, 0);
	g_free (s);
    } else {
	pqname = name_quote (pname, 0);
    }
    g_free (pname);

#define COPY_CHAR \
    if (t-pad>sizeof(pad)) { \
	g_free (pqname); \
	return -1; \
    } \
    else \
	*t++ = *s;

#define COPY_STRING(a) \
    if ((t-pad)+strlen(a)>sizeof(pad)) { \
	g_free (pqname); \
	return -1; \
    } else { \
	strcpy (t, a); \
	t+= strlen(a); \
    }

    for (s = sfs_command[w]; *s; s++) {
	if (was_percent) {

	    const char *ptr = NULL;
	    was_percent = 0;

	    switch (*s) {
	    case '1':
		ptr = pqname;
		break;
	    case '2':
		ptr = op + strlen (sfs_prefix[w]);
		break;
	    case '3':
		ptr = cache;
		break;
	    case '%':
		COPY_CHAR;
		continue;
	    }
	    COPY_STRING (ptr);
	} else {
	    if (*s == '%')
		was_percent = 1;
	    else
		COPY_CHAR;
	}
    }

    g_free (pqname);
    open_error_pipe ();
    if (my_system (EXECUTE_AS_SHELL, "/bin/sh", pad)) {
	close_error_pipe (1, NULL);
	return -1;
    }

    close_error_pipe (0, NULL);
    return 0;			/* OK */
}

static char *
sfs_redirect (struct vfs_class *me, const char *name)
{
    struct cachedfile *cur = head;
    char *cache;
    int handle;

    while (cur) {
	if ((!strcmp (name, cur->name))
	    && (sfs_uptodate (cur->name, cur->cache))) {
	    vfs_stamp (&vfs_sfs_ops, cur);
	    return cur->cache;
	}
	cur = cur->next;
    }

    handle = vfs_mkstemps (&cache, "sfs", name);

    if (handle == -1) {
	return "/SOMEONE_PLAYING_DIRTY_TMP_TRICKS_ON_US";
    }

    close (handle);

    if (!sfs_vfmake (me, name, cache)) {
	cur = g_new (struct cachedfile, 1);
	cur->name = g_strdup (name);
	cur->cache = cache;
	cur->next = head;
	head = cur;

	vfs_stamp_create (&vfs_sfs_ops, head);

	return cache;
    }

    unlink (cache);
    g_free (cache);
    return "/I_MUST_NOT_EXIST";
}

static void *
sfs_open (struct vfs_class *me, const char *path, int flags, int mode)
{
    int *sfs_info;
    int fd;

    path = sfs_redirect (me, path);
    fd = open (path, NO_LINEAR (flags), mode);
    if (fd == -1)
	return 0;

    sfs_info = g_new (int, 1);
    *sfs_info = fd;

    return sfs_info;
}

static int sfs_stat (struct vfs_class *me, char *path, struct stat *buf)
{
    path = sfs_redirect (me, path);
    return stat (path, buf);
}

static int sfs_lstat (struct vfs_class *me, char *path, struct stat *buf)
{
    path = sfs_redirect (me, path);
#ifndef HAVE_STATLSTAT
    return lstat (path, buf);
#else
    return statlstat (path, buf);
#endif
}

static int sfs_chmod (struct vfs_class *me, char *path, int mode)
{
    path = sfs_redirect (me, path);
    return chmod (path, mode);
}

static int sfs_chown (struct vfs_class *me, char *path, int owner, int group)
{
    path = sfs_redirect (me, path);
    return chown (path, owner, group);
}

static int sfs_utime (struct vfs_class *me, char *path, struct utimbuf *times)
{
    path = sfs_redirect (me, path);
    return utime (path, times);
}

static int sfs_readlink (struct vfs_class *me, char *path, char *buf, int size)
{
    path = sfs_redirect (me, path);
    return readlink (path, buf, size);
}

static vfsid
sfs_getid (struct vfs_class *me, const char *path)
{
    struct cachedfile *cur = head;

    while (cur) {
	if (!strcmp (path, cur->name))
	    break;
	cur = cur->next;
    }

    return (vfsid) cur;
}

static void sfs_free (vfsid id)
{
    struct cachedfile *which = (struct cachedfile *) id;
    struct cachedfile *cur, *prev;

    for (cur = head, prev = 0; cur && cur != which; prev = cur, cur = cur->next)
	;
    if (!cur)
    	vfs_die( "Free of thing which is unknown to me\n" );
    unlink (cur->cache);

    if (prev)
	prev->next = cur->next;
    else
	head = cur->next;

    g_free (cur->cache);
    g_free (cur->name);
    g_free (cur);
}

static void sfs_fill_names (struct vfs_class *me, void (*func)(char *))
{
    struct cachedfile *cur = head;

    while (cur){
	(*func)(cur->name);
	cur = cur->next;
    }
}

static int sfs_nothingisopen (vfsid id)
{
    /* FIXME: Investigate whether have to guard this like in
       the other VFSs (see fd_usage in extfs) -- Norbert */
    return 1;
}

static char *
sfs_getlocalcopy (struct vfs_class *me, const char *path)
{
    path = sfs_redirect (me, path);
    return g_strdup (path);
}

static int
sfs_ungetlocalcopy (struct vfs_class *me, const char *path,
		    const char *local, int has_changed)
{
    return 0;
}

static int sfs_init (struct vfs_class *me)
{
    char *mc_sfsini;
    FILE *cfg;

    mc_sfsini = concat_dir_and_file (mc_home, "extfs" PATH_SEP_STR "sfs.ini");
    cfg = fopen (mc_sfsini, "r");

    if (!cfg){
	fprintf (stderr, _("Warning: file %s not found\n"), mc_sfsini);
	g_free (mc_sfsini);
	return 0;
    }
    g_free (mc_sfsini);

    sfs_no = 0;
    while (sfs_no < MAXFS){
	char key[256];
	char *c, *semi = NULL, flags = 0;

        if (!fgets (key, sizeof (key), cfg))
	    break;

	if (*key == '#')
	    continue;

	for (c = key; *c; c++)
	    if ((*c == ':') || (*c == '/')){
		semi = c;
		if (*c == '/'){
		    *c = 0;
		    flags |= F_FULLMATCH;
		}
		break;
	    }

	if (!semi){
	    fprintf (stderr, _("Warning: Invalid line in %s:\n%s\n"),
		     "sfs.ini", key);
	    continue;
	}

	c = semi + 1;
	while ((*c != ' ') && (*c != '\t')) {
	    switch (*c) {
	    case '1': flags |= F_1; break;
	    case '2': flags |= F_2; break;
	    case 'R': flags |= F_NOLOCALCOPY; break;
	    default:
		fprintf (stderr, _("Warning: Invalid flag %c in %s:\n%s\n"),
			 *c, "sfs.ini", key);
	    }	    
	    c++;
	}
	c++;
	*(semi+1) = 0;
	if ((semi = strchr (c, '\n')))
	    *semi = 0;

	sfs_prefix [sfs_no] = g_strdup (key);
	sfs_command [sfs_no] = g_strdup (c);
	sfs_flags [sfs_no] = flags;
	sfs_no++;
    }
    fclose (cfg);
    return 1;
}

static void
sfs_done (struct vfs_class *me)
{
    int i;

    for (i = 0; i < sfs_no; i++){
        g_free (sfs_prefix [i]);
	g_free (sfs_command [i]);
	sfs_prefix [i] = sfs_command [i] = NULL;
    }
    sfs_no = 0;
}

static int
sfs_which (struct vfs_class *me, char *path)
{
    int i;

    for (i = 0; i < sfs_no; i++)
        if (sfs_flags [i] & F_FULLMATCH) {
	    if (!strcmp (path, sfs_prefix [i]))
	        return i;
	} else
	    if (!strncmp (path, sfs_prefix [i], strlen (sfs_prefix [i])))
	        return i;

    return -1;
}

void
init_sfs (void)
{
    vfs_sfs_ops.name = "sfs";
    vfs_sfs_ops.init = sfs_init;
    vfs_sfs_ops.done = sfs_done;
    vfs_sfs_ops.fill_names = sfs_fill_names;
    vfs_sfs_ops.which = sfs_which;
    vfs_sfs_ops.open = sfs_open;
    vfs_sfs_ops.close = local_close;
    vfs_sfs_ops.read = local_read;
    vfs_sfs_ops.stat = sfs_stat;
    vfs_sfs_ops.lstat = sfs_lstat;
    vfs_sfs_ops.fstat = local_fstat;
    vfs_sfs_ops.chmod = sfs_chmod;
    vfs_sfs_ops.chown = sfs_chown;
    vfs_sfs_ops.utime = sfs_utime;
    vfs_sfs_ops.readlink = sfs_readlink;
    vfs_sfs_ops.ferrno = local_errno;
    vfs_sfs_ops.lseek = local_lseek;
    vfs_sfs_ops.getid = sfs_getid;
    vfs_sfs_ops.nothingisopen = sfs_nothingisopen;
    vfs_sfs_ops.free = sfs_free;
    vfs_sfs_ops.getlocalcopy = sfs_getlocalcopy;
    vfs_sfs_ops.ungetlocalcopy = sfs_ungetlocalcopy;
#ifdef HAVE_MMAP
    vfs_sfs_ops.mmap = local_mmap;
    vfs_sfs_ops.munmap = local_munmap;
#endif
    vfs_register_class (&vfs_sfs_ops);
}
