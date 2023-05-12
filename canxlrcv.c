/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * canxlrcv.c - CAN XL frame receiver
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include <linux/sockios.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#include "printframe.h"

#define ANYDEV "any"

extern int optind, opterr, optopt;

void print_usage(char *prg)
{
	fprintf(stderr, "%s - CAN XL frame receiver\n\n", prg);
	fprintf(stderr, "Usage: %s [options] <CAN interface>\n", prg);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "         -P (check data pattern)\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Use interface name '%s' to receive from all CAN interfaces.\n", ANYDEV);
}

int main(int argc, char **argv)
{
	int opt;
	int s;
	struct sockaddr_can addr;
	struct ifreq ifr;
	int ifindex = 0;
	int max_devname_len = 0; /* to prevent frazzled device name output */
	int nbytes, ret, i;
	int sockopt = 1;
	int check_pattern = 0;
	struct timeval tv;
	union {
		struct can_frame cc;
		struct canfd_frame fd;
		struct canxl_frame xl;
	} can;

	while ((opt = getopt(argc, argv, "Ph?")) != -1) {
		switch (opt) {

		case 'P':
			check_pattern = 1;
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

	if (strlen(argv[optind]) >= IFNAMSIZ) {
		printf("Name of CAN device '%s' is too long!\n\n", argv[optind]);
		return 1;
	}

	s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (s < 0) {
		perror("socket");
		return 1;
	}

	ret = setsockopt(s, SOL_CAN_RAW, CAN_RAW_XL_FRAMES,
			 &sockopt, sizeof(sockopt));
	if (ret < 0) {
		perror("sockopt CAN_RAW_XL_FRAMES");
		return 1;
	}

	if (strcmp(argv[optind], ANYDEV) != 0) {
		strcpy(ifr.ifr_name, argv[optind]);
		ioctl(s, SIOCGIFINDEX, &ifr);
		ifindex = ifr.ifr_ifindex;
	}

	addr.can_family = AF_CAN;
	addr.can_ifindex = ifindex;

	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return 1;
	}

	while (1) {
		socklen_t len = sizeof(addr);

		nbytes = recvfrom(s, &can.xl, sizeof(struct canxl_frame),
				  0, (struct sockaddr*)&addr, &len);
		if (nbytes < 0) {
			perror("read");
			return 1;
		}
		
		if (ioctl(s, SIOCGSTAMP, &tv) < 0) {
			perror("SIOCGSTAMP");
			return 1;
		} else {
			printf("(%ld.%06ld) ", tv.tv_sec, tv.tv_usec);
		}

		ifr.ifr_ifindex = addr.can_ifindex;
		if (ioctl(s, SIOCGIFNAME, &ifr) < 0) {
			perror("SIOCGIFNAME");
			return 1;
		} else {
			if (max_devname_len < (int)strlen(ifr.ifr_name))
				max_devname_len = strlen(ifr.ifr_name);
			printf("%*s ", max_devname_len, ifr.ifr_name);
		}

		if (nbytes < CANXL_HDR_SIZE + CANXL_MIN_DLEN) {
			fprintf(stderr, "read: no CAN frame\n");
			return 1;
		}

		if (can.xl.flags & CANXL_XLF) {
			if (nbytes != CANXL_HDR_SIZE + can.xl.len) {
				printf("nbytes = %d\n", nbytes);
				fprintf(stderr, "read: no CAN XL frame\n");
				return 1;
			}

			if (check_pattern) {
				for (i = 0; i < can.xl.len; i++) {
					if (can.xl.data[i] != ((can.xl.len + i) & 0xFFU)) {
						fprintf(stderr, "check pattern failed %02X %04X\n",
							can.xl.data[i], can.xl.len + i);
						return 1;
					}
				}
			}
			printxlframe(&can.xl);
			continue;
		}

		if (nbytes == CANFD_MTU) {
			printfdframe(&can.fd);
			continue;
		}

		if (nbytes == CAN_MTU) {
			printccframe(&can.cc);
			continue;
		}

		fprintf(stderr, "read: incomplete CAN(FD) frame\n");
		return 1;
	}

	close(s);

	return 0;
}
