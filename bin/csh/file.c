/*-
 * Copyright (c) 1980, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static char sccsid[] = "@(#)file.c	8.4 (Berkeley) 5/6/95";
#endif /* not lint */

#ifdef FILEC

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <dirent.h>
#include <pwd.h>
#include <stdlib.h>
#include <unistd.h>
#ifndef SHORT_STRINGS
#include <string.h>
#endif /* SHORT_STRINGS */
#if __STDC__
# include <stdarg.h>
#else
# include <varargs.h>
#endif

#include "csh.h"
#include "extern.h"

/*
 * Tenex style file name recognition, .. and more.
 * History:
 *	Author: Ken Greer, Sept. 1975, CMU.
 *	Finally got around to adding to the Cshell., Ken Greer, Dec. 1981.
 */

#define ON	1
#define OFF	0
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define ESC	'\033'

typedef enum {
    LIST, RECOGNIZE
}       COMMAND;

static void	 setup_tty __P((int));
static void	 back_to_col_1 __P((void));
static void	 pushback __P((Char *));
static void	 catn __P((Char *, Char *, int));
static void	 copyn __P((Char *, Char *, int));
static Char	 filetype __P((Char *, Char *));
static void	 print_by_column __P((Char *, Char *[], int));
static Char	*tilde __P((Char *, Char *));
static void	 retype __P((void));
static void	 beep __P((void));
static void	 print_recognized_stuff __P((Char *));
static void	 extract_dir_and_name __P((Char *, Char *, Char *));
static Char	*getentry __P((DIR *, int));
static void	 free_items __P((Char **));
static int	 tsearch __P((Char *, COMMAND, int));
static int	 recognize __P((Char *, Char *, int, int));
static int	 is_prefix __P((Char *, Char *));
static int	 is_suffix __P((Char *, Char *));
static int	 ignored __P((Char *));

/*
 * Put this here so the binary can be patched with adb to enable file
 * completion by default.  Filec controls completion, nobeep controls
 * ringing the terminal bell on incomplete expansions.
 */
bool    filec = 0;

static void
setup_tty(on)
    int     on;
{
    static struct termios tchars;

    (void) tcgetattr(SHIN, &tchars);

    if (on) {
	tchars.c_cc[VEOL] = ESC;
	if (tchars.c_lflag & ICANON)
	    on = TCSANOW;
	else {
	    on = TCSAFLUSH;
	    tchars.c_lflag |= ICANON;
	}
    }
    else {
	tchars.c_cc[VEOL] = _POSIX_VDISABLE;
	on = TCSANOW;
    }

    (void) tcsetattr(SHIN, TCSANOW, &tchars);
}

/*
 * Move back to beginning of current line
 */
static void
back_to_col_1()
{
    struct termios tty, tty_normal;
    sigset_t sigset, osigset;

    sigemptyset(&sigset);
    sigaddset(&sigset, SIGINT);
    sigprocmask(SIG_BLOCK, &sigset, &osigset);
    (void) tcgetattr(SHOUT, &tty);
    tty_normal = tty;
    tty.c_iflag &= ~INLCR;
    tty.c_oflag &= ~ONLCR;
    (void) tcsetattr(SHOUT, TCSANOW, &tty);
    (void) write(SHOUT, "\r", 1);
    (void) tcsetattr(SHOUT, TCSANOW, &tty_normal);
    sigprocmask(SIG_SETMASK, &osigset, NULL);
}

/*
 * Push string contents back into tty queue
 */
static void
pushback(string)
    Char   *string;
{
    register Char *p;
    struct termios tty, tty_normal;
    sigset_t sigset, osigset;
    char    c;

    sigemptyset(&sigset);
    sigaddset(&sigset, SIGINT);
    sigprocmask(SIG_BLOCK, &sigset, &osigset);
    (void) tcgetattr(SHOUT, &tty);
    tty_normal = tty;
    tty.c_lflag &= ~(ECHOKE | ECHO | ECHOE | ECHOK | ECHONL | ECHOPRT | ECHOCTL);
    (void) tcsetattr(SHOUT, TCSANOW, &tty);

#ifndef __OpenBSD__
    for (p = string; (c = *p) != '\0'; p++)
	(void) ioctl(SHOUT, TIOCSTI, (ioctl_t) & c);
#endif
    (void) tcsetattr(SHOUT, TCSANOW, &tty_normal);
    sigprocmask(SIG_SETMASK, &osigset, NULL);
}

/*
 * Concatenate src onto tail of des.
 * Des is a string whose maximum length is count.
 * Always null terminate.
 */
