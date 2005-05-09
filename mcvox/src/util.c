/* Various utilities
   Copyright (C) 1994, 1995, 1996 the Free Software Foundation.
   Written 1994, 1995, 1996 by:
   Miguel de Icaza, Janne Kukonlehto, Dugan Porter,
   Jakub Jelinek, Mauricio Plaza.

   The file_date routine is mostly from GNU's fileutils package,
   written by Richard Stallman and David MacKenzie.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#include <config.h>
#include <stdio.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#   include <unistd.h>
#endif
#include <limits.h>		/* INT_MAX */
#include <sys/stat.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#include "global.h"
#include "profile.h"
#include "main.h"		/* mc_home */
#include "cmd.h"		/* guess_message_value */
#include "mountlist.h"
#include "win.h"		/* xterm_flag */

#ifdef HAVE_CHARSET
#include "charsets.h"
#endif

static const char app_text [] = "Midnight-Commander";
int easy_patterns = 1;

static inline int
is_7bit_printable (unsigned char c)
{
    return (c > 31 && c < 127);
}

static inline int
is_iso_printable (unsigned char c)
{
    return ((c > 31 && c < 127) || c >= 160);
}

static inline int
is_8bit_printable (unsigned char c)
{
    /* "Full 8 bits output" doesn't work on xterm */
    if (xterm_flag)
	return is_iso_printable (c);

    return (c > 31 && c != 127 && c != 155);
}

int
is_printable (int c)
{
    c &= 0xff;

#ifdef HAVE_CHARSET
    /* "Display bits" is ignored, since the user controls the output
       by setting the output codepage */
    return is_8bit_printable (c);
#else
    if (!eight_bit_clean)
	return is_7bit_printable (c);

    if (full_eight_bits) {
	return is_8bit_printable (c);
    } else
	return is_iso_printable (c);
#endif				/* !HAVE_CHARSET */
}

/* Returns the message dimensions (lines and columns) */
int msglen (const char *text, int *lines)
{
    int max = 0;
    int line_len = 0;
    
    for (*lines = 1;*text; text++){
	if (*text == '\n'){
	    line_len = 0;
	    (*lines)++;
	} else {
	    line_len++;
	    if (line_len > max)
		max = line_len;
	}
    }
    return max;
}

/*
 * Copy from s to d, and trim the beginning if necessary, and prepend
 * "..." in this case.  The destination string can have at most len
 * bytes, not counting trailing 0.
 */
char *
trim (char *s, char *d, int len)
{
    int source_len;

    /* Sanity check */
    len = max (len, 0);

    source_len = strlen (s);
    if (source_len > len) {
	/* Cannot fit the whole line */
	if (len <= 3) {
	    /* We only have room for the dots */
	    memset (d, '.', len);
	    d[len] = 0;
	    return d;
	} else {
	    /* Begin with ... and add the rest of the source string */
	    memset (d, '.', 3);
	    strcpy (d + 3, s + 3 + source_len - len);
	}
    } else
	/* We can copy the whole line */
	strcpy (d, s);
    return d;
}

/*
 * Quote the filename for the purpose of inserting it into the command
 * line.  If quote_percent is 1, replace "%" with "%%" - the percent is
 * processed by the mc command line.
 */
char *
name_quote (const char *s, int quote_percent)
{
    char *ret, *d;

    d = ret = g_malloc (strlen (s) * 2 + 2 + 1);
    if (*s == '-') {
	*d++ = '.';
	*d++ = '/';
    }

    for (; *s; s++, d++) {
	switch (*s) {
	case '%':
	    if (quote_percent)
		*d++ = '%';
	    break;
	case '\'':
	case '\\':
	case '\r':
	case '\n':
	case '\t':
	case '"':
	case ';':
	case ' ':
	case '?':
	case '|':
	case '[':
	case ']':
	case '{':
	case '}':
	case '<':
	case '>':
	case '`':
	case '!':
	case '$':
	case '&':
	case '*':
	case '(':
	case ')':
	    *d++ = '\\';
	    break;
	case '~':
	case '#':
	    if (d == ret)
		*d++ = '\\';
	    break;
	}
	*d = *s;
    }
    *d = '\0';
    return ret;
}

char *
fake_name_quote (const char *s, int quote_percent)
{
    return g_strdup (s);
}

/*
 * Remove the middle part of the string to fit given length.
 * Use "~" to show where the string was truncated.
 * Return static buffer, no need to free() it.
 */
