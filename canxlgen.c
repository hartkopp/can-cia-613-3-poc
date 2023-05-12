/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * canxlgen.c - CAN XL frame generator
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#include "printframe.h"

#define DEFAULT_PRIO_ID 0x242
#define DEFAULT_GAP 2
#define DEFAULT_FROM 1
#define DEFAULT_TO 2048

extern int optind, opterr, optopt;

void print_usage(char *prg)
{
	fprintf(stderr, "%s - CAN XL frame generator\n\n", prg);
	fprintf(stderr, "Usage: %s [options] <CAN interface>\n", prg);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "         -l <from>:<to> (length of CAN XL frames "
		"- default: %d to %d\n", DEFAULT_FROM, DEFAULT_TO);
	fprintf(stderr, "         -g <ms>        (gap in milli seconds "
		"- default: %d ms)\n", DEFAULT_GAP);
	fprintf(stderr, "         -p <prio_id>   (PRIO ID "
		"- default: 0x%03X)\n", DEFAULT_PRIO_ID);
	fprintf(stderr, "         -s             (set SEC bit)\n");
	fprintf(stderr, "         -P             (create data pattern)\n");
	fprintf(stderr, "         -v             (verbose)\n");
}

int main(int argc, char **argv)
{
	int opt;
	double gap = DEFAULT_GAP;
	unsigned int from = DEFAULT_FROM;
	unsigned int to = DEFAULT_TO;
	canid_t prio = DEFAULT_PRIO_ID;
	int create_pattern = 0;
	__u8 sec_bit = 0;
	int verbose = 0;

	int s;
	struct sockaddr_can addr;
	struct timespec ts;
	struct canxl_frame cfx = {0};
	int nbytes, ret, dlen, i;
	int sockopt = 1;

	while ((opt = getopt(argc, argv, "l:g:p:sPvh?")) != -1) {
		switch (opt) {

		case 'l':
			if (sscanf(optarg, "%d:%d", &from, &to) != 2) {
				print_usage(basename(argv[0]));
				return 1;
			}
			if (from < CANXL_MIN_DLEN || to > CANXL_MAX_DLEN || from > to) {
				print_usage(basename(argv[0]));
				return 1;
			}
			break;

		case 'g':
			gap = strtod(optarg, NULL);
			break;

		case 'p':
			prio = strtoul(optarg, NULL, 16);
			if (prio & ~CANXL_PRIO_MASK) {
				print_usage(basename(argv[0]));
				return 1;
			}
			break;

		case 's':
			sec_bit = CANXL_SEC;
			break;

		case 'P':
			create_pattern = 1;
			break;

		case 'v':
			verbose = 1;
			break;

		case '?':
		case 'h':
		default:
			print_usage(basename(argv[0]));
			return 1;
			break;
		}
	}

	if (optind == argc) {
		print_usage(basename(argv[0]));
		exit(0);
	}

	ts.tv_sec = gap / 1000;
	ts.tv_nsec = (long)(((long long)(gap * 1000000)) % 1000000000LL);

	if (strlen(argv[optind]) >= IFNAMSIZ) {
		printf("Name of CAN device '%s' is too long!\n\n", argv[optind]);
		return 1;
	}

	s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (s < 0) {
		perror("socket");
		return 1;
	}
	addr.can_family = AF_CAN;
	addr.can_ifindex = if_nametoindex(argv[optind]);

	ret = setsockopt(s, SOL_CAN_RAW, CAN_RAW_XL_FRAMES,
			 &sockopt, sizeof(sockopt));
	if (ret < 0) {
		perror("sockopt CAN_RAW_XL_FRAMES");
		exit(1);
	}

	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return 1;
	}

	cfx.prio = prio;
	cfx.flags = (CANXL_XLF | sec_bit);
	cfx.sdt = 0;
	cfx.af = 0xAFAFAFAF;

	for (dlen = from; dlen <= to; dlen++) {
		cfx.len = dlen;

		/* fill data with a length depended content */
		if (create_pattern)
			for (i = 0; i < dlen; i++)
				cfx.data[i] = (dlen + i) & 0xFFU;

		/* write CAN XL frame */
		nbytes = write(s, &cfx, CANXL_HDR_SIZE + dlen);
		if (nbytes != CANXL_HDR_SIZE + dlen) {
			printf("nbytes = %d\n", nbytes);
			perror("write can_frame");
			exit(1);
		}

		if (verbose)
			printxlframe(&cfx);

		if (gap)
			if (nanosleep(&ts, NULL))
				return 1;
	}

	close(s);

	return 0;
}
