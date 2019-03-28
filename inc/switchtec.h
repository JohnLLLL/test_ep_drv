/*
 * Microsemi Switchtec PCIe Driver
 * Copyright (c) 2017, Microsemi Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef _SWITCHTEC_H
#define _SWITCHTEC_H

#include <linux/pci.h>
#include <linux/cdev.h>
#include <linux/dmaengine.h>

#define MICROSEMI_VENDOR_ID         0x11f8
#define MICROSEMI_NTB_CLASSCODE     0x068000
#define MICROSEMI_MGMT_CLASSCODE    0x058000

//#define SWITCHTEC_DMA_REVA

#ifdef SWITCHTEC_DMA_REVA
#define cpu_to_swt_hw_32(val)                           cpu_to_be32(val)
#define swt_hw_32_to_cpu(val)                           be32_to_cpu(val)

#else
#define cpu_to_swt_hw_32(val)					(val)
#define swt_hw_32_to_cpu(val)					(val)
#endif

#define copy_se_ce_buf(src, dst, dw_sz)			do{\
													typeof(dw_sz) __i; \
													for(__i = 0; __i < (dw_sz); ++__i)\
														*(((u32*)(dst)) + __i) = \
														cpu_to_swt_hw_32(*(((u32*)(src)) + __i)); \
												}while(0);


/* The supported bus width by the DMA controller */
#define SWITCHTEC_DMA_BUSWIDTHS			  \
	BIT(DMA_SLAVE_BUSWIDTH_UNDEFINED)	| \
	BIT(DMA_SLAVE_BUSWIDTH_1_BYTE)		| \
	BIT(DMA_SLAVE_BUSWIDTH_2_BYTES)		| \
	BIT(DMA_SLAVE_BUSWIDTH_4_BYTES)

#define SWITCHTEC_DMA_FW_REGS_OFF								 (0)
#define SWITCHTEC_DMA_FW_REGS_SIZE							(0x1000)

#define SWITCHTEC_DMA_HW_CHANNEL_REGS_OFF					(0x1000)
#define SWITCHTEC_DMA_HW_CHANNEL_REGS_SIZE					(0x1000)

#define SWITCHTEC_DMA_FW_CHANNEL_REGS_OFF					(0x160 * 0x1000)
#define SWITCHTEC_DMA_FW_CHANNEL_REGS_SIZE					(0x1000)

#define SWITCHTEC_DMA_CHAN_HW_CTRL_BITMSK_CH_PAUSE					(0x1)
#define SWITCHTEC_DMA_CHAN_HW_CTRL_BITMSK_CH_HALT					(0x2)
#define SWITCHTEC_DMA_CHAN_HW_CTRL_BITMSK_CH_RESET					(0x4)

#define SWITCHTEC_DMA_CHAN_HW_STATUS_BITMSK_CH_PAUSED				(0x1)
#define SWITCHTEC_DMA_CHAN_HW_STATUS_BITMSK_CH_HALTED				(0x2)
#define SWITCHTEC_DMA_CHAN_HW_STATUS_BITMSK_CH_ERROR_PAUSED			(0x4)

#define SWITCHTEC_DMA_CHAN_HW_STAT_BITMSK_CH_PAUSED					(0x1)
#define SWITCHTEC_DMA_CHAN_HW_STAT_BITMSK_CH_HALTED					(0x2)

#define SWITCHTEC_DMA_CMD_READ_IMMD									(0x01)
#define SWITCHTEC_DMA_CMD_NOP										(0x07)

#define SWITCHTEC_DMA_CMD_OP_TRANSFER								(0x0)
#define SWITCHTEC_DMA_CMD_OP_READ_IMM								(0x1)
#define SWITCHTEC_DMA_CMD_OP_WRITE_IMM								(0x2)
#define SWITCHTEC_DMA_CMD_OP_ECHO									(0x7)