char *
name_trunc (const char *txt, int trunc_len)
{
    static char x[MC_MAXPATHLEN + MC_MAXPATHLEN];
    int txt_len;
    char *p;

    if (trunc_len > sizeof (x) - 1) {
	trunc_len = sizeof (x) - 1;
    }
    txt_len = strlen (txt);
    if (txt_len <= trunc_len) {
	strcpy (x, txt);
    } else {
	int y = (trunc_len / 2) + (trunc_len % 2);
	strncpy (x, txt, y);
	strncpy (x + y, txt + txt_len - (trunc_len / 2), trunc_len / 2);
	x[y] = '~';
    }
    x[trunc_len] = 0;
    for (p = x; *p; p++)
	if (!is_printable (*p))
	    *p = '?';
    return x;
}

char *size_trunc (double size)
{
    static char x [BUF_TINY];
    long int divisor = 1;
    char *xtra = "";
    
    if (size > 999999999L){
	divisor = 1024;
	xtra = "K";
	if (size/divisor > 999999999L){
	    divisor = 1024*1024;
	    xtra = "M";
	}
    }
    g_snprintf (x, sizeof (x), "%.0f%s", (size/divisor), xtra);
    return x;
}

char *size_trunc_sep (double size)
{
    static char x [60];
    int  count;
    char *p, *d, *y;

    p = y = size_trunc (size);
    p += strlen (p) - 1;
    d = x + sizeof (x) - 1;
    *d-- = 0;
    while (p >= y && isalpha ((unsigned char) *p))
	*d-- = *p--;
    for (count = 0; p >= y; count++){
	if (count == 3){
	    *d-- = ',';
	    count = 0;
	}
	*d-- = *p--;
    }
    d++;
    if (*d == ',')
	d++;
    return d;
}

/*
 * Print file SIZE to BUFFER, but don't exceed LEN characters,
 * not including trailing 0. BUFFER should be at least LEN+1 long.
 * This function is called for every file on panels, so avoid
 * floating point by any means.
 *
 * Units: size units (filesystem sizes are 1K blocks)
 *    0=bytes, 1=Kbytes, 2=Mbytes, etc.
 */
void
size_trunc_len (char *buffer, int len, off_t size, int units)
{
    /* Avoid taking power for every file.  */
    static const off_t power10 [] =
	{1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000,
	 1000000000};
    static const char * const suffix [] =
	{"", "K", "M", "G", "T", "P", "E", "Z", "Y", NULL};
    int j = 0;

    /* Don't print more than 9 digits - use suffix.  */
    if (len == 0 || len > 9)
	len = 9;

    for (j = units; suffix [j] != NULL; j++) {
	if (size == 0) {
	    if (j == units) {
		/* Empty files will print "0" even with minimal width.  */
		g_snprintf (buffer, len + 1, "0");
		break;
	    }

	    /* Use "~K" or just "K" if len is 1.  Use "B" for bytes.  */
	    g_snprintf (buffer, len + 1, (len > 1) ? "~%s" : "%s",
			(j > 1) ? suffix[j - 1] : "B");
	    break;
	}

	if (size < power10 [len - (j > 0)]) {
	    g_snprintf (buffer, len + 1, "%lu%s", (unsigned long) size, suffix[j]);
	    break;
	}

	/* Powers of 1024, with rounding.  */
	size = (size + 512) >> 10;
    }
}

int is_exe (mode_t mode)
{
    if ((S_IXUSR & mode) || (S_IXGRP & mode) || (S_IXOTH & mode))
	return 1;
    return 0;
}

#define ismode(n,m) ((n & m) == m)

char *
string_perm (mode_t mode_bits)
{
    static char mode[11];

    strcpy (mode, "----------");
    if (S_ISDIR (mode_bits))
	mode[0] = 'd';
    if (S_ISCHR (mode_bits))
	mode[0] = 'c';
    if (S_ISBLK (mode_bits))
	mode[0] = 'b';
    if (S_ISLNK (mode_bits))
	mode[0] = 'l';
    if (S_ISFIFO (mode_bits))
	mode[0] = 'p';
    if (S_ISSOCK (mode_bits))
	mode[0] = 's';
    if (S_ISDOOR (mode_bits))
	mode[0] = 'D';
    if (ismode (mode_bits, S_IXOTH))
	mode[9] = 'x';
    if (ismode (mode_bits, S_IWOTH))
	mode[8] = 'w';
    if (ismode (mode_bits, S_IROTH))
	mode[7] = 'r';
    if (ismode (mode_bits, S_IXGRP))
	mode[6] = 'x';
    if (ismode (mode_bits, S_IWGRP))
	mode[5] = 'w';
    if (ismode (mode_bits, S_IRGRP))
	mode[4] = 'r';
    if (ismode (mode_bits, S_IXUSR))
	mode[3] = 'x';
    if (ismode (mode_bits, S_IWUSR))
	mode[2] = 'w';
    if (ismode (mode_bits, S_IRUSR))
	mode[1] = 'r';
#ifdef S_ISUID
    if (ismode (mode_bits, S_ISUID))
	mode[3] = (mode[3] == 'x') ? 's' : 'S';
#endif				/* S_ISUID */
#ifdef S_ISGID
    if (ismode (mode_bits, S_ISGID))
	mode[6] = (mode[6] == 'x') ? 's' : 'S';
#endif				/* S_ISGID */
#ifdef S_ISVTX
    if (ismode (mode_bits, S_ISVTX))
	mode[9] = (mode[9] == 'x') ? 't' : 'T';
#endif				/* S_ISVTX */
    return mode;
}

