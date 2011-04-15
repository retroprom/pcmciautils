/*
 * read-cis.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * (C) 1999             David A. Hinds
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

#include <sysfs/libsysfs.h>

#include "cistpl.h"

#define MAX_TUPLES                0x200

#define PATH_TO_SOCKET "/sys/class/pcmcia_socket/"

/* Bits in attr field */
#define IS_ATTR         1
#define IS_INDIRECT     8

static unsigned int functions;
static unsigned char cis_copy[MAX_TUPLES];
static unsigned int cis_length = MAX_TUPLES;

static void read_cis(int attr, unsigned int addr, unsigned int len, void *ptr)
{
	if (cis_length > addr+len)
		memcpy(ptr, cis_copy + addr, len);
	else
		memset(ptr, 0xff, len);
	return;
}

static int follow_link(tuple_t *tuple)
{
	unsigned char link[5];
	unsigned int ofs;

	if (tuple->Flags.mfc_fn) {
		/* Get indirect link from the MFC tuple */
		read_cis(tuple->Flags.link_space,
			       tuple->LinkOffset, 5, link);
		ofs = *(u_int *)(link+1);
		tuple->Flags.space = (link[0] == CISTPL_MFC_ATTR);
		/* Move to the next indirect link */
		tuple->LinkOffset += 5;
		tuple->Flags.mfc_fn--;
	} else if (tuple->Flags.has_link) {
		ofs = tuple->LinkOffset;
		tuple->Flags.space = tuple->Flags.link_space;
		tuple->Flags.has_link = 0;
	} else {
		return -1;
	}
	if (tuple->Flags.space) {
		/* This is ugly, but a common CIS error is to code the long
		   link offset incorrectly, so we check the right spot... */
		read_cis(tuple->Flags.space, ofs, 5, link);
		if ((link[0] == CISTPL_LINKTARGET) && (link[1] >= 3) &&
		    (strncmp(link+2, "CIS", 3) == 0))
			return ofs;
		/* Then, we try the wrong spot... */
		ofs = ofs >> 1;
	}
	read_cis(tuple->Flags.space, ofs, 5, link);
	if ((link[0] == CISTPL_LINKTARGET) && (link[1] >= 3) &&
	    (strncmp(link+2, "CIS", 3) == 0))
		return ofs;
	return -1;
}

int pcmcia_get_next_tuple(unsigned int function, tuple_t *tuple)
{
	unsigned char link[2], tmp;
	int ofs, i, attr;

	link[1] = tuple->TupleLink;
	ofs = tuple->CISOffset + tuple->TupleLink;
	attr = tuple->Flags.space;

	for (i = 0; i < MAX_TUPLES; i++) {
		if (link[1] == 0xff) {
			link[0] = CISTPL_END;
		} else {
			read_cis(attr, ofs, 2, link);
			if (link[0] == CISTPL_NULL) {
				ofs++; continue;
			}
		}

		/* End of chain?  Follow long link if possible */
		if (link[0] == CISTPL_END) {
			ofs = follow_link(tuple);
			if (ofs < 0)
				return -ENODEV;
			attr = tuple->Flags.space;
			read_cis(attr, ofs, 2, link);
		}

		/* Is this a link tuple?  Make a note of it */
		if ((link[0] == CISTPL_LONGLINK_A) ||
		    (link[0] == CISTPL_LONGLINK_C) ||
		    (link[0] == CISTPL_LONGLINK_MFC) ||
		    (link[0] == CISTPL_LINKTARGET) ||
		    (link[0] == CISTPL_INDIRECT) ||
		    (link[0] == CISTPL_NO_LINK)) {
			switch (link[0]) {
			case CISTPL_LONGLINK_A:
				tuple->Flags.has_link = 1;
				tuple->Flags.link_space = attr | IS_ATTR;
				read_cis(attr, ofs+2, 4, &tuple->LinkOffset);
				break;
			case CISTPL_LONGLINK_C:
				tuple->Flags.has_link = 1;
				tuple->Flags.link_space = attr & ~IS_ATTR;
				read_cis(attr, ofs+2, 4, &tuple->LinkOffset);
				break;
			case CISTPL_INDIRECT:
				tuple->Flags.has_link = 1;
				tuple->Flags.link_space = IS_ATTR | IS_INDIRECT;
				tuple->LinkOffset = 0;
				break;
			case CISTPL_LONGLINK_MFC:
				tuple->LinkOffset = ofs + 3;
				tuple->Flags.link_space = attr;
				if (function == BIND_FN_ALL) {
					/* Follow all the MFC links */
					read_cis(attr, ofs+2, 1, &tmp);
					tuple->Flags.mfc_fn = tmp;
				} else {
					/* Follow exactly one of the links */
					tuple->Flags.mfc_fn = 1;
					tuple->LinkOffset += function * 5;
				}
				break;
			case CISTPL_NO_LINK:
				tuple->Flags.has_link = 0;
				break;
			}
			if ((tuple->Attributes & TUPLE_RETURN_LINK) &&
			    (tuple->DesiredTuple == RETURN_FIRST_TUPLE))
				break;
		} else
			if (tuple->DesiredTuple == RETURN_FIRST_TUPLE)
				break;

		if (link[0] == tuple->DesiredTuple)
			break;
		ofs += link[1] + 2;
	}
	if (i == MAX_TUPLES)
		return -ENODEV;

	tuple->TupleCode = link[0];
	tuple->TupleLink = link[1];
	tuple->CISOffset = ofs + 2;

	return 0;
}

