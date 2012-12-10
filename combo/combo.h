#ifndef __PB173_COMBO_H_
#define __PB173_COMBO_H_

#define COMBO_VENDOR	0x18ec
#define COMBO_DEVICE	0xc058


/* Combo DMA registers */
#define BAR0_DMA_SRC		0x00000080
#define BAR0_DMA_DEST		0x00000084
#define BAR0_DMA_NBYTES		0x00000088
#define BAR0_DMA_CMD		0x0000008C

/* Fields inside BAR0_DMA_CMD */ 
#define BAR0_DMA_CMD_RUN	0x00000001
#define BAR0_DMA_CMD_SRC(x)	((x&0x07)<<1)
#define BAR0_DMA_CMD_DEST(x)	((x&0x07)<<4)
#define BAR0_DMA_CMD_INT_NO	0x00000080
#define BAR0_DMA_CMD_INT_ACK	0x80000000

/* DMA bus numbers */
#define COMBO_DMA_LOCALBUS	0x1
#define COMBO_DMA_PCI		0x2
#define COMBO_DMA_PPC		0x4


/* Something inside PPC address space */
#define COMBO_DMA_PPC_BUFFER	0x00040000

/* combo interrupts */
#define BAR0_BRIDGE_ID_REV	0x0000
#define BAR0_BRIDGE_BUILD_DATE	0x0004
#define BAR0_INT_RAISED		0x0040
#define BAR0_INT_ENABLED	0x0044
#define BAR0_INT_TRIGGER	0x0060
#define BAR0_INT_ACK		0x0064



/* interrupt numbers */
#define COMBO_INT_DMA		0x08


struct combo_data {
	void __iomem *bar0;
	struct timer_list timer;

	/* some dma-mapped memory */
	size_t dma_nb;
	dma_addr_t dma_phys;
	char *dma_virt;
	size_t dma_int_off;	/* data offset */

	/* use only using interrupts */
	int way;	/* 1 == out, 2 == in */
};


#endif