/* p: string which might contain an url with a password (this parameter is
      modified in place).
   has_prefix = 0: The first parameter is an url without a prefix
                   (user[:pass]@]machine[:port][remote-dir). Delete
                   the password.
   has_prefix = 1: Search p for known url prefixes. If found delete
                   the password from the url. 
                   Cavevat: only the first url is found
*/ 
char *
strip_password (char *p, int has_prefix)
{
    static const struct {
	char *name;
        size_t len;
    } prefixes[] = { {"/#ftp:", 6},
		     {"/#mc:", 5},
		     {"ftp://", 6},
		     {"/#smb:", 6},
    };
    char *at, *inner_colon, *dir;
    int i;
    char *result = p;
    
    for (i = 0; i < sizeof (prefixes)/sizeof (prefixes[0]); i++) {
	char *q;

	if (has_prefix) {
	    if((q = strstr (p, prefixes[i].name)) == 0)
	       continue;
            else
	        p = q + prefixes[i].len;
       	};

        if ((dir = strchr (p, PATH_SEP)) != NULL)
   	    *dir = '\0';
        /* search for any possible user */
        at = strchr (p, '@');

        /* We have a username */
        if (at) {
            *at = 0;
            inner_colon = strchr (p, ':');
  	    *at = '@';
            if (inner_colon)
                strcpy (inner_colon, at);
        }
        if (dir)
	    *dir = PATH_SEP;
	break;
    }
    return (result);
}

char *strip_home_and_password(const char *dir)
{
    size_t len;
    static char newdir [MC_MAXPATHLEN];

    if (home_dir && !strncmp (dir, home_dir, len = strlen (home_dir)) && 
	(dir[len] == PATH_SEP || dir[len] == '\0')){
	newdir [0] = '~';
	strcpy (&newdir [1], &dir [len]);
	return newdir;
    } 

    /* We do not strip homes in /#ftp tree, I do not like ~'s there 
       (see ftpfs.c why) */
    strcpy (newdir, dir);
    strip_password (newdir, 1);
    return newdir;
}

static char *maybe_start_group (char *d, int do_group, int *was_wildcard)
{
    if (!do_group)
	return d;
    if (*was_wildcard)
	return d;
    *was_wildcard = 1;
    *d++ = '\\';
    *d++ = '(';
    return d;
}

static char *maybe_end_group (char *d, int do_group, int *was_wildcard)
{
    if (!do_group)
	return d;
    if (!*was_wildcard)
	return d;
    *was_wildcard = 0;
    *d++ = '\\';
    *d++ = ')';
    return d;
}

/* If shell patterns are on converts a shell pattern to a regular
   expression. Called by regexp_match and mask_rename. */
/* Shouldn't we support [a-fw] type wildcards as well ?? */
char *convert_pattern (char *pattern, int match_type, int do_group)
{
    char *s, *d;
    char *new_pattern;
    int was_wildcard = 0;

    if ((match_type != match_regex) && easy_patterns){
	new_pattern = g_malloc (MC_MAXPATHLEN);
	d = new_pattern;
	if (match_type == match_file)
	    *d++ = '^';
	for (s = pattern; *s; s++, d++){
	    switch (*s){
	    case '*':
		d = maybe_start_group (d, do_group, &was_wildcard);
		*d++ = '.';
		*d   = '*';
		break;
		
	    case '?':
		d = maybe_start_group (d, do_group, &was_wildcard);
		*d = '.';
		break;
		
	    case '.':
		d = maybe_end_group (d, do_group, &was_wildcard);
		*d++ = '\\';
		*d   = '.';
		break;

	    default:
		d = maybe_end_group (d, do_group, &was_wildcard);
		*d = *s;
		break;
	    }
	}
	d = maybe_end_group (d, do_group, &was_wildcard);
	if (match_type == match_file)
	    *d++ = '$';
	*d = 0;
	return new_pattern;
    } else
	return  g_strdup (pattern);
}

