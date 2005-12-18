/*
 *  (C) 2004-2005  Dominik Brodowski <linux@brodo.de>
 *
 * Partly based on cardctl.c from pcmcia-cs-3.2.7/cardmgr/, which states
 * in its header:
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 *  are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <libintl.h>
#include <locale.h>

#include <sysfs/libsysfs.h>

#include <getopt.h>

#define MAX_SOCKET 8


static int pccardctl_power_one(unsigned long socket_no, unsigned int device,
			       unsigned int power)
{
	int ret;
	char file[SYSFS_PATH_MAX];
        struct sysfs_attribute *attr;

	snprintf(file, SYSFS_PATH_MAX,
		 "/sys/bus/pcmcia/devices/%lu.%u/power/state",
		 socket_no, device);

        attr = sysfs_open_attribute(file);
        if (!attr)
                return -ENODEV;

        ret = sysfs_write_attribute(attr, power ? "2" : "0", 1);

        sysfs_close_attribute(attr);

	return (ret);
}

static int pccardctl_power(unsigned long socket_no, unsigned int power)
{
	unsigned int i;
	for (i=0; i<2; i++) /* max 2 devices per socket */
		pccardctl_power_one(socket_no, i, power);

	return 0;
}

static int pccardctl_echo_one(unsigned long socket_no, const char *in_file)
{
        int ret;
        char file[SYSFS_PATH_MAX];
        struct sysfs_attribute *attr;

	if (!file)
		return -EINVAL;

        snprintf(file, SYSFS_PATH_MAX, "/sys/class/pcmcia_socket/pcmcia_socket%lu/%s",
                 socket_no, in_file);

        attr = sysfs_open_attribute(file);
        if (!attr)
                return -ENODEV;

        ret = sysfs_write_attribute(attr, "42", 2);

        sysfs_close_attribute(attr);

        return (ret);
}

static int pccardctl_socket_exists(unsigned long socket_no)
{
	char file[SYSFS_PATH_MAX];

	snprintf(file, SYSFS_PATH_MAX,
		 "/sys/class/pcmcia_socket/pcmcia_socket%lu/card_insert",
		 socket_no);

	return (!(sysfs_path_is_file(file)));
}

static char * read_out_file(char * file)
{
	struct sysfs_attribute *attr = sysfs_open_attribute(file);
	int ret;
	char *result = NULL;

	if (!attr)
		return NULL;
	ret = sysfs_read_attribute(attr);

	if (ret || !attr->value || !attr->len || (attr->len > SYSFS_PATH_MAX))
		goto close_out;

	result = malloc(attr->len + 1);
	if (result) {
		memcpy(result, attr->value, attr->len);
		result[attr->len] = '\0';
		if (result[attr->len - 1] == '\n')
			result[attr->len - 1] = '\0';
	}

 close_out:
	sysfs_close_attribute(attr);
	return result;
}

static inline char * pccardctl_get_string_socket(unsigned long socket_no, const char *in_file)
{
	char file[SYSFS_PATH_MAX];

	snprintf(file, SYSFS_PATH_MAX, "/sys/class/pcmcia_socket/pcmcia_socket%lu/%s",
		 socket_no, in_file);

	return read_out_file(file);
}

static inline char * pccardctl_get_string(unsigned long socket_no, const char *in_file)
{
	char file[SYSFS_PATH_MAX];

	snprintf(file, SYSFS_PATH_MAX, "/sys/bus/pcmcia/devices/%lu.0/%s",
		 socket_no, in_file);

	return read_out_file(file);
}

static inline int pccardctl_get_one_f(unsigned long socket_no, unsigned int dev, const char *in_file, unsigned int *result)
{
	char *value;
	char file[SYSFS_PATH_MAX];

	snprintf(file, SYSFS_PATH_MAX, "/sys/bus/pcmcia/devices/%lu.%u/%s",
		 socket_no, dev, in_file);
	value = read_out_file(file);
	if (!value)
		return -EINVAL;

	if (sscanf(value, "0x%X", result) != 1)
		return -EIO;
	return 0;
}

static int pccardctl_get_one(unsigned long socket_no, const char *in_file, unsigned int *result)
{
	return pccardctl_get_one_f(socket_no, 0, in_file, result);
}

static int pccardctl_get_power(unsigned long socket_no, unsigned int func)
{
	char * value;
	char file[SYSFS_PATH_MAX];

	snprintf(file, SYSFS_PATH_MAX, "/sys/bus/pcmcia/devices/%lu.%u/power/state",
		 socket_no, func);
	value = read_out_file(file);
	if (value) {
		if (strncmp(value, "0", 1))
			return 3;
		return 0;
	}

	return -ENODEV;
}


