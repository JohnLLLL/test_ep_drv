#ifndef _MPI_DRV_H_
#define _MPI_DRV_H_

/* linux header files */

#include <linux/version.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
    #include <linux/config.h>
#else
    #include <generated/autoconf.h>
#endif
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/bio.h>
#include <linux/blkpg.h>
#include <linux/timer.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/hdreg.h>
#include <linux/spinlock.h>
#include <linux/compat.h>
#include <linux/version.h>
#include <linux/dma-mapping.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/completion.h>
#include <linux/cdev.h>


/* mpi tool header files */


#endif
