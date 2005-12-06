/*
 * pcmcia-check-broken-cis.c
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
 * (C) 2005             Dominik Brodowski
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

#include "cistpl.h"

struct needs_cis {
	unsigned long code;
	unsigned long ofs;
	char *info;
	char *cisfile;
};

#define NEEDS_CIS_ENTRY(_code, _ofs, _info, _cisfile) \
{ .code = _code, .ofs = _ofs, .info = _info, .cisfile = _cisfile, }

static struct needs_cis cis_table[] = {
	/* "D-Link DE-650 Ethernet" */
	NEEDS_CIS_ENTRY(0x40, 0x0009, "D-Link PC Ethernet Card", "D-Link.dat"),
	/* "Linksys Ethernet E-CARD PC Ethernet Card */
	NEEDS_CIS_ENTRY(0x40, 0x0009, "E-CARD PC Ethernet Card", "E-CARD.dat"),
	{ },
};

int main(int argc, char **argv) {
	int ret;
	unsigned int socket_no;
	struct needs_cis * entry = NULL;
	tuple_t tuple;
	unsigned char buf[256];

	if (argc != 2)
		return -EINVAL;

	ret = sscanf(argv[1], "%u", &socket_no);
	if (ret != 1)
		return -ENODEV;

	ret = read_out_cis(socket_no, NULL);
	if (ret)
		return (ret);

	entry = &cis_table[0];

	while (entry) {
		if (!entry->cisfile)
			return 0;

		tuple.DesiredTuple = entry->code;
		tuple.Attributes = TUPLE_RETURN_COMMON;
		tuple.TupleData = buf;
		tuple.TupleDataMax = 255;
		pcmcia_get_first_tuple(BIND_FN_ALL, &tuple);

		tuple.TupleOffset = entry->ofs;

		pcmcia_get_tuple_data(&tuple);

		if (strncmp((char *) tuple.TupleData, entry->info,
			    strlen(entry->info)) != 0) {
			entry++;
			continue;
		}

		printf("%s", entry->cisfile);
	};

	return 0;
}