static int pccardctl_ident(unsigned long socket_no)
{
	static char *fn[] = {
		"multifunction",
		"memory",
		"serial",
		"parallel",
		"fixed disk",
		"video",
		"network",
		"AIMS",
		"SCSI"
	};
	char *prod_id[4];
	int valid_prod_id = 0;
	int i;
	unsigned int manf_id, card_id;

	if (!pccardctl_socket_exists(socket_no))
		return -ENODEV;

	for (i=1; i<=4; i++) {
		char file[SYSFS_PATH_MAX];
		snprintf(file, SYSFS_PATH_MAX, "prod_id%u", i);

		prod_id[i-1] = pccardctl_get_string(socket_no, file);
		if (prod_id[i-1])
			valid_prod_id++;
	}

	if (valid_prod_id) {
		printf("  product info: ");
		for (i=0;i<4;i++) {
			printf("%s\"%s\"", (i>0) ? ", " : "",
			       prod_id[i] ? prod_id[i] : "" );
			if (prod_id[i])
				free(prod_id[i]);
		}
		printf("\n");
	} else
		printf("  no product info available\n");

	if (!pccardctl_get_one(socket_no, "manf_id", &manf_id))
		if (!pccardctl_get_one(socket_no, "card_id", &card_id))
			printf("  manfid: 0x%04x, 0x%04x\n", manf_id, card_id);

	if (!pccardctl_get_one(socket_no, "func_id", &manf_id))
		printf("  function: %d (%s)\n", manf_id, fn[manf_id]);


	return 0;
}

static int pccardctl_info(unsigned long socket_no)
{
	int i;
	unsigned int manf_id, card_id, func_id;
	char *prod_id;

	if (!pccardctl_socket_exists(socket_no))
		return -ENODEV;

	for (i=1; i<=4; i++) {
		char file[SYSFS_PATH_MAX];
		snprintf(file, SYSFS_PATH_MAX, "prod_id%u", i);

		prod_id = pccardctl_get_string(socket_no, file);

		printf("PRODID_%d=\"%s\"\n", i, (prod_id) ? prod_id : "");

		free(prod_id);
		prod_id = NULL;
	}

	printf("MANFID=%04x,%04x\n",
	       (!pccardctl_get_one(socket_no, "manf_id", &manf_id)) ? manf_id : 0,
	       (!pccardctl_get_one(socket_no, "card_id", &card_id)) ? card_id : 0);

	printf("FUNCID=%d\n",
	       (!pccardctl_get_one(socket_no, "func_id", &func_id)) ? func_id : 255);

	return 0;
}

static int pccardctl_status(unsigned long socket_no)
{
	char *card_type;
	char *card_voltage;
	int susp;
	int is_cardbus = 0;
	int i, ret;

	if (!pccardctl_socket_exists(socket_no))
		return -ENODEV;

	card_type = pccardctl_get_string_socket(socket_no, "card_type");
	if (!card_type) {
		printf("  no card\n");
		return 0;
	}

	card_voltage = pccardctl_get_string_socket(socket_no, "card_voltage");

	strncmp(card_type, "16", 2) ? is_cardbus = 0 : 1;

	printf("  %s %s %s", card_voltage, card_type, is_cardbus ? "CardBus card" : "PC Card");

	susp = pccardctl_get_power(socket_no, 0);
	if (susp > 0)
		printf(" [suspended]");
	printf("\n");

	if (is_cardbus) {
		/* FIXME: handle functions */
		return 0;
	}

	for (i=0; i<4; i++) {
		int function;
		char drv[SYSFS_PATH_MAX];
		char file[SYSFS_PATH_MAX];
		if (pccardctl_get_one_f(socket_no, i, "function", &function))
			continue;

		printf("  Subdevice %u (function %u)", i, function);

		snprintf(file, SYSFS_PATH_MAX, "/sys/bus/pcmcia/devices/%lu.%u/driver",
			 socket_no, i);
		ret = readlink(file, drv, sizeof(drv) - 1);
		if (ret <= 0)
			printf(" [unbound]");
		else if (ret > 0) {
			drv[ret]='\0';
			printf(" bound to driver \"%s\"", basename(drv));
		}

		susp = pccardctl_get_power(socket_no, i);
		if (susp > 0)
			printf(" [suspended]");

		printf("\n");
	}

	return 0;
}

