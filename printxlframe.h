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