static void
catn(des, src, count)
    register Char *des, *src;
    register int count;
{
    while (--count >= 0 && *des)
	des++;
    while (--count >= 0)
	if ((*des++ = *src++) == 0)
	    return;
    *des = '\0';
}

/*
 * Like strncpy but always leave room for trailing \0
 * and always null terminate.
 */
static void
copyn(des, src, count)
    register Char *des, *src;
    register int count;
{
    while (--count >= 0)
	if ((*des++ = *src++) == 0)
	    return;
    *des = '\0';
}

static  Char
filetype(dir, file)
    Char   *dir, *file;
{
    Char    path[MAXPATHLEN];
    struct stat statb;

    catn(Strcpy(path, dir), file, sizeof(path) / sizeof(Char));
    if (lstat(short2str(path), &statb) == 0) {
	switch (statb.st_mode & S_IFMT) {
	case S_IFDIR:
	    return ('/');

	case S_IFLNK:
	    if (stat(short2str(path), &statb) == 0 &&	/* follow it out */
		S_ISDIR(statb.st_mode))
		return ('>');
	    else
		return ('@');

	case S_IFSOCK:
	    return ('=');

	default:
	    if (statb.st_mode & 0111)
		return ('*');
	}
    }
    return (' ');
}

static struct winsize win;

/*
 * Print sorted down columns
 */
static void
print_by_column(dir, items, count)
    Char   *dir, *items[];
    int     count;
{
    register int i, rows, r, c, maxwidth = 0, columns;

    if (ioctl(SHOUT, TIOCGWINSZ, (ioctl_t) & win) < 0 || win.ws_col == 0)
	win.ws_col = 80;
    for (i = 0; i < count; i++)
	maxwidth = maxwidth > (r = Strlen(items[i])) ? maxwidth : r;
    maxwidth += 2;		/* for the file tag and space */
    columns = win.ws_col / maxwidth;
    if (columns == 0)
	columns = 1;
    rows = (count + (columns - 1)) / columns;
    for (r = 0; r < rows; r++) {
	for (c = 0; c < columns; c++) {
	    i = c * rows + r;
	    if (i < count) {
		register int w;

		(void) fprintf(cshout, "%s", vis_str(items[i]));
		(void) fputc(dir ? filetype(dir, items[i]) : ' ', cshout);
		if (c < columns - 1) {	/* last column? */
		    w = Strlen(items[i]) + 1;
		    for (; w < maxwidth; w++)
			(void) fputc(' ', cshout);
		}
	    }
	}
	(void) fputc('\r', cshout);
	(void) fputc('\n', cshout);
    }
}

/*
 * Expand file name with possible tilde usage
 *	~person/mumble
 * expands to
 *	home_directory_of_person/mumble
 */
static Char *
tilde(new, old)
    Char   *new, *old;
{
    register Char *o, *p;
    register struct passwd *pw;
    static Char person[40];

    if (old[0] != '~')
	return (Strcpy(new, old));

    for (p = person, o = &old[1]; *o && *o != '/'; *p++ = *o++)
	continue;
    *p = '\0';
    if (person[0] == '\0')
	(void) Strcpy(new, value(STRhome));
    else {
	pw = getpwnam(short2str(person));
	if (pw == NULL)
	    return (NULL);
	(void) Strcpy(new, str2short(pw->pw_dir));
    }
    (void) Strcat(new, o);
    return (new);
}

/*
 * Cause pending line to be printed
 */
static void
retype()
{
    struct termios tty;

    (void) tcgetattr(SHOUT, &tty);
    tty.c_lflag |= PENDIN;
    (void) tcsetattr(SHOUT, TCSANOW, &tty);
}

static void
beep()
{
    if (adrof(STRnobeep) == 0)
	(void) write(SHOUT, "\007", 1);
}

/*
 * Erase that silly ^[ and
 * print the recognized part of the string
 */
static void
print_recognized_stuff(recognized_part)
    Char   *recognized_part;
{
    /* An optimized erasing of that silly ^[ */
    (void) fputc('\b', cshout);
    (void) fputc('\b', cshout);
    switch (Strlen(recognized_part)) {

    case 0:			/* erase two Characters: ^[ */
	(void) fputc(' ', cshout);
	(void) fputc(' ', cshout);
	(void) fputc('\b', cshout);
	(void) fputc('\b', cshout);
	break;

    case 1:			/* overstrike the ^, erase the [ */
	(void) fprintf(cshout, "%s", vis_str(recognized_part));
	(void) fputc(' ', cshout);
	(void) fputc('\b', cshout);
	break;

    default:			/* overstrike both Characters ^[ */
	(void) fprintf(cshout, "%s", vis_str(recognized_part));
	break;
    }
    (void) fflush(cshout);
}

