/*
 * Microsemi Switchtec(tm) PCIe Management Driver
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

#include "../inc/switchtec.h"
#include "../inc/switchtec_ioctl.h"

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/dmaengine.h>


#define VERSION                            "test"

MODULE_DESCRIPTION("Microsemi Switchtec(tm) PCIe Management Driver");
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Microsemi Corporation");

static int max_devices = 16;
module_param(max_devices, int, 0644);
MODULE_PARM_DESC(max_devices, "max number of switchtec device instances");

static bool use_dma_mrpc = 1;
module_param(use_dma_mrpc, bool, 0644);
MODULE_PARM_DESC(use_dma_mrpc,
		 "Enable the use of the DMA MRPC feature");

static dev_t switchtec_devt;
static DEFINE_IDA(switchtec_minor_ida);

struct class *switchtec_class;
EXPORT_SYMBOL_GPL(switchtec_class);
#if 0
enum mrpc_state {
	MRPC_IDLE = 0,
	MRPC_QUEUED,
	MRPC_RUNNING,
	MRPC_DONE,
};
#endif
struct switchtec_user {
	struct switchtec_dev *stdev;
	struct kref kref;
#if 0
	enum mrpc_state state;

	struct completion comp;
	struct list_head list;

	u32 cmd;
	u32 status;
	u32 return_code;
	size_t data_len;
	size_t read_len;
	unsigned char data[SWITCHTEC_MRPC_PAYLOAD_SIZE];
	int event_cnt;
#endif
};

unsigned int __attribute__((aligned(4))) buf_4k[ 1024 ];

static struct switchtec_user *stuser_create(struct switchtec_dev *stdev)
{
	struct switchtec_user *stuser;

	stuser = kzalloc(sizeof(*stuser), GFP_KERNEL);
	if (!stuser)
		return ERR_PTR(-ENOMEM);

	get_device(&stdev->dev);
	stuser->stdev = stdev;
	kref_init(&stuser->kref);
#if 0
	INIT_LIST_HEAD(&stuser->list);
	init_completion(&stuser->comp);
	stuser->event_cnt = atomic_read(&stdev->event_cnt);
#endif

	dev_dbg(&stdev->dev, "%s: %p\n", __func__, stuser);

	return stuser;
}

static void stuser_free(struct kref *kref)
{
	struct switchtec_user *stuser;

	stuser = container_of(kref, struct switchtec_user, kref);

	dev_dbg(&stuser->stdev->dev, "%s: %p\n", __func__, stuser);

	put_device(&stuser->stdev->dev);
	kfree(stuser);
}

static void stuser_put(struct switchtec_user *stuser)
{
	kref_put(&stuser->kref, stuser_free);
}

#if 0
static void stuser_set_state(struct switchtec_user *stuser,
			     enum mrpc_state state)
{
	/* requires the mrpc_mutex to already be held when called */

	const char * const state_names[] = {
		[MRPC_IDLE] = "IDLE",
		[MRPC_QUEUED] = "QUEUED",
		[MRPC_RUNNING] = "RUNNING",
		[MRPC_DONE] = "DONE",
	};

	stuser->state = state;

	dev_dbg(&stuser->stdev->dev, "stuser state %p -> %s",
		stuser, state_names[state]);
}

static void mrpc_complete_cmd(struct switchtec_dev *stdev);

static void flush_wc_buf(struct switchtec_dev *stdev)
{
	struct ntb_dbmsg_regs __iomem *mmio_dbmsg;

	mmio_dbmsg = (void __iomem *)stdev->mmio_ntb +
		SWITCHTEC_NTB_REG_DBMSG_OFFSET;
	ioread32(&mmio_dbmsg->reserved1[0]);
}

static void mrpc_cmd_submit(struct switchtec_dev *stdev)
{
	/* requires the mrpc_mutex to already be held when called */

	struct switchtec_user *stuser;

	if (stdev->mrpc_busy)
		return;

	if (list_empty(&stdev->mrpc_queue))
		return;

	stuser = list_entry(stdev->mrpc_queue.next, struct switchtec_user,
			    list);

	if (stdev->dma_mrpc) {
		stdev->dma_mrpc->status = SWITCHTEC_MRPC_STATUS_INPROGRESS;
		memset(stdev->dma_mrpc->data, 0xFF, SWITCHTEC_MRPC_PAYLOAD_SIZE);
	}

	stuser_set_state(stuser, MRPC_RUNNING);
	stdev->mrpc_busy = 1;
	memcpy_toio(&stdev->mmio_mrpc->input_data,
		    stuser->data, stuser->data_len);
	flush_wc_buf(stdev);
	iowrite32(stuser->cmd, &stdev->mmio_mrpc->cmd);

	schedule_delayed_work(&stdev->mrpc_timeout,
			      msecs_to_jiffies(500));
}

static int mrpc_queue_cmd(struct switchtec_user *stuser)
{
	/* requires the mrpc_mutex to already be held when called */

	struct switchtec_dev *stdev = stuser->stdev;

	kref_get(&stuser->kref);
	stuser->read_len = sizeof(stuser->data);
	stuser_set_state(stuser, MRPC_QUEUED);
	init_completion(&stuser->comp);
	list_add_tail(&stuser->list, &stdev->mrpc_queue);

	mrpc_cmd_submit(stdev);

	return 0;
}

static void mrpc_complete_cmd(struct switchtec_dev *stdev)
{
	/* requires the mrpc_mutex to already be held when called */
	struct switchtec_user *stuser;

	if (list_empty(&stdev->mrpc_queue))
		return;

	stuser = list_entry(stdev->mrpc_queue.next, struct switchtec_user,
			    list);

	if (stdev->dma_mrpc)
		stuser->status = stdev->dma_mrpc->status;
	else
		stuser->status = ioread32(&stdev->mmio_mrpc->status);

	if (stuser->status == SWITCHTEC_MRPC_STATUS_INPROGRESS)
		return;

	stuser_set_state(stuser, MRPC_DONE);
	stuser->return_code = 0;

	if (stuser->status != SWITCHTEC_MRPC_STATUS_DONE)
		goto out;

	if (stdev->dma_mrpc)
		stuser->return_code = stdev->dma_mrpc->rtn_code;
	else
		stuser->return_code = ioread32(&stdev->mmio_mrpc->ret_value);
	if (stuser->return_code != 0)
		goto out;

	if (stdev->dma_mrpc)
		memcpy(stuser->data, &stdev->dma_mrpc->data,
			      stuser->read_len);
	else
		memcpy_fromio(stuser->data, &stdev->mmio_mrpc->output_data,
			      stuser->read_len);
out:
	complete_all(&stuser->comp);
	list_del_init(&stuser->list);
	stuser_put(stuser);
	stdev->mrpc_busy = 0;

	mrpc_cmd_submit(stdev);
}

static void mrpc_event_work(struct work_struct *work)
{
	struct switchtec_dev *stdev;

	stdev = container_of(work, struct switchtec_dev, mrpc_work);

	dev_dbg(&stdev->dev, "%s\n", __func__);

	mutex_lock(&stdev->mrpc_mutex);
	cancel_delayed_work(&stdev->mrpc_timeout);
	mrpc_complete_cmd(stdev);
	mutex_unlock(&stdev->mrpc_mutex);
}

static void mrpc_timeout_work(struct work_struct *work)
{
	struct switchtec_dev *stdev;
	u32 status;

	stdev = container_of(work, struct switchtec_dev, mrpc_timeout.work);

	dev_dbg(&stdev->dev, "%s\n", __func__);

	mutex_lock(&stdev->mrpc_mutex);

	if (stdev->dma_mrpc)
		status = stdev->dma_mrpc->status;
	else
		status = ioread32(&stdev->mmio_mrpc->status);
	if (status == SWITCHTEC_MRPC_STATUS_INPROGRESS) {
		schedule_delayed_work(&stdev->mrpc_timeout,
				      msecs_to_jiffies(500));
		goto out;
	}

	mrpc_complete_cmd(stdev);
out:
	mutex_unlock(&stdev->mrpc_mutex);
}
#endif

#define DEVICE_ATTR_SYS_INFO_VAL(field) \
static ssize_t field ## _show(struct device *dev, \
	struct device_attribute *attr, char *buf) \
{ \
	struct switchtec_dev *stdev = to_stdev(dev); \
	return sprintf(buf, "0x%08x\n", \
			switchtec_readl(stdev, field)); \
} \
\
static DEVICE_ATTR_RO(field)

DEVICE_ATTR_SYS_INFO_VAL(fw_ver);
DEVICE_ATTR_SYS_INFO_VAL(intf_ver);
DEVICE_ATTR_SYS_INFO_VAL(hw_ver);
DEVICE_ATTR_SYS_INFO_VAL(cap);
DEVICE_ATTR_SYS_INFO_VAL(chan_num);
DEVICE_ATTR_SYS_INFO_VAL(glb_wrr);
DEVICE_ATTR_SYS_INFO_VAL(cpl_to);
DEVICE_ATTR_SYS_INFO_VAL(tag_lim);
DEVICE_ATTR_SYS_INFO_VAL(fw_int);
DEVICE_ATTR_SYS_INFO_VAL(stat);
DEVICE_ATTR_SYS_INFO_VAL(reset);
DEVICE_ATTR_SYS_INFO_VAL(intv);
DEVICE_ATTR_SYS_INFO_VAL(int_msk);

static ssize_t io_string_show(char *buf, void __iomem *attr, size_t len)
{
	int i;

	memcpy_fromio(buf, attr, len);
	buf[len] = '\n';
	buf[len + 1] = 0;

	for (i = len - 1; i > 0; i--) {
		if (buf[i] != ' ')
			break;
		buf[i] = '\n';
		buf[i + 1] = 0;
	}

	return strlen(buf);
}
#define DEVICE_ATTR_SYS_INFO_STR(field) \
static ssize_t field ## _show(struct device *dev, \
	struct device_attribute *attr, char *buf) \
{ \
	struct switchtec_dev *stdev = to_stdev(dev); \
	return io_string_show(buf, &__fw_reg(stdev)->field, \
			    sizeof(__fw_reg(stdev)->field)); \
} \
\
static DEVICE_ATTR_RO(field)

DEVICE_ATTR_SYS_INFO_STR(build_time);
#if 0
DEVICE_ATTR_SYS_INFO_STR(product_id);
DEVICE_ATTR_SYS_INFO_STR(product_revision);
DEVICE_ATTR_SYS_INFO_STR(component_vendor);
#endif