struct dma_se_cmd {
	/* dw0 */
	u32 opc: 8;
	u32 : 5;
	u32 dfm: 1;
	u32 liof: 1;
	u32 brr: 1;
	u32 dro: 1;
	u32 dns: 1;
	u32 dat: 2;
	u32 dtc: 3;
	u32 : 1;
	u32 sro: 1;
	u32 sns: 1;
	u32 sat: 2;
	u32 stc: 3;
	u32 : 1;
	/* dw1 */
	u32 resv1: 16;
	u32 cmd_id:16;
	/* dw2 */
	u32 byte_cnt;
	/* dw3 */
	u32 src_addr_lo;
	/* dw4 */
	u32 src_addr_hi;
	/* dw6 */
	u32 dst_addr_lo;
	/* dw7 */
	u32 dst_addr_hi;
	/* dw8 */
	u32 resv2;
} __packed;

struct dma_ce_cpl {
#ifdef SWITCHTEC_DMA_REVA
	/* dw0 */
	u32 rd_im_dw;
	/* dw1 */
	u32 resv0;
	/* dw2 */
	u32 resv1;
	/* dw3 */
	u32 cpl_byte_cnt;
	/* dw4 */
	u32 sq_head:16;
	u32 resv2:16;
	/* dw5 */
	u32 cmd_id:16;
	u32 phase:1;
	u32 resv3:15;
	/* dw6 */
	u32 cpl_stat:16;
	u32 resv4:16;
	/* dw7 */
	u32 resv5;
#else
	/* dw0 */
	u32 rd_im_dw0;
	/* dw1 */
	u32 rd_im_dw1;
	/* dw2 */
	u32 resv1;
	/* dw3 */
	u32 cpl_byte_cnt;
	/* dw4 */
	u32 sq_head:16;
	u32 resv2:16;
	/* dw5 */
	u32 resv3;
	/* dw6 */
	u32 cpl_stat:16;
	u32 resv4:16;
	/* dw5 */
	u32 cmd_id:16;
	u32 phase:1;
	u32 resv5:15;
#endif
} __packed;

struct dma_hw_ch_regs {
#ifdef SWITCHTEC_DMA_REVA
	u32 cq_head;
	u32 sq_tail;
#else
	u16 cq_head;
	u16 resv0;
	u16 sq_tail;
	u16 resv1;
#endif
	u32 ctrl;
	u32 stat;
} __packed;

#define SWITCHTEC_DMA_CHAN_FW_ARB_WEIGHT_BITMSK_ARB_WEIGHT			(0xff)

#define SWITCHTEC_DMA_CHAN_FW_CFG_BITMSK_MRRS						(0x7)
#define SWITCHTEC_DMA_CHAN_FW_CFG_BITMSK_BURST_INTERVAL				(0x700)
#define SWITCHTEC_DMA_CHAN_FW_CFG_BITMSK_BURST_SZ					(0x7000)


#define DMA_FW_CHANNEL_CFG_REG_VALID_DW_OFF                                  (0)
#define DMA_FW_CHANNEL_CFG_REG_VALID_BITMSK_VALID                   (0x00000001)
#define DMA_FW_CHANNEL_CFG_REG_VALID_BITOFF_VALID                            (0)
#define DMA_FW_CHANNEL_CFG_REG_VALID_BITMSK_ENABLE                  (0x00000002)
#define DMA_FW_CHANNEL_CFG_REG_VALID_BITOFF_ENABLE                           (1)
#define DMA_FW_CHANNEL_CFG_REG_VALID_BITMSK_SE_BUF_LEN              (0x000ff000)
#define DMA_FW_CHANNEL_CFG_REG_VALID_BITOFF_SE_BUF_LEN                      (12)
#define DMA_FW_CHANNEL_CFG_REG_VALID_BITMSK_SE_THRESHOLD            (0xff800000)
#define DMA_FW_CHANNEL_CFG_REG_VALID_BITOFF_SE_THRESHOLD                    (23)