int regexp_match (char *pattern, char *string, int match_type)
{
    static regex_t r;
    static char *old_pattern = NULL;
    static int old_type;
    int    rval;

    if (!old_pattern || STRCOMP (old_pattern, pattern) || old_type != match_type){
	if (old_pattern){
	    regfree (&r);
	    g_free (old_pattern);
	    old_pattern = NULL;
	}
	pattern = convert_pattern (pattern, match_type, 0);
	if (regcomp (&r, pattern, REG_EXTENDED|REG_NOSUB|MC_ARCH_FLAGS)) {
	    g_free (pattern);
	    return -1;
	}
	old_pattern = pattern;
	old_type = match_type;
    }
    rval = !regexec (&r, string, 0, NULL, 0);
    return rval;
}

char *extension (char *filename)
{
    char *d;

    if (!(*filename))
	return "";
    
    d = filename + strlen (filename) - 1;
    for (;d >= filename; d--){
	if (*d == '.')
	    return d+1;
    }
    return "";
}

int get_int (char *file, char *key, int def)
{
    return GetPrivateProfileInt (app_text, key, def, file);
}

int set_int (char *file, char *key, int value)
{
    char buffer [BUF_TINY];

    g_snprintf (buffer, sizeof (buffer), "%d", value);
    return WritePrivateProfileString (app_text, key, buffer, file);
}

int exist_file (char *name)
{
    return access (name, R_OK) == 0;
}

char *load_file (char *filename)
{
    FILE *data_file;
    struct stat s;
    char *data;
    long read_size;
    
    if ((data_file = fopen (filename, "r")) == NULL){
	return 0;
    }
    if (fstat (fileno (data_file), &s) != 0){
	fclose (data_file);
	return 0;
    }
    data = (char *) g_malloc (s.st_size+1);
    read_size = fread (data, 1, s.st_size, data_file);
    data [read_size] = 0;
    fclose (data_file);

    if (read_size > 0)
	return data;
    else {
	 g_free (data);
	return 0;
    }
}

char *
load_mc_home_file (const char *filename, char **allocated_filename)
{
    char *hintfile_base, *hintfile;
    char *lang;
    char *data;

    hintfile_base = concat_dir_and_file (mc_home, filename);
    lang = guess_message_value ();

    hintfile = g_strconcat (hintfile_base, ".", lang, NULL);
    data = load_file (hintfile);

    if (!data) {
	g_free (hintfile);
	/* Fall back to the two-letter language code */
	if (lang[0] && lang[1])
	    lang[2] = 0;
	hintfile = g_strconcat (hintfile_base, ".", lang, NULL);
	data = load_file (hintfile);

	if (!data) {
	    g_free (hintfile);
	    hintfile = hintfile_base;
	    data = load_file (hintfile_base);
	}
    }

    g_free (lang);

    if (hintfile != hintfile_base)
	g_free (hintfile_base);

    if (allocated_filename)
	*allocated_filename = hintfile;
    else
	g_free (hintfile);

    return data;
}

/* Check strftime() results. Some systems (i.e. Solaris) have different
short-month-name sizes for different locales */ 
size_t i18n_checktimelength (void)
{
    size_t length, a, b;
    char buf [MAX_I18NTIMELENGTH + 1];
    time_t testtime = time (NULL);
    
    a = strftime (buf, sizeof(buf)-1, _("%b %e %H:%M"), localtime(&testtime));
    b = strftime (buf, sizeof(buf)-1, _("%b %e  %Y"), localtime(&testtime));
    
    length = max (a, b);
    
    /* Don't handle big differences. Use standard value (email bug, please) */
    if ( length > MAX_I18NTIMELENGTH || length < MIN_I18NTIMELENGTH )
	length = STD_I18NTIMELENGTH;
    
    return length;
}

char *file_date (time_t when)
{
    static char timebuf [MAX_I18NTIMELENGTH + 1];
    time_t current_time = time ((time_t) 0);
    static size_t i18n_timelength = 0;
    static char *fmtyear, *fmttime;
    char *fmt;

    if (i18n_timelength == 0){
	i18n_timelength = i18n_checktimelength() + 1;
	
	/* strftime() format string for old dates */
	fmtyear = _("%b %e  %Y");
	/* strftime() format string for recent dates */
	fmttime = _("%b %e %H:%M");
    }

    if (current_time > when + 6L * 30L * 24L * 60L * 60L /* Old. */
	|| current_time < when - 60L * 60L) /* In the future. */
	/* The file is fairly old or in the future.
	   POSIX says the cutoff is 6 months old;
	   approximate this by 6*30 days.
	   Allow a 1 hour slop factor for what is considered "the future",
	   to allow for NFS server/client clock disagreement.
	   Show the year instead of the time of day.  */

	fmt = fmtyear;
    else
	fmt = fmttime;
    
    strftime (timebuf, i18n_timelength, fmt, localtime(&when));
    return timebuf;
}