#if 0
static ssize_t device_version_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct switchtec_dev *stdev = to_stdev(dev);
	u32 fw_ver;
	u32 intf_ver;
	u32 hw_ver;

	fw_ver = switchtec_readl(stdev, fw_ver);
	intf_ver = switchtec_readl(stdev, intf_ver);
	hw_ver = switchtec_readl(stdev, hw_ver);

	return sprintf(buf, "fw_ver 0x%08x, intf_ver 0x%08x hw_ver 0x%08x\n",
			fw_ver, intf_ver, hw_ver);
}
static DEVICE_ATTR_RO(device_version);

static ssize_t device_capability_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct switchtec_dev *stdev = to_stdev(dev);
	u32 dma_cap;

	dma_cap = switchtec_readl(stdev, dma_cap);

	return sprintf(buf, "%08x\n", dma_cap);
}
static DEVICE_ATTR_RO(device_capability);
#endif
#if 0
static ssize_t component_id_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct switchtec_dev *stdev = to_stdev(dev);
	int id = ioread16(&stdev->mmio_sys_info->component_id);

	return sprintf(buf, "PM%04X\n", id);
}
static DEVICE_ATTR_RO(component_id);

static ssize_t component_revision_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct switchtec_dev *stdev = to_stdev(dev);
	int rev = ioread8(&stdev->mmio_sys_info->component_revision);

	return sprintf(buf, "%d\n", rev);
}
static DEVICE_ATTR_RO(component_revision);

static ssize_t partition_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct switchtec_dev *stdev = to_stdev(dev);

	return sprintf(buf, "%d\n", stdev->partition);
}
static DEVICE_ATTR_RO(partition);

static ssize_t partition_count_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct switchtec_dev *stdev = to_stdev(dev);

	return sprintf(buf, "%d\n", stdev->partition_count);
}
static DEVICE_ATTR_RO(partition_count);
#endif
static struct attribute *switchtec_device_attrs[] = {
	&dev_attr_fw_ver.attr,
	&dev_attr_intf_ver.attr,
	&dev_attr_hw_ver.attr,
	&dev_attr_cap.attr,
	&dev_attr_chan_num.attr,
	&dev_attr_glb_wrr.attr,
	&dev_attr_cpl_to.attr,
	&dev_attr_tag_lim.attr,
	&dev_attr_fw_int.attr,
	&dev_attr_stat.attr,
	&dev_attr_reset.attr,
	&dev_attr_intv.attr,
	&dev_attr_int_msk.attr,
	&dev_attr_build_time.attr,
#if 0
	&dev_attr_vendor_id.attr,
	&dev_attr_product_id.attr,
	&dev_attr_product_revision.attr,
	&dev_attr_component_vendor.attr,
	&dev_attr_component_id.attr,
	&dev_attr_component_revision.attr,
	&dev_attr_partition.attr,
	&dev_attr_partition_count.attr,
#endif
	NULL,
};

ATTRIBUTE_GROUPS(switchtec_device);

static int switchtec_dev_open(struct inode *inode, struct file *filp)
{
	struct switchtec_dev *stdev;
	struct switchtec_user *stuser;

	stdev = container_of(inode->i_cdev, struct switchtec_dev, cdev);

	stuser = stuser_create(stdev);
	if (IS_ERR(stuser))
		return PTR_ERR(stuser);

	filp->private_data = stuser;

	filp->f_pos = 0;
#if 0
	nonseekable_open(inode, filp);
#endif

	dev_dbg(&stdev->dev, "%s: %p\n", __func__, stuser);

	return 0;
}

static int switchtec_dev_release(struct inode *inode, struct file *filp)
{
	struct switchtec_user *stuser = filp->private_data;

	stuser_put(stuser);

	return 0;
}

static int dma_alloc_chan_resources(struct dma_chan *chan)
{
	struct switchtec_dma_chan *dma_chan = to_st_dma_chan(chan);
	struct dma_device *dma_dev = dma_chan->chan.device;
	u64 temp;


	dma_chan->cq_size = 100;
	dma_chan->cq = devm_kcalloc(dma_dev->dev, 100, 10, GFP_DMA | GFP_KERNEL);

	if (IS_ERR(dma_chan->cq))
		return PTR_ERR(dma_chan->cq);

	dma_chan->sq_size = 100;
	dma_chan->sq = devm_kcalloc(dma_dev->dev, 100, 10, GFP_DMA | GFP_KERNEL);

	if (IS_ERR(dma_chan->sq))
		goto err_free_cq;

	temp = (u64)dma_chan->cq;

	switchtec_ch_cfg_writel(dma_chan, cq_size, dma_chan->cq_size);
	switchtec_ch_cfg_writel(dma_chan, cq_base_lo, (u32)temp);
	switchtec_ch_cfg_writel(dma_chan, cq_base_hi, (u32)(temp>>32));

	temp = (u64)dma_chan->sq;

	switchtec_ch_cfg_writel(dma_chan, sq_size, dma_chan->sq_size);
	switchtec_ch_cfg_writel(dma_chan, sq_base_lo, (u32)temp);
	switchtec_ch_cfg_writel(dma_chan, sq_base_hi, (u32)(temp>>32));

	dma_chan->used = 1;

	return 0;
err_free_cq:
	devm_kfree(dma_dev->dev, dma_chan->cq);

	return PTR_ERR(dma_chan->sq);
#if 0
	struct dw_dma		*dw = to_dw_dma(chan->device);

	dev_vdbg(chan2dev(chan), "%s\n", __func__);

	/* ASSERT:  channel is idle */
	if (dma_readl(dw, CH_EN) & dwc->mask) {
		dev_dbg(chan2dev(chan), "DMA channel not idle?\n");
		return -EIO;
	}

	dma_cookie_init(chan);

	/*
	 * NOTE: some controllers may have additional features that we
	 * need to initialize here, like "scatter-gather" (which
	 * doesn't mean what you think it means), and status writeback.
	 */

	/*
	 * We need controller-specific data to set up slave transfers.
	 */
	if (chan->private && !dw_dma_filter(chan, chan->private)) {
		dev_warn(chan2dev(chan), "Wrong controller-specific data\n");
		return -EINVAL;
	}

	/* Enable controller here if needed */
	if (!dw->in_use)
		dw_dma_on(dw);
	dw->in_use |= dwc->mask;

	return 0;
#endif
}

static void dma_free_chan_resources(struct dma_chan *chan)
{
#if 0
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	struct dw_dma		*dw = to_dw_dma(chan->device);
	unsigned long		flags;
	LIST_HEAD(list);

	dev_dbg(chan2dev(chan), "%s: descs allocated=%u\n", __func__,
			dwc->descs_allocated);

	/* ASSERT:  channel is idle */
	BUG_ON(!list_empty(&dwc->active_list));
	BUG_ON(!list_empty(&dwc->queue));
	BUG_ON(dma_readl(to_dw_dma(chan->device), CH_EN) & dwc->mask);

	spin_lock_irqsave(&dwc->lock, flags);

	/* Clear custom channel configuration */
	memset(&dwc->dws, 0, sizeof(struct dw_dma_slave));

	clear_bit(DW_DMA_IS_INITIALIZED, &dwc->flags);

	/* Disable interrupts */
	channel_clear_bit(dw, MASK.XFER, dwc->mask);
	channel_clear_bit(dw, MASK.BLOCK, dwc->mask);
	channel_clear_bit(dw, MASK.ERROR, dwc->mask);

	spin_unlock_irqrestore(&dwc->lock, flags);

	/* Disable controller in case it was a last user */
	dw->in_use &= ~dwc->mask;
	if (!dw->in_use)
		dw_dma_off(dw);

	dev_vdbg(chan2dev(chan), "%s: done\n", __func__);
#endif
}

static struct dma_async_tx_descriptor *
dma_prep_dma_memcpy(struct dma_chan *chan, dma_addr_t dest, dma_addr_t src,
		size_t len, unsigned long flags)
{
#if 0
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	struct dw_dma		*dw = to_dw_dma(chan->device);
	struct dw_desc		*desc;
	struct dw_desc		*first;
	struct dw_desc		*prev;
	size_t			xfer_count;
	size_t			offset;
	u8			m_master = dwc->dws.m_master;
	unsigned int		src_width;
	unsigned int		dst_width;
	unsigned int		data_width = dw->pdata->data_width[m_master];
	u32			ctllo;
	u8			lms = DWC_LLP_LMS(m_master);

	dev_vdbg(chan2dev(chan),
			"%s: d%pad s%pad l0x%zx f0x%lx\n", __func__,
			&dest, &src, len, flags);

	if (unlikely(!len)) {
		dev_dbg(chan2dev(chan), "%s: length is zero!\n", __func__);
		return NULL;
	}

	dwc->direction = DMA_MEM_TO_MEM;

	src_width = dst_width = __ffs(data_width | src | dest | len);

	ctllo = DWC_DEFAULT_CTLLO(chan)
			| DWC_CTLL_DST_WIDTH(dst_width)
			| DWC_CTLL_SRC_WIDTH(src_width)
			| DWC_CTLL_DST_INC
			| DWC_CTLL_SRC_INC
			| DWC_CTLL_FC_M2M;
	prev = first = NULL;

	for (offset = 0; offset < len; offset += xfer_count) {
		desc = dwc_desc_get(dwc);
		if (!desc)
			goto err_desc_get;

		lli_write(desc, sar, src + offset);
		lli_write(desc, dar, dest + offset);
		lli_write(desc, ctllo, ctllo);
		lli_write(desc, ctlhi, bytes2block(dwc, len - offset, src_width, &xfer_count));
		desc->len = xfer_count;

		if (!first) {
			first = desc;
		} else {
			lli_write(prev, llp, desc->txd.phys | lms);
			list_add_tail(&desc->desc_node, &first->tx_list);
		}
		prev = desc;
	}

	if (flags & DMA_PREP_INTERRUPT)
		/* Trigger interrupt after last block */
		lli_set(prev, ctllo, DWC_CTLL_INT_EN);

	prev->lli.llp = 0;
	lli_clear(prev, ctllo, DWC_CTLL_LLP_D_EN | DWC_CTLL_LLP_S_EN);
	first->txd.flags = flags;
	first->total_len = len;

	return &first->txd;

err_desc_get:
	dwc_desc_put(dwc, first);
	return NULL;
#endif
	return NULL;
}