/*
 * Parse full path in file into 2 parts: directory and file names
 * Should leave final slash (/) at end of dir.
 */
static void
extract_dir_and_name(path, dir, name)
    Char   *path, *dir, *name;
{
    register Char *p;

    p = Strrchr(path, '/');
    if (p == NULL) {
	copyn(name, path, MAXNAMLEN);
	dir[0] = '\0';
    }
    else {
	copyn(name, ++p, MAXNAMLEN);
	copyn(dir, path, p - path);
    }
}

static Char *
getentry(dir_fd, looking_for_lognames)
    DIR    *dir_fd;
    int     looking_for_lognames;
{
    register struct passwd *pw;
    register struct dirent *dirp;

    if (looking_for_lognames) {
	if ((pw = getpwent()) == NULL)
	    return (NULL);
	return (str2short(pw->pw_name));
    }
    if ((dirp = readdir(dir_fd)) != NULL)
	return (str2short(dirp->d_name));
    return (NULL);
}

static void
free_items(items)
    register Char **items;
{
    register int i;

    for (i = 0; items[i]; i++)
	xfree((ptr_t) items[i]);
    xfree((ptr_t) items);
}

#define FREE_ITEMS(items) { \
	sigset_t sigset, osigset;\
\
	sigemptyset(&sigset);\
	sigaddset(&sigset, SIGINT);\
	sigprocmask(SIG_BLOCK, &sigset, &osigset);\
	free_items(items);\
	items = NULL;\
	sigprocmask(SIG_SETMASK, &osigset, NULL);\
}

/*
 * Perform a RECOGNIZE or LIST command on string "word".
 */
static int
tsearch(word, command, max_word_length)
    Char   *word;
    COMMAND command;
    int     max_word_length;
{
    static Char **items = NULL;
    register DIR *dir_fd;
    register int numitems = 0, ignoring = TRUE, nignored = 0;
    register int name_length, looking_for_lognames;
    Char    tilded_dir[MAXPATHLEN + 1], dir[MAXPATHLEN + 1];
    Char    name[MAXNAMLEN + 1], extended_name[MAXNAMLEN + 1];
    Char   *entry;

#define MAXITEMS 1024

    if (items != NULL)
	FREE_ITEMS(items);

    looking_for_lognames = (*word == '~') && (Strchr(word, '/') == NULL);
    if (looking_for_lognames) {
	(void) setpwent();
	copyn(name, &word[1], MAXNAMLEN);	/* name sans ~ */
	dir_fd = NULL;
    }
    else {
	extract_dir_and_name(word, dir, name);
	if (tilde(tilded_dir, dir) == 0)
	    return (0);
	dir_fd = opendir(*tilded_dir ? short2str(tilded_dir) : ".");
	if (dir_fd == NULL)
	    return (0);
    }

again:				/* search for matches */
    name_length = Strlen(name);
    for (numitems = 0; (entry = getentry(dir_fd, looking_for_lognames)) != NULL;) {
	if (!is_prefix(name, entry))
	    continue;
	/* Don't match . files on null prefix match */
	if (name_length == 0 && entry[0] == '.' &&
	    !looking_for_lognames)
	    continue;
	if (command == LIST) {
	    if (numitems >= MAXITEMS) {
		(void) fprintf(csherr, "\nYikes!! Too many %s!!\n",
			       looking_for_lognames ?
			       "names in password file" : "files");
		break;
	    }
	    if (items == NULL)
		items = (Char **) xcalloc(sizeof(items[0]), MAXITEMS);
	    items[numitems] = (Char *) xmalloc((size_t) (Strlen(entry) + 1) *
					       sizeof(Char));
	    copyn(items[numitems], entry, MAXNAMLEN);
	    numitems++;
	}
	else {			/* RECOGNIZE command */
	    if (ignoring && ignored(entry))
		nignored++;
	    else if (recognize(extended_name,
			       entry, name_length, ++numitems))
		break;
	}
    }
    if (ignoring && numitems == 0 && nignored > 0) {
	ignoring = FALSE;
	nignored = 0;
	if (looking_for_lognames)
	    (void) setpwent();
	else
	    rewinddir(dir_fd);
	goto again;
    }

    if (looking_for_lognames)
	(void) endpwent();
    else
	(void) closedir(dir_fd);
    if (numitems == 0)
	return (0);
    if (command == RECOGNIZE) {
	if (looking_for_lognames)
	    copyn(word, STRtilde, 1);
	else
	    /* put back dir part */
	    copyn(word, dir, max_word_length);
	/* add extended name */
	catn(word, extended_name, max_word_length);
	return (numitems);
    }
    else {			/* LIST */
	qsort((ptr_t) items, numitems, sizeof(items[0]), 
		(int (*) __P((const void *, const void *))) sortscmp);
	print_by_column(looking_for_lognames ? NULL : tilded_dir,
			items, numitems);
	if (items != NULL)
	    FREE_ITEMS(items);
    }
    return (0);
}