char *extract_line (char *s, char *top)
{
    static char tmp_line [BUF_MEDIUM];
    char *t = tmp_line;
    
    while (*s && *s != '\n' && (t - tmp_line) < sizeof (tmp_line)-1 && s < top)
	*t++ = *s++;
    *t = 0;
    return tmp_line;
}

/* FIXME: I should write a faster version of this (Aho-Corasick stuff) */
char * _icase_search (char *text, char *data, int *lng)
{
    char *d = text;
    char *e = data;
    int dlng = 0;

    if (lng)
	*lng = 0;
    for (;*e; e++) {
	while (*(e+1) == '\b' && *(e+2)) {
	    e += 2;
	    dlng += 2;
	}
	if (toupper((unsigned char) *d) == toupper((unsigned char) *e))
	    d++;
	else {
	    e -= d - text;
	    d = text;
	    dlng = 0;
	}
	if (!*d) {
	    if (lng)
		*lng = strlen (text) + dlng;
	    return e+1;
	}
    }
    return 0;
}

/* The basename routine */
char *x_basename (char *s)
{
    char  *where;
    return ((where = strrchr (s, PATH_SEP))) ? where + 1 : s;
}


char *unix_error_string (int error_num)
{
    static char buffer [BUF_LARGE];
#if GLIB_MAJOR_VERSION >= 2
    gchar *strerror_currentlocale;
	
    strerror_currentlocale = g_locale_from_utf8(g_strerror (error_num), -1, NULL, NULL, NULL);
    g_snprintf (buffer, sizeof (buffer), "%s (%d)",
		strerror_currentlocale, error_num);
    g_free(strerror_currentlocale);
#else
    g_snprintf (buffer, sizeof (buffer), "%s (%d)",
		g_strerror (error_num), error_num);
#endif
    return buffer;
}

char *skip_separators (char *s)
{
    for (;*s; s++)
	if (*s != ' ' && *s != '\t' && *s != ',')
	    break;
    return s;
}

char *skip_numbers (char *s)
{
    for (;*s; s++)
	if (!isdigit ((unsigned int) *s))
	    break;
    return s;
}

/* Remove all control sequences from the argument string.  We define
 * "control sequence", in a sort of pidgin BNF, as follows:
 *
 * control-seq = Esc non-'['
 *	       | Esc '[' (0 or more digits or ';' or '?') (any other char)
 *
 * This scheme works for all the terminals described in my termcap /
 * terminfo databases, except the Hewlett-Packard 70092 and some Wyse
 * terminals.  If I hear from a single person who uses such a terminal
 * with MC, I'll be glad to add support for it.  (Dugan)
 * Non-printable characters are also removed.
 */

char *strip_ctrl_codes (char *s)
{
    char *w; /* Current position where the stripped data is written */
    char *r; /* Current position where the original data is read */

    if (!s)
	return 0;

    for (w = s, r = s; *r; ) {
	if (*r == ESC_CHAR) {
	    /* Skip the control sequence's arguments */ ;
	    if (*(++r) == '[') {
		/* strchr() matches trailing binary 0 */
		while (*(++r) && strchr ("0123456789;?", *r));
	    }

	    /*
	     * Now we are at the last character of the sequence.
	     * Skip it unless it's binary 0.
	     */
	    if (*r)
		r++;
	    continue;
	}

	if (is_printable(*r))
	    *w++ = *r;
	++r;
    }
    *w = 0;
    return s;
}


#ifndef USE_VFS
char *get_current_wd (char *buffer, int size)
{
    char *p;
    int len;

    p = g_get_current_dir ();
    len = strlen(p) + 1;

    if (len > size) {
	g_free (p);
	return NULL;
    }

    strncpy (buffer, p, len);
    g_free (p);

    return buffer;
}
#endif /* !USE_VFS */

/* This function returns 0 if the file is not in not compressed by
 * one of the supported compressors (gzip, bzip, bzip2).  Otherwise,
 * the compression type is returned, as defined in util.h
 * Warning: this function moves the current file pointer */