static struct dma_async_tx_descriptor *
dma_prep_slave_sg(struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_transfer_direction direction,
		unsigned long flags, void *context)
{
#if 0
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	struct dw_dma		*dw = to_dw_dma(chan->device);
	struct dma_slave_config	*sconfig = &dwc->dma_sconfig;
	struct dw_desc		*prev;
	struct dw_desc		*first;
	u32			ctllo;
	u8			m_master = dwc->dws.m_master;
	u8			lms = DWC_LLP_LMS(m_master);
	dma_addr_t		reg;
	unsigned int		reg_width;
	unsigned int		mem_width;
	unsigned int		data_width = dw->pdata->data_width[m_master];
	unsigned int		i;
	struct scatterlist	*sg;
	size_t			total_len = 0;

	dev_vdbg(chan2dev(chan), "%s\n", __func__);

	if (unlikely(!is_slave_direction(direction) || !sg_len))
		return NULL;

	dwc->direction = direction;

	prev = first = NULL;

	switch (direction) {
	case DMA_MEM_TO_DEV:
		reg_width = __ffs(sconfig->dst_addr_width);
		reg = sconfig->dst_addr;
		ctllo = (DWC_DEFAULT_CTLLO(chan)
				| DWC_CTLL_DST_WIDTH(reg_width)
				| DWC_CTLL_DST_FIX
				| DWC_CTLL_SRC_INC);

		ctllo |= sconfig->device_fc ? DWC_CTLL_FC(DW_DMA_FC_P_M2P) :
			DWC_CTLL_FC(DW_DMA_FC_D_M2P);

		for_each_sg(sgl, sg, sg_len, i) {
			struct dw_desc	*desc;
			u32		len, mem;
			size_t		dlen;

			mem = sg_dma_address(sg);
			len = sg_dma_len(sg);

			mem_width = __ffs(data_width | mem | len);

slave_sg_todev_fill_desc:
			desc = dwc_desc_get(dwc);
			if (!desc)
				goto err_desc_get;

			lli_write(desc, sar, mem);
			lli_write(desc, dar, reg);
			lli_write(desc, ctlhi, bytes2block(dwc, len, mem_width, &dlen));
			lli_write(desc, ctllo, ctllo | DWC_CTLL_SRC_WIDTH(mem_width));
			desc->len = dlen;

			if (!first) {
				first = desc;
			} else {
				lli_write(prev, llp, desc->txd.phys | lms);
				list_add_tail(&desc->desc_node, &first->tx_list);
			}
			prev = desc;

			mem += dlen;
			len -= dlen;
			total_len += dlen;

			if (len)
				goto slave_sg_todev_fill_desc;
		}
		break;
	case DMA_DEV_TO_MEM:
		reg_width = __ffs(sconfig->src_addr_width);
		reg = sconfig->src_addr;
		ctllo = (DWC_DEFAULT_CTLLO(chan)
				| DWC_CTLL_SRC_WIDTH(reg_width)
				| DWC_CTLL_DST_INC
				| DWC_CTLL_SRC_FIX);

		ctllo |= sconfig->device_fc ? DWC_CTLL_FC(DW_DMA_FC_P_P2M) :
			DWC_CTLL_FC(DW_DMA_FC_D_P2M);

		for_each_sg(sgl, sg, sg_len, i) {
			struct dw_desc	*desc;
			u32		len, mem;
			size_t		dlen;

			mem = sg_dma_address(sg);
			len = sg_dma_len(sg);

slave_sg_fromdev_fill_desc:
			desc = dwc_desc_get(dwc);
			if (!desc)
				goto err_desc_get;

			lli_write(desc, sar, reg);
			lli_write(desc, dar, mem);
			lli_write(desc, ctlhi, bytes2block(dwc, len, reg_width, &dlen));
			mem_width = __ffs(data_width | mem | dlen);
			lli_write(desc, ctllo, ctllo | DWC_CTLL_DST_WIDTH(mem_width));
			desc->len = dlen;

			if (!first) {
				first = desc;
			} else {
				lli_write(prev, llp, desc->txd.phys | lms);
				list_add_tail(&desc->desc_node, &first->tx_list);
			}
			prev = desc;

			mem += dlen;
			len -= dlen;
			total_len += dlen;

			if (len)
				goto slave_sg_fromdev_fill_desc;
		}
		break;
	default:
		return NULL;
	}

	if (flags & DMA_PREP_INTERRUPT)
		/* Trigger interrupt after last block */
		lli_set(prev, ctllo, DWC_CTLL_INT_EN);

	prev->lli.llp = 0;
	lli_clear(prev, ctllo, DWC_CTLL_LLP_D_EN | DWC_CTLL_LLP_S_EN);
	first->total_len = total_len;

	return &first->txd;

err_desc_get:
	dev_err(chan2dev(chan),
		"not enough descriptors available. Direction %d\n", direction);
	dwc_desc_put(dwc, first);
	return NULL;
#endif
	return NULL;
}

static int dma_config(struct dma_chan *chan, struct dma_slave_config *sconfig)
{
#if 0
	struct dw_dma_chan *dwc = to_dw_dma_chan(chan);
	struct dma_slave_config *sc = &dwc->dma_sconfig;
	struct dw_dma *dw = to_dw_dma(chan->device);
	/*
	 * Fix sconfig's burst size according to dw_dmac. We need to convert
	 * them as:
	 * 1 -> 0, 4 -> 1, 8 -> 2, 16 -> 3.
	 *
	 * NOTE: burst size 2 is not supported by DesignWare controller.
	 *       iDMA 32-bit supports it.
	 */
	u32 s = dw->pdata->is_idma32 ? 1 : 2;

	/* Check if chan will be configured for slave transfers */
	if (!is_slave_direction(sconfig->direction))
		return -EINVAL;

	memcpy(&dwc->dma_sconfig, sconfig, sizeof(*sconfig));
	dwc->direction = sconfig->direction;

	sc->src_maxburst = sc->src_maxburst > 1 ? fls(sc->src_maxburst) - s : 0;
	sc->dst_maxburst = sc->dst_maxburst > 1 ? fls(sc->dst_maxburst) - s : 0;
#endif
	return 0;
}

#if 0
static void dwc_chan_pause(struct dw_dma_chan *dwc, bool drain)
{
	struct dw_dma *dw = to_dw_dma(dwc->chan.device);
	unsigned int		count = 20;	/* timeout iterations */
	u32			cfglo;

	cfglo = channel_readl(dwc, CFG_LO);
	if (dw->pdata->is_idma32) {
		if (drain)
			cfglo |= IDMA32C_CFGL_CH_DRAIN;
		else
			cfglo &= ~IDMA32C_CFGL_CH_DRAIN;
	}
	channel_writel(dwc, CFG_LO, cfglo | DWC_CFGL_CH_SUSP);
	while (!(channel_readl(dwc, CFG_LO) & DWC_CFGL_FIFO_EMPTY) && count--)
		udelay(2);

	set_bit(DW_DMA_IS_PAUSED, &dwc->flags);
}
#endif

static int dma_pause(struct dma_chan *chan)
{
#if 0
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	unsigned long		flags;

	spin_lock_irqsave(&dwc->lock, flags);
	dwc_chan_pause(dwc, false);
	spin_unlock_irqrestore(&dwc->lock, flags);
#endif
	return 0;
}

#if 0
static inline void dwc_chan_resume(struct dw_dma_chan *dwc)
{
	u32 cfglo = channel_readl(dwc, CFG_LO);

	channel_writel(dwc, CFG_LO, cfglo & ~DWC_CFGL_CH_SUSP);

	clear_bit(DW_DMA_IS_PAUSED, &dwc->flags);
}
#endif

static int dma_resume(struct dma_chan *chan)
{
#if 0
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	unsigned long		flags;

	spin_lock_irqsave(&dwc->lock, flags);

	if (test_bit(DW_DMA_IS_PAUSED, &dwc->flags))
		dwc_chan_resume(dwc);

	spin_unlock_irqrestore(&dwc->lock, flags);
#endif
	return 0;
}

static int dma_terminate_all(struct dma_chan *chan)
{
#if 0
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	struct dw_dma		*dw = to_dw_dma(chan->device);
	struct dw_desc		*desc, *_desc;
	unsigned long		flags;
	LIST_HEAD(list);

	spin_lock_irqsave(&dwc->lock, flags);

	clear_bit(DW_DMA_IS_SOFT_LLP, &dwc->flags);

	dwc_chan_pause(dwc, true);

	dwc_chan_disable(dw, dwc);

	dwc_chan_resume(dwc);

	/* active_list entries will end up before queued entries */
	list_splice_init(&dwc->queue, &list);
	list_splice_init(&dwc->active_list, &list);

	static int ioctl_dma_chan_cfg(struct switchtec_dev *stdev,
				    struct switchtec_ioctl_dma_chan_cfg __user *uinfo)
	{
		struct switchtec_ioctl_dma_chan_cfg dma_ch_cfg = {0};
		struct switchtec_dma_chan *dma_chan;


		if (copy_from_user(&dma_ch_cfg, uinfo, sizeof(dma_ch_cfg)))
			return -EFAULT;

		if (stdev->dma.chancnt <= dma_ch_cfg.chan_id )
			return -EINVAL;

		dma_chan = &stdev->dma_ch[dma_ch_cfg.chan_id];

		return dma_alloc_chan_resources(dma_chan);
	}

	static long switchtec_dev_ioctl(struct file *filp, unsigned int cmd,
					unsigned long arg)
	{
		struct switchtec_user *stuser = filp->private_data;
		struct switchtec_dev *stdev = stuser->stdev;
		int rc;
		void __user *argp = (void __user *)arg;

		switch (cmd) {
		case SWITCHTEC_IOCTL_DMA_CHAN_CFG:
			rc = ioctl_dma_chan_cfg(stdev, argp);
			break;
		default:
			rc = -ENOTTY;
			break;
		}

		return rc;
	}

	static const struct file_operations switchtec_fops = {
		.owner = THIS_MODULE,
		.open = switchtec_dev_open,
		.release = switchtec_dev_release,
		.write = switchtec_dev_write,
		.read = switchtec_dev_read,
		.llseek = switchtec_dev_llseek,
		.compat_ioctl = switchtec_dev_ioctl,
	#if 0
		.poll = switchtec_dev_poll,
		.unlocked_ioctl = switchtec_dev_ioctl,
	#endif
	};

	spin_unlock_irqrestore(&dwc->lock, flags);

	/* Flush all pending and queued descriptors */
	list_for_each_entry_safe(desc, _desc, &list, desc_node)
		dwc_descriptor_complete(dwc, desc, false);
#endif
	return 0;
}

static enum dma_status
dma_tx_status(struct dma_chan *chan,
	      dma_cookie_t cookie,
	      struct dma_tx_state *txstate)
{
#if 0
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	enum dma_status		ret;

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return ret;

	dwc_scan_descriptors(to_dw_dma(chan->device), dwc);

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return ret;

	dma_set_residue(txstate, dwc_get_residue(dwc, cookie));

	if (test_bit(DW_DMA_IS_PAUSED, &dwc->flags) && ret == DMA_IN_PROGRESS)
		return DMA_PAUSED;

