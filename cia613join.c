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
#include <arpa/inet.h> /* for network byte order conversion */

#include <linux/sockios.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include "cia-613-3.h"
#include "printframe.h"

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
	unsigned int rxfragsz;
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

	/* src_if and dst_if are two mandatory parameters */
	if (argc - optind != 2) {
		print_usage(basename(argv[0]));
		exit(0);
	}

	/* src_if */
	if (strlen(argv[optind]) >= IFNAMSIZ) {
		printf("Name of src CAN device '%s' is too long!\n\n",
		       argv[optind]);
		return 1;
	}

	/* dst_if */
	if (strlen(argv[optind + 1]) >= IFNAMSIZ) {
		printf("Name of dst CAN device '%s' is too long!\n\n",
		       argv[optind]);
		return 1;
	}

	/* open src socket */
	src = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (src < 0) {
		perror("src socket");
		return 1;
	}
	addr.can_family = AF_CAN;
	addr.can_ifindex = if_nametoindex(argv[optind]);

	/* enable CAN XL frames */
	ret = setsockopt(src, SOL_CAN_RAW, CAN_RAW_XL_FRAMES,
			 &sockopt, sizeof(sockopt));
	if (ret < 0) {
		perror("src sockopt CAN_RAW_XL_FRAMES");
		exit(1);
	}

	/* filter only for transfer_id (= prio_id) */
	rfilter.can_id = transfer_id;
	rfilter.can_mask = CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_SFF_MASK;
	ret = setsockopt(src, SOL_CAN_RAW, CAN_RAW_FILTER,
			 &rfilter, sizeof(rfilter));
	if (ret < 0) {
		perror("src sockopt CAN_RAW_FILTER");
		exit(1);
	}

	if (bind(src, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return 1;
	}

	/* open dst socket */
	dst = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (dst < 0) {
		perror("dst socket");
		return 1;
	}
	addr.can_family = AF_CAN;
	addr.can_ifindex = if_nametoindex(argv[optind + 1]);

	/* enable CAN XL frames */
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

	/* main loop */
	while (1) {

		/* read fragmented CAN XL source frame */
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
			}

			/* print timestamp and device name */
			printf("(%ld.%06ld) %s ", tv.tv_sec, tv.tv_usec,
			       argv[optind]);

			printxlframe(&cfsrc);
		}

		/* check for SEC bit and CiA 613-3 AOT (fragmentation) */
		if (!((cfsrc.flags & CANXL_SEC) &&
		      (cfsrc.len >= CANXL_MIN_DLEN + LLC_613_3_SIZE) &&
		      ((llc->pci & PCI_AOT_MASK) == CIA_613_3_AOT))) {
			/* no CiA 613-3 fragment frame => just forward frame */

			nbytes = write(dst, &cfsrc, CANXL_HDR_SIZE + cfsrc.len);
			if (nbytes != CANXL_HDR_SIZE + cfsrc.len) {
				printf("nbytes = %d\n", nbytes);
				perror("forward src canxl_frame");
				exit(1);
			}

			if (verbose) {
				printf("FW - ");
				printxlframe(&cfsrc);
			}
			continue; /* wait for next frame */
		}

		/* common FCNT reception handling */
		rxfcnt = ntohs(llc->fcnt); /* read from PCI with byte order */

		if (fcnt == NO_FCNT_VALUE) {
			/* first reception */
			fcnt = rxfcnt;
		} else if (fcnt == rxfcnt) {
			printf("dropped frame with identical FCNT!\n");
			continue;
		}

		/* retrieve real fragment data size from this CAN XL frame */
		rxfragsz = cfsrc.len - LLC_613_3_SIZE;

		/* check for first frame */
		if ((llc->pci & PCI_XF_MASK) == PCI_FF) {

			if (rxfragsz <  MIN_FRAG_SIZE || rxfragsz > MAX_FRAG_SIZE) {
				printf("FF: dropped LLC frame illegal fragment size!\n");
				continue;
			}

			if (rxfragsz % FRAG_STEP_SIZE) {
				printf("FF: dropped LLC frame illegal fragment step size!\n");
				continue;
			}

			/* take current rxfcnt as initial fcnt */ 
			fcnt = rxfcnt;

			/* copy CAN XL header w/o data */
			memcpy(&cfdst, &cfsrc, CANXL_HDR_SIZE);

			/* clear SEC bit from our segmentation process */
			cfdst.flags &= ~CANXL_SEC;

			/* restore original SEC bit from DLX (for other AOT) */
			if (llc->pci & PCI_SECN)
				cfdst.flags |= CANXL_SEC;

			/* 'reassembled' length without the LLC information */
			cfdst.len = rxfragsz;

			/* copy CAN XL fragment data w/o LLC information */
			memcpy(&cfdst.data[0],
			       &cfsrc.data[LLC_613_3_SIZE],
			       cfdst.len);

			/* update data pointer for next fragment data */
			dataptr = cfdst.len;

			if (0) {
				printf("TX - ");
				printxlframe(&cfdst);
				printf("\n");
			}
			continue; /* wait for next frame */
		} /* FF */

		/* consecutive frame (FF/LF are unset) */
		if ((llc->pci & PCI_XF_MASK) == 0) {

			/* check that rxfcnt has increased */ 
			if (fcnt + 1 != rxfcnt) {
				printf("dropped CF frame wrong FCNT! (%d/%d)\n",
				       fcnt, rxfcnt);
				continue;
			}

			/* update fcnt after check */
			fcnt = rxfcnt;

			if (rxfragsz <  MIN_FRAG_SIZE || rxfragsz > MAX_FRAG_SIZE) {
				printf("CF: dropped LLC frame illegal fragment size!\n");
				continue;
			}

			if (rxfragsz % FRAG_STEP_SIZE) {
				printf("CF: dropped LLC frame illegal fragment step size!\n");
				continue;
			}

			/* make sure the data fits into the unfragmented frame */
			if (dataptr + rxfragsz > CANXL_MAX_DLEN) {
				printf("dropped CF frame size overflow!\n");
				continue;
			}

			/* copy CAN XL fragment data w/o LLC information */
			memcpy(&cfdst.data[dataptr],
			       &cfsrc.data[LLC_613_3_SIZE],
			       rxfragsz);

			/* update data pointer and len for next fragment data */
			dataptr += rxfragsz;
			cfdst.len += rxfragsz;

			if (0) {
				printf("TX - ");
				printxlframe(&cfdst);
				printf("\n");
			}
			continue; /* wait for next frame */
		} /* CF */

		/* last frame */
		if ((llc->pci & PCI_XF_MASK) == PCI_LF) {

			/* check that rxfcnt has increased */ 
			if (fcnt + 1 != rxfcnt) {
				printf("dropped LF frame wrong FCNT! (%d/%d)\n",
				       fcnt, rxfcnt);
				continue;
			}

			/* update fcnt */
			fcnt = rxfcnt;

			if (rxfragsz < LF_MIN_FRAG_SIZE || rxfragsz > MAX_FRAG_SIZE) {
				printf("LF: dropped LLC frame illegal fragment size!\n");
				continue;
			}

			/* make sure the data fits into the unfragmented frame */
			if (dataptr + rxfragsz > CANXL_MAX_DLEN) {
				printf("dropped LF frame size overflow!\n");
				continue;
			}

			/* copy CAN XL fragment data w/o LLC information */
			memcpy(&cfdst.data[dataptr],
			       &cfsrc.data[LLC_613_3_SIZE],
			       rxfragsz);

			/* update length value with last frame content size */
			cfdst.len += rxfragsz;

			/* write 'reassembled' CAN XL frame */
			nbytes = write(dst, &cfdst, CANXL_HDR_SIZE + cfdst.len);
			if (nbytes != CANXL_HDR_SIZE + cfdst.len) {
				printf("nbytes = %d\n", nbytes);
				perror("write dst canxl_frame");
				exit(1);
			}

			if (verbose) {
				printf("TX - ");
				printxlframe(&cfdst);
				printf("\n");
			}
			continue; /* wait for next frame */
		} /* LF */

		/* TODO: add handling for reserved FF/LF set bits here? */

	} /* while(1) */

	close(src);
	close(dst);

	return 0;
}
