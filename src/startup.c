/*
 * Startup tool for non statically mapped PCMCIA sockets
 *
 * (C) 2005		Dominik Brodowski <linux@brodo.de>
 *
 *  The initial developer of the original code is David A. Hinds
 *  <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 *  are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * License: GPL v2
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <unistd.h>

#include <sysfs/libsysfs.h>

#include "startup.h"

/* uncomment for debug output */
#ifdef DEBUG
#define dprintf printf
#else
#define dprintf(...) do { } while(0);
#endif

/* Linked list of resource adjustments */
struct adjust_list_t *root_adjust = NULL;

/* path for config file, device scripts */
static char *configpath = "/etc/pcmcia";

enum {
        RESOURCE_MEM,
        RESOURCE_IO,
        MAX_RESOURCE_FILES
};


static const char *resource_files[MAX_RESOURCE_FILES] = {
	[RESOURCE_MEM]	= "available_resources_mem",
	[RESOURCE_IO]	= "available_resources_io",
};

#define PATH_TO_SOCKET "/sys/class/pcmcia_socket/"


static int add_available_resource(unsigned int socket_no, unsigned int type,
				  unsigned long start, unsigned long end)
{
	char file[SYSFS_PATH_MAX];
	char content[SYSFS_PATH_MAX];
	struct sysfs_attribute *attr;
	int ret;
	size_t len;

	if (type >= MAX_RESOURCE_FILES)
		return -EINVAL;

	if (end <= start)
		return -EINVAL;

	dprintf("%d %d %lx %lx\n", socket_no, type, start, end);

	snprintf(file, SYSFS_PATH_MAX, PATH_TO_SOCKET "pcmcia_socket%u/%s",
		socket_no, resource_files[type]);

	len = snprintf(content, SYSFS_PATH_MAX, "0x%08lx - 0x%08lx", start, end);

	dprintf("content is %s\n", content);

	dprintf("file is %s\n", file);

	attr = sysfs_open_attribute(file);
	if (!attr)
		return -ENODEV;

	dprintf("open, len %d\n", len);

	ret = sysfs_write_attribute(attr, content, len);

	dprintf("ret is %d\n", ret);

	sysfs_close_attribute(attr);

	return (ret);
}

static int setup_done(unsigned int socket_no)
{
	int ret;
	char file[SYSFS_PATH_MAX];
	struct sysfs_attribute *attr;

	snprintf(file, SYSFS_PATH_MAX, PATH_TO_SOCKET
		 "pcmcia_socket%u/available_resources_setup_done",
		 socket_no);

	attr = sysfs_open_attribute(file);
	if (!attr)
		return -ENODEV;

	ret = sysfs_write_attribute(attr, "42", 2);

	sysfs_close_attribute(attr);

	return (ret);
}


static int disallow_irq(unsigned int socket_no, unsigned int irq)
{
	char file[SYSFS_PATH_MAX];
	char content[SYSFS_PATH_MAX];
	struct sysfs_attribute *attr;
	unsigned int mask = 0xfff;
	unsigned int new_mask;
	int ret;
	size_t len;

	if (irq >= 32)
		return -EINVAL;

	snprintf(file, SYSFS_PATH_MAX, PATH_TO_SOCKET
		 "pcmcia_socket%u/card_irq_mask",
		 socket_no);

	attr = sysfs_open_attribute(file);
	if (!attr)
		return -ENODEV;

	dprintf("open, len %d\n", len);

	ret = sysfs_read_attribute(attr);
	if (ret) {
		ret = -EINVAL;
		goto out;
	}

	if (!attr->value || (attr->len < 6)) {
		ret = -EIO;
		goto out;
	}

	ret = sscanf(attr->value, "0x%x\n", &mask);

	new_mask = 1 << irq;

	mask &= ~new_mask;

	len = snprintf(content, SYSFS_PATH_MAX, "0x%04x", mask);

	dprintf("content is %s\n", content);

	ret = sysfs_write_attribute(attr, content, len);

 out:
	sysfs_close_attribute(attr);

	return (ret);
}


static void load_config(void)
{
    if (chdir(configpath) != 0) {
	syslog(LOG_ERR, "chdir to %s failed: %m", configpath);
	exit(EXIT_FAILURE);
    }
    parse_configfile("config.opts");
    return;
}


static void adjust_resources(unsigned int socket_no)
{
    adjust_list_t *al;
    char tmp[64];

    for (al = root_adjust; al; al = al->next) {
	    switch (al->adj.Resource) {
	    case RES_MEMORY_RANGE:
		    add_available_resource(socket_no, RESOURCE_MEM,
					   al->adj.resource.memory.Base,
					   al->adj.resource.memory.Base +
					   al->adj.resource.memory.Size - 1);
		    break;
	    case RES_IO_RANGE:
		    add_available_resource(socket_no, RESOURCE_IO,
					   al->adj.resource.io.BasePort,
					   al->adj.resource.io.BasePort +
					   al->adj.resource.io.NumPorts - 1);
		    break;
	    case RES_IRQ:
		    disallow_irq(socket_no, al->adj.resource.irq.IRQ);
		    break;
	    }
	    syslog(LOG_WARNING, "could not adjust resource: %s: %m", tmp);
    }
}


int main(int argc, char *argv[])
{
	unsigned long socket;

	if (argc != 2)
		return -EINVAL;

	socket = strtoul(argv[1], NULL, 0);

	load_config();

	adjust_resources(socket);

	setup_done(socket);

	return 0;
}