#define DMA_FW_CHANNEL_CFG_REG_TRANSFER_SET_DW_OFF                           (8)
#define DMA_FW_CHANNEL_CFG_REG_TRANSFER_SET_BITMSK_BURST_SCALE      (0x0000000c)
#define DMA_FW_CHANNEL_CFG_REG_TRANSFER_SET_BITOFF_BURST_SCALE               (2)
#define DMA_FW_CHANNEL_CFG_REG_TRANSFER_SET_BITMSK_MRRS             (0x00000070)
#define DMA_FW_CHANNEL_CFG_REG_TRANSFER_SET_BITOFF_MRRS                      (4)
#define DMA_FW_CHANNEL_CFG_REG_TRANSFER_SET_BITMSK_INTERVAL         (0x00000700)
#define DMA_FW_CHANNEL_CFG_REG_TRANSFER_SET_BITOFF_INTERVAL                  (8)
#define DMA_FW_CHANNEL_CFG_REG_TRANSFER_SET_BITMSK_BURST_SIZE       (0x00007000)
#define DMA_FW_CHANNEL_CFG_REG_TRANSFER_SET_BITOFF_BURST_SIZE               (12)
#define DMA_FW_CHANNEL_CFG_REG_TRANSFER_SET_BITMSK_ARB_WEIGHT       (0xff000000)
#define DMA_FW_CHANNEL_CFG_REG_TRANSFER_SET_BITOFF_ARB_WEIGHT               (24)

struct dma_fw_ch_regs {
	/* FW register per channel - 0x0*/
	u32 cfg;
	u32 cq_base_lo;
	u32 cq_base_hi;
	u32 cq_size;
	u32 sq_base_lo;
	u32 sq_base_hi;
	u32 sq_size;
	u32 intv;
	u32 trans_set;
	u32 ch_stat;
	u32 resv0[22];
} __packed;

struct switchtec_dma_desc {
	/* Switchtec data */
	struct dma_async_tx_descriptor txd;
	struct dma_se_cmd cmd;
	struct list_head desc_list;
};

struct switchtec_dma_chan {
	struct dma_chan chan;
	bool used;

	u8 phase;
	void __iomem *mmio_hw_ch;
	void __iomem *mmio_fw_ch;
	struct dma_ce_cpl *cq_base;
	u16  cq_head;
	u16  cq_size;
	dma_addr_t cq_dma_base;

	struct dma_se_cmd *sq_base;
	u16  sq_head;
	u16  sq_tail;
	u16  sq_size;
	dma_addr_t sq_dma_base;

	void *dst_test_buf;
	dma_addr_t dst_test_base;

	void *src_test_buf;
	dma_addr_t src_test_base;

	struct switchtec_dma_desc *dma_desc;
	struct list_head free_list;
	struct list_head used_list;
};

static inline struct dma_hw_ch_regs __iomem * __hw_ch_reg(struct switchtec_dma_chan *sw_ch)
{
	return sw_ch->mmio_hw_ch;
}

static inline struct dma_fw_ch_regs __iomem * __fw_ch_reg(struct switchtec_dma_chan *sw_ch)
{
	return sw_ch->mmio_fw_ch;
}

static inline struct switchtec_dma_chan *to_st_dma_chan(struct dma_chan *chan)
{
	return container_of(chan, struct switchtec_dma_chan, chan);
}

static inline struct switchtec_dma_desc *
txd_to_dma_desc(struct dma_async_tx_descriptor *txd)
{
	return container_of(txd, struct switchtec_dma_desc, txd);
}

#define switchtec_ch_cfg_readl(sw_ch, name) \
		ioread32(&(__fw_ch_reg(sw_ch)->name))

#define switchtec_ch_cfg_readw(sw_ch, name) \
		ioread16(&(__fw_ch_reg(sw_ch)->name))

#define switchtec_ch_cfg_readb(sw_ch, name) \
		ioread8(&(__fw_ch_reg(sw_ch)->name))

#define switchtec_ch_cfg_writel(sw_ch, name, val) \
		iowrite32(val, &(__fw_ch_reg(sw_ch)->name))

#define switchtec_ch_cfg_writew(sw_ch, name, val) \
		iowrite16(val, &(__fw_ch_reg(sw_ch)->name))

#define switchtec_ch_cfg_writeb(sw_ch, name, val) \
		iowrite8(val, &(__fw_ch_reg(sw_ch)->name))

#define switchtec_ch_ctrl_readl(sw_ch, name) \
		swt_hw_32_to_cpu(ioread32(&(__hw_ch_reg(sw_ch)->name)))

