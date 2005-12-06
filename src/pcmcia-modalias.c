/*
 * pcmcia-modalias - generate a MODULE_ALIAS string appropriate for already insterted PCMCIA cards
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * (C) 2005        Dominik Brodowski <linux@brodo.de>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

#include <sysfs/libsysfs.h>


#define PATH_TO_DEVICE "/sys/bus/pcmcia/devices/"


unsigned int crc32(unsigned char const *p, unsigned int len)
{
        int i;
        unsigned int crc = 0;
        while (len--) {
                crc ^= *p++;
                for (i = 0; i < 8; i++)
                        crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
        }
        return crc;
}

static unsigned int get_one_hash(char *device, unsigned int prod_id_nr) {
	char file[SYSFS_PATH_MAX];
	char value[SYSFS_PATH_MAX];
	int ret;

	snprintf(file, SYSFS_PATH_MAX, PATH_TO_DEVICE "%s/prod_id%d", device, prod_id_nr);

        ret = sysfs_read_attribute_value(file, value, SYSFS_PATH_MAX);
	if (ret)
		return 0;

	if (strlen(value) < 2)
		return 0;

	return crc32(value, strlen(value) - 1);
}


static unsigned int read_one(char *device, char *name) {
	char file[SYSFS_PATH_MAX];
	char value[SYSFS_PATH_MAX];
 	unsigned int value2;
	int ret;

	snprintf(file, SYSFS_PATH_MAX, PATH_TO_DEVICE "%s/%s", device, name);

        ret = sysfs_read_attribute_value(file, value, SYSFS_PATH_MAX);
	if (ret)
		return 0;

	ret = sscanf(value, "0x%X", &value2);
	if (ret != 1)
		return 0;

	return value2;
}

static int extract_modalias_string(char *device) {
	char modalias_string[SYSFS_PATH_MAX]; /* that's more than enough */
	int pos = 0;
	unsigned int value;
	unsigned int tmp, tmp2;

	value = read_one(device, "manf_id");
	pos += snprintf(&modalias_string[pos], SYSFS_PATH_MAX - pos,
		       "pcmcia:m%04X", value);
	if (pos > (SYSFS_PATH_MAX - 10))
		return -ENOMEM;

	value = read_one(device, "card_id");
	pos += snprintf(&modalias_string[pos], SYSFS_PATH_MAX - pos,
		       "c%04X", value);
	if (pos > (SYSFS_PATH_MAX - 10))
		return -ENOMEM;

	value = read_one(device, "func_id");
	pos += snprintf(&modalias_string[pos], SYSFS_PATH_MAX - pos,
		       "f%02X", value);
	if (pos > (SYSFS_PATH_MAX - 10))
		return -ENOMEM;

	value = read_one(device, "function");
	pos += snprintf(&modalias_string[pos], SYSFS_PATH_MAX - pos,
		       "pfn%02X", value);
	if (pos > (SYSFS_PATH_MAX - 10))
		return -ENOMEM;

	tmp2 = sscanf(device, "%d.%d", &tmp, &value);
	if (tmp2 != 2)
		return -EIO;
	pos += snprintf(&modalias_string[pos], SYSFS_PATH_MAX - pos,
		       "fn%02X", value);
	if (pos > (SYSFS_PATH_MAX - 10))
		return -ENOMEM;

	value = get_one_hash(device, 1);
	pos += snprintf(&modalias_string[pos], SYSFS_PATH_MAX - pos,
		       "pa%08X", value);
	if (pos > (SYSFS_PATH_MAX - 10))
		return -ENOMEM;

	value = get_one_hash(device, 2);
	pos += snprintf(&modalias_string[pos], SYSFS_PATH_MAX - pos,
		       "pb%08X", value);
	if (pos > (SYSFS_PATH_MAX - 10))
		return -ENOMEM;

	value = get_one_hash(device, 3);
	pos += snprintf(&modalias_string[pos], SYSFS_PATH_MAX - pos,
		       "pc%08X", value);
	if (pos > (SYSFS_PATH_MAX - 10))
		return -ENOMEM;

	value = get_one_hash(device, 4);
	pos += snprintf(&modalias_string[pos], SYSFS_PATH_MAX - pos,
		       "pd%08X", value);
	if (pos > (SYSFS_PATH_MAX - 10))
		return -ENOMEM;


	printf("%s\n", modalias_string);

	return 0;
}

int main(int argc, char **argv)
{
	if (argc != 2)
		return -EINVAL;

	extract_modalias_string(argv[1]);

	return 0;
}