int pcmcia_get_first_tuple(unsigned int function, tuple_t *tuple)
{
	tuple->TupleLink = 0;
	tuple->Flags.link_space = tuple->Flags.mfc_fn = 0;
	/* Assume presence of a LONGLINK_C to address 0 */
	tuple->CISOffset = tuple->LinkOffset = 0;
	tuple->Flags.space = tuple->Flags.has_link = 1;

	if ((functions > 1) &&
	    !(tuple->Attributes & TUPLE_RETURN_COMMON)) {
		unsigned char req = tuple->DesiredTuple;
		tuple->DesiredTuple = CISTPL_LONGLINK_MFC;
		if (!pcmcia_get_next_tuple(function, tuple)) {
			tuple->DesiredTuple = CISTPL_LINKTARGET;
			if (pcmcia_get_next_tuple(function, tuple))
				return -ENODEV;
		} else
			tuple->CISOffset = tuple->TupleLink = 0;
		tuple->DesiredTuple = req;
	}
	return pcmcia_get_next_tuple(function, tuple);
}

#define _MIN(a, b)              (((a) < (b)) ? (a) : (b))

int pcmcia_get_tuple_data(tuple_t *tuple)
{
	unsigned int len;

	if (tuple->TupleLink < tuple->TupleOffset)
		return -ENODEV;
	len = tuple->TupleLink - tuple->TupleOffset;
	tuple->TupleDataLen = tuple->TupleLink;
	if (len == 0)
		return 0;

	read_cis(tuple->Flags.space,
		tuple->CISOffset + tuple->TupleOffset,
		_MIN(len, tuple->TupleDataMax),
		tuple->TupleData);

	return 0;
}


int read_out_cis(unsigned int socket_no, FILE *fd)
{
	char file[SYSFS_PATH_MAX];
	int ret, i;
	tuple_t tuple;
	unsigned char buf[256];

	snprintf(file, SYSFS_PATH_MAX, PATH_TO_SOCKET "pcmcia_socket%d/cis",
		 socket_no);

	if (!fd) {
		fd = fopen(file, "r");
		if (!fd)
			return -EIO;
	}

	for (i = 0; i < MAX_TUPLES; i++) {
		ret = fgetc(fd);
		if (ret == EOF) {
			cis_length = i + 1;
			break;
		}
		cis_copy[i] = (unsigned char) ret;
	}
	fclose(fd);

	if (cis_length < 4)
		return -EINVAL;

	functions = 1;

	tuple.DesiredTuple = CISTPL_LONGLINK_MFC;
	tuple.Attributes = TUPLE_RETURN_COMMON;

	ret = pcmcia_get_first_tuple(BIND_FN_ALL, &tuple);
	if (ret)
		functions = 1;

	tuple.TupleData = buf;
	tuple.TupleOffset = 0;
	tuple.TupleDataMax = 255;
	ret = pcmcia_get_tuple_data(&tuple);
	if (ret)
		return -EBADF;

	functions = tuple.TupleData[0];

	return 0;
}