int get_compression_type (int fd)
{
    unsigned char magic[4];

    /* Read the magic signature */
    if (mc_read (fd, (char *) magic, 4) != 4)
	return COMPRESSION_NONE;

    /* GZIP_MAGIC and OLD_GZIP_MAGIC */
    if (magic[0] == 037 && (magic[1] == 0213 || magic[1] == 0236)) {
	return COMPRESSION_GZIP;
    }

    /* PKZIP_MAGIC */
    if (magic[0] == 0120 && magic[1] == 0113 && magic[2] == 003
	&& magic[3] == 004) {
	/* Read compression type */
	mc_lseek (fd, 8, SEEK_SET);
	if (mc_read (fd, (char *) magic, 2) != 2)
	    return COMPRESSION_NONE;

	/* Gzip can handle only deflated (8) or stored (0) files */
	if ((magic[0] != 8 && magic[0] != 0) || magic[1] != 0)
	    return COMPRESSION_NONE;

	/* Compatible with gzip */
	return COMPRESSION_GZIP;
    }

    /* PACK_MAGIC and LZH_MAGIC and compress magic */
    if (magic[0] == 037
	&& (magic[1] == 036 || magic[1] == 0240 || magic[1] == 0235)) {
	/* Compatible with gzip */
	return COMPRESSION_GZIP;
    }

    /* BZIP and BZIP2 files */
    if ((magic[0] == 'B') && (magic[1] == 'Z') &&
	(magic[3] >= '1') && (magic[3] <= '9')) {
	switch (magic[2]) {
	case '0':
	    return COMPRESSION_BZIP;
	case 'h':
	    return COMPRESSION_BZIP2;
	}
    }
    return 0;
}

const char *
decompress_extension (int type)
{
	switch (type){
	case COMPRESSION_GZIP: return "#ugz";
	case COMPRESSION_BZIP:   return "#ubz";
	case COMPRESSION_BZIP2:  return "#ubz2";
	}
	/* Should never reach this place */
	fprintf (stderr, "Fatal: decompress_extension called with an unknown argument\n");
	return 0;
}

/* Hooks */
void add_hook (Hook **hook_list, void (*hook_fn)(void *), void *data)
{
    Hook *new_hook = g_new (Hook, 1);

    new_hook->hook_fn = hook_fn;
    new_hook->next    = *hook_list;
    new_hook->hook_data = data;
      
    *hook_list = new_hook;
}

void execute_hooks (Hook *hook_list)
{
    Hook *new_hook = 0;
    Hook *p;

    /* We copy the hook list first so tahat we let the hook
     * function call delete_hook
     */
    
    while (hook_list){
	add_hook (&new_hook, hook_list->hook_fn, hook_list->hook_data);
	hook_list = hook_list->next;
    }
    p = new_hook;
    
    while (new_hook){
	(*new_hook->hook_fn)(new_hook->hook_data);
	new_hook = new_hook->next;
    }
    
    for (hook_list = p; hook_list;){
	p = hook_list;
	hook_list = hook_list->next;
	 g_free (p);
    }
}

void delete_hook (Hook **hook_list, void (*hook_fn)(void *))
{
    Hook *current, *new_list, *next;

    new_list = 0;
    
    for (current = *hook_list; current; current = next){
	next = current->next;
	if (current->hook_fn == hook_fn)
	    g_free (current);
	else
	    add_hook (&new_list, current->hook_fn, current->hook_data);
    }
    *hook_list = new_list;
}

int hook_present (Hook *hook_list, void (*hook_fn)(void *))
{
    Hook *p;
    
    for (p = hook_list; p; p = p->next)
	if (p->hook_fn == hook_fn)
	    return 1;
    return 0;
}

void wipe_password (char *passwd)
{
    char *p = passwd;
    
    if (!p)
	return;
    for (;*p ; p++)
        *p = 0;
    g_free (passwd);
}

/* Convert "\E" -> esc character and ^x to control-x key and ^^ to ^ key */
/* Returns a newly allocated string */
char *convert_controls (char *s)
{
    char *valcopy = g_strdup (s);
    char *p, *q;

    /* Parse the escape special character */
    for (p = s, q = valcopy; *p;){
	if (*p == '\\'){
	    p++;
	    if ((*p == 'e') || (*p == 'E')){
		p++;
		*q++ = ESC_CHAR;
	    }
	} else {
	    if (*p == '^'){
		p++;
		if (*p == '^')
		    *q++ = *p++;
		else {
		    *p = (*p | 0x20);
		    if (*p >= 'a' && *p <= 'z') {
		        *q++ = *p++ - 'a' + 1;
		    } else
		        p++;
		}
	    } else
		*q++ = *p++;
	}
    }
    *q = 0;
    return valcopy;
}