#define switchtec_ch_ctrl_writel(sw_ch, name, val) \
		iowrite32(cpu_to_swt_hw_32(val), &(__hw_ch_reg(sw_ch)->name))

#define switchtec_ch_ctrl_readw(sw_ch, name) \
		ioread16(&(__hw_ch_reg(sw_ch)->name))

#define switchtec_ch_ctrl_writew(sw_ch, name, val) \
		iowrite16(val, &(__hw_ch_reg(sw_ch)->name))

#define switchtec_ch_ctrl_readb(sw_ch, name) \
		ioread8(&(__hw_ch_reg(sw_ch)->name))

#define switchtec_ch_ctrl_writeb(sw_ch, name, val) \
		iowrite8(val, &(__hw_ch_reg(sw_ch)->name))

struct dma_fw_regs {
	/* Version register - 0x0*/
	u32 fw_ver;
	u32 intf_ver;
	u32 hw_ver;
	u32 resv0[13];
	char build_time[64];

	/* Capability register - 0x80*/
	u32 cap;
	u32 chan_num;
	u32 glb_wrr;
	u32 cpl_to;
	u32 tag_lim;
	u32 fw_int;
	u32 resv1[26];

	/* Status register - 0x100*/
	u32 stat;
	u32 resv2[31];

	/* Config register - 0x180*/
	u32 reset;
	u32 reva_wa;
	u32 resv3[30];

	/* FW interrupt register - 0x200*/
	u32 intv;
	u32 int_msk;
} __packed;

struct switchtec_dev {
	struct pci_dev *pdev;
	struct msix_entry msix[4];
	struct device dev;
	struct cdev cdev;
	struct dma_device dma;
	struct dma_pool *desc_pool;

	void __iomem *mmio;
	struct switchtec_dma_chan *dma_ch;

	/*Unit test*/
	struct dma_chan **test_chan;
#if 0
	unsigned int event_irq;
	unsigned int dma_mrpc_irq;

	int partition;
	int partition_count;
	int pff_csr_count;
	char pff_local[SWITCHTEC_MAX_PFF_CSR];
#endif
#if 0
	struct mrpc_regs __iomem *mmio_mrpc;
	struct sw_event_regs __iomem *mmio_sw_event;
	struct sys_info_regs __iomem *mmio_sys_info;
	struct flash_info_regs __iomem *mmio_flash_info;
	struct ntb_info_regs __iomem *mmio_ntb;
	struct part_cfg_regs __iomem *mmio_part_cfg;
	struct part_cfg_regs __iomem *mmio_part_cfg_all;
	struct pff_csr_regs __iomem *mmio_pff_csr;

	/*
	 * The mrpc mutex must be held when accessing the other
	 * mrpc_ fields, alive flag and stuser->state field
	 */
	struct mutex mrpc_mutex;
	struct list_head mrpc_queue;
	int mrpc_busy;
	struct work_struct mrpc_work;
	struct delayed_work mrpc_timeout;
	bool alive;

	wait_queue_head_t event_wq;
	atomic_t event_cnt;

	struct work_struct link_event_work;
	void (*link_notifier)(struct switchtec_dev *stdev);
	u8 link_event_count[SWITCHTEC_MAX_PFF_CSR];

	struct switchtec_ntb *sndev;

	struct dma_mrpc_output *dma_mrpc;
	dma_addr_t dma_mrpc_dma_addr;
#endif
};

static inline struct dma_fw_regs __iomem * __fw_reg(struct switchtec_dev *sw)
{
	return sw->mmio;
}

#define switchtec_readl(sw, name) \
		ioread32(&(__fw_reg(sw)->name))

#define switchtec_writel(sw, name, val) \
		iowrite32(val, &(__fw_reg(sw)->name))

static inline struct switchtec_dev *to_stdev(struct device *dev)
{
	return container_of(dev, struct switchtec_dev, dev);
}

static inline struct switchtec_dev *dma_dev_to_stdev(struct dma_device *dev)
{
	return container_of(dev, struct switchtec_dev, dma);
}

extern struct class *switchtec_dma_class;

#endif
