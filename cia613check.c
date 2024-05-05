/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * cia613check.c - CAN XL CiA 613-3 protocol checker
 *
 * uses the defragmentation from cia613join to check
 * the buffer and PDU discard processes specified in
 * CAN CiA 613-3 document v009.
 *
 * CAN CiA plugfest Baden-Baden 2024-05-16
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
#include <arpa/inet.h> /* for network byte order conversion */

#include <linux/sockios.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include "cia-613-3.h"
#include "printframe.h"

#define DEFAULT_BUFFERS 3
#define NO_FCNT_VALUE 0x0FFF0000U
#define BUFMEMSZ 16 /* for 15 TIDs + invalid index */
#define TESTDATA_PRIO_BASE 0x400
#define DEBUG_ID_PRIO_BASE 0x200 /* Bosch 0x100, VW 0x200, Vector 0x300 */
#define TID_MASK 0x03F

extern int optind, opterr, optopt;

 /* 15 buffers for 64 possible TID lower bits
  * zero -> no valid TID from plugfest testcases.
  * Therefore index 0 of the 16 buffers is unused.
  */
const unsigned int tid2bufidx[] = {
	 1,  2,  3,  0,  0,  0,  0,  4, /* 0x00 .. 0x07 */
	 5,  6,  0,  0,  0,  0,  0,  0, /* 0x08 .. 0x0F */
	 7,  8,  9,  0,  0,  0,  0,  0, /* 0x10 .. 0x17 */
	 0,  0,  0,  0,  0,  0,  0,  0, /* 0x18 .. 0x1F */
	10, 11, 12,  0,  0,  0,  0,  0, /* 0x20 .. 0x27 */
	 0,  0,  0,  0,  0,  0,  0,  0, /* 0x28 .. 0x2F */
	13, 14, 15,  0,  0,  0,  0,  0, /* 0x30 .. 0x37 */
	 0,  0,  0,  0,  0,  0,  0,  0, /* 0x38 .. 0x3F */
};

void print_usage(char *prg)
{
	fprintf(stderr, "%s - CAN XL CiA 613-3 protocol checker\n\n", prg);
	fprintf(stderr, "Usage: %s [options] <canxl_if>\n", prg);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "         -b <buffers> (default: %d)\n", DEFAULT_BUFFERS);
	fprintf(stderr, "         -v           (verbose)\n");
}

int sendstate(int can_if, unsigned int nn, unsigned int tid)
{
	int nbytes;
	struct canxl_frame state = {
		.prio = DEBUG_ID_PRIO_BASE | tid,
		.flags = CANXL_XLF,
		.sdt = 0,
		.len = 1,
		.af = 0,
	};

	state.data[0] = nn;

	nbytes = write(can_if, &state, CANXL_HDR_SIZE + state.len);
	if (nbytes == CANXL_HDR_SIZE + state.len)
		return 0;

	printf("nbytes = %d\n", nbytes);
	perror("sendstate");
	return nbytes;
}

int framecmp(struct canxl_frame *s1, struct canxl_frame *s2)
{
	if (s1->len != s2->len)
		return s1->len - s2->len;

	return memcmp(s1, s2, CANXL_HDR_SIZE + s1->len);
}

