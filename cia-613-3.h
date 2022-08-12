/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * cia-613-3.h - CAN CiA 613-3 definitions
 *
 */

#ifndef CIA_613_3_H
#define CIA_613_3_H

#include <linux/types.h>

#define DEFAULT_FRAG_SIZE 128
#define MIN_FRAG_SIZE 64
#define MAX_FRAG_SIZE 1024

#define PCI_CF 0x00 /* Consecutive Frame */
#define PCI_LF 0x40 /* Last Frame */
#define PCI_FF 0x80 /* First Frame */
#define PCI_SF 0xC0 /* Single Frame */

#define PCI_XF_MASK 0xC0 /* mask for frame related PCI bits */

struct llc_613_3 {
	__u8 pci; /* protocol control information */
	__u8 res; /* reserved / set to zero */
	__u8 fcnt_hi; /* FCNT frame counter high byte */
	__u8 fcnt_lo; /* FCNT frame counter low byte */
};

#define LLC_613_3_SIZE (sizeof(struct llc_613_3))

#endif /* CIA_613_3_H */