static void print_header(void) {
	printf("pcmciautils %s\n", PCMCIAUTILS_VERSION);
	printf("Copyright (C) 2004-2005 Dominik Brodowski, (C) 1999 David A. Hinds\n");
	printf("Report errors and bugs to <linux-pcmcia@lists.infradead.org>, please.\n");
}

static char *cmdname[] = {
	"insert",
	"eject",
	"suspend",
	"resume",
	"reset",
	"info",
	"status",
	"config",
	"ident",
};

static void print_help(void) {
	unsigned int i;

	printf("Usage: pccardctl COMMAND\n");
	printf("Supported commands are:\n");
	for (i = 0; i < sizeof(cmdname)/sizeof(cmdname[0]); i++) {
		printf("\t%s\n", cmdname[i]);
	}
}

static void print_unknown_arg(void) {
	print_header();
	printf("invalid or unknown argument\n");
	print_help();
}

static struct option pccardctl_opts[] = {
	{ .name="version",	.has_arg=no_argument,		.flag=NULL,	.val='V'},
	{ .name="help",		.has_arg=no_argument,		.flag=NULL,	.val='h'},
//	{ .name="socket",	.has_arg=required_argument,	.flag=NULL,	.val='s'},
//	{ .name="socketdir",	.has_arg=required_argument,	.flag=NULL,	.val='d'},
};

enum {
	PCCARDCTL_INSERT,
	PCCARDCTL_EJECT,
	PCCARDCTL_SUSPEND,
	PCCARDCTL_RESUME,
	PCCARDCTL_RESET,
	PCCARDCTL_INFO,
	PCCARDCTL_STATUS,
	PCCARDCTL_CONFIG,
	PCCARDCTL_IDENT,
	NCMD
};


int main(int argc, char **argv) {
	extern char *optarg;
	extern int optind, opterr, optopt;
	int ret = 0;
	unsigned int cont = 1;
	unsigned long socket = 0;
	unsigned int socket_is_set = 0;
	char *s;
	unsigned int cmd;

	do {
		ret = getopt_long(argc, argv, "Vhc:f:s:", pccardctl_opts, NULL);
		switch (ret) {
		case -1:
			cont = 0;
			break;
		case 'V':
			print_header();
			return 0;
		case 'h':
			print_header();
			print_help();
			return 0;
		case 'c':
		case 's':
		case 'f':
			/* ignored */
			fprintf(stderr, "ignoring parameter '%c'\n", ret);
			break;
		default:
			print_unknown_arg();
			return -EINVAL;
		}
	} while (cont);

	if ((argc == optind) || (argc > (optind + 2))) {
		print_unknown_arg();
		return -EINVAL;
	}

	/* determine command */
	for (cmd = 0; cmd < NCMD; cmd++)
		if (strcmp(argv[optind], cmdname[cmd]) == 0)
			break;
	if (cmd == NCMD) {
		print_unknown_arg();
		return -EINVAL;
	}

	if (argc == optind+2) {
		socket_is_set = 1;
		socket = strtol(argv[optind+1], &s, 0);
		if ((*argv[optind+1] == '\0') || (*s != '\0') || (socket >= MAX_SOCKET)) {
			print_unknown_arg();
			return -EINVAL;
		}
	}

	for (cont = 0; cont < MAX_SOCKET; cont++) {
		if (socket_is_set && (socket != cont))
			continue;

		if (!socket_is_set && !pccardctl_socket_exists(cont))
			continue;

		if (!socket_is_set && (cmd > PCCARDCTL_INFO))
			printf("Socket %d:\n", cont);

		ret = 0;
		switch (cmd) {
		case PCCARDCTL_INSERT:
			ret = pccardctl_echo_one(cont, "card_insert");
			break;
		case PCCARDCTL_EJECT:
			ret = pccardctl_echo_one(cont, "card_eject");
			break;
		case PCCARDCTL_INFO:
			ret = pccardctl_info(cont);
			break;
		case PCCARDCTL_IDENT:
			ret = pccardctl_ident(cont);
			break;
		case PCCARDCTL_SUSPEND:
			ret = pccardctl_power(cont, 3);
			break;
		case PCCARDCTL_RESET:
			ret = pccardctl_power(cont, 3);
			if (ret && socket_is_set)
				return (ret);
			/* fall through */
		case PCCARDCTL_RESUME:
			ret = pccardctl_power(cont, 0);
			break;
		case PCCARDCTL_STATUS:
			ret = pccardctl_status(cont);
			break;
		default:
			fprintf(stderr, "command '%s' not yet handled by pccardctl\n", cmdname[cmd]);
			return -EAGAIN;
		}

		if (ret && socket_is_set)
			return (ret);
	}

	return 0;
}