int main(int argc, char **argv)
{
	int opt;
	int can_if;
	struct sockaddr_can addr;
	struct can_filter rfilter;
	int nbytes, ret;
	int sockopt = 1;
	struct timeval tv;

	unsigned int buffers = DEFAULT_BUFFERS;
	unsigned int verbose = 0;

	unsigned int rxfragsz;
	unsigned int rxfcnt;
	struct canxl_frame cf;
	struct llc_613_3 *llc = (struct llc_613_3 *) cf.data;

	unsigned int tid; /* cf.prio & TID_MASK */
	unsigned int bufidx; /* buffer index of valid plugfest TID */
	unsigned int nn; /* notification number */
	struct canxl_frame testdata[BUFMEMSZ] = {0};
	struct canxl_frame pdudata[BUFMEMSZ] = {0};
	unsigned int dataptr[BUFMEMSZ] = {0};
	unsigned int fcnt[BUFMEMSZ]; /* init when testdata is received */

	while ((opt = getopt(argc, argv, "b:vh?")) != -1) {
		switch (opt) {

		case 'b':
			buffers = strtoul(optarg, NULL, 10);
			if (buffers > BUFMEMSZ -1) {
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

	/* can_if is a mandatory parameters */
	if (argc - optind != 1) {
		print_usage(basename(argv[0]));
		exit(0);
	}

	/* can_if */
	if (strlen(argv[optind]) >= IFNAMSIZ) {
		printf("Name of can_if CAN device '%s' is too long!\n\n",
		       argv[optind]);
		return 1;
	}

	/* open can_if socket */
	can_if = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (can_if < 0) {
		perror("can_if socket");
		return 1;
	}
	addr.can_family = AF_CAN;
	addr.can_ifindex = if_nametoindex(argv[optind]);
	if (addr.can_ifindex <= 0) {
		perror("can_if");
		exit(1);
	}

	/* enable CAN XL frames */
	ret = setsockopt(can_if, SOL_CAN_RAW, CAN_RAW_XL_FRAMES,
			 &sockopt, sizeof(sockopt));
	if (ret < 0) {
		perror("can_if sockopt CAN_RAW_XL_FRAMES");
		exit(1);
	}

	/* filter prio for 0x000 - 0x03F and 0x400 - 0x43F */
	rfilter.can_id = 0;
	rfilter.can_mask = (CAN_EFF_FLAG | CAN_RTR_FLAG | CANXL_PRIO_MASK) - TESTDATA_PRIO_BASE - TID_MASK;
	ret = setsockopt(can_if, SOL_CAN_RAW, CAN_RAW_FILTER,
			 &rfilter, sizeof(rfilter));
	if (ret < 0) {
		perror("can_if sockopt CAN_RAW_FILTER");
		exit(1);
	}

	if (bind(can_if, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return 1;
	}

	/* main loop */
	while (1) {

		/* read fragmented CAN XL source frame */
		nbytes = read(can_if, &cf, sizeof(struct canxl_frame));
		if (nbytes < 0) {
			perror("read");
			return 1;
		}

		if (nbytes < CANXL_HDR_SIZE + CANXL_MIN_DLEN) {
			fprintf(stderr, "read: no CAN frame\n");
			return 1;
		}

		if (!(cf.flags & CANXL_XLF)) {
			fprintf(stderr, "read: no CAN XL frame flag\n");
			continue;
		}

		if (nbytes != CANXL_HDR_SIZE + cf.len) {
			printf("nbytes = %d\n", nbytes);
			fprintf(stderr, "read: no CAN XL frame len\n");
			continue;
		}

		if (verbose) {
			if (ioctl(can_if, SIOCGSTAMP, &tv) < 0) {
				perror("SIOCGSTAMP");
				return 1;
			}

			/* print timestamp and device name */
			printf("(%ld.%06ld) %s ", tv.tv_sec, tv.tv_usec,
			       argv[optind]);

			printxlframe(&cf);
		}

		tid = cf.prio & TID_MASK;

		/* get buffer index based on received prio */
		bufidx = tid2bufidx[tid];
		if (!bufidx)
			continue;

		/* is this a test data prio id ? */
		if (cf.prio & TESTDATA_PRIO_BASE) {
			cf.prio &= TID_MASK; /* for memcmp testing */
			testdata[bufidx] = cf;
			fcnt[bufidx] = NO_FCNT_VALUE;

			nn = 0x01;
			printf("TID %02X - state %02X: stored PDU test data\n", nn, tid);
			if (ret = sendstate(can_if, nn, tid))
				return ret;

			continue; /* wait for next frame */
		}

		/* we have a valid TID with 613-3 content */
		if (testdata[bufidx].len == 0) {
			/* no test data available */
			nn = 0x02;
			printf("TID %02X - state %02X: no stored PDU test data available\n", nn, tid);
			if (ret = sendstate(can_if, nn, tid))
				return ret;
			continue; /* wait for next frame */
		}

		/* check for SEC bit and CiA 613-3 AOT (fragmentation) */
		if (!((cf.flags & CANXL_SEC) &&
		      (cf.len >= LLC_613_3_SIZE) &&
		      ((llc->pci & PCI_AOT_MASK) == CIA_613_3_AOT))) {
			/* no CiA 613-3 fragment frame => just forward frame */

			if (!framecmp(&cf, &testdata[bufidx])) {
				nn = 0x03;
				printf("TID %02X - state %02X: received correct unfragmented PDU\n", nn, tid);
			} else {
				nn = 0x04;
				printf("TID %02X - state %02X: received incorrect unfragmented PDU\n", nn, tid);
			}
			if (ret = sendstate(can_if, nn, tid))
				return ret;
			continue; /* wait for next frame */
		}

		if ((llc->pci & PCI_VX_MASK) != CIA_613_3_VERSION) {
			nn = 0x05;
			printf("TID %02X - state %02X: dropped frame due to wrong CiA 613-3 version\n", nn, tid);
			if (ret = sendstate(can_if, nn, tid))
				return ret;
			continue; /* wait for next frame */
		}

		/* common FCNT reception handling */
		rxfcnt = ntohs(llc->fcnt); /* read from PCI with byte order */

		/* retrieve real fragment data size from this CAN XL frame */
		rxfragsz = cf.len - LLC_613_3_SIZE;

		/* check for first frame */
		if ((llc->pci & PCI_XF_MASK) == PCI_FF) {

			if (rxfragsz <  MIN_FRAG_SIZE || rxfragsz > MAX_FRAG_SIZE) {
				nn = 0x06;
				printf("TID %02X - state %02X: FF: dropped LLC frame illegal fragment size\n", nn, tid);
				if (ret = sendstate(can_if, nn, tid))
					return ret;
				continue;
			}

			if (rxfragsz % FRAG_STEP_SIZE) {
				nn = 0x07;
				printf("TID %02X - state %02X: FF: dropped LLC frame illegal fragment step size\n", nn, tid);
				if (ret = sendstate(can_if, nn, tid))
					return ret;
				continue;
			}

			/* take current rxfcnt as initial fcnt */
			fcnt[bufidx] = rxfcnt;

			/* copy CAN XL header w/o data */
			memcpy(&pdudata[bufidx], &cf, CANXL_HDR_SIZE);

			/* clear SEC bit from our segmentation process */
			pdudata[bufidx].flags &= ~CANXL_SEC;

			/* restore original SEC bit from DLX (for other AOT) */
			if (llc->pci & PCI_SECN)
				pdudata[bufidx].flags |= CANXL_SEC;

			/* 'reassembled' length without the LLC information */
			pdudata[bufidx].len = rxfragsz;

			/* copy CAN XL fragment data w/o LLC information */
			memcpy(&pdudata[bufidx].data[0],
			       &cf.data[LLC_613_3_SIZE],
			       pdudata[bufidx].len);

			/* update data pointer for next fragment data */
			dataptr[bufidx] = pdudata[bufidx].len;

			nn = 0x08;
			printf("TID %02X - state %02X: FF: correctly received first fragment\n", nn, tid);
			if (ret = sendstate(can_if, nn, tid))
				return ret;
			continue; /* wait for next frame */
		} /* FF */

		/* consecutive frame (FF/LF are unset) */
		if ((llc->pci & PCI_XF_MASK) == 0) {

			if (fcnt[bufidx] != NO_FCNT_VALUE) {
				fcnt[bufidx]++;
				fcnt[bufidx] &= 0xFFFFU;
			}

			/* check that rxfcnt has increased */
			if (fcnt[bufidx] != rxfcnt) {
				nn = 0xE3;
				printf("TID %02X - state %02X: CF: abort reception wrong FCNT! (%d/%d)\n",
				       fcnt[bufidx], rxfcnt, nn, tid);
				if (ret = sendstate(can_if, nn, tid))
					return ret;

				/* only FF can set a proper fcnt value */
				fcnt[bufidx] = NO_FCNT_VALUE;
				continue;
			}

			if (rxfragsz <  MIN_FRAG_SIZE || rxfragsz > MAX_FRAG_SIZE) {
				nn = 0x09;
				printf("TID %02X - state %02X: CF: dropped LLC frame illegal fragment size\n", nn, tid);
				if (ret = sendstate(can_if, nn, tid))
					return ret;
				continue;
			}

			if (rxfragsz % FRAG_STEP_SIZE) {
				nn = 0x0A;
				printf("TID %02X - state %02X: CF: dropped LLC frame illegal fragment step size\n", nn, tid);
				if (ret = sendstate(can_if, nn, tid))
					return ret;
				continue;
			}

			/* make sure the data fits into the unfragmented frame */
			if (dataptr[bufidx] + rxfragsz > CANXL_MAX_DLEN) {
				nn = 0x0B;
				printf("TID %02X - state %02X: CF: dropped CF frame size overflow\n", nn, tid);
				if (ret = sendstate(can_if, nn, tid))
					return ret;
				continue;
			}

			/* copy CAN XL fragment data w/o LLC information */
			memcpy(&pdudata[bufidx].data[dataptr[bufidx]],
			       &cf.data[LLC_613_3_SIZE],
			       rxfragsz);

			/* update data pointer and len for next fragment data */
			dataptr[bufidx] += rxfragsz;
			pdudata[bufidx].len += rxfragsz;

			continue; /* wait for next frame */
		} /* CF */

		/* last frame */
		if ((llc->pci & PCI_XF_MASK) == PCI_LF) {

			if (fcnt[bufidx] != NO_FCNT_VALUE) {
				fcnt[bufidx]++;
				fcnt[bufidx] &= 0xFFFFU;
			}

			/* check that rxfcnt has increased */
			if (fcnt[bufidx] != rxfcnt) {
				nn = 0xE3;
				printf("TID %02X - state %02X: LF: abort reception wrong FCNT! (%d/%d)\n",
				       fcnt[bufidx], rxfcnt, nn, tid);
				if (ret = sendstate(can_if, nn, tid))
					return ret;

				/* only FF can set a proper fcnt value */
				fcnt[bufidx] = NO_FCNT_VALUE;
				continue;
			}

			if (rxfragsz < LF_MIN_FRAG_SIZE || rxfragsz > MAX_FRAG_SIZE) {
				nn = 0x0C;
				printf("TID %02X - state %02X: LF: dropped LLC frame illegal fragment size\n", nn, tid);
				if (ret = sendstate(can_if, nn, tid))
					return ret;
				continue;
			}

			/* make sure the data fits into the unfragmented frame */
			if (dataptr[bufidx] + rxfragsz > CANXL_MAX_DLEN) {
				nn = 0x0D;
				printf("TID %02X - state %02X: LF: dropped LF frame size overflow\n", nn, tid);
				if (ret = sendstate(can_if, nn, tid))
					return ret;
				continue;
			}

			/* copy CAN XL fragment data w/o LLC information */
			memcpy(&pdudata[bufidx].data[dataptr[bufidx]],
			       &cf.data[LLC_613_3_SIZE],
			       rxfragsz);

			/* update length value with last frame content size */
			pdudata[bufidx].len += rxfragsz;

			if (!framecmp(&cf, &testdata[bufidx])) {
				nn = 0x0E;
				printf("TID %02X - state %02X: received correct PDU\n", nn, tid);
			} else {
				nn = 0x0F;
				printf("TID %02X - state %02X: received incorrect PDU\n", nn, tid);
			}
			if (ret = sendstate(can_if, nn, tid))
				return ret;

			if (verbose) {
				printf("TX - ");
				printxlframe(&pdudata[bufidx]);
				printf("\n");
			}

			/* only FF can set a proper fcnt value */
			fcnt[bufidx] = NO_FCNT_VALUE;

			continue; /* wait for next frame */
		} /* LF */

		/* invalid (reserved) FF/LF combination */
		nn = 0xE1;
		printf("TID %02X - state %02X: FF/LF: dropped LLC frame with reserved FF/LF bits set\n", nn, tid);
		if (ret = sendstate(can_if, nn, tid))
			return ret;
		continue; /* wait for next frame */

	} /* while(1) */

	close(can_if);

	return 0;
}
