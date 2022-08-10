/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * llc-gw-tx.c - CAN XL CiA 613-3 sender
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

#define DEFAULT_SEG_SIZE 128
#define MIN_SEG_SIZE 64
#define MAX_SEG_SIZE 1024
#define DEFAULT_PRIO_ID 0x242

extern int optind, opterr, optopt;

void print_usage(char *prg)
{
	fprintf(stderr, "%s - CAN XL CiA 613-3 sender\n\n", prg);
	fprintf(stderr, "Usage: %s [options] <src_if> <dst_if>\n", prg);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "         -s <segsize>   (segment size "
		"- default: %d bytes)\n", DEFAULT_SEG_SIZE);
	fprintf(stderr, "         -p <prio_id>   (PRIO ID "
		"- default: 0x%03X)\n", DEFAULT_PRIO_ID);
	fprintf(stderr, "         -v             (verbose)\n");
}

int main(int argc, char **argv)
{
	int opt;
	unsigned int segsz = DEFAULT_SEG_SIZE;
	canid_t prio = DEFAULT_PRIO_ID;
	int verbose = 0;

	int src, dst;
	struct sockaddr_can addr;
	struct can_filter rfilter;
	struct canxl_frame cfsrc, cfdst;
	int nbytes, ret;
	int sockopt = 1;
	struct timeval tv;

	while ((opt = getopt(argc, argv, "s:p:vh?")) != -1) {
		switch (opt) {

		case 's':
			segsz = strtoul(optarg, NULL, 10);
			if (segsz < MIN_SEG_SIZE || segsz > MAX_SEG_SIZE) {
				print_usage(basename(argv[0]));
				return 1;
			}
			break;

		case 'p':
			prio = strtoul(optarg, NULL, 16);
			if (prio & ~CANXL_PRIO_MASK) {
				print_usage(basename(argv[0]));
				return 1;
			}
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

	if (argc - optind != 2) {
		print_usage(basename(argv[0]));
		exit(0);
	}

	if (strlen(argv[optind]) >= IFNAMSIZ) {
		printf("Name of CAN device '%s' is too long!\n\n", argv[optind]);
		return 1;
	}

	if (strlen(argv[optind + 1]) >= IFNAMSIZ) {
		printf("Name of CAN device '%s' is too long!\n\n", argv[optind]);
		return 1;
	}

	src = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (src < 0) {
		perror("src socket");
		return 1;
	}
	addr.can_family = AF_CAN;
	addr.can_ifindex = if_nametoindex(argv[optind]);

	ret = setsockopt(src, SOL_CAN_RAW, CAN_RAW_XL_FRAMES,
			 &sockopt, sizeof(sockopt));
	if (ret < 0) {
		perror("src sockopt CAN_RAW_XL_FRAMES");
		exit(1);
	}

	rfilter.can_id = prio;
	rfilter.can_mask = CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_SFF_MASK;
	ret = setsockopt(src, SOL_CAN_RAW, CAN_RAW_FILTER, &rfilter, sizeof(rfilter));
	if (ret < 0) {
		perror("src sockopt CAN_RAW_FILTER");
		exit(1);
	}

	if (bind(src, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return 1;
	}

	dst = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (dst < 0) {
		perror("dst socket");
		return 1;
	}
	addr.can_family = AF_CAN;
	addr.can_ifindex = if_nametoindex(argv[optind + 1]);

	ret = setsockopt(dst, SOL_CAN_RAW, CAN_RAW_XL_FRAMES,
			 &sockopt, sizeof(sockopt));
	if (ret < 0) {
		perror("dst sockopt CAN_RAW_XL_FRAMES");
		exit(1);
	}

	if (bind(dst, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return 1;
	}

	while (1) {

		/* read source frame */
		nbytes = read(src, &cfsrc, sizeof(struct canxl_frame));
		if (nbytes < 0) {
			perror("read");
			return 1;
		}

		if (nbytes < CANXL_HDR_SIZE + CANXL_MIN_DLEN) {
			fprintf(stderr, "read: no CAN frame\n");
			return 1;
		}

		if (!(cfsrc.flags & CANXL_XLF)) {
			fprintf(stderr, "read: no CAN XL frame flag\n");
			return 1;
		}

		if (nbytes != CANXL_HDR_SIZE + cfsrc.len) {
			printf("nbytes = %d\n", nbytes);
			fprintf(stderr, "read: no CAN XL frame len\n");
			return 1;
		}

		if (verbose) {

			if (ioctl(src, SIOCGSTAMP, &tv) < 0) {
				perror("SIOCGSTAMP");
				return 1;
			} else {
				printf("(%ld.%06ld) %s ",
				       tv.tv_sec, tv.tv_usec,
				       argv[optind]);
			}

			printf("%03X###%02X%02X%08X(%d)\n",
			       cfsrc.prio, cfsrc.flags, cfsrc.sdt,
			       cfsrc.af, cfsrc.len);
			fflush(stdout);
		}

		/* send segmented frame(s) */
		memcpy(&cfdst, &cfsrc, sizeof(struct canxl_frame));

		nbytes = write(dst, &cfdst, CANXL_HDR_SIZE + cfdst.len);
		if (nbytes != CANXL_HDR_SIZE + cfdst.len) {
			printf("nbytes = %d\n", nbytes);
			perror("write dst canxl_frame");
			exit(1);
		}

#if 0
		for (dlen = from; dlen <= to; dlen++) {
			cfx.len = dlen;

			nbytes = write(s, &cfx, CANXL_HDR_SIZE + dlen);
			if (nbytes != CANXL_HDR_SIZE + dlen) {
				printf("nbytes = %d\n", nbytes);
				perror("write can_frame");
				exit(1);
			}

			if (verbose)
				printf("%03X###%02X%02X%08X(%d)\n",
				       cfx.prio, cfx.flags, cfx.sdt, cfx.af, dlen);

		}
#endif
	}
	close(src);
	close(dst);

	return 0;
}
