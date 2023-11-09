/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * cia-613-3.h - CAN CiA 613-3 definitions
 *
 * add-on types (AOT) definitions see in CAN CiA 613-1
 *
 */

#ifndef CIA_613_3_H
#define CIA_613_3_H

#include <linux/types.h>

#define DEFAULT_FRAG_SIZE 128
#define MIN_FRAG_SIZE 128
#define MAX_FRAG_SIZE 1024
#define FRAG_STEP_SIZE 128
#define LF_MIN_FRAG_SIZE 1

/* Protocol Control Information definitions: */

/* frame type identification (both unset => consecutive frame) */
#define PCI_LF    0x01 /* last frame */
#define PCI_FF    0x02 /* first frame */

/* protocol version */
#define PCI_VL    0x04 /* version low bit */
#define PCI_VH    0x08 /* version high bit */

/* data link extension indicator (DLX) => SEC + AOT */
#define PCI_SECN  0x10 /* (further) simple/extended content */
#define PCI_AOTL  0x20 /* add-on type low bit */
#define PCI_AOTM  0x40 /* add-on type mid bit */
#define PCI_AOTH  0x80 /* add-on type high bit */

#define PCI_AOT_MASK (PCI_AOTL | PCI_AOTM | PCI_AOTH)
#define PCI_VX_MASK (PCI_VL | PCI_VH)
#define PCI_XF_MASK (PCI_LF | PCI_FF)

#define CIA_613_3_AOT     (PCI_AOTL) /* 001b - fragmentation add-on type */
#define CIA_613_3_VERSION (PCI_VL)   /*  01b - protocol version 1 */

struct llc_613_3 {
	__u8 pci; /* protocol control information */
	__u8 res; /* reserved / set to zero */
	__u16 fcnt; /* FCNT frame counter (network byte order) */
};

#define LLC_613_3_SIZE (sizeof(struct llc_613_3))

#endif /* CIA_613_3_H */