	return ret;
#endif
	return DMA_COMPLETE;
}

static void dma_issue_pending(struct dma_chan *chan)
{
#if 0
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	unsigned long		flags;

	spin_lock_irqsave(&dwc->lock, flags);
	if (list_empty(&dwc->active_list))
		dwc_dostart_first_queued(dwc);
	spin_unlock_irqrestore(&dwc->lock, flags);
#endif
}

#if 0
static int lock_mutex_and_test_alive(struct switchtec_dev *stdev)
{
	if (mutex_lock_interruptible(&stdev->mrpc_mutex))
		return -EINTR;

	if (!stdev->alive) {
		mutex_unlock(&stdev->mrpc_mutex);
		return -ENODEV;
	}

	return 0;
}
#endif
static ssize_t switchtec_dev_write(struct file *filp, const char __user *data,
				   size_t size, loff_t *off)
{
	struct switchtec_user *stuser = filp->private_data;
	struct switchtec_dev *stdev = stuser->stdev;
	size_t copy_sz;
	size_t first_len;
	size_t first_off;
	loff_t offset = filp->f_pos;
	size_t i;

	copy_sz = size;

	if ( offset & 0x3 )
	{
		/*not dw aligned*/
		first_len = 4 - (offset & 0x3);
		first_off = offset & 0x3;

		/* Read */
		buf_4k[0] = *((unsigned int*)(stdev->mmio + (offset & ~((loff_t)0x3)) ));

		if ( copy_sz < first_len )
		{
			first_len = copy_sz;
		}

		/* Modify */
		copy_from_user( ((void*)buf_4k) + first_off, data, first_len );

		/* Write back */
		*((unsigned int*)(stdev->mmio + (offset & ~((loff_t)0x3)) )) = buf_4k[0];

		copy_sz -= first_len;
		offset += first_len;
	}

	while( copy_sz )
	{
		if ( copy_sz > 4096 )
		{
			copy_from_user( (void*)buf_4k, data + size - copy_sz, 4096 );

			for( i = 0; i < 1024; i++ )
			{
				*(((unsigned int*)(stdev->mmio + offset) + i )) = buf_4k[i];
			}

			copy_sz -= 4096;
			offset += 4096;
		}
		else
		{
			if ( copy_sz%4 )
			{
				/* Read last dword*/
				buf_4k[ copy_sz/4 ] = *(((unsigned int*)(stdev->mmio + offset) + (copy_sz/4) ));
			}

			/* Modify */
			copy_from_user( (void*)buf_4k, data + size - copy_sz, copy_sz );

			/* Write back */
			for( i = 0; i < (((copy_sz + 0x3) & ~((loff_t)0x3))/4)  ; i++ )
			{
				*(((unsigned int*)(stdev->mmio + offset) + i )) = buf_4k[i];
			}


			break;
		}
	}

	*off += size;

	return size;
#if 0
	struct switchtec_user *stuser = filp->private_data;
	struct switchtec_dev *stdev = stuser->stdev;
	int rc;

	if (size < sizeof(stuser->cmd) ||
	    size > sizeof(stuser->cmd) + sizeof(stuser->data))
		return -EINVAL;

	stuser->data_len = size - sizeof(stuser->cmd);

	rc = lock_mutex_and_test_alive(stdev);
	if (rc)
		return rc;

	if (stuser->state != MRPC_IDLE) {
		rc = -EBADE;
		goto out;
	}

	rc = copy_from_user(&stuser->cmd, data, sizeof(stuser->cmd));
	if (rc) {
		rc = -EFAULT;
		goto out;
	}

	data += sizeof(stuser->cmd);
	rc = copy_from_user(&stuser->data, data, size - sizeof(stuser->cmd));
	if (rc) {
		rc = -EFAULT;
		goto out;
	}

	rc = mrpc_queue_cmd(stuser);

out:
	mutex_unlock(&stdev->mrpc_mutex);

	if (rc)
		return rc;

	return size;
#endif
}

static ssize_t switchtec_dev_read(struct file *filp, char __user *data,
				  size_t size, loff_t *off)
{
	struct switchtec_user *stuser = filp->private_data;
	struct switchtec_dev *stdev = stuser->stdev;
	size_t copy_sz;
	size_t first_len;
	size_t first_off;
	loff_t offset = filp->f_pos;
	size_t i;

	copy_sz = size;

	if ( offset & 0x3 )
	{
		/*not dw aligned*/
		first_len = 4 - (offset & 0x3);
		first_off = offset & 0x3;

		buf_4k[0] = *((unsigned int*)(stdev->mmio + (offset & ~((loff_t)0x3)) ));

		if ( copy_sz < first_len )
		{
			first_len = copy_sz;
		}

		copy_to_user( data, ((void*)buf_4k) + first_off, first_len );

		copy_sz -= first_len;
		offset += first_len;
	}

	while( copy_sz )
	{
		if ( copy_sz > 4096 )
		{
			for( i = 0; i < 1024; i++ )
			{
				buf_4k[i] = *(((unsigned int*)(stdev->mmio + offset) + i ));;
			}

			copy_to_user( data + size - copy_sz, (void*)buf_4k, 4096 );

			copy_sz -= 4096;
			offset += 4096;
		}
		else
		{
			for( i = 0; i < (((copy_sz + 0x3) & ~((loff_t)0x3))/4) ; i++ )
			{
				buf_4k[i] = *(((unsigned int*)(stdev->mmio + offset) + i ));;
			}

			copy_to_user( data + size - copy_sz, (void*)buf_4k, copy_sz);

			break;
		}
	}

	*off += size;

	return size;
#if 0
	struct switchtec_user *stuser = filp->private_data;
	struct switchtec_dev *stdev = stuser->stdev;
	int rc;

	if (size < sizeof(stuser->cmd) ||
	    size > sizeof(stuser->cmd) + sizeof(stuser->data))
		return -EINVAL;

	rc = lock_mutex_and_test_alive(stdev);
	if (rc)
		return rc;

	if (stuser->state == MRPC_IDLE) {
		mutex_unlock(&stdev->mrpc_mutex);
		return -EBADE;
	}

	stuser->read_len = size - sizeof(stuser->return_code);

	mutex_unlock(&stdev->mrpc_mutex);

	if (filp->f_flags & O_NONBLOCK) {
		if (!try_wait_for_completion(&stuser->comp))
			return -EAGAIN;
	} else {
		rc = wait_for_completion_interruptible(&stuser->comp);
		if (rc < 0)
			return rc;
	}

	rc = lock_mutex_and_test_alive(stdev);
	if (rc)
		return rc;

	if (stuser->state != MRPC_DONE) {
		mutex_unlock(&stdev->mrpc_mutex);
		return -EBADE;
	}

	rc = copy_to_user(data, &stuser->return_code,
			  sizeof(stuser->return_code));
	if (rc) {
		rc = -EFAULT;
		goto out;
	}

	data += sizeof(stuser->return_code);
	rc = copy_to_user(data, &stuser->data,
			  size - sizeof(stuser->return_code));
	if (rc) {
		rc = -EFAULT;
		goto out;
	}

	stuser_set_state(stuser, MRPC_IDLE);

out:
	mutex_unlock(&stdev->mrpc_mutex);

	if (stuser->status == SWITCHTEC_MRPC_STATUS_DONE)
		return size;
	else if (stuser->status == SWITCHTEC_MRPC_STATUS_INTERRUPTED)
		return -ENXIO;
	else
		return -EBADMSG;
#endif
}

static loff_t switchtec_dev_llseek(struct file *filp, loff_t off, int whence)
{
    loff_t newpos;

    switch(whence) {
      case 0: /* SEEK_SET */
        newpos = off;
        break;

      case 1: /* SEEK_CUR */
        newpos = filp->f_pos + off;
        break;

      case 2: /* SEEK_END */
        newpos = filp->f_pos + off;
        break;

      default: /* can't happen */
        return -EINVAL;
    }
    if (newpos < 0)
    	return -EINVAL;

	filp->f_pos = newpos;

    return newpos;
}
#if 0
static unsigned int switchtec_dev_poll(struct file *filp, poll_table *wait)
{
	struct switchtec_user *stuser = filp->private_data;
	struct switchtec_dev *stdev = stuser->stdev;
	int ret = 0;

	poll_wait(filp, &stuser->comp.wait, wait);
	poll_wait(filp, &stdev->event_wq, wait);

	if (lock_mutex_and_test_alive(stdev))
		return POLLIN | POLLRDHUP | POLLOUT | POLLERR | POLLHUP;

	mutex_unlock(&stdev->mrpc_mutex);

	if (try_wait_for_completion(&stuser->comp))
		ret |= POLLIN | POLLRDNORM;

	if (stuser->event_cnt != atomic_read(&stdev->event_cnt))
		ret |= POLLPRI | POLLRDBAND;

	return ret;
}
#endif
#if 0

static int ioctl_flash_info(struct switchtec_dev *stdev,
			    struct switchtec_ioctl_flash_info __user *uinfo)
{
	struct switchtec_ioctl_flash_info info = {0};
	struct flash_info_regs __iomem *fi = stdev->mmio_flash_info;

	info.flash_length = ioread32(&fi->flash_length);
	info.num_partitions = SWITCHTEC_IOCTL_NUM_PARTITIONS;

