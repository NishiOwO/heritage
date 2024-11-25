/*-
 * Copyright (c) 1992 Keith Muller.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Keith Muller of the University of California, San Diego.
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
static char sccsid[] = "@(#)gen_subs.c	8.1 (Berkeley) 5/31/93";
#endif /* not lint */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <stdio.h>
#include <ctype.h>
#ifndef __linux__
#ifndef __FreeBSD__
#ifndef __OpenBSD__
#include <tzfile.h>
#endif
#endif
#endif
#ifndef __FreeBSD__
#include <utmp.h>
#endif
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "pax.h"
#include "extern.h"

#ifdef __linux__
#include <bsdcompat.c>
#endif

#if defined(__FreeBSD__) || defined(__OpenBSD__)
#include <datedata.h>
#endif

/*
 * a collection of general purpose subroutines used by pax
 */

/*
 * constants used by ls_list() when printing out archive members
 */
#define MODELEN 20
#define DATELEN 64
#define SIXMONTHS	 ((DAYSPERNYEAR / 2) * SECSPERDAY)
#define CURFRMT		"%b %e %H:%M"
#define OLDFRMT		"%b %e  %Y"
#ifndef UT_NAMESIZE
#define UT_NAMESIZE	8
#endif
#define UT_GRPSIZE	6

/*
 * ls_list()
 *	list the members of an archive in ls format
 */

#if __STDC__
void
ls_list(register ARCHD *arcn, time_t now)
#else
void
ls_list(arcn, now)
	register ARCHD *arcn;
	time_t now;
