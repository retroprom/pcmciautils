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
#include <libgen.h>
#include <locale.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>

#define MAX_SOCKET 8
#define SYSFS_PATH_MAX 255

static const char * const fn[] = {
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

/* crc32hash.c - derived from linux/lib/crc32.c, GNU GPL v2 */
static unsigned int crc32(unsigned char const *p, unsigned int len)
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

static int sysfs_write_file(const char *fname, const char *value, size_t len)
{
	ssize_t numwrite;
	int fd;
	int ret = 0;

	fd = open(fname, O_WRONLY);
	if (fd <= 0)
		return fd;

	numwrite = write(fd, value, len);
	if ((numwrite < 1) || ((size_t) numwrite != len))
		ret = -EIO;

	close(fd);
	return ret;
}

static int pccardctl_power_socket(unsigned long socket_no, unsigned int power)
{
	char file[SYSFS_PATH_MAX];

	snprintf(file, SYSFS_PATH_MAX,
		 "/sys/class/pcmcia_socket/pcmcia_socket%lu/card_pm_state",
		 socket_no);

	return sysfs_write_file(file, power ? "off" : "on", power ? 3 : 2);
}

static int pccardctl_echo_one(unsigned long socket_no, const char *in_file)
{
	char file[SYSFS_PATH_MAX];

	snprintf(file, SYSFS_PATH_MAX,
		"/sys/class/pcmcia_socket/pcmcia_socket%lu/%s",
		socket_no, in_file);

	return sysfs_write_file(file, "42", 2);
}

static int pccardctl_socket_exists(unsigned long socket_no)
{
	char file[SYSFS_PATH_MAX];
	struct stat st;

	snprintf(file, SYSFS_PATH_MAX,
		 "/sys/class/pcmcia_socket/pcmcia_socket%lu/card_insert",
		 socket_no);

	return !stat(file, &st);
}

static int sysfs_read_whole_file(char *file, char **output)
{
	char *result = NULL;
	ssize_t numread;
	off_t size;
	int fd, ret = 0;

	*output = NULL;

	fd = open(file, O_RDONLY);
	if (fd <= 0)
		return fd;

	/* determine size */
	size = lseek(fd, 0, SEEK_END) + SYSFS_PATH_MAX;
	result = malloc(size);
	if (!result) {
		close(fd);
		return -ENOMEM;
	}

	lseek(fd, 0, SEEK_SET);
	numread = read(fd, result, size - 1);
	if (numread < 1)
		ret = -EIO;
	else {
		result[numread] = '\0';
		*output = result;
	}

	close(fd);
	return ret;
}

static int pccardctl_get_string_socket(unsigned long socket_no,
				const char *in_file, char **output)
{
	char file[SYSFS_PATH_MAX];

	snprintf(file, SYSFS_PATH_MAX,
		"/sys/class/pcmcia_socket/pcmcia_socket%lu/%s",
		 socket_no, in_file);

	return sysfs_read_whole_file(file, output);
}

static int pccardctl_get_string(unsigned long socket_no,
				const char *in_file, char **output)
{
	char file[SYSFS_PATH_MAX];

	snprintf(file, SYSFS_PATH_MAX, "/sys/bus/pcmcia/devices/%lu.0/%s",
		 socket_no, in_file);

	return sysfs_read_whole_file(file, output);
}

static int pccardctl_get_one_f(unsigned long socket_no, unsigned int dev,
			const char *in_file, unsigned int *result)
{
	char *value;
	char file[SYSFS_PATH_MAX];
	int ret = 0;

	snprintf(file, SYSFS_PATH_MAX, "/sys/bus/pcmcia/devices/%lu.%u/%s",
		 socket_no, dev, in_file);
	ret = sysfs_read_whole_file(file, &value);
	if (ret || !value)
		return -EINVAL;

	if (sscanf(value, "0x%X", result) != 1)
		ret = -EIO;

	free(value);
	return ret;
}

static int pccardctl_get_one(unsigned long socket_no, const char *in_file,
			unsigned int *result)
{
	return pccardctl_get_one_f(socket_no, 0, in_file, result);
}

static int pccardctl_get_power_device(unsigned long socket_no,
				unsigned int func)
{
	char *value;
	char file[SYSFS_PATH_MAX];
	int ret = -ENODEV;

	snprintf(file, SYSFS_PATH_MAX,
		"/sys/bus/pcmcia/devices/%lu.%u/pm_state",
		 socket_no, func);
	sysfs_read_whole_file(file, &value);
	if (value) {
		if (!strncmp(value, "off", 3))
			ret = 1;
		ret = 0;
		free(value);
	}

	return ret;
}

static int pccardctl_get_power_socket(unsigned long socket_no)
{
	char *value;
	char file[SYSFS_PATH_MAX];
	int ret = -ENODEV;

	snprintf(file, SYSFS_PATH_MAX,
		"/sys/class/pcmcia_socket/pcmcia_socket%lu/card_pm_state",
		 socket_no);
	sysfs_read_whole_file(file, &value);
	if (value) {
		if (!strncmp(value, "off", 3))
			ret = 1;
		ret = 0;
		free(value);
	}

	return ret;
}


static int pccardctl_ident(unsigned long socket_no)
{
	char *prod_id[4];
	int valid_prod_id = 0;
	int i;
	unsigned int manf_id, card_id;

	if (!pccardctl_socket_exists(socket_no))
		return -ENODEV;

	for (i = 1; i <= 4; i++) {
		char file[SYSFS_PATH_MAX];
		snprintf(file, SYSFS_PATH_MAX, "prod_id%u", i);

		pccardctl_get_string(socket_no, file, &prod_id[i-1]);
		if (prod_id[i-1])
			valid_prod_id++;
	}

	if (valid_prod_id) {
		printf("  product info: ");
		for (i = 0; i < 4; i++) {
			printf("%s\"%s\"", (i > 0) ? ", " : "",
			       prod_id[i] ? prod_id[i] : "");
			if (prod_id[i])
				free(prod_id[i]);
		}
		printf("\n");
	} else
		printf("  no product info available\n");

	if (!pccardctl_get_one(socket_no, "manf_id", &manf_id))
		if (!pccardctl_get_one(socket_no, "card_id", &card_id))
			printf("  manfid: 0x%04x, 0x%04x\n", manf_id, card_id);

	if (!pccardctl_get_one(socket_no, "func_id", &manf_id)) {
		const char *s = "unknown";
		if (manf_id < sizeof(fn)/sizeof(*fn))
			s = fn[manf_id];
		printf("  function: %d (%s)\n", manf_id, s);
	}

	return 0;
}

static int pccardctl_info(unsigned long socket_no)
{
	int i;
	unsigned int manf_id, card_id, func_id;
	char *prod_id;

	if (!pccardctl_socket_exists(socket_no))
		return -ENODEV;

	for (i = 1; i <= 4; i++) {
		char file[SYSFS_PATH_MAX];
		snprintf(file, SYSFS_PATH_MAX, "prod_id%u", i);

		pccardctl_get_string(socket_no, file, &prod_id);

		printf("PRODID_%d=\"%s\"\n", i, (prod_id) ? prod_id : "");

		free(prod_id);
		prod_id = NULL;
	}

	printf("MANFID=%04x,%04x\n",
	       (!pccardctl_get_one(socket_no, "manf_id",
			       &manf_id)) ? manf_id : 0,
	       (!pccardctl_get_one(socket_no, "card_id",
			       &card_id)) ? card_id : 0);

	printf("FUNCID=%d\n",
	       (!pccardctl_get_one(socket_no, "func_id",
			       &func_id)) ? func_id : 255);

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

	pccardctl_get_string_socket(socket_no, "card_type", &card_type);
	if (!card_type) {
		printf("  no card\n");
		return 0;
	}

	pccardctl_get_string_socket(socket_no, "card_voltage", &card_voltage);

	strncmp(card_type, "16", 2) ? is_cardbus = 0 : 1;

	printf("  %s %s %s", card_voltage, card_type, is_cardbus ?
		"CardBus card" : "PC Card");
	free(card_type);
	free(card_voltage);

	susp = pccardctl_get_power_socket(socket_no);
	if (susp > 0)
		printf(" [suspended]");
	printf("\n");

	if (is_cardbus)
		return 0;

	for (i = 0; i < 4; i++) {
		int function;
		char drv[SYSFS_PATH_MAX];
		char file[SYSFS_PATH_MAX];
		if (pccardctl_get_one_f(socket_no, i, "function", &function))
			continue;

		printf("  Subdevice %u (function %u)", i, function);

		snprintf(file, SYSFS_PATH_MAX,
			"/sys/bus/pcmcia/devices/%lu.%u/driver",
			socket_no, i);
		ret = readlink(file, drv, sizeof(drv) - 1);
		if (ret <= 0)
			printf(" [unbound]");
		else if (ret > 0) {
			drv[ret] = '\0';
			printf(" bound to driver \"%s\"", basename(drv));
		}

		susp = pccardctl_get_power_device(socket_no, i);
		if (susp > 0)
			printf(" [suspended]");

		printf("\n");
	}

	return 0;
}

static void print_header(void)
{
	printf("pcmciautils %s\n", PCMCIAUTILS_VERSION);
	printf("Copyright (C) 2004-2011 Dominik Brodowski, "
		"(C) 1999 David A. Hinds\n");
	printf("Report errors and bugs to <linux-pcmcia@lists.infradead.org>,"
		"please.\n");
}

static char *cmdname[] = {
	"ls", /* needs to be first */
	"insert",
	"eject",
	"suspend",
	"resume",
	"reset",
	"info",
	"status",
	"ident",
};

static void print_help(void)
{
	unsigned int i;

	printf("Usage: pccardctl COMMAND\n");
	printf("Supported commands are:\n");
	for (i = 0; i < sizeof(cmdname)/sizeof(cmdname[0]); i++)
		printf("\t%s\n", cmdname[i]);
}

static void print_unknown_arg(void)
{
	print_header();
	printf("invalid or unknown argument\n");
	print_help();
}

static struct option pccardctl_opts[] = {
	{ .name = "version", .has_arg = no_argument, .flag = NULL, .val = 'V'},
	{ .name = "help",    .has_arg = no_argument, .flag = NULL, .val = 'h'},
	{ .name = "verbose", .has_arg = no_argument, .flag = NULL, .val = 'v'},
	{ 0, 0, 0, 0 }
};

static void lspcmcia_socket_available_resources(unsigned long socket_no,
						char *which)
{
	char file[SYSFS_PATH_MAX];
	int ret, length, first = 0;
	char *sep;
	char *result = NULL;

	snprintf(file, SYSFS_PATH_MAX,
	"/sys/class/pcmcia_socket/pcmcia_socket%lu/available_resources_%s",
		socket_no, which);

	printf("\t\t\tAvailable %s:\t", which[0] == 'i' ? "ioports" : "iomem");

	ret = sysfs_read_whole_file(file, &result);
	if (ret)
		goto close_out;

	ret = 0;
	do {
		sep = strchr(&result[ret], '\n');
		if (sep) {
			length = sep - &result[ret];
			if (length > SYSFS_PATH_MAX)
				break;
			memcpy(file, &result[ret], length);
			file[length] = '\0';
			printf("%s\n\t\t\t\t\t\t", file);
			first++;
			ret += length + 1;
		}
	} while (sep);
	printf("%s\n", &result[ret]);
	first++;
	free(result);

 close_out:
	if (!first)
		printf("--none--\n");
	return;
}

static void lspcmcia_socket(unsigned long socket_no, int verbose, char *driver)
{
	char *card_voltage, *card_vpp, *card_vcc, *ready;
	int pm_state;

	pm_state = pccardctl_get_power_socket(socket_no);
	pccardctl_get_string_socket(socket_no,
				"available_resources_setup_done", &ready);

	printf("\tConfiguration:\tstate: %s\tready: %s\n",
		pm_state ? "suspended" : "on", ready ? ready : "unknown");

	pccardctl_get_string_socket(socket_no, "card_voltage", &card_voltage);
	pccardctl_get_string_socket(socket_no, "card_vpp", &card_vpp);
	pccardctl_get_string_socket(socket_no, "card_vcc", &card_vcc);
	if (card_voltage && card_vpp && card_vcc)
		printf("\t\t\tVoltage: %s Vcc: %s Vpp: %s\n",
			card_voltage, card_vcc, card_vpp);
	free(card_voltage);
	free(card_vpp);
	free(card_vcc);
	free(ready);

	if (verbose > 1) {
		char *irq_mask_s;
		int i, irqs = 0;
		unsigned int irq_mask;

		pccardctl_get_string_socket(socket_no, "card_irq_mask",
					&irq_mask_s);
		if (irq_mask_s && sscanf(irq_mask_s, "0x%X", &irq_mask) == 1) {
			printf("\t\t\tAvailable IRQs: ");
			for (i = 0; i < 32; i++) {
				if (!(irq_mask & (1 << i)))
					continue;
				if (irqs)
					printf(", ");
				printf("%d", i);
				irqs++;
			}
			if (!irqs)
				printf("none");
			printf("\n");
		}
		free(irq_mask_s);
		lspcmcia_socket_available_resources(socket_no, "io");
		lspcmcia_socket_available_resources(socket_no, "mem");
	}
	return;
}

static void lspcmcia_device_resources(unsigned long socket_no, int fun)
{
	char file[SYSFS_PATH_MAX];
	int ret, length;
	char *sep;
	char *result = NULL;

	snprintf(file, SYSFS_PATH_MAX,
		"/sys/bus/pcmcia/devices/%lu.%u/resources", socket_no, fun);

	ret = sysfs_read_whole_file(file, &result);
	if (ret)
		return;

	ret = 0;
	printf("\t\t\t");
	do {
		sep = strchr(&result[ret], '\n');
		if (sep) {
			length = sep - &result[ret];
			if (length > SYSFS_PATH_MAX)
				break;
			memcpy(file, &result[ret], length);
			file[length] = '\0';
			printf("%s\n\t\t\t", file);
			ret += length + 1;
		}
	} while (sep);
	printf("%s\n", &result[ret]);

	free(result);
	return;
}

static int lspcmcia(unsigned long socket_no, int verbose)
{
	char file[SYSFS_PATH_MAX];
	char drv_s[SYSFS_PATH_MAX];
	char dev_s[SYSFS_PATH_MAX];
	char *drv;
	char *dev;
	char *res;
	int ret, i;

	if (!pccardctl_socket_exists(socket_no))
		return -ENODEV;

	snprintf(file, SYSFS_PATH_MAX,
		"/sys/class/pcmcia_socket/pcmcia_socket%lu/device", socket_no);
	ret = readlink(file, dev_s, sizeof(dev_s) - 1);
	if (ret > 0) {
		dev_s[ret] = '\0';
		dev = basename(dev_s);
	} else {
		snprintf(file, SYSFS_PATH_MAX,
			"/sys/class/pcmcia_socket/pcmcia_socket%lu", socket_no);
		ret = readlink(file, dev_s, sizeof(dev_s) - 1);
		if (ret <= 0)
			return -ENODEV;
		dev_s[ret] = '\0';
		dev = basename(dirname(dev_s));
	}

	snprintf(file, SYSFS_PATH_MAX,
		"/sys/class/pcmcia_socket/pcmcia_socket%lu/device/driver",
		socket_no);
	ret = readlink(file, drv_s, sizeof(drv_s) - 1);
	if (ret <= 0) {
		snprintf(file, SYSFS_PATH_MAX,
			"/sys/class/pcmcia_socket/pcmcia_socket%lu/../driver",
			socket_no);
		ret = readlink(file, drv_s, sizeof(drv_s) - 1);
		if (ret <= 0)
			return -ENODEV;
	}
	drv_s[ret] = '\0';
	drv = basename(drv_s);

	printf("Socket %lu Bridge:   \t[%s] \t(bus ID: %s)\n",
		socket_no, drv, dev);
	if (verbose)
		lspcmcia_socket(socket_no, verbose, drv);

	pccardctl_get_string_socket(socket_no, "card_type", &res);
	if (!res)
		return 0;

	if (!strncmp(res, "32", 2)) {
		printf("  CardBus card -- see \"lspci\" "
			"for more information\n");
		free(res);
		return 0;
	}
	free(res);

	for (i = 0; i < 4; i++) {
		int function;

		if (pccardctl_get_one_f(socket_no, i, "function", &function))
			continue;

		printf("Socket %lu Device %d:\t", socket_no, i);

		snprintf(file, SYSFS_PATH_MAX,
			"/sys/bus/pcmcia/devices/%lu.%u/driver",
			socket_no, i);
		ret = readlink(file, drv_s, sizeof(drv_s) - 1);
		if (ret <= 0)
			printf("[-- no driver --]\t");
		else if (ret > 0) {
			drv_s[ret] = '\0';
			printf("[%s]\t\t", basename(drv_s));
		}
		printf("(bus ID: %lu.%d)\n", socket_no, i);

		if (verbose) {
			int j;
			unsigned int manf_id, card_id;
			int pm_state = pccardctl_get_power_device(socket_no, i);

			printf("\tConfiguration:\tstate: %s\n",
				pm_state ? "suspended" : "on");
			lspcmcia_device_resources(socket_no, i);

			printf("\tProduct Name:   ");
			for (j = 1; j <= 4; j++) {
				snprintf(file, SYSFS_PATH_MAX, "prod_id%d", j);
				pccardctl_get_string(socket_no, file, &res);
				if (res)
					printf("%s ", res);
				free(res);
			}
			printf("\n");

			printf("\tIdentification:\t");
			if (!pccardctl_get_one(socket_no, "manf_id", &manf_id))
				if (!pccardctl_get_one(socket_no, "card_id",
							&card_id))
					printf("manf_id: 0x%04x\t"
						"card_id: 0x%04x\n\t\t\t",
						manf_id, card_id);
			if (!pccardctl_get_one(socket_no, "func_id",
						&manf_id)) {
				const char *s = "unknown";
				if (manf_id < sizeof(fn)/sizeof(*fn))
					s = fn[manf_id];
				printf("function: %d (%s)\n\t\t\t", manf_id, s);
			}
			for (j = 1; j <= 4; j++) {
				snprintf(file, SYSFS_PATH_MAX, "prod_id%d", j);
				pccardctl_get_string(socket_no, file, &res);
				if (res)
					printf("prod_id(%u): \"%s\" "
						"(0x%08x)\n", j, res,
						crc32(res, strlen(res)));
				else
					printf("prod_id(%u): --- (---)\n", j);
				if (j < 4)
					printf("\t\t\t");
				free(res);
			}
		}
	}

	return 0;
}

enum {
	PCCARDCTL_LSPCMCIA, /* needs to be first */
	PCCARDCTL_INSERT,
	PCCARDCTL_EJECT,
	PCCARDCTL_SUSPEND,
	PCCARDCTL_RESUME,
	PCCARDCTL_RESET,
	PCCARDCTL_INFO,
	PCCARDCTL_STATUS,
	PCCARDCTL_IDENT,
	NCMD
};


int main(int argc, char **argv)
{
	extern char *optarg;
	extern int optind, opterr, optopt;
	int ret = 0;
	int verbose = 0;
	unsigned int cont = 1;
	unsigned long socket = 0;
	unsigned int socket_is_set = 0;
	char *s;
	unsigned int cmd;

	do {
		ret = getopt_long(argc, argv, "Vhvc:f:s:",
				pccardctl_opts, NULL);
		switch (ret) {
		case -1:
			cont = 0;
			break;
		case 'V':
			print_header();
			return 0;
		case 'v':
			verbose++;
			break;
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

	if (strcmp(basename(argv[0]), "lspcmcia") == 0) {
		cmd = 0;
		goto check_socket;
	}

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

 check_socket:
	if (argc == optind+2) {
		socket_is_set = 1;
		socket = strtol(argv[optind+1], &s, 0);
		if ((*argv[optind+1] == '\0') || (*s != '\0') ||
			(socket >= MAX_SOCKET)) {
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
		case PCCARDCTL_LSPCMCIA:
			ret = lspcmcia(cont, verbose);
			break;
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
			ret = pccardctl_power_socket(cont, 1);
			break;
		case PCCARDCTL_RESET:
			ret = pccardctl_power_socket(cont, 1);
			if (ret && socket_is_set)
				return ret;
			/* fall through */
		case PCCARDCTL_RESUME:
			ret = pccardctl_power_socket(cont, 0);
			break;
		case PCCARDCTL_STATUS:
			ret = pccardctl_status(cont);
			break;
		}

		if (ret && socket_is_set)
			return ret;
	}

	return 0;
}
