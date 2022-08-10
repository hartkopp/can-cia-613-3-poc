/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * llc-gw-rx.c - CAN XL CiA 613-3 receiver
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
#include "cia-613-3.h"

#define DEFAULT_TRANSFER_ID 0x242
#define NO_FCNT_VALUE 0xFFFF0000U

extern int optind, opterr, optopt;

void print_usage(char *prg)
{
	fprintf(stderr, "%s - CAN XL CiA 613-3 receiver\n\n", prg);
	fprintf(stderr, "Usage: %s [options] <src_if> <dst_if>\n", prg);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "         -t <transfer_id> (TRANSFER ID "
		"- default: 0x%03X)\n", DEFAULT_TRANSFER_ID);
	fprintf(stderr, "         -v               (verbose)\n");
}

int main(int argc, char **argv)
{
	int opt;
	unsigned int segsz = 0;
	unsigned int rxsegsz;
	unsigned int fcnt = NO_FCNT_VALUE;
	unsigned int rxfcnt;
	canid_t transfer_id = DEFAULT_TRANSFER_ID;
	int verbose = 0;

	int src, dst;
	struct sockaddr_can addr;
	struct can_filter rfilter;
	struct canxl_frame cfsrc, cfdst;
	struct llc_613_3 *llc = (struct llc_613_3 *) cfsrc.data;
	unsigned int dataptr = 0;

	int nbytes, ret;
	int sockopt = 1;
	struct timeval tv;

	while ((opt = getopt(argc, argv, "t:vh?")) != -1) {
		switch (opt) {

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

		/* read segmented source frame */
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

		if (cfsrc.len < CANXL_MIN_DLEN + LLC_613_3_SIZE) {
			printf("nbytes = %d\n", nbytes);
			fprintf(stderr, "read: no CAN XL LLC frame\n");
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

		/* common FCNT handling */
		rxfcnt = (llc->fcnt_hi<<8) + llc->fcnt_lo;
		if (fcnt == NO_FCNT_VALUE) {
			/* first reception */
			fcnt = rxfcnt;
		} else if (fcnt == rxfcnt) {
			printf("dropped frame with identical FCNT!\n");
			continue;
		}

		/* decrease length for the LLC information */
		rxsegsz = cfsrc.len - LLC_613_3_SIZE;

		if ((llc->pci & PCI_XF_MASK) == PCI_SF) {

			/* take current rxfcnt as fcnt */ 
			fcnt = rxfcnt;

			/* copy CAN XL header w/o data */
			memcpy(&cfdst, &cfsrc, CANXL_HDR_SIZE);

			/* length without the LLC information */
			cfdst.len = rxsegsz;

			/* copy CAN XL fragment data w/o LLC information */
			memcpy(&cfdst.data[0], &cfsrc.data[LLC_613_3_SIZE], cfdst.len);

			nbytes = write(dst, &cfdst, CANXL_HDR_SIZE + cfdst.len);
			if (nbytes != CANXL_HDR_SIZE + cfdst.len) {
				printf("nbytes = %d\n", nbytes);
				perror("write dst canxl_frame");
				exit(1);
			}

			if (verbose) {
				printf("%03X###%02X%02X%08X[%02X%02X%02X%02X%02X%02X](%d)\n",
				       cfdst.prio, cfdst.flags, cfdst.sdt, cfdst.af,
				       cfdst.data[0], cfdst.data[1], cfdst.data[2],
				       cfdst.data[3], cfdst.data[4], cfdst.data[5],
				       cfdst.len);
				fflush(stdout);
			}

			continue; /* wait for next frame */
		} /* SF */

		if ((llc->pci & PCI_XF_MASK) == PCI_FF) {

			if (rxsegsz <  MIN_SEG_SIZE || rxsegsz > MAX_SEG_SIZE) {
				printf("dropped LLC frame illegal fragment size!\n");
				continue;
			}

			/* take current rxfcnt as initial fcnt */ 
			fcnt = rxfcnt;

			/* copy CAN XL header w/o data */
			memcpy(&cfdst, &cfsrc, CANXL_HDR_SIZE);

			/* length without the LLC information */
			cfdst.len = rxsegsz;

			/* copy CAN XL fragment data w/o LLC information */
			memcpy(&cfdst.data[0], &cfsrc.data[LLC_613_3_SIZE], cfdst.len);

			/* set data pointer for next fragment data */
			dataptr = cfdst.len;

			/* get fragment size from first frame */
			segsz = cfdst.len;

			if (verbose) {
				printf("%03X###%02X%02X%08X[%02X%02X%02X%02X%02X%02X](%d)\n",
				       cfdst.prio, cfdst.flags, cfdst.sdt, cfdst.af,
				       cfdst.data[0], cfdst.data[1], cfdst.data[2],
				       cfdst.data[3], cfdst.data[4], cfdst.data[5],
				       cfdst.len);
				fflush(stdout);
			}

			continue; /* wait for next frame */
		} /* FF */

		if ((llc->pci & PCI_XF_MASK) == PCI_CF) {

			/* check that rxfcnt has increased */ 
			if (fcnt + 1 != rxfcnt) {
				printf("dropped CF frame wrong FCNT! (%d/%d)\n",
				       fcnt, rxfcnt);
				continue;
			}

			/* update fcnt */
			fcnt = rxfcnt;

			/* check fragment size in consecutive frames */
			if (rxsegsz != segsz) {
				printf("dropped CF frame wrong fragment size!\n");
				continue;
			}

			/* copy CAN XL fragment data w/o LLC information */
			memcpy(&cfdst.data[dataptr], &cfsrc.data[LLC_613_3_SIZE], rxsegsz);

			/* set data pointer for next fragment data */
			dataptr += rxsegsz;
			cfdst.len += rxsegsz;

			if (verbose) {
				printf("%03X###%02X%02X%08X[%02X%02X%02X%02X%02X%02X](%d)\n",
				       cfdst.prio, cfdst.flags, cfdst.sdt, cfdst.af,
				       cfdst.data[0], cfdst.data[1], cfdst.data[2],
				       cfdst.data[3], cfdst.data[4], cfdst.data[5],
				       cfdst.len);
				fflush(stdout);
			}

			continue; /* wait for next frame */
		} /* CF */

		if ((llc->pci & PCI_XF_MASK) == PCI_LF) {

			/* check that rxfcnt has increased */ 
			if (fcnt + 1 != rxfcnt) {
				printf("dropped LF frame wrong FCNT! (%d/%d)\n",
				       fcnt, rxfcnt);
				continue;
			}

			/* update fcnt */
			fcnt = rxfcnt;

			/* check fragment size in consecutive frames */
			if (rxsegsz > segsz) {
				printf("dropped LF frame wrong fragment size!\n");
				continue;
			}

			/* copy CAN XL fragment data w/o LLC information */
			memcpy(&cfdst.data[dataptr], &cfsrc.data[LLC_613_3_SIZE], rxsegsz);

			/* set data pointer for next fragment data */
			dataptr += rxsegsz;
			cfdst.len += rxsegsz;

			nbytes = write(dst, &cfdst, CANXL_HDR_SIZE + cfdst.len);
			if (nbytes != CANXL_HDR_SIZE + cfdst.len) {
				printf("nbytes = %d\n", nbytes);
				perror("write dst canxl_frame");
				exit(1);
			}

			if (verbose) {
				printf("%03X###%02X%02X%08X[%02X%02X%02X%02X%02X%02X](%d)\n",
				       cfdst.prio, cfdst.flags, cfdst.sdt, cfdst.af,
				       cfdst.data[0], cfdst.data[1], cfdst.data[2],
				       cfdst.data[3], cfdst.data[4], cfdst.data[5],
				       cfdst.len);
				fflush(stdout);
			}

			continue; /* wait for next frame */
		} /* LF */

	} /* while(1) */

	close(src);
	close(dst);

	return 0;
}
