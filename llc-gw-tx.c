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

#include <linux/sockios.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include "cia-613-3.h"

#define DEFAULT_TRANSFER_ID 0x242

extern int optind, opterr, optopt;

void print_usage(char *prg)
{
	fprintf(stderr, "%s - CAN XL CiA 613-3 sender\n\n", prg);
	fprintf(stderr, "Usage: %s [options] <src_if> <dst_if>\n", prg);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "         -f <fragsize>     (fragment size "
		"- default: %d bytes)\n", DEFAULT_FRAG_SIZE);
	fprintf(stderr, "         -t <transfer_id> (TRANSFER ID "
		"- default: 0x%03X)\n", DEFAULT_TRANSFER_ID);
	fprintf(stderr, "         -v               (verbose)\n");
}

int main(int argc, char **argv)
{
	int opt;
	unsigned int fragsz = DEFAULT_FRAG_SIZE;
	unsigned int fcnt = 0;
	canid_t transfer_id = DEFAULT_TRANSFER_ID;
	int verbose = 0;

	int src, dst;
	struct sockaddr_can addr;
	struct can_filter rfilter;
	struct canxl_frame cfsrc, cfdst;
	struct llc_613_3 *llc = (struct llc_613_3 *) cfdst.data;
	unsigned int dataptr = 0;

	int nbytes, ret;
	int sockopt = 1;
	struct timeval tv;

	while ((opt = getopt(argc, argv, "f:t:vh?")) != -1) {
		switch (opt) {

		case 'f':
			fragsz = strtoul(optarg, NULL, 10);
			if (fragsz < MIN_FRAG_SIZE || fragsz > MAX_FRAG_SIZE) {
				print_usage(basename(argv[0]));
				return 1;
			}
			break;

		case 't':
			transfer_id = strtoul(optarg, NULL, 16);
			if (transfer_id & ~CANXL_PRIO_MASK) {
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

	rfilter.can_id = transfer_id;
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

			printf("%03X###%02X%02X%08X[%02X%02X%02X%02X%02X%02X](%d)\n",
			       cfsrc.prio, cfsrc.flags, cfsrc.sdt, cfsrc.af,
			       cfsrc.data[0], cfsrc.data[1], cfsrc.data[2],
			       cfsrc.data[3], cfsrc.data[4], cfsrc.data[5],
			       cfsrc.len);
			fflush(stdout);
		}

		/* check for single frame */
		if (cfsrc.len <= fragsz) {

			/* copy CAN XL header w/o data */
			memcpy(&cfdst, &cfsrc, CANXL_HDR_SIZE);

			/* fill LLC information */
			llc->pci = PCI_SF;
			llc->res = 0;
			llc->fcnt_hi = (fcnt>>8) & 0xFF;
			llc->fcnt_lo = fcnt & 0xFF;

			/* copy CAN XL fragment data */
			memcpy(&cfdst.data[4], &cfsrc.data[0], cfsrc.len);

			/* increase length for the LLC information */
			cfdst.len += LLC_613_3_SIZE;

			nbytes = write(dst, &cfdst, CANXL_HDR_SIZE + cfdst.len);
			if (nbytes != CANXL_HDR_SIZE + cfdst.len) {
				printf("nbytes = %d\n", nbytes);
				perror("write SF dst canxl_frame");
				exit(1);
			}

			/* update FCNT */
			fcnt++;
			fcnt &= 0xFFFFU;

			if (verbose) {
				printf("%03X###%02X%02X%08X[%02X%02X%02X%02X%02X%02X](%d)\n",
				       cfdst.prio, cfdst.flags, cfdst.sdt, cfdst.af,
				       cfdst.data[0], cfdst.data[1], cfdst.data[2],
				       cfdst.data[3], cfdst.data[4], cfdst.data[5],
				       cfdst.len);
				fflush(stdout);
			}

			continue; /* wait for next frame */
		}

		/* send fragmented frame(s) */
		memcpy(&cfdst, &cfsrc, sizeof(struct canxl_frame));

		for (dataptr = 0; dataptr < cfsrc.len; dataptr += fragsz) {

			/* copy CAN XL header w/o data */
			memcpy(&cfdst, &cfsrc, CANXL_HDR_SIZE);

			/* fill LLC information */
			llc->res = 0;
			llc->fcnt_hi = (fcnt>>8) & 0xFF;
			llc->fcnt_lo = fcnt & 0xFF;

			/* start of fragmentation => set FF */
			if (dataptr == 0)
				llc->pci = PCI_FF;
			else
				llc->pci = PCI_CF;

			/* copy CAN XL fragment data */
			if (cfsrc.len - dataptr > fragsz) {
				/* FF / CF */
				memcpy(&cfdst.data[4], &cfsrc.data[dataptr], fragsz);
				/* increase length for the LLC information */
				cfdst.len = fragsz + LLC_613_3_SIZE;
			} else {
				/* LF */
				llc->pci = PCI_LF;
				memcpy(&cfdst.data[4], &cfsrc.data[dataptr], cfsrc.len - dataptr);
				cfdst.len = cfsrc.len - dataptr + LLC_613_3_SIZE;
			}

			nbytes = write(dst, &cfdst, CANXL_HDR_SIZE + cfdst.len);
			if (nbytes != CANXL_HDR_SIZE + cfdst.len) {
				printf("nbytes = %d\n", nbytes);
				perror("write non-SF dst canxl_frame");
				exit(1);
			}

			/* update FCNT */
			fcnt++;
			fcnt &= 0xFFFFU;

			if (verbose) {
				printf("%03X###%02X%02X%08X[%02X%02X%02X%02X%02X%02X](%d)\n",
				       cfdst.prio, cfdst.flags, cfdst.sdt, cfdst.af,
				       cfdst.data[0], cfdst.data[1], cfdst.data[2],
				       cfdst.data[3], cfdst.data[4], cfdst.data[5],
				       cfdst.len);
				fflush(stdout);
			}
		}
	}
	close(src);
	close(dst);

	return 0;
}