static char *resolve_symlinks (char *path)
{
    char *buf, *buf2, *p, *q, *r, c;
    int len;
    struct stat mybuf;
    
    if (*path != PATH_SEP)
        return NULL;
    r = buf = g_malloc (MC_MAXPATHLEN);
    buf2 = g_malloc (MC_MAXPATHLEN); 
    *r++ = PATH_SEP;
    *r = 0;
    p = path;
    for (;;) {
	q = strchr (p + 1, PATH_SEP);
	if (!q) {
	    q = strchr (p + 1, 0);
	    if (q == p + 1)
	        break;
	}
	c = *q;
	*q = 0;
	if (mc_lstat (path, &mybuf) < 0) {
	    g_free (buf);
	    g_free (buf2);
	    *q = c;
	    return NULL;
	}
	if (!S_ISLNK (mybuf.st_mode))
	    strcpy (r, p + 1);
	else {
	    len = mc_readlink (path, buf2, MC_MAXPATHLEN - 1);
	    if (len < 0) {
		 g_free (buf);
		 g_free (buf2);
		*q = c;
		return NULL;
	    }
	    buf2 [len] = 0;
	    if (*buf2 == PATH_SEP)
		strcpy (buf, buf2);
	    else
		strcpy (r, buf2);
	}
	canonicalize_pathname (buf);
	r = strchr (buf, 0);
	if (!*r || *(r - 1) != PATH_SEP) {
	    *r++ = PATH_SEP;
	    *r = 0;
	}
	*q = c;
	p = q;
	if (!c)
	    break;
    }
    if (!*buf)
	strcpy (buf, PATH_SEP_STR);
    else if (*(r - 1) == PATH_SEP && r != buf + 1)
	*(r - 1) = 0;
    g_free (buf2);
    return buf;
}

/* Finds out a relative path from first to second, i.e. goes as many ..
 * as needed up in first and then goes down using second */
char *diff_two_paths (char *first, char *second) 
{
    char *p, *q, *r, *s, *buf = 0;
    int i, j, prevlen = -1, currlen;
    
    first = resolve_symlinks (first);
    if (first == NULL)
        return NULL;
    for (j = 0; j < 2; j++) {
	p = first;
	if (j) {
	    second = resolve_symlinks (second);
	    if (second == NULL) {
		 g_free (first);
	        return buf;
	    }
	}
	q = second;
	for (;;) {
	    r = strchr (p, PATH_SEP);
	    s = strchr (q, PATH_SEP);
	    if (!r || !s)
	      break;
	    *r = 0; *s = 0;
	    if (strcmp (p, q)) {
		*r = PATH_SEP; *s = PATH_SEP;
		break;
	    } else {
		*r = PATH_SEP; *s = PATH_SEP;
	    }
	    p = r + 1;
	    q = s + 1;
	}
	p--;
	for (i = 0; (p = strchr (p + 1, PATH_SEP)) != NULL; i++);
	currlen = (i + 1) * 3 + strlen (q) + 1;
	if (j) {
	    if (currlen < prevlen)
	        g_free (buf);
	    else {
		 g_free (first);
		 g_free (second);
		return buf;
	    }
	}
	p = buf = g_malloc (currlen);
	prevlen = currlen;
	for (; i >= 0; i--, p += 3)
	  strcpy (p, "../");
	strcpy (p, q);
    }
    g_free (first);
    g_free (second);
    return buf;
}

/* If filename is NULL, then we just append PATH_SEP to the dir */
char *
concat_dir_and_file (const char *dir, const char *file)
{
    int i = strlen (dir);
    
    if (dir [i-1] == PATH_SEP)
	return  g_strconcat (dir, file, NULL);
    else
	return  g_strconcat (dir, PATH_SEP_STR, file, NULL);
}


/* Append text to GList, remove all entries with the same text */
GList *
list_append_unique (GList *list, char *text)
{
    GList *link, *newlink;

    /*
     * Go to the last position and traverse the list backwards
     * starting from the second last entry to make sure that we
     * are not removing the current link.
     */
    list = g_list_append (list, text);
    list = g_list_last (list);
    link = g_list_previous (list);

    while (link) {
	newlink = g_list_previous (link);
	if (!strcmp ((char *) link->data, text)) {
	    g_free (link->data);
	    g_list_remove_link (list, link);
	    g_list_free_1 (link);
	}
	link = newlink;
    }

    return list;
}


/* Following code heavily borrows from libiberty, mkstemps.c */

/* Number of attempts to create a temporary file */
#ifndef TMP_MAX
#define TMP_MAX 16384
#endif /* !TMP_MAX */