/*
 * Object: extend what user typed up to an ambiguity.
 * Algorithm:
 * On first match, copy full entry (assume it'll be the only match)
 * On subsequent matches, shorten extended_name to the first
 * Character mismatch between extended_name and entry.
 * If we shorten it back to the prefix length, stop searching.
 */
static int
recognize(extended_name, entry, name_length, numitems)
    Char   *extended_name, *entry;
    int     name_length, numitems;
{
    if (numitems == 1)		/* 1st match */
	copyn(extended_name, entry, MAXNAMLEN);
    else {			/* 2nd & subsequent matches */
	register Char *x, *ent;
	register int len = 0;

	x = extended_name;
	for (ent = entry; *x && *x == *ent++; x++, len++)
	    continue;
	*x = '\0';		/* Shorten at 1st Char diff */
	if (len == name_length)	/* Ambiguous to prefix? */
	    return (-1);	/* So stop now and save time */
    }
    return (0);
}

/*
 * Return true if check matches initial Chars in template.
 * This differs from PWB imatch in that if check is null
 * it matches anything.
 */
static int
is_prefix(check, template)
    register Char *check, *template;
{
    do
	if (*check == 0)
	    return (TRUE);
    while (*check++ == *template++);
    return (FALSE);
}

/*
 *  Return true if the Chars in template appear at the
 *  end of check, I.e., are it's suffix.
 */
static int
is_suffix(check, template)
    Char   *check, *template;
{
    register Char *c, *t;

    for (c = check; *c++;)
	continue;
    for (t = template; *t++;)
	continue;
    for (;;) {
	if (t == template)
	    return 1;
	if (c == check || *--t != *--c)
	    return 0;
    }
}

int
tenex(inputline, inputline_size)
    Char   *inputline;
    int     inputline_size;
{
    register int numitems, num_read;
    char    tinputline[BUFSIZ];


    setup_tty(ON);

    while ((num_read = read(SHIN, tinputline, BUFSIZ)) > 0) {
	int     i;
	static Char delims[] = {' ', '\'', '"', '\t', ';', '&', '<',
	'>', '(', ')', '|', '^', '%', '\0'};
	register Char *str_end, *word_start, last_Char, should_retype;
	register int space_left;
	COMMAND command;

	for (i = 0; i < num_read; i++)
	    inputline[i] = (unsigned char) tinputline[i];
	last_Char = inputline[num_read - 1] & ASCII;

	if (last_Char == '\n' || num_read == inputline_size)
	    break;
	command = (last_Char == ESC) ? RECOGNIZE : LIST;
	if (command == LIST)
	    (void) fputc('\n', cshout);
	str_end = &inputline[num_read];
	if (last_Char == ESC)
	    --str_end;		/* wipeout trailing cmd Char */
	*str_end = '\0';
	/*
	 * Find LAST occurence of a delimiter in the inputline. The word start
	 * is one Character past it.
	 */
	for (word_start = str_end; word_start > inputline; --word_start)
	    if (Strchr(delims, word_start[-1]))
		break;
	space_left = inputline_size - (word_start - inputline) - 1;
	numitems = tsearch(word_start, command, space_left);

	if (command == RECOGNIZE) {
	    /* print from str_end on */
	    print_recognized_stuff(str_end);
	    if (numitems != 1)	/* Beep = No match/ambiguous */
		beep();
	}

	/*
	 * Tabs in the input line cause trouble after a pushback. tty driver
	 * won't backspace over them because column positions are now
	 * incorrect. This is solved by retyping over current line.
	 */
	should_retype = FALSE;
	if (Strchr(inputline, '\t')) {	/* tab Char in input line? */
	    back_to_col_1();
	    should_retype = TRUE;
	}
	if (command == LIST)	/* Always retype after a LIST */
	    should_retype = TRUE;
	if (should_retype)
	    printprompt();
	pushback(inputline);
	if (should_retype)
	    retype();
    }
    setup_tty(OFF);
    return (num_read);
}

static int
ignored(entry)
    register Char *entry;
{
    struct varent *vp;
    register Char **cp;

    if ((vp = adrof(STRfignore)) == NULL || (cp = vp->vec) == NULL)
	return (FALSE);
    for (; *cp != NULL; cp++)
	if (is_suffix(entry, *cp))
	    return (TRUE);
    return (FALSE);
}
#endif				/* FILEC */