#endif
{
	register struct stat *sbp;
	char f_mode[MODELEN];
	char f_date[DATELEN];
	char *timefrmt;

	/*
	 * if not verbose, just print the file name
	 */
	if (!vflag) {
		(void)printf("%s\n", arcn->name);
		(void)fflush(stdout);
		return;
	}

	/*
	 * user wants long mode
	 */
	sbp = &(arcn->sb);
	strmode(sbp->st_mode, f_mode);

	if (ltmfrmt == NULL) {
		/*
		 * no locale specified format. time format based on age
		 * compared to the time pax was started.
		 */
		if ((sbp->st_mtime + SIXMONTHS) <= now)
			timefrmt = OLDFRMT;
		else
			timefrmt = CURFRMT;
	} else
		timefrmt = ltmfrmt;

	/*
	 * print file mode, link count, uid, gid and time
	 */
	if (strftime(f_date,DATELEN,timefrmt,localtime(&(sbp->st_mtime))) == 0)
		f_date[0] = '\0';
	(void)printf("%s%2u %-*s %-*s ", f_mode, sbp->st_nlink, UT_NAMESIZE,
		name_uid(sbp->st_uid, 1), UT_GRPSIZE,
		name_gid(sbp->st_gid, 1));

	/*
	 * print device id's for devices, or sizes for other nodes
	 */
	if ((arcn->type == PAX_CHR) || (arcn->type == PAX_BLK))
#		ifdef NET2_STAT
		(void)printf("%4u,%4u ", MAJOR(sbp->st_rdev),
#		else
		(void)printf("%4lu,%4lu ", MAJOR(sbp->st_rdev),
#		endif
		    MINOR(sbp->st_rdev));
	else {
#		ifdef NET2_STAT
		(void)printf("%9lu ", sbp->st_size);
#		else
		(void)printf("%9qu ", sbp->st_size);
#		endif
	}

	/*
	 * print name and link info for hard and soft links
	 */
	(void)printf("%s %s", f_date, arcn->name);
	if ((arcn->type == PAX_HLK) || (arcn->type == PAX_HRG))
		(void)printf(" == %s\n", arcn->ln_name);
	else if (arcn->type == PAX_SLK)
		(void)printf(" => %s\n", arcn->ln_name);
	else
		(void)putchar('\n');
	(void)fflush(stdout);
	return;
}

/*
 * tty_ls()
 * 	print a short summary of file to tty.
 */

#if __STDC__
void
ls_tty(register ARCHD *arcn)
#else
void
ls_tty(arcn)
	register ARCHD *arcn;
#endif
{
	char f_date[DATELEN];
	char f_mode[MODELEN];
	char *timefrmt;

	if (ltmfrmt == NULL) {
		/*
		 * no locale specified format
		 */
		if ((arcn->sb.st_mtime + SIXMONTHS) <= time((time_t *)NULL))
			timefrmt = OLDFRMT;
		else
			timefrmt = CURFRMT;
	} else
		timefrmt = ltmfrmt;

	/*
	 * convert time to string, and print
	 */
	if (strftime(f_date, DATELEN, timefrmt,
	    localtime(&(arcn->sb.st_mtime))) == 0)
		f_date[0] = '\0';
	strmode(arcn->sb.st_mode, f_mode);
	tty_prnt("%s%s %s\n", f_mode, f_date, arcn->name);
	return;
}

/*
 * zf_strncpy()
 *	copy src to dest up to len chars (stopping at first '\0'), when src is
 *	shorter than len, pads to len with '\0'. big performance win (and 
 *	a lot easier to code) over strncpy(), then a strlen() then a
 *	bzero(). (or doing the bzero() first).
 */

#if __STDC__
void
zf_strncpy(register char *dest, register char *src, int len)
#else
void
zf_strncpy(dest, src, len)
	register char *dest;
	register char *src;
	int len;
#endif
{
	register char *stop;

	stop = dest + len;
	while ((dest < stop) && (*src != '\0'))
		*dest++ = *src++;
	while (dest < stop)
		*dest++ = '\0';
	return;
}

/*
 * l_strncpy()
 *	copy src to dest up to len chars (stopping at first '\0')
 * Return:
 *	number of chars copied. (Note this is a real performance win over
 *	doing a strncpy() then a strlen()
 */

#if __STDC__
int
l_strncpy(register char *dest, register char *src, int len)
#else
int
l_strncpy(dest, src, len)
	register char *dest;
	register char *src;
	int len;
#endif
{
	register char *stop;
	register char *start;

	stop = dest + len;
	start = dest;
	while ((dest < stop) && (*src != '\0'))
		*dest++ = *src++;
	if (dest < stop)
		*dest = '\0';
	return(dest - start);
}

/*
 * asc_ul()
 *	convert hex/octal character string into a u_long. We do not have to
 *	check for overflow! (the headers in all supported formats are not large
 *	enough to create an overflow).
 *	NOTE: strings passed to us are NOT TERMINATED.
 * Return:
 *	unsigned long value
 */

#if __STDC__
u_long
asc_ul(register char *str, int len, register int base)
#else
u_long
asc_ul(str, len, base)
	register char *str;
	int len;
	register int base;
#endif
{
	register char *stop;
	u_long tval = 0;

	stop = str + len;

	/*
	 * skip over leading blanks and zeros
	 */
	while ((str < stop) && ((*str == ' ') || (*str == '0')))
		++str;

	/*
	 * for each valid digit, shift running value (tval) over to next digit
	 * and add next digit
	 */
	if (base == HEX) {
		while (str < stop) {
			if ((*str >= '0') && (*str <= '9'))
				tval = (tval << 4) + (*str++ - '0');
			else if ((*str >= 'A') && (*str <= 'F'))
				tval = (tval << 4) + 10 + (*str++ - 'A');
			else if ((*str >= 'a') && (*str <= 'f'))
				tval = (tval << 4) + 10 + (*str++ - 'a');
			else
				break;
		}
	} else {
 		while ((str < stop) && (*str >= '0') && (*str <= '7'))
			tval = (tval << 3) + (*str++ - '0');
	}
	return(tval);
}

/*
 * ul_asc()
 *	convert an unsigned long into an hex/oct ascii string. pads with LEADING
 *	ascii 0's to fill string completely
 *	NOTE: the string created is NOT TERMINATED.
 */

#if __STDC__
int
ul_asc(u_long val, register char *str, register int len, register int base)
#else
int
ul_asc(val, str, len, base)
	u_long val;
	register char *str;
	register int len;
	register int base;
#endif
{
	register char *pt;
	u_long digit;
	
	/*
	 * WARNING str is not '\0' terminated by this routine
	 */
	pt = str + len - 1;

	/*
	 * do a tailwise conversion (start at right most end of string to place
	 * least significant digit). Keep shifting until conversion value goes
	 * to zero (all digits were converted)
	 */
	if (base == HEX) {
		while (pt >= str) {
			if ((digit = (val & 0xf)) < 10)
				*pt-- = '0' + (char)digit;
			else 
				*pt-- = 'a' + (char)(digit - 10);
			if ((val = (val >> 4)) == (u_long)0)
				break;
		}
	} else {
		while (pt >= str) {
			*pt-- = '0' + (char)(val & 0x7);
			if ((val = (val >> 3)) == (u_long)0)
				break;
		}
	}

	/*
	 * pad with leading ascii ZEROS. We return -1 if we ran out of space.
	 */
	while (pt >= str)
		*pt-- = '0';
	if (val != (u_long)0)
		return(-1);
	return(0);
}

#ifndef NET2_STAT
/*
 * asc_uqd()
 *	convert hex/octal character string into a u_quad_t. We do not have to
 *	check for overflow! (the headers in all supported formats are not large
 *	enough to create an overflow).
 *	NOTE: strings passed to us are NOT TERMINATED.
 * Return:
 *	u_quad_t value
 */

#if __STDC__
u_quad_t
asc_uqd(register char *str, int len, register int base)
#else
u_quad_t
asc_uqd(str, len, base)
	register char *str;
	int len;
	register int base;
#endif
{
	register char *stop;
	u_quad_t tval = 0;

	stop = str + len;

	/*
	 * skip over leading blanks and zeros
	 */
	while ((str < stop) && ((*str == ' ') || (*str == '0')))
		++str;

	/*
	 * for each valid digit, shift running value (tval) over to next digit
	 * and add next digit
	 */
	if (base == HEX) {
		while (str < stop) {
			if ((*str >= '0') && (*str <= '9'))
				tval = (tval << 4) + (*str++ - '0');
			else if ((*str >= 'A') && (*str <= 'F'))
				tval = (tval << 4) + 10 + (*str++ - 'A');
			else if ((*str >= 'a') && (*str <= 'f'))
				tval = (tval << 4) + 10 + (*str++ - 'a');
			else
				break;
		}
	} else {
 		while ((str < stop) && (*str >= '0') && (*str <= '7'))
			tval = (tval << 3) + (*str++ - '0');
	}
	return(tval);
}

/*
 * uqd_asc()
 *	convert an u_quad_t into a hex/oct ascii string. pads with LEADING
 *	ascii 0's to fill string completely
 *	NOTE: the string created is NOT TERMINATED.
 */

#if __STDC__
int
uqd_asc(u_quad_t val, register char *str, register int len, register int base)
#else
int
uqd_asc(val, str, len, base)
	u_quad_t val;
	register char *str;
	register int len;
	register int base;
#endif
{
	register char *pt;
	u_quad_t digit;
	
	/*
	 * WARNING str is not '\0' terminated by this routine
	 */
	pt = str + len - 1;

	/*
	 * do a tailwise conversion (start at right most end of string to place
	 * least significant digit). Keep shifting until conversion value goes
	 * to zero (all digits were converted)
	 */
	if (base == HEX) {
		while (pt >= str) {
			if ((digit = (val & 0xf)) < 10)
				*pt-- = '0' + (char)digit;
			else 
				*pt-- = 'a' + (char)(digit - 10);
			if ((val = (val >> 4)) == (u_quad_t)0)
				break;
		}
	} else {
		while (pt >= str) {
			*pt-- = '0' + (char)(val & 0x7);
			if ((val = (val >> 3)) == (u_quad_t)0)
				break;
		}
	}

	/*
	 * pad with leading ascii ZEROS. We return -1 if we ran out of space.
	 */
	while (pt >= str)
		*pt-- = '0';
	if (val != (u_quad_t)0)
		return(-1);
	return(0);
}
#endif