	if (copy_to_user(uinfo, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static void set_fw_info_part(struct switchtec_ioctl_flash_part_info *info,
			     struct partition_info __iomem *pi)
{
	info->address = ioread32(&pi->address);
	info->length = ioread32(&pi->length);
}

static int ioctl_flash_part_info(struct switchtec_dev *stdev,
	struct switchtec_ioctl_flash_part_info __user *uinfo)
{
	struct switchtec_ioctl_flash_part_info info = {0};
	struct flash_info_regs __iomem *fi = stdev->mmio_flash_info;
	struct sys_info_regs __iomem *si = stdev->mmio_sys_info;
	u32 active_addr = -1;

	if (copy_from_user(&info, uinfo, sizeof(info)))
		return -EFAULT;

	switch (info.flash_partition) {
	case SWITCHTEC_IOCTL_PART_CFG0:
		active_addr = ioread32(&fi->active_cfg);
		set_fw_info_part(&info, &fi->cfg0);
		if (ioread16(&si->cfg_running) == SWITCHTEC_CFG0_RUNNING)
			info.active |= SWITCHTEC_IOCTL_PART_RUNNING;
		break;
	case SWITCHTEC_IOCTL_PART_CFG1:
		active_addr = ioread32(&fi->active_cfg);
		set_fw_info_part(&info, &fi->cfg1);
		if (ioread16(&si->cfg_running) == SWITCHTEC_CFG1_RUNNING)
			info.active |= SWITCHTEC_IOCTL_PART_RUNNING;
		break;
	case SWITCHTEC_IOCTL_PART_IMG0:
		active_addr = ioread32(&fi->active_img);
		set_fw_info_part(&info, &fi->img0);
		if (ioread16(&si->img_running) == SWITCHTEC_IMG0_RUNNING)
			info.active |= SWITCHTEC_IOCTL_PART_RUNNING;
		break;
	case SWITCHTEC_IOCTL_PART_IMG1:
		active_addr = ioread32(&fi->active_img);
		set_fw_info_part(&info, &fi->img1);
		if (ioread16(&si->img_running) == SWITCHTEC_IMG1_RUNNING)
			info.active |= SWITCHTEC_IOCTL_PART_RUNNING;
		break;
	case SWITCHTEC_IOCTL_PART_NVLOG:
		set_fw_info_part(&info, &fi->nvlog);
		break;
	case SWITCHTEC_IOCTL_PART_VENDOR0:
		set_fw_info_part(&info, &fi->vendor[0]);
		break;
	case SWITCHTEC_IOCTL_PART_VENDOR1:
		set_fw_info_part(&info, &fi->vendor[1]);
		break;
	case SWITCHTEC_IOCTL_PART_VENDOR2:
		set_fw_info_part(&info, &fi->vendor[2]);
		break;
	case SWITCHTEC_IOCTL_PART_VENDOR3:
		set_fw_info_part(&info, &fi->vendor[3]);
		break;
	case SWITCHTEC_IOCTL_PART_VENDOR4:
		set_fw_info_part(&info, &fi->vendor[4]);
		break;
	case SWITCHTEC_IOCTL_PART_VENDOR5:
		set_fw_info_part(&info, &fi->vendor[5]);
		break;
	case SWITCHTEC_IOCTL_PART_VENDOR6:
		set_fw_info_part(&info, &fi->vendor[6]);
		break;
	case SWITCHTEC_IOCTL_PART_VENDOR7:
		set_fw_info_part(&info, &fi->vendor[7]);
		break;
	default:
		return -EINVAL;
	}

	if (info.address == active_addr)
		info.active |= SWITCHTEC_IOCTL_PART_ACTIVE;

	if (copy_to_user(uinfo, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static int ioctl_event_summary(struct switchtec_dev *stdev,
	struct switchtec_user *stuser,
	struct switchtec_ioctl_event_summary __user *usum)
{
	struct switchtec_ioctl_event_summary s = {0};
	int i;
	u32 reg;

	s.global = ioread32(&stdev->mmio_sw_event->global_summary);
	s.part_bitmap = ioread32(&stdev->mmio_sw_event->part_event_bitmap);
	s.local_part = ioread32(&stdev->mmio_part_cfg->part_event_summary);

	for (i = 0; i < stdev->partition_count; i++) {
		reg = ioread32(&stdev->mmio_part_cfg_all[i].part_event_summary);
		s.part[i] = reg;
	}

	for (i = 0; i < SWITCHTEC_MAX_PFF_CSR; i++) {
		reg = ioread16(&stdev->mmio_pff_csr[i].vendor_id);
		if (reg != MICROSEMI_VENDOR_ID)
			break;

		reg = ioread32(&stdev->mmio_pff_csr[i].pff_event_summary);
		s.pff[i] = reg;
	}

	if (copy_to_user(usum, &s, sizeof(s)))
		return -EFAULT;

	stuser->event_cnt = atomic_read(&stdev->event_cnt);

	return 0;
}

static u32 __iomem *global_ev_reg(struct switchtec_dev *stdev,
				  size_t offset, int index)
{
	return (void __iomem *)stdev->mmio_sw_event + offset;
}

static u32 __iomem *part_ev_reg(struct switchtec_dev *stdev,
				size_t offset, int index)
{
	return (void __iomem *)&stdev->mmio_part_cfg_all[index] + offset;
}

static u32 __iomem *pff_ev_reg(struct switchtec_dev *stdev,
			       size_t offset, int index)
{
	return (void __iomem *)&stdev->mmio_pff_csr[index] + offset;
}

#define EV_GLB(i, r)[i] = {offsetof(struct sw_event_regs, r), global_ev_reg}
#define EV_PAR(i, r)[i] = {offsetof(struct part_cfg_regs, r), part_ev_reg}
#define EV_PFF(i, r)[i] = {offsetof(struct pff_csr_regs, r), pff_ev_reg}

static const struct event_reg {
	size_t offset;
	u32 __iomem *(*map_reg)(struct switchtec_dev *stdev,
				size_t offset, int index);
} event_regs[] = {
	EV_GLB(SWITCHTEC_IOCTL_EVENT_STACK_ERROR, stack_error_event_hdr),
	EV_GLB(SWITCHTEC_IOCTL_EVENT_PPU_ERROR, ppu_error_event_hdr),
	EV_GLB(SWITCHTEC_IOCTL_EVENT_ISP_ERROR, isp_error_event_hdr),
	EV_GLB(SWITCHTEC_IOCTL_EVENT_SYS_RESET, sys_reset_event_hdr),
	EV_GLB(SWITCHTEC_IOCTL_EVENT_FW_EXC, fw_exception_hdr),
	EV_GLB(SWITCHTEC_IOCTL_EVENT_FW_NMI, fw_nmi_hdr),
	EV_GLB(SWITCHTEC_IOCTL_EVENT_FW_NON_FATAL, fw_non_fatal_hdr),
	EV_GLB(SWITCHTEC_IOCTL_EVENT_FW_FATAL, fw_fatal_hdr),
	EV_GLB(SWITCHTEC_IOCTL_EVENT_TWI_MRPC_COMP, twi_mrpc_comp_hdr),
	EV_GLB(SWITCHTEC_IOCTL_EVENT_TWI_MRPC_COMP_ASYNC,
	       twi_mrpc_comp_async_hdr),
	EV_GLB(SWITCHTEC_IOCTL_EVENT_CLI_MRPC_COMP, cli_mrpc_comp_hdr),
	EV_GLB(SWITCHTEC_IOCTL_EVENT_CLI_MRPC_COMP_ASYNC,
	       cli_mrpc_comp_async_hdr),
	EV_GLB(SWITCHTEC_IOCTL_EVENT_GPIO_INT, gpio_interrupt_hdr),
	EV_GLB(SWITCHTEC_IOCTL_EVENT_GFMS, gfms_event_hdr),
	EV_PAR(SWITCHTEC_IOCTL_EVENT_PART_RESET, part_reset_hdr),
	EV_PAR(SWITCHTEC_IOCTL_EVENT_MRPC_COMP, mrpc_comp_hdr),
	EV_PAR(SWITCHTEC_IOCTL_EVENT_MRPC_COMP_ASYNC, mrpc_comp_async_hdr),
	EV_PAR(SWITCHTEC_IOCTL_EVENT_DYN_PART_BIND_COMP, dyn_binding_hdr),
	EV_PAR(SWITCHTEC_IOCTL_EVENT_INTERCOMM_REQ_NOTIFY, intercomm_notify_hdr),
	EV_PFF(SWITCHTEC_IOCTL_EVENT_AER_IN_P2P, aer_in_p2p_hdr),
	EV_PFF(SWITCHTEC_IOCTL_EVENT_AER_IN_VEP, aer_in_vep_hdr),
	EV_PFF(SWITCHTEC_IOCTL_EVENT_DPC, dpc_hdr),
	EV_PFF(SWITCHTEC_IOCTL_EVENT_CTS, cts_hdr),
	EV_PFF(SWITCHTEC_IOCTL_EVENT_HOTPLUG, hotplug_hdr),
	EV_PFF(SWITCHTEC_IOCTL_EVENT_IER, ier_hdr),
	EV_PFF(SWITCHTEC_IOCTL_EVENT_THRESH, threshold_hdr),
	EV_PFF(SWITCHTEC_IOCTL_EVENT_POWER_MGMT, power_mgmt_hdr),
	EV_PFF(SWITCHTEC_IOCTL_EVENT_TLP_THROTTLING, tlp_throttling_hdr),
	EV_PFF(SWITCHTEC_IOCTL_EVENT_FORCE_SPEED, force_speed_hdr),
	EV_PFF(SWITCHTEC_IOCTL_EVENT_CREDIT_TIMEOUT, credit_timeout_hdr),
	EV_PFF(SWITCHTEC_IOCTL_EVENT_LINK_STATE, link_state_hdr),
};

static u32 __iomem *event_hdr_addr(struct switchtec_dev *stdev,
				   int event_id, int index)
{
	size_t off;

	if (event_id < 0 || event_id >= SWITCHTEC_IOCTL_MAX_EVENTS)
		return ERR_PTR(-EINVAL);

	off = event_regs[event_id].offset;

	if (event_regs[event_id].map_reg == part_ev_reg) {
		if (index == SWITCHTEC_IOCTL_EVENT_LOCAL_PART_IDX)
			index = stdev->partition;
		else if (index < 0 || index >= stdev->partition_count)
			return ERR_PTR(-EINVAL);
	} else if (event_regs[event_id].map_reg == pff_ev_reg) {
		if (index < 0 || index >= stdev->pff_csr_count)
			return ERR_PTR(-EINVAL);
	}

	return event_regs[event_id].map_reg(stdev, off, index);
}

static int event_ctl(struct switchtec_dev *stdev,
		     struct switchtec_ioctl_event_ctl *ctl)
{
	int i;
	u32 __iomem *reg;
	u32 hdr;

	reg = event_hdr_addr(stdev, ctl->event_id, ctl->index);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	hdr = ioread32(reg);
	for (i = 0; i < ARRAY_SIZE(ctl->data); i++)
		ctl->data[i] = ioread32(&reg[i + 1]);

	ctl->occurred = hdr & SWITCHTEC_EVENT_OCCURRED;
	ctl->count = (hdr >> 5) & 0xFF;

	if (!(ctl->flags & SWITCHTEC_IOCTL_EVENT_FLAG_CLEAR))
		hdr &= ~SWITCHTEC_EVENT_CLEAR;
	if (ctl->flags & SWITCHTEC_IOCTL_EVENT_FLAG_EN_POLL)
		hdr |= SWITCHTEC_EVENT_EN_IRQ;
	if (ctl->flags & SWITCHTEC_IOCTL_EVENT_FLAG_DIS_POLL)
		hdr &= ~SWITCHTEC_EVENT_EN_IRQ;
	if (ctl->flags & SWITCHTEC_IOCTL_EVENT_FLAG_EN_LOG)
		hdr |= SWITCHTEC_EVENT_EN_LOG;
	if (ctl->flags & SWITCHTEC_IOCTL_EVENT_FLAG_DIS_LOG)
		hdr &= ~SWITCHTEC_EVENT_EN_LOG;
	if (ctl->flags & SWITCHTEC_IOCTL_EVENT_FLAG_EN_CLI)
		hdr |= SWITCHTEC_EVENT_EN_CLI;
	if (ctl->flags & SWITCHTEC_IOCTL_EVENT_FLAG_DIS_CLI)
		hdr &= ~SWITCHTEC_EVENT_EN_CLI;
	if (ctl->flags & SWITCHTEC_IOCTL_EVENT_FLAG_EN_FATAL)
		hdr |= SWITCHTEC_EVENT_FATAL;
	if (ctl->flags & SWITCHTEC_IOCTL_EVENT_FLAG_DIS_FATAL)
		hdr &= ~SWITCHTEC_EVENT_FATAL;

	if (ctl->flags)
		iowrite32(hdr, reg);

	ctl->flags = 0;
	if (hdr & SWITCHTEC_EVENT_EN_IRQ)
		ctl->flags |= SWITCHTEC_IOCTL_EVENT_FLAG_EN_POLL;
	if (hdr & SWITCHTEC_EVENT_EN_LOG)
		ctl->flags |= SWITCHTEC_IOCTL_EVENT_FLAG_EN_LOG;
	if (hdr & SWITCHTEC_EVENT_EN_CLI)
		ctl->flags |= SWITCHTEC_IOCTL_EVENT_FLAG_EN_CLI;
	if (hdr & SWITCHTEC_EVENT_FATAL)
		ctl->flags |= SWITCHTEC_IOCTL_EVENT_FLAG_EN_FATAL;

	return 0;
}

static int ioctl_event_ctl(struct switchtec_dev *stdev,
	struct switchtec_ioctl_event_ctl __user *uctl)
{
	int ret;
	int nr_idxs;
	unsigned int event_flags;
	struct switchtec_ioctl_event_ctl ctl;

	if (copy_from_user(&ctl, uctl, sizeof(ctl)))
		return -EFAULT;

	if (ctl.event_id >= SWITCHTEC_IOCTL_MAX_EVENTS)
		return -EINVAL;

	if (ctl.flags & SWITCHTEC_IOCTL_EVENT_FLAG_UNUSED)
		return -EINVAL;

	if (ctl.index == SWITCHTEC_IOCTL_EVENT_IDX_ALL) {
		if (event_regs[ctl.event_id].map_reg == global_ev_reg)
			nr_idxs = 1;
		else if (event_regs[ctl.event_id].map_reg == part_ev_reg)
			nr_idxs = stdev->partition_count;
		else if (event_regs[ctl.event_id].map_reg == pff_ev_reg)
			nr_idxs = stdev->pff_csr_count;
		else
			return -EINVAL;

		event_flags = ctl.flags;
		for (ctl.index = 0; ctl.index < nr_idxs; ctl.index++) {
			ctl.flags = event_flags;
			ret = event_ctl(stdev, &ctl);
			if (ret < 0)
				return ret;
		}
	} else {
		ret = event_ctl(stdev, &ctl);
		if (ret < 0)
			return ret;
	}

	if (copy_to_user(uctl, &ctl, sizeof(ctl)))
		return -EFAULT;

	return 0;
}

static int ioctl_pff_to_port(struct switchtec_dev *stdev,
			     struct switchtec_ioctl_pff_port *up)
{
	int i, part;
	u32 reg;
	struct part_cfg_regs *pcfg;
	struct switchtec_ioctl_pff_port p;

	if (copy_from_user(&p, up, sizeof(p)))
		return -EFAULT;

	p.port = -1;
	for (part = 0; part < stdev->partition_count; part++) {
		pcfg = &stdev->mmio_part_cfg_all[part];
		p.partition = part;

		reg = ioread32(&pcfg->usp_pff_inst_id);
		if (reg == p.pff) {
			p.port = 0;
			break;
		}

		reg = ioread32(&pcfg->vep_pff_inst_id);
		if (reg == p.pff) {
			p.port = SWITCHTEC_IOCTL_PFF_VEP;
			break;
		}

		for (i = 0; i < ARRAY_SIZE(pcfg->dsp_pff_inst_id); i++) {
			reg = ioread32(&pcfg->dsp_pff_inst_id[i]);
			if (reg != p.pff)
				continue;

			p.port = i + 1;
			break;
		}

		if (p.port != -1)
			break;
	}

	if (copy_to_user(up, &p, sizeof(p)))
		return -EFAULT;

	return 0;
}

static int ioctl_port_to_pff(struct switchtec_dev *stdev,
			     struct switchtec_ioctl_pff_port *up)
{
	struct switchtec_ioctl_pff_port p;
	struct part_cfg_regs *pcfg;

	if (copy_from_user(&p, up, sizeof(p)))
		return -EFAULT;

	if (p.partition == SWITCHTEC_IOCTL_EVENT_LOCAL_PART_IDX)
		pcfg = stdev->mmio_part_cfg;
	else if (p.partition < stdev->partition_count)
		pcfg = &stdev->mmio_part_cfg_all[p.partition];
	else
		return -EINVAL;

	switch (p.port) {
	case 0:
		p.pff = ioread32(&pcfg->usp_pff_inst_id);
		break;
	case SWITCHTEC_IOCTL_PFF_VEP:
		p.pff = ioread32(&pcfg->vep_pff_inst_id);
		break;
	default:
		if (p.port > ARRAY_SIZE(pcfg->dsp_pff_inst_id))
			return -EINVAL;
		p.pff = ioread32(&pcfg->dsp_pff_inst_id[p.port - 1]);
		break;
	}

	if (copy_to_user(up, &p, sizeof(p)))
		return -EFAULT;

	return 0;
}

#endif


static int ioctl_dma_chan_cfg(struct switchtec_dev *stdev,
			    struct switchtec_ioctl_dma_chan_cfg __user *uinfo)
{
	struct switchtec_ioctl_dma_chan_cfg dma_ch_cfg = {0};
	struct switchtec_dma_chan *dma_chan;


	if (copy_from_user(&dma_ch_cfg, uinfo, sizeof(dma_ch_cfg)))
		return -EFAULT;

	if (stdev->dma.chancnt <= dma_ch_cfg.chan_id )
		return -EINVAL;

	dma_chan = &stdev->dma_ch[dma_ch_cfg.chan_id];

	return dma_alloc_chan_resources(&dma_chan->chan);
}

static long switchtec_dev_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	struct switchtec_user *stuser = filp->private_data;
	struct switchtec_dev *stdev = stuser->stdev;
	int rc;
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case SWITCHTEC_IOCTL_DMA_CHAN_CFG:
		rc = ioctl_dma_chan_cfg(stdev, argp);
		break;
	default:
		rc = -ENOTTY;
		break;
	}

	return rc;
}

static const struct file_operations switchtec_fops = {
	.owner = THIS_MODULE,
	.open = switchtec_dev_open,
	.release = switchtec_dev_release,
	.write = switchtec_dev_write,
	.read = switchtec_dev_read,
	.llseek = switchtec_dev_llseek,
	.compat_ioctl = switchtec_dev_ioctl,
#if 0
	.poll = switchtec_dev_poll,
	.unlocked_ioctl = switchtec_dev_ioctl,
#endif
};

#if 0
static void link_event_work(struct work_struct *work)
{
	struct switchtec_dev *stdev;

	stdev = container_of(work, struct switchtec_dev, link_event_work);

	if (stdev->link_notifier)
		stdev->link_notifier(stdev);
}


static void check_link_state_events(struct switchtec_dev *stdev)
{
	int idx;
	u32 reg;
	int count;
	int occurred = 0;

	for (idx = 0; idx < stdev->pff_csr_count; idx++) {
		reg = ioread32(&stdev->mmio_pff_csr[idx].link_state_hdr);
		dev_dbg(&stdev->dev, "link_state: %d->%08x\n", idx, reg);
		count = (reg >> 5) & 0xFF;

		if (count != stdev->link_event_count[idx]) {
			occurred = 1;
			stdev->link_event_count[idx] = count;
		}
	}

	if (occurred)
		schedule_work(&stdev->link_event_work);
}

static void enable_link_state_events(struct switchtec_dev *stdev)
{
	int idx;

	for (idx = 0; idx < stdev->pff_csr_count; idx++) {
		iowrite32(SWITCHTEC_EVENT_CLEAR |
			  SWITCHTEC_EVENT_EN_IRQ,
			  &stdev->mmio_pff_csr[idx].link_state_hdr);
	}
}

static void enable_dma_mrpc(struct switchtec_dev *stdev)
{
	writeq(stdev->dma_mrpc_dma_addr, &stdev->mmio_mrpc->dma_addr);
	iowrite32(SWITCHTEC_DMA_MRPC_EN, &stdev->mmio_mrpc->dma_en);
}
#endif
static void stdev_release(struct device *dev)
{
	struct switchtec_dev *stdev = to_stdev(dev);
#if 0
	if (stdev->dma_mrpc){
		iowrite32(0, &stdev->mmio_mrpc->dma_en);
		writeq(0, &stdev->mmio_mrpc->dma_addr);
		dma_free_coherent(&stdev->pdev->dev, sizeof(*stdev->dma_mrpc),
				stdev->dma_mrpc, stdev->dma_mrpc_dma_addr);
	}
#endif
	kfree(stdev);
}

static void stdev_kill(struct switchtec_dev *stdev)
{
#if 0
	struct switchtec_user *stuser, *tmpuser;
#endif

	pci_clear_master(stdev->pdev);

#if 0
	cancel_delayed_work_sync(&stdev->mrpc_timeout);

	/* Mark the hardware as unavailable and complete all completions */
	mutex_lock(&stdev->mrpc_mutex);
	stdev->alive = false;

	/* Wake up and kill any users waiting on an MRPC request */
	list_for_each_entry_safe(stuser, tmpuser, &stdev->mrpc_queue, list) {
		complete_all(&stuser->comp);
		list_del_init(&stuser->list);
		stuser_put(stuser);
	}

	mutex_unlock(&stdev->mrpc_mutex);

	/* Wake up any users waiting on event_wq */
	wake_up_interruptible(&stdev->event_wq);
#endif
}

static struct switchtec_dev *stdev_create(struct pci_dev *pdev)
{
	struct switchtec_dev *stdev;
	int minor;
	struct device *dev;
	struct cdev *cdev;
	int rc;

	stdev = kzalloc_node(sizeof(*stdev), GFP_KERNEL,
			     dev_to_node(&pdev->dev));
	if (!stdev)
		return ERR_PTR(-ENOMEM);

	stdev->pdev = pdev;

	dev = &stdev->dev;
	device_initialize(dev);
	dev->class = switchtec_class;
	dev->parent = &pdev->dev;
	dev->groups = switchtec_device_groups;
	dev->release = stdev_release;

	minor = ida_simple_get(&switchtec_minor_ida, 0, 0,
				   GFP_KERNEL);
	if (minor < 0) {
		rc = minor;
		goto err_put;
	}

	dev->devt = MKDEV(MAJOR(switchtec_devt), minor);
	dev_set_name(dev, "switchtec%d", minor);

	cdev = &stdev->cdev;
	cdev_init(cdev, &switchtec_fops);
	cdev->owner = THIS_MODULE;
	cdev->kobj.parent = &dev->kobj;

	dev_info(&stdev->dev, "switchtec%d char dev\n",
			 minor);

	return stdev;
#if 0
err_dma_register:
	cdev_del(&stdev->cdev);
#endif
err_put:
	put_device(&stdev->dev);
	return ERR_PTR(rc);
#if 0
	stdev->alive = true;
	stdev->pdev = pdev;
	INIT_LIST_HEAD(&stdev->mrpc_queue);
	mutex_init(&stdev->mrpc_mutex);
	stdev->mrpc_busy = 0;
	INIT_WORK(&stdev->mrpc_work, mrpc_event_work);
	INIT_DELAYED_WORK(&stdev->mrpc_timeout, mrpc_timeout_work);
	INIT_WORK(&stdev->link_event_work, link_event_work);
	init_waitqueue_head(&stdev->event_wq);
	atomic_set(&stdev->event_cnt, 0);

	dev = &stdev->dev;
	device_initialize(dev);
	dev->class = switchtec_class;
	dev->parent = &pdev->dev;
	dev->groups = switchtec_device_groups;
	dev->release = stdev_release;

	minor = ida_simple_get(&switchtec_minor_ida, 0, 0,
			       GFP_KERNEL);
	if (minor < 0) {
		rc = minor;
		goto err_put;
	}

	dev->devt = MKDEV(MAJOR(switchtec_devt), minor);
	dev_set_name(dev, "switchtec%d", minor);

	cdev = &stdev->cdev;
	cdev_init(cdev, &switchtec_fops);
	cdev->owner = THIS_MODULE;
	cdev->kobj.parent = &dev->kobj;

	return stdev;

err_put:
	put_device(&stdev->dev);
	return ERR_PTR(rc);
#endif
}
#if 0

static int mask_event(struct switchtec_dev *stdev, int eid, int idx)
{
	size_t off = event_regs[eid].offset;
	u32 __iomem *hdr_reg;
	u32 hdr;

	hdr_reg = event_regs[eid].map_reg(stdev, off, idx);
	hdr = ioread32(hdr_reg);

	if (!(hdr & SWITCHTEC_EVENT_OCCURRED && hdr & SWITCHTEC_EVENT_EN_IRQ))
		return 0;

	if (eid == SWITCHTEC_IOCTL_EVENT_LINK_STATE)
		return 0;

	dev_dbg(&stdev->dev, "%s: %d %d %x\n", __func__, eid, idx, hdr);
	hdr &= ~(SWITCHTEC_EVENT_EN_IRQ | SWITCHTEC_EVENT_OCCURRED);
	iowrite32(hdr, hdr_reg);

	return 1;
}

static int mask_all_events(struct switchtec_dev *stdev, int eid)
{
	int idx;
	int count = 0;

	if (event_regs[eid].map_reg == part_ev_reg) {
		for (idx = 0; idx < stdev->partition_count; idx++)
			count += mask_event(stdev, eid, idx);
	} else if (event_regs[eid].map_reg == pff_ev_reg) {
		for (idx = 0; idx < stdev->pff_csr_count; idx++) {
			if (!stdev->pff_local[idx])
				continue;

			count += mask_event(stdev, eid, idx);
		}
	} else {
		count += mask_event(stdev, eid, 0);
	}

	return count;
}
#endif

static irqreturn_t switchtec_isr(int irq, void *dev)
{
	struct switchtec_dev *stdev = dev;

	dev_dbg(&stdev->dev, "%s: %d irq\n", __func__,
			irq);
#if 0
	struct switchtec_dev *stdev = dev;
	u32 reg;
	irqreturn_t ret = IRQ_NONE;
	int eid, event_count = 0;

	reg = ioread32(&stdev->mmio_part_cfg->mrpc_comp_hdr);
	if (reg & SWITCHTEC_EVENT_OCCURRED) {
		dev_dbg(&stdev->dev, "%s: mrpc comp\n", __func__);
		ret = IRQ_HANDLED;
		schedule_work(&stdev->mrpc_work);
		iowrite32(reg, &stdev->mmio_part_cfg->mrpc_comp_hdr);
	}

	check_link_state_events(stdev);

	for (eid = 0; eid < SWITCHTEC_IOCTL_MAX_EVENTS; eid++)
		event_count += mask_all_events(stdev, eid);

	if (event_count) {
		atomic_inc(&stdev->event_cnt);
		wake_up_interruptible(&stdev->event_wq);
		dev_dbg(&stdev->dev, "%s: %d events\n", __func__,
			event_count);
		return IRQ_HANDLED;
	}

	return ret;
#endif
	return IRQ_HANDLED;
}

#if 0
static irqreturn_t switchtec_dma_mrpc_isr(int irq, void *dev)
{
	struct switchtec_dev *stdev = dev;
	irqreturn_t ret = IRQ_NONE;

	iowrite32(SWITCHTEC_EVENT_CLEAR |
		  SWITCHTEC_EVENT_EN_IRQ,
		  &stdev->mmio_part_cfg->mrpc_comp_hdr);
	schedule_work(&stdev->mrpc_work);

	ret = IRQ_HANDLED;
	return ret;
}
#endif

static int switchtec_init_msix_isr(struct switchtec_dev *stdev)
{
	struct pci_dev *pdev = stdev->pdev;
	int rc, i, msix_count;

	for (i = 0; i < ARRAY_SIZE(stdev->msix); ++i)
		stdev->msix[i].entry = i;

	msix_count = pci_enable_msix_range(pdev, stdev->msix, 1,
									   ARRAY_SIZE(stdev->msix));
	if (msix_count < 0)
		return msix_count;

	for (i = 0; i < ARRAY_SIZE(stdev->msix); ++i )
	{
		rc = devm_request_irq(&pdev->dev, stdev->msix[i].vector,
					switchtec_isr, 0,
					KBUILD_MODNAME, stdev);

		if (rc)
			return rc;
	}

	return 0;

#if 0
        stdev->event_irq = ioread32(&stdev->mmio_part_cfg->vep_vector_number);
        if (stdev->event_irq < 0 || stdev->event_irq >= msix_count) {
                rc = -EFAULT;
                goto err_msix_request;
        }

        stdev->event_irq = stdev->msix[stdev->event_irq].vector;
        dev_dbg(&stdev->dev, "Using msix interrupts: event_irq=%d\n",
                stdev->event_irq);

	if (!stdev->dma_mrpc)
		return 0;

	stdev->dma_mrpc_irq = ioread32(&stdev->mmio_mrpc->dma_vector);
        if (stdev->dma_mrpc_irq < 0 || stdev->dma_mrpc_irq >= msix_count) {
                rc = -EFAULT;
                goto err_msix_request;
        }

        stdev->dma_mrpc_irq = stdev->msix[stdev->dma_mrpc_irq].vector;
        dev_dbg(&stdev->dev, "Using msix interrupts: dma_mrpc_irq=%d\n",
                stdev->dma_mrpc_irq);

	return 0;

err_msix_request:
        pci_disable_msix(pdev);
        return rc;
#endif
}

static int switchtec_init_msi_isr(struct switchtec_dev *stdev)
{
	int rc;
	struct pci_dev *pdev = stdev->pdev;
	int i;

	/* Try to set up msi irq */
	rc = pci_enable_msi_range(pdev, 1, 4);
	if (rc < 0)
		return rc;

	for (i = 0; i < ARRAY_SIZE(stdev->msix); ++i )
	{
		stdev->msix[i].vector = pdev->irq + i;

		rc = devm_request_irq(&pdev->dev, stdev->msix[i].vector,
					switchtec_isr, 0,
					KBUILD_MODNAME, stdev);

		if (rc)
			return rc;
	}

	return 0;
#if 0
	stdev->event_irq = ioread32(&stdev->mmio_part_cfg->vep_vector_number);
	if (stdev->event_irq < 0 || stdev->event_irq >= 4) {
		rc = -EFAULT;
		goto err_msi_request;
	}

	for (i = 0; i < ARRAY_SIZE(stdev->msix); ++i)
		stdev->msix[i].vector = pdev->irq + i;

        stdev->event_irq = pdev->irq + stdev->event_irq;
        dev_dbg(&stdev->dev, "Using msi interrupts: event_irq=%d\n",
                stdev->event_irq);

	if (!stdev->dma_mrpc)
		return 0;

	stdev->dma_mrpc_irq = ioread32(&stdev->mmio_mrpc->dma_vector);
        if (stdev->dma_mrpc_irq < 0 || stdev->dma_mrpc_irq >= 4) {
                rc = -EFAULT;
                goto err_msi_request;
        }

	stdev->dma_mrpc_irq = pdev->irq + stdev->dma_mrpc_irq;
        dev_dbg(&stdev->dev, "Using msi interrupts: dma_mrpc_irq=%d\n",
                stdev->dma_mrpc_irq);

	return 0;

err_msi_request:
        pci_disable_msi(pdev);
        return rc;
#endif
}


static int switchtec_init_isr(struct switchtec_dev *stdev)
{
	int rc;

	rc = switchtec_init_msix_isr(stdev);
	if (rc)
		rc = switchtec_init_msi_isr(stdev);

	if (rc)
		return rc;

#if 0
	rc = devm_request_irq(&stdev->pdev->dev, stdev->event_irq,
				switchtec_isr, 0,
				KBUILD_MODNAME, stdev);

	if (!stdev->dma_mrpc)
		return rc;

	rc = devm_request_irq(&stdev->pdev->dev, stdev->dma_mrpc_irq,
				switchtec_dma_mrpc_isr, 0,
				KBUILD_MODNAME, stdev);
#endif

	return rc;
}
#if 0
static void init_pff(struct switchtec_dev *stdev)
{
	int i;
	u32 reg;
	struct part_cfg_regs *pcfg = stdev->mmio_part_cfg;

	for (i = 0; i < SWITCHTEC_MAX_PFF_CSR; i++) {
		reg = ioread16(&stdev->mmio_pff_csr[i].vendor_id);
		if (reg != MICROSEMI_VENDOR_ID)
			break;
	}

	stdev->pff_csr_count = i;

	reg = ioread32(&pcfg->usp_pff_inst_id);
	if (reg < SWITCHTEC_MAX_PFF_CSR)
		stdev->pff_local[reg] = 1;

	reg = ioread32(&pcfg->vep_pff_inst_id);
	if (reg < SWITCHTEC_MAX_PFF_CSR)
		stdev->pff_local[reg] = 1;

	for (i = 0; i < ARRAY_SIZE(pcfg->dsp_pff_inst_id); i++) {
		reg = ioread32(&pcfg->dsp_pff_inst_id[i]);
		if (reg < SWITCHTEC_MAX_PFF_CSR)
			stdev->pff_local[reg] = 1;
	}
}
#endif

static int switchtec_init_dma(struct switchtec_dev *stdev)
{
	struct dma_device *dma;
	unsigned long chan_num;
	unsigned long i;
	int rc;

	/* Setup DMA engine device */
	dma = &stdev->dma;
	dma->dev = &stdev->dev;

	/* Set capabilities */
	dma_cap_set(DMA_MEMCPY, dma->cap_mask);

	dma->device_alloc_chan_resources = dma_alloc_chan_resources;
	dma->device_free_chan_resources = dma_free_chan_resources;

	dma->device_prep_dma_memcpy = dma_prep_dma_memcpy;
	dma->device_prep_slave_sg = dma_prep_slave_sg;

	dma->device_config = dma_config;
	dma->device_pause = dma_pause;
	dma->device_resume = dma_resume;
	dma->device_terminate_all = dma_terminate_all;

	dma->device_tx_status = dma_tx_status;
	dma->device_issue_pending = dma_issue_pending;

	/* DMA capabilities */
	dma->src_addr_widths = SWITCHTEC_DMA_BUSWIDTHS;
	dma->dst_addr_widths = SWITCHTEC_DMA_BUSWIDTHS;
	dma->directions = /*BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV) |*/
			     BIT(DMA_MEM_TO_MEM);
	dma->residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;

	/* Setup DMA channel */
	chan_num = switchtec_readl(stdev, chan_num);

	dma->chancnt = chan_num;
	stdev->dma_ch = devm_kcalloc(&stdev->dev,
			chan_num, sizeof(struct switchtec_dma_chan),
			GFP_KERNEL);
	if (!stdev->dma_ch)
		return -ENOMEM;

	INIT_LIST_HEAD(&dma->channels);
	for (i = 0; i < chan_num; i++) {
		struct switchtec_dma_chan *dma_ch = &stdev->dma_ch[i];

		dma_ch->chan.device = dma;
		dma_ch->chan.cookie = DMA_MIN_COOKIE;
		dma_ch->chan.completed_cookie = DMA_MIN_COOKIE;
		list_add_tail(&dma_ch->chan.device_node,
				&dma->channels);

		/* TODO: remap the fw and hw register in the dma chan allocation */
		dma_ch->mmio_fw_ch = stdev->mmio + \
				SWITCHTEC_DMA_HW_CHANNEL_REGS_OFF +
				0x1000 * i;
		dma_ch->mmio_hw_ch = stdev->mmio + \
				SWITCHTEC_DMA_FW_CHANNEL_REGS_OFF +
				sizeof(struct dma_fw_ch_regs) * i;
	}

#if 0
	rc = dma_async_device_register(dma);
	if (rc)
		goto err_dma_register;
#endif

	dev_info(&stdev->dev, "switchtec dma dev\n");

	return 0;

err_dma_register:

	dev_err(&stdev->dev, "failed register dma dev\n");
	return rc;
}

static int switchtec_init_pci(struct switchtec_dev *stdev,
			      struct pci_dev *pdev)
{
	int rc;
	void __iomem *map;
	unsigned long res_start, res_len;

	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	rc = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64));
	if (rc)
		return rc;

	pci_set_master(pdev);

	res_start = pci_resource_start(pdev, 0);
	res_len = pci_resource_len(pdev, 0);

	if (!devm_request_mem_region(&pdev->dev, res_start,
				     res_len, KBUILD_MODNAME))
		return -EBUSY;

	map =  devm_ioremap(&pdev->dev,
			  res_start,
			  res_len);
	if (!map)
		return -ENOMEM;

	stdev->mmio = map;

#if 0
	stdev->mmio_mrpc = devm_ioremap_wc(&pdev->dev, res_start,
					   SWITCHTEC_GAS_TOP_CFG_OFFSET);
	if (!stdev->mmio_mrpc)
		return -ENOMEM;

	map = devm_ioremap(&pdev->dev,
			   res_start + SWITCHTEC_GAS_TOP_CFG_OFFSET,
			   res_len - SWITCHTEC_GAS_TOP_CFG_OFFSET);
	if (!map)
		return -ENOMEM;

	stdev->mmio = map - SWITCHTEC_GAS_TOP_CFG_OFFSET;
	stdev->mmio_sw_event = stdev->mmio + SWITCHTEC_GAS_SW_EVENT_OFFSET;
	stdev->mmio_sys_info = stdev->mmio + SWITCHTEC_GAS_SYS_INFO_OFFSET;
	stdev->mmio_flash_info = stdev->mmio + SWITCHTEC_GAS_FLASH_INFO_OFFSET;
	stdev->mmio_ntb = stdev->mmio + SWITCHTEC_GAS_NTB_OFFSET;
	stdev->partition = ioread8(&stdev->mmio_sys_info->partition_id);
	stdev->partition_count = ioread8(&stdev->mmio_ntb->partition_count);
	stdev->mmio_part_cfg_all = stdev->mmio + SWITCHTEC_GAS_PART_CFG_OFFSET;
	stdev->mmio_part_cfg = &stdev->mmio_part_cfg_all[stdev->partition];
	stdev->mmio_pff_csr = stdev->mmio + SWITCHTEC_GAS_PFF_CSR_OFFSET;

	if (stdev->partition_count < 1)
		stdev->partition_count = 1;

#endif

#if 0
	init_pff(stdev);
#endif

	pci_set_drvdata(pdev, stdev);

#if 0
	if (!use_dma_mrpc)
		return 0;

	if(!(ioread32(&stdev->mmio_mrpc->dma_ver)? true : false))
		return 0;

	stdev->dma_mrpc = dma_zalloc_coherent(&stdev->pdev->dev, sizeof(*stdev->dma_mrpc),
						&stdev->dma_mrpc_dma_addr, GFP_KERNEL);
	if (stdev->dma_mrpc == NULL)
		return -ENOMEM;
#endif

	return 0;
}

static int switchtec_pci_probe(struct pci_dev *pdev,
			       const struct pci_device_id *id)
{
	struct switchtec_dev *stdev;
	int rc;

#if 0
	if (pdev->class == MICROSEMI_NTB_CLASSCODE)
		request_module_nowait("ntb_hw_switchtec");
#endif

	stdev = stdev_create(pdev);
	if (IS_ERR(stdev))
		return PTR_ERR(stdev);

	rc = switchtec_init_pci(stdev, pdev);
	if (rc)
		goto err_put;

	rc = switchtec_init_isr(stdev);
	if (rc) {
		dev_err(&stdev->dev, "failed to init isr.\n");
		goto err_put;
	}

#if 0
	iowrite32(SWITCHTEC_EVENT_CLEAR |
		  SWITCHTEC_EVENT_EN_IRQ,
		  &stdev->mmio_part_cfg->mrpc_comp_hdr);
	enable_link_state_events(stdev);

	if (stdev->dma_mrpc)
		enable_dma_mrpc(stdev);

#endif

	rc = switchtec_init_dma(stdev);
	if (rc)
		goto err_put;

	rc = cdev_add(&stdev->cdev, stdev->dev.devt, 1);
	if (rc)
		goto err_put;

	rc = device_add(&stdev->dev);
	if (rc)
		goto err_devadd;

	dev_info(&stdev->dev, "DMA device registered.\n");

	return 0;

err_devadd:
	cdev_del(&stdev->cdev);
	stdev_kill(stdev);
err_put:
	ida_simple_remove(&switchtec_minor_ida, MINOR(stdev->dev.devt));
	put_device(&stdev->dev);
	return rc;
}

static void switchtec_pci_remove(struct pci_dev *pdev)
{
	struct switchtec_dev *stdev = pci_get_drvdata(pdev);

	pci_set_drvdata(pdev, NULL);

	device_del(&stdev->dev);
	cdev_del(&stdev->cdev);
	ida_simple_remove(&switchtec_minor_ida, MINOR(stdev->dev.devt));
	dev_info(&stdev->dev, "unregistered.\n");
	stdev_kill(stdev);
	put_device(&stdev->dev);
}

#define SWITCHTEC_PCI_DEVICE(device_id) \
	{ \
		.vendor     = MICROSEMI_VENDOR_ID, \
		.device     = device_id, \
		.subvendor  = PCI_ANY_ID, \
		.subdevice  = PCI_ANY_ID, \
		.class      = 0x058000, \
		.class_mask = 0xFFFFFFFF, \
	}

static const struct pci_device_id switchtec_pci_tbl[] = {
	SWITCHTEC_PCI_DEVICE(0xbeef),
	{0}
};
MODULE_DEVICE_TABLE(pci, switchtec_pci_tbl);

static struct pci_driver switchtec_pci_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= switchtec_pci_tbl,
	.probe		= switchtec_pci_probe,
	.remove		= switchtec_pci_remove,
};

static int __init switchtec_init(void)
{
	int rc;

	rc = alloc_chrdev_region(&switchtec_devt, 0, max_devices,
				 "switchtec");
	if (rc)
		return rc;

	switchtec_class = class_create(THIS_MODULE, "switchtec");
	if (IS_ERR(switchtec_class)) {
		rc = PTR_ERR(switchtec_class);
		goto err_create_class;
	}

	rc = pci_register_driver(&switchtec_pci_driver);
	if (rc)
		goto err_pci_register;

	pr_info(KBUILD_MODNAME ": loaded.\n");

	return 0;

err_pci_register:
	class_destroy(switchtec_class);

err_create_class:
	unregister_chrdev_region(switchtec_devt, max_devices);

	return rc;
}
module_init(switchtec_init);

static void __exit switchtec_exit(void)
{
	pci_unregister_driver(&switchtec_pci_driver);
	class_destroy(switchtec_class);
	unregister_chrdev_region(switchtec_devt, max_devices);
	ida_destroy(&switchtec_minor_ida);

	pr_info(KBUILD_MODNAME ": unloaded.\n");
}
module_exit(switchtec_exit);