/*
 * Arguments:
 * pname (output) - pointer to the name of the temp file (needs g_free).
 *                  NULL if the function fails.
 * prefix - part of the filename before the random part.
 *          Prepend $TMPDIR or /tmp if there are no path separators.
 * suffix - if not NULL, part of the filename after the random part.
 *
 * Result:
 * handle of the open file or -1 if couldn't open any.
 */
int
mc_mkstemps (char **pname, const char *prefix, const char *suffix)
{
    static const char letters[]
	= "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static unsigned long value;
    struct timeval tv;
    char *tmpbase;
    char *tmpname;
    char *XXXXXX;
    int count;

    if (strchr (prefix, PATH_SEP) == NULL) {
	/* Add prefix first to find the position of XXXXXX */
	tmpbase = concat_dir_and_file (mc_tmpdir (), prefix);
    } else {
	tmpbase = g_strdup (prefix);
    }

    tmpname = g_strconcat (tmpbase, "XXXXXX", suffix, NULL);
    *pname = tmpname;
    XXXXXX = &tmpname[strlen (tmpbase)];
    g_free (tmpbase);

    /* Get some more or less random data.  */
    gettimeofday (&tv, NULL);
    value += (tv.tv_usec << 16) ^ tv.tv_sec ^ getpid ();

    for (count = 0; count < TMP_MAX; ++count) {
	unsigned long v = value;
	int fd;

	/* Fill in the random bits.  */
	XXXXXX[0] = letters[v % 62];
	v /= 62;
	XXXXXX[1] = letters[v % 62];
	v /= 62;
	XXXXXX[2] = letters[v % 62];
	v /= 62;
	XXXXXX[3] = letters[v % 62];
	v /= 62;
	XXXXXX[4] = letters[v % 62];
	v /= 62;
	XXXXXX[5] = letters[v % 62];

	fd = open (tmpname, O_RDWR | O_CREAT | O_TRUNC | O_EXCL,
		   S_IRUSR | S_IWUSR);
	if (fd >= 0) {
	    /* Successfully created.  */
	    return fd;
	}

	/* This is a random value.  It is only necessary that the next
	   TMP_MAX values generated by adding 7777 to VALUE are different
	   with (module 2^32).  */
	value += 7777;
    }

    /* Unsuccessful. Free the filename. */
    g_free (tmpname);
    *pname = NULL;

    return -1;
}


/*
 * Read and restore position for the given filename.
 * If there is no stored data, return line 1 and col 0.
 */
void
load_file_position (char *filename, long *line, long *column)
{
    char *fn;
    FILE *f;
    char buf[MC_MAXPATHLEN + 20];
    int len;

    /* defaults */
    *line = 1;
    *column = 0;

    /* open file with positions */
    fn = concat_dir_and_file (home_dir, MC_FILEPOS);
    f = fopen (fn, "r");
    g_free (fn);
    if (!f)
	return;

    len = strlen (filename);

    while (fgets (buf, sizeof (buf), f)) {
	char *p;

	/* check if the filename matches the beginning of string */
	if (strncmp (buf, filename, len) != 0)
	    continue;

	/* followed by single space */
	if (buf[len] != ' ')
	    continue;

	/* and string without spaces */
	p = &buf[len + 1];
	if (strchr (p, ' '))
	    continue;

	*line = atol (p);
	p = strchr (buf, ';');
	*column = atol (&p[1]);
    }
    fclose (f);
}

/* Save position for the given file */
void
save_file_position (char *filename, long line, long column)
{
    char *tmp, *fn;
    FILE *f, *t;
    char buf[MC_MAXPATHLEN + 20];
    int i = 1;
    int len;

    len = strlen (filename);

    tmp = concat_dir_and_file (home_dir, MC_FILEPOS_TMP);
    fn = concat_dir_and_file (home_dir, MC_FILEPOS);

    /* open temporary file */
    t = fopen (tmp, "w");
    if (!t) {
	g_free (tmp);
	g_free (fn);
	return;
    }

    /* put the new record */
    fprintf (t, "%s %ld;%ld\n", filename, line, column);

    /* copy records from the old file */
    f = fopen (fn, "r");
    if (f) {
	while (fgets (buf, sizeof (buf), f)) {
	    /* Skip entries for the current filename */
	    if (strncmp (buf, filename, len) == 0 && buf[len] == ' '
		&& !strchr (&buf[len + 1], ' '))
		continue;

	    fprintf (t, "%s", buf);
	    if (++i > MC_FILEPOS_ENTRIES)
		break;
	}
	fclose (f);
    }

    fclose (t);
    rename (tmp, fn);
    g_free (tmp);
    g_free (fn);
}
