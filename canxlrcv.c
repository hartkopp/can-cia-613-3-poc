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

extern int optind, opterr, optopt;

static void printxlframe(struct canxl_frame *cfx)
{
	int i;

	/* print prio and CAN XL header content */
	printf("%03X###%02X%02X%08X",
	       cfx->prio, cfx->flags, cfx->sdt, cfx->af);

	/* print up to 8 data bytes */
	for (i = 0; i < cfx->len && i < 8; i++)
		printf("%02X", cfx->data[i]);

	/* print CAN XL data length */
	printf("(%d)\n", cfx->len);
	fflush(stdout);
}

void print_usage(char *prg)
{
	fprintf(stderr, "%s - CAN XL frame receiver\n\n", prg);
	fprintf(stderr, "Usage: %s [options] <CAN interface>\n", prg);
	fprintf(stderr, "         -P (check data pattern)\n");
}

int main(int argc, char **argv)
{
	int opt;
	int s;
	struct sockaddr_can addr;
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

	while (1) {

		nbytes = read(s, &can.xl, sizeof(struct canxl_frame));
		if (nbytes < 0) {
			perror("read");
			return 1;
		}
		
		if (ioctl(s, SIOCGSTAMP, &tv) < 0) {
			perror("SIOCGSTAMP");
			return 1;
		} else {
			printf("(%ld.%06ld) %s ",
			       tv.tv_sec, tv.tv_usec,
			       argv[optind]);
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

		if (nbytes != sizeof(struct can_frame) &&
		    nbytes != sizeof(struct canfd_frame)) {
			fprintf(stderr, "read: incomplete CAN(FD) frame\n");
			return 1;
		} else {
			if (can.fd.can_id & CAN_EFF_FLAG)
				printf("%8X  ", can.fd.can_id & CAN_EFF_MASK);
			else
				printf("%3X  ", can.fd.can_id & CAN_SFF_MASK);

			printf("[%d] ", can.fd.len);

			for (i = 0; i < can.fd.len; i++) {
				printf("%02X ", can.fd.data[i]);
			}
			if (can.fd.can_id & CAN_RTR_FLAG)
				printf("remote request");
			printf("\n");
			fflush(stdout);
		}
	}

	close(s);

	return 0;
}
