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
#include <arpa/inet.h> /* for network byte order conversion */

#include <linux/sockios.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include "cia-613-3.h"
#include "printframe.h"

#define DEFAULT_TRANSFER_ID 0x242

extern int optind, opterr, optopt;

void print_usage(char *prg)
{
	fprintf(stderr, "%s - CAN XL CiA 613-3 sender\n\n", prg);
	fprintf(stderr, "Usage: %s [options] <src_if> <dst_if>\n", prg);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "         -f <fragsize>    (fragment size "
		"- default: %d bytes)\n", DEFAULT_FRAG_SIZE);
	fprintf(stderr, "         -t <transfer_id> (TRANSFER ID "
		"- default: 0x%03X)\n", DEFAULT_TRANSFER_ID);
	fprintf(stderr, "         -V <vcid>        (set virtual CAN network ID)\n");
	fprintf(stderr, "         -v               (verbose)\n");
}

int main(int argc, char **argv)
{
	int opt;
	unsigned int fragsz = DEFAULT_FRAG_SIZE;
	unsigned int txfcnt = 0;
	canid_t transfer_id = DEFAULT_TRANSFER_ID;
	__u8 vcid = 0;
	int verbose = 0;

	int src, dst;
	struct sockaddr_can addr;
	struct can_raw_vcid_options vcid_opts = {};
	struct can_filter rfilter;
	struct canxl_frame cfsrc, cfdst;
	struct llc_613_3 *srcllc = (struct llc_613_3 *) cfsrc.data;
	struct llc_613_3 *llc = (struct llc_613_3 *) cfdst.data;
	unsigned int dataptr = 0;
	__u8 tx_pci = 0;

	int nbytes, ret;
	int sockopt = 1;
	struct timeval tv;

	while ((opt = getopt(argc, argv, "f:t:V:vh?")) != -1) {
		switch (opt) {

		case 'f':
			fragsz = strtoul(optarg, NULL, 10);
			if (fragsz < MIN_FRAG_SIZE || fragsz > MAX_FRAG_SIZE) {
				printf("fragment size out of range!\n");
				print_usage(basename(argv[0]));
				return 1;
			}
			if (fragsz % FRAG_STEP_SIZE) {
				printf("illegal fragment step size!\n");
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

		case 'V':
			if (sscanf(optarg, "%hhx", &vcid) != 1) {
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

	if (vcid) {
		vcid_opts.tx_vcid = vcid;
		vcid_opts.flags = CAN_RAW_XL_VCID_TX_SET;
		ret = setsockopt(dst, SOL_CAN_RAW, CAN_RAW_XL_VCID_OPTS,
				 &vcid_opts, sizeof(vcid_opts));
		if (ret < 0) {
			perror("sockopt CAN_RAW_XL_VCID_OPTS");
			exit(1);
		}
	}

	if (bind(dst, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return 1;
	}

	/* main loop */
	while (1) {

		/* read source CAN XL frame */
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
			printf("\n(%ld.%06ld) %s ", tv.tv_sec, tv.tv_usec,
			       argv[optind]);

			printxlframe(&cfsrc);
		}

		/* check for SEC bit and CiA 613-3 AOT (fragmentation) */
		if ((cfsrc.flags & CANXL_SEC) &&
		    (cfsrc.len >= LLC_613_3_SIZE) &&
		    ((srcllc->pci & PCI_AOT_MASK) == CIA_613_3_AOT)) {

			/* 613-3 inside 613-3 fragmentation is not allowed */
			printf("detected tunnel encapsulation -> frame dropped\n");
			continue; /* wait for next frame */
		}

		/* check for unsegmented transfer (forwarding) */
		if (cfsrc.len <= fragsz) {

			/* just forward the unsegmented src frame */
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

		/* send fragmented frame(s) */

		/* initialize fixed LLC information */
		llc->res = 0;

		/* set protocol version number and AOT to tx_pci */
		tx_pci = CIA_613_3_VERSION | CIA_613_3_AOT;

		/* save original SEC bit for DLX (further SEC handling) */
		if (cfsrc.flags & CANXL_SEC)
			tx_pci |= PCI_SECN;

		for (dataptr = 0; dataptr < cfsrc.len; dataptr += fragsz) {

			/* start of fragmentation => init header and set FF */
			if (dataptr == 0) {
				/* initial copy of CAN XL header w/o data */
				memcpy(&cfdst, &cfsrc, CANXL_HDR_SIZE);

				/* set bit for segmentation in CAN XL header */
				cfdst.flags |= CANXL_SEC;

				/* first frame */
				llc->pci = tx_pci | PCI_FF;
			} else {
				/* consecutive frame */
				llc->pci = tx_pci; /* no FF/LF is set */
			}

			/* update FCNT */
			txfcnt++;
			txfcnt &= 0xFFFFU;

			/* set current FCNT counter into LLC information */
			llc->fcnt = htons(txfcnt); /* network byte order */

			/* copy CAN XL fragmented data content */
			if (cfsrc.len - dataptr > fragsz) {
				/* FF / CF */
				memcpy(&cfdst.data[LLC_613_3_SIZE],
				       &cfsrc.data[dataptr], fragsz);
				/* increase length for the LLC information */
				cfdst.len = fragsz + LLC_613_3_SIZE;
			} else {
				/* last frame */
				llc->pci = tx_pci | PCI_LF;
				memcpy(&cfdst.data[LLC_613_3_SIZE],
				       &cfsrc.data[dataptr],
				       cfsrc.len - dataptr);
				cfdst.len = cfsrc.len - dataptr + LLC_613_3_SIZE;
			}

			/* write fragment frame */
			nbytes = write(dst, &cfdst, CANXL_HDR_SIZE + cfdst.len);
			if (nbytes != CANXL_HDR_SIZE + cfdst.len) {
				printf("nbytes = %d\n", nbytes);
				perror("write dst canxl_frame");
				exit(1);
			}

			if (verbose) {
				printf("TX - ");
				printxlframe(&cfdst);
			}
		} /* send fragmented frame(s) */
	} /* while (1) */

	close(src);
	close(dst);

	return 0;
}
