#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/can.h>

static inline void printxlframe(struct canxl_frame *cfx)
{
	int i;

	/* print prio and CAN XL header content */
	printf("%03X###%02X.%02X.%08X",
	       cfx->prio, cfx->flags, cfx->sdt, cfx->af);

	/* print up to 12 data bytes */
	for (i = 0; i < cfx->len && i < 12; i++) {
		if (!(i%4))
			printf(".");
		printf("%02X", cfx->data[i]);
	}

	/* print CAN XL data length */
	printf("(%d)\n", cfx->len);
	fflush(stdout);
}

static inline void printfdframe(struct canfd_frame *cfd)
{
	int i;

	if (cfd->can_id & CAN_EFF_FLAG)
		printf("%08X#", cfd->can_id & CAN_EFF_MASK);
	else
		printf("%03X#", cfd->can_id & CAN_SFF_MASK);

	printf("#%X", cfd->flags & 0xF);

	for (i = 0; i < cfd->len; i++)
		printf("%02X", cfd->data[i]);

	printf("\n");
	fflush(stdout);
}

static inline void printccframe(struct can_frame *cf)
{
	int i;

	if (cf->can_id & CAN_EFF_FLAG)
		printf("%08X#", cf->can_id & CAN_EFF_MASK);
	else
		printf("%03X#", cf->can_id & CAN_SFF_MASK);

	if (cf->can_id & CAN_RTR_FLAG) {
		printf("R");
		if (cf->len > 0)
			printf("%d", cf->len);
	} else {
		for (i = 0; i < cf->len; i++)
			printf("%02X", cf->data[i]);
	}
	if (cf->len == CAN_MAX_DLEN &&
	    cf->len8_dlc > CAN_MAX_DLEN &&
	    cf->len8_dlc <= CAN_MAX_RAW_DLC)
		printf("_%X", cf->len8_dlc);

	printf("\n");
	fflush(stdout);
}
